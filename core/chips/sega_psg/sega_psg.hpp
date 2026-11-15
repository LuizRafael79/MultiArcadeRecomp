#pragma once

#include <array>
#include <cstdint>

namespace multiarcaderecomp::chips {

// Sega VDP PSG compatible with the SN76489 register and divider model.
// The host supplies input clock ticks and routes the mono DAC sample.
class SegaPsg {
public:
    void reset() noexcept;
    void write(std::uint8_t value) noexcept;
    void advance_clocks(std::uint32_t clocks) noexcept;

    std::int16_t output() const noexcept;
    std::uint64_t writes() const noexcept { return m_writes; }

private:
    void clock_divided_tick() noexcept;

    std::array<std::uint16_t, 8> m_registers{};
    std::array<std::int32_t, 4> m_counters{};
    std::array<std::uint8_t, 4> m_outputs{};
    std::array<std::int32_t, 16> m_volume{};
    std::uint16_t m_noise_lfsr = 0x8000;
    std::uint8_t m_last_register = 3;
    std::uint8_t m_clock_divider = 0;
    std::uint64_t m_writes = 0;
};

} // namespace multiarcaderecomp::chips
