#include "interpreter.hpp"

#include "dag.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>

namespace multiarcaderecomp::adsp2105 {
namespace {
void trace_dcs_audio_step(const State& state, std::uint16_t pc,
                          std::uint32_t opcode) noexcept {
    static const bool enabled = std::getenv("MULTIARCADERECOMP_ADSP_TRACE_AUDIO") != nullptr;
    static std::uint32_t lines = 0;
    // Focus this opt-in diagnostic on the sample-buffer writer. The old wide
    // range filled the cap in a busy wait before reaching the writes at 0x147.
    if (!enabled || pc < 0x0144 || pc > 0x014b || lines >= 256) return;
    ++lines;
    const auto& c = state.core;
    std::fprintf(stderr,
        "[ADSP audio trace] PC=%04X OP=%06X ASTAT=%04X MSTAT=%04X "
        "AR=%04X AF=%04X AX=%04X/%04X AY=%04X/%04X "
        "MX=%04X/%04X MY=%04X/%04X MR=%010llX "
        "I=%04X,%04X,%04X,%04X M=%04X,%04X,%04X,%04X "
        "L=%04X,%04X,%04X,%04X BASE=%04X,%04X,%04X,%04X\n",
        pc, opcode & opcode_mask, state.astat, state.mstat,
        static_cast<std::uint16_t>(c.ar), static_cast<std::uint16_t>(c.af),
        static_cast<std::uint16_t>(c.ax[0]), static_cast<std::uint16_t>(c.ax[1]),
        static_cast<std::uint16_t>(c.ay[0]), static_cast<std::uint16_t>(c.ay[1]),
        static_cast<std::uint16_t>(c.mx[0]), static_cast<std::uint16_t>(c.mx[1]),
        static_cast<std::uint16_t>(c.my[0]), static_cast<std::uint16_t>(c.my[1]),
        static_cast<unsigned long long>(c.mr),
        state.i[4], state.i[5], state.i[6], state.i[7],
        static_cast<std::uint16_t>(state.m[4]), static_cast<std::uint16_t>(state.m[5]),
        static_cast<std::uint16_t>(state.m[6]), static_cast<std::uint16_t>(state.m[7]),
        state.l[4], state.l[5], state.l[6], state.l[7],
        state.base[4], state.base[5], state.base[6], state.base[7]);
}

constexpr std::uint16_t kPcEmpty = 0x01;
constexpr std::uint16_t kPcOverflow = 0x02;
constexpr std::uint16_t kCounterEmpty = 0x04;
constexpr std::uint16_t kCounterOverflow = 0x08;
constexpr std::uint16_t kStatusEmpty = 0x10;
constexpr std::uint16_t kStatusOverflow = 0x20;
constexpr std::uint16_t kLoopEmpty = 0x40;
constexpr std::uint16_t kLoopOverflow = 0x80;
constexpr std::uint16_t kAstatS = 0x10;
constexpr std::uint16_t kAstatQ = 0x20;
constexpr std::uint16_t kAstatMV = 0x40;
constexpr std::uint16_t kAstatSS = 0x80;
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

void push_pc(State& state) noexcept;
void push_pc_value(State& state, std::uint16_t value) noexcept;
std::uint16_t pop_pc_value(State& state) noexcept;

void update_mstat(State& state, std::uint16_t previous) noexcept {
    if ((state.mstat ^ previous) & kMstatBank) {
        auto active = state.core;
        state.core = state.alternate;
        state.alternate = active;
    }
    if ((state.mstat ^ previous) & kMstatTimer)
        state.timer_scale_counter = 0;
}

void execute_mode_control(State& state, std::uint32_t opcode) noexcept {
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
    case 11: return 0;
    case 12: return state.interrupt_clear;
    case 13: return state.counter;
    case 15: return pop_pc_value(state);
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
    case 11: {
        constexpr unsigned s1_autobuf = 0x0f;
        constexpr unsigned s1_sclkdiv = 0x11;
        constexpr unsigned s1_control = 0x12;
        constexpr unsigned sysctl = 0x1f;
        if ((state.control[sysctl] & 0x0800) &&
            (state.control[s1_autobuf] & 0x0002)) {
            state.tx_index_register = (state.control[s1_autobuf] >> 9) & 7;
            state.tx_modifier_register = ((state.control[s1_autobuf] >> 7) & 3) |
                                         (state.tx_index_register & 4);
            state.tx_size = state.l[state.tx_index_register];
            state.tx_base = state.i[state.tx_index_register] & ~0x0fu;
            // MAME's set_state_int(Ix, source) also recalculates the DAG
            // circular base. Keeping the old base here makes subsequent
            // SPORT autobuffer writes wrap against the pre-DMA buffer.
            dag_write_i(state, state.tx_index_register, state.tx_base);
            state.tx_enabled = state.tx_size != 0;
            state.tx_counter = 0;
            state.tx_period = (state.control[s1_control] & 0x4000)
                ? 2 * (state.control[s1_sclkdiv] + 1) * 16 : 320;
        } else {
            state.tx_enabled = false;
        }
        if (bus.external_sport_timing && bus.sport_tx)
            bus.sport_tx(bus.opaque, 1, static_cast<std::uint16_t>(value));
        break;
    }
    case 12:
        state.interrupt_clear = static_cast<std::uint16_t>(value);
        if (value & 0x0002) state.irq_latched[0] = false;
        if (value & 0x0004) state.irq_latched[1] = false;
        if (value & 0x0008) state.irq_latched[3] = false;
        if (value & 0x0010) state.irq_latched[4] = false;
        if (value & 0x0020) state.irq_latched[2] = false;
        if (value & 0x0080) state.irq_latched[0] = true;
        if (value & 0x0100) state.irq_latched[1] = true;
        if (value & 0x0200) state.irq_latched[3] = true;
        if (value & 0x0400) state.irq_latched[4] = true;
        if (value & 0x0800) state.irq_latched[2] = true;
        break;
    case 13: state.counter = value & 0x3fff; break;
    case 15: push_pc_value(state, static_cast<std::uint16_t>(value) & pc_mask); break;
    default: break;
    }
}

} // namespace

std::uint16_t data_read(State& state, Bus& bus, std::uint16_t address) noexcept {
    address &= pc_mask;
    if (bus.core_internal_data_memory && address >= 0x3800 && address < 0x3a00)
        return state.internal_data[address - 0x3800];
    if (bus.core_internal_data_memory && address >= 0x3fe0)
        return state.control[address - 0x3fe0];
    return bus.read_data16 ? bus.read_data16(bus.opaque, address) : 0xffff;
}

void data_write(State& state, Bus& bus, std::uint16_t address,
                std::uint16_t value) noexcept {
    address &= pc_mask;
    if (bus.core_internal_data_memory && address >= 0x3800 && address < 0x3a00) {
        state.internal_data[address - 0x3800] = value;
        return;
    }
    if (bus.core_internal_data_memory && address >= 0x3fe0) {
        const unsigned reg = address - 0x3fe0;
        state.control[reg] = value;
        constexpr unsigned s1_autobuf = 0x0f;
        constexpr unsigned s1_sclkdiv = 0x11;
        constexpr unsigned s1_control = 0x12;
        constexpr unsigned timer_scale = 0x1b;
        constexpr unsigned timer_count = 0x1c;
        constexpr unsigned timer_period = 0x1d;
        constexpr unsigned sysctl = 0x1f;
        switch (reg) {
        case sysctl:
            if (value & 0x0200) {
                state.control[sysctl] = 0;
                state.tx_enabled = false;
                if (bus.soft_reset) bus.soft_reset(bus.opaque);
                return;
            }
            if (!(value & 0x0800)) state.tx_enabled = false;
            break;
        case s1_autobuf:
            if (!(value & 0x0002)) state.tx_enabled = false;
            break;
        case s1_control:
        case s1_sclkdiv:
            if (state.tx_enabled)
                state.tx_period = (state.control[s1_control] & 0x4000)
                    ? 2 * (state.control[s1_sclkdiv] + 1) * 16 : 320;
            break;
        case timer_scale:
            state.timer_scale_counter = 0;
            break;
        case timer_count:
        case timer_period:
            break;
        default:
            break;
        }
        if (bus.write_data16) bus.write_data16(bus.opaque, address, value);
        return;
    }
    if (bus.write_data16) bus.write_data16(bus.opaque, address, value);
}

void advance_clock(State& state, Bus& bus, std::uint64_t cycles) noexcept {
    constexpr std::uint16_t timer_enable = 0x20;
    constexpr unsigned timer_scale = 0x1b;
    constexpr unsigned timer_count = 0x1c;
    constexpr unsigned timer_period = 0x1d;
    state.cycles += cycles;
    if (state.mstat & timer_enable) {
        const auto scale = (state.control[timer_scale] & 0xff) + 1;
        state.timer_scale_counter += static_cast<std::int32_t>(cycles);
        while (state.timer_scale_counter >= scale) {
            state.timer_scale_counter -= scale;
            if (state.control[timer_count] == 0) {
                state.control[timer_count] = state.control[timer_period];
                state.irq_latched[5] = true;
            } else {
                --state.control[timer_count];
            }
        }
    }
    if (bus.external_sport_timing || !state.tx_enabled || state.tx_period <= 0) return;
    state.tx_counter += static_cast<std::int32_t>(cycles);
    while (state.tx_counter >= state.tx_period) {
        state.tx_counter -= state.tx_period;
        const auto index = static_cast<unsigned>(state.tx_index_register);
        const auto sample = data_read(state, bus, state.i[index]);
        if (bus.sport_tx) bus.sport_tx(bus.opaque, 1, sample);
        std::uint16_t next = static_cast<std::uint16_t>(
            (state.i[index] + state.m[state.tx_modifier_register]) & pc_mask);
        const std::uint32_t end = static_cast<std::uint32_t>(state.tx_base) + state.tx_size;
        if (next >= end) {
            next = state.tx_base;
            state.irq_latched[1] = true;
        }
        state.i[index] = next;
    }
}

namespace {

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

// The ADSP has separate operand-selector tables for ALU and MAC operations.
// MAME initializes m_alu_*regs and m_mac_*regs independently in
// adsp2100.cpp; MAC selector 0 is MX0 (not AX0), and selector 2 on Y is MF
// (not AF).
std::uint16_t mac_x(const State& state, unsigned reg) noexcept {
    const auto& c = state.core;
    switch (reg & 7) {
    case 0: return c.mx[0]; case 1: return c.mx[1];
    case 2: return c.ar;
    case 3: return static_cast<std::uint16_t>(c.mr);
    case 4: return static_cast<std::uint16_t>(c.mr >> 16);
    case 5: return static_cast<std::uint16_t>(c.mr >> 32);
    case 6: return c.sr[0]; default: return c.sr[1];
    }
}

std::uint16_t mac_y(const State& state, unsigned reg) noexcept {
    switch (reg & 3) {
    case 0: return state.core.my[0];
    case 1: return state.core.my[1];
    case 2: return static_cast<std::uint16_t>(state.core.mf);
    default: return 0;
    }
}

std::int64_t mac_product(const State& state, unsigned operation,
                         std::uint16_t xbits, std::uint16_t ybits) noexcept {
    // MAME's MAC helpers store the multiplier result in int32_t before using
    // it in the 40-bit accumulator. Preserve that 32-bit wrap explicitly.
    const bool xu = operation == 6 || operation == 7 || operation == 10 ||
                    operation == 11 || operation == 14 || operation == 15;
    const bool yu = operation == 5 || operation == 7 || operation == 9 ||
                    operation == 11 || operation == 13 || operation == 15;
    const std::int64_t x = xu ? xbits : static_cast<std::int16_t>(xbits);
    const std::int64_t y = yu ? ybits : static_cast<std::int16_t>(ybits);
    std::uint32_t product = static_cast<std::uint32_t>(x * y);
    if (!(state.mstat & kMstatInteger)) product <<= 1;
    return std::bit_cast<std::int32_t>(product);
}

std::uint16_t shift_x(const State& state, unsigned reg) noexcept {
    const auto& c = state.core;
    switch (reg & 7) {
    case 0: case 1: return static_cast<std::uint16_t>(c.si);
    case 2: return static_cast<std::uint16_t>(c.ar);
    case 3: return static_cast<std::uint16_t>(c.mr);
    case 4: return static_cast<std::uint16_t>(c.mr >> 16);
    case 5: return static_cast<std::uint16_t>(c.mr >> 32);
    case 6: return c.sr[0];
    default: return c.sr[1];
    }
}

bool shift_execute(State& state, std::uint32_t opcode,
                   std::int8_t shift) noexcept {
    const unsigned xreg = (opcode >> 8) & 7;
    const unsigned operation = (opcode >> 11) & 15;
    const std::uint32_t x = shift_x(state, xreg);
    std::uint32_t value = 0;
    std::uint32_t result = 0;
    bool merge = (operation & 1) != 0;
    switch (operation) {
    case 0: case 1: {
        value = x << 16;
        if (shift >= 0) result = shift < 32 ? value << shift : 0;
        else result = shift > -32 ? value >> -shift : 0;
        break;
    }
    case 2: case 3:
        if (shift >= 0) result = shift < 32 ? x << shift : 0;
        else result = shift > -32 ? x >> -shift : 0;
        break;
    case 4: case 5: {
        value = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(x))) << 16;
        if (shift >= 0) result = shift < 32 ? value << shift : 0;
        else result = shift > -32
            ? static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> -shift)
            : ((value & 0x80000000u) ? 0xffffffffu : 0u);
        break;
    }
    case 6: case 7: {
        const auto signed_x = static_cast<std::int32_t>(
            static_cast<std::int16_t>(x));
        if (shift >= 0) result = shift < 32
            ? static_cast<std::uint32_t>(signed_x) << shift : 0;
        else result = shift > -32
            ? static_cast<std::uint32_t>(signed_x >> -shift)
            : (signed_x < 0 ? 0xffffffffu : 0u);
        break;
    }
    case 8: case 9: {
        value = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(x))) << 16;
        if (shift > 0) {
            value = (value >> 1) | ((state.astat & kAstatC) ? 0x80000000u : 0u);
            result = shift < 32 ? value >> (shift - 1) : 0;
        } else result = shift > -32 ? value << -shift : 0;
        break;
    }
    case 10: case 11:
        if (shift > 0) result = shift < 32 ? x >> shift : 0;
        else result = shift > -32 ? x << -shift : 0;
        break;
    case 12: {
        const auto signed_x = static_cast<std::int32_t>(
            static_cast<std::int16_t>(x));
        const auto leading = signed_x < 0
            ? std::countl_one(static_cast<std::uint32_t>(signed_x))
            : std::countl_zero(static_cast<std::uint32_t>(signed_x));
        const int exponent = static_cast<int>(leading) - 17;
        if (signed_x < 0) state.astat |= kAstatSS;
        else state.astat &= static_cast<std::uint16_t>(~kAstatSS);
        state.core.se = static_cast<std::int8_t>(-exponent);
        break;
    }
    case 13: {
        const auto signed_x = static_cast<std::int32_t>(
            static_cast<std::int16_t>(x));
        if (state.astat & kAstatV) {
            state.core.se = 1;
            if (signed_x < 0) state.astat &= static_cast<std::uint16_t>(~kAstatSS);
            else state.astat |= kAstatSS;
        } else {
            const auto leading = signed_x < 0
                ? std::countl_one(static_cast<std::uint32_t>(signed_x))
                : std::countl_zero(static_cast<std::uint32_t>(signed_x));
            const int exponent = static_cast<int>(leading) - 17;
            if (signed_x < 0) state.astat |= kAstatSS;
            else state.astat &= static_cast<std::uint16_t>(~kAstatSS);
            state.core.se = static_cast<std::int8_t>(-exponent);
        }
        break;
    }
    case 14:
        if (state.core.se == -15) {
            const auto signed_x = static_cast<std::int32_t>(
                static_cast<std::int16_t>(x));
            const auto normalized = static_cast<std::uint32_t>(
                ((state.astat & kAstatSS) ? ~signed_x : signed_x) & 0xffff);
            state.core.se = static_cast<std::int8_t>(
                -(static_cast<int>(std::countl_zero(normalized)) - 1));
        }
        break;
    case 15: {
        const auto signed_x = static_cast<std::int32_t>(
            static_cast<std::int16_t>(x));
        const auto normalized = static_cast<std::uint32_t>(
            signed_x < 0 ? ~signed_x : signed_x);
        const int exponent = static_cast<int>(std::countl_zero(normalized)) - 17;
        if (exponent < -static_cast<int>(state.core.sb))
            state.core.sb = static_cast<std::int8_t>(-exponent);
        break;
    }
    default:
        return false;
    }
    if (merge) {
        const auto old = (static_cast<std::uint32_t>(state.core.sr[1]) << 16) |
                         state.core.sr[0];
        result |= old;
    }
    state.core.sr[0] = static_cast<std::uint16_t>(result);
    state.core.sr[1] = static_cast<std::uint16_t>(result >> 16);
    return true;
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
    const auto xbits = mac_x(state, xr);
    const auto ybits = mac_y(state, yr);
    const std::int64_t product = mac_product(state, operation, xbits, ybits);
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

void mac_to_mf(State& state, std::uint32_t opcode) noexcept {
    const unsigned operation = (opcode >> 13) & 15;
    if (!operation) return;
    const auto xb = mac_x(state, (opcode >> 8) & 7);
    const auto yb = mac_y(state, (opcode >> 11) & 3);
    const std::int64_t product = mac_product(state, operation, xb, yb);
    std::int64_t result{};
    if (operation <= 3) {
        result = operation == 1 ? product : operation == 2
            ? state.core.mr + product : state.core.mr - product;
        result += 0x8000;
        if ((product & 0xffff) == 0x8000) result &= ~std::int64_t{0x10000};
    } else if (operation <= 7) {
        result = product;
    } else if (operation <= 11) {
        result = state.core.mr + product;
    } else {
        result = state.core.mr - product;
    }
    state.core.mf = static_cast<std::int16_t>(
        static_cast<std::uint32_t>(result) >> 16);
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
    push_pc_value(state, state.pc);
}

void push_pc_value(State& state, std::uint16_t value) noexcept {
    if (state.pc_stack_pointer < state.pc_stack.size()) {
        state.pc_stack[state.pc_stack_pointer++] = value & pc_mask;
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
    case 0x05: return "SATURATE_MR";
    case 0x06: return "DIVS";
    case 0x07: return "DIVQ";
    case 0x02: return (opcode & 0x8000) ? "IDLE" : "FLAG_CONTROL";
    case 0x03: return "JUMP_FLAG_IN";
    case 0x04: return "STACK_CONTROL";
    case 0x09: return "MODIFY_ADDRESS";
    case 0x0a: return "RETURN_CONDITIONAL";
    case 0x0b: return "JUMP_INDIRECT";
    case 0x0c: return "MODE_CONTROL";
    case 0x0d: return "INTERNAL_DATA_MOVE";
    case 0x0e: return "SHIFT_CONDITIONAL";
    case 0x0f: return "SHIFT_IMMEDIATE";
    case 0x10: return "SHIFT_INTERNAL_MOVE";
    case 0x11: return "SHIFT_PROGRAM_DAG2";
    case 0x12: return "SHIFT_DATA_DAG1";
    case 0x13: return "SHIFT_DATA_DAG2";
    case 0x14: case 0x15: case 0x16: case 0x17: return "DO_UNTIL";
    case 0x18: case 0x19: case 0x1a: case 0x1b: return "JUMP_IMMEDIATE";
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: return "CALL_IMMEDIATE";
    case 0x28: case 0x29: return "MAC_MR_MOVE";
    case 0x2a: case 0x2b: return "ALU_AR_MOVE";
    case 0x2c: case 0x2d: return "MAC_MF_MOVE";
    case 0x2e: case 0x2f: return "ALU_AF_MOVE";
    case 0x50: case 0x51: return "MAC_MR_PROGRAM_READ_DAG2";
    case 0x52: case 0x53: return "ALU_AR_PROGRAM_READ_DAG2";
    case 0x54: case 0x55: return "MAC_MF_PROGRAM_READ_DAG2";
    case 0x56: case 0x57: return "ALU_AF_PROGRAM_READ_DAG2";
    case 0x58: case 0x59: return "MAC_MR_PROGRAM_WRITE_DAG2";
    case 0x5a: case 0x5b: return "ALU_AR_PROGRAM_WRITE_DAG2";
    case 0x5c: case 0x5d: return "MAC_MF_PROGRAM_WRITE_DAG2";
    case 0x5e: case 0x5f: return "ALU_AF_PROGRAM_WRITE_DAG2";
    case 0xa0: case 0xa1: case 0xa2: case 0xa3:
    case 0xa4: case 0xa5: case 0xa6: case 0xa7:
    case 0xa8: case 0xa9: case 0xaa: case 0xab:
    case 0xac: case 0xad: case 0xae: case 0xaf:
        return "DATA_WRITE_IMMEDIATE_DAG1";
    case 0xb0: case 0xb1: case 0xb2: case 0xb3:
    case 0xb4: case 0xb5: case 0xb6: case 0xb7:
    case 0xb8: case 0xb9: case 0xba: case 0xbb:
    case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        return "DATA_WRITE_IMMEDIATE_DAG2";
    default: return "UNSUPPORTED";
    }
}

bool execute_data_dag_transfer(State& state, Bus& bus,
                               std::uint32_t opcode) noexcept {
    const unsigned group = (opcode >> 16) & 0xff;
    const unsigned destination = (opcode >> 4) & 15;
    const auto source = static_cast<std::uint16_t>(read_reg0(state, destination));
    std::uint16_t value{};
    switch (group) {
    case 0x60: case 0x61:
        mac_to_mr(state, opcode);
        if (!dag1_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x62: case 0x63:
        state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        if (!dag1_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x64: case 0x65:
        mac_to_mf(state, opcode);
        if (!dag1_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x66: case 0x67:
        state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        if (!dag1_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x68: case 0x69:
        if (!dag1_write(state, bus, opcode, source)) return false;
        mac_to_mr(state, opcode);
        return true;
    case 0x6a: case 0x6b:
        if (!dag1_write(state, bus, opcode, source)) return false;
        state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        return true;
    case 0x6c: case 0x6d:
        if (!dag1_write(state, bus, opcode, source)) return false;
        mac_to_mf(state, opcode);
        return true;
    case 0x6e: case 0x6f:
        if (!dag1_write(state, bus, opcode, source)) return false;
        state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        return true;
    case 0x70: case 0x71:
        mac_to_mr(state, opcode);
        if (!dag2_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x72: case 0x73:
        state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        if (!dag2_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x74: case 0x75:
        mac_to_mf(state, opcode);
        if (!dag2_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x76: case 0x77:
        state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        if (!dag2_read(state, bus, opcode, value)) return false;
        write_reg0(state, destination, value);
        return true;
    case 0x78: case 0x79:
        if (!dag2_write(state, bus, opcode, source)) return false;
        mac_to_mr(state, opcode);
        return true;
    case 0x7a: case 0x7b:
        if (!dag2_write(state, bus, opcode, source)) return false;
        state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        return true;
    case 0x7c: case 0x7d:
        if (!dag2_write(state, bus, opcode, source)) return false;
        mac_to_mf(state, opcode);
        return true;
    case 0x7e: case 0x7f:
        if (!dag2_write(state, bus, opcode, source)) return false;
        state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        return true;
    default:
        return false;
    }
}

bool execute_program_dag_transfer(State& state, Bus& bus,
                                  std::uint32_t opcode) noexcept {
    const unsigned group = (opcode >> 16) & 0xfe;
    const unsigned reg = (opcode >> 4) & 15;
    if (group >= 0x50 && group <= 0x56) {
        switch (group) {
        case 0x50: mac_to_mr(state, opcode); break;
        case 0x52: state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode)); break;
        case 0x54: mac_to_mf(state, opcode); break;
        case 0x56: state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode)); break;
        default: return false;
        }
        std::uint16_t value{};
        if (!dag2_program_read(state, bus, opcode, value)) return false;
        write_reg0(state, reg, value);
        return true;
    }
    if (group >= 0x58 && group <= 0x5e) {
        if (!dag2_program_write(state, bus, opcode,
                                static_cast<std::uint16_t>(read_reg0(state, reg))))
            return false;
        switch (group) {
        case 0x58: mac_to_mr(state, opcode); break;
        case 0x5a: state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode)); break;
        case 0x5c: mac_to_mf(state, opcode); break;
        case 0x5e: state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode)); break;
        default: return false;
        }
        return true;
    }
    return false;
}

bool execute_misc(State& state, std::uint32_t opcode) noexcept {
    switch ((opcode >> 16) & 0xff) {
    case 0x05: // Saturate MR when the previous MAC overflowed.
        if (state.astat & kAstatMV)
            state.core.mr = (state.core.mr & (std::int64_t{1} << 39))
                ? -0x8000000000ll : 0x7fffffffffll;
        return true;
    case 0x06: {
        const auto x = static_cast<std::uint32_t>(alu_x(state, (opcode >> 8) & 7));
        const auto y = static_cast<std::uint32_t>(alu_y(state, (opcode >> 11) & 3));
        const auto temp = x ^ y;
        state.astat = static_cast<std::uint16_t>(
            (state.astat & ~kAstatQ) | ((temp >> 10) & kAstatQ));
        state.core.af = static_cast<std::int16_t>((y << 1) |
            (static_cast<std::uint16_t>(state.core.ay[0]) >> 15));
        state.core.ay[0] = static_cast<std::int16_t>(
            (static_cast<std::uint16_t>(state.core.ay[0]) << 1) | (temp >> 15));
        return true;
    }
    case 0x07: {
        const auto x = static_cast<std::uint32_t>(alu_x(state, (opcode >> 8) & 7));
        const std::uint32_t af = static_cast<std::uint16_t>(state.core.af);
        const std::uint32_t result = (state.astat & kAstatQ) ? af + x : af - x;
        const auto temp = result ^ x;
        state.astat = static_cast<std::uint16_t>(
            (state.astat & ~kAstatQ) | ((temp >> 10) & kAstatQ));
        state.core.af = static_cast<std::int16_t>((result << 1) |
            (static_cast<std::uint16_t>(state.core.ay[0]) >> 15));
        state.core.ay[0] = static_cast<std::int16_t>(
            (static_cast<std::uint16_t>(state.core.ay[0]) << 1) |
            ((~temp >> 15) & 1));
        return true;
    }
    default:
        return false;
    }
}
} // namespace

bool native_shift(State& state, std::uint32_t opcode,
                  std::int8_t shift) noexcept {
    return shift_execute(state, opcode, shift);
}

bool native_execute_instruction(State& state, Bus& bus,
                                std::uint32_t opcode) noexcept {
    opcode &= opcode_mask;
    const unsigned group = (opcode >> 16) & 0xff;
    const auto pc = static_cast<std::uint16_t>(state.pc & pc_mask);
    const bool loop_end = state.loop == pc;
    const bool native_loop_end_nop = loop_end && group == 0x00;
    const bool native_loop_end = loop_end &&
        ((group == 0x09) || (group == 0x20 || group == 0x21) ||
         ((group == 0x50 || group == 0x51) && ((opcode >> 13) & 15) == 0) ||
         ((group == 0x58 || group == 0x59 ||
           group == 0x68 || group == 0x69) && ((opcode >> 13) & 15) == 0));
    if (loop_end && !native_loop_end_nop && !native_loop_end) return false;
    if (bus.read_program24 &&
        (bus.read_program24(bus.opaque, pc) & opcode_mask) != opcode)
        return false;
    const bool native_loop_end_program_store = native_loop_end &&
        (group == 0x58 || group == 0x59);
    if (native_loop_end_program_store && !bus.write_program24) return false;

    state.previous_pc = pc;
    if (native_loop_end_nop || native_loop_end)
        advance_pc_for_loop(state);
    if (group == 0x00 && native_loop_end_nop) {
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    if (group >= 0x05 && group <= 0x07) {
        if (!execute_misc(state, opcode)) return false;
        state.pc = (state.pc + 1) & pc_mask;
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    if (group >= 0x50 && group <= 0x5f) {
        if (!execute_program_dag_transfer(state, bus, opcode)) return false;
        if (!native_loop_end) state.pc = (state.pc + 1) & pc_mask;
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    if (group >= 0x60 && group <= 0x7f) {
        if (!execute_data_dag_transfer(state, bus, opcode)) return false;
        if (native_loop_end) {
            advance_clock(state, bus);
            ++state.native_instructions;
            return true;
        }
        state.pc = (state.pc + 1) & pc_mask;
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    if (group >= 0x14 && group <= 0x17) {
        // The interpreter advances PC before executing DO UNTIL and stores
        // that next PC as the loop body entry. Match that order here; loop-end
        // execution itself is left to interpreter_step via the AOT boundary.
        state.pc = (state.pc + 1) & pc_mask;
        push_loop(state, opcode & 0x3ffff);
        push_pc(state);
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    switch (group) {
    case 0x00:
        break;
    case 0x0c:
        execute_mode_control(state, opcode);
        break;
    case 0x0a:
        if (condition(state, opcode & 15)) {
            pop_pc(state);
            if (opcode & 0x10) pop_status(state);
        }
        break;
    case 0x09: {
        const unsigned ireg = (opcode >> 2) & 7;
        const unsigned mreg = (ireg & 4) | (opcode & 3);
        (void)dag_advance(state, ireg, mreg);
        break;
    }
    case 0x0d: {
        const unsigned dst_group = (opcode >> 10) & 3;
        const unsigned src_group = (opcode >> 8) & 3;
        const unsigned dst = (opcode >> 4) & 15;
        const unsigned src = opcode & 15;
        std::int32_t value = src_group == 0 ? read_reg0(state, src) :
            (src_group == 3 ? read_reg3(state, bus, src) :
                              read_reg12(state, src_group, src));
        if (dst_group == 0) write_reg0(state, dst, value);
        else if (dst_group == 3) write_reg3(state, bus, dst, value);
        else write_reg12(state, dst_group, dst, value);
        break;
    }
    case 0x50: case 0x51: {
        if (((opcode >> 13) & 15) != 0) return false;
        std::uint16_t value{};
        if (!dag2_program_read(state, bus, opcode, value)) return false;
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x58: case 0x59:
        // MAC function zero is a no-op; the instruction still performs the
        // DAG2 program-space write (including its PX byte and I/M update).
        if (((opcode >> 13) & 15) != 0 ||
            !dag2_program_write(state, bus, opcode,
                static_cast<std::uint16_t>(
                    read_reg0(state, (opcode >> 4) & 15))))
            return false;
        if (native_loop_end_program_store) {
            advance_clock(state, bus);
            ++state.native_instructions;
            return true;
        }
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
    case 0x10:
        if (!shift_execute(state, opcode, state.core.se)) return false;
        write_reg0(state, (opcode >> 4) & 15, read_reg0(state, opcode & 15));
        break;
    case 0x11:
        if (opcode & 0x8000) {
            if (!dag2_program_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15))) ||
                !shift_execute(state, opcode, state.core.se)) return false;
        } else {
            std::uint16_t value{};
            if (!shift_execute(state, opcode, state.core.se) ||
                !dag2_program_read(state, bus, opcode, value)) return false;
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x12:
        if (opcode & 0x8000) {
            if (!dag1_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15))) ||
                !shift_execute(state, opcode, state.core.se)) return false;
        } else {
            std::uint16_t value{};
            if (!shift_execute(state, opcode, state.core.se) ||
                !dag1_read(state, bus, opcode, value)) return false;
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x13:
        if (opcode & 0x8000) {
            if (!dag2_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15))) ||
                !shift_execute(state, opcode, state.core.se)) return false;
        } else {
            std::uint16_t value{};
            if (!shift_execute(state, opcode, state.core.se) ||
                !dag2_read(state, bus, opcode, value)) return false;
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x22: case 0x23:
        if (condition(state, opcode & 15))
            state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        break;
    case 0x20: case 0x21:
        if (condition(state, opcode & 15)) mac_to_mr(state, opcode);
        break;
    case 0x26: case 0x27:
        if (condition(state, opcode & 15))
            state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
        break;
    case 0x28: case 0x29: case 0x2a: case 0x2b:
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        const auto value = read_reg0(state, opcode & 15);
        switch (group) {
        case 0x28: case 0x29: mac_to_mr(state, opcode); break;
        case 0x2a: case 0x2b:
            state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
            break;
        case 0x2c: case 0x2d: mac_to_mf(state, opcode); break;
        default:
            state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
            break;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8a: case 0x8b:
    case 0x8c: case 0x8d: case 0x8e: case 0x8f: {
        if (!bus.read_data16) return false;
        const unsigned destination_group = (group - 0x80) >> 2;
        const unsigned destination = opcode & 15;
        const auto value = data_read(state, bus,
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
        if (!bus.write_data16) return false;
        const unsigned source_group = (group - 0x90) >> 2;
        const unsigned source = opcode & 15;
        const std::int32_t value = source_group == 0 ? read_reg0(state, source) :
            (source_group == 3 ? read_reg3(state, bus, source) :
                                 read_reg12(state, source_group, source));
        data_write(state, bus,
            static_cast<std::uint16_t>((opcode >> 4) & 0x3fff),
            static_cast<std::uint16_t>(value));
        break;
    }
    case 0x18: case 0x19: case 0x1a: case 0x1b:
        if (condition(state, opcode & 15))
            state.pc = static_cast<std::uint16_t>((opcode >> 4) & pc_mask);
        else state.pc = (state.pc + 1) & pc_mask;
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        if (condition(state, opcode & 15)) {
            state.pc = (state.pc + 1) & pc_mask;
            push_pc(state);
            state.pc = static_cast<std::uint16_t>((opcode >> 4) & pc_mask);
        } else state.pc = (state.pc + 1) & pc_mask;
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    default:
        return false;
    }

    if (native_loop_end) {
        advance_clock(state, bus);
        ++state.native_instructions;
        return true;
    }
    state.pc = (state.pc + 1) & pc_mask;
    advance_clock(state, bus);
    ++state.native_instructions;
    return true;
}

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MultiArcadeRecompFallbackReason reason) noexcept {
    if (!bus.read_program24) return BlockExit::BusFault;
    if (state.cycles >= stop_cycle) return BlockExit::Budget;

    const std::uint16_t pc = state.pc & pc_mask;
    const std::uint32_t opcode = bus.read_program24(bus.opaque, pc) & opcode_mask;
    const unsigned group = (opcode >> 16) & 0xff;
    trace_dcs_audio_step(state, pc, opcode);
    const std::uint64_t cycles_before = state.cycles;
    state.previous_pc = pc;
    advance_pc_for_loop(state);

    if (group >= 0x05 && group <= 0x07) {
        (void)execute_misc(state, opcode);
        advance_clock(state, bus);
        ++state.interpreted_instructions;
        multiarcaderecomp_interpreter_trace_step("ADSP-2105", "MultiArcadeRecomp-interpreter",
            reason, pc, opcode, 24, cycles_before, state.cycles, state.pc,
            instruction_name(opcode));
        return state.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
    }

    if (group >= 0x50 && group <= 0x5f) {
        if (!execute_program_dag_transfer(state, bus, opcode)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        advance_clock(state, bus);
        ++state.interpreted_instructions;
        multiarcaderecomp_interpreter_trace_step("ADSP-2105", "MultiArcadeRecomp-interpreter",
            reason, pc, opcode, 24, cycles_before, state.cycles, state.pc,
            instruction_name(opcode));
        return state.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
    }

    if (group >= 0x60 && group <= 0x7f) {
        if (!execute_data_dag_transfer(state, bus, opcode)) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        advance_clock(state, bus);
        ++state.interpreted_instructions;
        multiarcaderecomp_interpreter_trace_step("ADSP-2105", "MultiArcadeRecomp-interpreter",
            reason, pc, opcode, 24, cycles_before, state.cycles, state.pc,
            instruction_name(opcode));
        return state.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
    }

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
        execute_mode_control(state, opcode);
        break;
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
    case 0x0e:
        if (condition(state, opcode & 15) &&
            !shift_execute(state, opcode, state.core.se)) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        break;
    case 0x0f:
        if (!shift_execute(state, opcode, static_cast<std::int8_t>(opcode))) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        break;
    case 0x10: {
        if (!shift_execute(state, opcode, state.core.se)) {
            state.pc = pc;
            return BlockExit::NeedsInterpreter;
        }
        const auto value = read_reg0(state, opcode & 15);
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
    case 0x11:
        if (opcode & 0x8000) {
            if (!dag2_program_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15)))) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
        } else {
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
            std::uint16_t value{};
            if (!dag2_program_read(state, bus, opcode, value)) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x12:
        if (opcode & 0x8000) {
            if (!dag1_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15)))) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
        } else {
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
            std::uint16_t value{};
            if (!dag1_read(state, bus, opcode, value)) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x13:
        if (opcode & 0x8000) {
            if (!dag2_write(state, bus, opcode,
                    static_cast<std::uint16_t>(read_reg0(state, (opcode >> 4) & 15)))) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
        } else {
            if (!shift_execute(state, opcode, state.core.se)) {
                state.pc = pc;
                return BlockExit::NeedsInterpreter;
            }
            std::uint16_t value{};
            if (!dag2_read(state, bus, opcode, value)) {
                state.pc = pc;
                return BlockExit::BusFault;
            }
            write_reg0(state, (opcode >> 4) & 15, value);
        }
        break;
    case 0x22: case 0x23:
        if (condition(state, opcode & 15))
            state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
        break;
    case 0x28: case 0x29: case 0x2a: case 0x2b:
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        const auto value = read_reg0(state, opcode & 15);
        switch (group) {
        case 0x28: case 0x29: mac_to_mr(state, opcode); break;
        case 0x2a: case 0x2b:
            state.core.ar = static_cast<std::int16_t>(alu_execute(state, opcode));
            break;
        case 0x2c: case 0x2d: mac_to_mf(state, opcode); break;
        default:
            state.core.af = static_cast<std::int16_t>(alu_execute(state, opcode));
            break;
        }
        write_reg0(state, (opcode >> 4) & 15, value);
        break;
    }
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
        const auto value = data_read(state, bus,
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
        data_write(state, bus,
            static_cast<std::uint16_t>((opcode >> 4) & 0x3fff),
            static_cast<std::uint16_t>(value));
        break;
    }
    case 0xa0: case 0xa1: case 0xa2: case 0xa3:
    case 0xa4: case 0xa5: case 0xa6: case 0xa7:
    case 0xa8: case 0xa9: case 0xaa: case 0xab:
    case 0xac: case 0xad: case 0xae: case 0xaf:
        if (!dag1_write(state, bus, opcode,
                        static_cast<std::uint16_t>((opcode >> 4) & 0xffff))) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        break;
    case 0xb0: case 0xb1: case 0xb2: case 0xb3:
    case 0xb4: case 0xb5: case 0xb6: case 0xb7:
    case 0xb8: case 0xb9: case 0xba: case 0xbb:
    case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        if (!dag2_write(state, bus, opcode,
                        static_cast<std::uint16_t>((opcode >> 4) & 0xffff))) {
            state.pc = pc;
            return BlockExit::BusFault;
        }
        break;
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
        multiarcaderecomp_interpreter_trace_step("ADSP-2105", "MultiArcadeRecomp-interpreter",
            group == 0x01 ? MULTIARCADERECOMP_UNSUPPORTED_VARIANT :
                            MULTIARCADERECOMP_UNSUPPORTED_OPCODE,
            pc, opcode, 24, cycles_before, state.cycles, pc,
            instruction_name(opcode));
        return BlockExit::NeedsInterpreter;
    }

    advance_clock(state, bus);
    ++state.interpreted_instructions;
    multiarcaderecomp_interpreter_trace_step("ADSP-2105", "MultiArcadeRecomp-interpreter",
        reason, pc, opcode, 24, cycles_before, state.cycles, state.pc,
        instruction_name(opcode));
    return state.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
}

BlockExit native_nop_counter_loop(State& state, std::uint16_t pc,
                                  std::uint64_t stop_cycle) noexcept {
    pc &= pc_mask;
    if ((state.pc & pc_mask) != pc || state.loop != pc ||
        state.loop_condition != 14 || pc_stack_top(state) != pc)
        return BlockExit::NeedsInterpreter;
    if (state.cycles >= stop_cycle) return BlockExit::Budget;

    // For condition 14, the hardware decrements the counter before testing.
    // The PC stack check proves that the loop body is this single NOP, so its
    // iterations have no side effects between the counter tests.
    const auto signed_counter = static_cast<std::int32_t>(state.counter);
    const std::uint64_t iterations = signed_counter > 0
        ? static_cast<std::uint32_t>(signed_counter) : 1;
    const auto available = stop_cycle - state.cycles;
    const auto executed = std::min(iterations, available);
    state.previous_pc = pc;
    state.cycles += executed;
    state.native_instructions += executed;

    if (executed < iterations) {
        state.counter -= static_cast<std::uint32_t>(executed);
        return BlockExit::Budget;
    }

    state.counter -= static_cast<std::uint32_t>(iterations);
    pop_counter(state);
    pop_loop(state);
    (void)pop_pc_value(state);
    state.pc = (state.pc + 1) & pc_mask;
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
        multiarcaderecomp_interpreter_trace_event(
            "ADSP-2105", "MultiArcadeRecomp-interpreter",
            MULTIARCADERECOMP_EXCEPTION_BOUNDARY, old_pc, state.pc,
            state.cycles, state.cycles);
        return true;
    }
    return false;
}

} // namespace multiarcaderecomp::adsp2105
