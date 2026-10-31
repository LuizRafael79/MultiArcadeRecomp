#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mamerecomp::naomi {

struct NaomiRomBoardImage {
    std::string set_id;
    std::vector<std::uint8_t> bytes;
    std::size_t rom_files_loaded = 0;
    std::vector<std::uint8_t> board_id_eeprom;
};

struct NaomiBiosImage {
    std::string filename;
    std::vector<std::uint8_t> bytes;
    std::uint32_t crc = 0;
};

// Assembles the MAME-declared cartridge region. BIOS and EEPROM loading are
// separate so each can be checked and mapped according to its own metadata.
NaomiRomBoardImage load_mvc2_rom_board(const std::string& set_id,
                                       const std::filesystem::path& zip_path);

// Selects a regional 2 MiB Naomi BIOS from the standard naomi.zip archive,
// validating each candidate against Flycast/MAME CRC metadata.
NaomiBiosImage load_naomi_bios(const std::string& set_id,
                               const std::filesystem::path& zip_path);

} // namespace mamerecomp::naomi
