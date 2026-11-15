#include "runtime/sega/runner/rom_commands.hpp"
#include "boards/sega/common/sega_address_space.hpp"
#include "runtime/sega/board_runtime.hpp"

#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
#endif
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" int rc_raw_main(int argc, char** argv);

namespace mamerecomp::sega {
namespace {
std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 3 >= bytes.size()) throw std::runtime_error("reset vectors are truncated");
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::vector<std::uint8_t> load_profile_cpu(const mamerecomp::GameProfile& profile,
    const std::filesystem::path& zip_path) {
    auto bytes = mamerecomp::load_maincpu_roms(profile, zip_path);
    std::cout << "Profile: " << profile.id << " (" << profile.title << ")\n"
              << "Board: " << mamerecomp::board_family_name(mamerecomp::board_family(profile)) << "\n"
              << "Validated and assembled " << profile.maincpu_roms.size() << " 68000 ROMs into 0x"
              << std::hex << profile.maincpu_size << " bytes\n";
    return bytes;
}

std::vector<std::uint8_t> decrypt_fd1094_for_translation(
    const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    const std::vector<std::uint8_t>& encrypted) {
    if (profile.fd1094_key_region_size == 0) return encrypted;
    // Static AOT discovery uses the same power-on decryption implemented by
    // the selected System16B runtime. Runtime callbacks preserve state changes.
    return decrypt_board_initial_program(profile, zip_path, encrypted);
}

void stop_fd1094_aot_at_state_changes(const std::filesystem::path& output_dir,
    std::string_view prefix) {
    const std::regex state_change_pattern(
        R"(/\*\s*([0-9A-Fa-f]{6,8}):\s+cmpi\.l\s+#\$([0-9A-Fa-f]+),\s*D0\s*\*/)"
    );
    std::optional<std::uint32_t> first_state_change;
    const auto prefix_name = std::string(prefix);
    for (const auto& item : std::filesystem::directory_iterator(output_dir)) {
        if (!item.is_regular_file()) continue;
        const auto filename = item.path().filename().string();
        if (!filename.starts_with(prefix_name + "recomp_") || item.path().extension() != ".c"
            || filename.ends_with("_table.c")) continue;
        std::ifstream input(item.path());
        std::string line;
        std::smatch match;
        while (std::getline(input, line)) {
            if (!std::regex_search(line, match, state_change_pattern)) continue;
            const auto immediate = static_cast<std::uint32_t>(std::stoul(match[2].str(), nullptr, 16));
            if ((immediate & 0xffffU) != 0xffffU) continue;
            const auto address = static_cast<std::uint32_t>(std::stoul(match[1].str(), nullptr, 16));
            if (!first_state_change || address < *first_state_change) first_state_change = address;
        }
    }
    if (!first_state_change) return;

    const auto table_path = output_dir / (prefix_name + "recomp_table.c");
    std::ifstream input(table_path);
    if (!input) throw std::runtime_error("could not read generated FD1094 dispatch table");
    std::vector<std::string> lines;
    std::size_t retained_entries = 0;
    const std::regex entry_pattern(R"(^\s*\{0x([0-9A-Fa-f]+)u,.*\},\s*$)");
    std::string line;
    std::smatch match;
    while (std::getline(input, line)) {
        if (std::regex_match(line, match, entry_pattern)) {
            const auto address = static_cast<std::uint32_t>(std::stoul(match[1].str(), nullptr, 16));
            if (address >= *first_state_change) continue;
            ++retained_entries;
        }
        lines.push_back(line);
    }
    input.close();
    std::ofstream output(table_path, std::ios::trunc);
    if (!output) throw std::runtime_error("could not update generated FD1094 dispatch table");
    const std::regex count_pattern(R"(^(const size_t [A-Za-z0-9_]+_ng_recomp_count = )[0-9]+;$)");
    for (auto& table_line : lines) {
        if (std::regex_match(table_line, match, count_pattern)) {
            table_line = match[1].str() + std::to_string(retained_entries) + ";";
        }
        output << table_line << '\n';
    }
    std::cout << "FD1094 static AOT stops before state switch at 0x" << std::hex << *first_state_change
              << std::dec << "; retained " << retained_entries << " dispatch entries\n";
}
}
void print_game_profiles() {
    for (const auto& profile : mamerecomp::game_profiles()) {
        std::cout << profile.id << "  " << profile.title << "  [" << mamerecomp::board_family_name(mamerecomp::board_family(profile)) << "]\n";
    }
}


const mamerecomp::GameProfile& require_profile(std::string_view id) {
    const auto* profile = mamerecomp::find_game_profile(id);
    if (!profile) {
        throw std::runtime_error("unknown game profile: " + std::string(id) + " (run 'mamerecomp list-games')");
    }
    return *profile;
}

void inspect_game(const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path* output_path) {
    const auto maincpu = load_profile_cpu(profile, zip_path);
    if (profile.tile_region_size != 0 && profile.sprite_region_size != 0) {
        const auto graphics = mamerecomp::load_graphics_roms(profile, zip_path);
        std::cout << "Validated raw graphics ROM regions: tiles=0x" << std::hex << graphics.tiles.size()
                  << " sprites=0x" << graphics.sprites.size() << std::dec << " bytes\n";
    }
    if (profile.soundcpu_region_size != 0) {
        const auto soundcpu = mamerecomp::load_soundcpu_roms(profile, zip_path);
        std::cout << "Validated sound CPU ROM region: 0x" << std::hex << soundcpu.size() << std::dec << " bytes\n";
    }
    if (profile.mcu_region_size != 0) {
        const auto mcu = mamerecomp::load_mcu_roms(profile, zip_path);
        std::cout << "Validated MCU ROM region: 0x" << std::hex << mcu.size() << std::dec << " bytes\n";
    }
    const std::uint32_t initial_sp = read_be32(maincpu, 0);
    const std::uint32_t initial_pc = read_be32(maincpu, 4);
    std::cout << "Initial 68000 SP: 0x" << std::hex << std::setw(8) << std::setfill('0') << initial_sp << '\n'
              << "Initial 68000 PC: 0x" << std::setw(8) << initial_pc << '\n';

    if ((initial_pc & 1U) != 0 || initial_pc + 2 > maincpu.size()) {
        throw std::runtime_error("reset PC is not a valid address in the main CPU ROM region");
    }

    const std::size_t word_count = std::min<std::size_t>(16, (maincpu.size() - initial_pc) / 2);
    std::cout << "Initial 68000 words:\n";
    for (std::size_t i = 0; i < word_count; ++i) {
        const std::size_t address = initial_pc + i * 2;
        std::cout << "  0x" << std::setw(6) << address << ": 0x"
                  << std::setw(4) << ((static_cast<unsigned>(maincpu[address]) << 8) | maincpu[address + 1]) << '\n';
    }

    if (output_path) {
        if (output_path->has_parent_path()) {
            std::filesystem::create_directories(output_path->parent_path());
        }
        std::ofstream output(*output_path, std::ios::binary);
        if (!output) {
            throw std::runtime_error("could not create output image: " + output_path->string());
        }
        output.write(reinterpret_cast<const char*>(maincpu.data()), static_cast<std::streamsize>(maincpu.size()));
        if (!output) {
            throw std::runtime_error("failed while writing output image: " + output_path->string());
        }
        std::cout << "Wrote main CPU image to " << output_path->string() << '\n';
    }
}

void translate_full_game(const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path& output_dir, const std::vector<std::uint32_t>& additional_entries,
    std::size_t output_files, bool scan_pointers) {
    const auto encrypted_maincpu = load_profile_cpu(profile, zip_path);
    const auto maincpu = decrypt_fd1094_for_translation(profile, zip_path, encrypted_maincpu);
    const std::uint32_t initial_pc = read_be32(maincpu, 4);
    if ((initial_pc & 1U) != 0 || initial_pc + 2 > maincpu.size()) {
        throw std::runtime_error("reset PC is not a valid address in the main CPU ROM region");
    }
    std::vector<std::uint32_t> entry_points{initial_pc};
    // Exception and interrupt handlers are independent control-flow roots:
    // the ROM scanner cannot discover them by following the reset path.
    // Seed the standard 68000 vectors so IRQs and generated exceptions resume
    // in translated code instead of stopping at an unregistered handler PC.
    for (std::uint32_t vector = 1; vector < 64; ++vector) {
        const std::uint32_t address = read_be32(maincpu, vector * 4);
        if (address == 0 || (address & 1U) != 0 || address + 2 > maincpu.size()) continue;
        if (std::find(entry_points.begin(), entry_points.end(), address) == entry_points.end()) {
            entry_points.push_back(address);
        }
    }
    for (const auto address : profile.m68k_entry_points) {
        if ((address & 1U) != 0 || address + 2 > maincpu.size()) {
            throw std::runtime_error("profile 68000 entry must be aligned and inside the main CPU ROM region: 0x"
                + [&] { std::ostringstream out; out << std::hex << address; return out.str(); }());
        }
        if (std::find(entry_points.begin(), entry_points.end(), address) == entry_points.end()) {
            entry_points.push_back(address);
        }
    }
    for (const auto address : additional_entries) {
        if ((address & 1U) != 0 || address + 2 > maincpu.size()) {
            throw std::runtime_error("additional 68000 entry must be aligned and inside the main CPU ROM region: 0x"
                + [&] { std::ostringstream out; out << std::hex << address; return out.str(); }());
        }
        if (std::find(entry_points.begin(), entry_points.end(), address) == entry_points.end()) {
            entry_points.push_back(address);
        }
    }

    std::filesystem::create_directories(output_dir);
    const auto entry_file = output_dir / (std::string(profile.id) + "_translation_entries.txt");
    {
        std::ofstream output(entry_file, std::ios::trunc);
        if (!output) throw std::runtime_error("could not create 68000 translation entry list: " + entry_file.string());
        output << std::hex << std::setfill('0');
        for (const auto address : entry_points) output << std::setw(6) << address << '\n';
        if (!output) throw std::runtime_error("failed writing 68000 translation entry list: " + entry_file.string());
    }
    const auto sound_rom = mamerecomp::load_soundcpu_roms(profile, zip_path);
    const auto z80_dir = output_dir / "z80_input";
    std::filesystem::create_directories(z80_dir);
    std::vector<std::pair<std::uint64_t, std::uint16_t>> observed_z80_entries;
    const auto fallback_log = output_dir / "z80_fallbacks.txt";
    if (std::ifstream input(fallback_log); input) {
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream record(line);
            std::string kind, source, pc_field, opcode_field, mapping_field, count_field;
            if (!(record >> kind >> source >> pc_field >> opcode_field >> mapping_field >> count_field)
                || kind != "UNCOVERED_ENTRY" || source != "CART"
                || !pc_field.starts_with("PC=") || !mapping_field.starts_with("MAP=")
                || !count_field.starts_with("COUNT=")) continue;
            try {
                const auto pc = std::stoul(pc_field.substr(3), nullptr, 16);
                const auto mapping = std::stoul(mapping_field.substr(4), nullptr, 16);
                const auto count = std::stoull(count_field.substr(6));
                if (pc < 0x8000U && mapping == 0 && count >= 100)
                    observed_z80_entries.emplace_back(count, static_cast<std::uint16_t>(pc));
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    std::sort(observed_z80_entries.begin(), observed_z80_entries.end(),
        [](const auto& left, const auto& right) { return left.first > right.first; });
    std::vector<std::uint16_t> z80_entries{0x0066};
    for (const auto& [count, address] : observed_z80_entries) {
        (void)count;
        if (std::find(z80_entries.begin(), z80_entries.end(), address) == z80_entries.end())
            z80_entries.push_back(address);
        if (z80_entries.size() >= 33) break;
    }
    {
        std::ofstream sound_image(z80_dir / "sound.bin", std::ios::binary);
        if (!sound_image) throw std::runtime_error("could not create Z80 sound image");
        sound_image.write(reinterpret_cast<const char*>(sound_rom.data()),
            static_cast<std::streamsize>(std::min<std::size_t>(sound_rom.size(), 0x8000)));
        std::ofstream extra_entries(z80_dir / "entries.txt");
        if (!extra_entries) throw std::runtime_error("could not create Z80 entry list");
        extra_entries << std::hex << std::uppercase << std::setfill('0');
        for (const auto address : z80_entries) extra_entries << std::setw(4) << address << '\n';
        if (!extra_entries) throw std::runtime_error("failed writing Z80 entry list");
    }
    const auto raw_path = output_dir / (std::string(profile.id) + "_maincpu.bin");
    {
        std::ofstream raw(raw_path, std::ios::binary);
        if (!raw) throw std::runtime_error("could not create raw CPU image: " + raw_path.string());
        raw.write(reinterpret_cast<const char*>(maincpu.data()), static_cast<std::streamsize>(maincpu.size()));
        if (!raw) throw std::runtime_error("failed while writing raw CPU image: " + raw_path.string());
    }

    const std::string prefix = std::string(profile.id) + "_";
    std::vector<std::string> arguments{
        "m68krecomp", "--raw", std::filesystem::absolute(raw_path).string(),
        "--base", "0", "--entry", std::to_string(initial_pc),
        "--entries", std::filesystem::absolute(entry_file).string(), "--board", "sega"
    };
    arguments.insert(arguments.end(), {
        "--prefix", prefix, "--out", std::filesystem::absolute(output_dir).string(),
        "--files", std::to_string(std::max<std::size_t>(1, output_files))
    });
    if (!scan_pointers) arguments.emplace_back("--no-pointer-scan");
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    std::cout << "Full 68000 frontend: " << std::dec << entry_points.size() << " entry point(s), reset PC 0x" << std::hex << initial_pc
              << "; output directory " << std::filesystem::absolute(output_dir) << '\n';
    if (rc_raw_main(static_cast<int>(arguments.size()), argv.data()) != 0) {
        throw std::runtime_error("NeoGeoRecomp raw 68000 frontend failed");
    }

    if (profile.fd1094_key_region_size != 0) {
        stop_fd1094_aot_at_state_changes(output_dir, prefix);
    }

    std::ofstream wrapper(output_dir / "mamerecomp_generated_table.c");
    if (!wrapper) throw std::runtime_error("could not create generated table adapter");
    wrapper << "#include <string.h>\n#include <neogeorecomp/neogeo.h>\n"
            << "extern const ng_func_entry_t " << prefix << "ng_recomp_table[];\n"
            << "extern const size_t " << prefix << "ng_recomp_count;\n"
            << "const ng_func_entry_t* mamerecomp_generated_table(const char* profile_id, size_t* count) {\n"
            << "    if (strcmp(profile_id, \"" << prefix << "\") != 0) { *count = 0; return NULL; }\n"
            << "    *count = " << prefix << "ng_recomp_count;\n"
            << "    return " << prefix << "ng_recomp_table;\n"
            << "}\n";
    if (!wrapper) throw std::runtime_error("failed while writing generated table adapter");
    std::cout << "Wrote build adapter: " << output_dir / "mamerecomp_generated_table.c" << '\n';
}

void extract_graphics(const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path& output_dir) {
    if (profile.tile_region_size == 0 || profile.sprite_region_size == 0) {
        throw std::runtime_error("profile has no graphics ROM layout");
    }
    const auto graphics = mamerecomp::load_graphics_roms(profile, zip_path);
    std::filesystem::create_directories(output_dir);
    const auto write_region = [](const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("could not create graphics region: " + path.string());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("failed while writing graphics region: " + path.string());
    };
    const auto decoded_tiles = decode_board_tiles(profile, graphics.tiles);
    const auto tiles_path = output_dir / (std::string(profile.id) + "_tiles.bin");
    const auto decoded_path = output_dir / (std::string(profile.id) + "_tile_pixels.bin");
    const auto sprites_path = output_dir / (std::string(profile.id) + "_sprites.bin");
    write_region(tiles_path, graphics.tiles);
    write_region(decoded_path, decoded_tiles);
    write_region(sprites_path, graphics.sprites);
    std::cout << "Validated and assembled graphics ROM regions:\n"
              << "  tiles:        0x" << std::hex << graphics.tiles.size() << " bytes -> " << tiles_path << '\n'
              << "  decoded tiles: " << std::dec << decoded_tiles.size() / 64 << " 8x8 tiles -> " << decoded_path << '\n'
              << "  sprites:      0x" << std::hex << graphics.sprites.size() << " bytes -> " << sprites_path << '\n';
}

void print_memory_map(const mamerecomp::GameProfile& profile, const std::vector<std::uint8_t>& maincpu) {
    for (const auto& region : inspect_board_memory_map(profile, maincpu)) {
        std::cout << "  " << std::left << std::setw(10) << region.name << std::right << "  0x" << std::hex
                  << std::setw(6) << std::setfill('0') << region.start << "-0x"
                  << std::setw(6) << (region.start + region.size - 1) << "  mirror=0x"
                  << std::setw(6) << region.mirror_mask << std::setfill(' ')
                  << (region.device ? "  device" : (region.writable ? "  RAM" : "  ROM")) << "\n";
    }
}

int run_rom_command(int argc, char** argv) {
    if (argc < 4) throw std::runtime_error("Sega ROM command requires a game profile and ROM ZIP");
    const std::string_view command(argv[1]);
    const auto& profile = require_profile(argv[2]);
    const std::filesystem::path zip_path(argv[3]);

    if (command == "extract-graphics") {
        std::filesystem::path output_dir = std::filesystem::path("build") / "graphics" / std::string(profile.id);
        if (argc == 6 && std::string_view(argv[4]) == "--out") output_dir = argv[5];
        else if (argc != 4) throw std::runtime_error("usage: extract-graphics <profile> <romset.zip> [--out <directory>]");
        extract_graphics(profile, zip_path, output_dir);
        return 0;
    }

    if (command == "map") {
        const auto maincpu = load_maincpu_roms(profile, zip_path);
        std::cout << board_family_name(board_family(profile))
                  << " address map (24-bit 68000 bus; mapper reset state):\n";
        print_memory_map(profile, maincpu);
        return 0;
    }

    if (command == "inspect") {
        std::filesystem::path output_path;
        const std::filesystem::path* output_path_ptr = nullptr;
        if (argc == 6 && std::string_view(argv[4]) == "--write") {
            output_path = argv[5];
            output_path_ptr = &output_path;
        } else if (argc != 4) {
            throw std::runtime_error("usage: inspect <profile> <romset.zip> [--write <output.toml]");
        }
        inspect_game(profile, zip_path, output_path_ptr);
        return 0;
    }

    if (command == "translate-full") {
        auto output_dir = std::filesystem::path("build") / "generated" / std::string(profile.id);
        std::vector<std::uint32_t> additional_entries;
        std::optional<std::filesystem::path> entries_file;
        bool scan_pointers = false;
        std::size_t output_files = 16;
        for (int i = 4; i < argc; ++i) {
            const std::string_view option(argv[i]);
            if (option == "--out" && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (option == "--entry" && i + 1 < argc) {
                std::string_view value(argv[++i]);
                int base = 10;
                if (value.starts_with("0x") || value.starts_with("0X")) {
                    value.remove_prefix(2);
                    base = 16;
                }
                std::uint32_t address = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), address, base);
                if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    throw std::runtime_error("invalid 68000 entry address: " + std::string(value));
                additional_entries.push_back(address);
            } else if (option == "--entries-file" && i + 1 < argc) {
                entries_file = argv[++i];
            } else if (option == "--files" && i + 1 < argc) {
                std::string_view value(argv[++i]);
                int base = 10;
                if (value.starts_with("0x") || value.starts_with("0X")) {
                    value.remove_prefix(2);
                    base = 16;
                }
                std::uint32_t parsed_value = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value, base);
                if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                    || parsed_value == 0 || parsed_value > 4096)
                    throw std::runtime_error("--files must be between 1 and 4096");
                output_files = parsed_value;
            } else if (option == "--no-pointer-scan") {
                scan_pointers = false;
            } else if (option == "--pointer-scan") {
                scan_pointers = true;
            } else {
                throw std::runtime_error("unknown translate-full option: " + std::string(option));
            }
        }
        if (entries_file) {
            std::ifstream input(*entries_file);
            if (!input) throw std::runtime_error("could not open runtime 68000 entries: " + entries_file->string());
            std::string line;
            while (std::getline(input, line)) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '#') continue;
                std::string_view value(line.data() + first, line.size() - first);
                const auto last = value.find_last_not_of(" \t\r");
                value = value.substr(0, last + 1);
                int base = 16;
                if (value.starts_with("0x") || value.starts_with("0X")) value.remove_prefix(2);
                std::uint32_t address = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), address, base);
                if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                    throw std::runtime_error("invalid runtime 68000 entry in " + entries_file->string() + ": " + line);
                additional_entries.push_back(address);
            }
        }
        translate_full_game(profile, zip_path, output_dir, additional_entries, output_files, scan_pointers);
        return 0;
    }

    throw std::runtime_error("unknown Sega ROM command: " + std::string(command));
}



} // namespace mamerecomp::sega
