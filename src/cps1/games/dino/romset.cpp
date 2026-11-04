#include "cps1_driver.hpp"

#include <array>

namespace mamerecomp::cps1 {
namespace {
constexpr std::array<Cps1RomRegion, 4> kRegions{{
    {"maincpu", 0x400000, 0x180000, 0xff},
    {"gfx", 0x400000, 0x400000, 0x00},
    {"audiocpu", 0x028000, 0x028000, 0xff},
    {"qsound", 0x200000, 0x200000, 0x00},
}};
constexpr std::array<Cps1RomLoad, 17> kLoads{{
    {"cde_23a.8f", "maincpu", 0x80000, 0x8f4e585e, 0, 0x000000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},
    {"cde_22a.7f", "maincpu", 0x80000, 0x9278aa12, 0, 0x080000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},
    {"cde_21a.6f", "maincpu", 0x80000, 0x66d23de2, 0, 0x100000, 0x80000, 2, 2, Cps1RomTransform::WordSwap},
    {"cd-1m.3a", "gfx", 0x80000, 0x8da4f917, 0, 0x000000, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-3m.5a", "gfx", 0x80000, 0x6c40f603, 0, 0x000002, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-2m.4a", "gfx", 0x80000, 0x09c8fc2d, 0, 0x000004, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-4m.6a", "gfx", 0x80000, 0x637ff38f, 0, 0x000006, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-5m.7a", "gfx", 0x80000, 0x470befee, 0, 0x200000, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-7m.9a", "gfx", 0x80000, 0x22bfb7a3, 0, 0x200002, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-6m.8a", "gfx", 0x80000, 0xe7599ac4, 0, 0x200004, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd-8m.10a", "gfx", 0x80000, 0x211b4b15, 0, 0x200006, 0x80000, 2, 8, Cps1RomTransform::Identity},
    {"cd_q.5k", "audiocpu", 0x20000, 0x605fdb0b, 0, 0x00000, 0x08000, 1, 1, Cps1RomTransform::Identity},
    {"cd_q.5k", "audiocpu", 0x20000, 0x605fdb0b, 0x08000, 0x10000, 0x18000, 1, 1, Cps1RomTransform::Identity},
    {"cd-q1.1k", "qsound", 0x80000, 0x60927775, 0, 0x000000, 0x80000, 1, 1, Cps1RomTransform::Identity},
    {"cd-q2.2k", "qsound", 0x80000, 0x770f4c47, 0, 0x080000, 0x80000, 1, 1, Cps1RomTransform::Identity},
    {"cd-q3.3k", "qsound", 0x80000, 0x2f273ffc, 0, 0x100000, 0x80000, 1, 1, Cps1RomTransform::Identity},
    {"cd-q4.4k", "qsound", 0x80000, 0x2c67821d, 0, 0x180000, 0x80000, 1, 1, Cps1RomTransform::Identity},
}};
constexpr Cps1RomSet kRomSet{"dino", {}, kRegions, kLoads};
}
const Cps1RomSet& dino_romset() { return kRomSet; }
}
