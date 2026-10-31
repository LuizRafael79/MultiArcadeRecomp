#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mamerecomp {

// Intel MCS-48 / 8048 execution core used by the NEC uPD7751. One returned
// cycle is one 15-clock MCS-48 machine cycle.
class Mcs48 {
public:
    struct Bus {
        std::function<std::uint8_t()> read_bus;
        std::function<void(std::uint8_t)> write_bus;
        std::function<std::uint8_t(unsigned)> read_port;
        std::function<void(unsigned, std::uint8_t)> write_port;
        std::function<void(bool)> prog_line;
    };

    explicit Mcs48(std::span<const std::uint8_t> program, Bus bus);
    void reset();
    void set_reset(bool asserted);
    void set_irq(bool asserted);
    std::uint32_t step();

    std::uint16_t pc() const { return m_pc; }
    std::uint8_t accumulator() const { return m_a; }
    std::uint64_t machine_cycles() const { return m_cycles; }

private:
    static constexpr std::uint8_t C = 0x80;
    static constexpr std::uint8_t AC = 0x40;
    static constexpr std::uint8_t F0 = 0x20;
    static constexpr std::uint8_t RB = 0x10;

    std::uint8_t fetch();
    std::uint8_t fetch_arg();
    std::uint8_t& reg(unsigned index);
    std::uint8_t indirect(unsigned index) const;
    void set_indirect(unsigned index, std::uint8_t value);
    void add(std::uint8_t value, bool with_carry);
    void branch(bool take);
    void jump(std::uint16_t address);
    void call(std::uint16_t address);
    void ret(bool restore_psw);
    void expander(unsigned operation, unsigned port);
    void advance_timer(unsigned machine_cycles);

    std::array<std::uint8_t, 0x400> m_rom{};
    std::array<std::uint8_t, 64> m_ram{};
    Bus m_bus;
    std::uint16_t m_pc = 0;
    std::uint16_t m_a11 = 0;
    std::uint8_t m_a = 0;
    std::uint8_t m_psw = 0;
    std::uint8_t m_p1 = 0xff;
    std::uint8_t m_p2 = 0xff;
    std::uint8_t m_timer = 0;
    std::uint8_t m_prescaler = 0;
    bool m_f1 = false;
    bool m_timer_enabled = false;
    bool m_timer_flag = false;
    bool m_timer_irq_enabled = false;
    bool m_timer_irq_pending = false;
    bool m_irq_enabled = false;
    bool m_irq_asserted = false;
    bool m_irq_in_progress = false;
    bool m_reset_asserted = false;
    std::uint64_t m_cycles = 0;
};

} // namespace mamerecomp
