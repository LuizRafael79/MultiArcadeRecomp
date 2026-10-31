#include "kof98_module_abi.h"

#ifdef MAMERECOMP_KOF98_HAS_Z80_AOT
#include "native_z80.h"
namespace mamerecomp::z80 {
extern const Z80Code kZ80Code_bios;
extern const Z80Code kZ80Code_kof98;
}
#endif

extern "C" {
extern const ng_func_entry_t ng_recomp_table[];
extern const std::size_t ng_recomp_count;

unsigned mamerecomp_neogeo_game_module_abi() {
    return MAMERECOMP_NEOGEO_GAME_MODULE_ABI;
}

const char* mamerecomp_neogeo_game_module_id() {
    return "kof98";
}

unsigned mamerecomp_neogeo_game_module_revision() {
    return 98;
}

const ng_func_entry_t* mamerecomp_neogeo_game_recomp_table(std::size_t* count) {
    if (!count) return nullptr;
    *count = ng_recomp_count;
    return ng_recomp_table;
}

const void* mamerecomp_neogeo_game_z80_bios_code() {
#ifdef MAMERECOMP_KOF98_HAS_Z80_AOT
    return &mamerecomp::z80::kZ80Code_bios;
#else
    return nullptr;
#endif
}

const void* mamerecomp_neogeo_game_z80_cart_code() {
#ifdef MAMERECOMP_KOF98_HAS_Z80_AOT
    return &mamerecomp::z80::kZ80Code_kof98;
#else
    return nullptr;
#endif
}
}
