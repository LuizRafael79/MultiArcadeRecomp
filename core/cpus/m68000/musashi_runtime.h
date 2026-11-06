#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define MAMERECOMP_M68K_API __declspec(dllexport)
#else
#define MAMERECOMP_M68K_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*mamerecomp_m68k_read8_fn)(void *opaque, uint32_t address);
typedef void (*mamerecomp_m68k_write8_fn)(void *opaque, uint32_t address, uint8_t value);
typedef int (*mamerecomp_m68k_irq_ack_fn)(void *opaque, unsigned level);
typedef void (*mamerecomp_m68k_instruction_fn)(void *opaque, uint32_t pc);

typedef struct mamerecomp_m68k_bus {
    void *opaque;
    mamerecomp_m68k_read8_fn read8;
    mamerecomp_m68k_write8_fn write8;
    mamerecomp_m68k_irq_ack_fn interrupt_ack;
    mamerecomp_m68k_instruction_fn instruction;
} mamerecomp_m68k_bus;

/* Musashi keeps internal state outside the architectural registers. The
 * caller owns a buffer of this size per emulated CPU and passes it back to
 * each operation, allowing independent CPU contexts to be stepped serially. */
MAMERECOMP_M68K_API size_t mamerecomp_m68k_context_size(void);
MAMERECOMP_M68K_API int mamerecomp_m68k_reset(void *context, size_t context_size,
                                             const mamerecomp_m68k_bus *bus);
MAMERECOMP_M68K_API int mamerecomp_m68k_run(void *context, size_t context_size,
                                           const mamerecomp_m68k_bus *bus, int cycle_budget);
MAMERECOMP_M68K_API void mamerecomp_m68k_set_irq(void *context, size_t context_size,
                                                 const mamerecomp_m68k_bus *bus, unsigned level);
MAMERECOMP_M68K_API int mamerecomp_m68k_current_cycles_run(void);

#ifdef __cplusplus
}
#endif
