#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mamerecomp {

std::vector<std::uint8_t> decode_sega_tiles_3bpp(std::span<const std::uint8_t> region);

} // namespace mamerecomp
