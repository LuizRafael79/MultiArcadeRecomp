#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mamerecomp::neogeo {

struct RomRegionInfo {
    std::string name;
    std::uint32_t size;
    std::size_t loads;
    std::uint64_t bytes_loaded;
};

struct RomSetInfo {
    std::string set_id;
    std::size_t files_loaded;
    std::vector<RomRegionInfo> regions;
    std::vector<std::uint8_t> maincpu;
    std::string bios_filename;
    std::vector<std::uint8_t> bios;
};

// KOF98 bootstrap prototype: BIOS is loaded from neogeo.zip and the game's
// PROGSF1 program transform is applied before the image is returned.
RomSetInfo load_kof98(const std::filesystem::path& game_zip,
                      const std::filesystem::path& bios_zip);
void decrypt_kof98_program(std::vector<std::uint8_t>& program);
void extract_kof98_inputs(const std::filesystem::path& game_zip,
                          const std::filesystem::path& bios_zip,
                          const std::filesystem::path& game_directory,
                          const std::filesystem::path& bios_directory);

} // namespace mamerecomp::neogeo
