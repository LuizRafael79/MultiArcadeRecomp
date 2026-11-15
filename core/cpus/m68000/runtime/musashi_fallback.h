#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int multiarcaderecomp_m68k_musashi_step(void);
/* Run a contiguous interpreted span, yielding at the next translated entry,
 * board event, bus fault, or invalidated AOT state. */
int multiarcaderecomp_m68k_musashi_run_until(const uint32_t* aot_entry_pcs, size_t aot_entry_count,
    const bool* aot_state_valid, uint64_t* instruction_count);
void multiarcaderecomp_m68k_musashi_describe(uint32_t pc, uint16_t* opcode, char* disassembly, size_t capacity);

/* Optional opcode-space overlay (for example FD1094); data reads remain on the normal bus. */
typedef uint16_t (*MultiArcadeRecompM68kOpcodeRead16Fn)(void* context, uint32_t address);
void multiarcaderecomp_m68k_musashi_set_opcode_reader(
    MultiArcadeRecompM68kOpcodeRead16Fn reader, void* context);
typedef void (*MultiArcadeRecompM68kCmpildFn)(void* context, uint32_t value, unsigned reg);
typedef void (*MultiArcadeRecompM68kRteFn)(void* context);
typedef int (*MultiArcadeRecompM68kIrqAckFn)(void* context, int level);
void multiarcaderecomp_m68k_musashi_set_extension_callbacks(
    MultiArcadeRecompM68kCmpildFn cmpild, MultiArcadeRecompM68kRteFn rte,
    MultiArcadeRecompM68kIrqAckFn irq_ack, void* context);

#ifdef __cplusplus
}
#endif
