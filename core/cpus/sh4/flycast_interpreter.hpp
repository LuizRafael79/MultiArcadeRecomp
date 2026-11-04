#pragma once

namespace mamerecomp::naomi { struct Sh4Context; }

namespace mamerecomp::cpus::sh4::flycast {

// Execute one SH-4 instruction with semantics adapted from Flycast's LLE
// interpreter. Returns false for an instruction family not ported yet.
bool step(mamerecomp::naomi::Sh4Context& context);

} // namespace mamerecomp::cpus::sh4::flycast
