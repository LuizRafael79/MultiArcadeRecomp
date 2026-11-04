#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace mamerecomp::cps1::detail {

extern std::array<std::uint8_t, 0x40000> g_gfxram;
std::uint16_t read_gfxram_word(std::size_t byte_offset);
void write_gfxram_word(std::size_t byte_offset, std::uint16_t value);

} // namespace mamerecomp::cps1::detail
