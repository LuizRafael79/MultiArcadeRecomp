#pragma once

#include "runtime.hpp"
#include "core/cpus/interpreter_trace.h"

namespace mamerecomp::tms34010 {

// Execute one instruction at state.pc, returning NeedsInterpreter when the
// current fallback does not yet implement its opcode.
BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle) noexcept;

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept;

// Classify an AOT miss at the current PC before entering the interpreter.
MamerecompFallbackReason classify_unsupported_opcode(
    std::uint16_t opcode) noexcept;
bool opcode_causes_exception(std::uint16_t opcode) noexcept;

} // namespace mamerecomp::tms34010
