#pragma once

#include "boards/sega/common/video_frame.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mamerecomp {

using System16AFrame = SegaVideoFrame;
using System16AVideoBackend = SegaVideoBackend;
using System16AVideoProfile = SegaVideoProfile;

struct System16ARenderOptions {
    bool display_enable = true;
    bool flip = false;
    bool row_scroll_enabled = false;
    bool column_scroll_enabled = false;
};

System16AFrame render_system16a_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<const std::uint8_t> sprite_ram,
    std::span<const std::uint8_t> palette_ram, System16ARenderOptions options = {},
    std::vector<System16AFrame>* stage_outputs = nullptr, System16AVideoProfile* profile = nullptr,
    System16AVideoBackend backend = System16AVideoBackend::Software);
bool system16a_opengl_backend_available();
System16AFrame render_system16a_tilemaps_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    System16ARenderOptions options, System16AVideoProfile* profile = nullptr);

} // namespace mamerecomp
