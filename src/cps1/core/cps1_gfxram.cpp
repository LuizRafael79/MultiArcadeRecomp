#include "cps1_gfxram.hpp"

namespace multiarcaderecomp::cps1::detail {

std::array<std::uint8_t, 0x40000> g_gfxram{};

std::uint16_t read_gfxram_word(std::size_t byte_offset) {
    byte_offset &= 0x3fffe;
    return static_cast<std::uint16_t>((g_gfxram[byte_offset] << 8) | g_gfxram[byte_offset + 1]);
}

void write_gfxram_word(std::size_t byte_offset, std::uint16_t value) {
    byte_offset &= 0x3fffe;
    g_gfxram[byte_offset] = static_cast<std::uint8_t>(value >> 8);
    g_gfxram[byte_offset + 1] = static_cast<std::uint8_t>(value);
}

} // namespace multiarcaderecomp::cps1::detail
