#pragma once

#include <array>
#include <cstdint>

namespace mamerecomp::chips {

// Sega 315-5248 signed 16x16 multiplier used on selected System16B ROM boards.
class Sega3155248Multiplier {
public:
    std::uint16_t read(std::uint32_t word_offset) const;
    void write(std::uint32_t word_offset, std::uint16_t data, std::uint8_t lanes);

private:
    std::array<std::uint16_t, 2> m_operands{};
};

} // namespace mamerecomp::chips
