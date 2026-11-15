#include "runtime.hpp"

#include "interpreter.hpp"

namespace multiarcaderecomp::adsp2105 {

BlockExit run_until(State& state, Bus& bus, std::uint64_t stop_cycle,
                    MultiArcadeRecompFallbackReason missing_entry_reason,
                    bool interpreter_fallback,
                    bool native_enabled) noexcept {
    while (state.cycles < stop_cycle) {
        (void)service_interrupt(state);
        if (state.halted || state.idle) {
            // SPORT1 autobuffer DMA and the timer continue while the ADSP core
            // executes IDLE. Consume the caller's time slice through those
            // peripherals instead of freezing both the DSP clock and audio.
            advance_clock(state, bus, stop_cycle - state.cycles);
            return BlockExit::Idle;
        }
        const auto pc = static_cast<std::uint16_t>(state.pc & pc_mask);
        const std::uint32_t opcode = bus.read_program24
            ? bus.read_program24(bus.opaque, pc) & opcode_mask : 0;
        const Block block = native_enabled ? lookup(pc, opcode) : nullptr;
        if (block) {
            const auto result = block(state, bus, stop_cycle);
            if (result == BlockExit::Dispatch) continue;
            if (result == BlockExit::Budget || result == BlockExit::BusFault)
                return result;
            if (!interpreter_fallback) return result;
            // A block can decline execution when its code has become mutable.
            const auto fallback = bus.is_mutable_program &&
                bus.is_mutable_program(bus.opaque, pc)
                    ? MULTIARCADERECOMP_DYNAMIC_CODE
                    : (state.loop == pc ? MULTIARCADERECOMP_EXCEPTION_BOUNDARY
                                        : MULTIARCADERECOMP_UNSUPPORTED_VARIANT);
            const auto interpreted = interpreter_step(state, bus, stop_cycle, fallback);
            if (interpreted != BlockExit::Dispatch) return interpreted;
            continue;
        }

        if (!interpreter_fallback) return BlockExit::NeedsInterpreter;
        const auto fallback_reason = !native_enabled ? MULTIARCADERECOMP_UNSUPPORTED_VARIANT :
            bus.is_mutable_program &&
            bus.is_mutable_program(bus.opaque, pc)
                ? MULTIARCADERECOMP_DYNAMIC_CODE
                : (lookup(pc) ? MULTIARCADERECOMP_UNSUPPORTED_VARIANT
                              : missing_entry_reason);
        const auto interpreted = interpreter_step(
            state, bus, stop_cycle, fallback_reason);
        if (interpreted != BlockExit::Dispatch) return interpreted;
    }
    return BlockExit::Budget;
}

} // namespace multiarcaderecomp::adsp2105
