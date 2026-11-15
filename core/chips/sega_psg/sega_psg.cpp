#include "sega_psg.hpp"

#include <algorithm>
#include <cmath>

namespace mamerecomp::chips {
namespace {
constexpr std::uint16_t kNoiseFeedback = 0x8000;
constexpr std::uint16_t kNoiseTap0 = 0x0001;
constexpr std::uint16_t kNoiseTap3 = 0x0008;
constexpr std::uint8_t kClockDivider = 16;
constexpr std::int32_t kChannelPeak = 8191;
}

void SegaPsg::reset() noexcept {
    m_registers.fill(0);
    for (unsigned reg = 1; reg < m_registers.size(); reg += 2) m_registers[reg] = 0x0f;
    m_counters.fill(0);
    m_outputs.fill(0);
    m_noise_lfsr = kNoiseFeedback;
    m_last_register = 3;
    m_clock_divider = 0;
    m_writes = 0;
    for (unsigned volume = 0; volume < 15; ++volume) {
        const double attenuation = std::pow(10.0, static_cast<double>(volume * 2U) / 20.0);
        m_volume[volume] = static_cast<std::int32_t>(static_cast<double>(kChannelPeak) / attenuation);
    }
    m_volume[15] = 0;
}

void SegaPsg::write(std::uint8_t value) noexcept {
    ++m_writes;
    if (value & 0x80U) {
        m_last_register = static_cast<std::uint8_t>((value >> 4U) & 7U);
        m_registers[m_last_register] = static_cast<std::uint16_t>(
            (m_registers[m_last_register] & 0x03f0U) | (value & 0x0fU));
    } else if (m_last_register < 6 && (m_last_register & 1U) == 0) {
        m_registers[m_last_register] = static_cast<std::uint16_t>(
            (m_registers[m_last_register] & 0x000fU) | ((value & 0x3fU) << 4U));
    } else if (m_last_register & 1U) {
        m_registers[m_last_register] = value & 0x0fU;
    }

    // Tone writes change the next counter reload without resetting the current
    // countdown, preserving oscillator phase across register updates.
    if (m_last_register == 6 && (value & 0x80U)) {
        m_noise_lfsr = kNoiseFeedback;
    }
}

void SegaPsg::advance_clocks(std::uint32_t clocks) noexcept {
    while (clocks-- != 0) {
        if (++m_clock_divider < kClockDivider) continue;
        m_clock_divider = 0;
        clock_divided_tick();
    }
}

std::int16_t SegaPsg::output() const noexcept {
    std::int32_t mixed = 0;
    for (unsigned channel = 0; channel < 4; ++channel) {
        const auto volume = m_volume[m_registers[channel * 2U + 1U] & 0x0fU];
        mixed += m_outputs[channel] ? volume : -volume;
    }
    return static_cast<std::int16_t>(std::clamp(mixed, -32768, 32767));
}

void SegaPsg::clock_divided_tick() noexcept {
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (--m_counters[channel] <= 0) {
            m_outputs[channel] ^= 1U;
            m_counters[channel] = m_registers[channel * 2U] & 0x03ffU;
        }
    }
    if (--m_counters[3] <= 0) {
        const bool tap0 = (m_noise_lfsr & kNoiseTap0) != 0;
        const bool tap3 = (m_noise_lfsr & kNoiseTap3) != 0;
        const bool white_noise = (m_registers[6] & 0x04U) != 0;
        const bool feedback = tap0 != (tap3 && white_noise);
        m_noise_lfsr >>= 1U;
        if (feedback) m_noise_lfsr |= kNoiseFeedback;
        m_outputs[3] = static_cast<std::uint8_t>(m_noise_lfsr & 1U);
        m_counters[3] = std::max<std::int32_t>(1,
            ((m_registers[6] & 3U) == 3U)
                ? static_cast<std::int32_t>(m_registers[4] & 0x03ffU) * 2
                : (1 << (5U + (m_registers[6] & 3U))));
    }
}

} // namespace mamerecomp::chips
