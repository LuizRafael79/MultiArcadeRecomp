#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*M68kBusRead8)(void *, uint32_t, uint8_t *);
typedef bool (*M68kBusRead16)(void *, uint32_t, uint16_t *);
typedef bool (*M68kBusWrite8)(void *, uint32_t, uint8_t);
typedef bool (*M68kBusWrite16)(void *, uint32_t, uint16_t);
typedef void (*M68kBusReadObserver)(uint32_t, uint8_t, uint32_t);
typedef void (*M68kBusWriteObserver)(uint32_t, uint8_t, uint32_t);

typedef struct {
    void *context;
    M68kBusRead8 read8;
    M68kBusRead16 read16;
    M68kBusWrite8 write8;
    M68kBusWrite16 write16;
} M68kBusCallbacks;

typedef enum {
    M68K_BUS_ACCESS_NONE = 0,
    M68K_BUS_ACCESS_READ8,
    M68K_BUS_ACCESS_READ16,
    M68K_BUS_ACCESS_WRITE8,
    M68K_BUS_ACCESS_WRITE16
} M68kBusAccess;

typedef struct {
    M68kBusAccess access;
    uint32_t address;
} M68kBusFault;

void m68k_system16_bus_bind(const M68kBusCallbacks *callbacks);
void m68k_system16_bus_set_read_observer(M68kBusReadObserver observer);
void m68k_system16_bus_set_write_observer(M68kBusWriteObserver observer);
void m68k_system16_bus_clear_fault(void);
M68kBusFault m68k_system16_bus_fault(void);
void m68k_system16_bind_address_space(void *address_space);

#ifdef __cplusplus
}
#endif
