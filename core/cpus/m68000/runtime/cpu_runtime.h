#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t (*multiarcade_recomp_m68k_read16_fn)(uint32_t address);
typedef void (*multiarcade_recomp_m68k_write16_fn)(uint32_t address, uint16_t value);

/* Exception entry uses the selected board's word bus for stack/vector cycles. */
void multiarcade_recomp_m68k_set_exception_bus(multiarcade_recomp_m68k_read16_fn read16,
    multiarcade_recomp_m68k_write16_fn write16);

#ifdef __cplusplus
}
#endif
