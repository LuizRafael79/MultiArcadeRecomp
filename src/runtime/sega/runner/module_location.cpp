#include "runtime/sega/runner/module_location.hpp"

#include <cstdlib>
#include <utility>

namespace mamerecomp::sega {
namespace {
std::filesystem::path g_module_directory;
}

void set_module_directory(std::filesystem::path path) {
    g_module_directory = std::move(path);
}

std::filesystem::path module_directory() {
    const char* configured_directory = std::getenv("MAMERECOMP_MODULE_DIR");
    if (configured_directory && *configured_directory) return configured_directory;
    return g_module_directory;
}

} // namespace mamerecomp::sega
