#pragma once

#include <cstddef>
#include <cstdint>
#include <neogeorecomp/neogeo.h>

// Versioned boundary between the shared Neo Geo runner and a per-game AOT
// package. Game-specific ROM transforms/protection stay in the Neo Geo game
// profile; generated 68000 code and its dispatch table live in this module.
inline constexpr unsigned MULTIARCADERECOMP_NEOGEO_GAME_MODULE_ABI = 2;

extern "C" {
unsigned multiarcaderecomp_neogeo_game_module_abi();
const char* multiarcaderecomp_neogeo_game_module_id();
unsigned multiarcaderecomp_neogeo_game_module_revision();
const ng_game_t* multiarcaderecomp_neogeo_game_descriptor();
void multiarcaderecomp_neogeo_game_reset();
const ng_func_entry_t* multiarcaderecomp_neogeo_game_recomp_table(std::size_t* count);
const void* multiarcaderecomp_neogeo_game_z80_bios_code();
const void* multiarcaderecomp_neogeo_game_z80_cart_code();
}
