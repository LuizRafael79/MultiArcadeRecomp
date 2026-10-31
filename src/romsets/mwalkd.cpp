#include "profiles.hpp"

#include <array>

namespace mamerecomp::romsets {
namespace {

// MAME: src/mame/sega/segas18.cpp, ROM_START(mwalkd). This is the
// unencrypted bootleg set; the parent mwalk set requires FD1094 decryption.
constexpr std::array<RomLoad16Byte, 2> maincpu{{
    {"bootleg_epr-13235.a6", 0x000000, 0x040000, 0x9973f6c3},
    {"bootleg_epr-13234.a5", 0x000001, 0x040000, 0xef5098e9},
}};

constexpr std::array<RomRegionLoad, 3> tiles{{
    {"mpr-13216.b1", 0x040000, 0x862d2c03, 0, 0x000000, 0x040000, 1},
    {"mpr-13217.b2", 0x040000, 0x7d1ac3ec, 0, 0x040000, 0x040000, 1},
    {"mpr-13218.b3", 0x040000, 0x56d3393c, 0, 0x080000, 0x040000, 1},
}};

constexpr std::array<RomRegionLoad, 8> sprites{{
    {"mpr-13224.b11", 0x040000, 0xc59f107b, 0, 0x000001, 0x040000, 2},
    {"mpr-13231.a11", 0x040000, 0xa5e96346, 0, 0x000000, 0x040000, 2},
    {"mpr-13223.b10", 0x040000, 0x364f60ff, 0, 0x080001, 0x040000, 2},
    {"mpr-13230.a10", 0x040000, 0x9550091f, 0, 0x080000, 0x040000, 2},
    {"mpr-13222.b9",  0x040000, 0x523df3ed, 0, 0x100001, 0x040000, 2},
    {"mpr-13229.a9",  0x040000, 0xf40dc45d, 0, 0x100000, 0x040000, 2},
    {"epr-13221.b8",  0x040000, 0x9ae7546a, 0, 0x180001, 0x040000, 2},
    {"epr-13228.a8",  0x040000, 0xde3786be, 0, 0x180000, 0x040000, 2},
}};

constexpr std::array<RomRegionLoad, 4> soundcpu{{
    {"epr-13225.a4", 0x020000, 0x56c2e82b, 0, 0x000000, 0x020000, 1},
    {"mpr-13219.b4", 0x040000, 0x19e2061f, 0, 0x080000, 0x040000, 1},
    {"mpr-13220.b5", 0x040000, 0x58d4d9ce, 0, 0x100000, 0x040000, 1},
    {"mpr-13249.b6", 0x040000, 0x623edc5d, 0, 0x180000, 0x040000, 1},
}};

constexpr std::array<RomRegionLoad, 1> mcu{{
    {"315-5437.ic4", 0x001000, 0x4bf63bc1, 0, 0x000000, 0x001000, 1},
}};

} // namespace

const GameProfile& mwalkd() {
    static constexpr GameProfile profile{
        "mwalkd", "Michael Jackson's Moonwalker (World, System 18, decrypted bootleg)",
        BoardFamily::System18, 0x080000, maincpu, 0x0c0000, tiles, 0x200000, sprites,
        0x200000, soundcpu, 0, {}, 0, {}, 0x010000, mcu,
        System16BBoard::Sega5797, "mwalk", 0xff, System18RomBoard::Sega5874};
    return profile;
}

} // namespace mamerecomp::romsets
