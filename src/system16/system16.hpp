#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace mamerecomp {

enum class BoardFamily {
    System16A,
    System16B,
    System18,
};

enum class System16BBoard : std::uint8_t { Sega5358, Sega5521, Sega5797 };
enum class System18RomBoard : std::uint8_t { ShadowDancer171, Sega5874 };

struct RomLoad16Byte {
    std::string_view filename;
    std::uint32_t destination;
    std::uint32_t size;
    std::uint32_t crc32;
    std::string_view merge_filename{};
};

struct RomRegionLoad {
    std::string_view filename;
    std::uint32_t file_size;
    std::uint32_t crc32;
    std::uint32_t source_offset;
    std::uint32_t destination;
    std::uint32_t length;
    std::uint8_t destination_stride;
    std::string_view merge_filename{};
};

struct GameProfile {
    std::string_view id;
    std::string_view title;
    BoardFamily board_family;
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
    System16BBoard board = System16BBoard::Sega5797;
    std::string_view parent_id{};
    std::uint8_t soundcpu_fill = 0;
    System18RomBoard system18_rom_board = System18RomBoard::ShadowDancer171;
    std::uint32_t fd1094_key_region_size = 0;
    std::span<const RomRegionLoad> fd1094_key_roms{};
};

struct System16GraphicsRoms {
    std::vector<std::uint8_t> tiles;
    std::vector<std::uint8_t> sprites;
};

struct System16ASoundRoms {
    std::vector<std::uint8_t> soundcpu;
    std::vector<std::uint8_t> upd7751;
    std::vector<std::uint8_t> upd7751_data;
};

std::span<const GameProfile> game_profiles();
const GameProfile* find_game_profile(std::string_view id);
std::vector<std::uint8_t> load_maincpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
System16GraphicsRoms load_graphics_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
System16ASoundRoms load_system16a_sound_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_soundcpu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_mcu_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
std::vector<std::uint8_t> load_fd1094_key_roms(const GameProfile& profile, const std::filesystem::path& zip_path);
const char* board_family_name(BoardFamily family);

} // namespace mamerecomp
