#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace multiarcaderecomp::rom {

struct RomRegions {
    std::unordered_map<std::string, std::vector<std::uint8_t>> regions;
    const std::vector<std::uint8_t>& at(std::string_view name) const {
        const auto it = regions.find(std::string(name));
        if (it == regions.end()) throw std::runtime_error("missing ROM region " + std::string(name));
        return it->second;
    }
};

} // namespace multiarcaderecomp::rom
