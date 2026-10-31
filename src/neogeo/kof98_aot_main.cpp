#include "kof98_game.h"
#include "neogeo/neogeo_romset.hpp"

#include <neogeorecomp/recomp.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::filesystem::path game_zip, bios_zip, output_dir="build/neogeo/kof98";
        std::string bios_name="sp-s2.sp1";
        std::vector<std::string> extra_entries;
        for (int i=1; i<argc; ++i) {
            const std::string arg=argv[i];
            if (arg=="--rom-zip" && i+1<argc) game_zip=argv[++i];
            else if (arg=="--bios-zip" && i+1<argc) bios_zip=argv[++i];
            else if (arg=="--bios" && i+1<argc) bios_name=argv[++i];
            else if (arg=="--out" && i+1<argc) output_dir=argv[++i];
            else if (arg=="--entries" && i+1<argc) extra_entries.emplace_back(argv[++i]);
            else throw std::runtime_error("usage: kof98_aot --rom-zip kof98.zip [--bios-zip neogeo.zip] [--bios FILE] [--out DIR] [--entries FILE]");
        }
        if (game_zip.empty()) throw std::runtime_error("--rom-zip is required");
        if (bios_zip.empty()) bios_zip=game_zip.parent_path()/"neogeo.zip";
        const auto assets=output_dir/"assets";
        mamerecomp::neogeo::extract_kof98_inputs(game_zip,bios_zip,assets/"kof98",assets/"bios");
        const auto generated=output_dir/"generated";
        std::filesystem::create_directories(generated);
        std::vector<std::string> args{"kof98_aot","--rom-path",(assets/"kof98").string(),
            "--bios-path",(assets/"bios").string(),"--bios",bios_name,"--out",generated.string(),"--files","32"};
        for (const auto& entries:extra_entries) { args.emplace_back("--entries"); args.push_back(entries); }
        std::vector<char*> cargs; cargs.reserve(args.size()+1);
        for (auto& arg:args) cargs.push_back(arg.data());
        cargs.push_back(nullptr);
        std::cerr << "[kof98-aot] validating ROMs, applying PROGSF1 transform, then discovering "
                  << bios_name << " and cart code\n";
        const int result=ng_recomp_main(static_cast<int>(args.size()),cargs.data(),&mamerecomp_kof98_game);
        if (result) return result;
        std::cout << "KOF98 AOT source generated in " << std::filesystem::absolute(generated)
                  << ". The game runner compiles these BIOS and cart routines natively.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "kof98_aot: " << e.what() << '\n';
        return 1;
    }
}
