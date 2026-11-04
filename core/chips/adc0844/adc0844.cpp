#include "core/chips/adc0844/adc0844.hpp"

#include <algorithm>

namespace mamerecomp::chips {

void ADC0844::set_inputs(std::uint8_t ch1, std::uint8_t ch2,
                         std::uint8_t ch3, std::uint8_t ch4) noexcept {
    m_channels = {ch1, ch2, ch3, ch4};
}

void ADC0844::write(std::uint8_t data) noexcept {
    m_interrupt = false;
    m_channel = data & 0x0f;
    m_conversion_cycles = 1000; // MAME: 40 us at the C31's 25 MHz cycle rate.
    m_conversion_active = true;
}

std::uint8_t ADC0844::read() noexcept {
    m_interrupt = false;
    return m_result;
}

std::uint8_t ADC0844::clamp(int value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void ADC0844::advance_cycles(std::uint64_t cycles) noexcept {
    if (!m_conversion_active) return;
    if (cycles >= m_conversion_cycles) {
        m_conversion_cycles = 0;
        m_conversion_active = false;
        complete_conversion();
    } else {
        m_conversion_cycles -= cycles;
    }
}

void ADC0844::complete_conversion() noexcept {
    const auto c1 = static_cast<int>(m_channels[0]);
    const auto c2 = static_cast<int>(m_channels[1]);
    const auto c3 = static_cast<int>(m_channels[2]);
    const auto c4 = static_cast<int>(m_channels[3]);
    switch (m_channel) {
    case 0x00: case 0x08: m_result = clamp(0xff - (c2 - c1)); break;
    case 0x01: case 0x09: m_result = clamp(0xff - (c1 - c2)); break;
    case 0x02: case 0x0a: m_result = clamp(0xff - (c4 - c3)); break;
    case 0x03: case 0x0b: m_result = clamp(0xff - (c3 - c4)); break;
    case 0x04: m_result = static_cast<std::uint8_t>(c1); break;
    case 0x05: m_result = static_cast<std::uint8_t>(c2); break;
    case 0x06: m_result = static_cast<std::uint8_t>(c3); break;
    case 0x07: m_result = static_cast<std::uint8_t>(c4); break;
    case 0x0c: m_result = clamp(0xff - (c4 - c1)); break;
    case 0x0d: m_result = clamp(0xff - (c4 - c2)); break;
    case 0x0e: m_result = clamp(0xff - (c4 - c3)); break;
    case 0x0f: m_result = 0x00; break;
    }
    m_interrupt = true;
}

} // namespace mamerecomp::chips
