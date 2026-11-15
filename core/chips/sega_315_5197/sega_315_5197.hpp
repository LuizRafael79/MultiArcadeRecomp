#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace multiarcaderecomp::chips::sega3155197 {

enum class TileFormat {
    Tile,
    Text,
    AlternateTile,
    AlternateText,
};

struct TileAttributes {
    std::uint16_t code = 0;
    std::uint8_t color = 0;
    bool high_priority = false;
};

struct TilemapState {
    bool display_enable = true;
    bool flip = false;
    std::array<std::uint8_t, 8> banks{0, 1, 2, 3, 4, 5, 6, 7};
    // The default is System 16B's tilemap_init(..., numbanks=2): MAME divides
    // its 0x2000-code address space into two 0x1000-code banks. System 18
    // overrides this to 0x0400 for its eight-bank setup.
    std::uint16_t bank_size = 0x1000;
    bool registers_latched = false;
    std::array<std::uint16_t, 4> latched_pages{};
    std::array<std::uint16_t, 4> latched_yscroll{};
    std::array<std::uint16_t, 4> latched_xscroll{};
};

inline constexpr unsigned kFrameWidth = 320;
inline constexpr unsigned kFrameHeight = 224;

struct TilemapOutput {
    std::vector<std::uint16_t> palette_indices;
    std::vector<std::uint8_t> priorities;
    std::vector<std::uint8_t> shadow_pixels;
};

using StageHook = std::function<void(TilemapOutput&, unsigned)>;

// Persistent software pixmap cache, matching the ASIC-facing tile invalidation
// behavior used by the board renderer. RAM changes only rebuild affected tiles.
class TilemapRenderer {
public:
    // Call this if decoded graphics are changed in place between renders.
    void invalidate_graphics() noexcept { m_cache_initialized = false; }
    TilemapOutput render(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
        TilemapState state = {}, const StageHook& after_stage = {});

private:
    void refresh_cache(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
        const TilemapState& state);

    std::array<std::uint8_t, 8> m_banks{};
    std::uint16_t m_bank_size = 0;
    bool m_cache_initialized = false;
    const std::uint8_t* m_graphics_data = nullptr;
    std::size_t m_graphics_size = 0;
    std::vector<std::uint16_t> m_tile_words;
    std::vector<std::uint16_t> m_text_words;
    std::vector<std::uint8_t> m_tile_valid;
    std::vector<std::uint8_t> m_text_valid;
    std::vector<std::uint16_t> m_tile_pixels;
    std::vector<std::uint16_t> m_text_pixels;
};

TileAttributes decode_tile_attributes(std::uint16_t word, TileFormat format,
    std::span<const std::uint8_t> banks, std::uint16_t bank_size);

// The ASIC samples page and scroll registers at the frame boundary.
TilemapState latch_frame_registers(std::span<const std::uint8_t> text_ram,
    TilemapState state = {});

// Generate the 315-5197 tilemap layers. Board video mixers may modify the
// intermediate output after each hardware composition stage.
TilemapOutput render_tilemaps(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    TilemapState state = {}, const StageHook& after_stage = {});

} // namespace multiarcaderecomp::chips::sega3155197
