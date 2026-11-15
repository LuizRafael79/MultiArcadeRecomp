#pragma once

#include "chips/sega_315_5011/sega_315_5011.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace multiarcaderecomp::chips::sega3155012 {

inline constexpr std::uint16_t kTransparentPixel = 0xffff;

struct State {
    bool flip = false;
};

struct Stats {
    std::uint64_t words_read = 0;
    std::uint64_t pen_lookups = 0;
    std::uint64_t pixels_rasterized = 0;
};

struct Output {
    std::vector<std::uint16_t> pixels;
    Stats stats;
};

// Rasterize sprite descriptors and pixel data. The board supplies the 315-5011
// comparator instance and remains responsible for final palette/priority mixing.
Output rasterize(const sega3155011::Comparator& line_comparator,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> sprite_ram, State state);

} // namespace multiarcaderecomp::chips::sega3155012
