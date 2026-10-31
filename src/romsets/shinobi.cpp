#include "../system16/system16.hpp"
#include <array>
namespace mamerecomp::romsets {
constexpr std::array<RomLoad16Byte, 4> kShinobiMainCpuRoms{{
    {"epr-12010.43", 0x00000, 0x10000, 0x7df7f4a2},
    {"epr-12008.26", 0x00001, 0x10000, 0xf5ae64cd},
    {"epr-12011.42", 0x20000, 0x10000, 0x9d46e707},
    {"epr-12009.25", 0x20001, 0x10000, 0x7961d07e},
}};

constexpr std::array<RomRegionLoad, 3> kShinobiTiles{{
    {"epr-11264.95", 0x10000, 0x46627e7d, 0, 0x00000, 0x10000, 1},
    {"epr-11265.94", 0x10000, 0x87d0f321, 0, 0x10000, 0x10000, 1},
    {"epr-11266.93", 0x10000, 0xefb4af87, 0, 0x20000, 0x10000, 1},
}};

constexpr std::array<RomRegionLoad, 1> kShinobiSoundCpu{{
    {"epr-11267.12", 0x8000, 0xdd50b745, 0, 0, 0x8000, 1},
}};

constexpr std::array<RomRegionLoad, 1> kShinobiUpd7751{{
    {"7751.bin", 0x0400, 0x6a9534fc, 0, 0, 0x0400, 1},
}};

constexpr std::array<RomRegionLoad, 1> kShinobiUpd7751Data{{
    {"epr-11268.1", 0x8000, 0x6d7966da, 0, 0, 0x8000, 1},
}};

constexpr std::array<RomRegionLoad, 16> kShinobiSprites{{
    {"epr-11290.10", 0x10000, 0x611f413a, 0x00000, 0x00001, 0x08000, 2},
    {"epr-11290.10", 0x10000, 0x611f413a, 0x08000, 0x40001, 0x08000, 2},
    {"epr-11294.11", 0x10000, 0x5eb00fc1, 0x00000, 0x00000, 0x08000, 2},
    {"epr-11294.11", 0x10000, 0x5eb00fc1, 0x08000, 0x40000, 0x08000, 2},
    {"epr-11291.17", 0x10000, 0x3c0797c0, 0x00000, 0x10001, 0x08000, 2},
    {"epr-11291.17", 0x10000, 0x3c0797c0, 0x08000, 0x50001, 0x08000, 2},
    {"epr-11295.18", 0x10000, 0x25307ef8, 0x00000, 0x10000, 0x08000, 2},
    {"epr-11295.18", 0x10000, 0x25307ef8, 0x08000, 0x50000, 0x08000, 2},
    {"epr-11292.23", 0x10000, 0xc29ac34e, 0x00000, 0x20001, 0x08000, 2},
    {"epr-11292.23", 0x10000, 0xc29ac34e, 0x08000, 0x60001, 0x08000, 2},
    {"epr-11296.24", 0x10000, 0x04a437f8, 0x00000, 0x20000, 0x08000, 2},
    {"epr-11296.24", 0x10000, 0x04a437f8, 0x08000, 0x60000, 0x08000, 2},
    {"epr-11293.29", 0x10000, 0x41f41063, 0x00000, 0x30001, 0x08000, 2},
    {"epr-11293.29", 0x10000, 0x41f41063, 0x08000, 0x70001, 0x08000, 2},
    {"epr-11297.30", 0x10000, 0xb6e1fd72, 0x00000, 0x30000, 0x08000, 2},
    {"epr-11297.30", 0x10000, 0xb6e1fd72, 0x08000, 0x70000, 0x08000, 2},
}};

const GameProfile& shinobi() {
    static const GameProfile profile{
        "shinobi", "Shinobi (set 6, System 16A, unprotected)", BoardFamily::System16A,
        0x40000, kShinobiMainCpuRoms, 0x30000, kShinobiTiles, 0x80000, kShinobiSprites,
        0x8000, kShinobiSoundCpu, 0x0400, kShinobiUpd7751, 0x8000, kShinobiUpd7751Data,
        0, {}};
    return profile;
}
}
