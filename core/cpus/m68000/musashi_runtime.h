#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define MULTIARCADERECOMP_M68K_API __declspec(dllexport)
#else
#define MULTIARCADERECOMP_M68K_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*multiarcaderecomp_m68k_read8_fn)(void *opaque, uint32_t address);
typedef void (*multiarcaderecomp_m68k_write8_fn)(void *opaque, uint32_t address, uint8_t value);
typedef int (*multiarcaderecomp_m68k_irq_ack_fn)(void *opaque, unsigned level);
typedef void (*multiarcaderecomp_m68k_instruction_fn)(void *opaque, uint32_t pc);

typedef struct multiarcaderecomp_m68k_bus {
    void *opaque;
    multiarcaderecomp_m68k_read8_fn read8;
    multiarcaderecomp_m68k_write8_fn write8;
    multiarcaderecomp_m68k_irq_ack_fn interrupt_ack;
    multiarcaderecomp_m68k_instruction_fn instruction;
} multiarcaderecomp_m68k_bus;

/* Musashi keeps internal state outside the architectural registers. The
 * caller owns a buffer of this size per emulated CPU and passes it back to
 * each operation, allowing independent CPU contexts to be stepped serially. */
MULTIARCADERECOMP_M68K_API size_t multiarcaderecomp_m68k_context_size(void);
MULTIARCADERECOMP_M68K_API int multiarcaderecomp_m68k_reset(void *context, size_t context_size,
                                             const multiarcaderecomp_m68k_bus *bus);
MULTIARCADERECOMP_M68K_API int multiarcaderecomp_m68k_run(void *context, size_t context_size,
                                           const multiarcaderecomp_m68k_bus *bus, int cycle_budget);
MULTIARCADERECOMP_M68K_API void multiarcaderecomp_m68k_set_irq(void *context, size_t context_size,
                                                 const multiarcaderecomp_m68k_bus *bus, unsigned level);
MULTIARCADERECOMP_M68K_API int multiarcaderecomp_m68k_current_cycles_run(void);

#ifdef __cplusplus
}
#endif
