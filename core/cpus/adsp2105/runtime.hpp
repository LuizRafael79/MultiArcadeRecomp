#pragma once

#include "adsp2105.hpp"
#include "core/cpus/interpreter_trace.h"

namespace mamerecomp::adsp2105 {

// One interpreted instruction. Unsupported operations leave PC unchanged and
// return NeedsInterpreter; callers log that opcode before stopping/falling
// back again, so uncovered ADSP code is never silently skipped.
BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept;

// Service the highest-priority pending ADSP-2105 interrupt. Returns true
// when an interrupt vector was accepted at this instruction boundary.
bool service_interrupt(State& state) noexcept;

// Execute shared AOT blocks, sending missing or unsupported entries through
// the interpreter while recording exact fallback reasons in the common trace.
BlockExit run_until(State& state, Bus& bus, std::uint64_t stop_cycle,
                    MamerecompFallbackReason missing_entry_reason =
                        MAMERECOMP_UNCOVERED_ENTRY,
                    bool interpreter_fallback = true,
                    bool native_enabled = true) noexcept;

} // namespace mamerecomp::adsp2105
