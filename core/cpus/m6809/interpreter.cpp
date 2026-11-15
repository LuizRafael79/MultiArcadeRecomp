#include "interpreter.hpp"

#include <cstdint>

namespace multiarcaderecomp::m6809 {
namespace {
constexpr std::uint8_t C = cc_c, V = cc_v, Z = cc_z, N = cc_n;

std::uint8_t read8(Bus& bus, std::uint16_t address) noexcept {
    return bus.read8 ? bus.read8(bus.opaque, address) : 0xff;
}
void write8(Bus& bus, std::uint16_t address, std::uint8_t value) noexcept {
    if (bus.write8) bus.write8(bus.opaque, address, value);
}
std::uint16_t read16(Bus& bus, std::uint16_t address) noexcept {
    return static_cast<std::uint16_t>((read8(bus, address) << 8) |
        read8(bus, static_cast<std::uint16_t>(address + 1)));
}
void write16(Bus& bus, std::uint16_t address, std::uint16_t value) noexcept {
    write8(bus, address, static_cast<std::uint8_t>(value >> 8));
    write8(bus, static_cast<std::uint16_t>(address + 1),
           static_cast<std::uint8_t>(value));
}
std::uint8_t fetch8(State& state, Bus& bus) noexcept {
    return read8(bus, state.pc++);
}
std::uint16_t fetch16(State& state, Bus& bus) noexcept {
    const auto high = fetch8(state, bus);
    return static_cast<std::uint16_t>((high << 8) | fetch8(state, bus));
}
void push8(Bus& bus, std::uint16_t& sp, std::uint8_t value) noexcept {
    write8(bus, --sp, value);
}
void push16(Bus& bus, std::uint16_t& sp, std::uint16_t value) noexcept {
    push8(bus, sp, static_cast<std::uint8_t>(value));
    push8(bus, sp, static_cast<std::uint8_t>(value >> 8));
}
std::uint8_t pull8(Bus& bus, std::uint16_t& sp) noexcept {
    return read8(bus, sp++);
}
std::uint16_t pull16(Bus& bus, std::uint16_t& sp) noexcept {
    const auto high = pull8(bus, sp);
    return static_cast<std::uint16_t>((high << 8) | pull8(bus, sp));
}
std::uint16_t d(const State& state) noexcept {
    return static_cast<std::uint16_t>((state.a << 8) | state.b);
}
void set_d(State& state, std::uint16_t value) noexcept {
    state.a = static_cast<std::uint8_t>(value >> 8);
    state.b = static_cast<std::uint8_t>(value);
}
std::uint8_t add8(State& s, std::uint8_t lhs, std::uint8_t rhs,
                  unsigned carry = 0) noexcept {
    const unsigned wide = unsigned(lhs) + rhs + carry;
    const auto result = static_cast<std::uint8_t>(wide);
    s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C | cc_h));
    if (!result) s.cc |= Z;
    if (result & 0x80) s.cc |= N;
    if ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) s.cc |= V;
    if (wide & 0x100) s.cc |= C;
    if ((lhs ^ rhs ^ result) & 0x10) s.cc |= cc_h;
    return result;
}
std::uint8_t sub8(State& s, std::uint8_t lhs, std::uint8_t rhs,
                  unsigned borrow = 0) noexcept {
    const unsigned subtrahend = unsigned(rhs) + borrow;
    const auto result = static_cast<std::uint8_t>(unsigned(lhs) - subtrahend);
    s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
    if (!result) s.cc |= Z;
    if (result & 0x80) s.cc |= N;
    if (((lhs ^ rhs) & (lhs ^ result)) & 0x80) s.cc |= V;
    if (unsigned(lhs) < subtrahend) s.cc |= C;
    return result;
}
std::uint16_t add16(State& s, std::uint16_t lhs,
                    std::uint16_t rhs) noexcept {
    const std::uint32_t wide = std::uint32_t(lhs) + rhs;
    const auto result = static_cast<std::uint16_t>(wide);
    s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
    if (!result) s.cc |= Z;
    if (result & 0x8000) s.cc |= N;
    if ((~(lhs ^ rhs) & (lhs ^ result)) & 0x8000) s.cc |= V;
    if (wide & 0x10000) s.cc |= C;
    return result;
}
std::uint16_t sub16(State& s, std::uint16_t lhs,
                    std::uint16_t rhs) noexcept {
    const auto result = static_cast<std::uint16_t>(lhs - rhs);
    s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
    if (!result) s.cc |= Z;
    if (result & 0x8000) s.cc |= N;
    if (((lhs ^ rhs) & (lhs ^ result)) & 0x8000) s.cc |= V;
    if (lhs < rhs) s.cc |= C;
    return result;
}
bool branch_condition(const State& s, unsigned condition) noexcept {
    const bool c = s.cc & C, v = s.cc & V, z = s.cc & Z, n = s.cc & N;
    switch (condition & 15) {
    case 0: return true;
    case 1: return false;
    case 2: return !c && !z;
    case 3: return c || z;
    case 4: return !c;
    case 5: return c;
    case 6: return !z;
    case 7: return z;
    case 8: return !v;
    case 9: return v;
    case 10: return !n;
    case 11: return n;
    case 12: return n == v;
    case 13: return n != v;
    case 14: return !z && n == v;
    default: return z || n != v;
    }
}
std::uint16_t indexed(State& s, Bus& bus) noexcept {
    const std::uint8_t post = fetch8(s, bus);
    std::uint16_t* regs[] = {&s.x, &s.y, &s.u, &s.s};
    std::uint16_t& base = *regs[(post >> 5) & 3];
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
    case 0x05: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(s.b));
    case 0x06: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(s.a));
    case 0x15: return read16(bus, static_cast<std::uint16_t>(
        base + static_cast<std::int8_t>(s.b)));
    case 0x16: return read16(bus, static_cast<std::uint16_t>(
        base + static_cast<std::int8_t>(s.a)));
    case 0x08: return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(fetch8(s, bus)));
    case 0x09: return static_cast<std::uint16_t>(base + static_cast<std::int16_t>(fetch16(s, bus)));
    case 0x0b: return static_cast<std::uint16_t>(base + static_cast<std::int16_t>(d(s)));
    case 0x0c: return static_cast<std::uint16_t>(s.pc + static_cast<std::int8_t>(fetch8(s, bus)));
    case 0x0d: return static_cast<std::uint16_t>(s.pc + static_cast<std::int16_t>(fetch16(s, bus)));
    case 0x0f: return fetch16(s, bus);
    case 0x11: { const auto ea = base; base += 2; return read16(bus, ea); }
    case 0x13: { base -= 2; return read16(bus, base); }
    case 0x14: return read16(bus, base);
    case 0x18: return read16(bus, static_cast<std::uint16_t>(base + static_cast<std::int8_t>(fetch8(s, bus))));
    case 0x19: return read16(bus, static_cast<std::uint16_t>(base + static_cast<std::int16_t>(fetch16(s, bus))));
    case 0x1b: return read16(bus, static_cast<std::uint16_t>(base + static_cast<std::int16_t>(d(s))));
    case 0x1c: return read16(bus, static_cast<std::uint16_t>(s.pc + static_cast<std::int8_t>(fetch8(s, bus))));
    case 0x1d: return read16(bus, static_cast<std::uint16_t>(s.pc + static_cast<std::int16_t>(fetch16(s, bus))));
    case 0x1f: return read16(bus, fetch16(s, bus));
    default: return 0;
    }
}

std::uint8_t unary(State& s, unsigned operation,
                   std::uint8_t value) noexcept {
    std::uint8_t result = value;
    const bool old_carry = (s.cc & C) != 0;
    switch (operation & 15) {
    case 0x0: // NEG
        result = static_cast<std::uint8_t>(-value);
        set_nz8(s, result);
        s.cc &= static_cast<std::uint8_t>(~(V | C));
        if (value == 0x80) s.cc |= V;
        if (value) s.cc |= C;
        break;
    case 0x3: // COM
        result = static_cast<std::uint8_t>(~value);
        set_nz8(s, result);
        s.cc = static_cast<std::uint8_t>((s.cc & ~(V | C)) | C);
        break;
    case 0x4: // LSR
        result = value >> 1;
        s.cc = static_cast<std::uint8_t>((s.cc & ~(N | Z | V | C)) |
            ((value & 1) ? C : 0) | (!result ? Z : 0) |
            ((value & 1) ? V : 0));
        break;
    case 0x6: // ROR
        result = static_cast<std::uint8_t>((value >> 1) |
            (old_carry ? 0x80 : 0));
        s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) s.cc |= Z;
        if (result & 0x80) s.cc |= N;
        if (value & 1) s.cc |= C;
        if (((result >> 7) ^ (s.cc & C)) & 1) s.cc |= V;
        break;
    case 0x7: // ASR
        result = static_cast<std::uint8_t>((value >> 1) | (value & 0x80));
        s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) s.cc |= Z;
        if (result & 0x80) s.cc |= N;
        if (value & 1) s.cc |= C;
        if (((result >> 7) ^ (s.cc & C)) & 1) s.cc |= V;
        break;
    case 0x8: // ASL
        result = static_cast<std::uint8_t>(value << 1);
        s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) s.cc |= Z;
        if (result & 0x80) s.cc |= N;
        if (value & 0x80) s.cc |= C;
        if (((result >> 7) ^ ((s.cc & C) != 0)) & 1) s.cc |= V;
        break;
    case 0x9: // ROL
        result = static_cast<std::uint8_t>((value << 1) | (old_carry ? 1 : 0));
        s.cc &= static_cast<std::uint8_t>(~(N | Z | V | C));
        if (!result) s.cc |= Z;
        if (result & 0x80) s.cc |= N;
        if (value & 0x80) s.cc |= C;
        if (((result >> 7) ^ ((s.cc & C) != 0)) & 1) s.cc |= V;
        break;
    case 0xa: // DEC
        result = static_cast<std::uint8_t>(value - 1);
        set_nz8(s, result);
        s.cc &= static_cast<std::uint8_t>(~V);
        if (value == 0x80) s.cc |= V;
        break;
    case 0xc: // INC
        result = static_cast<std::uint8_t>(value + 1);
        set_nz8(s, result);
        s.cc &= static_cast<std::uint8_t>(~V);
        if (value == 0x7f) s.cc |= V;
        break;
    case 0xd: // TST
        set_nz8(s, value);
        s.cc &= static_cast<std::uint8_t>(~V);
        break;
    case 0xf: // CLR
        result = 0;
        s.cc = static_cast<std::uint8_t>((s.cc & ~(N | V | C)) | Z);
        break;
    default: break;
    }
    return result;
}

unsigned normalize_unary(unsigned operation) noexcept {
    operation &= 15;
    if (operation == 1) return 0; // NEG alias
    if (operation == 5) return 4; // LSR alias
    return operation;
}

bool supported_unary(unsigned operation) noexcept {
    switch (normalize_unary(operation)) {
    case 0x0: case 0x3: case 0x4: case 0x6: case 0x7: case 0x8:
    case 0x9: case 0xa: case 0xc: case 0xd: case 0xf:
        return true;
    default:
        return false;
    }
}

} // namespace

std::uint32_t service_interrupt(State& s, Bus& bus) noexcept {
    if (!bus.read8 || !bus.write8) return 0;
    std::uint16_t vector = 0;
    bool full_state = false;
    const char* kind = nullptr;
    if (s.nmi) {
        vector = 0xfffc; full_state = true; kind = "NMI"; s.nmi = false;
    } else if (s.firq && !(s.cc & cc_f)) {
        vector = 0xfff6; kind = "FIRQ";
    } else if (s.irq && !(s.cc & cc_i)) {
        vector = 0xfff8; full_state = true; kind = "IRQ";
    } else {
        return 0;
    }
    const auto before_pc = s.pc;
    const auto before_cycles = s.cycles;
    if (full_state) {
        s.cc |= cc_e;
        push16(bus, s.s, s.pc);
        push16(bus, s.s, s.u);
        push16(bus, s.s, s.y);
        push16(bus, s.s, s.x);
        push8(bus, s.s, s.dp);
        push8(bus, s.s, s.b);
        push8(bus, s.s, s.a);
        push8(bus, s.s, s.cc);
    } else {
        s.cc &= static_cast<std::uint8_t>(~cc_e);
        push16(bus, s.s, s.pc);
        push8(bus, s.s, s.cc);
    }
    s.cc |= cc_i;
    if (!full_state) s.cc |= cc_f;
    s.pc = read16(bus, vector);
    const std::uint32_t consumed = full_state ? 19u : 10u;
    s.cycles += consumed;
    multiarcaderecomp_interpreter_trace_event("MC6809E", "mame-interrupt-entry",
        MULTIARCADERECOMP_EXCEPTION_BOUNDARY, before_pc, s.pc, before_cycles,
        s.cycles);
    (void)kind;
    return consumed;
}

BlockExit interpreter_step(State& s, Bus& bus, std::uint64_t stop_cycle,
                          MultiArcadeRecompFallbackReason reason) noexcept {
    if (!bus.read8 || !bus.write8) return BlockExit::BusFault;
    if (s.cycles >= stop_cycle) return BlockExit::Budget;
    const std::uint32_t pc = s.pc;
    const std::uint64_t before = s.cycles;
    std::uint8_t opcode = fetch8(s, bus);
    const std::uint8_t prefix = opcode;
    bool page10 = false, page11 = false;
    if (opcode == 0x10 || opcode == 0x11) {
        page10 = opcode == 0x10;
        page11 = opcode == 0x11;
        opcode = fetch8(s, bus);
    }
    const auto done = [&](const char* mnemonic, unsigned cycles) {
        s.cycles += cycles;
        multiarcaderecomp_interpreter_trace_step("MC6809E", "mame-fallback",
            reason, pc, page10 || page11 ?
                ((std::uint64_t(prefix) << 8) | opcode) : opcode,
            page10 || page11 ? 16u : 8u, before, s.cycles, s.pc, mnemonic);
        return s.cycles >= stop_cycle ? BlockExit::Budget : BlockExit::Dispatch;
    };
    const auto unhandled = [&]() {
        s.pc = static_cast<std::uint16_t>(pc);
        multiarcaderecomp_interpreter_trace_step("MC6809E", "mame-fallback",
            reason, pc, page10 || page11 ?
                ((std::uint64_t(prefix) << 8) | opcode) : opcode,
            page10 || page11 ? 16u : 8u, before, before, pc,
            "<unsupported-opcode>");
        return BlockExit::NeedsInterpreter;
    };

    if (page11 && opcode < 0x80) return unhandled();
    if (page10 && !((opcode >= 0x80 && opcode <= 0xff) ||
                    (opcode >= 0x20 && opcode <= 0x2f)))
        return unhandled();

    if (page10 || page11) {
        if (opcode >= 0x20 && opcode <= 0x2f) {
            const auto disp = static_cast<std::int16_t>(fetch16(s, bus));
            if (branch_condition(s, opcode & 15))
                s.pc = static_cast<std::uint16_t>(s.pc + disp);
            return done("LBcc", 5);
        }
        const unsigned mode = opcode >> 4;
        const unsigned low = opcode & 15;
        const bool immediate = mode == 8 || mode == 12;
        const bool direct = mode == 9 || mode == 13;
        const bool indexed_mode = mode == 10 || mode == 14;
        const bool extended = mode == 11 || mode == 15;
        if (!(immediate || direct || indexed_mode || extended))
            return unhandled();
        std::uint16_t ea = 0;
        if (direct) ea = static_cast<std::uint16_t>((s.dp << 8) | fetch8(s,bus));
        else if (indexed_mode) ea = indexed(s,bus);
        else if (extended) ea = fetch16(s,bus);
        const auto value16 = [&]() { return immediate ? fetch16(s,bus) : read16(bus,ea); };
        if (page11) {
            if (low == 3) { (void)sub16(s,s.u,value16()); return done("CMPU", immediate?5:7); }
            if (low == 12) { (void)sub16(s,s.y,value16()); return done("CMPS", immediate?5:7); }
            return unhandled();
        }
        if (low == 3) { (void)sub16(s,d(s),value16()); return done("CMPD", immediate?5:7); }
        if (low == 12) { (void)sub16(s,s.y,value16()); return done("CMPY", immediate?5:7); }
        if (low == 14) {
            const std::uint16_t value = value16();
            if (mode >= 12) s.s=value;
            else s.y=value;
            set_nz16(s,value); s.cc &= static_cast<std::uint8_t>(~V);
            return done(mode >= 12 ? "LDS" : "LDY", immediate?4:6);
        }
        if (low == 15 && !immediate) {
            const std::uint16_t value = mode >= 12 ? s.s : s.y;
            write16(bus,ea,value); set_nz16(s,value);
            s.cc &= static_cast<std::uint8_t>(~V);
            return done(mode >= 12 ? "STS" : "STY", 6);
        }
        return unhandled();
    }

    if (!page10 && !page11) {
        if (opcode == 0x12) return done("NOP", 2);
        if (opcode == 0x1a) { s.cc |= fetch8(s, bus); return done("ORCC", 3); }
        if (opcode == 0x1c) { s.cc &= fetch8(s, bus); return done("ANDCC", 3); }
        if (opcode == 0x1d) {
            s.a = (s.b & 0x80) ? 0xff : 0; set_nz16(s, d(s));
            return done("SEX", 2);
        }
        if (opcode >= 0x20 && opcode <= 0x2f) {
            const auto disp = static_cast<std::int8_t>(fetch8(s, bus));
            if (branch_condition(s, opcode & 15)) s.pc = static_cast<std::uint16_t>(s.pc + disp);
            return done("Bcc", 3);
        }
        if (opcode >= 0x30 && opcode <= 0x33) {
            const std::uint16_t ea = indexed(s, bus);
            switch (opcode) {
            case 0x30: s.x = ea; s.cc = static_cast<std::uint8_t>(
                (s.cc & ~Z) | (ea == 0 ? Z : 0)); return done("LEAX", 4);
            case 0x31: s.y = ea; s.cc = static_cast<std::uint8_t>(
                (s.cc & ~Z) | (ea == 0 ? Z : 0)); return done("LEAY", 4);
            case 0x32: s.s = ea; return done("LEAS", 4);
            default: s.u = ea; return done("LEAU", 4);
            }
        }
        if (opcode == 0x16) {
            const auto disp = static_cast<std::int16_t>(fetch16(s, bus));
            s.pc = static_cast<std::uint16_t>(s.pc + disp);
            return done("LBRA", 5);
        }
        if (opcode == 0x17) {
            const auto disp = static_cast<std::int16_t>(fetch16(s, bus));
            push16(bus, s.s, s.pc);
            s.pc = static_cast<std::uint16_t>(s.pc + disp);
            return done("LBSR", 9);
        }
        if (opcode >= 0x40 && opcode <= 0x5f) {
            auto& acc = (opcode & 0x10) ? s.b : s.a;
            const unsigned op = opcode & 15;
            if (!supported_unary(op)) return unhandled();
            acc = unary(s, normalize_unary(op), acc);
            return done("UnaryA", 2);
        }
        if (opcode >= 0x00 && opcode <= 0x0f) {
            const std::uint16_t ea = static_cast<std::uint16_t>(
                (s.dp << 8) | fetch8(s, bus));
            if (opcode == 0x0e) { s.pc = ea; return done("JMP", 3); }
            if (!supported_unary(opcode)) return unhandled();
            const auto value = read8(bus, ea);
            const auto result = unary(s, normalize_unary(opcode), value);
            if (opcode != 0x0d) write8(bus, ea, result);
            return done("UnaryDirect", opcode == 0x0d ? 4 : 6);
        }
        if (opcode >= 0x60 && opcode <= 0x6f) {
            const std::uint16_t ea = indexed(s, bus);
            if ((opcode & 15) == 0x0e) { s.pc = ea; return done("JMP", 3); }
            if (!supported_unary(opcode)) return unhandled();
            const auto value = read8(bus, ea);
            const auto result = unary(s, normalize_unary(opcode), value);
            if ((opcode & 15) != 0x0d) write8(bus, ea, result);
            return done("UnaryIndexed", 6);
        }
        if (opcode >= 0x70 && opcode <= 0x7f) {
            const std::uint16_t ea = fetch16(s, bus);
            if ((opcode & 15) == 0x0e) { s.pc = ea; return done("JMP", 4); }
            if (!supported_unary(opcode)) return unhandled();
            const auto value = read8(bus, ea);
            const auto result = unary(s, normalize_unary(opcode), value);
            if ((opcode & 15) != 0x0d) write8(bus, ea, result);
            return done("UnaryExtended", 7);
        }
        if (opcode == 0x1f) {
            const std::uint8_t post = fetch8(s, bus);
            const auto get = [&](unsigned r) -> std::uint16_t {
                switch (r) { case 0: return d(s); case 1: return s.x; case 2: return s.y;
                case 3: return s.u; case 4: return s.s; case 5: return s.pc;
                case 8: return s.a; case 9: return s.b; case 10: return s.cc;
                case 11: return s.dp; default: return 0xffff; }
            };
            const unsigned src = post >> 4, dst = post & 15;
            std::uint16_t val = get(src);
            switch (dst) { case 0: set_d(s,val); break; case 1:s.x=val;break;case 2:s.y=val;break;
            case 3:s.u=val;break;case 4:s.s=val;break;case 5:s.pc=val;break;
            case 8:s.a=static_cast<std::uint8_t>(val);break;case 9:s.b=static_cast<std::uint8_t>(val);break;
            case 10:s.cc=static_cast<std::uint8_t>(val);break;case 11:s.dp=static_cast<std::uint8_t>(val);break;
            default: return unhandled(); }
            return done("TFR", 6);
        }
        if (opcode == 0x39) { s.pc = pull16(bus, s.s); return done("RTS", 5); }
        if (opcode == 0x3a) { s.x = static_cast<std::uint16_t>(s.x + s.b); return done("ABX", 3); }
        if (opcode == 0x3b) {
            s.cc = pull8(bus, s.s);
            unsigned cycles = 6;
            if (s.cc & cc_e) {
                s.a = pull8(bus, s.s);
                s.b = pull8(bus, s.s);
                s.dp = pull8(bus, s.s);
                s.x = pull16(bus, s.s);
                s.y = pull16(bus, s.s);
                s.u = pull16(bus, s.s);
                cycles = 15;
            }
            s.pc = pull16(bus, s.s);
            return done("RTI", cycles);
        }
        if (opcode == 0x3f) {
            s.cc |= cc_e;
            push16(bus, s.s, s.pc);
            push16(bus, s.s, s.u);
            push16(bus, s.s, s.y);
            push16(bus, s.s, s.x);
            push8(bus, s.s, s.dp);
            push8(bus, s.s, s.b);
            push8(bus, s.s, s.a);
            push8(bus, s.s, s.cc);
            s.cc |= cc_i | cc_f;
            s.pc = read16(bus, 0xfffa);
            return done("SWI", 19);
        }
        if (opcode == 0x3d) {
            const std::uint16_t product = std::uint16_t(s.a) * s.b;
            set_d(s, product); s.cc &= static_cast<std::uint8_t>(~(Z | C));
            if (!product) s.cc |= Z; if (product & 0x80) s.cc |= C;
            return done("MUL", 11);
        }
        if (opcode == 0x34 || opcode == 0x35 || opcode == 0x36 || opcode == 0x37) {
            const auto mask = fetch8(s, bus);
            const bool use_s = opcode == 0x34 || opcode == 0x35;
            std::uint16_t& sp = use_s ? s.s : s.u;
            std::uint16_t& other = use_s ? s.u : s.s;
            unsigned bytes = 0;
            if (opcode == 0x34 || opcode == 0x36) {
                if (mask & 0x80) { push16(bus, sp, s.pc); bytes += 2; }
                if (mask & 0x40) { push16(bus, sp, other); bytes += 2; }
                if (mask & 0x20) { push16(bus, sp, s.y); bytes += 2; }
                if (mask & 0x10) { push16(bus, sp, s.x); bytes += 2; }
                if (mask & 0x08) { push8(bus, sp, s.dp); ++bytes; }
                if (mask & 0x04) { push8(bus, sp, s.b); ++bytes; }
                if (mask & 0x02) { push8(bus, sp, s.a); ++bytes; }
                if (mask & 0x01) { push8(bus, sp, s.cc); ++bytes; }
                return done(use_s ? "PSHS" : "PSHU", 5 + bytes);
            }
            if (mask & 0x01) { s.cc=pull8(bus, sp); ++bytes; }
            if (mask & 0x02) { s.a=pull8(bus, sp); ++bytes; }
            if (mask & 0x04) { s.b=pull8(bus, sp); ++bytes; }
            if (mask & 0x08) { s.dp=pull8(bus, sp); ++bytes; }
            if (mask & 0x10) { s.x=pull16(bus, sp); bytes+=2; }
            if (mask & 0x20) { s.y=pull16(bus, sp); bytes+=2; }
            if (mask & 0x40) { other=pull16(bus, sp); bytes+=2; }
            if (mask & 0x80) { s.pc=pull16(bus, sp); bytes+=2; }
            return done(use_s ? "PULS" : "PULU", 5 + bytes);
        }
        if (opcode == 0x8d) {
            const auto disp = static_cast<std::int8_t>(fetch8(s, bus));
            push16(bus, s.s, s.pc); s.pc = static_cast<std::uint16_t>(s.pc + disp);
            return done("BSR", 7);
        }
    }

    // Decode the common A/B arithmetic and load/store families. The AOT
    // translator and fallback share the same board bus and ROM-bank state.
    if (!page10 && !page11 && opcode >= 0x80) {
        const unsigned mode = opcode >> 4;
        const unsigned low = opcode & 15;
        const bool a_group = mode == 8 || mode == 9 || mode == 10 || mode == 11;
        const bool b_group = mode == 12 || mode == 13 || mode == 14 || mode == 15;
        if (a_group || b_group) {
            const bool immediate = mode == 8 || mode == 12;
            const bool direct = mode == 9 || mode == 13;
            const bool indexed_mode = mode == 10 || mode == 14;
            std::uint16_t ea = 0;
            if (direct) ea = static_cast<std::uint16_t>((s.dp << 8) | fetch8(s,bus));
            else if (indexed_mode) ea = indexed(s,bus);
            else if (mode == 11 || mode == 15) ea = fetch16(s,bus);
            auto operand8 = [&]() { return immediate ? fetch8(s,bus) : read8(bus,ea); };
            auto operand16 = [&]() { return immediate ? fetch16(s,bus) : read16(bus,ea); };
            auto& acc = a_group ? s.a : s.b;
            const unsigned base_cycles = immediate ? 2 : direct ? 4 : indexed_mode ? 4 : 5;
            switch (low) {
            case 0: acc = sub8(s, acc, operand8()); return done(a_group?"SUBA":"SUBB",base_cycles);
            case 1: (void)sub8(s, acc, operand8()); return done(a_group?"CMPA":"CMPB",base_cycles);
            case 2: acc = sub8(s, acc, operand8(), s.cc & C); return done(a_group?"SBCA":"SBCB",base_cycles);
            case 3:
                if (a_group) { set_d(s,sub16(s,d(s),operand16())); return done("SUBD",base_cycles+1); }
                set_d(s,add16(s,d(s),operand16())); return done("ADDD",base_cycles+1);
            case 4: acc &= operand8(); set_nz8(s,acc); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"ANDA":"ANDB",base_cycles);
            case 5: { const auto result=static_cast<std::uint8_t>(acc & operand8()); set_nz8(s,result); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"BITA":"BITB",base_cycles); }
            case 6: acc=operand8(); set_nz8(s,acc); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"LDA":"LDB",base_cycles);
            case 7: if (immediate) break; write8(bus,ea,acc); set_nz8(s,acc); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"STA":"STB",base_cycles);
            case 8: acc ^= operand8(); set_nz8(s,acc); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"EORA":"EORB",base_cycles);
            case 9: acc=add8(s,acc,operand8(),s.cc&C); return done(a_group?"ADCA":"ADCB",base_cycles);
            case 10: acc |= operand8(); set_nz8(s,acc); s.cc &= static_cast<std::uint8_t>(~V); return done(a_group?"ORA":"ORB",base_cycles);
            case 11: acc=add8(s,acc,operand8()); return done(a_group?"ADDA":"ADDB",base_cycles);
            case 12:
                if (a_group) { (void)sub16(s,s.x,operand16()); return done("CMPX",base_cycles+1); }
                if (immediate) { const auto value=fetch16(s,bus); set_d(s,value); set_nz16(s,value); s.cc &= static_cast<std::uint8_t>(~V); return done("LDD",base_cycles+1); }
                { const auto value=read16(bus,ea); set_d(s,value); set_nz16(s,value); s.cc &= static_cast<std::uint8_t>(~V); return done("LDD",base_cycles+1); }
            case 13:
                if (a_group) {
                    if (immediate) break;
                    push16(bus, s.s, s.pc); s.pc=ea; return done("JSR",base_cycles+3);
                }
                if (immediate) break;
                write16(bus,ea,d(s)); set_nz16(s,d(s)); s.cc &= static_cast<std::uint8_t>(~V); return done("STD",base_cycles+1);
            case 14:
                if (a_group) { s.x=operand16(); set_nz16(s,s.x); s.cc &= static_cast<std::uint8_t>(~V); return done("LDX",base_cycles+1); }
                if (immediate) { s.u=fetch16(s,bus); set_nz16(s,s.u); s.cc &= static_cast<std::uint8_t>(~V); return done("LDU",base_cycles+1); }
                s.u=read16(bus,ea); set_nz16(s,s.u); s.cc &= static_cast<std::uint8_t>(~V); return done("LDU",base_cycles+1);
            case 15:
                if (immediate) break;
                if (a_group) { write16(bus,ea,s.x); set_nz16(s,s.x); s.cc &= static_cast<std::uint8_t>(~V); return done("STX",base_cycles+1); }
                write16(bus,ea,s.u); set_nz16(s,s.u); s.cc &= static_cast<std::uint8_t>(~V); return done("STU",base_cycles+1);
            }
            s.pc = static_cast<std::uint16_t>(pc);
            return unhandled();
        }
    }

    return unhandled();
}

} // namespace multiarcaderecomp::m6809
