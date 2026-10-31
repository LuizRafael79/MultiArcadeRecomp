#include "profiles.hpp"

#include <array>

namespace mamerecomp::romsets {
namespace {

// MAME: src/mame/sega/segas18.cpp, ROM_START(shdancer).
constexpr std::array<RomLoad16Byte, 2> maincpu{{
    {"epr-12774b.a6", 0x000000, 0x040000, 0x3d5b3fa9},
    {"epr-12773b.a5", 0x000001, 0x040000, 0x2596004e},
}};

constexpr std::array<RomRegionLoad, 3> tiles{{
    {"mpr-12712.b1", 0x040000, 0x9bdabe3d, 0, 0x000000, 0x040000, 1},
    {"mpr-12713.b2", 0x040000, 0x852d2b1c, 0, 0x040000, 0x040000, 1},
    {"mpr-12714.b3", 0x040000, 0x448226ce, 0, 0x080000, 0x040000, 1},
}};

constexpr std::array<RomRegionLoad, 8> sprites{{
    {"mpr-12719.b11", 0x040000, 0xd6888534, 0, 0x000001, 0x040000, 2},
    {"mpr-12726.a11", 0x040000, 0xff344945, 0, 0x000000, 0x040000, 2},
    {"mpr-12718.b10", 0x040000, 0xba2efc0c, 0, 0x080001, 0x040000, 2},
    {"mpr-12725.a10", 0x040000, 0x268a0c17, 0, 0x080000, 0x040000, 2},
    {"mpr-12717.b9",  0x040000, 0xc81cc4f8, 0, 0x100001, 0x040000, 2},
    {"mpr-12724.a9",  0x040000, 0x0f4903dc, 0, 0x100000, 0x040000, 2},
    {"epr-12716.b8",  0x040000, 0xa870e629, 0, 0x180001, 0x040000, 2},
    {"epr-12723.a8",  0x040000, 0xc606cf90, 0, 0x180000, 0x040000, 2},
}};

constexpr std::array<RomRegionLoad, 2> soundcpu{{
    {"epr-12987.a4", 0x020000, 0xd1c020cc, 0, 0x000000, 0x020000, 1},
    {"mpr-12715.b4", 0x040000, 0x07051a52, 0, 0x080000, 0x040000, 1},
}};

} // namespace

const GameProfile& shdancer() {
    static constexpr GameProfile profile{
        "shdancer", "Shadow Dancer (World, System 18, unprotected)", BoardFamily::System18,
        0x080000, maincpu, 0x0c0000, tiles, 0x200000, sprites,
        0x200000, soundcpu, 0, {}, 0, {}, 0, {},
        System16BBoard::Sega5797, {}, 0xff};
    return profile;
}

} // namespace mamerecomp::romsets
