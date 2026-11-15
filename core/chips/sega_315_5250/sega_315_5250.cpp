#include "sega_315_5250.hpp"

#include <algorithm>

namespace mamerecomp::chips {

std::uint16_t Sega3155250CompareTimer::read(std::uint32_t offset) const {
    switch (offset & 0x0fU) {
    case 0x0: return m_registers[0];
    case 0x1: case 0x5: return m_registers[1];
    case 0x2: case 0x6: return m_registers[2];
    case 0x3: return m_registers[3];
    case 0x4: return m_registers[4];
    case 0x7: return m_registers[7];
    default: return 0xffff;
    }
}

void Sega3155250CompareTimer::execute_compare(bool update_history) {
    const auto value = static_cast<std::int16_t>(m_registers[2]);
    const auto low = static_cast<std::int16_t>(m_registers[0]);
    const auto high = static_cast<std::int16_t>(m_registers[1]);
    const auto minimum = std::min(low, high);
    const auto maximum = std::max(low, high);
    m_registers[7] = value < minimum ? static_cast<std::uint16_t>(minimum)
        : value > maximum ? static_cast<std::uint16_t>(maximum) : m_registers[2];
    m_registers[3] = value < minimum ? 0x8000U : value > maximum ? 0x4000U : 0U;
    if (update_history) {
        if (m_history_bit < 16U && m_registers[3] == 0)
            m_registers[4] |= static_cast<std::uint16_t>(1U << m_history_bit);
        ++m_history_bit;
    }
}

void Sega3155250CompareTimer::write(std::uint32_t offset, std::uint16_t data, std::uint8_t lanes) {
    const auto combine = [&](std::uint16_t& reg) {
        if (lanes & 2U) reg = static_cast<std::uint16_t>((reg & 0x00ffU) | (data & 0xff00U));
        if (lanes & 1U) reg = static_cast<std::uint16_t>((reg & 0xff00U) | (data & 0x00ffU));
    };
    switch (offset & 0x0fU) {
    case 0x0: case 0x1:
        combine(m_registers[offset & 1U]);
        execute_compare(false);
        break;
    case 0x2:
        combine(m_registers[2]);
        execute_compare(true);
        break;
    case 0x4: m_registers[4] = 0; m_history_bit = 0; break;
    case 0x6:
        combine(m_registers[2]);
        execute_compare(false);
        break;
    case 0x8: case 0xc: combine(m_registers[8]); break;
    case 0xa: case 0xe: combine(m_registers[10]); break;
    // 0xb/0xf feed the sound CPU IRQ path; the board wires that separately.
    case 0xb: case 0xf: break;
    default: break;
    }
}

} // namespace mamerecomp::chips
