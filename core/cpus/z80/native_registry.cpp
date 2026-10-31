#include "native_z80.h"
#include "z80_decl.h"

extern "C" const mamerecomp::z80::Z80Code *mamerecomp_z80_aot_code() {
    return &mamerecomp::z80::kZ80Code;
}
