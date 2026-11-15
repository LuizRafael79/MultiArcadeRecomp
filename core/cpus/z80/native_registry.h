#pragma once

namespace multiarcaderecomp::z80 {
struct Z80Code;
}

extern "C" const multiarcaderecomp::z80::Z80Code *multiarcaderecomp_z80_aot_code();
