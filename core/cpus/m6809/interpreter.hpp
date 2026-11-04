#pragma once

#include "m6809.hpp"
#include "core/cpus/interpreter_trace.h"

namespace mamerecomp::m6809 {

// Execute one instruction in the shared MC6809/MC6809E fallback. Unsupported
// instructions leave PC unchanged and return NeedsInterpreter to the caller;
// implemented instructions are traced with the reason the AOT path fell back.
BlockExit interpreter_step(State& state, Bus& bus, std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept;
std::uint32_t service_interrupt(State& state, Bus& bus) noexcept;

} // namespace mamerecomp::m6809
