#pragma once

#include "m6809.hpp"
#include "core/cpus/interpreter_trace.h"

namespace multiarcaderecomp::m6809 {

// Execute one instruction in the shared MC6809/MC6809E fallback. Unsupported
// instructions leave PC unchanged and return NeedsInterpreter to the caller;
// implemented instructions are traced with the reason the AOT path fell back.
BlockExit interpreter_step(State& state, Bus& bus, std::uint64_t stop_cycle,
                           MultiArcadeRecompFallbackReason reason) noexcept;
std::uint32_t service_interrupt(State& state, Bus& bus) noexcept;

} // namespace multiarcaderecomp::m6809
