#include "sega_i8751.hpp"

#include <stdexcept>
#include <utility>

namespace multiarcaderecomp::chips {
namespace {
constexpr std::uint32_t kOscillatorHz = 8'000'000;
constexpr std::uint32_t kClocksPerMachineCycle = 12;
}

SegaI8751::SegaI8751(std::span<const std::uint8_t> firmware, Bus bus)
    : m_bus(std::move(bus)), m_cpu(firmware, make_cpu_bus()) {}

mcs51::Core::Bus SegaI8751::make_cpu_bus() {
    mcs51::Core::Bus cpu_bus;
    cpu_bus.external_data_mask = m_bus.external_data_mask;
    cpu_bus.read_external = [this](std::uint16_t address) {
        return m_bus.read_external ? m_bus.read_external(address) : 0xff;
    };
    cpu_bus.write_external = [this](std::uint16_t address, std::uint8_t value) {
        if (m_bus.write_external) m_bus.write_external(address, value);
    };
    cpu_bus.read_port[1] = [this] {
        return m_bus.read_port1 ? m_bus.read_port1() : 0xff;
    };
    cpu_bus.write_port[1] = [this](std::uint8_t value) {
        if (m_started && m_bus.write_port1) m_bus.write_port1(value);
    };
    cpu_bus.interrupt_ack = [this](unsigned interrupt) {
        if (interrupt == 0) m_cpu.set_irq0(false);
        else if (interrupt == 2) m_cpu.set_irq1(false);
        if (m_bus.interrupt_ack) m_bus.interrupt_ack(interrupt);
    };
    return cpu_bus;
}

void SegaI8751::advance_to_host_cycle(std::uint64_t cycle, std::uint32_t host_clock_hz) {
    if (host_clock_hz == 0) throw std::invalid_argument("I8751 host clock must be nonzero");
    if (cycle <= m_last_host_cycle) return;
    m_last_host_cycle = cycle;
    const std::uint64_t denominator = static_cast<std::uint64_t>(host_clock_hz) * kClocksPerMachineCycle;
    const std::uint64_t target_cycles = (cycle / denominator) * kOscillatorHz
        + (cycle % denominator) * kOscillatorHz / denominator;
    while (m_executed_cycles < target_cycles) {
        m_started = true;
        m_executed_cycles += m_cpu.step();
        ++m_instruction_count;
    }
}

void SegaI8751::set_vblank(bool asserted) { m_cpu.set_irq0(asserted); }
void SegaI8751::set_irq1(bool asserted) { m_cpu.set_irq1(asserted); }

} // namespace multiarcaderecomp::chips
