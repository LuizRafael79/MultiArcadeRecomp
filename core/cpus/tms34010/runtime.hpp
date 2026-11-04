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

// Mirror MAME's TMS34010 check_interrupt() at an instruction boundary. The
// TMS bus exposes the standard internal I/O registers at C0000000 in bit
// addresses; pending sources remain latched until software acknowledges them.
inline bool service_interrupt(State& state, Bus& bus) noexcept {
    constexpr std::uint32_t io_base = 0xc0000000u;
    constexpr std::uint32_t hstctlh = io_base + 16u * 16u;
    constexpr std::uint32_t intenable = io_base + 17u * 16u;
    constexpr std::uint32_t intpending = io_base + 18u * 16u;
    constexpr std::uint32_t ie_flag = 1u << 21;
    if (!bus.read16 || !bus.write16) return false;

    std::uint16_t host_control{}, pending{}, enabled{};
    if (bus.read_interrupt_state) {
        bus.read_interrupt_state(bus.opaque, &host_control, &pending, &enabled);
    } else {
        host_control = bus.read16(bus.opaque, hstctlh);
        pending = bus.read16(bus.opaque, intpending);
        enabled = bus.read16(bus.opaque, intenable);
    }
    std::uint32_t vector = 0;
    bool save_state = true;
    if (host_control & 0x0100u) {
        vector = 0xfffffee0u;
        save_state = (host_control & 0x0200u) == 0;
        bus.write16(bus.opaque, hstctlh,
                    static_cast<std::uint16_t>(host_control & ~0x0100u));
    } else if (state.st & ie_flag) {
        const std::uint16_t active = pending & enabled;
        if (active & 0x0200u) vector = 0xfffffec0u;      // host interrupt
        else if (active & 0x0400u) vector = 0xfffffea0u; // display interrupt
        else if (active & 0x0800u) vector = 0xfffffe80u; // window violation
        else if (active & 0x0002u) vector = 0xffffffc0u; // external 1
        else if (active & 0x0004u) vector = 0xffffffa0u; // external 2
    }
    if (!vector) return false;

    if (save_state) {
        state.sp -= 32u;
        bus.write16(bus.opaque, state.sp, static_cast<std::uint16_t>(state.pc));
        bus.write16(bus.opaque, state.sp + 16u,
                    static_cast<std::uint16_t>(state.pc >> 16));
        state.sp -= 32u;
        bus.write16(bus.opaque, state.sp, static_cast<std::uint16_t>(state.st));
        bus.write16(bus.opaque, state.sp + 16u,
                    static_cast<std::uint16_t>(state.st >> 16));
    }
    state.st = 0x10u;
    const std::uint32_t low = bus.read16(bus.opaque, vector);
    const std::uint32_t high = bus.read16(bus.opaque, vector + 16u);
    state.pc = (low | (high << 16)) & 0xfffffff0u;
    state.cycles += 16u;
    return true;
}

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
