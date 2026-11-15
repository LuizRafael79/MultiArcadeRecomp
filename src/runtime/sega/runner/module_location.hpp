#pragma once

#include <filesystem>

namespace mamerecomp::sega {

void set_module_directory(std::filesystem::path path);
std::filesystem::path module_directory();

} // namespace mamerecomp::sega
