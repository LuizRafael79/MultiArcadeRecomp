#pragma once

#include <filesystem>
#include <string_view>

namespace mamerecomp::app::sega {

void configure_module_directory(const std::filesystem::path& directory);
int list_games();
int retired_run_full();
bool handles_utility_command(std::string_view command);
int run_utility_command(int argc, char** argv);

} // namespace mamerecomp::app::sega
