#pragma once

#include "cps1_driver.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace multiarcaderecomp::cps1 {

using RomRegions = std::unordered_map<std::string, std::vector<std::uint8_t>>;

RomRegions load_romset(const Cps1RomSet& romset, const std::filesystem::path& zip_path);
void export_romset(const Cps1RomSet& romset, const std::filesystem::path& zip_path,
                   const std::filesystem::path& output_dir);

} // namespace multiarcaderecomp::cps1
