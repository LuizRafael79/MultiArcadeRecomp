#include "runtime.hpp"

#include "interpreter.hpp"

namespace mamerecomp::adsp2105 {

BlockExit run_until(State& state, Bus& bus, std::uint64_t stop_cycle,
                    MamerecompFallbackReason missing_entry_reason) noexcept {
    while (state.cycles < stop_cycle) {
        (void)service_interrupt(state);
        if (state.halted || state.idle) return BlockExit::Idle;
        const auto pc = static_cast<std::uint16_t>(state.pc & pc_mask);
        const Block block = lookup(pc);
        if (block) {
            const auto result = block(state, bus, stop_cycle);
            if (result == BlockExit::Dispatch) continue;
            if (result == BlockExit::Budget || result == BlockExit::BusFault)
                return result;
            // A block can decline execution when its code has become mutable.
            const auto fallback = bus.is_mutable_program &&
                bus.is_mutable_program(bus.opaque, pc)
                    ? MAMERECOMP_DYNAMIC_CODE
                    : (state.loop == pc ? MAMERECOMP_EXCEPTION_BOUNDARY
                                        : MAMERECOMP_UNSUPPORTED_VARIANT);
            const auto interpreted = interpreter_step(state, bus, stop_cycle, fallback);
            if (interpreted != BlockExit::Dispatch) return interpreted;
            continue;
        }

        const auto interpreted = interpreter_step(
            state, bus, stop_cycle, missing_entry_reason);
        if (interpreted != BlockExit::Dispatch) return interpreted;
    }
    return BlockExit::Budget;
}

} // namespace mamerecomp::adsp2105
