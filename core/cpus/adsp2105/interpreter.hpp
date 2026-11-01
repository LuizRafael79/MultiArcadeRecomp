#pragma once

#include "adsp2105.hpp"
#include "core/cpus/interpreter_trace.h"

namespace mamerecomp::adsp2105 {

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept;

} // namespace mamerecomp::adsp2105
