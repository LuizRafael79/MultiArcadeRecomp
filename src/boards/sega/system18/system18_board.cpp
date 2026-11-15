#include "system18_board.hpp"

#include <array>

namespace multiarcaderecomp {
namespace {

// Mirrors and offsets follow the separate map_as_* calls in MAME. In
// particular, mapper region 5 contains two windows with different mirrors.
constexpr std::array<System18MapperWindow, 8> kShadowDancerWindows{{
    {7, System18Target::Io,         0x00000, 0x04000, 0xffc000},
    {6, System18Target::PaletteRam, 0x00000, 0x01000, 0xfff000},
    {5, System18Target::TileRam,    0x00000, 0x10000, 0xfe0000},
    {5, System18Target::TextRam,    0x10000, 0x01000, 0xfef000},
    {4, System18Target::SpriteRam,  0x00000, 0x00800, 0xfff800},
    {3, System18Target::WorkRam,    0x00000, 0x04000, 0xffc000},
    {1, System18Target::Vdp,        0x00000, 0x00010, 0xfffff0},
    {0, System18Target::MainRom,    0x00000, 0x80000, 0xf80000},
}};

// MAME's init_generic_5874 maps the same board RAM/windows but places the VDP
// on mapper region 2 instead of Shadow Dancer's region 1. Region 1 is ROM bank
// 1 on this board; this 512 KiB mwalkd program set has no bytes at ROM offset
// 0x80000, so that bank is intentionally absent from this profile.
constexpr std::array<System18MapperWindow, 8> kSega5874Windows{{
    {7, System18Target::Io,         0x00000, 0x04000, 0xffc000},
    {6, System18Target::PaletteRam, 0x00000, 0x01000, 0xfff000},
    {5, System18Target::TileRam,    0x00000, 0x10000, 0xfe0000},
    {5, System18Target::TextRam,    0x10000, 0x01000, 0xfef000},
    {4, System18Target::SpriteRam,  0x00000, 0x00800, 0xfff800},
    {3, System18Target::WorkRam,    0x00000, 0x04000, 0xffc000},
    {2, System18Target::Vdp,        0x00000, 0x00010, 0xfffff0},
    {0, System18Target::MainRom,    0x00000, 0x80000, 0xf80000},
}};

} // namespace

std::span<const System18MapperWindow> system18_shdancer_mapper_windows() {
    return system18_mapper_windows(System18RomBoard::ShadowDancer171);
}

std::span<const System18MapperWindow> system18_mapper_windows(System18RomBoard board) {
    return board == System18RomBoard::Sega5874 ? std::span<const System18MapperWindow>(kSega5874Windows)
                                               : std::span<const System18MapperWindow>(kShadowDancerWindows);
}

} // namespace multiarcaderecomp
