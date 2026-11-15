#pragma once

#include <cstdint>

namespace mamerecomp {

enum class SegaBoardFamily : std::uint8_t {
    System16A,
    System16B,
    System18,
};

enum class System16BBoard : std::uint8_t {
    Sega5358,
    Sega5521,
    Sega5797,
};

enum class System18RomBoard : std::uint8_t {
    ShadowDancer171,
    Sega5874,
};

} // namespace mamerecomp
