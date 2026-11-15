#pragma once

#include "boards/sega/common/board_family.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace mamerecomp {

struct RomLoad16Byte {
    std::string_view filename;
    std::uint32_t destination;
    std::uint32_t size;
    std::uint32_t crc32;
    std::string_view sha1;
    std::string_view merge_filename{};
};

struct RomRegionLoad {
    std::string_view filename;
    std::uint32_t file_size;
    std::uint32_t crc32;
    std::string_view sha1;
    std::uint32_t source_offset;
    std::uint32_t destination;
    std::uint32_t length;
    std::uint8_t destination_stride;
    std::string_view merge_filename{};
};

struct System16AConfiguration {};

struct System16BConfiguration {
    System16BBoard rom_board = System16BBoard::Sega5797;
};

struct System18Configuration {
    System18RomBoard rom_board = System18RomBoard::ShadowDancer171;
};

using SegaBoardConfiguration = std::variant<
    System16AConfiguration,
    System16BConfiguration,
    System18Configuration>;

struct GameProfile {
    std::string_view id;
    std::string_view title;
    SegaBoardConfiguration board_configuration;
    bool has_interactive_configuration = false;
    std::optional<std::uint8_t> default_dsw1;
    std::optional<std::uint8_t> default_dsw2;
    std::uint32_t maincpu_size;
    std::span<const RomLoad16Byte> maincpu_roms;
    std::uint32_t tile_region_size;
    std::span<const RomRegionLoad> tile_roms;
    std::uint32_t sprite_region_size;
    std::span<const RomRegionLoad> sprite_roms;
    std::uint32_t soundcpu_region_size;
    std::span<const RomRegionLoad> soundcpu_roms;
    std::uint32_t upd7751_region_size;
    std::span<const RomRegionLoad> upd7751_roms;
    std::uint32_t upd7751_data_region_size;
    std::span<const RomRegionLoad> upd7751_data_roms;
    std::uint32_t mcu_region_size;
    std::span<const RomRegionLoad> mcu_roms;
    std::string_view parent_id{};
    std::uint8_t soundcpu_fill = 0;
    std::uint32_t fd1094_key_region_size = 0;
    std::span<const RomRegionLoad> fd1094_key_roms{};
    std::uint32_t pld_region_size = 0;
    std::span<const RomRegionLoad> pld_roms{};
    // Additional statically discovered or runtime-observed code roots that
    // cannot be reached by the initial vector/pointer scan.
    std::span<const std::uint32_t> m68k_entry_points{};
};

struct SegaGraphicsRoms {
    std::vector<std::uint8_t> tiles;
    std::vector<std::uint8_t> sprites;
};

std::span<const GameProfile> game_profiles();
const GameProfile* find_game_profile(std::string_view id);
SegaBoardFamily board_family(const GameProfile& profile);
std::vector<std::uint8_t> load_maincpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
SegaGraphicsRoms load_graphics_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_profile_rom_region(const GameProfile& profile,
    const std::filesystem::path& zip_path, std::uint32_t region_size,
    std::span<const RomRegionLoad> loads, std::uint8_t fill = 0);
std::vector<std::uint8_t> load_soundcpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
void validate_profile_sound_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_mcu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_fd1094_key_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_pld_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
const char* board_family_name(SegaBoardFamily family);

} // namespace mamerecomp
