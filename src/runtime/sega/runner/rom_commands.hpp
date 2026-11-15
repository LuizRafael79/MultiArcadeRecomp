#pragma once

#include "runtime/sega/profiles/sega_profiles.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace mamerecomp::sega {

void print_game_profiles();
const GameProfile& require_profile(std::string_view id);
void inspect_game(const GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path* output_path);
void translate_full_game(const GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path& output_dir, const std::vector<std::uint32_t>& additional_entries,
    std::size_t output_files, bool scan_pointers);
void extract_graphics(const GameProfile& profile, const std::filesystem::path& zip_path,
    const std::filesystem::path& output_dir);
void print_memory_map(const GameProfile& profile, const std::vector<std::uint8_t>& maincpu);
int run_rom_command(int argc, char** argv);

} // namespace mamerecomp::sega
