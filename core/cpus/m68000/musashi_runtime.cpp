#include "musashi_runtime.h"

#include <m68k.h>

#include <algorithm>
#include <mutex>

namespace {

std::once_flag s_initialized;
const mamerecomp_m68k_bus *s_active_bus = nullptr;

int interrupt_acknowledge(int level)
{
    const int vector = s_active_bus && s_active_bus->interrupt_ack
        ? s_active_bus->interrupt_ack(s_active_bus->opaque,
                                      static_cast<unsigned>(level))
        : static_cast<int>(M68K_INT_ACK_AUTOVECTOR);
    // Musashi keeps externally asserted IRQ lines active after acknowledge.
    // The bus callback acknowledges the source (MAME HOLD_LINE semantics);
    // clear the CPU pin too. A still-pending level source can be reasserted by
    // the board before its next execution slice.
    m68k_set_irq(0);
    return vector;
}

void instruction_hook(unsigned int pc)
{
    if (s_active_bus && s_active_bus->instruction)
        s_active_bus->instruction(s_active_bus->opaque, pc & 0x00ffffffu);
}

uint8_t read8(uint32_t address)
{
    return s_active_bus && s_active_bus->read8
        ? s_active_bus->read8(s_active_bus->opaque, address & 0x00ffffffu)
        : 0xff;
}

void write8(uint32_t address, uint8_t value)
{
    if (s_active_bus && s_active_bus->write8)
        s_active_bus->write8(s_active_bus->opaque, address & 0x00ffffffu, value);
}

void initialize()
{
    std::call_once(s_initialized, [] {
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_set_int_ack_callback(interrupt_acknowledge);
        m68k_set_instr_hook_callback(instruction_hook);
    });
}

void load_context(void *context, size_t context_size,
                  const mamerecomp_m68k_bus *bus)
{
    initialize();
    s_active_bus = bus;
    m68k_set_context(context);
    (void)context_size;
}

} // namespace

extern "C" {

unsigned int m68k_read_memory_8(unsigned int address) { return read8(address); }
unsigned int m68k_read_memory_16(unsigned int address)
{
    return (unsigned(read8(address)) << 8) | read8(address + 1);
}
unsigned int m68k_read_memory_32(unsigned int address)
{
    return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
}
unsigned int m68k_read_immediate_16(unsigned int address) { return m68k_read_memory_16(address); }
unsigned int m68k_read_immediate_32(unsigned int address) { return m68k_read_memory_32(address); }
unsigned int m68k_read_pcrelative_8(unsigned int address) { return read8(address); }
unsigned int m68k_read_pcrelative_16(unsigned int address) { return m68k_read_memory_16(address); }
unsigned int m68k_read_pcrelative_32(unsigned int address) { return m68k_read_memory_32(address); }
unsigned int m68k_read_disassembler_8(unsigned int address) { return read8(address); }
unsigned int m68k_read_disassembler_16(unsigned int address) { return m68k_read_memory_16(address); }
unsigned int m68k_read_disassembler_32(unsigned int address) { return m68k_read_memory_32(address); }
void m68k_write_memory_8(unsigned int address, unsigned int value)
{ write8(address, static_cast<uint8_t>(value)); }
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    write8(address, static_cast<uint8_t>(value >> 8));
    write8(address + 1, static_cast<uint8_t>(value));
}
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address, value >> 16);
    m68k_write_memory_16(address + 2, value);
}
void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{ m68k_write_memory_32(address, value); }

size_t mamerecomp_m68k_context_size(void)
{
    initialize();
    return m68k_get_context(nullptr);
}

int mamerecomp_m68k_reset(void *context, size_t context_size,
                          const mamerecomp_m68k_bus *bus)
{
    if (!context || !bus || context_size < mamerecomp_m68k_context_size()) return 0;
    s_active_bus = bus;
    m68k_pulse_reset();
    return m68k_get_context(context) != 0;
}

int mamerecomp_m68k_run(void *context, size_t context_size,
                        const mamerecomp_m68k_bus *bus, int cycle_budget)
{
    if (!context || !bus || cycle_budget <= 0 ||
        context_size < mamerecomp_m68k_context_size()) return 0;
    load_context(context, context_size, bus);
    const int consumed = m68k_execute(cycle_budget);
    (void)m68k_get_context(context);
    return consumed;
}

void mamerecomp_m68k_set_irq(void *context, size_t context_size,
                             const mamerecomp_m68k_bus *bus, unsigned level)
{
    if (!context || !bus || context_size < mamerecomp_m68k_context_size()) return;
    load_context(context, context_size, bus);
    m68k_set_irq(std::min(level, 7u));
    (void)m68k_get_context(context);
}

int mamerecomp_m68k_current_cycles_run(void)
{
    return m68k_cycles_run();
}

} // extern "C"
