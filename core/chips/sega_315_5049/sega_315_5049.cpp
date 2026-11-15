#include "sega_315_5049.hpp"

#include <stdexcept>

namespace mamerecomp::chips::sega3155049 {
namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint8_t tile_pen(std::span<const std::uint8_t> tiles, unsigned code, unsigned x, unsigned y) {
    const std::size_t offset = static_cast<std::size_t>(code) * 64U + (y & 7U) * 8U + (x & 7U);
    return offset < tiles.size() ? tiles[offset] : 0;
}

} // namespace

TilemapOutput render_layer(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    TilemapState state, Layer layer, unsigned category, bool opaque,
    std::uint8_t priority_mark) {
    if (decoded_tiles.empty() || decoded_tiles.size() % 64U != 0
        || tile_ram.size() < 0x8000U || text_ram.size() < 0x1000U || category > 1U)
        throw std::invalid_argument("315-5049 rendering requires decoded tiles, full tile/text RAM, and a valid category");

    const auto pixel_count = static_cast<std::size_t>(kFrameWidth) * kFrameHeight;
    TilemapOutput output{
        std::vector<std::uint16_t>(pixel_count, kTransparentPixel),
        std::vector<std::uint8_t>(pixel_count, 0),
        {}};

    if (layer == Layer::Text) {
        for (unsigned y = 0; y < kFrameHeight; ++y) {
            for (unsigned x = 0; x < kFrameWidth; ++x) {
                ++output.stats.output_pixel_visits;
                const unsigned screen_x = state.flip ? kFrameWidth - 1U - x : x;
                const unsigned screen_y = state.flip ? kFrameHeight - 1U - y : y;
                const unsigned map_x = (screen_x + 192U) & 0x1ffU;
                const unsigned map_y = screen_y & 0xffU;
                const std::size_t word_offset = ((map_y >> 3) * 64U + (map_x >> 3)) * 2U;
                const std::uint16_t data = read_be16(text_ram, word_offset);
                if (((data >> 11) & 1U) != category) continue;
                ++output.stats.tile_pen_lookups;
                const std::uint8_t pen = tile_pen(decoded_tiles, data & 0xffU, map_x, map_y);
                if (pen == 0) continue;
                const auto pixel = static_cast<std::size_t>(y) * kFrameWidth + x;
                output.palette_indices[pixel] = static_cast<std::uint16_t>(((data >> 8) & 7U) * 8U + pen);
                output.priority_marks[pixel] = priority_mark;
                ++output.stats.palette_writes;
                if (priority_mark != 0) ++output.stats.priority_marks;
            }
        }
        return output;
    }

    const bool background = layer == Layer::Background;
    const unsigned page_reg = state.flip ? (background ? 0xe8cU : 0xe8eU)
                                         : (background ? 0xe9cU : 0xe9eU);
    std::uint16_t pages = read_be16(text_ram, page_reg);
    pages = static_cast<std::uint16_t>(((pages >> 4) & 0x0707U) | ((pages << 4) & 0x7070U));
    if (state.flip) {
        const auto reverse_nibble = [](unsigned value) {
            return ((value & 1U) << 3) | ((value & 2U) << 1)
                | ((value & 4U) >> 1) | ((value & 8U) >> 3);
        };
        std::uint16_t flipped_pages = 0;
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
            flipped_pages |= static_cast<std::uint16_t>(reverse_nibble((pages >> (quadrant * 4U)) & 0xfU) << (quadrant * 4U));
        pages = flipped_pages;
    }
    const unsigned xscroll_reg = background ? 0xffaU : 0xff8U;
    const unsigned yscroll_reg = background ? 0xf26U : 0xf24U;
    const unsigned lane = background ? 1U : 0U;
    const unsigned global_xscroll = read_be16(text_ram, xscroll_reg) & 0x1ffU;
    const unsigned global_yscroll = read_be16(text_ram, yscroll_reg) & 0xffU;

    for (unsigned y = 0; y < kFrameHeight; ++y) {
        for (unsigned x = 0; x < kFrameWidth; ++x) {
            ++output.stats.output_pixel_visits;
            const unsigned draw_x = state.flip ? kFrameWidth - 1U - x : x;
            const unsigned draw_y = state.flip ? kFrameHeight - 1U - y : y;
            const int row_chunk_y = static_cast<int>(y & ~7U);
            const int row_index = (state.flip ? (216 - row_chunk_y) : row_chunk_y) / 8;
            const unsigned column_index = x / 16U;
            unsigned raw_xscroll = global_xscroll;
            unsigned raw_yscroll = global_yscroll;
            if (state.row_scroll_enabled)
                raw_xscroll = read_be16(text_ram, 0xf80U + static_cast<unsigned>(row_index) * 4U + lane * 2U) & 0x1ffU;
            if (state.column_scroll_enabled)
                raw_yscroll = read_be16(text_ram, 0xf30U + column_index * 4U + lane * 2U) & 0xffU;
            if (state.flip) raw_xscroll += 17U;
            const unsigned xscroll = (0xc8U - raw_xscroll) & 0x3ffU;
            const unsigned yscroll = raw_yscroll & 0x1ffU;
            const unsigned virtual_x = (draw_x + xscroll) & 0x3ffU;
            const unsigned virtual_y = (draw_y + yscroll) & 0x1ffU;
            const unsigned quadrant = ((virtual_y >> 8) * 2U) + (virtual_x >> 9);
            const unsigned page = (pages >> (quadrant * 4U)) & 0xfU;
            const unsigned tile_x = (virtual_x & 0x1ffU) >> 3;
            const unsigned tile_y = (virtual_y & 0xffU) >> 3;
            const std::size_t word_offset = (static_cast<std::size_t>(page) * 2048U + tile_y * 64U + tile_x) * 2U;
            const std::uint16_t data = read_be16(tile_ram, word_offset);
            if (((data >> 12) & 1U) != category) continue;
            const unsigned code = ((data >> 1) & 0x1000U) | (data & 0xfffU);
            ++output.stats.tile_pen_lookups;
            const std::uint8_t pen = tile_pen(decoded_tiles, code, virtual_x, virtual_y);
            const auto pixel = static_cast<std::size_t>(y) * kFrameWidth + x;
            if (priority_mark != 0 && pen != 0) {
                output.priority_marks[pixel] = priority_mark;
                ++output.stats.priority_marks;
            }
            if (opaque || pen != 0) {
                output.palette_indices[pixel] = static_cast<std::uint16_t>(((data >> 5) & 0x7fU) * 8U + pen);
                ++output.stats.palette_writes;
            }
        }
    }
    return output;
}

} // namespace mamerecomp::chips::sega3155049
