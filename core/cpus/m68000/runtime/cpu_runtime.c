/*
 * System 16 binding for the shared 68000 register helpers and exception entry.
 * Keep the CPU state/flags ABI from m68krecomp, but route exception stack and
 * vector accesses through this board's bus callbacks instead of Neo Geo's bus.
 */
#include <neogeorecomp/cpu.h>

#include "cpu_runtime.h"

uint16_t ng_r16_slow(uint32_t address);
void ng_w16_slow(uint32_t address, uint16_t value);

ng_cpu_t ng_cpu;
int64_t ng_cycles;
int64_t ng_next_event;
uint64_t ng_icount;
ng_debug_instruction_hook_t ng_debug_instruction_hook;

static multiarcade_recomp_m68k_read16_fn s_exception_read16;
static multiarcade_recomp_m68k_write16_fn s_exception_write16;

void multiarcade_recomp_m68k_set_exception_bus(multiarcade_recomp_m68k_read16_fn read16,
    multiarcade_recomp_m68k_write16_fn write16) {
    s_exception_read16 = read16;
    s_exception_write16 = write16;
}

void ng_set_sr(uint16_t sr) {
    uint8_t s = (sr >> 13) & 1;
    if (s != CPU.s) {
        uint32_t t = CPU.a[7];
        CPU.a[7] = CPU.osp;
        CPU.osp = t;
        CPU.s = s;
    }
    CPU.t = (sr >> 15) & 1;
    CPU.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
}

static void push16(uint16_t value) {
    CPU.a[7] -= 2;
    if (s_exception_write16 != 0) s_exception_write16(CPU.a[7], value);
}

static void push32(uint32_t value) {
    CPU.a[7] -= 4;
    if (s_exception_write16 != 0) {
        s_exception_write16(CPU.a[7], (uint16_t)(value >> 16));
        s_exception_write16(CPU.a[7] + 2, (uint16_t)value);
    }
}

static uint32_t read32(uint32_t address) {
    if (s_exception_read16 == 0) return 0;
    return ((uint32_t)s_exception_read16(address) << 16)
        | s_exception_read16(address + 2);
}

void ng_exception(int vector, uint32_t ret_pc) {
    uint16_t old_sr = ng_get_sr();
    ng_set_sr((uint16_t)((old_sr | 0x2000) & ~0x8000));
    push32(ret_pc);
    push16(old_sr);
    CPU.pc = read32((uint32_t)vector * 4);
    ng_cycles += 34;
}
