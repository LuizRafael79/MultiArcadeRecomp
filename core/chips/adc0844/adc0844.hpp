#pragma once

#include <array>
#include <cstdint>

namespace mamerecomp::chips {

// ADC0844/ADC0848 conversion/interrupt behavior from MAME's BSD-3-Clause
// device. Inputs are supplied by the owning board in 8-bit channel values.
class ADC0844 final {
public:
    void set_inputs(std::uint8_t ch1, std::uint8_t ch2,
                    std::uint8_t ch3, std::uint8_t ch4) noexcept;
    void write(std::uint8_t data) noexcept;
    std::uint8_t read() noexcept;
    void advance_cycles(std::uint64_t c31_cycles) noexcept;
    bool interrupt_asserted() const noexcept { return m_interrupt; }

private:
    static std::uint8_t clamp(int value) noexcept;
    void complete_conversion() noexcept;
    std::array<std::uint8_t, 4> m_channels{0xff, 0xff, 0xff, 0xff};
    std::uint8_t m_channel{0x0f};
    std::uint8_t m_result{0xff};
    std::uint64_t m_conversion_cycles{};
    bool m_conversion_active{};
    bool m_interrupt{};
};

} // namespace mamerecomp::chips
