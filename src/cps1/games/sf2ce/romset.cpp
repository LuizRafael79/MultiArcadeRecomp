#include "cps1_driver.hpp"

#include <array>

namespace mamerecomp::cps1 {
namespace {
constexpr std::array<Cps1RomRegion, 4> kRegions{{
    {"maincpu", 0x400000, 0x180000, 0xff},
    {"gfx", 0x600000, 0x600000, 0x00},
    {"audiocpu", 0x018000, 0x018000, 0xff},
    {"oki", 0x040000, 0x040000, 0x00},
}};

constexpr std::array<Cps1RomLoad, 19> kLoads{{
    // ROM_LOAD16_WORD_SWAP program ROMs.
    {"s92e_23b.8f", "maincpu", 0x80000, 0x0aaa1a3a, 0, 0x000000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},
    {"s92_22b.7f", "maincpu", 0x80000, 0x2bbe15ed, 0, 0x080000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},
    {"s92_21a.6f", "maincpu", 0x80000, 0x925a7877, 0, 0x100000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},

    // ROM_LOAD64_WORD graphics, with each source word on one two-byte lane.
    {"s92-1m.3a", "gfx", 0x80000, 0x03b0d852, 0, 0x000000, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-3m.5a", "gfx", 0x80000, 0x840289ec, 0, 0x000002, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-2m.4a", "gfx", 0x80000, 0xcdb5f027, 0, 0x000004, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-4m.6a", "gfx", 0x80000, 0xe2799472, 0, 0x000006, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-5m.7a", "gfx", 0x80000, 0xba8a2761, 0, 0x200000, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-7m.9a", "gfx", 0x80000, 0xe584bfb5, 0, 0x200002, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-6m.8a", "gfx", 0x80000, 0x21e3f87d, 0, 0x200004, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-8m.10a", "gfx", 0x80000, 0xbefc47df, 0, 0x200006, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-10m.3c", "gfx", 0x80000, 0x960687d5, 0, 0x400000, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-12m.5c", "gfx", 0x80000, 0x978ecd18, 0, 0x400002, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-11m.4c", "gfx", 0x80000, 0xd6ec9a0a, 0, 0x400004, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"s92-13m.6c", "gfx", 0x80000, 0xed2c67f6, 0, 0x400006, 0x80000, 2, 8, Cps1RomTransform::Identity},

    // ROM_CONTINUE sends the second half of the Z80 image to region offset $10000.
    {"s92_09.11a", "audiocpu", 0x10000, 0x08f6b60e, 0x0000, 0x00000, 0x8000, 1, 1, Cps1RomTransform::Identity},
    {"s92_09.11a", "audiocpu", 0x10000, 0x08f6b60e, 0x8000, 0x10000, 0x8000, 1, 1, Cps1RomTransform::Identity},

    // SF2CE uses YM2151/OKI samples; it has no QSound region.
    {"s92_18.11c", "oki", 0x20000, 0x7f162009, 0, 0x00000, 0x20000, 1, 1, Cps1RomTransform::Identity},
    {"s92_19.12c", "oki", 0x20000, 0xbeade53f, 0, 0x20000, 0x20000, 1, 1, Cps1RomTransform::Identity},
}};

constexpr Cps1RomSet kSf2ceRomSet{"sf2ce", {}, kRegions, kLoads};
}

const Cps1RomSet& sf2ce_romset() { return kSf2ceRomSet; }
}
