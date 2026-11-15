#include "rf5c68.hpp"

#include <algorithm>
#include <cstdlib>

namespace multiarcaderecomp::chips {

void Rf5c68::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
    offset &= 0x0fU;
    auto& channel = m_channels[m_channel_bank];
    switch (offset) {
    case 0x00: channel.env = value; break;
    case 0x01: channel.pan = value; break;
    case 0x02: channel.step = static_cast<std::uint16_t>((channel.step & 0xff00U) | value); break;
    case 0x03: channel.step = static_cast<std::uint16_t>((channel.step & 0x00ffU) | (std::uint16_t(value) << 8)); break;
    case 0x04: channel.loop = static_cast<std::uint16_t>((channel.loop & 0xff00U) | value); break;
    case 0x05: channel.loop = static_cast<std::uint16_t>((channel.loop & 0x00ffU) | (std::uint16_t(value) << 8)); break;
    case 0x06:
        channel.start = value;
        if (!channel.enabled) channel.address = std::uint32_t(value) << 19;
        break;
    case 0x07:
        m_enabled = (value & 0x80U) != 0;
        if (value & 0x40U) m_channel_bank = value & 7U;
        else m_write_bank = static_cast<std::uint16_t>((value & 0x0fU) << 12);
        break;
    case 0x08:
        for (unsigned index = 0; index < m_channels.size(); ++index) {
            auto& item = m_channels[index];
            item.enabled = (value & (1U << index)) == 0;
            if (!item.enabled) item.address = std::uint32_t(item.start) << 19;
        }
        update_enabled_channels();
        break;
    default: break;
    }
}

std::uint8_t Rf5c68::read_sample_ram(std::uint16_t offset) const noexcept {
    return m_ram[m_write_bank | (offset & 0x0fffU)];
}

void Rf5c68::write_sample_ram(std::uint16_t offset, std::uint8_t value) noexcept {
    m_ram[m_write_bank | (offset & 0x0fffU)] = value;
    ++m_sample_ram_writes;
}

void Rf5c68::advance_clocks(std::uint32_t clocks) noexcept {
    m_clock_fraction += clocks;
    while (m_clock_fraction >= 384U) {
        m_clock_fraction -= 384U;
        generate_sample();
    }
}

void Rf5c68::generate_sample() noexcept {
    std::int32_t left = 0;
    std::int32_t right = 0;
    if (m_enabled) {
        for (auto& channel : m_channels) {
            if (!channel.enabled) continue;
            std::uint8_t sample = m_ram[(channel.address >> 11) & 0xffffU];
            if (sample == 0xffU) {
                channel.address = std::uint32_t(channel.loop) << 11;
                sample = m_ram[(channel.address >> 11) & 0xffffU];
                if (sample == 0xffU) {
                    channel.enabled = false;
                    continue;
                }
            }
            channel.address += channel.step;
            const int magnitude = sample & 0x7fU;
            const int sign = (sample & 0x80U) ? 1 : -1;
            const int left_gain = static_cast<int>(channel.pan & 0x0fU) * channel.env;
            const int right_gain = static_cast<int>((channel.pan >> 4) & 0x0fU) * channel.env;
            left += sign * magnitude * left_gain / 32;
            right += sign * magnitude * right_gain / 32;
        }
    }
    update_enabled_channels();
    // The RF5C68 output stage has a 10-bit DAC; the lower six bits are clear.
    left = std::clamp(left, -32768, 32767) & ~0x3f;
    right = std::clamp(right, -32768, 32767) & ~0x3f;
    m_output = {static_cast<std::int16_t>(left), static_cast<std::int16_t>(right)};
    m_peak = std::max<std::uint16_t>(m_peak, static_cast<std::uint16_t>(
        std::max(std::abs(left), std::abs(right))));
}

void Rf5c68::update_enabled_channels() noexcept {
    m_enabled_channels = 0;
    for (unsigned index = 0; index < m_channels.size(); ++index)
        if (m_channels[index].enabled)
            m_enabled_channels |= static_cast<std::uint8_t>(1U << index);
}

} // namespace multiarcaderecomp::chips
