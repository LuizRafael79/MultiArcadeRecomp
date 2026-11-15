#include "kof98_game.h"
#include "neogeo/neogeo_romset.hpp"

#include "recomp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint32_t read_be32(const std::vector<std::uint8_t>& image, std::size_t offset) {
    if (offset + 4 > image.size()) return 0;
    return (std::uint32_t(image[offset]) << 24) | (std::uint32_t(image[offset + 1]) << 16)
        | (std::uint32_t(image[offset + 2]) << 8) | image[offset + 3];
}

void write_image(const std::filesystem::path& path, const std::vector<std::uint8_t>& image) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("could not write AoT image " + path.string());
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) throw std::runtime_error("failed writing AoT image " + path.string());
}

void write_roots(const std::filesystem::path& path,
                 const multiarcaderecomp::neogeo::RomSetInfo& roms) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("could not write Neo Geo 68000 roots " + path.string());
    // Match the Neo Geo board entry discovery used by the former frontend:
    // exception vectors from both address-space ROMs, plus the BIOS's four
    // game-facing header entry points.
    for (std::uint32_t vector = 1; vector < 64; ++vector) {
        const auto offset = std::size_t(vector) * 4;
        out << std::hex << std::uppercase << read_be32(roms.maincpu, offset) << '\n';
        out << std::hex << std::uppercase << read_be32(roms.bios, offset) << '\n';
    }
    for (const std::uint32_t address : std::array<std::uint32_t, 4>{0x122, 0x128, 0x12E, 0x134})
        out << std::hex << std::uppercase << address << '\n';
    if (!out) throw std::runtime_error("failed writing Neo Geo 68000 roots " + path.string());
}
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path game_zip, bios_zip, output_dir = "build/neogeo/kof98";
        std::string bios_name = "uni-bios_4_0.rom";
        std::vector<std::filesystem::path> extra_entries;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--rom-zip" && i + 1 < argc) game_zip = argv[++i];
            else if (arg == "--bios-zip" && i + 1 < argc) bios_zip = argv[++i];
            else if (arg == "--bios" && i + 1 < argc) bios_name = argv[++i];
            else if (arg == "--out" && i + 1 < argc) output_dir = argv[++i];
            else if (arg == "--entries" && i + 1 < argc) extra_entries.emplace_back(argv[++i]);
            else throw std::runtime_error("usage: kof98_aot --rom-zip kof98.zip [--bios-zip neogeo.zip] [--bios FILE] [--out DIR] [--entries FILE]");
        }
        if (game_zip.empty()) throw std::runtime_error("--rom-zip is required");
        if (bios_zip.empty()) bios_zip = game_zip.parent_path() / "neogeo.zip";

        auto roms = multiarcaderecomp::neogeo::load_kof98(game_zip, bios_zip, bios_name);
        multiarcaderecomp_kof98_decrypt(roms.maincpu.data(), roms.maincpu.size());
        if (roms.maincpu.size() < 0x200000)
            throw std::runtime_error("KOF98 decrypted program image is smaller than its fixed and bank windows");

        const auto assets = output_dir / "aot-input";
        const auto generated = output_dir / "generated";
        std::filesystem::create_directories(assets);
        std::filesystem::create_directories(generated);
        const auto fixed_program_path = assets / "maincpu-fixed.bin";
        const auto bank_program_path = assets / "maincpu-bank-window.bin";
        const auto bios_path = assets / "mainbios.bin";
        const auto roots_path = assets / "neogeo-board-roots.txt";
        const std::vector<std::uint8_t> fixed_program(roms.maincpu.begin(), roms.maincpu.begin() + 0x100000);
        const std::vector<std::uint8_t> bank_program(roms.maincpu.begin() + 0x100000, roms.maincpu.end());
        write_image(fixed_program_path, fixed_program);
        write_image(bank_program_path, bank_program);
        write_image(bios_path, roms.bios);
        write_roots(roots_path, roms);

        std::vector<std::string> args{
            "m68krecomp", "--raw", fixed_program_path.string(), "--base", "0",
            "--region", "0x200000", bank_program_path.string(),
            "--region", "0xC00000", bios_path.string(), "--board", "neogeo",
            "--entry", "0x122", "--out", generated.string(), "--files", "128",
            "--entries", roots_path.string()
        };
        for (const auto& entries : extra_entries) {
            args.emplace_back("--entries");
            args.push_back(entries.string());
        }
        std::vector<char*> cargs;
        cargs.reserve(args.size() + 1);
        for (auto& arg : args) cargs.push_back(arg.data());
        cargs.push_back(nullptr);

        std::cerr << "[kof98-aot] validated TOML ROM profile and BIOS " << roms.bios_filename
                  << "; applying the game-owned PROGSF1 transform, then invoking the shared "
                     "m68krecomp frontend with the Neo Geo board discovery profile and all "
                     "physical P-ROM banks\n";
        const int result = rc_raw_main(static_cast<int>(args.size()), cargs.data());
        if (result) return result;
        std::cout << "KOF98 AOT source generated in " << std::filesystem::absolute(generated)
                  << " by the shared MultiArcadeRecomp 68000 frontend.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "kof98_aot: " << e.what() << '\n';
        return 1;
    }
}
