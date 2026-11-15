#include "native_z80.h"
#include "z80_decl.h"

extern "C" const multiarcaderecomp::z80::Z80Code *multiarcaderecomp_z80_aot_code() {
    return &multiarcaderecomp::z80::kZ80Code;
}
