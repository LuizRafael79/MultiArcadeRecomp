#include "m68k_recomp_dispatch.h"

#include "system16/system16_m68k_bus.h"

#include <neogeorecomp/cpu.h>

static uint32_t s_bad_dispatch_pc;
static int s_bad_dispatch;

void ng_bad_dispatch(uint32_t pc)
{
    s_bad_dispatch = 1;
    s_bad_dispatch_pc = pc;
}

M68kRecompDispatchResult m68k_recomp_dispatch_once(
    const ng_func_entry_t *entries, size_t count)
{
    M68kRecompDispatchResult result = {
        M68K_RECOMP_DISPATCH_BAD_PC, CPU.pc, 0, M68K_BUS_ACCESS_NONE
    };
    if (entries == 0 || count == 0 || (CPU.pc & 1u) != 0) {
        return result;
    }

    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (entries[mid].addr < CPU.pc) lo = mid + 1;
        else hi = mid;
    }
    if (lo == count || entries[lo].addr != CPU.pc || entries[lo].fn == 0) {
        result.status = M68K_RECOMP_DISPATCH_NO_ENTRY;
        return result;
    }

    s_bad_dispatch = 0;
    s_bad_dispatch_pc = 0;
    m68k_system16_bus_clear_fault();
    entries[lo].fn();

    const M68kBusFault fault = m68k_system16_bus_fault();
    if (fault.access != M68K_BUS_ACCESS_NONE) {
        result.status = M68K_RECOMP_DISPATCH_BUS_FAULT;
        result.pc = CPU.pc;
        result.fault_address = fault.address;
        result.fault_access = (uint8_t)fault.access;
        return result;
    }
    if (s_bad_dispatch) {
        result.status = M68K_RECOMP_DISPATCH_BAD_PC;
        result.pc = s_bad_dispatch_pc;
        return result;
    }

    result.status = M68K_RECOMP_DISPATCH_OK;
    result.pc = CPU.pc;
    return result;
}

M68kRecompDispatchResult m68k_recomp_accept_interrupt(uint8_t level)
{
    M68kRecompDispatchResult result = {
        M68K_RECOMP_DISPATCH_INVALID_INTERRUPT, CPU.pc, 0, M68K_BUS_ACCESS_NONE
    };
    if (level == 0 || level > 7) return result;
    if (level <= CPU.ipl) {
        result.status = M68K_RECOMP_DISPATCH_INTERRUPT_MASKED;
        return result;
    }

    s_bad_dispatch = 0;
    m68k_system16_bus_clear_fault();
    CPU.stopped = 0;
    ng_exception(24 + level, CPU.pc);
    CPU.ipl = level;
    /* The generic exception helper charges 34 cycles; 68000 autovector
     * interrupt entry is 44 cycles in the Musashi reference table. */
    ng_cycles += 10;

    const M68kBusFault fault = m68k_system16_bus_fault();
    result.pc = CPU.pc;
    if (fault.access != M68K_BUS_ACCESS_NONE) {
        result.status = M68K_RECOMP_DISPATCH_BUS_FAULT;
        result.fault_address = fault.address;
        result.fault_access = (uint8_t)fault.access;
        return result;
    }
    result.status = M68K_RECOMP_DISPATCH_OK;
    return result;
}
