#include "neogeo_board_module_api.h"

#include "kof98_game.h"
#include "kof98_module_abi.h"
#include "kof98_z80_host.h"
#include "neogeo/neogeo_romset.hpp"

#include <neogeorecomp/neogeo.h>

#include <dlfcn.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef MAMERECOMP_BINARY_DIR
#define MAMERECOMP_BINARY_DIR "build"
#endif
#ifndef MAMERECOMP_SOURCE_DIR
#define MAMERECOMP_SOURCE_DIR "."
#endif

namespace {

class Kof98GameModule {
public:
    explicit Kof98GameModule(const std::filesystem::path& path) {
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle)
            throw std::runtime_error("could not load KOF98 game module " + path.string() + ": " + dlerror());
        m_path = path;

        const auto abi = symbol<unsigned (*)()>("mamerecomp_neogeo_game_module_abi");
        const auto id = symbol<const char* (*)()>("mamerecomp_neogeo_game_module_id");
        const auto revision = symbol<unsigned (*)()>("mamerecomp_neogeo_game_module_revision");
        const auto descriptor = symbol<const ng_game_t* (*)()>("mamerecomp_neogeo_game_descriptor");
        m_reset = symbol<void (*)()>("mamerecomp_neogeo_game_reset");
        const auto table = symbol<TableFn>("mamerecomp_neogeo_game_recomp_table");
        const auto z80_bios = symbol<const void* (*)()>("mamerecomp_neogeo_game_z80_bios_code");
        const auto z80_cart = symbol<const void* (*)()>("mamerecomp_neogeo_game_z80_cart_code");
        if (!abi || !id || !revision || !descriptor || !m_reset || !table ||
            abi() != MAMERECOMP_NEOGEO_GAME_MODULE_ABI || std::string(id()) != "kof98" || revision() != 98)
            fail("KOF98 game module ABI/profile mismatch");

        m_game = descriptor();
        if (!m_game || !m_game->name || std::string(m_game->name) != "kof98")
            fail("KOF98 game module has no matching game descriptor");
        m_table = table(&m_count);
        if (!m_table || !m_count) fail("KOF98 game module has no static 68000 table");
        m_z80_bios = z80_bios ? z80_bios() : nullptr;
        m_z80_cart = z80_cart ? z80_cart() : nullptr;
    }

    ~Kof98GameModule() { if (m_handle) dlclose(m_handle); }
    Kof98GameModule(const Kof98GameModule&) = delete;
    Kof98GameModule& operator=(const Kof98GameModule&) = delete;

    const ng_game_t* game() const { return m_game; }
    const ng_func_entry_t* table() const { return m_table; }
    std::size_t count() const { return m_count; }
    const void* z80_bios() const { return m_z80_bios; }
    const void* z80_cart() const { return m_z80_cart; }
    void reset() const { m_reset(); }

private:
    using TableFn = const ng_func_entry_t* (*)(std::size_t*);

    template<class Fn> Fn symbol(const char* name) {
        dlerror();
        auto result = reinterpret_cast<Fn>(dlsym(m_handle, name));
        if (dlerror()) return nullptr;
        return result;
    }

    [[noreturn]] void fail(const char* message) {
        if (m_handle) dlclose(m_handle);
        m_handle = nullptr;
        throw std::runtime_error(std::string(message) + ": " + m_path.string());
    }

    void* m_handle = nullptr;
    std::filesystem::path m_path;
    const ng_game_t* m_game = nullptr;
    void (*m_reset)() = nullptr;
    const ng_func_entry_t* m_table = nullptr;
    std::size_t m_count = 0;
    const void* m_z80_bios = nullptr;
    const void* m_z80_cart = nullptr;
};

std::filesystem::path module_directory() {
    if (const char* configured = std::getenv("MAMERECOMP_MODULE_DIR"); configured && *configured)
        return configured;
    return std::filesystem::absolute(MAMERECOMP_BINARY_DIR);
}

struct RunOptions {
    std::filesystem::path game_zip;
    std::filesystem::path bios_zip;
    std::filesystem::path nvram;
    std::string bios = "neo-po.bin";
    std::string system = "aes";
    std::string bios_variant;
    bool bios_selected = false;
    long frame_limit = -1;
    std::optional<std::string> frame_out;
    std::vector<std::string> runtime_args;
};

RunOptions parse_options(int argc, char** argv) {
    RunOptions out;
    const auto root = std::filesystem::path(MAMERECOMP_SOURCE_DIR);
    const auto roms = root.parent_path() / "mame/roms";
    out.game_zip = roms / "kof98.zip";
    out.nvram = std::filesystem::path(MAMERECOMP_BINARY_DIR) / "neogeo/kof98/run-assets/kof98/kof98.nvram";
    bool bios_zip_selected = false;
    bool bios_selected = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto value = [&](std::string_view option) -> std::string_view {
            if (arg == option) {
                if (i + 1 >= argc) throw std::runtime_error(std::string(option) + " requires a value");
                return argv[++i];
            }
            const std::string prefix = std::string(option) + "=";
            if (arg.starts_with(prefix)) return arg.substr(prefix.size());
            return {};
        };
        if (arg == "run") continue;
        if (const auto v = value("--game"); !v.empty()) {
            if (v != "kof98") throw std::runtime_error("Neo Geo runtime currently supports --game=kof98");
        } else if (const auto v = value("--machine"); !v.empty()) {
            if (v != "neogeo") throw std::runtime_error("Neo Geo board module received a different machine id");
        } else if (const auto v = value("--board"); !v.empty()) {
            if (v != "aes" && v != "mvs") throw std::runtime_error("Neo Geo board must be aes or mvs");
            out.system = v;
        } else if (const auto v = value("--rom-dir"); !v.empty()) out.game_zip = v;
        else if (const auto v = value("--bios-zip"); !v.empty()) { out.bios_zip = v; bios_zip_selected = true; }
        else if (const auto v = value("--bios"); !v.empty()) { out.bios = v; bios_selected = true; }
        else if (const auto v = value("--nvram"); !v.empty()) out.nvram = v;
        else if (const auto v = value("--frames"); !v.empty()) {
            out.runtime_args.emplace_back("--frames");
            out.runtime_args.emplace_back(v);
            out.frame_limit = std::strtol(std::string(v).c_str(), nullptr, 10);
        } else if (const auto v = value("--frame-out"); !v.empty()) {
            out.frame_out = std::string(v);
        } else if (const auto v = value("--dump-misses"); !v.empty()) {
            out.runtime_args.emplace_back("--dump-misses");
            out.runtime_args.emplace_back(v);
        } else if (const auto v = value("--dump-fallbacks"); !v.empty()) {
            out.runtime_args.emplace_back("--dump-fallbacks");
            out.runtime_args.emplace_back(v);
        } else if (const auto v = value("--input"); !v.empty()) {
            out.runtime_args.emplace_back("--input");
            out.runtime_args.emplace_back(v);
        } else if (arg == "--headless") out.runtime_args.emplace_back(arg);
        else if (arg == "--window" || arg == "--audio") continue;
        else if (arg == "--interp" || arg == "--verify" || arg == "--mute") out.runtime_args.emplace_back(arg);
        else if (arg == "--cycles") throw std::runtime_error("Neo Geo runtime accepts --frames, not --cycles");
        else throw std::runtime_error("unsupported Neo Geo run option: " + std::string(arg));
    }
    if (!bios_zip_selected)
        out.bios_zip = out.game_zip.parent_path() / (out.system == "aes" ? "aes.zip" : "neogeo.zip");
    if (!bios_selected && out.system == "mvs") out.bios = "sp-s2.sp1";
    if (out.bios != (out.system == "aes" ? "neo-po.bin" : "sp-s2.sp1"))
        out.bios_variant = std::filesystem::path(out.bios).stem().string();
    if (out.frame_out) {
        const long frame = out.frame_limit > 0 ? out.frame_limit - 1 : 0;
        out.runtime_args.emplace_back("--screenshot");
        out.runtime_args.emplace_back(std::to_string(frame) + ":" + *out.frame_out);
    }
    return out;
}

} // namespace

extern "C" unsigned mamerecomp_board_module_abi(void) { return 1; }
extern "C" const char* mamerecomp_board_module_id(void) { return "neogeo"; }

extern "C" int mamerecomp_board_run(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (!std::filesystem::is_regular_file(options.game_zip))
            throw std::runtime_error("KOF98 ROM ZIP not found: " + options.game_zip.string());
        if (!std::filesystem::is_regular_file(options.bios_zip))
            throw std::runtime_error("Neo Geo BIOS ZIP not found: " + options.bios_zip.string());

        const auto modules = module_directory();
        const auto cache = std::filesystem::path(MAMERECOMP_BINARY_DIR) / "neogeo/kof98/run-assets";
        const auto game_dir = cache / "kof98";
        const auto bios_dir = cache / "bios";
        const auto common_bios_zip = options.game_zip.parent_path() / "neogeo.zip";
        mamerecomp::neogeo::extract_kof98_inputs(
            options.game_zip, options.bios_zip, game_dir, bios_dir, common_bios_zip);

        std::string module_id = options.system;
        if (!options.bios_variant.empty()) {
            auto bios_variant = options.bios_variant;
            for (auto& ch : bios_variant)
                ch = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
            for (auto& ch : bios_variant)
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) ch = '_';
            module_id += "_" + bios_variant;
        }
        const auto game_module_path = modules / ("mamerecomp_game_kof98_" + module_id + ".so");
        if (!std::filesystem::is_regular_file(game_module_path))
            throw std::runtime_error("KOF98 AoT module for " + options.system + " BIOS " + options.bios +
                " is missing; build it with `mamerecomp build --machine=neogeo --board=" + options.system +
                " --game=kof98 --bios=" + options.bios + "`");
        Kof98GameModule game_module(game_module_path);
        ng_z80_recomp_set_codes(game_module.z80_bios(), game_module.z80_cart());

        std::vector<std::string> args{"mamerecomp-neogeo", "--rom-path", game_dir.string(),
            "--bios-path", bios_dir.string(), "--bios", options.bios, "--system", options.system,
            "--nvram", options.nvram.string()};
        args.insert(args.end(), options.runtime_args.begin(), options.runtime_args.end());
        std::vector<char*> cargs;
        cargs.reserve(args.size() + 1);
        for (auto& arg : args) cargs.push_back(arg.data());
        game_module.reset();
        std::cerr << "[mamerecomp] Neo Geo KOF98 module AoT 68000 entries=" << game_module.count()
                  << "; launching through the registered board runtime\n";
        return ng_main(static_cast<int>(args.size()), cargs.data(), game_module.game(),
                       game_module.table(), game_module.count());
    } catch (const std::exception& error) {
        std::cerr << "neogeo board module: " << error.what() << '\n';
        return 1;
    }
}
