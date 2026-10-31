#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace mamerecomp {

struct System16GameSettings;

// Decode the shared Sega 8x8x3 planar graphics layout into pen indices.
// System 16A and Golden Axe System 16B use the same MAME gfx_8x8x3_planar
// bitplane ordering; their tilemap ASICs and tile attributes remain distinct.
std::vector<std::uint8_t> decode_system16_tiles_3bpp(std::span<const std::uint8_t> region);
std::vector<std::uint8_t> decode_system16a_tiles(std::span<const std::uint8_t> region);
std::vector<std::uint8_t> decode_system16b_tiles(std::span<const std::uint8_t> region);

enum class System16BTileFormat {
    Tile,
    Text,
    AlternateTile,
    AlternateText,
};

struct System16BTileAttributes {
    std::uint16_t code = 0;
    std::uint8_t color = 0;
    bool high_priority = false;
};

struct System16Frame {
    static constexpr int width = 320;
    static constexpr int height = 224;
    std::vector<std::uint16_t> palette_indices;
    std::vector<std::uint8_t> tile_priorities;
    std::vector<std::uint8_t> shadow_pixels;
    // Direct RGB lookup for auxiliary video layers such as the System 18 VDP.
    std::array<std::uint32_t, 64> vdp_palette_rgb{};
};

using System16AFrame = System16Frame;
using System16BFrame = System16Frame;

// Decode a 16B tile/text RAM word using the 315-5197 format. bank_size and
// bank selection match segaic16_video_device::tilemap_init/tilemap_set_bank.
System16BTileAttributes decode_system16b_tile_attributes(std::uint16_t word,
    System16BTileFormat format, std::span<const std::uint8_t> banks, std::uint16_t bank_size);

struct System16BTilemapState {
    bool display_enable = true;
    bool flip = false;
    std::array<std::uint8_t, 8> banks{0, 1, 2, 3, 4, 5, 6, 7};
    std::uint16_t bank_size = 0x1000;
    bool registers_latched = false;
    std::array<std::uint16_t, 4> latched_pages{};
    std::array<std::uint16_t, 4> latched_yscroll{};
    std::array<std::uint16_t, 4> latched_xscroll{};
};

// Called after each board tilemap stage: 0=opaque background, 1=transparent
// background, 2=foreground, 3=text. System 18 inserts the VDP at one of these
// points using its video-control latch.
using System16BLayerHook = std::function<void(System16BFrame&, unsigned)>;

// The 315-5197 samples page/scroll registers at the board's scanline-261
// latch. Call once at that frame boundary and use the returned state for the
// following frame; row/column scroll tables remain live RAM.
System16BTilemapState latch_system16b_tilemap_registers(std::span<const std::uint8_t> text_ram,
    System16BTilemapState state = {});

// CPU reference compositor for the 315-5197 normal System 16B mode. It
// consumes the ASIC's tile/text RAM and latched registers, and preserves the
// priority marks used later by the 315-5196 sprite mixer.
System16BFrame render_system16b_tilemaps_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    System16BTilemapState state = {}, const System16BLayerHook& after_stage = {});
// 315-5196 sprite generator and System 16B priority mixer. sprite banks use
// MAME's 16-entry selector (default identity); the board normally supplies
// 0x20000-byte sprite banks.
void draw_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks = {}, bool flip = false);
std::vector<std::uint16_t> rasterize_system16b_sprites_cpu(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks = {}, bool flip = false);
void mix_system16b_sprites_cpu(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels);
System16BFrame render_system16b_frame_cpu(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    System16BTilemapState state = {}, std::span<const std::uint8_t> sprite_banks = {},
    const System16BLayerHook& after_stage = {});
bool system16b_opengl_backend_available();
System16BFrame render_system16b_tilemaps_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    System16BTilemapState state = {});
void mix_system16b_sprites_opengl(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels);
System16BFrame render_system16b_frame(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> tile_ram,
    std::span<const std::uint8_t> text_ram, std::span<std::uint8_t> sprite_ram,
    System16BTilemapState state = {}, std::span<const std::uint8_t> sprite_banks = {},
    bool use_opengl = false);

struct System16ARenderOptions {
    bool display_enable = true;
    bool flip = false;
    bool row_scroll_enabled = false;
    bool column_scroll_enabled = false;
};

enum class System16AVideoBackend {
    Software,
    OpenGL,
};

struct System16AVideoProfile {
    std::uint64_t background_ns = 0;
    std::uint64_t foreground_ns = 0;
    std::uint64_t text_ns = 0;
    std::uint64_t sprites_ns = 0;
    std::uint64_t render_ns = 0;
    std::uint64_t page_pixel_visits = 0;
    std::uint64_t page_pen_lookups = 0;
    std::uint64_t background_palette_writes = 0;
    std::uint64_t foreground_palette_writes = 0;
    std::uint64_t text_pixel_visits = 0;
    std::uint64_t text_pen_lookups = 0;
    std::uint64_t text_palette_writes = 0;
    std::uint64_t priority_marks = 0;
    std::uint64_t sprite_words_read = 0;
    std::uint64_t sprite_pen_lookups = 0;
    std::uint64_t sprite_pixels_rasterized = 0;
    std::uint64_t sprite_pixels_mixed = 0;
    std::uint64_t sprite_shadow_pixels = 0;
    std::uint64_t gpu_setup_ns = 0;
    std::uint64_t gpu_upload_ns = 0;
    std::uint64_t gpu_dispatch_readback_ns = 0;
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
std::vector<std::uint8_t> system16a_frame_rgb(const System16AFrame& frame,
    std::span<const std::uint8_t> palette_ram);
void write_system16a_frame_ppm(const System16AFrame& frame,
    std::span<const std::uint8_t> palette_ram, const std::filesystem::path& path);

struct System16ALiveInputs {
    std::uint8_t service = 0xff;
    std::uint8_t p1 = 0xff;
    std::uint8_t p2 = 0xff;
};

class System16APresenter {
public:
    explicit System16APresenter(std::string title);
    ~System16APresenter();
    System16APresenter(const System16APresenter&) = delete;
    System16APresenter& operator=(const System16APresenter&) = delete;
    bool present(const System16AFrame& frame, std::span<const std::uint8_t> palette_ram);
    System16ALiveInputs live_inputs() const;
    bool show_configuration_menu(System16GameSettings& settings, const std::filesystem::path& settings_path,
        const std::string& game_title);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool system16a_presenter_available();

} // namespace mamerecomp
