#pragma once

namespace multiarcaderecomp::naomi { struct Sh4Context; }

namespace multiarcaderecomp::cpus::sh4::flycast {

// Execute one SH-4 instruction with semantics adapted from Flycast's LLE
// interpreter. Returns false for an instruction family not ported yet.
bool step(multiarcaderecomp::naomi::Sh4Context& context);

} // namespace multiarcaderecomp::cpus::sh4::flycast
