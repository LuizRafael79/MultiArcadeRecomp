#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace multiarcaderecomp::chips::sega3155049 {

inline constexpr unsigned kFrameWidth = 320;
inline constexpr unsigned kFrameHeight = 224;
inline constexpr std::uint16_t kTransparentPixel = 0xffff;

enum class Layer : std::uint8_t { Background, Foreground, Text };

struct TilemapState {
    bool flip = false;
    bool row_scroll_enabled = false;
    bool column_scroll_enabled = false;
};

struct TilemapStats {
    std::uint64_t output_pixel_visits = 0;
    std::uint64_t tile_pen_lookups = 0;
    std::uint64_t palette_writes = 0;
    std::uint64_t priority_marks = 0;
};

struct TilemapOutput {
    std::vector<std::uint16_t> palette_indices;
    std::vector<std::uint8_t> priority_marks;
    TilemapStats stats;
};

// Model one 315-5049 tilemap generator pass. Board RAM and scroll mode are
// supplied by the board; the result is color/priority data for the compositor.
TilemapOutput render_layer(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    TilemapState state, Layer layer, unsigned category, bool opaque,
    std::uint8_t priority_mark);

} // namespace multiarcaderecomp::chips::sega3155049
