#include "sega_315_5248.hpp"

namespace mamerecomp::chips {

std::uint16_t Sega3155248Multiplier::read(std::uint32_t offset) const {
    const auto reg = offset & 3U;
    if (reg < 2U) return m_operands[reg];
    const auto product = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(static_cast<std::int16_t>(m_operands[0]))
        * static_cast<std::int32_t>(static_cast<std::int16_t>(m_operands[1])));
    return reg == 2U ? static_cast<std::uint16_t>(product >> 16)
                     : static_cast<std::uint16_t>(product);
}

void Sega3155248Multiplier::write(std::uint32_t offset, std::uint16_t data, std::uint8_t lanes) {
    auto& operand = m_operands[offset & 1U];
    if (lanes & 2U) operand = static_cast<std::uint16_t>((operand & 0x00ffU) | (data & 0xff00U));
    if (lanes & 1U) operand = static_cast<std::uint16_t>((operand & 0xff00U) | (data & 0x00ffU));
}

} // namespace mamerecomp::chips
