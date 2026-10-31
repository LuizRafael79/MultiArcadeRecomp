#include "system16_video.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef MAMERECOMP_HAS_OPENGL_VIDEO
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#endif

namespace mamerecomp {
namespace {
std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint8_t tile_pen(std::span<const std::uint8_t> tiles, unsigned code, unsigned x, unsigned y);
}

std::vector<std::uint8_t> decode_system16_tiles_3bpp(std::span<const std::uint8_t> region)
{
    if (region.empty() || region.size() % 3 != 0) {
        throw std::invalid_argument("System 16 tile region must contain three equal bitplanes");
    }

    const std::size_t plane_bytes = region.size() / 3;
    const std::size_t tiles_per_plane = (plane_bytes * 8) / 64;
    if (tiles_per_plane == 0 || (plane_bytes * 8) % 64 != 0) {
        throw std::invalid_argument("System 16A tile plane size is not a whole number of 8x8 tiles");
    }

    std::vector<std::uint8_t> pixels(tiles_per_plane * 64, 0);
    // Bit offsets are required here: the per-tile/row/pixel offsets below are
    // measured in bits, while plane_bytes is a byte count.
    const std::size_t plane_offsets[] = {plane_bytes * 16, plane_bytes * 8, 0};
    for (std::size_t tile = 0; tile < tiles_per_plane; ++tile) {
        const std::size_t tile_bit_offset = tile * 64;
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                std::uint8_t pen = 0;
                for (unsigned plane = 0; plane < 3; ++plane) {
                    const std::size_t bit = plane_offsets[plane] + tile_bit_offset + y * 8 + x;
                    const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (bit & 7u));
                    if (region[bit >> 3] & mask) {
                        pen |= static_cast<std::uint8_t>(1u << (2u - plane));
                    }
                }
                pixels[tile * 64 + y * 8 + x] = pen;
            }
        }
    }
    return pixels;
}

std::vector<std::uint8_t> decode_system16a_tiles(std::span<const std::uint8_t> region)
{
    return decode_system16_tiles_3bpp(region);
}

std::vector<std::uint8_t> decode_system16b_tiles(std::span<const std::uint8_t> region)
{
    return decode_system16_tiles_3bpp(region);
}

System16BTileAttributes decode_system16b_tile_attributes(std::uint16_t word,
    System16BTileFormat format, std::span<const std::uint8_t> banks, std::uint16_t bank_size)
{
    if (banks.empty() || bank_size == 0) {
        throw std::invalid_argument("System 16B tile attributes require configured banks and a nonzero bank size");
    }

    System16BTileAttributes result;
    result.high_priority = (word & 0x8000U) != 0;
    unsigned bank_index = 0;
    switch (format) {
    case System16BTileFormat::Tile:
        result.code = word & 0x1fffU;
        result.color = static_cast<std::uint8_t>((word >> 6) & 0x7fU);
        bank_index = result.code / bank_size;
        break;
    case System16BTileFormat::Text:
        result.code = word & 0x01ffU;
        result.color = static_cast<std::uint8_t>((word >> 9) & 0x07U);
        break;
    case System16BTileFormat::AlternateTile:
        result.code = word & 0x1fffU;
        result.color = static_cast<std::uint8_t>((word >> 5) & 0x7fU);
        bank_index = result.code / bank_size;
        break;
    case System16BTileFormat::AlternateText:
        result.code = word & 0x00ffU;
        result.color = static_cast<std::uint8_t>((word >> 8) & 0x07U);
        break;
    }
    if (bank_index >= banks.size()) {
        throw std::out_of_range("System 16B tile code references a bank selector not configured by this profile");
    }
    result.code = static_cast<std::uint16_t>(banks[bank_index] * bank_size + result.code % bank_size);
    return result;
}

System16BTilemapState latch_system16b_tilemap_registers(std::span<const std::uint8_t> text_ram,
    System16BTilemapState state) {
    if (text_ram.size() < 0x1000)
        throw std::invalid_argument("315-5197 frame latch requires the full 4 KiB text RAM");
    for (unsigned i = 0; i < 4; ++i) {
        state.latched_pages[i] = read_be16(text_ram, 0xe80U + i * 2U);
        state.latched_yscroll[i] = read_be16(text_ram, 0xe90U + i * 2U);
        state.latched_xscroll[i] = read_be16(text_ram, 0xe98U + i * 2U);
    }
    state.registers_latched = true;
    return state;
}

namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint8_t tile_pen(std::span<const std::uint8_t> tiles, unsigned code, unsigned x, unsigned y);

std::uint16_t sys16b_word(std::span<const std::uint8_t> ram, std::size_t byte_offset) {
    return read_be16(ram, byte_offset);
}

std::uint16_t sys16b_latched_word(std::span<const std::uint8_t> ram,
    const System16BTilemapState& state, std::size_t byte_offset) {
    if (!state.registers_latched) return sys16b_word(ram, byte_offset);
    if (byte_offset >= 0xe80U && byte_offset <= 0xe86U && (byte_offset & 1U) == 0)
        return state.latched_pages[(byte_offset - 0xe80U) / 2U];
    if (byte_offset >= 0xe90U && byte_offset <= 0xe96U && (byte_offset & 1U) == 0)
        return state.latched_yscroll[(byte_offset - 0xe90U) / 2U];
    if (byte_offset >= 0xe98U && byte_offset <= 0xe9eU && (byte_offset & 1U) == 0)
        return state.latched_xscroll[(byte_offset - 0xe98U) / 2U];
    return sys16b_word(ram, byte_offset);
}

System16BFrame make_empty_sys16b_frame() {
    System16BFrame frame;
    const auto count = static_cast<std::size_t>(System16BFrame::width * System16BFrame::height);
    frame.palette_indices.assign(count, 0);
    frame.tile_priorities.assign(count, 0);
    frame.shadow_pixels.assign(count, 0);
    return frame;
}

void draw_sys16b_layer(System16BFrame& frame, std::span<const std::uint8_t> tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    const System16BTilemapState& state, unsigned which, unsigned category, bool opaque,
    std::uint8_t priority_mark) {
    const unsigned reg_index = which; // 0=foreground, 1=background.
    const std::uint16_t pages = sys16b_latched_word(text_ram, state, 0xe80U + reg_index * 2U);
    const std::uint16_t yscroll = sys16b_latched_word(text_ram, state, 0xe90U + reg_index * 2U);
    const std::uint16_t xscroll = sys16b_latched_word(text_ram, state, 0xe98U + reg_index * 2U);
    const std::uint16_t alt_pages = sys16b_latched_word(text_ram, state, 0xe84U + reg_index * 2U);
    const std::uint16_t alt_yscroll = sys16b_latched_word(text_ram, state, 0xe94U + reg_index * 2U);
    const std::uint16_t alt_xscroll = sys16b_latched_word(text_ram, state, 0xe9cU + reg_index * 2U);
    const std::uint16_t bank_size = state.bank_size;

    for (unsigned y = 0; y < System16BFrame::height; ++y) {
        const unsigned draw_y = state.flip ? System16BFrame::height - 1U - y : y;
        const int row_chunk_y = static_cast<int>(y & ~7U);
        const unsigned row_index = static_cast<unsigned>((state.flip ? 216 - row_chunk_y : row_chunk_y) / 8);
        for (unsigned x = 0; x < System16BFrame::width; ++x) {
            const unsigned draw_x = state.flip ? System16BFrame::width - 1U - x : x;
            const unsigned column_index = (x + 8U) / 16U;
            const std::uint16_t rowscroll = sys16b_word(text_ram,
                0xf80U + reg_index * 0x40U + row_index * 2U);
            std::uint16_t effective_pages = pages;
            std::uint16_t effective_xscroll = (xscroll & 0x8000U) ? rowscroll : xscroll;
            std::uint16_t effective_yscroll = yscroll;
            if (yscroll & 0x8000U) {
                effective_yscroll = sys16b_word(text_ram, 0xf16U + reg_index * 0x40U + column_index * 2U);
            }
            if (rowscroll & 0x8000U) {
                effective_pages = alt_pages;
                effective_xscroll = alt_xscroll;
                effective_yscroll = alt_yscroll;
            }

            const unsigned source_x = (draw_x + ((0xc0U - effective_xscroll) & 0x3ffU)) & 0x3ffU;
            const unsigned source_y = (draw_y + (effective_yscroll & 0x1ffU)) & 0x1ffU;
            const unsigned quadrant = ((source_y >> 8) * 2U) + (source_x >> 9);
            const std::uint8_t page = static_cast<std::uint8_t>((effective_pages >> (quadrant * 4U)) & 0xfU);
            const unsigned tile_x = (source_x & 0x1ffU) >> 3;
            const unsigned tile_y = (source_y & 0xffU) >> 3;
            const std::size_t word_index = static_cast<std::size_t>(page) * 2048U + tile_y * 64U + tile_x;
            const auto attributes = decode_system16b_tile_attributes(sys16b_word(tile_ram, word_index * 2U),
                System16BTileFormat::Tile, state.banks, bank_size);
            if (attributes.high_priority != (category != 0)) continue;
            const auto pen = tile_pen(tiles, attributes.code, source_x, source_y);
            const std::size_t pixel = static_cast<std::size_t>(y) * System16BFrame::width + x;
            if (priority_mark != 0 && pen != 0) frame.tile_priorities[pixel] |= priority_mark;
            if (opaque || pen != 0)
                frame.palette_indices[pixel] = static_cast<std::uint16_t>(attributes.color * 8U + pen);
        }
    }
}

void draw_sys16b_text(System16BFrame& frame, std::span<const std::uint8_t> tiles,
    std::span<const std::uint8_t> text_ram, const System16BTilemapState& state,
    unsigned category, std::uint8_t priority_mark) {
    for (unsigned y = 0; y < System16BFrame::height; ++y) {
        const unsigned draw_y = state.flip ? System16BFrame::height - 1U - y : y;
        for (unsigned x = 0; x < System16BFrame::width; ++x) {
            const unsigned draw_x = state.flip ? System16BFrame::width - 1U - x : x;
            const unsigned map_x = (draw_x + 192U) & 0x1ffU;
            const unsigned map_y = draw_y & 0xffU;
            const std::size_t word_index = (map_y >> 3) * 64U + (map_x >> 3);
            const auto attributes = decode_system16b_tile_attributes(sys16b_word(text_ram, word_index * 2U),
                System16BTileFormat::Text, state.banks, state.bank_size);
            if (attributes.high_priority != (category != 0)) continue;
            const auto pen = tile_pen(tiles, attributes.code, map_x, map_y);
            if (pen == 0) continue;
            const std::size_t pixel = static_cast<std::size_t>(y) * System16BFrame::width + x;
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(attributes.color * 8U + pen);
            frame.tile_priorities[pixel] |= priority_mark;
        }
    }
}

} // namespace

System16BFrame render_system16b_tilemaps_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    System16BTilemapState state, const System16BLayerHook& after_stage) {
    if (decoded_tiles.empty() || decoded_tiles.size() % 64 != 0 || tile_ram.size() < 0x10000 || text_ram.size() < 0x1000)
        throw std::invalid_argument("System 16B tilemap rendering requires decoded 3bpp graphics and full tile/text RAM");
    auto frame = make_empty_sys16b_frame();
    if (!state.display_enable) {
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
        return frame;
    }

    // Match segas16b_v.cpp composition: opaque background color, then priority
    // marks, foreground, text, and finally the sprite pass (315-5196).
    for (unsigned category = 0; category < 2; ++category)
        draw_sys16b_layer(frame, decoded_tiles, tile_ram, text_ram, state, 1, category, true, 0);
    if (after_stage) after_stage(frame, 0);
    for (unsigned category = 0; category < 2; ++category)
        draw_sys16b_layer(frame, decoded_tiles, tile_ram, text_ram, state, 1, category, false, static_cast<std::uint8_t>(1U << category));
    if (after_stage) after_stage(frame, 1);
    for (unsigned category = 0; category < 2; ++category)
        draw_sys16b_layer(frame, decoded_tiles, tile_ram, text_ram, state, 0, category, false, static_cast<std::uint8_t>(2U << category));
    if (after_stage) after_stage(frame, 2);
    for (unsigned category = 0; category < 2; ++category)
        draw_sys16b_text(frame, decoded_tiles, text_ram, state, category, static_cast<std::uint8_t>(4U << category));
    if (after_stage) after_stage(frame, 3);
    return frame;
}

std::vector<std::uint16_t> rasterize_system16b_sprites_cpu(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks, bool flip) {
    constexpr std::size_t descriptor_bytes = 16;
    constexpr std::size_t sprite_bank_bytes = 0x20000;
    // The System 16B sprite device's default local origin is (184, 0), or
    // (-184, 0) when flipped. Convert descriptor coordinates into this
    // renderer's visible-frame coordinate space before clipping/writing.
    constexpr int sprite_origin_x = 184;
    if (sprite_ram.size() < descriptor_bytes || sprite_rom.empty() || sprite_rom.size() % sprite_bank_bytes != 0)
        return {};
    const std::size_t bank_count = sprite_rom.size() / sprite_bank_bytes;
    std::vector<std::uint16_t> sprite_pixels(
        static_cast<std::size_t>(System16BFrame::width * System16BFrame::height), 0xffff);
    for (std::size_t descriptor = 0; descriptor + descriptor_bytes <= sprite_ram.size(); descriptor += descriptor_bytes) {
        const std::uint16_t bottom_top = read_be16(sprite_ram, descriptor);
        int bottom = bottom_top >> 8;
        int top = bottom_top & 0xffU;
        if (read_be16(sprite_ram, descriptor + 4) & 0x8000U) break; // end of list
        const std::uint16_t control_pitch = read_be16(sprite_ram, descriptor + 4);
        const bool hidden = (control_pitch & 0x4000U) != 0;
        const bool hflip = (control_pitch & 0x0100U) != 0;
        const int pitch = static_cast<std::int8_t>(control_pitch & 0xffU);
        const std::uint16_t address_start = read_be16(sprite_ram, descriptor + 6);
        const std::uint16_t bank_color_priority = read_be16(sprite_ram, descriptor + 8);
        const unsigned logical_bank = (bank_color_priority >> 8) & 0xfU;
        const unsigned bank = sprite_banks.empty() ? logical_bank
            : (logical_bank < sprite_banks.size() ? sprite_banks[logical_bank] : 0xffU);
        const unsigned priority_color = ((bank_color_priority & 0xffU) << 4)
            | (((read_be16(sprite_ram, descriptor + 2) >> 9) & 0xfU) << 12);
        sprite_ram[descriptor + 14] = static_cast<std::uint8_t>(address_start >> 8);
        sprite_ram[descriptor + 15] = static_cast<std::uint8_t>(address_start);
        unsigned zoom = read_be16(sprite_ram, descriptor + 10);
        const unsigned vzoom = (zoom >> 5) & 0x1fU;
        const unsigned hzoom = zoom & 0x1fU;
        if (hidden || top >= bottom || bank == 0xffU) continue;
        const std::size_t actual_bank = bank % bank_count;
        if (flip) {
            const int old_top = top;
            top = 224 - bottom;
            bottom = 224 - old_top;
        }
        int xpos = read_be16(sprite_ram, descriptor + 2) & 0x1ffU;
        const int xdelta = flip ? -1 : 1;
        if (flip) xpos = 320 - xpos;
        std::uint16_t address = address_start;
        zoom &= 0x03ffU;
        std::uint16_t zoom_accum = static_cast<std::uint16_t>(zoom);
        sprite_ram[descriptor + 10] = static_cast<std::uint8_t>(zoom_accum >> 8);
        sprite_ram[descriptor + 11] = static_cast<std::uint8_t>(zoom_accum);
        for (int y = top; y < bottom; ++y) {
            address = static_cast<std::uint16_t>(address + pitch);
            zoom_accum = static_cast<std::uint16_t>(zoom_accum + (vzoom << 10));
            if (zoom_accum & 0x8000U) {
                address = static_cast<std::uint16_t>(address + pitch);
                zoom_accum &= 0x7fffU;
            }
            sprite_ram[descriptor + 10] = static_cast<std::uint8_t>(zoom_accum >> 8);
            sprite_ram[descriptor + 11] = static_cast<std::uint8_t>(zoom_accum);
            if (y < 0 || y >= System16BFrame::height) continue;
        const bool reverse = hflip;
            int x = xpos;
            unsigned xacc = 4U * hzoom;
            std::uint16_t word_address = reverse ? static_cast<std::uint16_t>(address + 1)
                : static_cast<std::uint16_t>(address - 1);
            for (unsigned words = 0; words < 0x8000U; ++words) {
                word_address = static_cast<std::uint16_t>(word_address + (reverse ? -1 : 1));
                // MAME's 315-5196 indexes a bank as 0x10000 16-bit words
                // (0x20000 bytes). The descriptor address is a full 16-bit
                // word address; masking it to 0x7fff incorrectly folded the
                // upper half of every sprite bank onto its lower half.
                const std::size_t source_offset = actual_bank * sprite_bank_bytes
                    + static_cast<std::size_t>(word_address) * 2U;
                if (source_offset + 1 >= sprite_rom.size()) break;
                const std::uint16_t packed = static_cast<std::uint16_t>((sprite_rom[source_offset] << 8)
                    | sprite_rom[source_offset + 1]);
                unsigned last_pen = 0;
                for (unsigned pixel_in_word = 0; pixel_in_word < 4; ++pixel_in_word) {
                    const unsigned shift = reverse ? pixel_in_word * 4U : 12U - pixel_in_word * 4U;
                    const unsigned pen = (packed >> shift) & 0xfU;
                    last_pen = pen;
                    xacc = (xacc & 0x3fU) + hzoom;
                    if (xacc < 0x40U) {
                        const int screen_x = x - (flip ? -sprite_origin_x : sprite_origin_x);
                        if (pen != 0 && pen != 15 && screen_x >= 0 && screen_x < System16BFrame::width) {
                            const std::size_t pixel = static_cast<std::size_t>(y) * System16BFrame::width + screen_x;
                            sprite_pixels[pixel] = static_cast<std::uint16_t>(priority_color | pen);
                        }
                        x += xdelta;
                    }
                }
                if (last_pen == 15) break;
                if (((xpos - x) & 0x1ff) == 1) break;
            }
            // The physical sprite ASIC stores the running address in word 7.
            // MAME treats this as scratch; preserve that observable RAM behavior.
            sprite_ram[descriptor + 14] = static_cast<std::uint8_t>(word_address >> 8);
            sprite_ram[descriptor + 15] = static_cast<std::uint8_t>(word_address);
        }
    }

    return sprite_pixels;
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
    System16BTilemapState state, std::span<const std::uint8_t> sprite_banks,
    const System16BLayerHook& after_stage) {
    auto frame = render_system16b_tilemaps_cpu(decoded_tiles, tile_ram, text_ram, state, after_stage);
    if (state.display_enable)
        draw_system16b_sprites_cpu(frame, sprite_rom, sprite_ram, sprite_banks, state.flip);
    else
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
    return frame;
}

namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint8_t tile_pen(std::span<const std::uint8_t> tiles, unsigned code, unsigned x, unsigned y) {
    const std::size_t offset = static_cast<std::size_t>(code) * 64 + (y & 7U) * 8 + (x & 7U);
    return offset < tiles.size() ? tiles[offset] : 0;
}

void draw_16a_page_layer(System16AFrame& frame, std::span<const std::uint8_t> tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    unsigned layer, unsigned category, bool opaque, std::uint8_t priority_mark,
    const System16ARenderOptions& options, System16AVideoProfile* profile) {
    // MAME's map selector is 0=foreground and 1=background; the register lanes
    // interleave the two maps in adjacent 16-bit words.
    const unsigned page_reg = options.flip ? (layer == 1 ? 0xe8c : 0xe8e) : (layer == 1 ? 0xe9c : 0xe9e);
    std::uint16_t pages = read_be16(text_ram, page_reg);
    pages = static_cast<std::uint16_t>(((pages >> 4) & 0x0707) | ((pages << 4) & 0x7070));
    if (options.flip) {
        const auto reverse_nibble = [](unsigned value) {
            return ((value & 1U) << 3) | ((value & 2U) << 1) | ((value & 4U) >> 1) | ((value & 8U) >> 3);
        };
        std::uint16_t flipped_pages = 0;
        for (unsigned q = 0; q < 4; ++q) flipped_pages |= static_cast<std::uint16_t>(reverse_nibble((pages >> (q * 4)) & 0xfU) << (q * 4));
        pages = flipped_pages;
    }
    const unsigned xscroll_reg = (layer == 1 ? 0xffa : 0xff8);
    const unsigned yscroll_reg = (layer == 1 ? 0xf26 : 0xf24);
    const unsigned lane = layer;
    const unsigned global_xscroll = read_be16(text_ram, xscroll_reg) & 0x1ffU;
    const unsigned global_yscroll = read_be16(text_ram, yscroll_reg) & 0xffU;

    for (unsigned y = 0; y < System16AFrame::height; ++y) {
        for (unsigned x = 0; x < System16AFrame::width; ++x) {
            if (profile) ++profile->page_pixel_visits;
            const unsigned draw_x = options.flip ? System16AFrame::width - 1 - x : x;
            const unsigned draw_y = options.flip ? System16AFrame::height - 1 - y : y;
            // MAME selects row scroll once per 8-pixel output chunk, before
            // the tilemap's flip transform. Quantize y first; with flip enabled,
            // computing (216 - y) per pixel would change rows inside each chunk.
            const int row_chunk_y = static_cast<int>(y & ~7U);
            const int row_index = (options.flip ? (216 - row_chunk_y) : row_chunk_y) / 8;
            const unsigned column_index = x / 16;
            unsigned raw_xscroll = global_xscroll;
            unsigned raw_yscroll = global_yscroll;
            if (options.row_scroll_enabled)
                raw_xscroll = read_be16(text_ram, 0xf80 + static_cast<unsigned>(row_index) * 4 + lane * 2) & 0x1ffU;
            if (options.column_scroll_enabled)
                raw_yscroll = read_be16(text_ram, 0xf30 + column_index * 4 + lane * 2) & 0xffU;
            if (options.flip) raw_xscroll += 17;
            const unsigned xscroll = (0xc8U - raw_xscroll) & 0x3ffU;
            const unsigned yscroll = raw_yscroll & 0x1ffU;
            // tilemap_t::effective_rowscroll/colscroll converts MAME's
            // set_scrollx/set_scrolly values to negative destination offsets;
            // the resulting visible source coordinate is screen + scroll.
            const unsigned virtual_x = (draw_x + xscroll) & 0x3ffU;
            const unsigned virtual_y = (draw_y + yscroll) & 0x1ffU;
            const unsigned quadrant = ((virtual_y >> 8) * 2) + (virtual_x >> 9);
            const unsigned page = (pages >> (quadrant * 4)) & 0xfU;
            const unsigned tile_x = (virtual_x & 0x1ffU) >> 3;
            const unsigned tile_y = (virtual_y & 0xffU) >> 3;
            const std::size_t word_offset = (static_cast<std::size_t>(page) * 2048 + tile_y * 64 + tile_x) * 2;
            const std::uint16_t data = read_be16(tile_ram, word_offset);
            if (((data >> 12) & 1U) != category) continue;
            const unsigned code = ((data >> 1) & 0x1000U) | (data & 0xfffU);
            if (profile) ++profile->page_pen_lookups;
            const std::uint8_t pen = tile_pen(tiles, code, virtual_x, virtual_y);
            const std::size_t pixel = y * System16AFrame::width + x;
            if (priority_mark && pen != 0) {
                frame.tile_priorities[pixel] |= priority_mark;
                if (profile) ++profile->priority_marks;
            }
            if (opaque || pen != 0) {
                frame.palette_indices[pixel] = static_cast<std::uint16_t>(((data >> 5) & 0x7fU) * 8 + pen);
                if (profile) {
                    if (layer == 1) ++profile->background_palette_writes;
                    else ++profile->foreground_palette_writes;
                }
            }
        }
    }
}

void draw_16a_text(System16AFrame& frame, std::span<const std::uint8_t> tiles,
    std::span<const std::uint8_t> text_ram, unsigned category, std::uint8_t priority_mark,
    const System16ARenderOptions& options, System16AVideoProfile* profile) {
    for (unsigned y = 0; y < System16AFrame::height; ++y) {
        for (unsigned x = 0; x < System16AFrame::width; ++x) {
            if (profile) ++profile->text_pixel_visits;
            const unsigned screen_x = options.flip ? System16AFrame::width - 1 - x : x;
            const unsigned screen_y = options.flip ? System16AFrame::height - 1 - y : y;
            const unsigned map_x = (screen_x + 192U) & 0x1ffU;
            const unsigned map_y = screen_y & 0xffU;
            const std::size_t word_offset = ((map_y >> 3) * 64 + (map_x >> 3)) * 2;
            const std::uint16_t data = read_be16(text_ram, word_offset);
            if (((data >> 11) & 1U) != category) continue;
            if (profile) ++profile->text_pen_lookups;
            const std::uint8_t pen = tile_pen(tiles, data & 0xffU, map_x, map_y);
            if (pen == 0) continue;
            const std::size_t pixel = y * System16AFrame::width + x;
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(((data >> 8) & 7U) * 8 + pen);
            frame.tile_priorities[pixel] |= priority_mark;
            if (profile) {
                ++profile->text_palette_writes;
                ++profile->priority_marks;
            }
        }
    }
}

void draw_16a_sprites(System16AFrame& frame, std::span<const std::uint8_t> sprite_rom,
    std::span<const std::uint8_t> sprite_ram, const System16ARenderOptions& options,
    System16AVideoProfile* profile) {
    if (sprite_rom.empty() || sprite_rom.size() % 0x10000 != 0 || sprite_ram.size() < 0x800) return;
    std::vector<std::uint16_t> sprite_pixels(frame.palette_indices.size(), 0xffff);
    const unsigned banks = static_cast<unsigned>(sprite_rom.size() / 0x10000);
    const auto sprite_word = [&](unsigned bank, unsigned address) {
        if (profile) ++profile->sprite_words_read;
        const std::size_t offset = static_cast<std::size_t>(bank) * 0x10000 + (address & 0x7fffU) * 2;
        return static_cast<std::uint16_t>((sprite_rom[offset] << 8) | sprite_rom[offset + 1]);
    };

    // System 16A descriptors contain eight big-endian words. Word 7 is scratch
    // used by MAME's renderer; the read pointer is local here, so guest RAM is untouched.
    for (std::size_t descriptor = 0; descriptor + 16 <= sprite_ram.size(); descriptor += 16) {
        const unsigned bottom_raw = read_be16(sprite_ram, descriptor) >> 8;
        if (bottom_raw > 0xf0) break;
        const unsigned top_raw = read_be16(sprite_ram, descriptor) & 0xffU;
        int xpos = read_be16(sprite_ram, descriptor + 2) & 0x1ffU;
        const int pitch = static_cast<std::int16_t>(read_be16(sprite_ram, descriptor + 4));
        std::uint16_t address = read_be16(sprite_ram, descriptor + 6);
        const std::uint16_t attributes = read_be16(sprite_ram, descriptor + 8);
        const unsigned bank = (attributes >> 4) & 7U;
        if (top_raw >= bottom_raw || bank >= banks) continue;
        const unsigned color_priority = (((attributes >> 8) & 0x3fU) << 4) | ((attributes & 3U) << 10);
        int top = static_cast<int>(top_raw);
        int bottom = static_cast<int>(bottom_raw);
        int xdelta = 1;
        int xorigin = 189;
        if (options.flip) {
            top = 224 - static_cast<int>(bottom_raw);
            bottom = 224 - static_cast<int>(top_raw);
            xpos = 320 - xpos;
            xdelta = -1;
            xorigin = -189;
        }

        for (int y = top; y < bottom; ++y) {
            address = static_cast<std::uint16_t>(address + pitch);
            const int screen_y = y + 1; // System 16A's configured sprite origin is -1.
            if (screen_y < 0 || screen_y >= System16AFrame::height) continue;
            const bool reverse = (address & 0x8000U) != 0;
            std::uint16_t word_address = reverse ? static_cast<std::uint16_t>(address + 1) : static_cast<std::uint16_t>(address - 1);
            int x = xpos;
            // Match MAME's System 16A row walk: the pen-15 terminator ends a
            // sprite row. Do not impose a guessed maximum sprite width.
            for (; ((xpos - x) & 0x1ff) != 1; ) {
                const std::uint16_t packed = sprite_word(bank, reverse ? --word_address : ++word_address);
                int last_pen = 0;
                for (unsigned nibble = 0; nibble < 4; ++nibble) {
                    if (profile) ++profile->sprite_pen_lookups;
                    const unsigned shift = reverse ? nibble * 4 : 12 - nibble * 4;
                    const unsigned pen = (packed >> shift) & 0xfU;
                    last_pen = static_cast<int>(pen);
                    const int screen_x = x - xorigin;
                    if (pen != 0 && pen != 15 && screen_x >= 0 && screen_x < System16AFrame::width) {
                        const std::size_t pixel = static_cast<std::size_t>(screen_y) * System16AFrame::width + screen_x;
                        sprite_pixels[pixel] = static_cast<std::uint16_t>(color_priority | pen);
                        if (profile) ++profile->sprite_pixels_rasterized;
                    }
                    x += xdelta;
                }
                if (last_pen == 15) break;
            }
        }
    }

    for (std::size_t pixel = 0; pixel < sprite_pixels.size(); ++pixel) {
        const std::uint16_t sprite = sprite_pixels[pixel];
        if (sprite == 0xffff) continue;
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

std::uint8_t palette_channel(unsigned value, bool effect_enabled, bool highlight) {
    // Rounded per-channel tables from MAME's resistor network:
    // normal {3900,2000,1000,500,250,0} ohms;
    // shadow/highlight {3900,2000,1000,500,250,470} ohms.
    static constexpr std::uint8_t normal[32] = {
        0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 91, 99, 107, 115, 123,
        132, 140, 148, 156, 164, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255
    };
    static constexpr std::uint8_t effect[2][32] = {
        {0, 7, 13, 20, 26, 32, 39, 45, 52, 58, 64, 71, 77, 84, 90, 97,
         103, 110, 116, 123, 129, 136, 142, 149, 155, 161, 168, 174, 181, 187, 193, 200},
        {55, 62, 68, 74, 81, 87, 94, 100, 106, 113, 119, 126, 132, 139, 145, 152,
         158, 165, 171, 178, 184, 191, 197, 203, 210, 216, 223, 229, 235, 242, 248, 255}
    };
    return effect_enabled ? effect[highlight ? 1 : 0][value & 0x1fU] : normal[value & 0x1fU];
}

System16AFrame make_empty_frame() {
    System16AFrame frame;
    const auto pixels = static_cast<std::size_t>(System16AFrame::width * System16AFrame::height);
    frame.palette_indices.assign(pixels, 0);
    frame.tile_priorities.assign(pixels, 0);
    frame.shadow_pixels.assign(pixels, 0);
    return frame;
}

void render_16a_tilemaps_software(System16AFrame& frame, std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    const System16ARenderOptions& options, std::vector<System16AFrame>* stage_outputs,
    System16AVideoProfile* profile) {
    if (!options.display_enable) return;
    using Clock = std::chrono::steady_clock;
    auto stage_start = profile ? Clock::now() : Clock::time_point{};
    for (unsigned category = 0; category < 2; ++category)
        draw_16a_page_layer(frame, decoded_tiles, tile_ram, text_ram, 1, category, true, 0, options, profile);
    for (unsigned category = 0; category < 2; ++category)
        draw_16a_page_layer(frame, decoded_tiles, tile_ram, text_ram, 1, category, false,
            static_cast<std::uint8_t>(1U << category), options, profile);
    if (profile) {
        profile->background_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_start).count();
        stage_start = Clock::now();
    }
    if (stage_outputs) stage_outputs->push_back(frame);
    for (unsigned category = 0; category < 2; ++category)
        draw_16a_page_layer(frame, decoded_tiles, tile_ram, text_ram, 0, category, false,
            static_cast<std::uint8_t>(2U << category), options, profile);
    if (profile) {
        profile->foreground_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_start).count();
        stage_start = Clock::now();
    }
    if (stage_outputs) stage_outputs->push_back(frame);
    for (unsigned category = 0; category < 2; ++category)
        draw_16a_text(frame, decoded_tiles, text_ram, category, static_cast<std::uint8_t>(4U << category), options, profile);
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

bool system16b_opengl_backend_available() {
#ifdef MAMERECOMP_HAS_OPENGL_VIDEO
    return true;
#else
    return false;
#endif
}

System16BFrame render_system16b_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    System16BTilemapState state, std::span<const std::uint8_t> sprite_banks, bool use_opengl) {
    System16BFrame frame;
    if (use_opengl) {
        if (!system16b_opengl_backend_available())
            throw std::runtime_error("System 16B OpenGL renderer was not built; install SDL3/OpenGL development files and reconfigure");
        frame = render_system16b_tilemaps_opengl(decoded_tiles, tile_ram, text_ram, state);
    } else {
        frame = render_system16b_tilemaps_cpu(decoded_tiles, tile_ram, text_ram, state);
    }
    if (state.display_enable) {
        const auto sprite_pixels = rasterize_system16b_sprites_cpu(sprite_rom, sprite_ram, sprite_banks, state.flip);
        if (use_opengl) mix_system16b_sprites_opengl(frame, sprite_pixels);
        else mix_system16b_sprites_cpu(frame, sprite_pixels);
    } else {
        std::fill(frame.palette_indices.begin(), frame.palette_indices.end(), 4096);
    }
    return frame;
}

#ifndef MAMERECOMP_HAS_OPENGL_VIDEO
bool system16a_opengl_backend_available() { return false; }

System16AFrame render_system16a_tilemaps_opengl(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
    std::span<const std::uint8_t>, System16ARenderOptions, System16AVideoProfile*) {
    throw std::runtime_error("OpenGL video backend was not built; install SDL3/OpenGL development files and reconfigure");
}

System16BFrame render_system16b_tilemaps_opengl(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
    std::span<const std::uint8_t>, System16BTilemapState) {
    throw std::runtime_error("System 16B OpenGL renderer was not built; install SDL3/OpenGL development files and reconfigure");
}

void mix_system16b_sprites_opengl(System16BFrame&, std::span<const std::uint16_t>) {
    throw std::runtime_error("System 16B OpenGL sprite mixer was not built; install SDL3/OpenGL development files and reconfigure");
}
#endif

std::vector<std::uint8_t> system16a_frame_rgb(const System16AFrame& frame,
    std::span<const std::uint8_t> palette_ram) {
    if (frame.palette_indices.size() != static_cast<std::size_t>(System16AFrame::width * System16AFrame::height)
        || frame.shadow_pixels.size() != frame.palette_indices.size() || palette_ram.size() < 0x1000) {
        throw std::invalid_argument("invalid System 16A frame or palette RAM");
    }
    std::vector<std::uint8_t> rgb(frame.palette_indices.size() * 3);
    for (std::size_t pixel = 0; pixel < frame.palette_indices.size(); ++pixel) {
        const auto index = frame.palette_indices[pixel];
        if (index >= 0x1000U && index < 0x1040U) {
            const auto color = frame.vdp_palette_rgb[index - 0x1000U];
            rgb[pixel * 3] = static_cast<std::uint8_t>((color >> 16) & 0xffU);
            rgb[pixel * 3 + 1] = static_cast<std::uint8_t>((color >> 8) & 0xffU);
            rgb[pixel * 3 + 2] = static_cast<std::uint8_t>(color & 0xffU);
            continue;
        }
        const bool shadow_palette = index >= 0x800U && index < 0x1000U;
        const auto palette_index = shadow_palette ? static_cast<std::uint16_t>(index - 0x800U) : index;
        if (palette_index >= palette_ram.size() / 2) {
            rgb[pixel * 3] = 0;
            rgb[pixel * 3 + 1] = 0;
            rgb[pixel * 3 + 2] = 0;
            continue;
        }
        const unsigned word = read_be16(palette_ram, static_cast<std::size_t>(palette_index) * 2);
        const unsigned r = ((word >> 12) & 1U) | ((word << 1) & 0x1eU);
        const unsigned g = ((word >> 13) & 1U) | ((word >> 3) & 0x1eU);
        const unsigned b = ((word >> 14) & 1U) | ((word >> 7) & 0x1eU);
        const bool effect = frame.shadow_pixels[pixel] != 0 || shadow_palette;
        const bool highlight = (word & 0x8000U) != 0;
        rgb[pixel * 3] = palette_channel(r, effect, highlight);
        rgb[pixel * 3 + 1] = palette_channel(g, effect, highlight);
        rgb[pixel * 3 + 2] = palette_channel(b, effect, highlight);
    }
    return rgb;
}

void write_system16a_frame_ppm(const System16AFrame& frame,
    std::span<const std::uint8_t> palette_ram, const std::filesystem::path& path) {
    const auto rgb = system16a_frame_rgb(frame, palette_ram);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("could not create frame image: " + path.string());
    out << "P6\n" << System16AFrame::width << ' ' << System16AFrame::height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!out) throw std::runtime_error("failed while writing frame image: " + path.string());
}

} // namespace mamerecomp
