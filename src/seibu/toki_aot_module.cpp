#include <cstddef>
#include <neogeorecomp/neogeo.h>
#include "z80_decl.h"

extern "C" {
extern const ng_func_entry_t toki_ng_recomp_table[];
extern const size_t toki_ng_recomp_count;

const ng_func_entry_t* mamerecomp_seibu_toki_m68k_table(size_t* count)
{
    if (count) *count = toki_ng_recomp_count;
    return toki_ng_recomp_table;
}

const mamerecomp::z80::Z80Code* mamerecomp_seibu_toki_z80_code()
{
    return &mamerecomp::z80::kZ80Code_toki;
}
}
