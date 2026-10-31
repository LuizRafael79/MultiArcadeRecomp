#include "../system16/system16.hpp"
#include "goldnaxe_shared.hpp"
#include <array>
namespace mamerecomp::romsets {
static constexpr std::array<RomLoad16Byte, 2> goldnaxeud_maincpu{{
    {"bootleg_epr-12543.ic2", 0x00000, 0x40000, 0xe3089080},
    {"bootleg_epr-12542.ic1", 0x00001, 0x40000, 0x1e84364b},
}};
const GameProfile& goldnaxeud() {
    static const GameProfile profile{
        "goldnaxeud", "Golden Axe (set 5, US, decrypted bootleg)", BoardFamily::System16B,
        0x80000, goldnaxeud_maincpu, 0x60000, goldnaxe_tiles, 0x1c0000, goldnaxe_sprites,
        0x90000, goldnaxe_soundcpu, 0, {}, 0, {}, 0, {}, System16BBoard::Sega5797, "goldnaxe"};
    return profile;
}
}
