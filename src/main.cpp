#include "neogeo/neogeo_romset.hpp"
#include "system16/system16.hpp"
#include "system16/system16_bus.hpp"
#include "system16/system16b_mapper.hpp"
#include "system18/system18_bus.hpp"
#include "system18/system18_sound.hpp"
#include "naomi/naomi_module.hpp"
#include "naomi/naomi_romset.hpp"
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#endif
#include "system16/system16_video.hpp"
#include "system16/system16_config.hpp"
#include "system16/system16a_sound.hpp"
#include "system16/system16b_sound.hpp"
#include "system16/m68k_debugger.hpp"
#include "system16/m68k_recomp_dispatch.h"
#include "system16/system16_m68k_bus.h"

#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
#include <dlfcn.h>

#ifndef MAMERECOMP_SOURCE_DIR
#define MAMERECOMP_SOURCE_DIR "."
#endif
#ifndef MAMERECOMP_PYTHON_EXECUTABLE
#define MAMERECOMP_PYTHON_EXECUTABLE "python3"
#endif
#endif

extern "C" int rc_raw_main(int argc, char** argv);
#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
extern "C" void mamerecomp_debug_write_adapter(std::uint32_t, std::uint8_t, std::uint32_t);
extern "C" void mamerecomp_debug_read_adapter(std::uint32_t, std::uint8_t, std::uint32_t);
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>

namespace {

std::filesystem::path g_executable_directory;

class NaomiMetadataModule {
public:
    NaomiMetadataModule() {
        const char* configured_directory = std::getenv("MAMERECOMP_MODULE_DIR");
        const auto module_directory = configured_directory && *configured_directory
            ? std::filesystem::path(configured_directory) : g_executable_directory;
        const auto path = module_directory / "mamerecomp_naomi.so";
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle) throw std::runtime_error("could not load Naomi module " + path.string() + ": " + dlerror());
        const auto abi = reinterpret_cast<std::uint32_t (*)()>(dlsym(m_handle, "mamerecomp_naomi_abi"));
        m_profiles = reinterpret_cast<ProfilesFn>(dlsym(m_handle, "mamerecomp_naomi_profiles"));
        m_probe_create = reinterpret_cast<ProbeCreateFn>(dlsym(m_handle, "mamerecomp_naomi_sh4_probe_create"));
        m_probe_destroy = reinterpret_cast<ProbeDestroyFn>(dlsym(m_handle, "mamerecomp_naomi_sh4_probe_destroy"));
        m_probe_start = reinterpret_cast<ProbeStartFn>(dlsym(m_handle, "mamerecomp_naomi_sh4_probe_start"));
        m_probe_run = reinterpret_cast<ProbeRunFn>(dlsym(m_handle, "mamerecomp_naomi_sh4_probe_run"));
        m_probe_frame = reinterpret_cast<ProbeFrameFn>(dlsym(m_handle, "mamerecomp_naomi_sh4_probe_frame"));
        if (!abi || !m_profiles || !m_probe_create || !m_probe_destroy ||
            !m_probe_start || !m_probe_run || !m_probe_frame || abi() != 6) {
            const auto error = dlerror();
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("Naomi module ABI mismatch: " + path.string()
                + (error ? ": " + std::string(error) : std::string{}));
        }
    }
    ~NaomiMetadataModule() { if (m_handle) dlclose(m_handle); }
    NaomiMetadataModule(const NaomiMetadataModule&) = delete;
    NaomiMetadataModule& operator=(const NaomiMetadataModule&) = delete;
    const NaomiProfileRecord* profiles(std::size_t* count) const { return m_profiles(count); }
    void* create_probe(const char* set_id, bool interpreter_only) const {
        return m_probe_create(set_id, interpreter_only ? 1 : 0);
    }
    void destroy_probe(void* probe) const { m_probe_destroy(probe); }
    int start_probe(void* probe, const std::vector<std::uint8_t>& rom,
                    const std::vector<std::uint8_t>& board_id_eeprom,
                    const std::vector<std::uint8_t>& bios,
                    NaomiSh4ProbeResult* result) const {
        return m_probe_start(probe, rom.data(), rom.size(),
                             board_id_eeprom.data(), board_id_eeprom.size(),
                             bios.data(), bios.size(), result);
    }
    int run_probe(void* probe, std::uint64_t budget,
                  NaomiSh4ProbeResult* result) const {
        return m_probe_run(probe, budget, result);
    }
    bool capture_frame(void* probe, NaomiPvrFrameInfo* info,
                       std::vector<std::uint8_t>* rgb) const {
        if (!info || !rgb || !m_probe_frame(probe, info, nullptr, 0)) return false;
        rgb->resize(info->rgb_bytes);
        return m_probe_frame(probe, info, rgb->data(), rgb->size()) != 0;
    }
private:
    using ProfilesFn = const NaomiProfileRecord* (*)(std::size_t*);
    using ProbeCreateFn = void* (*)(const char*, int);
    using ProbeDestroyFn = void (*)(void*);
    using ProbeStartFn = int (*)(void*, const std::uint8_t*, std::size_t,
                                 const std::uint8_t*, std::size_t,
                                 const std::uint8_t*, std::size_t,
                                 NaomiSh4ProbeResult*);
    using ProbeRunFn = int (*)(void*, std::uint64_t, NaomiSh4ProbeResult*);
    using ProbeFrameFn = int (*)(void*, NaomiPvrFrameInfo*, std::uint8_t*, std::size_t);
    void* m_handle = nullptr;
    ProfilesFn m_profiles = nullptr;
    ProbeCreateFn m_probe_create = nullptr;
    ProbeDestroyFn m_probe_destroy = nullptr;
    ProbeStartFn m_probe_start = nullptr;
    ProbeRunFn m_probe_run = nullptr;
    ProbeFrameFn m_probe_frame = nullptr;
};

#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
class BoardRecompModule {
public:
    explicit BoardRecompModule(mamerecomp::BoardFamily family) {
        const char* configured_directory = std::getenv("MAMERECOMP_MODULE_DIR");
        const auto module_directory = configured_directory && *configured_directory
            ? std::filesystem::path(configured_directory) : g_executable_directory;
        const bool is_system16b = family == mamerecomp::BoardFamily::System16B;
        const bool is_system18 = family == mamerecomp::BoardFamily::System18;
        const auto module_name = is_system18 ? "mamerecomp_system18.so"
            : is_system16b ? "mamerecomp_system16b.so" : "mamerecomp_system16a.so";
        const auto path = module_directory / module_name;
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle) throw std::runtime_error("could not load board module " + path.string() + ": " + dlerror());

        const auto abi = reinterpret_cast<unsigned (*)()>(dlsym(m_handle, "mamerecomp_board_abi"));
        const auto module_revision = reinterpret_cast<int (*)()>(dlsym(m_handle, "mamerecomp_board_revision"));
        m_generated_table = reinterpret_cast<GeneratedTableFn>(dlsym(m_handle, "mamerecomp_generated_table"));
        if (!abi || !module_revision || !m_generated_table) {
            const auto error = dlerror();
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("board module has an incomplete ABI: " + path.string()
                + (error ? ": " + std::string(error) : std::string{}));
        }
        const int expected_revision = is_system18 ? 18 : is_system16b ? 17 : 16;
        if (abi() != 1 || module_revision() != expected_revision) {
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("board module ABI/revision mismatch: " + path.string());
        }
    }

    ~BoardRecompModule() { if (m_handle) dlclose(m_handle); }
    BoardRecompModule(const BoardRecompModule&) = delete;
    BoardRecompModule& operator=(const BoardRecompModule&) = delete;

    const ng_func_entry_t* generated_table(const char* profile_id, std::size_t* count) const {
        return m_generated_table(profile_id, count);
    }

private:
    using GeneratedTableFn = const ng_func_entry_t* (*)(const char*, std::size_t*);
    void* m_handle = nullptr;
    GeneratedTableFn m_generated_table = nullptr;
};
#endif

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 3 >= bytes.size()) {
        throw std::runtime_error("reset vectors are truncated");
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint32_t parse_entry_address(std::string_view value) {
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
        base = 16;
    }
    std::uint32_t address = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), address, base);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("invalid 68000 entry address: " + std::string(value));
    }
    return address;
}

std::uint64_t parse_u64(std::string_view value, std::string_view label) {
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
        base = 16;
    }
    std::uint64_t parsed_value = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value, base);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("invalid " + std::string(label) + ": " + std::string(value));
    }
    return parsed_value;
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("could not open binary input: " + path.string());
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("could not determine input size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("failed reading binary input: " + path.string());
    return bytes;
}

struct InputEvent {
    std::uint64_t first_frame;
    std::uint64_t duration_frames;
    std::uint8_t active_low_mask;
    enum class Port { Service, P1, P2 } port;
};

InputEvent parse_input_event(std::string_view value) {
    const auto first = value.find(':');
    const auto second = first == value.npos ? value.npos : value.find(':', first + 1);
    const auto third = second == value.npos ? value.npos : value.find(':', second + 1);
    if (first == value.npos || second == value.npos || third == value.npos) {
        throw std::runtime_error("--input-event format is <frame>:<service|p1|p2>:<active-low-mask>:<duration-frames>");
    }
    const auto frame = parse_u64(value.substr(0, first), "input event frame");
    const auto port_name = value.substr(first + 1, second - first - 1);
    InputEvent::Port port;
    if (port_name == "service") port = InputEvent::Port::Service;
    else if (port_name == "p1") port = InputEvent::Port::P1;
    else if (port_name == "p2") port = InputEvent::Port::P2;
    else throw std::runtime_error("input event port must be service, p1, or p2");
    const auto mask = parse_u64(value.substr(second + 1, third - second - 1), "input event mask");
    const auto duration = parse_u64(value.substr(third + 1), "input event duration");
    if (mask > 0xff || duration == 0 || frame > std::numeric_limits<std::uint64_t>::max() - duration) {
        throw std::runtime_error("input event mask must fit one byte and duration must be positive and not overflow");
    }
    return {frame, duration, static_cast<std::uint8_t>(mask), port};
}

InputEvent parse_button_event(std::string_view value) {
    const auto first = value.find(':');
    const auto second = first == value.npos ? value.npos : value.find(':', first + 1);
    if (first == value.npos || second == value.npos) {
        throw std::runtime_error("--button-event format is <frame>:<button-name>:<duration-frames>");
    }
    const auto frame = parse_u64(value.substr(0, first), "button event frame");
    const auto name = value.substr(first + 1, second - first - 1);
    const auto duration = parse_u64(value.substr(second + 1), "button event duration");
    if (duration == 0 || frame > std::numeric_limits<std::uint64_t>::max() - duration) {
        throw std::runtime_error("button event duration must be positive and not overflow");
    }

    InputEvent::Port port;
    std::uint8_t bit = 0;
    if (name == "coin1") { port = InputEvent::Port::Service; bit = 0; }
    else if (name == "coin2") { port = InputEvent::Port::Service; bit = 1; }
    else if (name == "service_mode") { port = InputEvent::Port::Service; bit = 2; }
    else if (name == "service1") { port = InputEvent::Port::Service; bit = 3; }
    else if (name == "start1") { port = InputEvent::Port::Service; bit = 4; }
    else if (name == "start2") { port = InputEvent::Port::Service; bit = 5; }
    else if (name == "p1_button3") { port = InputEvent::Port::P1; bit = 0; }
    else if (name == "p1_button1") { port = InputEvent::Port::P1; bit = 1; }
    else if (name == "p1_button2") { port = InputEvent::Port::P1; bit = 2; }
    else if (name == "p1_down") { port = InputEvent::Port::P1; bit = 4; }
    else if (name == "p1_up") { port = InputEvent::Port::P1; bit = 5; }
    else if (name == "p1_right") { port = InputEvent::Port::P1; bit = 6; }
    else if (name == "p1_left") { port = InputEvent::Port::P1; bit = 7; }
    else if (name == "p2_button3") { port = InputEvent::Port::P2; bit = 0; }
    else if (name == "p2_button1") { port = InputEvent::Port::P2; bit = 1; }
    else if (name == "p2_button2") { port = InputEvent::Port::P2; bit = 2; }
    else if (name == "p2_down") { port = InputEvent::Port::P2; bit = 4; }
    else if (name == "p2_up") { port = InputEvent::Port::P2; bit = 5; }
    else if (name == "p2_right") { port = InputEvent::Port::P2; bit = 6; }
    else if (name == "p2_left") { port = InputEvent::Port::P2; bit = 7; }
    else {
        throw std::runtime_error("unknown button name; run-full supports coin1/2, service_mode, service1, start1/2, and p1/p2 button1/2/3 plus up/down/left/right");
    }
    return {frame, duration, static_cast<std::uint8_t>(0xffU ^ (1U << bit)), port};
}

void print_game_profiles() {
    for (const auto& profile : mamerecomp::game_profiles()) {
        std::cout << profile.id << "  " << profile.title << "  [" << mamerecomp::board_family_name(profile.board_family) << "]\n";
    }
}

void print_naomi_profiles() {
    const NaomiMetadataModule module;
    std::size_t count = 0;
    const auto* profiles = module.profiles(&count);
    std::cout << "Naomi game profiles (metadata only; SH-4 runtime is not implemented yet):\n";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& profile = profiles[i];
        std::cout << "  " << profile.set_id << "  " << profile.title
                  << "  [Naomi, " << profile.rom_board << ", ROM region 0x"
                  << std::hex << profile.rom_board_region_size << std::dec
                  << ", key=" << profile.security_key
                  << ", SH-4=" << static_cast<unsigned>(profile.sh4_count)
                  << ", PVR2=" << static_cast<unsigned>(profile.pvr2_count)
                  << ", ARM7/AICA=" << (profile.has_arm7_aica ? "yes" : "no")
                  << ", Maple/JVS=" << (profile.has_maple_jvs ? "yes" : "no")
                  << ", ELAN=" << (profile.has_elan ? "yes" : "no")
                  << ", GD-ROM=" << (profile.has_gdrom ? "yes" : "no") << "]\n";
    }
}

const mamerecomp::GameProfile& require_profile(std::string_view id) {
    const auto* profile = mamerecomp::find_game_profile(id);
    if (!profile) {
        throw std::runtime_error("unknown game profile: " + std::string(id) + " (run 'mamerecomp list-games')");
    }
    return *profile;
}

std::vector<std::uint8_t> load_profile_cpu(const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path) {
    auto bytes = mamerecomp::load_maincpu_roms(profile, zip_path);
    std::cout << "Profile: " << profile.id << " (" << profile.title << ")\n"
              << "Board: " << mamerecomp::board_family_name(profile.board_family) << "\n"
              << "Validated and assembled " << profile.maincpu_roms.size() << " 68000 ROMs into 0x"
              << std::hex << profile.maincpu_size << " bytes\n";
    return bytes;
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
    const std::filesystem::path& output_dir, const std::vector<std::uint32_t>& additional_entries, std::size_t output_files) {
    const auto maincpu = load_profile_cpu(profile, zip_path);
    const std::uint32_t initial_pc = read_be32(maincpu, 4);
    if ((initial_pc & 1U) != 0 || initial_pc + 2 > maincpu.size()) {
        throw std::runtime_error("reset PC is not a valid address in the main CPU ROM region");
    }
    std::vector<std::uint32_t> entry_points{initial_pc};
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
        "--base", "0"
    };
    for (const auto entry : entry_points) {
        arguments.emplace_back("--entry");
        arguments.push_back(std::to_string(entry));
    }
    arguments.insert(arguments.end(), {
        "--prefix", prefix, "--out", std::filesystem::absolute(output_dir).string(),
        "--files", std::to_string(std::max<std::size_t>(1, output_files))
    });
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    std::cout << "Full 68000 frontend: " << std::dec << entry_points.size() << " entry point(s), reset PC 0x" << std::hex << initial_pc
              << "; output directory " << std::filesystem::absolute(output_dir) << '\n';
    if (rc_raw_main(static_cast<int>(arguments.size()), argv.data()) != 0) {
        throw std::runtime_error("NeoGeoRecomp raw 68000 frontend failed");
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
    const auto decoded_tiles = profile.board_family != mamerecomp::BoardFamily::System16A
        ? mamerecomp::decode_system16b_tiles(graphics.tiles) : mamerecomp::decode_system16a_tiles(graphics.tiles);
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
    std::unique_ptr<mamerecomp::System16AddressSpace> bus;
    if (profile.board_family == mamerecomp::BoardFamily::System18)
        bus = std::make_unique<mamerecomp::System18AddressSpace>(maincpu, nullptr, nullptr,
            mamerecomp::System18AddressSpace::Devices{}, profile.system18_rom_board);
    else if (profile.board_family == mamerecomp::BoardFamily::System16B)
        bus = std::make_unique<mamerecomp::System16BAddressSpace>(maincpu, nullptr, nullptr, profile.board);
    else
        bus = std::make_unique<mamerecomp::System16AddressSpace>(mamerecomp::make_system16a_address_space(maincpu));
    for (const auto& region : bus->regions()) {
        std::cout << "  " << std::left << std::setw(10) << region.name << std::right << "  0x" << std::hex
                  << std::setw(6) << std::setfill('0') << region.start << "-0x"
                  << std::setw(6) << (region.start + region.size - 1) << "  mirror=0x"
                  << std::setw(6) << region.mirror_mask << std::setfill(' ')
                  << (region.device ? "  device" : (region.writable ? "  RAM" : "  ROM")) << "\n";
    }
}

#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
int run_full_game(const mamerecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    std::uint64_t cycle_budget, const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::uint64_t>& frame_out_vblank,
    const std::optional<std::filesystem::path>& video_state_out,
    const std::optional<std::filesystem::path>& nvram_in, bool mame_nvram_in, bool video_profile_enabled,
    mamerecomp::System16AVideoBackend video_backend, bool compare_cpu_video,
    const std::vector<InputEvent>& input_events, std::uint8_t lives_dip, bool infinite_lives_mode,
    std::optional<std::uint8_t> dsw1_override, std::optional<std::uint8_t> dsw2_override, bool debugger_enabled,
    const std::optional<std::filesystem::path>& cpu_trace_out,
    std::uint64_t cpu_trace_start, std::uint64_t cpu_trace_end, bool window_enabled, bool audio_enabled,
    bool audio_stereo_spread, std::uint8_t audio_volume, bool show_configuration_menu, const std::filesystem::path& settings_path,
    mamerecomp::System16GameSettings menu_settings) {
    BoardRecompModule board_module(profile.board_family);
    const bool system16b = profile.board_family == mamerecomp::BoardFamily::System16B;
    const bool system18 = profile.board_family == mamerecomp::BoardFamily::System18;
    std::unique_ptr<mamerecomp::System16APresenter> presenter;
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (window_enabled) presenter = std::make_unique<mamerecomp::System16APresenter>(std::string(profile.title) + " - MameRecomp");
#else
    if (window_enabled) throw std::runtime_error("--window requires SDL3 development files; reconfigure with SDL3 installed");
#endif
    if (show_configuration_menu) {
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
        if (!presenter->show_configuration_menu(menu_settings, settings_path, std::string(profile.title))) return 0;
        video_backend = menu_settings.video_backend;
        audio_enabled = menu_settings.audio_enabled;
        audio_stereo_spread = menu_settings.audio_stereo_spread;
        audio_volume = menu_settings.audio_volume;
        lives_dip = menu_settings.lives == 2 ? 0x08 : menu_settings.lives == 5 ? 0x04 : 0x0c;
        infinite_lives_mode = false;
        dsw1_override = menu_settings.dsw1();
        dsw2_override = menu_settings.dsw2();
#endif
    }
    const auto maincpu = load_profile_cpu(profile, zip_path);
    if (profile.mcu_region_size != 0) {
        const auto firmware = mamerecomp::load_mcu_roms(profile, zip_path);
        std::cout << "MCU stub: validated " << firmware.size() << " bytes of I8751 firmware; "
                  << "instruction execution and mapper IRQ wiring are not implemented yet\n";
    }
    std::unique_ptr<mamerecomp::System16ASound> sound_a;
    std::unique_ptr<mamerecomp::System16BSound> sound_b;
    std::unique_ptr<mamerecomp::System18Sound> sound_18;
    if (audio_enabled) {
        if (system18) {
            const auto roms = mamerecomp::load_soundcpu_roms(profile, zip_path);
            std::cout << "Validated System 18 sound ROM region: 0x" << std::hex << roms.size() << std::dec << " bytes\n";
            sound_18 = std::make_unique<mamerecomp::System18Sound>(roms, window_enabled, audio_volume,
                audio_stereo_spread);
            std::cout << "Audio path: System 18 Z80 + dual YM3438/YMFM + RF5C68 PCM path enabled (first pass)"
                      << (audio_stereo_spread ? " (alternating-channel stereo spread)\n" : " (game-programmed pan)\n");
        } else if (system16b) {
            const auto roms = mamerecomp::load_soundcpu_roms(profile, zip_path);
            std::cout << "Validated System 16B sound ROM region: 0x" << std::hex << roms.size() << std::dec << " bytes\n";
            sound_b = std::make_unique<mamerecomp::System16BSound>(roms, window_enabled, audio_volume,
                profile.board, audio_stereo_spread);
            std::cout << "Audio path: System 16B Z80 + YM2151/YMFM + uPD7759 slave ADPCM/NMI path enabled"
                      << (audio_stereo_spread ? " (alternating-channel stereo spread)\n" : " (game-programmed pan)\n");
        } else {
            const auto sound_roms = mamerecomp::load_system16a_sound_roms(profile, zip_path);
            std::cout << "Validated sound ROMs: Z80=0x" << std::hex << sound_roms.soundcpu.size()
                      << " uPD7751=0x" << sound_roms.upd7751.size()
                      << " sample-data=0x" << sound_roms.upd7751_data.size() << std::dec << '\n';
            sound_a = std::make_unique<mamerecomp::System16ASound>(sound_roms.soundcpu, sound_roms.upd7751,
                sound_roms.upd7751_data, window_enabled, audio_stereo_spread, audio_volume);
            std::cout << "Audio emulation: Z80 interpreter + YM2151/YMFM + uPD7751/MCS-48/DAC enabled"
                      << (window_enabled ? " with stereo playback" : " without host playback")
                      << (audio_stereo_spread ? " (alternating-channel stereo spread)" : " (game-programmed pan)")
                      << "; sample output is driven by the interpreted MCU firmware\n";
        }
    }
    const auto sound_active = [&]() { return sound_a || sound_b || sound_18; };
    const auto advance_sound = [&](std::uint64_t cycles) {
        if (sound_a) sound_a->advance_to_main_cycles(cycles);
        if (sound_b) sound_b->advance_to_main_cycles(cycles);
        if (sound_18) sound_18->advance_to_main_cycles(cycles);
    };
    const auto graphics_load_start = std::chrono::steady_clock::now();
    const auto graphics = mamerecomp::load_graphics_roms(profile, zip_path);
    const auto graphics_load_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - graphics_load_start).count();
    const auto tile_decode_start = std::chrono::steady_clock::now();
    const auto decoded_tiles = (system16b || system18) ? mamerecomp::decode_system16b_tiles(graphics.tiles)
                                                       : mamerecomp::decode_system16a_tiles(graphics.tiles);
    const auto tile_decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - tile_decode_start).count();
    const std::uint32_t initial_sp = read_be32(maincpu, 0);
    const std::uint32_t initial_pc = read_be32(maincpu, 4);
    if ((initial_pc & 1U) != 0 || initial_pc + 2 > maincpu.size() || (initial_sp & 1U) != 0) {
        throw std::runtime_error("invalid 68000 reset vectors");
    }

    mamerecomp::System16AVideoSignals video_signals;
    mamerecomp::System16AInputState input_state;
    if (system18) input_state.dsw2 = 0xfd; // Shadow Dancer MAME defaults: 3 lives, normal, 3:00 time.
    else if (system16b) input_state.dsw2 = 0x3d; // Golden Axe: 1 credit, demo sound on, normal difficulty.
    else input_state.dsw2 = static_cast<std::uint8_t>((input_state.dsw2 & ~0x0cU) | (lives_dip & 0x0cU));
    if (dsw1_override) input_state.dsw1 = *dsw1_override;
    if (dsw2_override) input_state.dsw2 = *dsw2_override;
    std::unique_ptr<mamerecomp::System16AddressSpace> bus_owner;
    if (system16b)
        bus_owner = std::make_unique<mamerecomp::System16BAddressSpace>(maincpu, &video_signals, &input_state, profile.board);
    else if (system18)
        bus_owner = std::make_unique<mamerecomp::System18AddressSpace>(maincpu, &input_state, &video_signals,
            mamerecomp::System18AddressSpace::Devices{}, profile.system18_rom_board);
    else
        bus_owner = std::make_unique<mamerecomp::System16AddressSpace>(
            mamerecomp::make_system16a_address_space(maincpu, &video_signals, &input_state, sound_a.get()));
    auto& bus = *bus_owner;
    auto* system16b_bus = system16b ? dynamic_cast<mamerecomp::System16BAddressSpace*>(&bus) : nullptr;
    auto* system18_bus = system18 ? dynamic_cast<mamerecomp::System18AddressSpace*>(&bus) : nullptr;
    if (system18_bus) {
        system18_bus->set_vdp_cycle_provider([] {
            return ng_cycles < 0 ? std::uint64_t{0} : static_cast<std::uint64_t>(ng_cycles);
        }, 10000000, 5723, 100); // MAME System 18 screen refresh: 57.23 Hz.
    }
    if (system16b && sound_b) {
        auto* bbus = dynamic_cast<mamerecomp::System16BAddressSpace*>(&bus);
        bbus->mapper().set_sound_callback([&](std::uint8_t value) { sound_b->write_sound_latch(value); });
    }
    if (system18 && sound_18) {
        system18_bus->mapper().set_sound_callback([&](std::uint8_t value) { sound_18->write_sound_latch(value); });
        sound_18->set_sound_reply_callback([&](std::uint8_t value) { system18_bus->set_sound_reply(value); });
    }
    if (nvram_in) {
        auto nvram = read_binary_file(*nvram_in);
        if (mame_nvram_in) {
            if ((nvram.size() & 1U) != 0) throw std::runtime_error("MAME NVRAM image must contain whole 16-bit words");
            for (std::size_t i = 0; i < nvram.size(); i += 2) std::swap(nvram[i], nvram[i + 1]);
        }
        bus.load_ram("nvram", nvram);
        std::cout << "Loaded 0x" << std::hex << nvram.size() << " bytes of NVRAM from "
                  << *nvram_in << (mame_nvram_in ? " (MAME word order converted)" : "") << std::dec << '\n';
    }
    m68k_system16_bind_address_space(&bus);
    const bool debug_hooks_enabled = debugger_enabled || cpu_trace_out.has_value();
    m68k_system16_bus_set_read_observer(debug_hooks_enabled ? mamerecomp_debug_read_adapter : nullptr);
    m68k_system16_bus_set_write_observer(debug_hooks_enabled ? mamerecomp_debug_write_adapter : nullptr);
    ng_cpu = {};
    ng_cpu.a[7] = initial_sp;
    ng_cpu.pc = initial_pc;
    ng_cpu.s = 1;
    ng_cpu.ipl = 7;
    ng_cycles = 0;
    ng_icount = 0;
    constexpr std::uint64_t maincpu_clock_hz = 10000000;
    const std::uint64_t frame_rate_num = system18 ? 5723U : 60U;
    const std::uint64_t frame_rate_den = system18 ? 100U : 1U;
    constexpr std::uint64_t sound_sync_period = maincpu_clock_hz / 1000; // Bound sound scheduling latency to 1 ms.
    std::uint64_t next_vblank = 0;
    std::uint64_t next_sound_sync = sound_active() ? sound_sync_period : std::numeric_limits<std::uint64_t>::max();
    std::uint64_t elapsed_vblanks = 0;
    mamerecomp::configure_m68k_debugger(debugger_enabled, &bus, &elapsed_vblanks,
        cpu_trace_out ? &*cpu_trace_out : nullptr, cpu_trace_start, cpu_trace_end);
    std::uint64_t accepted_vblanks = 0;
    std::uint32_t frame_remainder = 0;
    const auto schedule_next_vblank = [&]() {
        const std::uint64_t numerator = maincpu_clock_hz * frame_rate_den + frame_remainder;
        next_vblank += numerator / frame_rate_num;
        frame_remainder = static_cast<std::uint32_t>(numerator % frame_rate_num);
    };
    schedule_next_vblank();
    std::uint64_t next_vdp_scanline = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t vdp_scanline_remainder = 0;
    std::uint32_t vdp_scanline_line = 0;
    const std::uint64_t vdp_scanline_denominator = frame_rate_num * 262U;
    const auto schedule_next_vdp_scanline = [&]() {
        const std::uint64_t numerator = maincpu_clock_hz * frame_rate_den + vdp_scanline_remainder;
        next_vdp_scanline += numerator / vdp_scanline_denominator;
        vdp_scanline_remainder = numerator % vdp_scanline_denominator;
    };
    if (system18 && system18_bus) {
        system18_bus->vdp().set_scanline(0);
        next_vdp_scanline = 0;
        schedule_next_vdp_scanline();
    }
    const auto advance_vdp_scanlines = [&]() {
        while (system18 && static_cast<std::uint64_t>(ng_cycles) >= next_vdp_scanline) {
            vdp_scanline_line = (vdp_scanline_line + 1U) % 262U;
            system18_bus->vdp().set_scanline(vdp_scanline_line);
            schedule_next_vdp_scanline();
        }
    };
    bool vblank_pending = false;
    bool vblank_frame_written = false;
    bool window_open = true;
    mamerecomp::System16BTilemapState system16b_video_state;
    mamerecomp::System16BTilemapState system18_video_state;
    mamerecomp::System16BTilemapState system16b_compare_state;
    std::vector<std::uint8_t> system16b_compare_sprite_ram;
    const auto render_current_frame = [&](std::vector<mamerecomp::System16AFrame>* stages = nullptr,
        mamerecomp::System16AVideoProfile* profile = nullptr) -> mamerecomp::System16AFrame {
        if (!system16b && !system18) {
            return mamerecomp::render_system16a_frame(decoded_tiles, graphics.sprites,
                bus.memory("tileram"), bus.memory("textram"), bus.memory("sprites"), bus.memory("paletteram"),
                {video_signals.display_enable, video_signals.flip,
                    video_signals.row_scroll_enabled, video_signals.column_scroll_enabled},
                stages, profile, video_backend);
        }
        if (system18) {
            if (!system18_bus) throw std::logic_error("System 18 runner has no System 18 address space");
            system18_video_state.display_enable = system18_bus->display_enabled();
            system18_video_state.flip = video_signals.flip;
            system18_video_state.banks = system18_bus->tile_banks();
            system18_video_state.bank_size = 0x400; // MAME: tilemap_init(..., TILEMAP_16B, ..., 8).
            system18_video_state = mamerecomp::latch_system16b_tilemap_registers(
                bus.memory("textram"), system18_video_state);
            return mamerecomp::render_system16b_frame_cpu(decoded_tiles, graphics.sprites,
                bus.memory("tileram"), bus.memory("textram"), bus.mutable_memory("sprites"),
                system18_video_state, {}, [&](mamerecomp::System16BFrame& frame, unsigned stage) {
                    if (!system18_bus->vdp_enabled() || ((system18_bus->vdp_mixing() >> 1) & 3U) != stage)
                        return;
                    static constexpr std::array<std::uint8_t, 15> sega_level{
                        0, 29, 52, 70, 87, 101, 116, 130, 144, 158, 172, 187, 206, 228, 255};
                    const auto& vdp = system18_bus->vdp();
                    for (unsigned index = 0; index < 64; ++index) {
                        const unsigned raw = vdp.cram_word(index);
                        const unsigned converted = ((raw & 0x00eU) >> 1)
                            | ((raw & 0x0e0U) >> 2) | ((raw & 0xe00U) >> 3);
                        const unsigned r = converted & 7U;
                        const unsigned g = (converted >> 3) & 7U;
                        const unsigned b = (converted >> 6) & 7U;
                        frame.vdp_palette_rgb[index] = (static_cast<std::uint32_t>(sega_level[r << 1]) << 16)
                            | (static_cast<std::uint32_t>(sega_level[g << 1]) << 8)
                            | sega_level[b << 1];
                    }
                    const unsigned priority_mark = (system18_bus->vdp_mixing() & 1U)
                        ? (1U << stage) : 0U;
                    for (unsigned y = 0; y < mamerecomp::System16BFrame::height; ++y) {
                        const auto line = vdp.render_scanline(y);
                        for (unsigned x = 0; x < mamerecomp::System16BFrame::width; ++x) {
                            const auto& source = line[x];
                            if (!source.opaque) continue;
                            const auto pixel = static_cast<std::size_t>(y)
                                * mamerecomp::System16BFrame::width + x;
                            frame.palette_indices[pixel] = static_cast<std::uint16_t>(0x1000U + source.color);
                            frame.tile_priorities[pixel] |= static_cast<std::uint8_t>(priority_mark);
                        }
                    }
                });
        }
        if (!system16b_bus) throw std::logic_error("System 16B runner has no 315-5195 address space");
        system16b_video_state.display_enable = video_signals.display_enable;
        system16b_video_state.flip = video_signals.flip;
        system16b_video_state.banks = system16b_bus->tile_banks();
        system16b_video_state = mamerecomp::latch_system16b_tilemap_registers(
            bus.memory("textram"), system16b_video_state);
        if (compare_cpu_video) {
            system16b_compare_state = system16b_video_state;
            const auto sprite_ram = bus.memory("sprites");
            system16b_compare_sprite_ram.assign(sprite_ram.begin(), sprite_ram.end());
        }
        return mamerecomp::render_system16b_frame(decoded_tiles, graphics.sprites,
            bus.memory("tileram"), bus.memory("textram"), bus.mutable_memory("sprites"),
            system16b_video_state, system16b_bus->sprite_banks(),
            video_backend == mamerecomp::System16AVideoBackend::OpenGL);
    };
    struct LiveVideoStats {
        std::uint64_t frames = 0;
        std::uint64_t render_steady_ns = 0;
        std::uint64_t render_max_steady_ns = 0;
        std::uint64_t gpu_setup_ns = 0;
        std::uint64_t present_wait_ns = 0;
        std::uint64_t present_max_ns = 0;
    } live_video_stats;
    const auto compare_frame_with_cpu = [&](const mamerecomp::System16AFrame& frame, std::string_view context) {
        if (!compare_cpu_video || video_backend != mamerecomp::System16AVideoBackend::OpenGL) return;
        mamerecomp::System16AFrame cpu_frame;
        if (system16b) {
            auto sprite_ram = system16b_compare_sprite_ram;
            cpu_frame = mamerecomp::render_system16b_frame_cpu(decoded_tiles, graphics.sprites,
                bus.memory("tileram"), bus.memory("textram"), sprite_ram,
                system16b_compare_state, system16b_bus->sprite_banks());
        } else {
            cpu_frame = mamerecomp::render_system16a_frame(decoded_tiles, graphics.sprites,
                bus.memory("tileram"), bus.memory("textram"), bus.memory("sprites"), bus.memory("paletteram"),
                {video_signals.display_enable, video_signals.flip,
                    video_signals.row_scroll_enabled, video_signals.column_scroll_enabled});
        }
        std::uint64_t index_differences = 0;
        std::uint64_t priority_differences = 0;
        std::uint64_t shadow_differences = 0;
        for (std::size_t pixel = 0; pixel < frame.palette_indices.size(); ++pixel) {
            index_differences += frame.palette_indices[pixel] != cpu_frame.palette_indices[pixel];
            priority_differences += frame.tile_priorities[pixel] != cpu_frame.tile_priorities[pixel];
            shadow_differences += frame.shadow_pixels[pixel] != cpu_frame.shadow_pixels[pixel];
        }
        std::cout << (system16b ? "System 16B OpenGL vs CPU framebuffer comparison"
                                : "OpenGL vs CPU framebuffer comparison");
        if (!context.empty()) std::cout << " " << context;
        std::cout << ": palette-index=" << index_differences
                  << ", priority=" << priority_differences << ", shadow=" << shadow_differences
                  << " differing pixels\n";
    };
    const auto advance_vblank = [&]() {
        vblank_pending = true;
        ++elapsed_vblanks;
        schedule_next_vblank();
        const bool capture_this_frame = frame_out && frame_out_vblank && !vblank_frame_written
            && elapsed_vblanks == *frame_out_vblank;
        if (presenter || capture_this_frame) {
            if (debugger_enabled && capture_this_frame) mamerecomp::report_m68k_debug_snapshot(bus, elapsed_vblanks, ng_cycles);
            mamerecomp::System16AVideoProfile live_frame_profile;
            const auto frame = render_current_frame(nullptr,
                (presenter && video_profile_enabled && !system16b) ? &live_frame_profile : nullptr);
            if (presenter && video_profile_enabled) {
                const auto steady_render_ns = live_frame_profile.render_ns > live_frame_profile.gpu_setup_ns
                    ? live_frame_profile.render_ns - live_frame_profile.gpu_setup_ns : 0;
                ++live_video_stats.frames;
                live_video_stats.render_steady_ns += steady_render_ns;
                live_video_stats.render_max_steady_ns = std::max(live_video_stats.render_max_steady_ns, steady_render_ns);
                live_video_stats.gpu_setup_ns += live_frame_profile.gpu_setup_ns;
            }
            if (compare_cpu_video && video_backend == mamerecomp::System16AVideoBackend::OpenGL) {
                compare_frame_with_cpu(frame, "at physical vblank " + std::to_string(elapsed_vblanks));
            }
            if (presenter) {
                const auto present_start = std::chrono::steady_clock::now();
                const bool keep_open = presenter->present(frame, bus.memory("paletteram"));
                const auto present_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - present_start).count());
                if (video_profile_enabled) {
                    live_video_stats.present_wait_ns += present_ns;
                    live_video_stats.present_max_ns = std::max(live_video_stats.present_max_ns, present_ns);
                }
                if (!keep_open) window_open = false;
            }
            if (capture_this_frame) {
                mamerecomp::write_system16a_frame_ppm(frame, bus.memory("paletteram"), *frame_out);
                vblank_frame_written = true;
                std::cout << "Wrote video snapshot at physical vblank " << std::dec << elapsed_vblanks
                          << " to " << *frame_out << '\n';
            }
        }
    };

    std::size_t entry_count = 0;
    const std::string generated_profile_id(profile.id);
    const ng_func_entry_t* entries = board_module.generated_table(generated_profile_id.c_str(), &entry_count);
    if (!entries || entry_count == 0) {
        throw std::runtime_error("the loaded board module has no generated 68000 table for profile '"
            + generated_profile_id + "'; include its translation output in that board's module and rebuild");
    }
    std::uint64_t dispatches = 0;
    const std::uint64_t scaled_limit = cycle_budget > std::numeric_limits<std::uint64_t>::max() / 4
        ? std::numeric_limits<std::uint64_t>::max() : cycle_budget * 4;
    const std::uint64_t dispatch_limit = std::max<std::uint64_t>(scaled_limit, 1024);
    const auto cpu_run_start = std::chrono::steady_clock::now();
    while (static_cast<std::uint64_t>(ng_cycles) < cycle_budget && window_open) {
        if (sound_active()) {
            advance_sound(static_cast<std::uint64_t>(ng_cycles));
            while (next_sound_sync <= static_cast<std::uint64_t>(ng_cycles)) next_sound_sync += sound_sync_period;
        }
        input_state.service = 0xff;
        input_state.p1 = 0xff;
        input_state.p2 = 0xff;
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
        if (presenter) {
            const auto live = presenter->live_inputs();
            input_state.service &= live.service;
            input_state.p1 &= live.p1;
            input_state.p2 &= live.p2;
        }
#endif
        for (const auto& event : input_events) {
            if (elapsed_vblanks < event.first_frame ||
                elapsed_vblanks - event.first_frame >= event.duration_frames) continue;
            auto* port = event.port == InputEvent::Port::Service ? &input_state.service
                : event.port == InputEvent::Port::P1 ? &input_state.p1 : &input_state.p2;
            *port &= event.active_low_mask;
        }
        while (static_cast<std::uint64_t>(ng_cycles) >= next_vblank) {
            advance_vblank();
        }
        advance_vdp_scanlines();
        if (vblank_pending && ng_cpu.ipl < 4) {
            const auto irq = m68k_recomp_accept_interrupt(4);
            if (irq.status == M68K_RECOMP_DISPATCH_BUS_FAULT) {
                std::cerr << "68000 IRQ4 vector bus fault at 0x" << std::hex << irq.fault_address << '\n';
                return 1;
            }
            if (irq.status == M68K_RECOMP_DISPATCH_OK) {
                vblank_pending = false;
                ++accepted_vblanks;
                continue;
            }
        }
        if (ng_cpu.stopped) {
            ng_cycles = static_cast<int64_t>(std::min({next_vblank, next_vdp_scanline, cycle_budget}));
            continue;
        }
        ng_next_event = static_cast<int64_t>(std::min({next_vblank, next_sound_sync, next_vdp_scanline, cycle_budget}));
        if (++dispatches > dispatch_limit) {
            throw std::runtime_error("dispatch safety limit reached before the cycle budget");
        }
        const auto result = m68k_recomp_dispatch_once(entries, entry_count);
        if (mamerecomp::m68k_debugger_quit_requested()) break;
        if (result.status != M68K_RECOMP_DISPATCH_OK) {
            std::cerr << "68000 execution stopped: ";
            if (result.status == M68K_RECOMP_DISPATCH_BUS_FAULT) {
                std::cerr << "bus fault access=" << static_cast<unsigned>(result.fault_access)
                          << " address=0x" << std::hex << result.fault_address;
            } else if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY) {
                std::cerr << "no generated entry";
            } else {
                std::cerr << "bad or odd guest PC";
            }
            std::cerr << " pc=0x" << std::hex << result.pc << '\n';
            return 1;
        }
    }
    // A whole guest instruction can take ng_cycles past the requested budget
    // and across a video edge. Account for those elapsed board events even if
    // the cycle-budget condition prevents another CPU dispatch.
    while (static_cast<std::uint64_t>(ng_cycles) >= next_vblank) {
        advance_vblank();
    }
    advance_vdp_scanlines();
    if (sound_active()) advance_sound(static_cast<std::uint64_t>(ng_cycles));
    if (frame_out_vblank && !vblank_frame_written) {
        throw std::runtime_error("cycle budget ended before the requested physical vblank capture");
    }
    const auto cpu_run_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - cpu_run_start).count();

    std::cout << "CPU-only run stopped at " << (ng_cpu.stopped ? "STOP instruction" : "cycle budget")
              << "\ncycles=" << std::dec << ng_cycles << " instructions=" << ng_icount
              << " dispatches=" << dispatches << " vblanks=" << elapsed_vblanks
              << " irq4=" << accepted_vblanks << "\npc=0x" << std::hex << std::setw(8)
              << std::setfill('0') << ng_cpu.pc << " sp=0x" << std::setw(8) << ng_cpu.a[7] << '\n';
    const auto actual_lives_dip = input_state.dsw2 & 0x0cU;
    const char* lives_mode_name = infinite_lives_mode && actual_lives_dip == 0x04U ? "infinite lives (5-lives DIP)"
        : actual_lives_dip == 0x08U ? "2 lives"
        : actual_lives_dip == 0x04U ? "5 lives"
        : actual_lives_dip == 0x0cU ? "3 lives"
        : "Free Play";
    std::cout << "Lives mode: " << lives_mode_name
              << " (DSW1=0x" << std::hex << static_cast<unsigned>(input_state.dsw1)
              << ", DSW2=0x" << static_cast<unsigned>(input_state.dsw2) << std::dec
              << ", reads=" << input_state.dsw1_reads << '/' << input_state.dsw2_reads << ")\n";
    if (sound_a) {
        std::cout << "Sound CPU: Z80 cycles=" << std::dec << sound_a->z80_cycles()
                  << " instructions=" << sound_a->z80_instructions() << " pc=0x" << std::hex << sound_a->z80_pc()
                  << "; YM2151 writes=" << std::dec << sound_a->ym2151_writes()
                  << " key-ons=" << sound_a->ym2151_key_ons()
                  << " key-ons/ch=";
        for (std::size_t channel = 0; channel < sound_a->ym2151_key_ons_by_channel().size(); ++channel) {
            if (channel != 0) std::cout << ',';
            std::cout << sound_a->ym2151_key_ons_by_channel()[channel];
        }
        std::cout << " pan=";
        for (std::size_t channel = 0; channel < sound_a->ym2151_pan_by_channel().size(); ++channel) {
            if (channel != 0) std::cout << ',';
            std::cout << std::hex << static_cast<unsigned>(sound_a->ym2151_pan_by_channel()[channel]);
        }
        std::cout
                  << " last-reg=0x" << std::hex << static_cast<unsigned>(sound_a->ym2151_last_register())
                  << " value=0x" << static_cast<unsigned>(sound_a->ym2151_last_value())
                  << " upd7751 commands=" << sound_a->upd7751_commands()
                  << " last-command=0x" << static_cast<unsigned>(sound_a->upd7751_last_command())
                  << " MCU-insns=" << std::dec << sound_a->upd7751_instructions()
                  << " MCU-PC=0x" << std::hex << sound_a->upd7751_pc()
                  << " DAC-writes=" << std::dec << sound_a->upd7751_p1_writes()
                  << " NMI-edges=" << std::dec << sound_a->nmi_edges()
                  << " latch-reads=" << sound_a->sound_latch_reads()
                  << " latch=0x" << std::hex << static_cast<unsigned>(sound_a->sound_latch())
                  << " active-voices=" << std::dec << sound_a->active_voice_count()
                  << std::dec << " generated samples=" << sound_a->audio_samples()
                  << " peak=" << sound_a->audio_peak()
                  << " LR-difference-peak=" << sound_a->stereo_difference_peak() << '\n';
    } else if (sound_b) {
        std::cout << "System 16B sound: Z80 cycles=" << sound_b->z80_cycles()
                  << " instructions=" << sound_b->z80_instructions() << " pc=0x" << std::hex << sound_b->z80_pc()
                  << "; YM2151 writes=" << std::dec << sound_b->ym2151_writes()
                  << " key-ons=" << sound_b->ym2151_key_ons()
                  << "; uPD7759 FIFO writes=" << sound_b->upd7759_writes()
                  << " last-byte=0x" << std::hex << static_cast<unsigned>(sound_b->upd7759_last_byte())
                  << " NMI-edges=" << std::dec << sound_b->upd7759_nmi_edges()
                  << " serviced=" << sound_b->z80_nmi_serviced()
                  << "; samples=" << std::dec << sound_b->audio_samples() << " peak=" << sound_b->audio_peak() << '\n';
    } else if (sound_18) {
        std::cout << "System 18 sound: Z80 cycles=" << sound_18->z80_cycles()
                  << "; YM3438 writes=" << sound_18->ym_writes(0) << '/' << sound_18->ym_writes(1)
                  << "; RF5C68 RAM writes=" << sound_18->pcm_memory_writes()
                  << " enabled channels=0x" << std::hex << static_cast<unsigned>(sound_18->pcm_enabled_channels())
                  << std::dec << " PCM peak=" << sound_18->pcm_peak()
                  << "; generated samples=" << sound_18->audio_samples()
                  << " peak=" << sound_18->audio_peak() << '\n';
    }
    if (!input_events.empty()) {
        std::cout << std::dec << "Input port reads: SERVICE=" << input_state.service_reads
                  << " (pressed=" << input_state.service_pressed_reads << ") P1=" << input_state.p1_reads
                  << " (pressed=" << input_state.p1_pressed_reads << ") P2=" << input_state.p2_reads
                  << " (pressed=" << input_state.p2_pressed_reads << ")\n";
    }
    // Input counters switch the stream to decimal; the register labels below
    // promise hexadecimal values regardless of whether scripted input is used.
    std::cout << std::hex;
    for (unsigned i = 0; i < 8; ++i) {
        std::cout << "D" << i << "=0x" << std::setw(8) << ng_cpu.d[i] << (i == 7 ? '\n' : ' ');
    }
    for (unsigned i = 0; i < 7; ++i) {
        std::cout << "A" << i << "=0x" << std::setw(8) << ng_cpu.a[i] << (i == 6 ? '\n' : ' ');
    }
    std::cout << std::setfill(' ');
    std::vector<mamerecomp::System16AFrame> video_stages;
    std::uint64_t ppm_write_ns = 0;
    if ((frame_out && !frame_out_vblank) || video_state_out || video_profile_enabled) {
        mamerecomp::System16AVideoProfile render_profile;
        const auto frame = render_current_frame(video_state_out ? &video_stages : nullptr,
            video_profile_enabled && !system16b ? &render_profile : nullptr);
        if (compare_cpu_video && video_backend == mamerecomp::System16AVideoBackend::OpenGL) {
            compare_frame_with_cpu(frame, "at final cycle-budget state");
        }
        if (frame_out && !frame_out_vblank) {
            const auto ppm_start = std::chrono::steady_clock::now();
            mamerecomp::write_system16a_frame_ppm(frame, bus.memory("paletteram"), *frame_out);
            ppm_write_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - ppm_start).count();
            std::cout << "Wrote indexed framebuffer (PPM) to " << *frame_out
                      << "; sprite rendering and palette shadow/highlight are enabled.\n";
            const char* backend_name = "software CPU";
            if (video_backend == mamerecomp::System16AVideoBackend::OpenGL) {
                backend_name = system16b ? "OpenGL 16B tilemaps + CPU sprite rasterizer/OpenGL mixer"
                                        : "OpenGL tilemap/text + CPU sprites";
            }
            std::cout << "Video backend: " << backend_name << '\n';
            std::cout << "Video signals: display=" << video_signals.display_enable
                      << " flip=" << video_signals.flip
                      << " row-scroll=" << video_signals.row_scroll_enabled
                      << " column-scroll=" << video_signals.column_scroll_enabled << '\n';
        }
        if (video_profile_enabled) {
            std::array<bool, 2048> used_palette{};
            std::array<std::uint64_t, 16> priority_histogram{};
            std::uint64_t sprite_output_pixels = 0;
            for (const auto index : frame.palette_indices) {
                if (index < used_palette.size()) used_palette[index] = true;
                if (index >= 0x400) ++sprite_output_pixels;
            }
            for (const auto priority : frame.tile_priorities) ++priority_histogram[priority & 0x0fU];
            const auto used_palette_entries = static_cast<std::uint64_t>(
                std::count(used_palette.begin(), used_palette.end(), true));
            const auto frame_pixels = static_cast<std::uint64_t>(frame.palette_indices.size());
            const auto ms = [](std::uint64_t ns) { return static_cast<double>(ns) / 1'000'000.0; };
            std::cout << std::dec << std::fixed << std::setprecision(3)
                      << "Video profile (wall-clock; independent of IRQ4 and MAME frame counters):\n"
                      << "  graphics ROM load/assembly: " << ms(graphics_load_ns) << " ms ("
                      << graphics.tiles.size() << " tile-ROM bytes, " << graphics.sprites.size() << " sprite-ROM bytes)\n"
                      << "  tile decode: " << ms(tile_decode_ns) << " ms ("
                      << decoded_tiles.size() / 64 << " 8x8 tiles, " << decoded_tiles.size() << " pen indices; "
                      << (tile_decode_ns ? decoded_tiles.size() * 1000.0 / tile_decode_ns : 0.0) << " Mpen/s)\n"
                      << "  CPU simulation, measured separately: " << ms(cpu_run_ns) << " ms for "
                      << ng_cycles << " guest cycles and " << accepted_vblanks << " accepted IRQ4s\n"
                      << "  framebuffer composition: " << ms(render_profile.render_ns) << " ms ("
                      << mamerecomp::System16AFrame::width << 'x' << mamerecomp::System16AFrame::height
                      << ", " << frame_pixels << " pixels)\n";
            if (video_backend == mamerecomp::System16AVideoBackend::OpenGL) {
                std::cout << "    OpenGL setup/upload/dispatch+readback: " << ms(render_profile.gpu_setup_ns) << "/"
                          << ms(render_profile.gpu_upload_ns) << "/" << ms(render_profile.gpu_dispatch_readback_ns) << " ms\n"
                          << "    compute invocations: " << (video_signals.display_enable ? frame_pixels : 0)
                          << "; tile-atlas pen lookups: " << (video_signals.display_enable ? frame_pixels * 3 : 0);
                if (system16b)
                    std::cout << "; CPU sprite rasterization + OpenGL priority/palette/shadow mixer\n";
                else
                    std::cout << "; sprites/mixer on CPU: " << ms(render_profile.sprites_ns) << " ms\n";
            } else {
                std::cout << "    background: " << ms(render_profile.background_ns) << " ms; foreground: "
                          << ms(render_profile.foreground_ns) << " ms; text: " << ms(render_profile.text_ns)
                          << " ms; sprites/mixer: " << ms(render_profile.sprites_ns) << " ms\n"
                          << "    page coordinates visited: " << render_profile.page_pixel_visits
                          << "; page pen lookups: " << render_profile.page_pen_lookups
                          << "; text coordinates/pen lookups: " << render_profile.text_pixel_visits << "/"
                          << render_profile.text_pen_lookups << '\n'
                          << "    palette-index writes BG/FG/text: " << render_profile.background_palette_writes << "/"
                          << render_profile.foreground_palette_writes << "/" << render_profile.text_palette_writes
                          << "; priority marks: " << render_profile.priority_marks << '\n';
            }
            std::cout << "    sprite ROM words/pens: " << render_profile.sprite_words_read << "/"
                      << render_profile.sprite_pen_lookups << "; rasterized pixels: "
                      << render_profile.sprite_pixels_rasterized << "; mixed pixels: "
                      << render_profile.sprite_pixels_mixed << "; shadow pixels: "
                      << render_profile.sprite_shadow_pixels << '\n'
                      << "  final framebuffer: " << frame_pixels << " indexed pixels ("
                      << frame.palette_indices.size() * sizeof(std::uint16_t) << " bytes), "
                      << frame.tile_priorities.size() << " priority bytes, " << frame.shadow_pixels.size()
                      << " shadow bytes; " << used_palette_entries << " palette indices used; "
                      << sprite_output_pixels << " sprite-color pixels\n"
                      << "  RGB PPM conversion/write: ";
            if (frame_out && !frame_out_vblank) {
                const auto ppm_bytes = std::filesystem::file_size(*frame_out);
                std::cout << ms(ppm_write_ns) << " ms (" << ppm_bytes << " bytes, "
                          << frame_pixels * 3 << " RGB payload bytes)\n";
            } else if (frame_out_vblank) {
                std::cout << "captured at vblank " << *frame_out_vblank
                          << "; not included in final-frame timing\n";
            } else {
                std::cout << "not measured; pass --frame-out <image.ppm>\n";
            }
            std::cout << "  priority-mask pixel counts:";
            for (std::size_t i = 0; i < priority_histogram.size(); ++i) {
                if (priority_histogram[i]) std::cout << ' ' << i << '=' << priority_histogram[i];
            }
            if (presenter && live_video_stats.frames != 0) {
                std::cout << std::dec << std::fixed << std::setprecision(3)
                          << "  Live window (" << live_video_stats.frames << " frames): composition mean/max excluding GPU setup "
                          << ms(live_video_stats.render_steady_ns / live_video_stats.frames) << "/"
                          << ms(live_video_stats.render_max_steady_ns) << " ms; presenter call mean/max (includes frame pacing) "
                          << ms(live_video_stats.present_wait_ns / live_video_stats.frames) << "/"
                          << ms(live_video_stats.present_max_ns) << " ms; GPU setup total "
                          << ms(live_video_stats.gpu_setup_ns) << " ms\n";
            }
            std::cout << std::defaultfloat << '\n';
        }
        if (video_state_out) {
            static constexpr const char* stage_names[] = {"background", "foreground", "text", "sprites"};
            for (std::size_t i = 0; i < video_stages.size(); ++i) {
                const auto stage_path = *video_state_out / (std::string("stage_") + stage_names[i] + ".ppm");
                mamerecomp::write_system16a_frame_ppm(video_stages[i], bus.memory("paletteram"), stage_path);
            }
            std::cout << "Wrote " << video_stages.size() << " intermediate composition stages to "
                      << *video_state_out << '\n';
        }
    }
    if (frame_out && !frame_out_vblank) {
        const auto textram = bus.memory("textram");
        const auto read_video_word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        std::cout << "Tilemap regs: page-bg=0x" << std::hex << read_video_word(0xe9c)
                  << " page-fg=0x" << read_video_word(0xe9e)
                  << " x-fg/bg=0x" << read_video_word(0xff8) << "/0x" << read_video_word(0xffa)
                  << " y-fg/bg=0x" << read_video_word(0xf24) << "/0x" << read_video_word(0xf26)
                  << std::dec << '\n';
    }
    if (video_state_out) {
        std::filesystem::create_directories(*video_state_out);
        const auto write_bytes = [](const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
            std::ofstream out(path, std::ios::binary);
            if (!out) throw std::runtime_error("could not create video-state dump: " + path.string());
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) throw std::runtime_error("failed writing video-state dump: " + path.string());
        };
        write_bytes(*video_state_out / "tileram.bin", bus.memory("tileram"));
        write_bytes(*video_state_out / "textram.bin", bus.memory("textram"));
        write_bytes(*video_state_out / "sprites.bin", bus.memory("sprites"));
        write_bytes(*video_state_out / "paletteram.bin", bus.memory("paletteram"));
        write_bytes(*video_state_out / "nvram.bin", bus.memory("nvram"));
        write_bytes(*video_state_out / "tiles_decoded.bin", decoded_tiles);
        write_bytes(*video_state_out / "sprites_decoded.bin", graphics.sprites);
        if (video_stages.size() >= 4) {
            const auto& final_frame = video_stages.back();
            std::vector<std::uint8_t> palette_indices_be;
            palette_indices_be.reserve(final_frame.palette_indices.size() * 2);
            for (const auto index : final_frame.palette_indices) {
                palette_indices_be.push_back(static_cast<std::uint8_t>(index >> 8));
                palette_indices_be.push_back(static_cast<std::uint8_t>(index));
            }
            write_bytes(*video_state_out / "frame_palette_indices_be.bin", palette_indices_be);
            write_bytes(*video_state_out / "frame_priorities.bin", final_frame.tile_priorities);
        }
        std::ofstream info(*video_state_out / "state.txt");
        if (!info) throw std::runtime_error("could not create video-state metadata");
        const auto textram = bus.memory("textram");
        const auto read_video_word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        info << "cycles=" << ng_cycles << '\n'
             << "display_enable=" << video_signals.display_enable << '\n'
             << "flip=" << video_signals.flip << '\n'
             << "row_scroll_enabled=" << video_signals.row_scroll_enabled << '\n'
             << "column_scroll_enabled=" << video_signals.column_scroll_enabled << '\n'
             << "page_bg=0x" << std::hex << read_video_word(0xe9c) << '\n'
             << "page_fg=0x" << read_video_word(0xe9e) << '\n'
             << "x_fg=0x" << read_video_word(0xff8) << '\n'
             << "x_bg=0x" << read_video_word(0xffa) << '\n'
             << "y_fg=0x" << read_video_word(0xf24) << '\n'
             << "y_bg=0x" << read_video_word(0xf26) << '\n';
        if (system18 && system18_bus) {
            const auto& vdp = system18_bus->vdp();
            write_bytes(*video_state_out / "vdp_vram_be.bin", std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(vdp.vram().data()), vdp.vram().size() * sizeof(std::uint16_t)));
            write_bytes(*video_state_out / "vdp_cram_be.bin", std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(vdp.cram().data()), vdp.cram().size() * sizeof(std::uint16_t)));
            write_bytes(*video_state_out / "vdp_vsram_be.bin", std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(vdp.vsram().data()), vdp.vsram().size() * sizeof(std::uint16_t)));
            std::ofstream regs(*video_state_out / "vdp_regs.bin", std::ios::binary);
            regs.write(reinterpret_cast<const char*>(vdp.registers().data()), vdp.registers().size());
            info << "vdp_enabled=" << system18_bus->vdp_enabled() << '\n'
                 << "vdp_mixing=0x" << std::hex << static_cast<unsigned>(system18_bus->vdp_mixing()) << '\n'
                 << "vdp_scanline=" << std::dec << vdp.scanline() << '\n'
                 << "vdp_hpos=" << vdp.horizontal_position() << '\n';
            const auto& vdp_events = system18_bus->vdp_signal_events();
            info << "vdp_signal_event_count=" << vdp_events.size() << '\n';
            for (std::size_t i = 0; i < vdp_events.size(); ++i) {
                const auto& event = vdp_events[i];
                info << "vdp_signal_event_" << i << "="
                     << (event.signal == mamerecomp::System18AddressSpace::VdpSignalEvent::Signal::Enable
                            ? "enable" : "mixing")
                     << ":0x" << std::hex << static_cast<unsigned>(event.value) << '\n';
            }
            for (unsigned i = 0; i < vdp.registers().size(); ++i)
                info << "vdp_reg_" << std::dec << i << "=0x" << std::hex
                     << static_cast<unsigned>(vdp.reg(i)) << '\n';
            for (const auto& mapping : system18_bus->mapper().mappings())
                info << "map_region_" << std::dec << mapping.region << "=target:" << static_cast<unsigned>(mapping.target)
                     << ",start:0x" << std::hex << mapping.start << ",end:0x" << mapping.end
                     << ",mirror:0x" << mapping.mirror << ",mask:0x" << mapping.size_mask << '\n';
        }
        std::cout << "Wrote raw video RAM, graphics, and signal metadata to " << *video_state_out << '\n';
    }
    return 0;
}
#endif

void print_usage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " build --machine=cps --board=cps1 --game=<sf2ce|dino> [--rom-dir <game.zip>] [--jobs <count>] [--rebuild]\n"
              << "  " << program << " run --machine=cps --board=cps1 --game=<sf2ce|dino> [--rom-dir <game.zip>]\n"
              << "  " << program << " list-games\n"
              << "  " << program << " list-naomi-games\n"
              << "  " << program << " inspect-naomi-roms <mvsc2u|mvsc2> <romset.zip>\n"
              << "  " << program << " inspect-neogeo-roms <kof98> <romset.zip>\n"
              << "  " << program << " run-naomi-sh4 <mvsc2u> <romset.zip> [--bios <naomi.zip>] [--instructions <count>] [--interpreter-only] [--window]\n"
              << "  " << program << " map <profile> <romset.zip>\n"
              << "  " << program << " extract-graphics <profile> <romset.zip> [--out <directory>]\n"
              << "  " << program << " inspect <profile> <romset.zip> [--write <maincpu.bin>]\n"
              << "  " << program << " translate-full <profile> <romset.zip> [--out <directory>] [--files <count>] [--entry <address>]...\n"
#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
              << "  " << program << " run-full <profile> <romset.zip> [--cycles <count>] [--lives 2|3|5|infinite] [--coin-mode freeplay|coin] [--dsw1 <byte>] [--dsw2 <byte>] [--button-event <frame>:<button-name>:<duration-frames>] [--input-event <frame>:<service|p1|p2>:<active-low-mask>:<duration-frames>] [--nvram-in <16-KiB-big-endian-file>|--mame-nvram-in <MAME-file>] [--window] [--audio] [--audio-pan game|spread] [--frame-out <image.ppm>] [--frame-out-at-vblank <frame>] [--video-state-out <directory>] [--video-profile] [--video-backend cpu|opengl] [--video-compare-cpu] [--debugger] [--cpu-trace <file> --cpu-trace-cycles <start>:<end>]\n"
#endif
              ;
}

int run_child_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) throw std::invalid_argument("cannot launch an empty command");
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("could not start child process: " + std::string(std::strerror(errno)));
    if (child == 0) {
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::cerr << "could not execute " << arguments.front() << ": " << std::strerror(errno) << '\n';
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("could not wait for child process: " + std::string(std::strerror(errno)));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

bool has_cps1_build_input_newer_than(const std::filesystem::path& root,
    const std::filesystem::file_time_type& stamp_time,
    const std::optional<std::filesystem::path>& rom_zip) {
    const auto newer = [&](const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return false;
        const auto modified = std::filesystem::last_write_time(path, error);
        return !error && modified > stamp_time;
    };
    if (rom_zip && newer(*rom_zip)) return true;

    const std::array<std::filesystem::path, 9> source_roots{
        root / "src/cps1", root / "core/cpus/m68000", root / "core/cpus/z80",
        root / "core/third_party/neogeorecomp-m68krecomp", root / "core/third_party/musashi",
        root / "core/third_party/z80-aot", root / "core/third_party/ymfm",
        root / "tools/generators/cps1", root / "tools/z80"
    };
    const auto is_source = [](const std::filesystem::path& path) {
        const auto extension = path.extension();
        return extension == ".c" || extension == ".cc" || extension == ".cpp"
            || extension == ".h" || extension == ".hpp" || extension == ".py";
    };
    for (const auto& source_root : source_roots) {
        std::error_code error;
        if (!std::filesystem::exists(source_root, error) || error) continue;
        std::filesystem::recursive_directory_iterator it(source_root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        for (; it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (it->is_regular_file(error) && !error && is_source(it->path()) && newer(it->path()))
                return true;
            error.clear();
        }
    }
    return newer(root / "CMakeLists.txt") || newer(root / "tools/recompile.py");
}

int ki_machine_command(const std::filesystem::path& root,
                        const std::string& game,
                        const std::optional<std::filesystem::path>& rom_arg,
                        bool build_mode, const std::string& jobs,
                        bool force_rebuild, bool window, std::uint64_t frames,
                        const std::optional<std::filesystem::path>& frame_out) {
    const auto rom_path = rom_arg ? std::filesystem::absolute(*rom_arg)
                                  : root.parent_path() / "mame/roms";
    const auto work = root / "build/ki/work" / game;
    const auto aot = work / "mips_aot";
    const auto adsp_aot = work / "adsp_aot";
    const auto host_build = game == "ki1" ? root / "build/ki/mamerecomp-host"
                                           : root / "build/ki" / ("mamerecomp-host-" + game);
    const auto runner = host_build / ("mamerecomp_" + game);
    const auto stamp = work / ".mamerecomp-build-stamp";

    bool stale = force_rebuild || !std::filesystem::is_regular_file(runner) ||
                 !std::filesystem::is_regular_file(aot / "rec_tab.c") ||
                 !std::filesystem::is_regular_file(adsp_aot / "rec_dsp.c");
    if (!stale && std::filesystem::is_regular_file(stamp)) {
        std::ifstream input(stamp);
        std::string line;
        bool same_rom = false;
        while (std::getline(input, line))
            if (line == "rom=" + rom_path.string()) same_rom = true;
        stale = !input.eof() || !same_rom;
        std::error_code error;
        const auto stamp_time = std::filesystem::last_write_time(stamp, error);
        if (error) stale = true;
        const std::array inputs{
            root / "CMakeLists.txt", root / "src/ki", root / "core/cpus/mips3",
            root / "core/cpus/adsp2105", root / "core/third_party/libchdr",
            root / "tools/ki", root / "tools/mips3", root / "tools/adsp2105",
            root / "core/third_party/ultracombo-adsp", root / "tools/recompile.py"
        };
        for (const auto& path : inputs) {
            if (error || !std::filesystem::exists(path)) continue;
            if (std::filesystem::is_regular_file(path)) {
                if (std::filesystem::last_write_time(path, error) > stamp_time) stale = true;
            } else {
                for (std::filesystem::recursive_directory_iterator it(path, error), end;
                     !error && it != end; it.increment(error)) {
                    if (it->is_regular_file(error) &&
                        std::filesystem::last_write_time(it->path(), error) > stamp_time) stale = true;
                }
            }
        }
    } else if (!std::filesystem::is_regular_file(stamp)) {
        stale = true;
    }

    if (build_mode) {
        if (!stale) {
            std::cout << game << " build is current; nothing to compile.\n" << std::flush;
            return 0;
        }
        std::cout << (force_rebuild ? "Forcing " + game + " rebuild.\n"
                                   : game + " runtime/AOT is missing or stale; building it.\n") << std::flush;
        std::vector<std::string> translate{
            MAMERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
            "--system", "ki", "--game", game, "--rom-dir", rom_path.string(),
            "--jobs", jobs
        };
        int status = run_child_process(translate);
        if (status != 0) return status;
        std::vector<std::string> configure{
            "cmake", "-S", root.string(), "-B", host_build.string(),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_FLAGS_RELEASE=-O3",
            "-DCMAKE_CXX_FLAGS_RELEASE=-O3",
            "-DMAMERECOMP_KI_MIPS_AOT_DIR=" + aot.string(),
            "-DMAMERECOMP_KI_MIPS_GAME_ID=" + game,
            "-DMAMERECOMP_KI_ADSP_AOT_DIR=" + adsp_aot.string()
        };
        status = run_child_process(configure);
        if (status != 0) return status;
        status = run_child_process({"cmake", "--build", host_build.string(),
                                    "--target", "mamerecomp_ki1", "--parallel", jobs});
        if (status != 0) return status;
        std::filesystem::create_directories(work);
        std::ofstream output(stamp, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not write KI build stamp: " + stamp.string());
        output << "game=" << game << "\nrom=" << rom_path.string() << '\n';
        if (!output) throw std::runtime_error("failed writing KI build stamp: " + stamp.string());
        return 0;
    }

    if (stale)
        throw std::runtime_error(game + " build is missing or stale; run `mamerecomp build --machine=ki --board=seattle --game=" + game + "` first");
    std::vector<std::string> command{runner.string(), "--rom-dir", rom_path.string(),
                                    "--work-dir", work.string(), "--game", game};
    if (window) command.emplace_back("--window");
    else command.insert(command.end(), {"--frames", std::to_string(frames ? frames : 1)});
    if (frame_out) command.insert(command.end(), {"--frame-out", frame_out->string()});
    std::cout << "Launching " << game << " on Seattle (MIPS III/R4600).\n" << std::flush;
    return run_child_process(command);
}

int machine_game_command(int argc, char** argv, bool build_mode) {
    std::string machine;
    std::string board;
    std::string game;
    std::optional<std::filesystem::path> rom_zip;
    std::string jobs = "2";
    bool force_rebuild = false;
    bool window = false;
    bool headless = false;
    std::uint64_t frames = 0;
    std::optional<std::filesystem::path> frame_out;
    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        auto value_after = [&](std::string_view name) -> std::optional<std::string> {
            const std::string prefix = std::string(name) + "=";
            if (option.starts_with(prefix)) return std::string(option.substr(prefix.size()));
            if (option == name && i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        if (const auto value = value_after("--machine")) machine = *value;
        else if (const auto value = value_after("--board")) board = *value;
        else if (const auto value = value_after("--game")) game = *value;
        else if (const auto value = value_after("--rom-dir")) rom_zip = *value;
        else if (const auto value = value_after("--frames")) frames = parse_u64(*value, "frame count");
        else if (const auto value = value_after("--frame-out")) frame_out = *value;
        else if (option == "--window") { window = true; headless = false; }
        else if (option == "--headless") { window = false; headless = true; }
        else if (option == "--jobs" && i + 1 < argc) {
            if (!build_mode) throw std::runtime_error("--jobs is only valid for the build command");
            jobs = argv[++i];
        } else if (option.starts_with("--jobs=")) {
            if (!build_mode) throw std::runtime_error("--jobs is only valid for the build command");
            jobs = std::string(option.substr(7));
        } else if (build_mode && option == "--rebuild") {
            force_rebuild = true;
        } else {
            throw std::runtime_error("unknown " + std::string(build_mode ? "build" : "run")
            + " option: " + std::string(option));
        }
    }
    if (machine == "ki" && board == "seattle") {
        if (game != "ki1" && game != "ki2")
            throw std::runtime_error("Seattle currently supports --game=ki1 or --game=ki2");
        if (build_mode && (window || frames || frame_out))
            throw std::runtime_error("--window, --frames, and --frame-out are run-only options");
        return ki_machine_command(std::filesystem::path(MAMERECOMP_SOURCE_DIR), game,
                                   rom_zip, build_mode, jobs, force_rebuild,
                                   build_mode ? false : (window || (!headless && frames == 0)),
                                   frames, frame_out);
    }
    if (machine != "cps" || board != "cps1")
        throw std::runtime_error("CPS1 adapter requires --machine=cps --board=cps1");
    if (game != "sf2ce" && game != "dino")
        throw std::runtime_error("CPS1 adapter currently supports only --game=sf2ce or --game=dino");

#ifndef MAMERECOMP_SOURCE_DIR
#define MAMERECOMP_SOURCE_DIR "."
#endif
#ifndef MAMERECOMP_PYTHON_EXECUTABLE
#define MAMERECOMP_PYTHON_EXECUTABLE "python3"
#endif
    const std::filesystem::path root(MAMERECOMP_SOURCE_DIR);
    const auto work_dir = root / "build/cps1/work" / game;
    const auto build_dir = root / "build/cps1/mamerecomp-host";
    const auto generated_roms = work_dir / "roms";
    const auto runner = build_dir / ("mamerecomp_" + game);
    const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
        : root.parent_path() / "mame/roms" / (game + ".zip");
    if (rom_zip && !std::filesystem::is_regular_file(rom_path))
        throw std::runtime_error("ROM ZIP not found: " + rom_path.string());

    const auto stamp = work_dir / ".mamerecomp-build-stamp";
    bool needs_build = force_rebuild;
    const std::array<std::filesystem::path, 11> required_artifacts{
        runner, build_dir / "libmamerecomp_cps1.so", build_dir / ("libmamerecomp_game_" + game + ".so"),
        work_dir / "program.bin", work_dir / "m68k_dynamic_entries.txt",
        work_dir / "z80_program.bin", work_dir / "z80_banked.bin",
        generated_roms / "maincpu.bin", generated_roms / "gfx.bin",
        generated_roms / "audiocpu.bin", generated_roms / (game == "dino" ? "qsound.bin" : "oki.bin")
    };
    for (const auto& artifact : required_artifacts) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(artifact, error) || error) needs_build = true;
    }
    std::error_code stamp_error;
    if (!std::filesystem::is_regular_file(stamp, stamp_error) || stamp_error) {
        needs_build = true;
    } else if (!needs_build) {
        std::ifstream stamp_input(stamp);
        std::string line;
        bool matching_rom_path = false;
        while (std::getline(stamp_input, line)) {
            if (line == "rom=" + rom_path.string()) matching_rom_path = true;
        }
        if (!stamp_input.eof() || !matching_rom_path) needs_build = true;
        const auto stamp_time = std::filesystem::last_write_time(stamp, stamp_error);
        if (stamp_error || has_cps1_build_input_newer_than(root, stamp_time, rom_path))
            needs_build = true;
    }

    if (build_mode) {
        if (!needs_build) {
            std::cout << "CPS-1 build is current; nothing to compile.\n" << std::flush;
            return 0;
        }
        std::vector<std::string> build_command{
            MAMERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
            "--system", "cps1", "--game", game, "--jobs", jobs
        };
        if (rom_zip) {
            build_command.emplace_back("--rom-dir");
            build_command.push_back(rom_path.string());
        }
        std::cout << (force_rebuild ? "Forcing CPS-1 rebuild.\n"
                                    : "CPS-1 build missing or stale; recompiling from the ROM driver.\n") << std::flush;
        const int build_status = run_child_process(build_command);
        if (build_status != 0) return build_status;
        std::filesystem::create_directories(work_dir);
        std::ofstream stamp_file(stamp, std::ios::binary | std::ios::trunc);
        if (!stamp_file) throw std::runtime_error("could not write CPS-1 build stamp: " + stamp.string());
        stamp_file << "game=" << game << '\n' << "rom=" << rom_path.string() << '\n';
        if (!stamp_file) throw std::runtime_error("failed writing CPS-1 build stamp: " + stamp.string());
        return 0;
    }

    if (needs_build) {
        std::string message = "CPS-1 build is missing or stale; run `mamerecomp build --machine=cps --board=cps1 --game="
            + game + "` first";
        if (rom_zip) message += " with the same `--rom-dir " + rom_path.string() + "`";
        throw std::runtime_error(message);
    }

    std::vector<std::string> run_command{
        runner.string(), (work_dir / "program.bin").string(), "9223372036854775807",
        (work_dir / "m68k_dynamic_entries.txt").string(),
        (work_dir / "z80_program.bin").string(), (work_dir / "z80_banked.bin").string(),
        (work_dir / "z80_misses.txt").string(), (work_dir / "z80_fallbacks.log").string(),
        (generated_roms / "gfx.bin").string(), (work_dir / "live-frame.ppm").string(),
        "--window", "--oki", (generated_roms / "oki.bin").string(),
        "--m68k-ram-entries", (work_dir / "m68k_ram_entries.txt").string(),
        "--gfxram-snapshot", (work_dir / "m68k_gfxram_snapshot.bin").string()
    };
    if (game == "dino") {
        run_command.emplace_back("--qsound-program");
        run_command.push_back((generated_roms / "audiocpu.bin").string());
        run_command.emplace_back("--qsound-samples");
        run_command.push_back((generated_roms / "qsound.bin").string());
    }
    std::cout << "Launching " << game << " on " << machine << '/' << board << ".\n" << std::flush;
    return run_child_process(run_command);
}

} // namespace

int main(int argc, char** argv) {
    try {
#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
        const char* configured_module_directory = std::getenv("MAMERECOMP_MODULE_DIR");
        g_executable_directory = configured_module_directory && *configured_module_directory
            ? std::filesystem::path(configured_module_directory)
            : std::filesystem::absolute(argv[0]).parent_path();
#else
        const char* configured_module_directory = std::getenv("MAMERECOMP_MODULE_DIR");
        g_executable_directory = configured_module_directory && *configured_module_directory
            ? std::filesystem::path(configured_module_directory)
            : std::filesystem::absolute(argv[0]).parent_path();
#endif
        if (argc == 2 && std::string_view(argv[1]) == "list-games") {
            print_game_profiles();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "list-naomi-games") {
            print_naomi_profiles();
            return 0;
        }
        if (argc >= 2 && std::string_view(argv[1]) == "build")
            return machine_game_command(argc, argv, true);
        if (argc >= 2 && std::string_view(argv[1]) == "run")
            return machine_game_command(argc, argv, false);
        if (argc >= 2 && std::string_view(argv[1]) == "inspect-naomi-roms") {
            if (argc != 4) {
                print_usage(argv[0]);
                return 2;
            }
            const auto image = mamerecomp::naomi::load_mvc2_rom_board(argv[2], argv[3]);
            std::cout << "Validated Naomi cartridge set " << image.set_id
                      << ": " << image.rom_files_loaded << " ROM loads assembled into 0x"
                      << std::hex << image.bytes.size() << std::dec
                      << " bytes; BIOS not loaded (BIOS-free path).\n";
            return 0;
        }
        if (argc >= 2 && std::string_view(argv[1]) == "inspect-neogeo-roms") {
            if (argc != 4 || std::string_view(argv[2]) != "kof98") {
                print_usage(argv[0]);
                return 2;
            }
            const auto bios_path = std::filesystem::path(argv[3]).parent_path() / "neogeo.zip";
            const auto image = mamerecomp::neogeo::load_kof98(argv[3], bios_path);
            std::cout << "Validated Neo Geo set " << image.set_id << ": " << image.files_loaded
                      << " ROM files; BIOS=" << image.bios_filename
                      << "; decrypted 68000 program assembled to 0x" << std::hex
                      << image.maincpu.size() << std::dec << " bytes. Regions:\n";
            for (const auto& region : image.regions)
                std::cout << "  " << region.name << ": " << region.loads << " files, 0x"
                          << std::hex << region.bytes_loaded << "/0x" << region.size << std::dec
                          << " bytes loaded\n";
            std::cout << "Prototype path: MAME KOF98 program transform and Neo Geo board runtime profile selected; static AOT coverage is generated by kof98_aot.\n";
            return 0;
        }
        if (argc >= 2 && std::string_view(argv[1]) == "run-naomi-sh4") {
            if (argc < 4) {
                print_usage(argv[0]);
                return 2;
            }
            std::uint64_t budget = 1000000;
            bool show_window = false;
            bool interpreter_only = false;
            std::filesystem::path bios_path =
                std::filesystem::path(argv[3]).parent_path() / "naomi.zip";
            for (int i = 4; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--instructions" && i + 1 < argc)
                    budget = parse_u64(argv[++i], "SH-4 instruction budget");
                else if (std::string_view(argv[i]) == "--bios" && i + 1 < argc)
                    bios_path = argv[++i];
                else if (std::string_view(argv[i]) == "--window")
                    show_window = true;
                else if (std::string_view(argv[i]) == "--interpreter-only")
                    interpreter_only = true;
                else
                    throw std::runtime_error("unknown run-naomi-sh4 option: " + std::string(argv[i]));
            }
            if (budget == 0) throw std::runtime_error("SH-4 instruction budget must be nonzero");
#ifndef MAMERECOMP_HAS_SDL3_PRESENTER
            if (show_window)
                throw std::runtime_error("--window requires SDL3 development files; reconfigure with SDL3 installed");
#endif

            const auto image = mamerecomp::naomi::load_mvc2_rom_board(argv[2], argv[3]);
            const auto bios = mamerecomp::naomi::load_naomi_bios(argv[2], bios_path);
            NaomiMetadataModule module;
            void* raw_probe = module.create_probe(argv[2], interpreter_only);
            if (!raw_probe)
                throw std::runtime_error("Naomi module has no SH-4 probe for set " + std::string(argv[2]));
            const auto destroy = [&module](void* probe) { module.destroy_probe(probe); };
            std::unique_ptr<void, decltype(destroy)> probe(raw_probe, destroy);

            NaomiSh4ProbeResult result{};
            if (!module.start_probe(probe.get(), image.bytes, image.board_id_eeprom,
                                    bios.bytes, &result))
                throw std::runtime_error(result.error[0] ? result.error : "Naomi SH-4 boot setup failed");
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
            std::unique_ptr<mamerecomp::naomi::NaomiPvrPresenter> presenter;
            if (show_window) {
                presenter = std::make_unique<mamerecomp::naomi::NaomiPvrPresenter>(
                    "MameRecomp - Naomi BIOS executing (PVR scanout pending)");
                NaomiPvrFrameInfo boot_surface{};
                boot_surface.width = 640;
                boot_surface.height = 480;
                boot_surface.rgb_bytes = std::size_t(boot_surface.width) *
                                         boot_surface.height * 3u;
                std::vector<std::uint8_t> black(boot_surface.rgb_bytes, 0);
                presenter->show(boot_surface, black, false);
                std::cout << "Naomi window opened before SH-4 execution; waiting for PVR scanout\n"
                          << std::flush;
            }
#endif
            if (!module.run_probe(probe.get(), budget, &result))
                throw std::runtime_error(result.error[0] ? result.error : "Naomi SH-4 execution failed");

            std::cout << "SH-4 "
                      << (interpreter_only ? "interpreter-only " : "AOT+interpreter ")
                      << "Naomi-BIOS boot "
                      << image.set_id << " BIOS="
                      << bios.filename << " CRC=" << std::hex << bios.crc
                      << ": entry=0x"
                      << std::hex << result.entry << " instructions=" << std::dec
                      << result.instructions_executed << " native/recompiled="
                      << result.native_instructions << " interpreted="
                      << result.interpreted_instructions << " budget=" << budget
                      << " pc=0x" << std::hex << result.pc;
            std::cout << " PVR2={TA:" << std::dec << result.pvr_ta_bytes
                      << "B,YUV:" << result.pvr_yuv_bytes
                      << "B,VRAM:" << result.pvr_vram_bytes
                      << "B,regs:" << result.pvr_register_writes
                      << ",render-starts:" << result.pvr_render_start_requests
                      << ",fb-enabled:" << (result.pvr_framebuffer_enabled ? "yes" : "no")
                      << ",lists:" << std::dec << result.pvr_ta_completed_lists
                      << ",line:" << result.pvr_scanline << '}';
            std::cout << " TA-PCW={";
            for (std::size_t i = 0; i < 8; ++i) {
                if (i) std::cout << ',';
                std::cout << i << ':' << std::dec << result.pvr_ta_parameter_counts[i];
            }
            std::cout << "} TA-lists={";
            for (std::size_t i = 0; i < 5; ++i) {
                if (i) std::cout << ',';
                std::cout << i << ':' << std::dec << result.pvr_ta_list_packet_counts[i];
            }
            std::cout << '}';
            std::cout << " Holly={ISTNRM:0x" << std::hex << result.holly_normal_status
                      << ",IML2:0x" << result.holly_level2_mask
                      << ",IML4:0x" << result.holly_level4_mask
                      << ",IML6:0x" << result.holly_level6_mask << std::dec
                      << ",accepted:" << result.interrupts_accepted << '}';
            std::cout << " indirect={count:" << std::dec << result.indirect_calls
                      << ",last:0x" << std::hex << result.last_indirect_target << ",recent:[";
            for (std::size_t i = 0; i < result.indirect_calls && i < 16; ++i) {
                if (i != 0) std::cout << ',';
                std::cout << "0x" << result.recent_indirect_targets[i];
            }
            std::cout << "]}";
            if (result.stopped_by_budget || result.execution_stopped)
                std::cout << " regs={r1:0x" << std::hex << result.r1
                          << ",r2:0x" << result.r2 << ",r3:0x" << result.r3
                          << ",r8:0x" << result.r8 << ",r9:0x" << result.r9
                          << ",r10:0x" << result.r10 << ",r11:0x" << result.r11
                          << ",r12:0x" << result.r12 << ",r14:0x" << result.r14
                          << ",r13:0x" << result.r13 << ",r15:0x" << result.r15 << '}';
            if (result.unresolved_target ||
                (result.execution_stopped && !result.stopped_by_budget))
                std::cout << " unresolved=0x" << result.unresolved_target;
            if (result.exception_code)
                std::cout << " exception=0x" << result.exception_code;
            if (result.stopped_by_budget)
                std::cout << " stopped-by-budget";
            if (result.execution_stopped && !result.stopped_by_budget &&
                !result.unresolved_target && !result.exception_code)
                std::cout << " stopped";
            std::cout << std::dec << '\n';
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
            if (show_window) {
                NaomiPvrFrameInfo frame_info{};
                std::vector<std::uint8_t> rgb;
                if (!module.capture_frame(probe.get(), &frame_info, &rgb))
                    throw std::runtime_error("could not capture the PVR direct framebuffer");
                const bool has_scanout = frame_info.width != 0 && frame_info.height != 0 &&
                    rgb.size() == static_cast<std::size_t>(frame_info.width) *
                                  frame_info.height * 3u;
                if (!has_scanout) {
                    // A cold Naomi BIOS may not have enabled FB_R yet. Keep
                    // --window useful in that state: open a clearly identified
                    // clearly identified window instead of exiting before SDL
                    // creates a window. This surface is not a PVR readback.
                    frame_info.width = 640;
                    frame_info.height = 480;
                    frame_info.rgb_bytes = static_cast<std::size_t>(frame_info.width) *
                                           frame_info.height * 3u;
                    rgb.assign(frame_info.rgb_bytes, 0);
                    std::cout << "No active PVR scanout; showing diagnostic black only "
                                 "(no framebuffer readback or guest pixels)\n";
                }
                if (has_scanout) {
                    std::cout << "PVR framebuffer readback: " << frame_info.width << 'x'
                              << frame_info.height << " RGB bytes=" << frame_info.rgb_bytes
                              << " FB_R_CTRL=0x" << std::hex << frame_info.control
                              << " FB_R_SIZE=0x" << frame_info.size
                              << " FB_R_SOF1=0x" << frame_info.start << std::dec
                              << " enabled=" << ((frame_info.control & 1u) ? "yes" : "no")
                              << "\n";
                } else {
                    std::cout << "PVR scanout state: FB_R_CTRL=0x" << std::hex << frame_info.control
                              << " FB_R_SIZE=0x" << frame_info.size
                              << " FB_R_SOF1=0x" << frame_info.start << std::dec
                              << " enabled=" << ((frame_info.control & 1u) ? "yes" : "no")
                              << "\n";
                }
                if (!presenter)
                    presenter = std::make_unique<mamerecomp::naomi::NaomiPvrPresenter>(
                        has_scanout ? "MameRecomp - Naomi PVR2 framebuffer"
                                    : "MameRecomp - Naomi PVR2 (no active scanout)");
                presenter->show(frame_info, rgb, has_scanout);
                presenter->waitUntilClosed();
            }
#endif
            return 0;
        }
        if (argc < 4) {
            print_usage(argv[0]);
            return 2;
        }

        const std::string_view command(argv[1]);
        const auto& profile = require_profile(argv[2]);
        const std::filesystem::path zip_path(argv[3]);

        if (command == "extract-graphics") {
            std::filesystem::path output_dir = std::filesystem::path("build") / "graphics" / std::string(profile.id);
            if (argc == 6 && std::string_view(argv[4]) == "--out") {
                output_dir = argv[5];
            } else if (argc != 4) {
                print_usage(argv[0]);
                return 2;
            }
            extract_graphics(profile, zip_path, output_dir);
            return 0;
        }

        if (command == "map") {
            const auto maincpu = load_profile_cpu(profile, zip_path);
            std::cout << mamerecomp::board_family_name(profile.board_family)
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
                print_usage(argv[0]);
                return 2;
            }
            inspect_game(profile, zip_path, output_path_ptr);
            return 0;
        }

#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
        if (command == "run-full") {
            std::uint64_t cycles = 1000000;
            bool cycles_explicit = false;
            bool window_enabled = false;
            bool audio_enabled = false;
            bool audio_stereo_spread = false;
            std::optional<std::filesystem::path> frame_out;
            std::optional<std::uint64_t> frame_out_vblank;
            std::optional<std::filesystem::path> video_state_out;
            std::optional<std::filesystem::path> nvram_in;
            bool mame_nvram_in = false;
            bool video_profile_enabled = false;
            bool compare_cpu_video = false;
            bool debugger_enabled = false;
            std::optional<std::filesystem::path> cpu_trace_out;
            std::optional<std::uint64_t> cpu_trace_start;
            std::optional<std::uint64_t> cpu_trace_end;
            std::vector<InputEvent> input_events;
            std::uint8_t lives_dip = 0x0c; // Shinobi DSW2:3-4 three-lives selection.
            bool infinite_lives_mode = false;
            bool coin_mode_freeplay = false;
            bool lives_option_set = false;
            bool coin_mode_option_set = false;
            bool audio_option_set = false;
            bool audio_pan_option_set = false;
            bool video_backend_option_set = false;
            bool raw_dsw1_option_set = false;
            bool raw_dsw2_option_set = false;
            std::optional<std::uint8_t> dsw1_override;
            std::optional<std::uint8_t> dsw2_override;
            auto video_backend = mamerecomp::System16AVideoBackend::Software;
            for (int i = 4; i < argc; ++i) {
                const std::string_view option(argv[i]);
                if (option == "--window") {
                    window_enabled = true;
                    if (!cycles_explicit) cycles = static_cast<std::uint64_t>(INT64_MAX);
                } else if (option == "--audio") {
                    audio_enabled = true;
                    audio_option_set = true;
                } else if (option == "--audio-pan" && i + 1 < argc) {
                    const std::string_view value(argv[++i]);
                    if (value == "game") audio_stereo_spread = false;
                    else if (value == "spread") audio_stereo_spread = true;
                    else throw std::runtime_error("--audio-pan must be game or spread");
                    audio_pan_option_set = true;
                } else if (option == "--cycles" && i + 1 < argc) {
                    cycles_explicit = true;
                    const std::string_view value(argv[++i]);
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), cycles);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || cycles == 0 ||
                        cycles > static_cast<std::uint64_t>(INT64_MAX)) {
                        throw std::runtime_error("--cycles must be a positive integer within the signed cycle range");
                    }
                } else if (option == "--frame-out" && i + 1 < argc) {
                    frame_out = argv[++i];
                } else if (option == "--frame-out-at-vblank" && i + 1 < argc) {
                    const std::string_view value(argv[++i]);
                    std::uint64_t parsed_frame = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_frame);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || parsed_frame == 0) {
                        throw std::runtime_error("--frame-out-at-vblank must be a positive physical vblank count");
                    }
                    frame_out_vblank = parsed_frame;
                } else if (option == "--video-state-out" && i + 1 < argc) {
                    video_state_out = argv[++i];
                } else if (option == "--nvram-in" && i + 1 < argc) {
                    nvram_in = argv[++i];
                } else if (option == "--mame-nvram-in" && i + 1 < argc) {
                    nvram_in = argv[++i];
                    mame_nvram_in = true;
                } else if (option == "--video-profile") {
                    video_profile_enabled = true;
                } else if (option == "--video-backend" && i + 1 < argc) {
                    video_backend_option_set = true;
                    const std::string_view value(argv[++i]);
                    if (value == "cpu") {
                        video_backend = mamerecomp::System16AVideoBackend::Software;
                    } else if (value == "opengl") {
                        video_backend = mamerecomp::System16AVideoBackend::OpenGL;
                    } else {
                        throw std::runtime_error("--video-backend must be cpu or opengl");
                    }
                } else if (option == "--video-compare-cpu") {
                    compare_cpu_video = true;
                } else if (option == "--debugger") {
                    debugger_enabled = true;
                } else if (option == "--cpu-trace" && i + 1 < argc) {
                    cpu_trace_out = argv[++i];
                } else if (option == "--cpu-trace-cycles" && i + 1 < argc) {
                    const std::string_view range(argv[++i]);
                    const auto colon = range.find(':');
                    if (colon == range.npos) throw std::runtime_error("--cpu-trace-cycles format is <start>:<end>");
                    cpu_trace_start = parse_u64(range.substr(0, colon), "CPU trace start cycle");
                    cpu_trace_end = parse_u64(range.substr(colon + 1), "CPU trace end cycle");
                    if (*cpu_trace_start > *cpu_trace_end) throw std::runtime_error("CPU trace start must not exceed end");
                } else if (option == "--input-event" && i + 1 < argc) {
                    input_events.push_back(parse_input_event(argv[++i]));
                } else if (option == "--button-event" && i + 1 < argc) {
                    input_events.push_back(parse_button_event(argv[++i]));
                } else if (option == "--coin-mode" && i + 1 < argc) {
                    coin_mode_option_set = true;
                    const std::string_view value(argv[++i]);
                    if (value == "freeplay") { coin_mode_freeplay = true; }
                    else if (value == "coin") {
                        coin_mode_freeplay = false;
                        if (!lives_option_set) lives_dip = 0x0c;
                        infinite_lives_mode = false;
                    }
                    else throw std::runtime_error("--coin-mode must be freeplay or coin");
                } else if ((option == "--dsw1" || option == "--dsw2") && i + 1 < argc) {
                    const auto value = parse_u64(argv[++i], option);
                    if (value > 0xffU) throw std::runtime_error(std::string(option) + " must be an 8-bit value");
                    if (option == "--dsw1") { dsw1_override = static_cast<std::uint8_t>(value); raw_dsw1_option_set = true; }
                    else { dsw2_override = static_cast<std::uint8_t>(value); raw_dsw2_option_set = true; }
                } else if (option == "--lives" && i + 1 < argc) {
                    lives_option_set = true;
                    const std::string_view value(argv[++i]);
                    if (value == "2") { lives_dip = 0x08; infinite_lives_mode = false; }
                    else if (value == "3") { lives_dip = 0x0c; infinite_lives_mode = false; }
                    else if (value == "5") { lives_dip = 0x04; infinite_lives_mode = false; }
                    // The System 16A DIP has no infinite-lives selection. Shinobi's
                    // 5-lives selection is empirically unlimited in this profile;
                    // expose that observed behavior under an explicit name.
                    else if (value == "infinite") { lives_dip = 0x04; infinite_lives_mode = true; }
                    else throw std::runtime_error("--lives must be 2, 3, 5, or infinite");
                } else {
                    print_usage(argv[0]);
                    return 2;
                }
            }
            if (frame_out_vblank && !frame_out) {
                throw std::runtime_error("--frame-out-at-vblank requires --frame-out <image.ppm>");
            }
            // Shinobi's Free Play is selected by both Coin A/B banks on SW1;
            // SW2:3-4 remains an independent lives setting.
            if (coin_mode_freeplay && !dsw1_override) dsw1_override = 0x00;
            if (cpu_trace_out.has_value() != cpu_trace_start.has_value()
                || cpu_trace_out.has_value() != cpu_trace_end.has_value()) {
                throw std::runtime_error("--cpu-trace and --cpu-trace-cycles must be specified together");
            }
            if (audio_stereo_spread && !audio_enabled) {
                throw std::runtime_error("--audio-pan spread requires --audio");
            }
            const bool show_configuration_menu = window_enabled
                && profile.board_family == mamerecomp::BoardFamily::System16A && !cycles_explicit
                && !raw_dsw1_option_set && !raw_dsw2_option_set;
            mamerecomp::System16GameSettings menu_settings;
            menu_settings.video_backend = video_backend;
            menu_settings.audio_enabled = profile.board_family == mamerecomp::BoardFamily::System16A;
            menu_settings.audio_stereo_spread = audio_stereo_spread;
            menu_settings.free_play = coin_mode_freeplay;
            menu_settings.lives = lives_dip == 0x08 ? 2 : lives_dip == 0x04 ? 5 : 3;
            const auto settings_path = mamerecomp::system16_settings_path(profile.id.data());
            if (show_configuration_menu) {
                menu_settings = mamerecomp::load_system16_settings(settings_path, menu_settings);
                if (video_backend_option_set) menu_settings.video_backend = video_backend;
                if (audio_option_set) menu_settings.audio_enabled = true;
                if (audio_pan_option_set) menu_settings.audio_stereo_spread = audio_stereo_spread;
                if (coin_mode_option_set) menu_settings.free_play = coin_mode_freeplay;
                if (lives_option_set) menu_settings.lives = lives_dip == 0x08 ? 2 : lives_dip == 0x04 ? 5 : 3;
            }
            return run_full_game(profile, zip_path, cycles, frame_out, frame_out_vblank, video_state_out, nvram_in, mame_nvram_in,
                video_profile_enabled, video_backend, compare_cpu_video, input_events, lives_dip, infinite_lives_mode,
                dsw1_override, dsw2_override, debugger_enabled,
                cpu_trace_out, cpu_trace_start.value_or(0), cpu_trace_end.value_or(0), window_enabled, audio_enabled, audio_stereo_spread,
                menu_settings.audio_volume, show_configuration_menu, settings_path, menu_settings);
        }
#endif

        if (command == "translate-full") {
            std::filesystem::path output_dir = std::filesystem::path("build") / "generated" / std::string(profile.id);
            std::vector<std::uint32_t> additional_entries;
            std::size_t output_files = 1;
            for (int i = 4; i < argc; ++i) {
                const std::string_view option(argv[i]);
                if (option == "--out" && i + 1 < argc) {
                    output_dir = argv[++i];
                } else if (option == "--entry" && i + 1 < argc) {
                    additional_entries.push_back(parse_entry_address(argv[++i]));
                } else if (option == "--files" && i + 1 < argc) {
                    const auto parsed = parse_u64(argv[++i], "--files");
                    if (parsed == 0 || parsed > 4096) throw std::runtime_error("--files must be between 1 and 4096");
                    output_files = static_cast<std::size_t>(parsed);
                } else {
                    print_usage(argv[0]);
                    return 2;
                }
            }
            translate_full_game(profile, zip_path, output_dir, additional_entries, output_files);
            return 0;
        }

        print_usage(argv[0]);
        return 2;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 1;
    }
}
