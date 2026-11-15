#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace mamerecomp::roms {

std::string sha1_hex(std::span<const std::uint8_t> data);

} // namespace mamerecomp::roms
