// Recompiled code dispatch: maps entry addresses to generated functions and
// checks that the game's code in RAM still matches the recompiled image.
#include "rec_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t rec_code_lo, rec_code_size, rec_code_gen = 1;
bool     rec_enabled;
rec_fn_t *rec_map;
uint32_t rec_map_gen;
const rec_module_t *rec_mod;
uint32_t rec_active_image_base, rec_active_image_words;
static rec_host_t host;
static char expected_game[8];

void rec_expect_game(const char *game_id)
{
    memset(expected_game, 0, sizeof expected_game);
    if (game_id) strncpy(expected_game, game_id, sizeof expected_game - 1);
}

static uint32_t *func_ok_gen;   // rec_code_gen at which each function last matched RAM
static uint32_t *func_bad_gen;  // ... or last failed to match

const rec_host_t *rec_host_info(void)
{
    host = (rec_host_t){
        .abi = REC_ABI_VERSION, .sizeof_cpu = sizeof(cpu_t), .sizeof_tlbc = sizeof(tlbc_t),
        .cpu = &cpu, .ram = ram, .ram_dirty = ram_dirty, .tlbc = tlbc,
        .code_lo = &rec_code_lo, .code_size = &rec_code_size,
        .irq_dirty = &irq_dirty, .cpu_exc = &cpu_exc, .deadline = &rec_deadline,
        .load = rec_load, .store = rec_store, .op = rec_op,
    };
    return &host;
}

static bool use(const rec_module_t *m)
{
    if (!m || !m->nfuncs || getenv("MAMERECOMP_MIPS3_INTERPRETER_ONLY") ||
        getenv("MAMERECOMP_MIPS3_COVER") || cpu_trace) return false;
    if (m->abi != REC_ABI_VERSION || m->sizeof_cpu != sizeof(cpu_t) || m->sizeof_tlbc != sizeof(tlbc_t)) {
        fprintf(stderr, "rec: translated code is from another version, not used\n");
        return false;
    }
    if (expected_game[0] && strncmp(m->game, expected_game, sizeof m->game)) return false;
    rec_mod = m;
    rec_active_image_base = m->image_base;
    rec_active_image_words = m->image_words;
    rec_code_lo = rec_active_image_base & 0x1fffffffu;
    rec_code_size = rec_active_image_words * 4;
    free(rec_map); free(func_ok_gen); free(func_bad_gen);
    rec_map = calloc(rec_active_image_words, sizeof *rec_map);
    func_ok_gen = calloc(m->nfuncs, sizeof *func_ok_gen);
    func_bad_gen = calloc(m->nfuncs, sizeof *func_bad_gen);
    rec_map_gen = 0;
    rec_enabled = true;
    fprintf(stderr, "rec: %u functions, %u entry points, %u KB of code\n",
            m->nfuncs, m->nlabels, rec_active_image_words * 4 / 1024);
    return true;
}

void rec_init(void) { use(rec_module_init(rec_host_info())); }

// The module itself stays loaded: little memory, and it is never called again.
void rec_unload(void) { rec_enabled = false; rec_mod = NULL; }

bool rec_use_module(rec_module_init_fn init) { return init && use(init(rec_host_info())); }

static bool func_matches(uint32_t f)
{
    if (func_ok_gen[f] == rec_code_gen) return true;
    if (func_bad_gen[f] == rec_code_gen) return false;
    const rec_func_t *fn = &rec_mod->funcs[f];
    uint32_t off = fn->lo - rec_active_image_base, len = fn->hi - fn->lo;
    bool ok = memcmp(&ram2[rec_code_lo - 0x08000000u + off], (const uint8_t *)rec_mod->image + off, len) == 0;
    (ok ? func_ok_gen : func_bad_gen)[f] = rec_code_gen;
    return ok;
}

static uint64_t last_rebuild;

// As at program start (machine power-on): identical on every machine.
void rec_reset(void)
{
    rec_code_gen = 1;
    last_rebuild = 0;
    rec_forget();
}

// Forget everything learned about RAM contents (after loading a state file).
void rec_forget(void)
{
    if (!rec_enabled) return;
    memset(func_ok_gen, 0, rec_mod->nfuncs * sizeof *func_ok_gen);
    memset(func_bad_gen, 0, rec_mod->nfuncs * sizeof *func_bad_gen);
    rec_map_gen = 0;           // generations start at 1, so this never matches
}

static void rebuild(void)
{
    memset(rec_map, 0, rec_active_image_words * sizeof *rec_map);
    for (uint32_t i = 0; i < rec_mod->nlabels; i++) {
        const rec_label_t *l = &rec_mod->labels[i];
        if (func_matches(l->fn)) rec_map[(l->pc - rec_active_image_base) >> 2] = rec_mod->funcs[l->fn].fn;
    }
    rec_map_gen = rec_code_gen;
}

// Code was written since the map was built: rebuild it lazily, entry by entry.
// While code is being loaded (stores every few instructions) this only runs
// every 10 ms of game time; the interpreter covers the gap.
rec_fn_t rec_lookup_slow(uint32_t pc)
{
    if (cpu.cycles - last_rebuild < 1000000 && last_rebuild) return 0;
    last_rebuild = cpu.cycles;
    rebuild();
    return rec_map[(pc - rec_active_image_base) >> 2];
}

// Which code runs translated must not change across a state load, or the
// replay would take a different (interpreted vs compiled) path. RAM is
// restored first, so rebuilding from it reproduces the saved map exactly.
size_t rec_state(uint8_t *buf, bool load)
{
    struct { uint32_t gen; bool map_valid; uint64_t last; } s;
    memset(&s, 0, sizeof s);     // padding too: states and netplay hashes compare bytes
    if (buf && load) {
        memcpy(&s, buf, sizeof s);
        if (rec_enabled) {
            rec_code_gen = s.gen;
            last_rebuild = s.last;
            if (s.map_valid) { if (rec_map_gen != rec_code_gen) rebuild(); }
            else rec_map_gen = rec_code_gen - 1;
        }
    } else if (buf) {
        s.gen = rec_code_gen; s.map_valid = rec_map_gen == rec_code_gen; s.last = last_rebuild;
        memcpy(buf, &s, sizeof s);
    }
    return sizeof s;
}
