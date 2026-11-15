#pragma once

#include "cpus/mcs51/mcs51.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace multiarcaderecomp::chips {

// Intel 8751-compatible MCU as wired on Sega ROM boards. The MCS-51 CPU is
// generic; each board supplies external memory, pins, interrupts, and its host
// clock when advancing the device.
class SegaI8751 {
public:
    struct Bus {
        std::function<std::uint8_t(std::uint16_t)> read_external;
        std::function<void(std::uint16_t, std::uint8_t)> write_external;
        std::function<std::uint8_t()> read_port1;
        std::function<void(std::uint8_t)> write_port1;
        std::function<void(unsigned)> interrupt_ack;
        std::uint16_t external_data_mask = 0x00ff;
    };

    explicit SegaI8751(std::span<const std::uint8_t> firmware, Bus bus);

    // The device has an 8 MHz oscillator; a classic MCS-51 machine cycle takes
    // 12 oscillator clocks. Host-cycle conversion is board wiring, so callers
    // pass their main-CPU clock instead of this chip knowing a board family.
    void advance_to_host_cycle(std::uint64_t cycle, std::uint32_t host_clock_hz);
    void set_vblank(bool asserted);
    void set_irq1(bool asserted);

    std::uint64_t executed_machine_cycles() const noexcept { return m_executed_cycles; }
    std::uint64_t instruction_count() const noexcept { return m_instruction_count; }
    std::uint16_t pc() const noexcept { return m_cpu.pc(); }
    std::uint8_t interrupt_enable() const noexcept { return m_cpu.interrupt_enable(); }
    std::uint8_t timer_control() const noexcept { return m_cpu.timer_control(); }

private:
    mcs51::Core::Bus make_cpu_bus();
    Bus m_bus;
    mcs51::Core m_cpu;
    std::uint64_t m_executed_cycles = 0;
    std::uint64_t m_instruction_count = 0;
    std::uint64_t m_last_host_cycle = 0;
    bool m_started = false;
};

} // namespace multiarcaderecomp::chips
