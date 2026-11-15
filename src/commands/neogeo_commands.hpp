#pragma once

#include <filesystem>
#include <string_view>

namespace multiarcaderecomp::app::neogeo {

int inspect_roms(std::string_view game, const std::filesystem::path& game_zip);

} // namespace multiarcaderecomp::app::neogeo
