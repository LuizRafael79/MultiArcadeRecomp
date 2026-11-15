#include <cstddef>

#include <neogeorecomp/neogeo.h>
#include "native_z80.h"
#include "z80_decl.h"

extern "C" {
extern const ng_func_entry_t tmnt_ng_recomp_table[];
extern const size_t tmnt_ng_recomp_count;

const ng_func_entry_t* multiarcaderecomp_konami_tmnt_m68k_table(size_t* count)
{
    if (count) *count = tmnt_ng_recomp_count;
    return tmnt_ng_recomp_table;
}
}

extern "C" const multiarcaderecomp::z80::Z80Code* multiarcaderecomp_konami_tmnt_z80_code()
{
    return &multiarcaderecomp::z80::kZ80Code;
}
