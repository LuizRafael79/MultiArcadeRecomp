#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace multiarcaderecomp::roms {

std::string sha1_hex(std::span<const std::uint8_t> data);

} // namespace multiarcaderecomp::roms
