#pragma once

#include "mcs48.hpp"

#include <cstdint>
#include <span>

namespace mamerecomp {

// NEC uPD7751: an 8048-compatible MCU with 1 KiB internal program ROM.
// The board owns the oscillator rate and the external ROM/DAC wiring.
class Upd7751 {
public:
    using Bus = Mcs48::Bus;
    static constexpr std::uint32_t clocks_per_machine_cycle = Mcs48::clocks_per_machine_cycle;

    explicit Upd7751(std::span<const std::uint8_t> program, Bus bus);
    void set_reset(bool asserted) { m_cpu.set_reset(asserted); }
    void set_irq(bool asserted) { m_cpu.set_irq(asserted); }
    std::uint32_t step() { return m_cpu.step(); }
    std::uint16_t pc() const { return m_cpu.pc(); }
    std::uint64_t machine_cycles() const { return m_cpu.machine_cycles(); }

private:
    Mcs48 m_cpu;
};

} // namespace mamerecomp
