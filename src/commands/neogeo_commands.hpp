#pragma once

#include <filesystem>
#include <string_view>

namespace mamerecomp::app::neogeo {

int inspect_roms(std::string_view game, const std::filesystem::path& game_zip);

} // namespace mamerecomp::app::neogeo
