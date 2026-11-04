#pragma once

#include <cstdint>

namespace mamerecomp::m6809 {

// MC6809 and MC6809E share the same programmer-visible instruction set.
// The E variant differs in its external bus/clock signals, which belong to
// the board runtime rather than this CPU state.
struct State {
    std::uint8_t a{};
    std::uint8_t b{};
    std::uint8_t dp{};
    std::uint8_t cc{};
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t u{};
    std::uint16_t s{};
    std::uint16_t pc{};
    std::uint64_t cycles{};
    bool irq{};
    bool firq{};
    bool nmi{};
};

struct Bus {
    void* opaque{};
    std::uint8_t (*read8)(void*, std::uint16_t address){};
    void (*write8)(void*, std::uint16_t address, std::uint8_t value){};
    std::uint8_t (*rom_bank)(void*){};
};

enum class BlockExit : std::uint8_t {
    Dispatch,
    Budget,
    NeedsInterpreter,
    BusFault,
};

using Block = BlockExit (*)(State&, Bus&, std::uint64_t stop_cycle);

// Generated game code supplies these symbols.
Block lookup(std::uint16_t pc, std::uint8_t rom_bank) noexcept;
std::uint16_t reset_vector(const Bus& bus) noexcept;

inline constexpr std::uint8_t cc_c = 0x01;
inline constexpr std::uint8_t cc_v = 0x02;
inline constexpr std::uint8_t cc_z = 0x04;
inline constexpr std::uint8_t cc_n = 0x08;
inline constexpr std::uint8_t cc_i = 0x10;
inline constexpr std::uint8_t cc_h = 0x20;
inline constexpr std::uint8_t cc_f = 0x40;
inline constexpr std::uint8_t cc_e = 0x80;

inline void set_nz8(State& state, std::uint8_t value) noexcept {
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z));
    if (!value) state.cc |= cc_z;
    if (value & 0x80) state.cc |= cc_n;
}

inline void set_nz16(State& state, std::uint16_t value) noexcept {
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z));
    if (!value) state.cc |= cc_z;
    if (value & 0x8000) state.cc |= cc_n;
}

inline std::uint16_t aot_d(const State& state) noexcept {
    return static_cast<std::uint16_t>((state.a << 8) | state.b);
}

inline void aot_set_d(State& state, std::uint16_t value) noexcept {
    state.a = static_cast<std::uint8_t>(value >> 8);
    state.b = static_cast<std::uint8_t>(value);
}

inline std::uint8_t aot_add8(State& state, std::uint8_t lhs, std::uint8_t rhs,
                             unsigned carry = 0) noexcept {
    const unsigned wide = unsigned(lhs) + rhs + carry;
    const auto result = static_cast<std::uint8_t>(wide);
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z | cc_v | cc_c | cc_h));
    if (!result) state.cc |= cc_z;
    if (result & 0x80) state.cc |= cc_n;
    if ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) state.cc |= cc_v;
    if (wide & 0x100) state.cc |= cc_c;
    if ((lhs ^ rhs ^ result) & 0x10) state.cc |= cc_h;
    return result;
}

inline std::uint8_t aot_sub8(State& state, std::uint8_t lhs, std::uint8_t rhs,
                             unsigned borrow = 0) noexcept {
    const unsigned subtrahend = unsigned(rhs) + borrow;
    const auto result = static_cast<std::uint8_t>(unsigned(lhs) - subtrahend);
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z | cc_v | cc_c));
    if (!result) state.cc |= cc_z;
    if (result & 0x80) state.cc |= cc_n;
    if (((lhs ^ rhs) & (lhs ^ result)) & 0x80) state.cc |= cc_v;
    if (unsigned(lhs) < subtrahend) state.cc |= cc_c;
    return result;
}

inline std::uint16_t aot_add16(State& state, std::uint16_t lhs,
                               std::uint16_t rhs) noexcept {
    const std::uint32_t wide = std::uint32_t(lhs) + rhs;
    const auto result = static_cast<std::uint16_t>(wide);
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z | cc_v | cc_c));
    if (!result) state.cc |= cc_z;
    if (result & 0x8000) state.cc |= cc_n;
    if ((~(lhs ^ rhs) & (lhs ^ result)) & 0x8000) state.cc |= cc_v;
    if (wide & 0x10000) state.cc |= cc_c;
    return result;
}

inline std::uint16_t aot_sub16(State& state, std::uint16_t lhs,
                               std::uint16_t rhs) noexcept {
    const auto result = static_cast<std::uint16_t>(lhs - rhs);
    state.cc &= static_cast<std::uint8_t>(~(cc_n | cc_z | cc_v | cc_c));
    if (!result) state.cc |= cc_z;
    if (result & 0x8000) state.cc |= cc_n;
    if (((lhs ^ rhs) & (lhs ^ result)) & 0x8000) state.cc |= cc_v;
    if (lhs < rhs) state.cc |= cc_c;
    return result;
}

inline std::uint16_t indexed_address(State& state, Bus& bus,
                                     std::uint8_t post,
                                     std::int16_t extension,
                                     std::uint16_t pc_after) noexcept {
    std::uint16_t* regs[] = {&state.x, &state.y, &state.u, &state.s};
    std::uint16_t& base = *regs[(post >> 5) & 3];
    const auto read16 = [&](std::uint16_t address) {
        if (!bus.read8) return std::uint16_t{0xffff};
        return static_cast<std::uint16_t>(
            (bus.read8(bus.opaque, address) << 8) |
            bus.read8(bus.opaque, static_cast<std::uint16_t>(address + 1)));
    };
    if (!(post & 0x80)) {
        int displacement = post & 0x1f;
        if (displacement & 0x10) displacement -= 0x20;
        return static_cast<std::uint16_t>(base + displacement);
    }
    switch (post & 0x1f) {
    case 0x00: { const auto ea = base; ++base; return ea; }
    case 0x01: { const auto ea = base; base += 2; return ea; }
    case 0x02: --base; return base;
    case 0x03: base -= 2; return base;
    case 0x04: return base;
    case 0x05: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(state.b));
    case 0x06: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(state.a));
    case 0x08: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(extension));
    case 0x09: return static_cast<std::uint16_t>(base + extension);
    case 0x0b: return static_cast<std::uint16_t>(base + static_cast<std::int16_t>(aot_d(state)));
    case 0x0c: return static_cast<std::uint16_t>(pc_after + static_cast<std::int8_t>(extension));
    case 0x0d: return static_cast<std::uint16_t>(pc_after + extension);
    case 0x0f: return static_cast<std::uint16_t>(extension);
    case 0x11: { const auto ea = base; base += 2; return read16(ea); }
    case 0x13: base -= 2; return read16(base);
    case 0x14: return read16(base);
    case 0x15: return read16(static_cast<std::uint16_t>(base + static_cast<std::int8_t>(state.b)));
    case 0x16: return read16(static_cast<std::uint16_t>(base + static_cast<std::int8_t>(state.a)));
    case 0x18: return read16(static_cast<std::uint16_t>(base + static_cast<std::int8_t>(extension)));
    case 0x19: return read16(static_cast<std::uint16_t>(base + extension));
    case 0x1b: return read16(static_cast<std::uint16_t>(base + static_cast<std::int16_t>(aot_d(state))));
    case 0x1c: return read16(static_cast<std::uint16_t>(pc_after + static_cast<std::int8_t>(extension)));
    case 0x1d: return read16(static_cast<std::uint16_t>(pc_after + extension));
    case 0x1f: return read16(static_cast<std::uint16_t>(extension));
    default: return 0;
    }
}

inline std::uint8_t aot_unary(State& state, unsigned operation,
                              std::uint8_t value) noexcept {
    constexpr auto C = cc_c, V = cc_v, Z = cc_z, N = cc_n;
    std::uint8_t result = value;
    const bool old_carry = (state.cc & C) != 0;
    switch (operation & 15) {
    case 0x0: result = static_cast<std::uint8_t>(-value); set_nz8(state, result);
        state.cc &= static_cast<std::uint8_t>(~(V | C));
        if (value == 0x80) state.cc |= V; if (value) state.cc |= C; break;
    case 0x1: result = static_cast<std::uint8_t>(-value); set_nz8(state, result);
        state.cc &= static_cast<std::uint8_t>(~(V | C));
        if (value == 0x80) state.cc |= V; if (value) state.cc |= C; break;
    case 0x3: result = static_cast<std::uint8_t>(~value); set_nz8(state, result);
        state.cc = static_cast<std::uint8_t>((state.cc & ~(V | C)) | C); break;
    case 0x4: case 0x5: result = value >> 1;
        state.cc = static_cast<std::uint8_t>((state.cc & ~(N | Z | V | C)) |
            ((value & 1) ? C : 0) | (!result ? Z : 0) | ((value & 1) ? V : 0)); break;
    case 0x6: case 0x7: result = static_cast<std::uint8_t>((value >> 1) | (old_carry ? 0x80 : 0));
        state.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) state.cc |= Z; if (result & 0x80) state.cc |= N;
        if (value & 1) state.cc |= C;
        if (((result >> 7) ^ (state.cc & C)) & 1) state.cc |= V; break;
    case 0x8: case 0x9: result = static_cast<std::uint8_t>(value << 1);
        state.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) state.cc |= Z; if (result & 0x80) state.cc |= N;
        if (value & 0x80) state.cc |= C;
        if (((result >> 7) ^ ((state.cc & C) != 0)) & 1) state.cc |= V; break;
    case 0xa: result = static_cast<std::uint8_t>(value - 1); set_nz8(state, result);
        state.cc &= static_cast<std::uint8_t>(~V); if (value == 0x80) state.cc |= V; break;
    case 0xc: result = static_cast<std::uint8_t>(value + 1); set_nz8(state, result);
        state.cc &= static_cast<std::uint8_t>(~V); if (value == 0x7f) state.cc |= V; break;
    case 0xd: set_nz8(state, value); state.cc &= static_cast<std::uint8_t>(~V); break;
    case 0xf: result = 0; state.cc = static_cast<std::uint8_t>((state.cc & ~(N | V | C)) | Z); break;
    default: break;
    }
    return result;
}

} // namespace mamerecomp::m6809
