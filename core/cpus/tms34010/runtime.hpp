#pragma once

#include "tms34010.hpp"

namespace mamerecomp::tms34010 {

// A fallback step executes one instruction at the current PC and returns
// Dispatch when it advances state. It may return NeedsInterpreter if it
// cannot handle that opcode, or Budget/BusFault to stop the run loop.
using InterpreterStep = BlockExit (*)(State&, Bus&, std::uint64_t stop_cycle);
using UntranslatedOpcodeHook = void (*)(void*, std::uint32_t bit_pc,
                                        std::uint16_t opcode);
using TraceHook = void (*)(void*, std::uint32_t bit_pc,
                           std::uint16_t opcode, std::uint32_t sp,
                           std::uint64_t cycles);

// Run generated blocks until the cycle budget is reached or an operation
// needs a fallback. A missing generated block uses the same fallback path as
// an unsupported instruction inside a generated block.
inline BlockExit run_until(State& state, Bus& bus, std::uint64_t stop_cycle,
                           InterpreterStep interpreter = nullptr,
                           UntranslatedOpcodeHook unhandled = nullptr,
                           void* unhandled_opaque = nullptr,
                           TraceHook trace = nullptr,
                           void* trace_opaque = nullptr) {
    while (state.cycles < stop_cycle) {
        if (trace && bus.read16)
            trace(trace_opaque, state.pc,
                  bus.read16(bus.opaque, state.pc), state.sp, state.cycles);
        BlockExit result = BlockExit::NeedsInterpreter;
        if (const Block block = lookup(state.pc))
            result = block(state, bus, stop_cycle);

        if (result == BlockExit::Dispatch)
            continue;
        if (result != BlockExit::NeedsInterpreter)
            return result;
        if (interpreter) {
            result = interpreter(state, bus, stop_cycle);
            if (result == BlockExit::Dispatch)
                continue;
            if (result != BlockExit::NeedsInterpreter)
                return result;
        }
        if (unhandled && bus.read16)
            unhandled(unhandled_opaque, state.pc,
                      bus.read16(bus.opaque, state.pc));
        if (!interpreter || result == BlockExit::NeedsInterpreter)
            return BlockExit::NeedsInterpreter;
    }
    return BlockExit::Budget;
}

} // namespace mamerecomp::tms34010
