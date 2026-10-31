#pragma once

#include <neogeorecomp/neogeo.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    M68K_RECOMP_DISPATCH_OK = 0,
    M68K_RECOMP_DISPATCH_NO_ENTRY,
    M68K_RECOMP_DISPATCH_BAD_PC,
    M68K_RECOMP_DISPATCH_BUS_FAULT,
    M68K_RECOMP_DISPATCH_INTERRUPT_MASKED,
    M68K_RECOMP_DISPATCH_INVALID_INTERRUPT
} M68kRecompDispatchStatus;

typedef struct {
    M68kRecompDispatchStatus status;
    uint32_t pc;
    uint32_t fault_address;
    uint8_t fault_access;
} M68kRecompDispatchResult;

/* Execute one generated routine for CPU.pc. The table must be sorted by addr,
 * as emitted by m68krecomp. A routine returns at a control-transfer boundary
 * or when its event budget is reached. */
M68kRecompDispatchResult m68k_recomp_dispatch_once(
    const ng_func_entry_t *entries, size_t count);

/* Accept an autovectored 68000 interrupt (vectors 25-31). */
M68kRecompDispatchResult m68k_recomp_accept_interrupt(uint8_t level);

#ifdef __cplusplus
}
#endif
