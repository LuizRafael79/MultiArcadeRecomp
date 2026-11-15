#include "chips/sega_315_5197/sega_315_5197.hpp"
#include "boards/sega/common/video_output.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace mamerecomp;
using namespace mamerecomp::chips::sega3155197;

static void assert_same(const TilemapOutput& a, const TilemapOutput& b) {
    assert(a.palette_indices == b.palette_indices);
    assert(a.priorities == b.priorities);
    assert(a.shadow_pixels == b.shadow_pixels);
}

int main() {
    std::vector<std::uint8_t> tiles(3 * 64, 0);
    std::fill(tiles.begin() + 64, tiles.begin() + 128, 1);
    std::fill(tiles.begin() + 128, tiles.end(), 2);
    std::vector<std::uint8_t> tile_ram(0x10000, 0), text_ram(0x1000, 0);
    TilemapState state;
    TilemapRenderer cached;
    auto frame = cached.render(tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 0);

    // Pixel (0,0) maps to tile word 24 with the default scroll settings.
    tile_ram[49] = 1;
    frame = cached.render(tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 1);
    assert_same(frame, render_tilemaps(tiles, tile_ram, text_ram, state));

    text_ram[49] = 2;
    frame = cached.render(tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 2);
    assert_same(frame, render_tilemaps(tiles, tile_ram, text_ram, state));

    state.banks[0] = 1;
    frame = cached.render(tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 0);
    assert_same(frame, render_tilemaps(tiles, tile_ram, text_ram, state));
    state.banks[0] = 0;

    auto other_tiles = tiles;
    std::fill(other_tiles.begin() + 128, other_tiles.end(), 3);
    frame = cached.render(other_tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 3);
    std::fill(other_tiles.begin() + 128, other_tiles.end(), 4);
    cached.invalidate_graphics();
    frame = cached.render(other_tiles, tile_ram, text_ram, state);
    assert(frame.palette_indices[0] == 4);
    assert_same(frame, render_tilemaps(other_tiles, tile_ram, text_ram, state));

    SegaVideoFrame video;
    constexpr std::size_t pixels = SegaVideoFrame::width * SegaVideoFrame::height;
    video.palette_indices.assign(pixels, 1);
    video.shadow_pixels.assign(pixels, 0);
    std::vector<std::uint8_t> palette(0x1000, 0);
    palette[3] = 0x0f;
    SegaFrameRgbConverter converter;
    auto rgb = converter.convert(video, palette);
    assert(rgb[0] == 247 && rgb[1] == 0 && rgb[2] == 0);
    video.palette_indices[0] = 0x801;
    rgb = converter.convert(video, palette);
    assert(rgb[0] == 193 && rgb[1] == 0 && rgb[2] == 0);
    palette[2] = 0x80;
    rgb = converter.convert(video, palette);
    assert(rgb[0] == 248 && rgb[1] == 55 && rgb[2] == 55);
    video.palette_indices[0] = 0x1000;
    video.vdp_palette_rgb[0] = 0x123456;
    rgb = converter.convert(video, palette);
    assert(rgb[0] == 0x12 && rgb[1] == 0x34 && rgb[2] == 0x56);
}
