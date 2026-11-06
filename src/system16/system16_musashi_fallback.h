#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mamerecomp_system16_musashi_step(void);
void mamerecomp_system16_musashi_describe(uint32_t pc, uint16_t* opcode, char* disassembly, size_t capacity);

/* Optional opcode-space overlay (for example FD1094); data reads remain on the normal bus. */
typedef uint16_t (*mamerecomp_system16_opcode_read16_fn)(void* context, uint32_t address);
void mamerecomp_system16_musashi_set_opcode_reader(
    mamerecomp_system16_opcode_read16_fn reader, void* context);
typedef void (*mamerecomp_system16_cmpild_fn)(void* context, uint32_t value, unsigned reg);
typedef void (*mamerecomp_system16_rte_fn)(void* context);
typedef int (*mamerecomp_system16_irq_ack_fn)(void* context, int level);
void mamerecomp_system16_musashi_set_fd1094_callbacks(
    mamerecomp_system16_cmpild_fn cmpild, mamerecomp_system16_rte_fn rte,
    mamerecomp_system16_irq_ack_fn irq_ack, void* context);

#ifdef __cplusplus
}
#endif
