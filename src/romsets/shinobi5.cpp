#include "profiles.hpp"

#include <array>

namespace mamerecomp::romsets {
namespace {
constexpr std::array<RomLoad16Byte, 4> maincpu{{
    {"epr-11299.a4", 0x00000, 0x10000, 0xb930399d}, {"epr-11298.a1", 0x00001, 0x10000, 0x343f4c46},
    {"epr-11283.a5", 0x20000, 0x10000, 0x9d46e707}, {"epr-11281.a2", 0x20001, 0x10000, 0x7961d07e},
}};
constexpr std::array<RomRegionLoad, 3> tiles{{
    {"opr-11284.b9", 0x10000, 0x5f62e163, 0, 0x00000, 0x10000, 1},
    {"opr-11285.b10",0x10000, 0x75f8fbc9, 0, 0x10000, 0x10000, 1},
    {"opr-11286.b11",0x10000, 0x06508bb9, 0, 0x20000, 0x10000, 1},
}};
constexpr std::array<RomRegionLoad, 8> sprites{{
    {"opr-11290.b1",0x10000,0x611f413a,0,0x00001,0x10000,2}, {"opr-11294.b5",0x10000,0x5eb00fc1,0,0x00000,0x10000,2},
    {"opr-11291.b2",0x10000,0x3c0797c0,0,0x20001,0x10000,2}, {"opr-11295.b6",0x10000,0x25307ef8,0,0x20000,0x10000,2},
    {"opr-11292.b3",0x10000,0xc29ac34e,0,0x40001,0x10000,2}, {"opr-11296.b7",0x10000,0x04a437f8,0,0x40000,0x10000,2},
    {"opr-11293.b4",0x10000,0x41f41063,0,0x60001,0x10000,2}, {"opr-11297.b8",0x10000,0xb6e1fd72,0,0x60000,0x10000,2},
}};
constexpr std::array<RomRegionLoad, 3> sound{{
    {"epr-11287.a7",0x08000,0xe8cccd42,0,0x00000,0x08000,1},
    {"epr-11288.a8",0x08000,0xc8df8460,0,0x10000,0x08000,1},
    {"epr-11289.a9",0x08000,0xe5a4cf30,0,0x20000,0x08000,1},
}};
}
const GameProfile& shinobi5() {
    static constexpr GameProfile profile{
        "shinobi5", "Shinobi (set 5, System 16B, unprotected)", BoardFamily::System16B,
        0x40000, maincpu, 0x30000, tiles, 0x80000, sprites,
        0x50000, sound, 0, {}, 0, {}, 0, {}, System16BBoard::Sega5358, "shinobi"};
    return profile;
}
}
