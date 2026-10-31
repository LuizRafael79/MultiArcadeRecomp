#include "kof98_game.h"
#include "kof98_module_abi.h"
#include "kof98_z80_host.h"
#include "neogeo/neogeo_romset.hpp"

#include <neogeorecomp/neogeo.h>

#include <filesystem>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class Kof98GameModule {
public:
    explicit Kof98GameModule(const std::filesystem::path& executable_dir) {
        const char* configured = std::getenv("MAMERECOMP_MODULE_DIR");
        const auto module_dir = configured && *configured
            ? std::filesystem::path(configured) : executable_dir;
        const auto path = module_dir / "mamerecomp_game_kof98.so";
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle) throw std::runtime_error("could not load KOF98 game module " +
            path.string() + ": " + dlerror());

        const auto abi = reinterpret_cast<unsigned (*)()>(
            dlsym(m_handle, "mamerecomp_neogeo_game_module_abi"));
        const auto id = reinterpret_cast<const char* (*)()>(
            dlsym(m_handle, "mamerecomp_neogeo_game_module_id"));
        const auto revision = reinterpret_cast<unsigned (*)()>(
            dlsym(m_handle, "mamerecomp_neogeo_game_module_revision"));
        const auto table = reinterpret_cast<TableFn>(
            dlsym(m_handle, "mamerecomp_neogeo_game_recomp_table"));
        const auto z80_bios = reinterpret_cast<const void* (*)()>(
            dlsym(m_handle, "mamerecomp_neogeo_game_z80_bios_code"));
        const auto z80_cart = reinterpret_cast<const void* (*)()>(
            dlsym(m_handle, "mamerecomp_neogeo_game_z80_cart_code"));
        if (!abi || !id || !revision || !table ||
            abi() != MAMERECOMP_NEOGEO_GAME_MODULE_ABI ||
            std::string(id()) != "kof98" || revision() != 98) {
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("KOF98 game module ABI/profile mismatch: " + path.string());
        }
        m_table = table(&m_count);
        if (!m_table || !m_count) {
            dlclose(m_handle);
            m_handle = nullptr;
            throw std::runtime_error("KOF98 game module has no static 68000 table: " + path.string());
        }
        if (z80_bios && z80_cart) {
            m_z80_bios = z80_bios();
            m_z80_cart = z80_cart();
        }
    }

    ~Kof98GameModule() { if (m_handle) dlclose(m_handle); }
    Kof98GameModule(const Kof98GameModule&) = delete;
    Kof98GameModule& operator=(const Kof98GameModule&) = delete;

    const ng_func_entry_t* table() const { return m_table; }
    size_t count() const { return m_count; }
    const void* z80_bios_code() const { return m_z80_bios; }
    const void* z80_cart_code() const { return m_z80_cart; }

private:
    using TableFn = const ng_func_entry_t* (*)(size_t*);
    void* m_handle = nullptr;
    const ng_func_entry_t* m_table = nullptr;
    size_t m_count = 0;
    const void* m_z80_bios = nullptr;
    const void* m_z80_cart = nullptr;
};
}

int main(int argc, char** argv) {
    try {
#ifndef MAMERECOMP_KOF98_HAS_AOT
        std::cerr << "KOF98 runner has no static 68000 table. Generate with kof98_aot and configure "
                     "MAMERECOMP_NEOGEO_GENERATED_DIR before building kof98recomp.\n";
        return 2;
#else
        std::filesystem::path game_zip,bios_zip,cache="build/neogeo/kof98/run-assets";
        std::vector<std::string> args{argv[0]};
        bool bios_selected=false;
        bool nvram_selected=false;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--rom-zip" && i+1<argc) game_zip=argv[++i];
            else if (arg=="--bios-zip" && i+1<argc) bios_zip=argv[++i];
            else if (arg=="--cache" && i+1<argc) cache=argv[++i];
            else {
                if (arg=="--bios" && i+1<argc) bios_selected=true;
                if (arg=="--nvram" && i+1<argc) nvram_selected=true;
                args.push_back(arg);
            }
        }
        if (game_zip.empty()) throw std::runtime_error("usage: kof98recomp --rom-zip kof98.zip [--bios-zip neogeo.zip] [--bios FILE] [--system mvs|aes] [--interp --headless --frames N]");
        if (bios_zip.empty()) bios_zip=game_zip.parent_path()/"neogeo.zip";
        Kof98GameModule game_module(std::filesystem::absolute(argv[0]).parent_path());
        ng_z80_recomp_set_codes(game_module.z80_bios_code(), game_module.z80_cart_code());
        const auto game_dir=cache/"kof98";
        const auto bios_dir=cache/"bios";
        mamerecomp::neogeo::extract_kof98_inputs(game_zip,bios_zip,game_dir,bios_dir);
        args.insert(args.begin()+1,{"--rom-path",game_dir.string(),"--bios-path",bios_dir.string()});
        if (!bios_selected) args.insert(args.begin()+3,{"--bios","sp-s2.sp1"});
        if (!nvram_selected) args.insert(args.begin()+3,{"--nvram",(game_dir/"kof98.nvram").string()});
        std::vector<char*> cargs; cargs.reserve(args.size()+1);
        for (auto& arg:args) cargs.push_back(arg.data());
        cargs.push_back(nullptr);
        mamerecomp_kof98_prot_reset();
        std::cerr << "[kof98recomp] KOF98 game module AOT 68000 entries=" << game_module.count()
                  << "; BIOS and cart code dispatch natively, Musashi handles uncovered PCs\n";
        return ng_main(static_cast<int>(args.size()),cargs.data(),&mamerecomp_kof98_game,
                       game_module.table(),game_module.count());
#endif
    } catch (const std::exception& e) {
        std::cerr << "kof98recomp: " << e.what() << '\n';
        return 1;
    }
}
