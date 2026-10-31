#include "system16_m68k_bus.h"

#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>
#include <string.h>

/* Generated modules and the host must share this hook through the core .so. */
ng_debug_instruction_hook_t ng_debug_instruction_hook = 0;

/* The generated 68000 code uses the NeoGeoRecomp bus ABI. Leave page entries
 * null so every access passes through the System 16 callbacks and preserves
 * MMIO side effects. */
uint8_t *ng_rmap[256];
uint8_t *ng_wmap[256];

static M68kBusCallbacks s_callbacks;
static M68kBusFault s_fault;
static M68kBusReadObserver s_read_observer;
static M68kBusWriteObserver s_write_observer;

static void record_fault(M68kBusAccess access, uint32_t address)
{
    if (s_fault.access == M68K_BUS_ACCESS_NONE) {
        s_fault.access = access;
        s_fault.address = address & 0x00ffffffu;
    }
}

void m68k_system16_bus_bind(const M68kBusCallbacks *callbacks)
{
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    s_read_observer = 0;
    s_write_observer = 0;
    memset(ng_rmap, 0, sizeof(ng_rmap));
    memset(ng_wmap, 0, sizeof(ng_wmap));
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    }
    m68k_system16_bus_clear_fault();
}

void m68k_system16_bus_set_write_observer(M68kBusWriteObserver observer)
{
    s_write_observer = observer;
}

void m68k_system16_bus_set_read_observer(M68kBusReadObserver observer)
{
    s_read_observer = observer;
}

void m68k_system16_bus_clear_fault(void)
{
    s_fault.access = M68K_BUS_ACCESS_NONE;
    s_fault.address = 0;
}

M68kBusFault m68k_system16_bus_fault(void)
{
    return s_fault;
}

uint8_t ng_r8_slow(uint32_t address)
{
    uint8_t value = 0xff;
    address &= 0x00ffffffu;
    if (s_fault.access != M68K_BUS_ACCESS_NONE || s_callbacks.read8 == NULL ||
        !s_callbacks.read8(s_callbacks.context, address, &value)) {
        record_fault(M68K_BUS_ACCESS_READ8, address);
        return 0xff;
    }
    if (s_read_observer != NULL) s_read_observer(address, 8, value);
    return value;
}

uint16_t ng_r16_slow(uint32_t address)
{
    uint16_t value = 0xffff;
    address &= 0x00fffffeu;
    if (s_fault.access != M68K_BUS_ACCESS_NONE || s_callbacks.read16 == NULL ||
        !s_callbacks.read16(s_callbacks.context, address, &value)) {
        record_fault(M68K_BUS_ACCESS_READ16, address);
        return 0xffff;
    }
    if (s_read_observer != NULL) s_read_observer(address, 16, value);
    return value;
}

void ng_w8_slow(uint32_t address, uint8_t value)
{
    address &= 0x00ffffffu;
    if (s_fault.access != M68K_BUS_ACCESS_NONE || s_callbacks.write8 == NULL ||
        !s_callbacks.write8(s_callbacks.context, address, value)) {
        record_fault(M68K_BUS_ACCESS_WRITE8, address);
        return;
    }
    if (s_write_observer != NULL) s_write_observer(address, 8, value);
}

void ng_w16_slow(uint32_t address, uint16_t value)
{
    address &= 0x00fffffeu;
    if (s_fault.access != M68K_BUS_ACCESS_NONE || s_callbacks.write16 == NULL ||
        !s_callbacks.write16(s_callbacks.context, address, value)) {
        record_fault(M68K_BUS_ACCESS_WRITE16, address);
        return;
    }
    if (s_write_observer != NULL) s_write_observer(address, 16, value);
}
