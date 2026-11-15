#pragma once

#include <filesystem>

namespace multiarcaderecomp::sega {

void set_module_directory(std::filesystem::path path);
std::filesystem::path module_directory();

} // namespace multiarcaderecomp::sega
