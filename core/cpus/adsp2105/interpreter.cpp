#include "interpreter.hpp"

#include "dag.hpp"

namespace mamerecomp::adsp2105 {
namespace {
constexpr std::uint16_t kPcEmpty = 0x01;
constexpr std::uint16_t kPcOverflow = 0x02;
constexpr std::uint16_t kCounterEmpty = 0x04;
constexpr std::uint16_t kCounterOverflow = 0x08;
constexpr std::uint16_t kStatusEmpty = 0x10;
constexpr std::uint16_t kStatusOverflow = 0x20;
constexpr std::uint16_t kLoopEmpty = 0x40;
constexpr std::uint16_t kLoopOverflow = 0x80;
constexpr std::uint16_t kAstatS = 0x10;
constexpr std::uint16_t kAstatC = 0x08;
constexpr std::uint16_t kAstatV = 0x04;
constexpr std::uint16_t kAstatN = 0x02;
constexpr std::uint16_t kAstatZ = 0x01;
constexpr std::uint16_t kMstatBank = 0x01;
constexpr std::uint16_t kMstatReverse = 0x02;
constexpr std::uint16_t kMstatStickyV = 0x04;
constexpr std::uint16_t kMstatSaturate = 0x08;
constexpr std::uint16_t kMstatInteger = 0x10;
constexpr std::uint16_t kMstatTimer = 0x20;
constexpr std::uint16_t kMstatGoMode = 0x40;

void update_mstat(State& state, std::uint16_t previous) noexcept {
    if ((state.mstat ^ previous) & kMstatBank) {
        auto active = state.core;
        state.core = state.alternate;
        state.alternate = active;
    }
}

std::int32_t read_reg0(const State& state, unsigned reg) noexcept {
    const auto& c = state.core;
    switch (reg & 15) {
    case 0: return c.ax[0]; case 1: return c.ax[1];
    case 2: return c.mx[0]; case 3: return c.mx[1];
    case 4: return c.ay[0]; case 5: return c.ay[1];
    case 6: return c.my[0]; case 7: return c.my[1];
    case 8: return c.si; case 9: return c.se;
    case 10: return c.ar;
    case 11: return static_cast<std::int16_t>(c.mr);
    case 12: return static_cast<std::int16_t>(c.mr >> 16);
    case 13: return static_cast<std::int16_t>(c.mr >> 32);
    case 14: return static_cast<std::int16_t>(c.sr[0]);
    default: return static_cast<std::int16_t>(c.sr[1]);
    }
}

void write_reg0(State& state, unsigned reg, std::int32_t value) noexcept {
    auto& c = state.core;
    const auto word = static_cast<std::uint16_t>(value);
    switch (reg & 15) {
    case 0: c.ax[0] = word; break; case 1: c.ax[1] = word; break;
    case 2: c.mx[0] = word; break; case 3: c.mx[1] = word; break;
    case 4: c.ay[0] = word; break; case 5: c.ay[1] = word; break;
    case 6: c.my[0] = word; break; case 7: c.my[1] = word; break;
    case 8: c.si = word; break;
    case 9: c.se = static_cast<std::int8_t>(value); break;
    case 10: c.ar = word; break;
    case 11:
        c.mr = (c.mr & ~std::int64_t{0xffff}) | word;
        break;
    case 12:
        c.mr = (c.mr & ~std::int64_t{0xffff0000}) |
               (static_cast<std::int64_t>(word) << 16);
        c.mr = (c.mr & 0xffffffffll) |
               (value < 0 ? (0xffll << 32) : 0ll);
        break;
    case 13: {
        const auto high = static_cast<std::int8_t>(value);
        c.mr = (c.mr & 0xffffffffll) |
               (static_cast<std::int64_t>(high) << 32);
        break;
    }
    case 14: c.sr[0] = word; break;
    case 15: c.sr[1] = word; break;
    }
}

std::int32_t read_reg12(const State& state, unsigned group,
                        unsigned reg) noexcept {
    const unsigned bank = group == 1 ? 0 : 4;
    const unsigned index = reg & 15;
    if (index < 4) return state.i[bank + index];
    if (index < 8) return state.m[bank + index - 4];
    return state.l[bank + ((index - 8) & 3)];
}

void write_reg12(State& state, unsigned group, unsigned reg,
                 std::int32_t value) noexcept {
    const unsigned bank = group == 1 ? 0 : 4;
    const unsigned index = reg & 15;
    if (index < 4) dag_write_i(state, bank + index,
                               static_cast<std::uint16_t>(value));
    else if (index < 8) {
        const auto raw = static_cast<std::uint16_t>(value) & 0x3fff;
        state.m[bank + index - 4] = static_cast<std::int16_t>(
            (raw & 0x2000) ? (raw | 0xc000) : raw);
    }
    else dag_write_l(state, bank + ((index - 8) & 3),
                     static_cast<std::uint16_t>(value));
}

std::int32_t read_reg3(State& state, Bus& bus, unsigned reg) noexcept {
    switch (reg & 15) {
    case 0: return state.astat;
    case 1: return state.mstat;
    case 2: return state.sstat;
    case 3: return state.imask;
    case 4: return state.icntl;
    case 5: return state.counter;
    case 6: return state.core.sb;
    case 7: return state.px;
    case 8: return bus.sport_rx ? bus.sport_rx(bus.opaque, 0) : 0;
    case 10: return bus.sport_rx ? bus.sport_rx(bus.opaque, 1) : 0;
    default: return 0;
    }
}

void write_reg3(State& state, Bus& bus, unsigned reg,
                std::int32_t value) noexcept {
    switch (reg & 15) {
    case 0: state.astat = value & 0xff; break;
    case 1: {
        const auto previous = state.mstat;
        state.mstat = value & 0x7f;
        update_mstat(state, previous);
        break;
    }
    case 3: state.imask = value & 0x3f; break;
    case 4: state.icntl = value & 0x1f; break;
    case 5:
        if (state.counter_stack_pointer < state.counter_stack.size())
            state.counter_stack[state.counter_stack_pointer++] = state.counter;
        state.counter = value & 0x3fff;
        break;
    case 6: state.core.sb = static_cast<std::int8_t>((value ^ 0x10) - 0x10); break;
    case 7: state.px = value; break;
    case 9: if (bus.sport_tx) bus.sport_tx(bus.opaque, 0, value); break;
    case 11: if (bus.sport_tx) bus.sport_tx(bus.opaque, 1, value); break;
    default: break;
    }
}

std::uint16_t alu_x(const State& state, unsigned reg) noexcept {
    const auto& c = state.core;
    switch (reg & 7) {
    case 0: return c.ax[0]; case 1: return c.ax[1];
    case 2: return c.ar;
    case 3: return static_cast<std::uint16_t>(c.mr);
    case 4: return static_cast<std::uint16_t>(c.mr >> 16);
    case 5: return static_cast<std::uint16_t>(c.mr >> 32);
    case 6: return c.sr[0]; default: return c.sr[1];
    }
}

std::uint16_t alu_y(const State& state, unsigned reg) noexcept {
    switch (reg & 3) {
    case 0: return state.core.ay[0];
    case 1: return state.core.ay[1];
    case 2: return state.core.af;
    default: return 0;
    }
}

std::uint16_t alu_execute(State& state, std::uint32_t opcode) noexcept {
    const unsigned xreg = (opcode >> 8) & 7;
    const unsigned yreg = (opcode >> 11) & 3;
    const auto x = static_cast<std::uint32_t>(alu_x(state, xreg));
    const auto y = static_cast<std::uint32_t>(alu_y(state, yreg));
    const unsigned operation = (opcode >> 13) & 15;
    std::uint32_t result = 0;
    std::uint16_t flags = state.astat;
    const auto clear = static_cast<std::uint16_t>(~(
        kAstatC | kAstatN | kAstatZ |
        ((state.mstat & kMstatStickyV) ? 0 : kAstatV)));
    auto set_nz = [&] {
        flags &= clear;
        if ((result & 0xffff) == 0) flags |= kAstatZ;
        if (result & 0x8000) flags |= kAstatN;
    };
    auto set_add = [&](std::uint32_t a, std::uint32_t b) {
        set_nz();
        if ((~(a ^ b) & (a ^ result) & 0x8000) != 0) flags |= kAstatV;
        if (result & 0x10000) flags |= kAstatC;
    };
    auto set_sub = [&](std::uint32_t a, std::uint32_t b) {
        set_nz();
        if (((a ^ b) & (a ^ result) & 0x8000) != 0) flags |= kAstatV;
        if ((~result) & 0x10000) flags |= kAstatC;
    };
    switch (operation) {
    case 0: result = y; set_nz(); break;
    case 1: result = y + 1; set_nz(); if (y == 0x7fff) flags |= kAstatV; else if (y == 0xffff) flags |= kAstatC; break;
    case 2: result = x + y + ((state.astat & kAstatC) ? 1u : 0u); set_add(x, y); break;
    case 3: result = x + y; set_add(x, y); break;
    case 4: result = y ^ 0xffff; set_nz(); break;
    case 5: result = 0u - y; set_nz(); if (y == 0x8000) flags |= kAstatV; if (y == 0) flags |= kAstatC; break;
    case 6: result = x - y + ((state.astat & kAstatC) ? 1u : 0u) - 1u; set_sub(x, y); break;
    case 7: result = x - y; set_sub(x, y); break;
    case 8: result = y - 1; set_nz(); if (y == 0x8000) flags |= kAstatV; else if (!y) flags |= kAstatC; break;
    case 9: result = y - x; set_sub(y, x); break;
    case 10: result = y - x + ((state.astat & kAstatC) ? 1u : 0u) - 1u; set_sub(y, x); break;
    case 11: result = x ^ 0xffff; set_nz(); break;
    case 12: result = x & y; set_nz(); break;
    case 13: result = x | y; set_nz(); break;
    case 14: result = x ^ y; set_nz(); break;
    case 15:
        result = (x & 0x8000) ? 0u - x : x;
        flags &= static_cast<std::uint16_t>(~(kAstatS | kAstatN | kAstatZ |
                                              kAstatV));
        if ((result & 0xffff) == 0) flags |= kAstatZ;
        if (x == 0x8000) flags |= kAstatN | kAstatV;
        if (x & 0x8000) flags |= kAstatS;
        break;
    }
    if ((state.mstat & kMstatSaturate) && (flags & kAstatV))
        result = (flags & kAstatC) ? 0x8000 : 0x7fff;
    state.astat = flags;
    return static_cast<std::uint16_t>(result);
}

void mac_to_mr(State& state, std::uint32_t opcode) noexcept {
    const unsigned operation = (opcode >> 13) & 15;
    if (!operation) return;
    const unsigned xr = (opcode >> 8) & 7;
    const unsigned yr = (opcode >> 11) & 3;
    const auto xbits = alu_x(state, xr);
    const auto ybits = alu_y(state, yr);
    std::int64_t x = (operation & 2) ? xbits : static_cast<std::int16_t>(xbits);
    std::int64_t y = (operation & 1) ? ybits : static_cast<std::int16_t>(ybits);
    std::int64_t product = x * y;
    if (!(state.mstat & kMstatInteger)) product <<= 1;
    std::int64_t result = product;
    if (operation <= 3) {
        if (operation == 2) result += state.core.mr;
        if (operation == 3) result = state.core.mr - product;
        if (operation == 1 || operation == 2 || operation == 3) {
            const auto low = static_cast<std::uint16_t>(product);
            result += 0x8000;
            if (low == 0x8000) result &= ~std::int64_t{0x10000};
        }
    } else if (operation >= 8 && operation <= 11) {
        result += state.core.mr;
    } else if (operation >= 12) {
        result = state.core.mr - product;
    }
    constexpr std::int64_t mask40 = (std::int64_t{1} << 40) - 1;
    result &= mask40;
    if (result & (std::int64_t{1} << 39)) result |= ~mask40;
    const auto extension = static_cast<unsigned>((result >> 31) & 0x1ff);
    state.astat &= static_cast<std::uint16_t>(~0x40u);
    if (extension != 0 && extension != 0x1ff) state.astat |= 0x40;
    state.core.mr = result;
}

bool condition(State& state, unsigned code) noexcept {
    const bool z = (state.astat & kAstatZ) != 0;
    const bool n = (state.astat & kAstatN) != 0;
    const bool v = (state.astat & kAstatV) != 0;
    const bool c = (state.astat & kAstatC) != 0;
    const bool s = (state.astat & kAstatS) != 0;
    const bool mv = (state.astat & 0x40) != 0;
    switch (code & 15) {
    case 0: return z;
    case 1: return !z;
    case 2: return !((n ^ v) || z);
    case 3: return (n ^ v) || z;
    case 4: return n ^ v;
    case 5: return !(n ^ v);
    case 6: return v;
    case 7: return !v;
    case 8: return c;
    case 9: return !c;
    case 10: return s;
    case 11: return !s;
    case 12: return mv;
    case 13: return !mv;
    case 14:
        if (static_cast<std::int32_t>(--state.counter) > 0) return true;
        if (state.counter_stack_pointer) {
            --state.counter_stack_pointer;
            if (!state.counter_stack_pointer) state.sstat |= kCounterEmpty;
        }
        state.counter = state.counter_stack[state.counter_stack_pointer];
        return false;
    default: return true;
    }
}

std::uint16_t pc_stack_top(const State& state) noexcept {
    return state.pc_stack_pointer
        ? state.pc_stack[state.pc_stack_pointer - 1] : state.pc_stack[0];
}

void push_pc(State& state) noexcept {
    if (state.pc_stack_pointer < state.pc_stack.size()) {
        state.pc_stack[state.pc_stack_pointer++] = state.pc & pc_mask;
        state.sstat &= static_cast<std::uint16_t>(~kPcEmpty);
    } else {
        state.sstat |= kPcOverflow;
    }
}

std::uint16_t pop_pc_value(State& state) noexcept {
    if (state.pc_stack_pointer) {
        --state.pc_stack_pointer;
        if (!state.pc_stack_pointer) state.sstat |= kPcEmpty;
    }
    return state.pc_stack[state.pc_stack_pointer];
}

void pop_pc(State& state) noexcept {
    state.pc = pop_pc_value(state) & pc_mask;
}

void push_loop(State& state, std::uint32_t value) noexcept {
    if (state.loop_stack_pointer < state.loop_stack.size()) {
        state.loop_stack[state.loop_stack_pointer++] = value & 0x3ffff;
        state.loop = static_cast<std::uint16_t>((value >> 4) & pc_mask);
        state.loop_condition = static_cast<std::uint16_t>(value & 15);
        state.sstat &= static_cast<std::uint16_t>(~kLoopEmpty);
    } else {
        state.sstat |= kLoopOverflow;
    }
}

void pop_loop(State& state) noexcept {
    if (state.loop_stack_pointer) {
        --state.loop_stack_pointer;
        if (!state.loop_stack_pointer) {
            state.loop = 0xffff;
            state.loop_condition = 0;
            state.sstat |= kLoopEmpty;
        } else {
            const auto value = state.loop_stack[state.loop_stack_pointer - 1];
            state.loop = static_cast<std::uint16_t>((value >> 4) & pc_mask);
            state.loop_condition = value & 15;
        }
    }
}

void push_counter(State& state) noexcept {
    if (state.counter_stack_pointer < state.counter_stack.size()) {
        state.counter_stack[state.counter_stack_pointer++] = state.counter;
        state.sstat &= static_cast<std::uint16_t>(~kCounterEmpty);
    } else {
        state.sstat |= kCounterOverflow;
    }
}

void pop_counter(State& state) noexcept {
    if (state.counter_stack_pointer) {
        --state.counter_stack_pointer;
        if (!state.counter_stack_pointer) state.sstat |= kCounterEmpty;
    }
    state.counter = state.counter_stack[state.counter_stack_pointer];
}

void push_status(State& state) noexcept {
    if (state.status_stack_pointer < state.status_stack.size()) {
        auto& saved = state.status_stack[state.status_stack_pointer++];
        saved = {state.mstat, state.imask, state.astat};
        state.sstat &= static_cast<std::uint16_t>(~kStatusEmpty);
    } else {
        state.sstat |= kStatusOverflow;
    }
}

void pop_status(State& state) noexcept {
    if (state.status_stack_pointer) {
        --state.status_stack_pointer;
        if (!state.status_stack_pointer) state.sstat |= kStatusEmpty;
    }
    const auto& saved = state.status_stack[state.status_stack_pointer];
    const auto previous_mstat = state.mstat;
    state.mstat = saved[0];
    state.imask = saved[1];
    state.astat = saved[2];
    update_mstat(state, previous_mstat);
}

void advance_pc_for_loop(State& state) noexcept {
    if (state.pc != state.loop) {
        state.pc = (state.pc + 1) & pc_mask;
    } else if (condition(state, state.loop_condition)) {
        state.pc = pc_stack_top(state);
    } else {
        pop_loop(state);
        (void)pop_pc_value(state);
        state.pc = (state.pc + 1) & pc_mask;
    }
}

const char* instruction_name(std::uint32_t opcode) noexcept {
    const unsigned group = (opcode >> 16) & 0xff;
    switch (group) {
    case 0x00: return "NOP";
    case 0x02: return (opcode & 0x8000) ? "IDLE" : "FLAG_CONTROL";
    case 0x03: return "JUMP_FLAG_IN";
    case 0x04: return "STACK_CONTROL";
    case 0x09: return "MODIFY_ADDRESS";
    case 0x0a: return "RETURN_CONDITIONAL";
    case 0x0b: return "JUMP_INDIRECT";
    case 0x0c: return "MODE_CONTROL";
    case 0x0d: return "INTERNAL_DATA_MOVE";
    case 0x14: case 0x15: case 0x16: case 0x17: return "DO_UNTIL";
    case 0x18: case 0x19: case 0x1a: case 0x1b: return "JUMP_IMMEDIATE";
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: return "CALL_IMMEDIATE";
    default: return "UNSUPPORTED";
    }
}
} // namespace

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept {
    if (!bus.read_program24) return BlockExit::BusFault;
    if (state.cycles >= stop_cycle) return BlockExit::Budget;

    const std::uint16_t pc = state.pc & pc_mask;
    const std::uint32_t opcode = bus.read_program24(bus.opaque, pc) & opcode_mask;
    const unsigned group = (opcode >> 16) & 0xff;
    const std::uint64_t cycles_before = state.cycles;
    state.previous_pc = pc;
    advance_pc_for_loop(state);

    switch (group) {
    case 0x00: // NOP
        break;
    case 0x02:
        if (opcode & 0x8000) {
            state.idle = true;
        } else if (condition(state, opcode & 15)) {
            if (opcode & 0x20) state.flag_out = 0;
            if (opcode & 0x10) state.flag_out ^= 1;
            if (opcode & 0x80) state.fl[0] = 0;
            if (opcode & 0x40) state.fl[0] ^= 1;
            if (opcode & 0x200) state.fl[1] = 0;
            if (opcode & 0x100) state.fl[1] ^= 1;
            if (opcode & 0x800) state.fl[2] = 0;
            if (opcode & 0x400) state.fl[2] ^= 1;
        }
        break;
    case 0x03:
        if (((opcode >> 1) & 1) ? state.flag_in : !state.flag_in) {
            if (opcode & 1) push_pc(state);
            state.pc = static_cast<std::uint16_t>(
                (((opcode >> 4) & 0x0fff) | (((opcode >> 2) & 3) << 12)) &
                pc_mask);
        }
        break;
    case 0x04:
        if (opcode & 0x10) (void)pop_pc_value(state);
        if (opcode & 0x08) pop_loop(state);
        if (opcode & 0x04) pop_counter(state);
        if (opcode & 0x02) {
            if (opcode & 0x01) pop_status(state);
            else push_status(state);
        }
        break;
    case 0x09: {
        const unsigned ireg = (opcode >> 2) & 7;
        const unsigned mreg = (ireg & 4) | (opcode & 3);
        (void)dag_advance(state, ireg, mreg);
        break;
    }
    case 0x0a:
        if (condition(state, opcode & 15)) {
            pop_pc(state);
            if (opcode & 0x10) pop_status(state);
        }
        break;
    case 0x0b:
        if (condition(state, opcode & 15)) {
            if (opcode & 0x10) push_pc(state);
            state.pc = state.i[4 + ((opcode >> 6) & 3)] & pc_mask;
        }
        break;
    case 0x0c:
    {
        const auto previous_mstat = state.mstat;
        if (opcode & 0x20) state.mstat = (opcode & 0x10)
            ? state.mstat | kMstatBank : state.mstat & ~kMstatBank;
        if (opcode & 0x80) state.mstat = (opcode & 0x40)
            ? state.mstat | kMstatReverse : state.mstat & ~kMstatReverse;
        if (opcode & 0x200) state.mstat = (opcode & 0x100)
            ? state.mstat | kMstatStickyV : state.mstat & ~kMstatStickyV;
        if (opcode & 0x800) state.mstat = (opcode & 0x400)
            ? state.mstat | kMstatSaturate : state.mstat & ~kMstatSaturate;
        // ADSP-2105 implements the later-family mode bits used here.
        if (opcode & 0x08) state.mstat = (opcode & 0x04)
            ? state.mstat | kMstatGoMode : state.mstat & ~kMstatGoMode;
        if (opcode & 0x2000) state.mstat = (opcode & 0x1000)
            ? state.mstat | kMstatInteger : state.mstat & ~kMstatInteger;
        if (opcode & 0x8000) state.mstat = (opcode & 0x4000)
            ? state.mstat | kMstatTimer : state.mstat & ~kMstatTimer;
        update_mstat(state, previous_mstat);
        break;
    }
    case 0x0d: {
        const unsigned dst_group = (opcode >> 10) & 3;
        const unsigned src_group = (opcode >> 8) & 3;
        const unsigned dst = (opcode >> 4) & 15;
        const unsigned src = opcode & 15;
        std::int32_t value = 0;
        switch (src_group) {
        case 0: value = read_reg0(state, src); break;
        case 1: case 2: value = read_reg12(state, src_group, src); break;
        case 3: value = read_reg3(state, bus, src); break;
        }
        switch (dst_group) {
        case 0: write_reg0(state, dst, value); break;
        case 1: case 2: write_reg12(state, dst_group, dst, value); break;
        case 3: write_reg3(state, bus, dst, value); break;
        }
        break;
    }
    case 0x0f: {
        const unsigned xreg = (opcode >> 8) & 7;
        const unsigned shift_op = (opcode >> 11) & 15;
        const int shift = static_cast<std::int8_t>(opcode);
        if (shift_op > 7) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        std::uint32_t x = alu_x(state, xreg);
        if (shift_op < 2 || (shift_op >= 4 && shift_op < 6)) x <<= 16;
        std::uint32_t result = 0;
        if (shift >= 0) result = shift < 32 ? x << shift : 0;
        else if (shift_op >= 4) {
            const auto signed_x = static_cast<std::int32_t>(x);
            result = shift > -32 ? static_cast<std::uint32_t>(signed_x >> -shift)
                                 : static_cast<std::uint32_t>(signed_x >> 31);
        } else result = shift > -32 ? x >> -shift : 0;
        std::uint32_t sr = (static_cast<std::uint32_t>(state.core.sr[1]) << 16) |
                           state.core.sr[0];
        if (shift_op & 1) result |= sr;
        state.core.sr[0] = static_cast<std::uint16_t>(result);
        state.core.sr[1] = static_cast<std::uint16_t>(result >> 16);
        break;
    }
    case 0x22: case 0x23:
        if (condition(state, opcode & 15))
            state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        break;
    case 0x20: case 0x21:
        if (condition(state, opcode & 15)) mac_to_mr(state, opcode);
        break;
    case 0x60: case 0x61: {
        mac_to_mr(state, opcode);
        std::uint16_t value = 0;
        if (!dag1_read(state, bus, opcode, value)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x62: case 0x63: {
        state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        std::uint16_t value = 0;
        if (!dag1_read(state, bus, opcode, value)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x26: case 0x27:
        if (condition(state, opcode & 15))
            state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        break;
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3a: case 0x3b:
    case 0x3c: case 0x3d: case 0x3e: case 0x3f: {
        const std::uint32_t raw = (opcode >> 4) & 0x3fff;
        const auto immediate = static_cast<std::int32_t>(
            (raw & 0x2000) ? (raw | 0xffffc000u) : raw);
        const unsigned destination = opcode & 15;
        if (group <= 0x33) write_reg0(state, destination, immediate);
        else if (group <= 0x37) write_reg12(state, 1, destination, immediate);
        else if (group <= 0x3b) write_reg12(state, 2, destination, immediate);
        else write_reg3(state, bus, destination, immediate);
        break;
    }
    case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4a: case 0x4b:
    case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        // MAME: load a 16-bit immediate into a group-0 core register.
        write_reg0(state, opcode & 15,
                   static_cast<std::int16_t>((opcode >> 4) & 0xffff));
        break;
    case 0x50: case 0x51: {
        // MAC function 0 is the defined no-op form used by the DCS bootstrap;
        // the instruction still performs its program-space DAG read.
        if (((opcode >> 13) & 15) != 0) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        std::uint16_t value = 0;
        if (!dag2_program_read(state, bus, opcode, value)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x58: case 0x59:
        // MAC function zero is a no-op; this variant performs a DAG2 program
        // write from the selected core register and PX byte first.
        if (((opcode >> 13) & 15) != 0) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        if (!dag2_program_write(state, bus, opcode,
                                static_cast<std::uint16_t>(
                                    read_reg0(state, (opcode >> 4) & 15)))) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        break;
    case 0x70: case 0x71: {
        if (((opcode >> 13) & 15) != 0) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        std::uint16_t value = 0;
        if (!dag2_read(state, bus, opcode, value)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x78: case 0x79:
        if (((opcode >> 13) & 15) != 0) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        if (!dag2_write(state, bus, opcode,
                        static_cast<std::uint16_t>(
                            read_reg0(state, (opcode >> 4) & 15)))) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        break;
    case 0x68: case 0x69:
        // MAC function zero is a no-op; store the selected core register
        // through DAG1 (including MSTAT bit-reversed addressing).
        if (((opcode >> 13) & 15) != 0) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        if (!dag1_write(state, bus, opcode,
                        static_cast<std::uint16_t>(
                            read_reg0(state, (opcode >> 4) & 15)))) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        break;
    case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8a: case 0x8b:
    case 0x8c: case 0x8d: case 0x8e: case 0x8f: {
        if (!bus.read_data16) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        const unsigned destination_group = (group - 0x80) >> 2;
        const unsigned destination = opcode & 15;
        const auto value = bus.read_data16(bus.opaque,
            static_cast<std::uint16_t>((opcode >> 4) & 0x3fff));
        if (destination_group == 0) write_reg0(state, destination, value);
        else if (destination_group == 1 || destination_group == 2)
            write_reg12(state, destination_group, destination, value);
        else write_reg3(state, bus, destination, value);
        break;
    }
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9a: case 0x9b:
    case 0x9c: case 0x9d: case 0x9e: case 0x9f: {
        const unsigned source_group = (group - 0x90) >> 2;
        const unsigned source = opcode & 15;
        std::int32_t value = 0;
        if (source_group == 0) value = read_reg0(state, source);
        else if (source_group == 1 || source_group == 2)
            value = read_reg12(state, source_group, source);
        else value = read_reg3(state, bus, source);
        if (!bus.write_data16) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        bus.write_data16(bus.opaque,
            static_cast<std::uint16_t>((opcode >> 4) & 0x3fff),
            static_cast<std::uint16_t>(value));
        break;
    }
    case 0x14: case 0x15: case 0x16: case 0x17:
        push_loop(state, opcode & 0x3ffff);
        push_pc(state);
        break;
    case 0x18: case 0x19: case 0x1a: case 0x1b:
        if (condition(state, opcode & 15))
            state.pc = static_cast<std::uint16_t>((opcode >> 4) & pc_mask);
        break;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        if (condition(state, opcode & 15)) {
            push_pc(state);
            state.pc = static_cast<std::uint16_t>((opcode >> 4) & pc_mask);
        }
        break;
    default:
        state.pc = pc;
        mamerecomp_interpreter_trace_step("ADSP-2105", "MameRecomp-interpreter",
            group == 0x01 ? MAMERECOMP_UNSUPPORTED_VARIANT :
                            MAMERECOMP_UNSUPPORTED_OPCODE,
            pc, opcode, 24, cycles_before, state.cycles, pc,
            instruction_name(opcode));
        return BlockExit::NeedsInterpreter;
    }

    ++state.cycles;
    ++state.interpreted_instructions;
    mamerecomp_interpreter_trace_step("ADSP-2105", "MameRecomp-interpreter",
        reason, pc, opcode, 24, cycles_before, state.cycles, state.pc,
        instruction_name(opcode));
    return state.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
}

bool service_interrupt(State& state) noexcept {
    // ADSP-2105 uses the ADSP-2101 interrupt priority/vector map. IRQ2 is the
    // DCS host-command input and is priority 0, vector 0x04. SPORT and timer
    // sources share this map and are modeled here for later DCS timing work.
    struct Source { unsigned line; unsigned priority; bool edge; };
    constexpr Source priority[] = {
        {2, 0, false}, // IRQ2 / host command
        {4, 1, true},  // SPORT0 transmit
        {3, 2, true},  // SPORT0 receive
        {1, 3, false}, // IRQ1 / SPORT1 transmit
        {0, 4, false}, // IRQ0 / SPORT1 receive
        {5, 5, true},  // timer
    };
    for (const auto source : priority) {
        const bool latched = source.edge ||
            (source.line == 2 && (state.icntl & 0x04)) ||
            (source.line == 1 && (state.icntl & 0x02)) ||
            (source.line == 0 && (state.icntl & 0x01));
        const bool asserted = latched ? state.irq_latched[source.line]
                                      : state.irq[source.line];
        const std::uint16_t mask = static_cast<std::uint16_t>(0x20u >> source.priority);
        if (!asserted || !(state.imask & mask)) continue;

        const auto old_pc = state.pc & pc_mask;
        push_pc(state);
        push_status(state);
        state.pc = static_cast<std::uint16_t>(0x04 + source.priority * 4);
        state.idle = false;
        state.irq_latched[source.line] = false;
        if (state.icntl & 0x10)
            state.imask &= static_cast<std::uint16_t>(~(0x3fu >> source.priority));
        else
            state.imask &= static_cast<std::uint16_t>(~0x3fu);
        mamerecomp_interpreter_trace_event(
            "ADSP-2105", "MameRecomp-interpreter",
            MAMERECOMP_EXCEPTION_BOUNDARY, old_pc, state.pc,
            state.cycles, state.cycles);
        return true;
    }
    return false;
}

} // namespace mamerecomp::adsp2105
