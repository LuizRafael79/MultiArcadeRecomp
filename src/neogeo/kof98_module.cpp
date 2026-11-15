#include "kof98_game.h"
#include "kof98_module_abi.h"

#ifdef MULTIARCADERECOMP_KOF98_HAS_Z80_AOT
#include "native_z80.h"
namespace multiarcaderecomp::z80 {
extern const Z80Code kZ80Code_bios;
extern const Z80Code kZ80Code_kof98;
}
#endif

extern "C" {
extern const ng_func_entry_t ng_recomp_table[];
extern const std::size_t ng_recomp_count;

unsigned multiarcaderecomp_neogeo_game_module_abi() {
    return MULTIARCADERECOMP_NEOGEO_GAME_MODULE_ABI;
}

const char* multiarcaderecomp_neogeo_game_module_id() {
    return "kof98";
}

unsigned multiarcaderecomp_neogeo_game_module_revision() {
    return 98;
}

const ng_game_t* multiarcaderecomp_neogeo_game_descriptor() {
    return &multiarcaderecomp_kof98_game;
}

void multiarcaderecomp_neogeo_game_reset() {
    multiarcaderecomp_kof98_prot_reset();
}

const ng_func_entry_t* multiarcaderecomp_neogeo_game_recomp_table(std::size_t* count) {
    if (!count) return nullptr;
    *count = ng_recomp_count;
    return ng_recomp_table;
}

const void* multiarcaderecomp_neogeo_game_z80_bios_code() {
#ifdef MULTIARCADERECOMP_KOF98_HAS_Z80_AOT
    return &multiarcaderecomp::z80::kZ80Code_bios;
#else
    return nullptr;
#endif
}

const void* multiarcaderecomp_neogeo_game_z80_cart_code() {
#ifdef MULTIARCADERECOMP_KOF98_HAS_Z80_AOT
    return &multiarcaderecomp::z80::kZ80Code_kof98;
#else
    return nullptr;
#endif
}
}
