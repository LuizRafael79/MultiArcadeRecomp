#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mamerecomp_m68k_musashi_step(void);
/* Run a contiguous interpreted span, yielding at the next translated entry,
 * board event, bus fault, or invalidated AOT state. */
int mamerecomp_m68k_musashi_run_until(const uint32_t* aot_entry_pcs, size_t aot_entry_count,
    const bool* aot_state_valid, uint64_t* instruction_count);
void mamerecomp_m68k_musashi_describe(uint32_t pc, uint16_t* opcode, char* disassembly, size_t capacity);

/* Optional opcode-space overlay (for example FD1094); data reads remain on the normal bus. */
typedef uint16_t (*MameRecompM68kOpcodeRead16Fn)(void* context, uint32_t address);
void mamerecomp_m68k_musashi_set_opcode_reader(
    MameRecompM68kOpcodeRead16Fn reader, void* context);
typedef void (*MameRecompM68kCmpildFn)(void* context, uint32_t value, unsigned reg);
typedef void (*MameRecompM68kRteFn)(void* context);
typedef int (*MameRecompM68kIrqAckFn)(void* context, int level);
void mamerecomp_m68k_musashi_set_extension_callbacks(
    MameRecompM68kCmpildFn cmpild, MameRecompM68kRteFn rte,
    MameRecompM68kIrqAckFn irq_ack, void* context);

#ifdef __cplusplus
}
#endif
