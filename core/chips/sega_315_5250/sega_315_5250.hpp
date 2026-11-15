#pragma once

#include <array>
#include <cstdint>

namespace mamerecomp::chips {

// Sega 315-5250 compare/timer register block. Each physical instance keeps
// its own comparison history, as on the System16B ROM board.
class Sega3155250CompareTimer {
public:
    std::uint16_t read(std::uint32_t word_offset) const;
    void write(std::uint32_t word_offset, std::uint16_t data, std::uint8_t lanes);

private:
    void execute_compare(bool update_history);

    std::array<std::uint16_t, 16> m_registers{};
    std::uint8_t m_history_bit = 0;
};

} // namespace mamerecomp::chips
