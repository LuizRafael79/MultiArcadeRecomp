#include "commands/neogeo_commands.hpp"

#include "neogeo/neogeo_romset.hpp"

#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace multiarcaderecomp::app::neogeo {

int inspect_roms(std::string_view game, const std::filesystem::path& game_zip) {
    if (game != "kof98")
        throw std::invalid_argument("unsupported Neo Geo ROM inspection target: " + std::string(game));
    const auto bios_path = game_zip.parent_path() / "neogeo.zip";
    const auto image = multiarcaderecomp::neogeo::load_kof98(game_zip, bios_path);
    std::cout << "Validated Neo Geo set " << image.set_id << ": " << image.files_loaded
              << " ROM files; BIOS=" << image.bios_filename
              << "; raw 68000 program assembled to 0x" << std::hex
              << image.maincpu.size() << std::dec << " bytes. Regions:\n";
    for (const auto& region : image.regions)
        std::cout << "  " << region.name << ": " << region.loads << " files, 0x"
                  << std::hex << region.bytes_loaded << "/0x" << region.size << std::dec
                  << " bytes loaded\n";
    std::cout << "ROM/profile validation passed. The KOF98 game module supplies PROGSF1 decryption at runtime.\n";
    return 0;
}

} // namespace multiarcaderecomp::app::neogeo
