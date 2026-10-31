#include "../system16/system16.hpp"
#include "goldnaxe_shared.hpp"
#include <array>
namespace mamerecomp::romsets {
static constexpr std::array<RomLoad16Byte, 2> goldnaxe_maincpu{{
    {"epr-12545.ic2", 0x00000, 0x40000, 0xa97c4e4d},
    {"epr-12544.ic1", 0x00001, 0x40000, 0x5e38f668},
}};

static constexpr std::array<RomRegionLoad, 1> goldnaxe_mcu{{
    {"317-0123a.c2", 0x01000, 0xcf19e7d4, 0, 0, 0x01000, 1},
}};

const GameProfile& goldnaxe() {
    static const GameProfile profile{
        "goldnaxe", "Golden Axe (set 6, US, System 16B, 8751)", BoardFamily::System16B,
        0x80000, goldnaxe_maincpu, 0x60000, goldnaxe_tiles, 0x1c0000, goldnaxe_sprites,
        0x90000, goldnaxe_soundcpu, 0, {}, 0, {}, 0x1000, goldnaxe_mcu};
    return profile;
}
}
