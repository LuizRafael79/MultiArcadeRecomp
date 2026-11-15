#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace multiarcaderecomp {

enum class SegaVideoBackend { Software, OpenGL };

struct SegaVideoProfile {
    std::uint64_t background_ns = 0, foreground_ns = 0, text_ns = 0, sprites_ns = 0, render_ns = 0;
    std::uint64_t page_pixel_visits = 0, page_pen_lookups = 0;
    std::uint64_t background_palette_writes = 0, foreground_palette_writes = 0;
    std::uint64_t text_pixel_visits = 0, text_pen_lookups = 0, text_palette_writes = 0;
    std::uint64_t priority_marks = 0, sprite_words_read = 0, sprite_pen_lookups = 0;
    std::uint64_t sprite_pixels_rasterized = 0, sprite_pixels_mixed = 0, sprite_shadow_pixels = 0;
    std::uint64_t gpu_setup_ns = 0, gpu_upload_ns = 0, gpu_dispatch_readback_ns = 0;
};

struct SegaVideoFrame {
    static constexpr int width = 320;
    static constexpr int height = 224;
    std::vector<std::uint16_t> palette_indices;
    std::vector<std::uint8_t> tile_priorities;
    std::vector<std::uint8_t> shadow_pixels;
    std::array<std::uint32_t, 64> vdp_palette_rgb{};
};

} // namespace multiarcaderecomp
