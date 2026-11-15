#include "sega_315_5197.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace multiarcaderecomp::chips::sega3155197 {
namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
        | bytes[offset + 1]);
}

} // namespace

TileAttributes decode_tile_attributes(std::uint16_t word, TileFormat format,
    std::span<const std::uint8_t> banks, std::uint16_t bank_size) {
    if (banks.empty() || bank_size == 0)
        throw std::invalid_argument("315-5197 tile attributes require configured banks and a nonzero bank size");

    TileAttributes result;
    result.high_priority = (word & 0x8000U) != 0;
    unsigned bank_index = 0;
    switch (format) {
    case TileFormat::Tile:
        result.code = word & 0x1fffU;
        result.color = static_cast<std::uint8_t>((word >> 6) & 0x7fU);
        bank_index = result.code / bank_size;
        break;
    case TileFormat::Text:
        result.code = word & 0x01ffU;
        result.color = static_cast<std::uint8_t>((word >> 9) & 0x07U);
        break;
    case TileFormat::AlternateTile:
        result.code = word & 0x1fffU;
        result.color = static_cast<std::uint8_t>((word >> 5) & 0x7fU);
        bank_index = result.code / bank_size;
        break;
    case TileFormat::AlternateText:
        result.code = word & 0x00ffU;
        result.color = static_cast<std::uint8_t>((word >> 8) & 0x07U);
        break;
    }
    if (bank_index >= banks.size())
        throw std::out_of_range("315-5197 tile code references an unconfigured bank selector");
    result.code = static_cast<std::uint16_t>(banks[bank_index] * bank_size + result.code % bank_size);
    return result;
}

TilemapState latch_frame_registers(std::span<const std::uint8_t> text_ram, TilemapState state) {
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

void TilemapRenderer::refresh_cache(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    const TilemapState& state) {
    constexpr std::size_t tile_count = 16U * 64U * 32U;
    constexpr std::size_t text_tile_count = 64U * 32U;
    const bool cache_changed = !m_cache_initialized || m_banks != state.banks || m_bank_size != state.bank_size
        || m_graphics_data != decoded_tiles.data() || m_graphics_size != decoded_tiles.size();
    if (m_tile_words.size() != tile_count) {
        m_tile_words.resize(tile_count);
        m_tile_valid.assign(tile_count, 0);
        m_tile_pixels.resize(tile_count * 64U);
    }
    if (m_text_words.size() != text_tile_count) {
        m_text_words.resize(text_tile_count);
        m_text_valid.assign(text_tile_count, 0);
        m_text_pixels.resize(text_tile_count * 64U);
    }
    if (cache_changed) {
        std::fill(m_tile_valid.begin(), m_tile_valid.end(), 0);
        std::fill(m_text_valid.begin(), m_text_valid.end(), 0);
        m_banks = state.banks;
        m_bank_size = state.bank_size;
        m_graphics_data = decoded_tiles.data();
        m_graphics_size = decoded_tiles.size();
        m_cache_initialized = true;
    }

    for (std::size_t index = 0; index < tile_count; ++index) {
        const auto value = read_be16(tile_ram, index * 2U);
        if (m_tile_valid[index] && m_tile_words[index] == value) continue;
        m_tile_valid[index] = 1;
        m_tile_words[index] = value;
        const auto attributes = decode_tile_attributes(value, TileFormat::Tile, state.banks, state.bank_size);
        const std::uint16_t high_priority = attributes.high_priority ? 0x8000U : 0;
        const std::size_t tile_base = index * 64U;
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            const std::size_t source = static_cast<std::size_t>(attributes.code) * 64U + pixel;
            const std::uint8_t pen = source < decoded_tiles.size() ? decoded_tiles[source] : 0;
            m_tile_pixels[tile_base + pixel] = static_cast<std::uint16_t>(high_priority
                | (attributes.color * 8U + pen));
        }
    }
    for (std::size_t index = 0; index < text_tile_count; ++index) {
        const auto value = read_be16(text_ram, index * 2U);
        if (m_text_valid[index] && m_text_words[index] == value) continue;
        m_text_valid[index] = 1;
        m_text_words[index] = value;
        const auto attributes = decode_tile_attributes(value, TileFormat::Text, state.banks, state.bank_size);
        const std::uint16_t high_priority = attributes.high_priority ? 0x8000U : 0;
        const std::size_t tile_base = index * 64U;
        for (unsigned pixel = 0; pixel < 64; ++pixel) {
            const std::size_t source = static_cast<std::size_t>(attributes.code) * 64U + pixel;
            const std::uint8_t pen = source < decoded_tiles.size() ? decoded_tiles[source] : 0;
            m_text_pixels[tile_base + pixel] = static_cast<std::uint16_t>(high_priority
                | (attributes.color * 8U + pen));
        }
    }
}

TilemapOutput TilemapRenderer::render(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    TilemapState state, const StageHook& after_stage) {
    if (decoded_tiles.empty() || decoded_tiles.size() % 64 != 0
        || tile_ram.size() < 0x10000 || text_ram.size() < 0x1000)
        throw std::invalid_argument("315-5197 tilemaps require decoded graphics and full tile/text RAM");
    const auto word = [](std::span<const std::uint8_t> bytes, std::size_t offset) {
        return read_be16(bytes, offset);
    };
    const auto latched_word = [&](std::size_t offset) {
        if (!state.registers_latched) return word(text_ram, offset);
        if (offset >= 0xe80U && offset <= 0xe86U && (offset & 1U) == 0)
            return state.latched_pages[(offset - 0xe80U) / 2U];
        if (offset >= 0xe90U && offset <= 0xe96U && (offset & 1U) == 0)
            return state.latched_yscroll[(offset - 0xe90U) / 2U];
        if (offset >= 0xe98U && offset <= 0xe9eU && (offset & 1U) == 0)
            return state.latched_xscroll[(offset - 0xe98U) / 2U];
        return word(text_ram, offset);
    };
    TilemapOutput output;
    const auto pixel_count = static_cast<std::size_t>(kFrameWidth * kFrameHeight);
    output.palette_indices.assign(pixel_count, 0);
    output.priorities.assign(pixel_count, 0);
    output.shadow_pixels.assign(pixel_count, 0);
    if (!state.display_enable) {
        std::fill(output.palette_indices.begin(), output.palette_indices.end(), 4096);
        return output;
    }
    refresh_cache(decoded_tiles, tile_ram, text_ram, state);

    const auto draw_layer = [&](unsigned which, bool opaque,
        std::uint8_t low_priority_mark, std::uint8_t high_priority_mark) {
        const unsigned reg_index = which; // 0=foreground, 1=background.
        const std::uint16_t pages = latched_word(0xe80U + reg_index * 2U);
        const std::uint16_t yscroll = latched_word(0xe90U + reg_index * 2U);
        const std::uint16_t xscroll = latched_word(0xe98U + reg_index * 2U);
        const std::uint16_t alt_pages = latched_word(0xe84U + reg_index * 2U);
        const std::uint16_t alt_yscroll = latched_word(0xe94U + reg_index * 2U);
        const std::uint16_t alt_xscroll = latched_word(0xe9cU + reg_index * 2U);

        for (unsigned y = 0; y < kFrameHeight; ++y) {
            const unsigned draw_y = state.flip ? kFrameHeight - 1U - y : y;
            const int row_chunk_y = static_cast<int>(y & ~7U);
            const unsigned row_index = static_cast<unsigned>((state.flip ? 216 - row_chunk_y : row_chunk_y) / 8);
            const std::uint16_t rowscroll = word(text_ram,
                0xf80U + reg_index * 0x40U + row_index * 2U);
            unsigned cached_column = ~0U;
            std::uint16_t effective_pages = 0;
            std::uint16_t effective_xscroll = 0;
            std::uint16_t effective_yscroll = 0;
            for (unsigned x = 0; x < kFrameWidth; ++x) {
                const unsigned draw_x = state.flip ? kFrameWidth - 1U - x : x;
                const unsigned column_index = (x + 8U) / 16U;
                if (column_index != cached_column) {
                    cached_column = column_index;
                    effective_pages = pages;
                    effective_xscroll = (xscroll & 0x8000U) ? rowscroll : xscroll;
                    effective_yscroll = yscroll;
                    if (yscroll & 0x8000U)
                        effective_yscroll = word(text_ram, 0xf16U + reg_index * 0x40U + column_index * 2U);
                    if (rowscroll & 0x8000U) {
                        effective_pages = alt_pages;
                        effective_xscroll = alt_xscroll;
                        effective_yscroll = alt_yscroll;
                    }
                }
                const unsigned source_x = (draw_x + ((0xc0U - effective_xscroll) & 0x3ffU)) & 0x3ffU;
                const unsigned source_y = (draw_y + (effective_yscroll & 0x1ffU)) & 0x1ffU;
                const unsigned quadrant = ((source_y >> 8) * 2U) + (source_x >> 9);
                const std::uint8_t page = static_cast<std::uint8_t>((effective_pages >> (quadrant * 4U)) & 0xfU);
                const unsigned tile_x = (source_x & 0x1ffU) >> 3;
                const unsigned tile_y = (source_y & 0xffU) >> 3;
                const std::size_t word_index = static_cast<std::size_t>(page) * 2048U + tile_y * 64U + tile_x;
                const std::uint16_t tile_pixel = m_tile_pixels[word_index * 64U
                    + ((source_y & 7U) * 8U) + (source_x & 7U)];
                const unsigned pixel_pen = tile_pixel & 7U;
                const std::size_t pixel = static_cast<std::size_t>(y) * kFrameWidth + x;
                if (pixel_pen != 0) {
                    const auto mark = (tile_pixel & 0x8000U) ? high_priority_mark : low_priority_mark;
                    if (mark != 0) output.priorities[pixel] |= mark;
                }
                if (opaque || pixel_pen != 0)
                    output.palette_indices[pixel] = static_cast<std::uint16_t>(tile_pixel & 0x7fffU);
            }
        }
    };
    const auto draw_text = [&](std::uint8_t low_priority_mark, std::uint8_t high_priority_mark) {
        for (unsigned y = 0; y < kFrameHeight; ++y) {
            const unsigned draw_y = state.flip ? kFrameHeight - 1U - y : y;
            for (unsigned x = 0; x < kFrameWidth; ++x) {
                const unsigned draw_x = state.flip ? kFrameWidth - 1U - x : x;
                const unsigned map_x = (draw_x + 192U) & 0x1ffU;
                const unsigned map_y = draw_y & 0xffU;
                const std::size_t word_index = (map_y >> 3) * 64U + (map_x >> 3);
                const std::uint16_t tile_pixel = m_text_pixels[word_index * 64U
                    + ((map_y & 7U) * 8U) + (map_x & 7U)];
                const unsigned pixel_pen = tile_pixel & 7U;
                if (pixel_pen == 0) continue;
                const std::size_t pixel = static_cast<std::size_t>(y) * kFrameWidth + x;
                output.palette_indices[pixel] = static_cast<std::uint16_t>(tile_pixel & 0x7fffU);
                output.priorities[pixel] |= (tile_pixel & 0x8000U) ? high_priority_mark : low_priority_mark;
            }
        }
    };

    draw_layer(1, true, 0, 0);
    if (after_stage) after_stage(output, 0);
    draw_layer(1, false, 1, 2);
    if (after_stage) after_stage(output, 1);
    draw_layer(0, false, 2, 4);
    if (after_stage) after_stage(output, 2);
    draw_text(4, 8);
    if (after_stage) after_stage(output, 3);
    return output;
}

TilemapOutput render_tilemaps(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    TilemapState state, const StageHook& after_stage) {
    TilemapRenderer renderer;
    return renderer.render(decoded_tiles, tile_ram, text_ram, state, after_stage);
}

} // namespace multiarcaderecomp::chips::sega3155197
