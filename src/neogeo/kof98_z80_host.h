#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*ng_z80_read_fn)(void *context, uint16_t address);
typedef void (*ng_z80_write_fn)(void *context, uint16_t address, uint8_t value);
typedef uint8_t (*ng_z80_in_fn)(void *context, uint16_t port);
typedef void (*ng_z80_out_fn)(void *context, uint16_t port, uint8_t value);
typedef int (*ng_z80_address_fn)(void *context, uint16_t address);
typedef uint32_t (*ng_z80_mapping_fn)(void *context);

void ng_z80_recomp_set_codes(const void *bios_code, const void *cart_code);
void ng_z80_recomp_set_bus(void *context, ng_z80_read_fn read,
                           ng_z80_write_fn write, ng_z80_in_fn port_in,
                           ng_z80_out_fn port_out, ng_z80_address_fn dynamic_code,
                           ng_z80_address_fn valid_code, ng_z80_mapping_fn mapping);
void ng_z80_recomp_reset(uint64_t cycle);
void ng_z80_recomp_run_until(uint64_t cycle, int bios_rom, int irq_asserted);
void ng_z80_recomp_raise_nmi(void);
void ng_z80_recomp_set_irq(int asserted);
void ng_z80_recomp_report(void);
void ng_z80_recomp_dump_misses(const char *path_prefix);
uint64_t ng_z80_recomp_cycles(void);
uint64_t ng_z80_recomp_bus_cycles(void);
uint64_t ng_z80_recomp_interpreted(void);
uint64_t ng_z80_recomp_native_blocks(void);
void ng_z80_recomp_snapshot(char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
