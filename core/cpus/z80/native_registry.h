#pragma once

namespace mamerecomp::z80 {
struct Z80Code;
}

extern "C" const mamerecomp::z80::Z80Code *mamerecomp_z80_aot_code();
