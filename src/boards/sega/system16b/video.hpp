#pragma once

#include "boards/sega/common/video_frame.hpp"
#include "sega_315_5197.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mamerecomp {

using System16BFrame = SegaVideoFrame;
using System16BLayerHook = std::function<void(System16BFrame&, unsigned)>;

System16BFrame render_system16b_tilemaps_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state = {}, const System16BLayerHook& after_stage = {},
    chips::sega3155197::TilemapRenderer* renderer = nullptr);
void draw_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks = {}, bool flip = false);
std::vector<std::uint16_t> rasterize_system16b_sprites_cpu(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks = {}, bool flip = false);
void mix_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels);
System16BFrame render_system16b_frame_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    chips::sega3155197::TilemapState state = {}, std::span<const std::uint8_t> sprite_banks = {},
    const System16BLayerHook& after_stage = {}, chips::sega3155197::TilemapRenderer* renderer = nullptr);
bool system16b_opengl_backend_available();
System16BFrame render_system16b_tilemaps_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state = {});
System16BFrame render_system16b_frame_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state, std::span<const std::uint16_t> sprite_pixels);
void mix_system16b_sprites_opengl(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels);
System16BFrame render_system16b_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    chips::sega3155197::TilemapState state = {}, std::span<const std::uint8_t> sprite_banks = {},
    bool use_opengl = false, chips::sega3155197::TilemapRenderer* renderer = nullptr);

} // namespace mamerecomp
