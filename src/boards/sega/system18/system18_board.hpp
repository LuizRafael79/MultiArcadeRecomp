#pragma once

#include "boards/sega/common/board_family.hpp"

#include <cstdint>
#include <span>

namespace multiarcaderecomp {

// A 315-5195 mapper region can fan out into multiple board windows. Keep this
// separate from the simplified single-window System 16B wiring table.
enum class System18Target : std::uint8_t {
    MainRom,
    Vdp,
    WorkRam,
    SpriteRam,
    TileRam,
    TextRam,
    PaletteRam,
    Io,
};

struct System18MapperWindow {
    std::uint8_t mapper_region;
    System18Target target;
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t mirror;
};

// Shadow Dancer's ROM board 171-SHADOW wiring from segas18_state::memory_mapper.
std::span<const System18MapperWindow> system18_shdancer_mapper_windows();
std::span<const System18MapperWindow> system18_mapper_windows(System18RomBoard board);

} // namespace multiarcaderecomp
