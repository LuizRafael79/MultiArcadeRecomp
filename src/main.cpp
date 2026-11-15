#include "commands/machine_game_command.hpp"
#include "commands/naomi_commands.hpp"
#include "commands/neogeo_commands.hpp"
#include "commands/sega_commands.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        const char* configured_module_directory = std::getenv("MAMERECOMP_MODULE_DIR");
        const auto module_directory = configured_module_directory && *configured_module_directory
            ? std::filesystem::path(configured_module_directory)
            : std::filesystem::absolute(argv[0]).parent_path();
        mamerecomp::app::sega::configure_module_directory(module_directory);
        if (argc == 2 && std::string_view(argv[1]) == "list-games") {
            return mamerecomp::app::sega::list_games();
        }
        if (argc == 2 && std::string_view(argv[1]) == "list-naomi-games") {
            return mamerecomp::app::naomi::list_games(module_directory);
        }
        if (argc >= 2 && std::string_view(argv[1]) == "build")
            return mamerecomp::app::machine_game_command(argc, argv, true);
        if (argc >= 2 && std::string_view(argv[1]) == "run")
            return mamerecomp::app::machine_game_command(argc, argv, false);
        if (argc >= 2 && std::string_view(argv[1]) == "audit")
            return mamerecomp::app::machine_game_command(argc, argv, false, true);
        if (argc >= 2 && std::string_view(argv[1]) == "run-full")
            return mamerecomp::app::sega::retired_run_full();
        if (argc >= 2 && std::string_view(argv[1]) == "inspect-naomi-roms") {
            if (argc != 4) {
                mamerecomp::app::print_usage(argv[0]);
                return 2;
            }
            return mamerecomp::app::naomi::inspect_roms(argv[2], argv[3]);
        }
        if (argc >= 2 && std::string_view(argv[1]) == "inspect-neogeo-roms") {
            if (argc != 4) {
                mamerecomp::app::print_usage(argv[0]);
                return 2;
            }
            return mamerecomp::app::neogeo::inspect_roms(argv[2], argv[3]);
        }
        if (argc >= 2 && std::string_view(argv[1]) == "run-naomi-sh4")
            return mamerecomp::app::naomi::run_sh4_command(argc, argv, module_directory);
        const std::string_view command(argv[1]);
        if (mamerecomp::app::sega::handles_utility_command(command))
            return mamerecomp::app::sega::run_utility_command(argc, argv);

        mamerecomp::app::print_usage(argv[0]);
        return 2;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 1;
    }
}
