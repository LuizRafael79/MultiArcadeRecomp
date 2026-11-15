/*
 * audio.c — the Z80 sound CPU, its latches to the 68000, and the YM2610.
 *
 * Z80 memory: $0000-$7FFF fixed M1 ROM (or the BIOS SM1 while the system
 * latch selects the board ROMs), $8000/$C000/$E000/$F000 four switchable
 * windows of 16/8/4/2 KB, $F800-$FFFF 2 KB RAM. Reading ports $08-$0B
 * selects a window's bank from the upper byte of the port address.
 *
 * Ports: $00 (r) command from the 68k, $04-$07 YM2610, $08/$18 (w) NMI
 * enable/disable, $0C (w) reply to the 68k.
 *
 * A 68k command write latches the byte and raises NMI (when enabled); that
 * is how the BIOS and every sound driver talk. The Z80 runs lazily: it is
 * caught up to the 68k's clock (NG_MAIN_CPU_CLOCK / NG_AUDIO_CPU_CLOCK)
 * whenever the 68k touches the sound latches, and at the end of each frame.
 *
 * The YM2610 here models the timers, status and IRQ line that drivers
 * sync to; ym2610.cpp makes the sound. Samples are generated up to the
 * Z80's clock before every register write, so each write lands on the
 * right sample, and the frame loop takes them at the end of each frame.
 */
#include "ng_internal.h"
#include <neogeorecomp/cpu.h>
#include "kof98_z80_host.h"
#include "z80.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const uint8_t *s_m1, *s_sm1;
static size_t s_m1_size;
static int s_use_bios_rom = 1;
static int s_mvs_audio_map;
static uint8_t s_ram[0x800];
static uint32_t s_bank[4];          /* byte offsets for $F000, $E000, $C000, $8000 */
static uint8_t s_cmd, s_reply;
static int s_nmi_enabled, s_nmi_pending, s_nmi_line;
static int64_t s_z80_clock;         /* Z80 clocks derived from the Neo Geo board clock ratio */
static uint64_t s_legacy_base;
static FILE *s_ym_trace;
static FILE *s_z80_state_trace;
static FILE *s_legacy_step_trace;
static uint64_t s_step_trace_until = 1000000;
static z80 s_legacy_z80;
static int s_use_legacy_z80;

static void trace_z80_state(const char *event, uint64_t target) {
    if (!s_z80_state_trace) return;
    char state[160];
    ng_z80_recomp_snapshot(state, sizeof(state));
    fprintf(s_z80_state_trace, "%s HOST=%llu Z80=%lld TARGET=%llu %s\n", event,
            (unsigned long long)ng_cycles, (long long)s_z80_clock,
            (unsigned long long)target, state);
}

/* Synthesis (ym2610.cpp): one stereo sample per 72 Z80 clocks (8 MHz / 144). */
void    ym2610_init(const uint8_t *const *vroms, const size_t *sizes, int n);
void    ym2610_reset(void);
void    ym2610_write(int port, uint8_t v);
uint8_t ym2610_read(int port);
void    ym2610_generate(int16_t *out, size_t n);

#define SAMPLE_CLOCKS NG_AUDIO_SAMPLE_CLOCKS
#define RING 16384                  /* stereo samples, about 0.3 s */
static int16_t s_ring[RING * 2];
static size_t s_ring_head, s_ring_count;
static int64_t s_samples_done;      /* samples generated since power-on */

static void generate_to_now(void) {
    int64_t target = s_z80_clock / SAMPLE_CLOCKS;
    while (s_samples_done < target) {
        size_t n = (size_t)(target - s_samples_done), tail = (s_ring_head + s_ring_count) % RING;
        if (n > RING - tail) n = RING - tail;
        ym2610_generate(s_ring + tail * 2, n);
        s_samples_done += (int64_t)n;
        s_ring_count += n;
        if (s_ring_count > RING) {   /* nobody is taking them: keep the newest */
            s_ring_head = (s_ring_head + s_ring_count - RING) % RING;
            s_ring_count = RING;
        }
    }
}

/* ---- YM2610: timers and status only ---- */
static struct {
    uint8_t addr[2];
    uint8_t regs[2][256];
    uint8_t ctrl, status;
    int64_t ta_deadline, tb_deadline;   /* in Z80 clocks */
} s_ym;

/* The YM2610 runs from 8 MHz; a timer A tick is one FM sample (144 clocks
 * = 72 Z80 clocks at 4 MHz), timer B ticks 16 times slower. */
static int64_t ym_ta_period(void) {
    int na = (s_ym.regs[0][0x24] << 2) | (s_ym.regs[0][0x25] & 3);
    return (int64_t)(1024 - na) * 72;
}
static int64_t ym_tb_period(void) { return (int64_t)(256 - s_ym.regs[0][0x26]) * 72 * 16; }

static void update_z80_irq_line(void) {
    const int asserted = (s_ym.status & 3) != 0;
    if (s_use_legacy_z80) s_legacy_z80.int_pending = asserted;
    else ng_z80_recomp_set_irq(asserted);
}

static void ym_update_timers(void) {
    while (s_z80_clock >= s_ym.ta_deadline) {
        if (s_ym.ctrl & 0x04) s_ym.status |= 1;
        s_ym.ta_deadline += ym_ta_period();
    }
    while (s_z80_clock >= s_ym.tb_deadline) {
        if (s_ym.ctrl & 0x08) s_ym.status |= 2;
        s_ym.tb_deadline += ym_tb_period();
    }
    update_z80_irq_line();
}

static void ym_write(int port, uint8_t v) {
    int part = port >> 1;
    if (!(port & 1)) { s_ym.addr[part] = v; return; }
    uint8_t r = s_ym.addr[part];
    s_ym.regs[part][r] = v;
    if (part == 0 && r == 0x27) {
        if ((v & 1) && !(s_ym.ctrl & 1)) s_ym.ta_deadline = s_z80_clock + ym_ta_period();
        if ((v & 2) && !(s_ym.ctrl & 2)) s_ym.tb_deadline = s_z80_clock + ym_tb_period();
        if (!(v & 1)) s_ym.ta_deadline = INT64_MAX;
        if (!(v & 2)) s_ym.tb_deadline = INT64_MAX;
        if (v & 0x10) s_ym.status &= (uint8_t)~1;
        if (v & 0x20) s_ym.status &= (uint8_t)~2;
        s_ym.ctrl = v;
        update_z80_irq_line();
    }
}

static uint8_t ym_read(int port) {
    switch (port) {
    case 0: return s_ym.status;           /* busy flag always clear */
    case 1: return s_ym.regs[0][s_ym.addr[0]];
    case 2: generate_to_now(); return ym2610_read(2);   /* ADPCM end flags */
    default: return 0;
    }
}

/* ---- Z80 bus ---- */

static const uint8_t *fixed_rom(void) { return s_use_bios_rom ? s_sm1 : s_m1; }
static size_t fixed_rom_size(void) { return s_use_bios_rom ? 0x20000 : s_m1_size; }

static uint32_t bank_base(unsigned window, uint32_t bank) {
    static const unsigned shift[4] = {11, 12, 13, 14};
    if (s_m1_size <= 0x10000) return 0;
    const uint32_t address_mask = (uint32_t)(s_m1_size - 0x10000 - 1) & 0x3FFFF;
    return 0x10000 + ((bank << shift[window]) & address_mask);
}

void audio_set_mvs_mode(int mvs) {
    s_mvs_audio_map = mvs != 0;
}

static uint8_t z_read(void *u, uint16_t a) {
    (void)u;
    const uint8_t *r = fixed_rom();
    size_t size = fixed_rom_size();
    if (a < 0x8000) return r[a % size];
    if (a >= 0xF800) return s_ram[a & 0x7FF];
    uint32_t off;
    if (a >= 0xF000) off = s_bank[0] + (a & 0x7FF);
    else if (a >= 0xE000) off = s_bank[1] + (a & 0xFFF);
    else if (a >= 0xC000) off = s_bank[2] + (a & 0x1FFF);
    else off = s_bank[3] + (a & 0x3FFF);
    /* AES keeps BIOS SM1 at $0000-$7fff while the bank windows use cart M1.
     * Preserve the already validated MVS mapping until its audio is retested. */
    if (s_mvs_audio_map)
        return size ? r[off % size] : 0xFF;
    return s_m1_size ? s_m1[off % s_m1_size] : 0xFF;
}

static void z_write(void *u, uint16_t a, uint8_t v) {
    (void)u;
    if (a >= 0xF800) s_ram[a & 0x7FF] = v;
}

static int z_dynamic_code(void *u, uint16_t a) {
    (void)u;
    return a >= 0xF800;
}

static int z_valid_code(void *u, uint16_t a) {
    (void)u;
    if (a >= 0xF800 || a < 0x8000) return fixed_rom_size() != 0;
    uint32_t off;
    if (a >= 0xF000) off = s_bank[0] + (a & 0x7FF);
    else if (a >= 0xE000) off = s_bank[1] + (a & 0xFFF);
    else if (a >= 0xC000) off = s_bank[2] + (a & 0x1FFF);
    else off = s_bank[3] + (a & 0x3FFF);
    return off < (s_mvs_audio_map ? fixed_rom_size() : s_m1_size);
}

static uint32_t z_mapping(void *u) {
    (void)u;
    return (s_use_bios_rom ? 0x80000000u : 0u) |
           (((s_bank[0] / 0x800u) & 0x3Fu) << 24) |
           (((s_bank[1] / 0x1000u) & 0x3Fu) << 18) |
           (((s_bank[2] / 0x2000u) & 0x3Fu) << 12) |
           (((s_bank[3] / 0x4000u) & 0x3Fu) << 6);
}

/* Callbacks execute in the middle of Z80::Run; the host-side clock otherwise
 * remains at the previous scheduler return and collapses several chip writes
 * onto the same YM2610 sample. Track the guest CPU's live instruction time. */
static void sync_z80_clock(void) {
    const int64_t current = s_use_legacy_z80
        ? (int64_t)(s_legacy_base + s_legacy_z80.cyc)
        : (int64_t)ng_z80_recomp_bus_cycles();
    if (current > s_z80_clock) s_z80_clock = current;
}

/* Diagnostic reference matching the pre-recompiler NeoGeoRecomp sound CPU. */
static uint8_t z_in(void *u, uint16_t port);
static void z_out(void *u, uint16_t port, uint8_t value);

static uint8_t legacy_port_high(z80 *cpu) {
    return z_read(NULL, (uint16_t)(cpu->pc - 2)) == 0xDB ? cpu->a : cpu->b;
}

static uint8_t legacy_in(z80 *cpu, uint8_t port) {
    return z_in(NULL, (uint16_t)((legacy_port_high(cpu) << 8) | port));
}

static void legacy_out(z80 *cpu, uint8_t port, uint8_t value) {
    z_out(NULL, port, value);
}

static uint8_t legacy_flags(z80 *cpu) {
    return (cpu->sf ? 0x80 : 0) | (cpu->zf ? 0x40 : 0) | (cpu->yf ? 0x20 : 0) |
           (cpu->hf ? 0x10 : 0) | (cpu->xf ? 0x08 : 0) | (cpu->pf ? 0x04 : 0) |
           (cpu->nf ? 0x02 : 0) | (cpu->cf ? 0x01 : 0);
}

/* NMI is edge-triggered: one NMI per rise of (enabled && command pending). */
static void update_nmi(void) {
    int line = s_nmi_enabled && s_nmi_pending;
    if (line && !s_nmi_line) {
        trace_z80_state("NMI", 0);
        if (s_use_legacy_z80) z80_gen_nmi(&s_legacy_z80);
        else ng_z80_recomp_raise_nmi();
    }
    s_nmi_line = line;
}

static uint8_t z_in(void *u, uint16_t port) {
    (void)u;
    switch (port & 0x0F) {
    case 0x00:
        s_nmi_pending = 0;
        update_nmi();
        return s_cmd;
    case 0x04: case 0x05: case 0x06: case 0x07:
        sync_z80_clock();
        ym_update_timers();
        return ym_read(port & 3);
    case 0x08: case 0x09: case 0x0A: case 0x0B: {
        static const uint32_t unit[4] = {0x800, 0x1000, 0x2000, 0x4000};
        int w = port & 3;
        const uint32_t bank = port >> 8;
        s_bank[w] = s_mvs_audio_map ? bank * unit[w] : bank_base((unsigned)w, bank);
        return 0;
    }
    default: return 0;
    }
}

static void z_out(void *u, uint16_t port, uint8_t v) {
    (void)u;
    switch (port & 0x0F) {
    case 0x00: s_cmd = 0; break;
    case 0x04: case 0x05: case 0x06: case 0x07:
        sync_z80_clock();
        ym_update_timers();
        if (s_ym_trace && s_use_legacy_z80) {
            fprintf(s_ym_trace, "%llu HOST=%llu %02X %02X PC=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X WZ=%04X\n",
                    (unsigned long long)(s_legacy_base + s_legacy_z80.cyc),
                    (unsigned long long)ng_cycles, port & 3, v,
                    (uint16_t)(s_legacy_z80.pc - 2), s_legacy_z80.a, legacy_flags(&s_legacy_z80),
                    s_legacy_z80.b, s_legacy_z80.c, s_legacy_z80.d, s_legacy_z80.e,
                    s_legacy_z80.h, s_legacy_z80.l, s_legacy_z80.ix, s_legacy_z80.iy,
                    s_legacy_z80.sp, s_legacy_z80.mem_ptr);
        } else if (s_ym_trace) {
            char state[160];
            ng_z80_recomp_snapshot(state, sizeof(state));
            fprintf(s_ym_trace, "%llu HOST=%llu %02X %02X %s\n",
                    (unsigned long long)ng_z80_recomp_bus_cycles(),
                    (unsigned long long)ng_cycles, port & 3, v, state);
        }
        ym_write(port & 3, v);
        generate_to_now();
        ym2610_write(port & 3, v);
        break;
    case 0x08: s_nmi_enabled = !(port & 0x10); update_nmi(); break;
    case 0x0C: s_reply = v; break;
    default: break;
    }
}

/* ---- interface to the 68k side ---- */

void audio_reset(void) {
    memset(s_ram, 0, sizeof(s_ram));
    if (s_mvs_audio_map) {
        s_bank[0] = 0x1E * 0x800; s_bank[1] = 0x0E * 0x1000;
        s_bank[2] = 0x06 * 0x2000; s_bank[3] = 0x02 * 0x4000;
    } else {
        s_bank[0] = bank_base(0, 0x1E); s_bank[1] = bank_base(1, 0x0E);
        s_bank[2] = bank_base(2, 0x06); s_bank[3] = bank_base(3, 0x02);
    }
    s_nmi_enabled = 0; s_nmi_pending = 0; s_nmi_line = 0;
    memset(&s_ym, 0, sizeof(s_ym));
    s_ym.ta_deadline = s_ym.tb_deadline = INT64_MAX;
    s_z80_clock = ng_cycles / NG_MAIN_TO_AUDIO_DIV;
    s_legacy_base = (uint64_t)s_z80_clock;
    ng_z80_recomp_reset((uint64_t)s_z80_clock);
    z80_init(&s_legacy_z80);
    s_legacy_z80.read_byte = z_read;
    s_legacy_z80.write_byte = z_write;
    s_legacy_z80.port_in = legacy_in;
    s_legacy_z80.port_out = legacy_out;
    update_z80_irq_line();
    ym2610_reset();
    s_samples_done = s_z80_clock / SAMPLE_CLOCKS;
}

int audio_init(const uint8_t *m1, size_t m1_size, const uint8_t *sm1,
               const uint8_t *const *vroms, const size_t *vrom_sizes, int nvrom) {
    ng_z80_recomp_set_bus(NULL, z_read, z_write, z_in, z_out,
                          z_dynamic_code, z_valid_code, z_mapping);
    const char *trace_path = getenv("NG_YM_TRACE");
    if (trace_path && *trace_path) {
        s_ym_trace = fopen(trace_path, "w");
        if (!s_ym_trace) fprintf(stderr, "[audio] cannot write YM trace %s\n", trace_path);
    }
    const char *state_trace_path = getenv("NG_Z80_STATE_TRACE");
    if (state_trace_path && *state_trace_path) {
        s_z80_state_trace = fopen(state_trace_path, "w");
        if (!s_z80_state_trace) fprintf(stderr, "[audio] cannot write Z80 state trace %s\n", state_trace_path);
    }
    const char *z80_mode = getenv("NG_Z80_MODE");
    s_use_legacy_z80 = z80_mode && strcmp(z80_mode, "legacy") == 0;
    const char *legacy_step_path = getenv("NG_Z80_STEP_TRACE_LEGACY");
    if (legacy_step_path && *legacy_step_path) {
        s_legacy_step_trace = fopen(legacy_step_path, "w");
        if (!s_legacy_step_trace) fprintf(stderr, "[audio] cannot write legacy Z80 trace %s\n", legacy_step_path);
    }
    const char *step_until = getenv("NG_Z80_STEP_TRACE_UNTIL");
    if (step_until && *step_until) s_step_trace_until = strtoull(step_until, NULL, 0);
    ym2610_init(vroms, vrom_sizes, nvrom);
    s_m1 = m1; s_m1_size = m1_size; s_sm1 = sm1;
    s_cmd = 0; s_reply = 0;
    audio_reset();
    return 0;
}

void audio_sync(void) {
    int64_t target = ng_cycles / NG_MAIN_TO_AUDIO_DIV;
    while (s_z80_clock < target) {
        if (s_use_legacy_z80) {
            unsigned long before = s_legacy_z80.cyc;
            const uint16_t pc = s_legacy_z80.pc;
            const uint8_t opcode = z_read(NULL, pc);
            s_legacy_z80.int_pending = (s_ym.status & 3) != 0;
            z80_step(&s_legacy_z80);
            if (s_legacy_step_trace && s_legacy_base + before <= s_step_trace_until)
                fprintf(s_legacy_step_trace,
                        "%llu HOST=%llu PC=%04X OP=%02X DELTA=%lu NEXT=%04X AF=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X IX=%04X IY=%04X SP=%04X IFF=%d%d IRQ=%d EIDELAY=%d\n",
                        (unsigned long long)(s_legacy_base + before), (unsigned long long)ng_cycles,
                        pc, opcode, s_legacy_z80.cyc - before, s_legacy_z80.pc,
                        s_legacy_z80.a, legacy_flags(&s_legacy_z80),
                        s_legacy_z80.b, s_legacy_z80.c, s_legacy_z80.d, s_legacy_z80.e,
                        s_legacy_z80.h, s_legacy_z80.l, s_legacy_z80.ix, s_legacy_z80.iy, s_legacy_z80.sp,
                        s_legacy_z80.iff1, s_legacy_z80.iff2, s_legacy_z80.int_pending,
                        s_legacy_z80.iff_delay);
            s_z80_clock += (int64_t)(s_legacy_z80.cyc - before);
            ym_update_timers();
            continue;
        }
        int64_t deadline = target;
        if (s_ym.ta_deadline < deadline) deadline = s_ym.ta_deadline;
        if (s_ym.tb_deadline < deadline) deadline = s_ym.tb_deadline;
        trace_z80_state("RUN_BEGIN", (uint64_t)deadline);
        ng_z80_recomp_run_until((uint64_t)deadline, s_use_bios_rom, s_ym.status & 3);
        s_z80_clock = (int64_t)ng_z80_recomp_cycles();
        trace_z80_state("RUN_END", (uint64_t)deadline);
        ym_update_timers();
    }
}

void audio_command(uint8_t cmd) {
    audio_sync();
    trace_z80_state("COMMAND", cmd);
    s_cmd = cmd;
    s_nmi_pending = 1;
    update_nmi();
}

uint8_t audio_reply(void) {
    audio_sync();
    return s_reply;
}

void audio_select_bios_rom(int bios) {
    if (bios == s_use_bios_rom) return;
    audio_sync();
    s_use_bios_rom = bios;
    /* Swapping the Z80's ROM restarts it, so the newly selected driver
     * boots from its own reset vector. */
    uint8_t cmd = s_cmd, reply = s_reply;
    audio_reset();
    s_cmd = cmd; s_reply = reply;
}

size_t audio_take_samples(int16_t *out, size_t max_frames) {
    generate_to_now();
    size_t n = s_ring_count < max_frames ? s_ring_count : max_frames;
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = s_ring[s_ring_head * 2];
        out[i * 2 + 1] = s_ring[s_ring_head * 2 + 1];
        s_ring_head = (s_ring_head + 1) % RING;
    }
    s_ring_count -= n;
    return n;
}

void audio_shutdown(void) {
    const char *miss_prefix = getenv("NG_Z80_MISSES");
    if (miss_prefix && *miss_prefix) ng_z80_recomp_dump_misses(miss_prefix);
    if (getenv("NG_PROFILE")) {
        const uint64_t z80_clock = s_use_legacy_z80
            ? s_legacy_base + s_legacy_z80.cyc : (uint64_t)s_z80_clock;
        const int64_t expected_z80 = ng_cycles / NG_MAIN_TO_AUDIO_DIV;
        fprintf(stderr, "[audio] Neo Geo clocks: 68000=%d Hz, Z80=%d Hz; host=%lld Z80=%llu skew=%lld\n",
                NG_MAIN_CPU_CLOCK, NG_AUDIO_CPU_CLOCK, (long long)ng_cycles,
                (unsigned long long)z80_clock, (long long)((int64_t)z80_clock - expected_z80));
        fprintf(stderr, "[audio] native Z80 blocks %llu; interpreted Z80 instructions %llu\n",
                (unsigned long long)ng_z80_recomp_native_blocks(),
                (unsigned long long)ng_z80_recomp_interpreted());
        ng_z80_recomp_report();
    }
    if (s_ym_trace) {
        fclose(s_ym_trace);
        s_ym_trace = NULL;
    }
    if (s_z80_state_trace) {
        fclose(s_z80_state_trace);
        s_z80_state_trace = NULL;
    }
}
