#pragma once

#include "adsp2105.hpp"
#include "core/cpus/interpreter_trace.h"

namespace mamerecomp::adsp2105 {

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept;

// Fast native path for the ADSP's common DO-UNTIL counter loop whose body is
// exactly one NOP. Returns NeedsInterpreter unless the complete loop shape is
// proven from architectural loop/PC-stack state.
BlockExit native_nop_counter_loop(State& state, std::uint16_t pc,
                                  std::uint64_t stop_cycle) noexcept;

// Shared arithmetic helper used by generated native shift blocks.
bool native_shift(State& state, std::uint32_t opcode,
                  std::int8_t shift) noexcept;
bool native_execute_instruction(State& state, Bus& bus,
                                std::uint32_t opcode) noexcept;

} // namespace mamerecomp::adsp2105
