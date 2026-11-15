#include "boards/sega/system16a/video.hpp"
#include "chips/sega_315_5049/sega_315_5049.hpp"
#include "chips/sega_315_5011/sega_315_5011.hpp"
#include "chips/sega_315_5012/sega_315_5012.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace mamerecomp {
namespace {
void draw_16a_sprites(System16AFrame& frame, std::span<const std::uint8_t> sprite_rom,
    std::span<const std::uint8_t> sprite_ram, const System16ARenderOptions& options,
    System16AVideoProfile* profile) {
    const chips::sega3155011::Comparator line_comparator;
    auto output = chips::sega3155012::rasterize(line_comparator, sprite_rom, sprite_ram, {options.flip});
    if (profile) {
        profile->sprite_words_read += output.stats.words_read;
        profile->sprite_pen_lookups += output.stats.pen_lookups;
        profile->sprite_pixels_rasterized += output.stats.pixels_rasterized;
    }
    for (std::size_t pixel = 0; pixel < output.pixels.size(); ++pixel) {
        const std::uint16_t sprite = output.pixels[pixel];
        if (sprite == chips::sega3155012::kTransparentPixel) continue;
        const unsigned priority = sprite >> 10;
        if ((1U << priority) <= frame.tile_priorities[pixel]) continue;
        if (profile) ++profile->sprite_pixels_mixed;
        if ((sprite & 0x03f0U) == 0x03f0U) {
            frame.shadow_pixels[pixel] = 1;
            if (profile) ++profile->sprite_shadow_pixels;
        } else {
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(0x400U | (sprite & 0x03ffU));
            frame.shadow_pixels[pixel] = 0;
        }
    }
}

System16AFrame make_empty_frame() {
    System16AFrame frame;
    const auto pixels = static_cast<std::size_t>(System16AFrame::width * System16AFrame::height);
    frame.palette_indices.assign(pixels, 0);
    frame.tile_priorities.assign(pixels, 0);
    frame.shadow_pixels.assign(pixels, 0);
    return frame;
}

void apply_315_5049_pass(System16AFrame& frame, std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    const System16ARenderOptions& options, chips::sega3155049::Layer layer,
    unsigned category, bool opaque, std::uint8_t priority_mark, System16AVideoProfile* profile) {
    const chips::sega3155049::TilemapState state{
        options.flip, options.row_scroll_enabled, options.column_scroll_enabled};
    auto result = chips::sega3155049::render_layer(decoded_tiles, tile_ram, text_ram,
        state, layer, category, opaque, priority_mark);
    for (std::size_t pixel = 0; pixel < result.palette_indices.size(); ++pixel) {
        if (result.palette_indices[pixel] != chips::sega3155049::kTransparentPixel)
            frame.palette_indices[pixel] = result.palette_indices[pixel];
        frame.tile_priorities[pixel] |= result.priority_marks[pixel];
    }
    if (!profile) return;
    if (layer == chips::sega3155049::Layer::Text) {
        profile->text_pixel_visits += result.stats.output_pixel_visits;
        profile->text_pen_lookups += result.stats.tile_pen_lookups;
        profile->text_palette_writes += result.stats.palette_writes;
    } else {
        profile->page_pixel_visits += result.stats.output_pixel_visits;
        profile->page_pen_lookups += result.stats.tile_pen_lookups;
        if (layer == chips::sega3155049::Layer::Background)
            profile->background_palette_writes += result.stats.palette_writes;
        else
            profile->foreground_palette_writes += result.stats.palette_writes;
    }
    profile->priority_marks += result.stats.priority_marks;
}

void render_16a_tilemaps_software(System16AFrame& frame, std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    const System16ARenderOptions& options, std::vector<System16AFrame>* stage_outputs,
    System16AVideoProfile* profile) {
    if (!options.display_enable) return;
    using Clock = std::chrono::steady_clock;
    auto stage_start = profile ? Clock::now() : Clock::time_point{};
    for (unsigned category = 0; category < 2; ++category)
        apply_315_5049_pass(frame, decoded_tiles, tile_ram, text_ram, options,
            chips::sega3155049::Layer::Background, category, true, 0, profile);
    for (unsigned category = 0; category < 2; ++category)
        apply_315_5049_pass(frame, decoded_tiles, tile_ram, text_ram, options,
            chips::sega3155049::Layer::Background, category, false,
            static_cast<std::uint8_t>(1U << category), profile);
    if (profile) {
        profile->background_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_start).count();
        stage_start = Clock::now();
    }
    if (stage_outputs) stage_outputs->push_back(frame);
    for (unsigned category = 0; category < 2; ++category)
        apply_315_5049_pass(frame, decoded_tiles, tile_ram, text_ram, options,
            chips::sega3155049::Layer::Foreground, category, false,
            static_cast<std::uint8_t>(2U << category), profile);
    if (profile) {
        profile->foreground_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_start).count();
        stage_start = Clock::now();
    }
    if (stage_outputs) stage_outputs->push_back(frame);
    for (unsigned category = 0; category < 2; ++category)
        apply_315_5049_pass(frame, decoded_tiles, tile_ram, text_ram, options,
            chips::sega3155049::Layer::Text, category, false,
            static_cast<std::uint8_t>(4U << category), profile);
    if (profile) profile->text_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_start).count();
    if (stage_outputs) stage_outputs->push_back(frame);
}

} // namespace

System16AFrame render_system16a_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<const std::uint8_t> sprite_ram,
    std::span<const std::uint8_t> palette_ram, System16ARenderOptions options,
    std::vector<System16AFrame>* stage_outputs, System16AVideoProfile* profile,
    System16AVideoBackend backend) {
    if (decoded_tiles.empty() || sprite_rom.empty() || tile_ram.size() < 0x8000 || text_ram.size() < 0x1000
        || sprite_ram.size() < 0x800 || palette_ram.size() < 0x1000) {
        throw std::invalid_argument("System 16A frame rendering requires decoded graphics and complete video/palette RAM");
    }
    using Clock = std::chrono::steady_clock;
    const auto render_start = profile ? Clock::now() : Clock::time_point{};
    if (profile) *profile = {};
    System16AFrame frame = make_empty_frame();
    if (backend == System16AVideoBackend::OpenGL) {
        if (!system16a_opengl_backend_available())
            throw std::runtime_error("OpenGL video backend was not built; install SDL3/OpenGL development files and reconfigure");
        frame = render_system16a_tilemaps_opengl(decoded_tiles, tile_ram, text_ram, options, profile);
        if (stage_outputs && options.display_enable) {
            auto cpu_stages = make_empty_frame();
            render_16a_tilemaps_software(cpu_stages, decoded_tiles, tile_ram, text_ram, options, stage_outputs, nullptr);
        }
    } else {
        render_16a_tilemaps_software(frame, decoded_tiles, tile_ram, text_ram, options, stage_outputs, profile);
    }
    // MAME fills a disabled System 16A display with palette_device::black_pen().
    // The palette has 4096 entries, so that reserved pen is 4096, just beyond
    // the 2048 hardware colors represented by palette RAM. Keep that state
    // explicit for both the PPM and SDL/RGB output paths.
    if (!options.display_enable) {
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
        std::fill(frame.tile_priorities.begin(), frame.tile_priorities.end(), 0);
        std::fill(frame.shadow_pixels.begin(), frame.shadow_pixels.end(), 0);
    }
    if (options.display_enable) {
        const auto sprite_start = profile ? Clock::now() : Clock::time_point{};
        draw_16a_sprites(frame, sprite_rom, sprite_ram, options, profile);
        if (profile) profile->sprites_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - sprite_start).count();
        if (stage_outputs) stage_outputs->push_back(frame);
    }
    if (profile) profile->render_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - render_start).count();
    return frame;
}

#ifndef MAMERECOMP_HAS_OPENGL_VIDEO
bool system16a_opengl_backend_available() { return false; }
System16AFrame render_system16a_tilemaps_opengl(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
    std::span<const std::uint8_t>, System16ARenderOptions, System16AVideoProfile*) {
    throw std::runtime_error("OpenGL video backend was not built; install SDL3/OpenGL development files and reconfigure");
}
#endif

} // namespace mamerecomp
