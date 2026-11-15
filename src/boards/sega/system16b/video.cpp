#include "boards/sega/system16b/video.hpp"

#include "sega_315_5196.hpp"

#include <stdexcept>
#include <utility>

namespace multiarcaderecomp {
namespace {
System16BFrame frame_from_tilemap_output(const chips::sega3155197::TilemapOutput& output) {
    System16BFrame frame;
    frame.palette_indices = output.palette_indices;
    frame.tile_priorities = output.priorities;
    frame.shadow_pixels = output.shadow_pixels;
    return frame;
}
System16BFrame frame_from_tilemap_output(chips::sega3155197::TilemapOutput&& output) {
    System16BFrame frame;
    frame.palette_indices = std::move(output.palette_indices);
    frame.tile_priorities = std::move(output.priorities);
    frame.shadow_pixels = std::move(output.shadow_pixels);
    return frame;
}
}

System16BFrame render_system16b_tilemaps_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state, const System16BLayerHook& after_stage,
    chips::sega3155197::TilemapRenderer* renderer) {
    const auto output = (renderer ? renderer->render(decoded_tiles, tile_ram, text_ram, state,
        [&](chips::sega3155197::TilemapOutput& stage_output, unsigned stage) {
            if (!after_stage) return;
            auto frame = frame_from_tilemap_output(stage_output);
            after_stage(frame, stage);
            stage_output.palette_indices = std::move(frame.palette_indices);
            stage_output.priorities = std::move(frame.tile_priorities);
            stage_output.shadow_pixels = std::move(frame.shadow_pixels);
        }) : chips::sega3155197::render_tilemaps(decoded_tiles, tile_ram, text_ram, state,
        [&](chips::sega3155197::TilemapOutput& stage_output, unsigned stage) {
            if (!after_stage) return;
            auto frame = frame_from_tilemap_output(stage_output);
            after_stage(frame, stage);
            stage_output.palette_indices = std::move(frame.palette_indices);
            stage_output.priorities = std::move(frame.tile_priorities);
            stage_output.shadow_pixels = std::move(frame.shadow_pixels);
        }));
    return frame_from_tilemap_output(std::move(output));
}

std::vector<std::uint16_t> rasterize_system16b_sprites_cpu(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks, bool flip) {
    return chips::sega3155196::rasterize_sprites(sprite_rom, sprite_ram, sprite_banks, flip);
}

void mix_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels) {
    constexpr std::uint16_t shadow_palette_base = 0x800;
    constexpr std::uint16_t sprite_palette_base = 0x400;
    if (frame.palette_indices.size() != static_cast<std::size_t>(System16BFrame::width * System16BFrame::height)
        || frame.tile_priorities.size() != frame.palette_indices.size()
        || frame.shadow_pixels.size() != frame.palette_indices.size()
        || sprite_pixels.size() != frame.palette_indices.size()) return;
    for (std::size_t pixel = 0; pixel < sprite_pixels.size(); ++pixel) {
        const std::uint16_t sprite = sprite_pixels[pixel];
        if (sprite == 0xffffU) continue;
        const unsigned priority = (sprite >> 10) & 3U;
        if ((1U << priority) <= frame.tile_priorities[pixel]) continue;
        if ((sprite & 0x03f0U) == 0x03f0U) {
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(frame.palette_indices[pixel] + shadow_palette_base);
            frame.shadow_pixels[pixel] = 1;
        } else {
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(sprite_palette_base | (sprite & 0x03ffU));
        }
    }
}

void draw_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks, bool flip) {
    const auto sprite_pixels = rasterize_system16b_sprites_cpu(sprite_rom, sprite_ram, sprite_banks, flip);
    mix_system16b_sprites_cpu(frame, sprite_pixels);
}

System16BFrame render_system16b_frame_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    chips::sega3155197::TilemapState state, std::span<const std::uint8_t> sprite_banks,
    const System16BLayerHook& after_stage, chips::sega3155197::TilemapRenderer* renderer) {
    auto frame = render_system16b_tilemaps_cpu(decoded_tiles, tile_ram, text_ram, state, after_stage, renderer);
    if (state.display_enable)
        draw_system16b_sprites_cpu(frame, sprite_rom, sprite_ram, sprite_banks, state.flip);
    else
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
    return frame;
}


bool system16b_opengl_backend_available() {
#ifdef MULTIARCADERECOMP_HAS_OPENGL_VIDEO
    return true;
#else
    return false;
#endif
}

System16BFrame render_system16b_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    chips::sega3155197::TilemapState state, std::span<const std::uint8_t> sprite_banks, bool use_opengl,
    chips::sega3155197::TilemapRenderer* renderer) {
    if (use_opengl && !system16b_opengl_backend_available())
        throw std::runtime_error("System 16B OpenGL renderer was not built; install SDL3/OpenGL development files and reconfigure");
    auto sprite_pixels = state.display_enable
        ? rasterize_system16b_sprites_cpu(sprite_rom, sprite_ram, sprite_banks, state.flip)
        : std::vector<std::uint16_t>{};
    if (use_opengl && state.display_enable)
        return render_system16b_frame_opengl(decoded_tiles, tile_ram, text_ram, state, sprite_pixels);

    System16BFrame frame = use_opengl
        ? render_system16b_tilemaps_opengl(decoded_tiles, tile_ram, text_ram, state)
        : render_system16b_tilemaps_cpu(decoded_tiles, tile_ram, text_ram, state, {}, renderer);
    if (state.display_enable) {
        if (use_opengl) mix_system16b_sprites_opengl(frame, sprite_pixels);
        else mix_system16b_sprites_cpu(frame, sprite_pixels);
    } else {
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
    }
    return frame;
}

#ifndef MULTIARCADERECOMP_HAS_OPENGL_VIDEO
System16BFrame render_system16b_tilemaps_opengl(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
    std::span<const std::uint8_t>, chips::sega3155197::TilemapState) {
    throw std::runtime_error("System 16B OpenGL renderer was not built; install SDL3/OpenGL development files and reconfigure");
}
void mix_system16b_sprites_opengl(System16BFrame&, std::span<const std::uint16_t>) {
    throw std::runtime_error("System 16B OpenGL sprite mixer was not built; install SDL3/OpenGL development files and reconfigure");
}
System16BFrame render_system16b_frame_opengl(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
    std::span<const std::uint8_t>, chips::sega3155197::TilemapState, std::span<const std::uint16_t>) {
    throw std::runtime_error("System 16B OpenGL renderer was not built; install SDL3/OpenGL development files and reconfigure");
}
#endif

} // namespace multiarcaderecomp
