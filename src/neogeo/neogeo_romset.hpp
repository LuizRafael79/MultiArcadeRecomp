#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace multiarcaderecomp::neogeo {

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

// ROM/profile validation for KOF98. Program bytes remain raw here; the game
// module supplies its PROGSF1 transform to the board runtime.
RomSetInfo load_kof98(const std::filesystem::path& game_zip,
                      const std::filesystem::path& bios_zip,
                      std::string_view bios_name = {});
void extract_kof98_inputs(const std::filesystem::path& game_zip,
                          const std::filesystem::path& bios_zip,
                          const std::filesystem::path& game_directory,
                          const std::filesystem::path& bios_directory,
                          const std::filesystem::path& system_rom_zip = {});

} // namespace multiarcaderecomp::neogeo
