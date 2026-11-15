#pragma once

#include <filesystem>

namespace mamerecomp::app::naomi {

int list_games(const std::filesystem::path& module_directory);
int inspect_roms(const char* set_id, const char* rom_path);
int run_sh4_command(int argc, char** argv, const std::filesystem::path& module_directory);

} // namespace mamerecomp::app::naomi
