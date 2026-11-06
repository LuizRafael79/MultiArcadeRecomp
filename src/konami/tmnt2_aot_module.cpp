#include <cstddef>

#include <neogeorecomp/neogeo.h>
#include "native_z80.h"
#include "z80_decl.h"

#ifndef MAMERECOMP_KONAMI_TMNT2_GAME_ID
#error "The TMNT2-family AOT module needs a game identifier"
#endif

#define TMNT2_JOIN_INNER(a, b) a##b
#define TMNT2_JOIN(a, b) TMNT2_JOIN_INNER(a, b)
#define TMNT2_M68K_TABLE TMNT2_JOIN(TMNT2_JOIN(MAMERECOMP_KONAMI_TMNT2_GAME_ID, _), ng_recomp_table)
#define TMNT2_M68K_COUNT TMNT2_JOIN(TMNT2_JOIN(MAMERECOMP_KONAMI_TMNT2_GAME_ID, _), ng_recomp_count)
#define TMNT2_Z80_CODE TMNT2_JOIN(kZ80Code_, MAMERECOMP_KONAMI_TMNT2_GAME_ID)

extern "C" {
extern const ng_func_entry_t TMNT2_M68K_TABLE[];
extern const size_t TMNT2_M68K_COUNT;

const ng_func_entry_t* mamerecomp_konami_tmnt2_m68k_table(size_t* count)
{
    if (count) *count = TMNT2_M68K_COUNT;
    return TMNT2_M68K_TABLE;
}
}

extern "C" const mamerecomp::z80::Z80Code* mamerecomp_konami_tmnt2_z80_code()
{
    return &mamerecomp::z80::TMNT2_Z80_CODE;
}
