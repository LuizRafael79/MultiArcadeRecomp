#include "commands/sega_commands.hpp"

#include "runtime/sega/profiles/sega_profiles.hpp"
#include "runtime/sega/runner/module_location.hpp"
#include "runtime/sega/runner/rom_commands.hpp"

#include <iostream>

namespace mamerecomp::app::sega {

void configure_module_directory(const std::filesystem::path& directory) {
    mamerecomp::sega::set_module_directory(directory);
}

int list_games() {
    mamerecomp::sega::print_game_profiles();
    return 0;
}

int retired_run_full() {
    std::cerr << "run-full is retired; use `mamerecomp run --machine=sega --board=<system16a|system16b|system18> --game=<profile>` instead.\n";
    return 2;
}

bool handles_utility_command(std::string_view command) {
    return command == "extract-graphics" || command == "map" || command == "inspect"
        || command == "translate-full";
}

int run_utility_command(int argc, char** argv) {
    return mamerecomp::sega::run_rom_command(argc, argv);
}

} // namespace mamerecomp::app::sega
