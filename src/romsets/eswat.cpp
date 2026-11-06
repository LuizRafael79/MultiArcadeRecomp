#include "system16.hpp"
#include <array>
namespace mamerecomp::romsets {
static constexpr std::array<RomLoad16Byte, 2> eswat_maincpu{{
    {"epr-12659.a2", 0x0, 0x40000, 0xc5ab2db9, {}},
    {"epr-12658.a1", 0x1, 0x40000, 0xaf40bd71, {}},
}};
static constexpr std::array<RomRegionLoad, 1> eswat_fd1094_key{{
    {"317-0130.key", 0x2000, 0xba7b717b, 0x0, 0x0, 0x2000, 1, {}},
}};
static constexpr std::array<RomRegionLoad, 3> eswat_tiles{{
    {"mpr-12624.b11", 0x40000, 0x375a5ec4, 0x0, 0x0, 0x40000, 1, {}},
    {"mpr-12625.b12", 0x40000, 0x3b8c757e, 0x0, 0x40000, 0x40000, 1, {}},
    {"mpr-12626.b13", 0x40000, 0x3efca25c, 0x0, 0x80000, 0x40000, 1, {}},
}};
static constexpr std::array<RomRegionLoad, 12> eswat_sprites{{
    {"mpr-12618.b1", 0x40000, 0xd1530bf, 0x0, 0x1, 0x20000, 2, {}},
    {"mpr-12618.b1", 0x40000, 0xd1530bf, 0x20000, 0x100001, 0x20000, 2, {}},
    {"mpr-12621.b4", 0x40000, 0x18ff0799, 0x0, 0x0, 0x20000, 2, {}},
    {"mpr-12621.b4", 0x40000, 0x18ff0799, 0x20000, 0x100000, 0x20000, 2, {}},
    {"mpr-12619.b2", 0x40000, 0x32069246, 0x0, 0x40001, 0x20000, 2, {}},
    {"mpr-12619.b2", 0x40000, 0x32069246, 0x20000, 0x140001, 0x20000, 2, {}},
    {"mpr-12622.b5", 0x40000, 0xa3dfe436, 0x0, 0x40000, 0x20000, 2, {}},
    {"mpr-12622.b5", 0x40000, 0xa3dfe436, 0x20000, 0x140000, 0x20000, 2, {}},
    {"mpr-12620.b3", 0x40000, 0xf6b096e0, 0x0, 0x80001, 0x20000, 2, {}},
    {"mpr-12620.b3", 0x40000, 0xf6b096e0, 0x20000, 0x180001, 0x20000, 2, {}},
    {"mpr-12623.b6", 0x40000, 0x6773fef6, 0x0, 0x80000, 0x20000, 2, {}},
    {"mpr-12623.b6", 0x40000, 0x6773fef6, 0x20000, 0x180000, 0x20000, 2, {}},
}};
static constexpr std::array<RomRegionLoad, 2> eswat_soundcpu{{
    {"epr-12617.a13", 0x8000, 0x7efecf23, 0x0, 0x0, 0x8000, 1, {}},
    {"mpr-12616.a11", 0x40000, 0x254347c2, 0x0, 0x10000, 0x40000, 1, {}},
}};
const GameProfile& eswat() {
    static const GameProfile profile{
        "eswat",
        "Cyber Police ESWAT (set 4, World) (FD1094 317-0130)",
        BoardFamily::System16B,
        0x80000,
        eswat_maincpu,
        0xc0000,
        eswat_tiles,
        0x1c0000,
        eswat_sprites,
        0x90000,
        eswat_soundcpu,
        0,
        {},
        0,
        {},
        0,
        {},
        System16BBoard::Sega5797,
        {},
        0,
        System18RomBoard::ShadowDancer171,
        0x2000,
        eswat_fd1094_key
    };
    return profile;
}
} // namespace mamerecomp::romsets
