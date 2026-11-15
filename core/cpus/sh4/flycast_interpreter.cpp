// Naomi adapter for Flycast's SH-4 interpreter semantics.
// Upstream sources: core/third_party/flycast-sh4/interpr/.
// This keeps instruction semantics separate from the AOT translator and
// Naomi device runtime. Unsupported families stop explicitly and are logged.

#include "flycast_interpreter.hpp"

#include "naomi/naomi_sh4_context.hpp"
#include "naomi/naomi_sh4_helpers.hpp"
#include "flycast_fpu.hpp"
#include "../interpreter_trace.h"

#include <cstdint>
#include <cfenv>
#include <cstdio>

namespace multiarcaderecomp::cpus::sh4::flycast {
namespace {

class ScopedFpscrRounding {
public:
    ScopedFpscrRounding(const multiarcaderecomp::naomi::Sh4Context& context,
                        std::uint16_t opcode)
        : previous_(std::fegetround()), active_((opcode & 0xf000u) == 0xf000u) {
        if (active_ && previous_ != -1) {
            const int requested = (context.fpscr & 3u) == 1u
                ? FE_TOWARDZERO : FE_TONEAREST;
            if (previous_ != requested && std::fesetround(requested) == 0)
                changed_ = true;
        }
    }

    ~ScopedFpscrRounding() {
        if (changed_) std::fesetround(previous_);
    }

private:
    int previous_ = -1;
    bool active_ = false;
    bool changed_ = false;
};

std::int32_t sign_extend8(std::uint32_t v) {
    return static_cast<std::int8_t>(v & 0xffu);
}

std::int32_t sign_extend12(std::uint32_t v) {
    v &= 0xfffu;
    return (v & 0x800u) ? static_cast<std::int32_t>(v) - 0x1000
                        : static_cast<std::int32_t>(v);
}

bool step_instruction(multiarcaderecomp::naomi::Sh4Context& c, bool delay_slot,
                      std::uint16_t prefetched_opcode = 0,
                      bool has_prefetched_opcode = false) {
    const auto instruction_pc = c.pc;
    std::uint16_t op = prefetched_opcode;
    if (!has_prefetched_opcode && !c.fetchInstruction16(instruction_pc, op))
        return false;
    if (!c.retire_opcode(op, true)) return false;
    c.pc = instruction_pc + 2;
    const unsigned n = (op >> 8) & 0xf;
    const unsigned m = (op >> 4) & 0xf;
    bool handled = true;
    MultiArcadeRecompFallbackReason unhandled_reason = MULTIARCADERECOMP_UNSUPPORTED_OPCODE;
    const char* unhandled_detail = "unimplemented SH-4 operation";

    // Delay-slot instructions are interpreted by this same one-step engine.
    // Interrupts are sampled only outside a delay slot, matching the AOT path.
    const bool old_delay_slot = c.delay_slot;
    c.delay_slot = delay_slot;

    // Flycast raises FPU-disabled before dispatching any FPU-family opcode.
    // Exception entry is board/runtime work and is not yet wired here, so
    // stop at the exact instruction instead of mutating FPU state silently.
    if ((c.sr & 0x8000u) && (op & 0xf000u) == 0xf000u) {
        c.delay_slot = old_delay_slot;
        const auto event = delay_slot ? 0x820u : 0x800u;
        const auto exception_pc = delay_slot ? instruction_pc - 2u : instruction_pc;
        multiarcaderecomp::naomi::host_raise_exception(c, event, exception_pc, 0x100u);
        std::fprintf(stderr,
                     "[SH-4 interpreter] PC=%08x OPCODE=%04x CATEGORY=EXCEPTION_BOUNDARY event=%03x\n",
                     instruction_pc, op, event);
        multiarcaderecomp_interpreter_trace_step(
            "SH-4", "Flycast-interpreter", MULTIARCADERECOMP_EXCEPTION_BOUNDARY,
            instruction_pc, op, 16, 0, 1, c.pc, "FPU disabled exception");
        return !c.execution_stopped;
    }
    const ScopedFpscrRounding rounding(c, op);
    switch (op) {
    case 0x0000: // NOP0 alias (Flycast opcode table: Looney Tunes, Samba de Amigo 2000)
    case 0x0009: // NOP
        break;
    case 0x000b: { // RTS, delayed
        if (delay_slot) { handled = false; break; }
        const auto target = c.pr;
        c.control_target = target;
        c.delay_slot = true;
        if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
        c.delay_slot = false;
        c.pc = target;
        break;
    }
    case 0x002b: { // RTE, delayed; fetch under old MD, execute under restored SSR.
        if (delay_slot) { handled = false; break; }
        const auto target = c.spc;
        const auto interrupted_sr = multiarcaderecomp::naomi::host_store_sr(c);
        c.control_target = target;
        // Flycast's ExecuteDelayslot_RTE fetches under the interrupted mode,
        // restores SR, then executes the already-fetched opcode. With MMU
        // translation, the fetch mode changes which mapping is selected.
        std::uint16_t delay_opcode = 0;
        c.delay_slot = true;
        if (!c.fetchInstruction16(instruction_pc + 2u, delay_opcode))
            return false;
        multiarcaderecomp::naomi::load_sr(c, c.ssr);
        if (!step_instruction(c, true, delay_opcode, true)) {
            // Unsupported instruction variants are declined before retiring
            // the delay slot. Undo the SR/RB transition and leave RTE intact
            // so the dispatcher reports the actual fallback boundary.
            multiarcaderecomp::naomi::load_sr(c, interrupted_sr);
            c.pc = instruction_pc;
            c.delay_slot = old_delay_slot;
            return false;
        }
        c.delay_slot = false;
        c.pc = target;
        // RTE changes control flow outside the translated call chain. The
        // dispatcher resumes at SPC and checks interrupts at the next slice.
        c.interrupt_taken = true;
        break;
    }
    case 0x0028: // CLRMAC
        c.mach = c.macl = 0;
        break;
    case 0x0008: c.t = false; break; // CLRT
    case 0x0018: c.t = true; break;  // SETT
    case 0x0048: c.sr &= ~(1u << 1); break; // CLRS
    case 0x0058: c.sr |= 1u << 1; break;    // SETS
    case 0x0038: { // LDTLB, Flycast sh4_opcodes.cpp
        const auto mmucr = c.read32(0xff000010u);
        const auto index = (mmucr >> 10u) & 0x3fu; // MMUCR.URC
        c.utlb[index] = {
            c.read32(0xff000000u), // CCN.PTEH
            c.read32(0xff000004u), // CCN.PTEL
            c.read32(0xff000034u)  // CCN.PTEA
        };
        break;
    }
    case 0x001b: // SLEEP; dispatcher advances device time until an interrupt wakes SH-4
        if (delay_slot) handled = false;
        else c.sleeping = true;
        break;
    default:
        if (!delay_slot && (op & 0xf000u) == 0xa000u) { // BRA disp12
            const auto target = instruction_pc + 4u +
                static_cast<std::uint32_t>(sign_extend12(op) * 2);
            c.control_target = target;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pc = target;
        } else if (!delay_slot && (op & 0xf000u) == 0xb000u) { // BSR disp12
            const auto target = instruction_pc + 4u +
                static_cast<std::uint32_t>(sign_extend12(op) * 2);
            c.control_target = target;
            const auto link = instruction_pc + 4u;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pr = link;
            c.pc = target;
        } else if (!delay_slot && (op & 0xf0ffu) == 0x402bu) { // JMP @Rn
            const auto target = c.r[n];
            c.control_target = target;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pc = target;
        } else if (!delay_slot && (op & 0xf0ffu) == 0x400bu) { // JSR @Rn
            const auto target = c.r[n];
            c.control_target = target;
            const auto link = instruction_pc + 4u;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pr = link;
            c.pc = target;
        } else if (!delay_slot && (op & 0xf0ffu) == 0x0023u) { // BRAF Rn
            const auto target = instruction_pc + 4u + c.r[n];
            c.control_target = target;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pc = target;
        } else if (!delay_slot && (op & 0xf0ffu) == 0x0003u) { // BSRF Rn
            const auto target = instruction_pc + 4u + c.r[n];
            c.control_target = target;
            const auto link = instruction_pc + 4u;
            if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
            c.pr = link;
            c.pc = target;
        } else if (!delay_slot && (op & 0xff00u) == 0x8b00u) { // BF
            if (!c.t) c.pc = instruction_pc + 4u + static_cast<std::uint32_t>(sign_extend8(op) * 2);
        } else if (!delay_slot && (op & 0xff00u) == 0x8900u) { // BT
            if (c.t) c.pc = instruction_pc + 4u + static_cast<std::uint32_t>(sign_extend8(op) * 2);
        } else if (!delay_slot && (op & 0xff00u) == 0x8f00u) { // BF/S
            if (!c.t) {
                const auto target = instruction_pc + 4u + static_cast<std::uint32_t>(sign_extend8(op) * 2);
                c.control_target = target;
                if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
                c.pc = target;
            }
        } else if (!delay_slot && (op & 0xff00u) == 0x8d00u) { // BT/S
            if (c.t) {
                const auto target = instruction_pc + 4u + static_cast<std::uint32_t>(sign_extend8(op) * 2);
                c.control_target = target;
                if (!step_instruction(c, true)) { c.delay_slot = old_delay_slot; return false; }
                c.pc = target;
            }
        } else if ((op & 0xf000u) == 0xe000u) { // MOV #imm,Rn
            c.r[n] = static_cast<std::uint32_t>(sign_extend8(op));
        } else if ((op & 0xf00fu) == 0x6003u) { // MOV Rm,Rn
            c.r[n] = c.r[m];
        } else if ((op & 0xf00fu) == 0x300cu) { // ADD Rm,Rn
            c.r[n] += c.r[m];
        } else if ((op & 0xf00fu) == 0x300eu) { // ADDC Rm,Rn
            const auto addend = static_cast<std::uint64_t>(c.r[m]) + (c.t ? 1u : 0u);
            const auto sum = static_cast<std::uint64_t>(c.r[n]) + addend;
            c.r[n] = static_cast<std::uint32_t>(sum);
            c.t = (sum >> 32) != 0;
        } else if ((op & 0xf00fu) == 0x3008u) { // SUB Rm,Rn
            c.r[n] -= c.r[m];
        } else if ((op & 0xf00fu) == 0x300au) { // SUBC Rm,Rn
            const auto borrow = c.t ? 1u : 0u;
            const auto subtrahend = static_cast<std::uint64_t>(c.r[m]) + borrow;
            const auto before = c.r[n];
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(before) - subtrahend);
            c.t = static_cast<std::uint64_t>(before) < subtrahend;
        } else if ((op & 0xf00fu) == 0x300bu) { // SUBV Rm,Rn
            const auto lhs = c.r[n];
            const auto rhs = c.r[m];
            const auto result = lhs - rhs;
            c.t = (((lhs ^ rhs) & (lhs ^ result)) >> 31) != 0;
            c.r[n] = result;
        } else if ((op & 0xf00fu) == 0x300fu) { // ADDV Rm,Rn
            const auto lhs = c.r[n];
            const auto rhs = c.r[m];
            const auto result = lhs + rhs;
            c.t = (((~(lhs ^ rhs)) & (lhs ^ result)) >> 31) != 0;
            c.r[n] = result;
        } else if ((op & 0xf00fu) == 0x3000u) { // CMP/EQ Rm,Rn
            c.t = c.r[n] == c.r[m];
        } else if ((op & 0xf00fu) == 0x3002u) { // CMP/HS Rm,Rn
            c.t = c.r[n] >= c.r[m];
        } else if ((op & 0xf00fu) == 0x3003u) { // CMP/GE Rm,Rn
            c.t = static_cast<std::int32_t>(c.r[n]) >= static_cast<std::int32_t>(c.r[m]);
        } else if ((op & 0xf00fu) == 0x3006u) { // CMP/HI Rm,Rn
            c.t = c.r[n] > c.r[m];
        } else if ((op & 0xf00fu) == 0x3007u) { // CMP/GT Rm,Rn
            c.t = static_cast<std::int32_t>(c.r[n]) > static_cast<std::int32_t>(c.r[m]);
        } else if ((op & 0xf00fu) == 0x200cu) { // CMP/STR Rm,Rn
            const auto x = c.r[n] ^ c.r[m];
            c.t = (((x - 0x01010101u) & ~x & 0x80808080u) != 0);
        } else if ((op & 0xf00fu) == 0x200du) { // XTRCT Rm,Rn
            c.r[n] = (c.r[n] >> 16) | (c.r[m] << 16);
        } else if ((op & 0xf00fu) == 0x2009u) { // AND Rm,Rn
            c.r[n] &= c.r[m];
        } else if ((op & 0xf00fu) == 0x200au) { // XOR Rm,Rn
            c.r[n] ^= c.r[m];
        } else if ((op & 0xf00fu) == 0x200bu) { // OR Rm,Rn
            c.r[n] |= c.r[m];
        } else if ((op & 0xf00fu) == 0x2008u) { // TST Rm,Rn
            c.t = (c.r[n] & c.r[m]) == 0;
        } else if ((op & 0xf00fu) == 0x200eu) { // MULU.W
            c.macl = (c.r[n] & 0xffffu) * (c.r[m] & 0xffffu);
        } else if ((op & 0xf00fu) == 0x200fu) { // MULS.W
            c.macl = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.r[n])) *
                                                static_cast<std::int32_t>(static_cast<std::int16_t>(c.r[m])));
        } else if ((op & 0xf00fu) == 0x0007u) { // MUL.L
            c.macl = c.r[n] * c.r[m];
        } else if ((op & 0xf00fu) == 0x000fu) { // MAC.L @Rm+,@Rn+
            if (c.sr & 0x00000002u) {
                handled = false;
                unhandled_reason = MULTIARCADERECOMP_UNSUPPORTED_VARIANT;
                unhandled_detail = "Flycast MAC.L handler requires SR.S == 0";
            } else {
                // Preserve Flycast's bus-read order: @Rm+ first, then @Rn+
                // (with the overlap offset when both operands use one reg).
                const auto rhs = static_cast<std::int32_t>(c.read32(c.r[m]));
                const auto lhs = static_cast<std::int32_t>(
                    c.read32(c.r[n] + (n == m ? 4u : 0u)));
                c.r[n] += 4;
                c.r[m] += 4;
                const auto acc = (static_cast<std::uint64_t>(c.mach) << 32) | c.macl;
                const auto product = static_cast<std::int64_t>(lhs) * rhs;
                const auto result = acc + static_cast<std::uint64_t>(product);
                c.macl = static_cast<std::uint32_t>(result);
                c.mach = static_cast<std::uint32_t>(result >> 32);
            }
        } else if ((op & 0xf00fu) == 0x3005u) { // DMULU.L
            const auto product = static_cast<std::uint64_t>(c.r[n]) * c.r[m];
            c.macl = static_cast<std::uint32_t>(product);
            c.mach = static_cast<std::uint32_t>(product >> 32);
        } else if ((op & 0xf00fu) == 0x300du) { // DMULS.L
            const auto product = static_cast<std::int64_t>(static_cast<std::int32_t>(c.r[n])) *
                                 static_cast<std::int64_t>(static_cast<std::int32_t>(c.r[m]));
            c.macl = static_cast<std::uint32_t>(product);
            c.mach = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
        } else if ((op & 0xf00fu) == 0x3004u) { // DIV1
            multiarcaderecomp::naomi::host_div1(c, n, m);
        } else if ((op & 0xf00fu) == 0x6007u) { // NOT Rm,Rn
            c.r[n] = ~c.r[m];
        } else if ((op & 0xf00fu) == 0x6008u) { // SWAP.B Rm,Rn
            const auto value = c.r[m];
            c.r[n] = (value & 0xffff0000u) | ((value & 0xffu) << 8) | ((value >> 8) & 0xffu);
        } else if ((op & 0xf00fu) == 0x6009u) { // SWAP.W Rm,Rn
            const auto value = c.r[m];
            c.r[n] = (value << 16) | (value >> 16);
        } else if ((op & 0xf00fu) == 0x600au) { // NEGC Rm,Rn
            const auto value = 0u - c.r[m];
            c.r[n] = value - (c.t ? 1u : 0u);
            c.t = value != 0 || value < c.r[n];
        } else if ((op & 0xf00fu) == 0x600bu) { // NEG Rm,Rn
            c.r[n] = 0u - c.r[m];
        } else if ((op & 0xf00fu) == 0x600cu) { // EXTU.B
            c.r[n] = c.r[m] & 0xffu;
        } else if ((op & 0xf00fu) == 0x600du) { // EXTU.W
            c.r[n] = c.r[m] & 0xffffu;
        } else if ((op & 0xf00fu) == 0x600eu) { // EXTS.B
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.r[m])));
        } else if ((op & 0xf00fu) == 0x600fu) { // EXTS.W
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.r[m])));
        } else if ((op & 0xf0ffu) == 0x0002u) { // STC SR,Rn
            c.r[n] = multiarcaderecomp::naomi::host_store_sr(c);
        } else if ((op & 0xf0ffu) == 0x0012u) { // STC GBR,Rn
            c.r[n] = c.gbr;
        } else if ((op & 0xf0ffu) == 0x0022u) { // STC VBR,Rn
            c.r[n] = c.vbr;
        } else if ((op & 0xf0ffu) == 0x0029u) { // MOVT Rn
            c.r[n] = c.t ? 1u : 0u;
        } else if ((op & 0xf0ffu) == 0x0032u) { // STC SSR,Rn
            c.r[n] = c.ssr;
        } else if ((op & 0xf0ffu) == 0x003au) { // STC SGR,Rn
            c.r[n] = c.sgr;
        } else if ((op & 0xf0ffu) == 0x0042u) { // STC SPC,Rn
            c.r[n] = c.spc;
        } else if ((op & 0xf0ffu) == 0x00fau) { // STC DBR,Rn
            c.r[n] = c.dbr;
        } else if ((op & 0xf08fu) == 0x0082u) { // STC Rm_BANK,Rn
            c.r[n] = c.r_bank[m & 7u];
        } else if ((op & 0xf0ffu) == 0x000au) { // STS MACH,Rn
            c.r[n] = c.mach;
        } else if ((op & 0xf0ffu) == 0x001au) { // STS MACL,Rn
            c.r[n] = c.macl;
        } else if ((op & 0xf0ffu) == 0x002au) { // STS PR,Rn
            c.r[n] = c.pr;
        } else if ((op & 0xf0ffu) == 0x005au) { // STS FPUL,Rn
            c.r[n] = c.fpul;
        } else if ((op & 0xf0ffu) == 0x006au) { // STS FPSCR,Rn
            c.r[n] = c.fpscr;
        } else if ((op & 0xf0ffu) == 0x400eu) { // LDC Rn,SR
            multiarcaderecomp::naomi::load_sr(c, c.r[n]);
        } else if ((op & 0xf0ffu) == 0x401eu) { // LDC Rn,GBR
            c.gbr = c.r[n];
        } else if ((op & 0xf0ffu) == 0x402eu) { // LDC Rn,VBR
            c.vbr = c.r[n];
        } else if ((op & 0xf0ffu) == 0x403eu) { // LDC Rn,SSR
            c.ssr = c.r[n];
        } else if ((op & 0xf0ffu) == 0x404eu) { // LDC Rn,SPC
            c.spc = c.r[n];
        } else if ((op & 0xf0ffu) == 0x403au) { // LDC Rn,SGR
            c.sgr = c.r[n];
        } else if ((op & 0xf0ffu) == 0x40fau) { // LDC Rn,DBR
            c.dbr = c.r[n];
        } else if ((op & 0xf08fu) == 0x408eu) { // LDC Rm_BANK,Rn
            c.r_bank[m & 7u] = c.r[n];
        } else if ((op & 0xf0ffu) == 0x400au) { // LDS Rn,MACH
            c.mach = c.r[n];
        } else if ((op & 0xf0ffu) == 0x401au) { // LDS Rn,MACL
            c.macl = c.r[n];
        } else if ((op & 0xf0ffu) == 0x402au) { // LDS Rn,PR
            c.pr = c.r[n];
        } else if ((op & 0xf0ffu) == 0x405au) { // LDS Rn,FPUL
            c.fpul = c.r[n];
        } else if ((op & 0xf0ffu) == 0x406au) { // LDS Rn,FPSCR
            fpu::load_fpscr(c, c.r[n]);
        } else if ((op & 0xf0ffu) == 0x4003u) { // STC.L SR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], multiarcaderecomp::naomi::host_store_sr(c));
        } else if ((op & 0xf0ffu) == 0x4013u) { // STC.L GBR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.gbr);
        } else if ((op & 0xf0ffu) == 0x4023u) { // STC.L VBR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.vbr);
        } else if ((op & 0xf0ffu) == 0x4033u) { // STC.L SSR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.ssr);
        } else if ((op & 0xf0ffu) == 0x4043u) { // STC.L SPC,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.spc);
        } else if ((op & 0xf0ffu) == 0x4032u) { // STC.L SGR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.sgr);
        } else if ((op & 0xf0ffu) == 0x40f2u) { // STC.L DBR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.dbr);
        } else if ((op & 0xf08fu) == 0x4083u) { // STC.L Rm_BANK,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.r_bank[m & 7u]);
        } else if ((op & 0xf0ffu) == 0x4002u) { // STS.L MACH,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.mach);
        } else if ((op & 0xf0ffu) == 0x4012u) { // STS.L MACL,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.macl);
        } else if ((op & 0xf0ffu) == 0x4022u) { // STS.L PR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.pr);
        } else if ((op & 0xf0ffu) == 0x4052u) { // STS.L FPUL,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.fpul);
        } else if ((op & 0xf0ffu) == 0x4062u) { // STS.L FPSCR,@-Rn
            c.r[n] -= 4; c.write32(c.r[n], c.fpscr);
        } else if ((op & 0xf0ffu) == 0x4007u) { // LDC.L @Rn+,SR
            const auto value = c.read32(c.r[n]); c.r[n] += 4;
            multiarcaderecomp::naomi::load_sr(c, value);
        } else if ((op & 0xf0ffu) == 0x4017u) { // LDC.L @Rn+,GBR
            c.gbr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4027u) { // LDC.L @Rn+,VBR
            c.vbr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4037u) { // LDC.L @Rn+,SSR
            c.ssr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4047u) { // LDC.L @Rn+,SPC
            c.spc = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4036u) { // LDC.L @Rn+,SGR
            c.sgr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x40f6u) { // LDC.L @Rn+,DBR
            c.dbr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf08fu) == 0x4087u) { // LDC.L @Rn+,Rm_BANK
            c.r_bank[m & 7u] = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4006u) { // LDS.L @Rn+,MACH
            c.mach = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4016u) { // LDS.L @Rn+,MACL
            c.macl = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4026u) { // LDS.L @Rn+,PR
            c.pr = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4056u) { // LDS.L @Rn+,FPUL
            c.fpul = c.read32(c.r[n]); c.r[n] += 4;
        } else if ((op & 0xf0ffu) == 0x4066u) { // LDS.L @Rn+,FPSCR
            const auto value = c.read32(c.r[n]); c.r[n] += 4;
            fpu::load_fpscr(c, value);
        } else if ((op & 0xf0ffu) == 0x4010u) { // DT Rn
            --c.r[n]; c.t = c.r[n] == 0;
        } else if ((op & 0xf0ffu) == 0x4011u) { // CMP/PZ Rn
            c.t = static_cast<std::int32_t>(c.r[n]) >= 0;
        } else if ((op & 0xf0ffu) == 0x4015u) { // CMP/PL Rn
            c.t = static_cast<std::int32_t>(c.r[n]) > 0;
        } else if ((op & 0xf0ffu) == 0x4000u) { // SHLL Rn
            c.t = (c.r[n] >> 31) != 0; c.r[n] <<= 1;
        } else if ((op & 0xf0ffu) == 0x4001u) { // SHLR Rn
            c.t = (c.r[n] & 1) != 0; c.r[n] >>= 1;
        } else if ((op & 0xf0ffu) == 0x4020u) { // SHAL Rn
            c.t = (c.r[n] >> 31) != 0; c.r[n] <<= 1;
        } else if ((op & 0xf0ffu) == 0x4021u) { // SHAR Rn
            c.t = (c.r[n] & 1) != 0; c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(c.r[n]) >> 1);
        } else if ((op & 0xf0ffu) == 0x4004u) { // ROTL Rn
            c.t = (c.r[n] >> 31) != 0; c.r[n] = (c.r[n] << 1) | (c.r[n] >> 31);
        } else if ((op & 0xf0ffu) == 0x4005u) { // ROTR Rn
            c.t = (c.r[n] & 1) != 0; c.r[n] = (c.r[n] >> 1) | (c.r[n] << 31);
        } else if ((op & 0xf0ffu) == 0x4024u) { // ROTCL Rn
            const auto carry = c.t ? 1u : 0u;
            c.t = (c.r[n] >> 31) != 0; c.r[n] = (c.r[n] << 1) | carry;
        } else if ((op & 0xf0ffu) == 0x4025u) { // ROTCR Rn
            const auto carry = c.t ? 0x80000000u : 0u;
            c.t = (c.r[n] & 1) != 0; c.r[n] = (c.r[n] >> 1) | carry;
        } else if ((op & 0xf0ffu) == 0x4028u) c.r[n] <<= 16;
        else if ((op & 0xf0ffu) == 0x4018u) c.r[n] <<= 8;
        else if ((op & 0xf0ffu) == 0x4008u) c.r[n] <<= 2;
        else if ((op & 0xf0ffu) == 0x4029u) c.r[n] >>= 16;
        else if ((op & 0xf0ffu) == 0x4019u) c.r[n] >>= 8;
        else if ((op & 0xf0ffu) == 0x4009u) c.r[n] >>= 2;
        else if ((op & 0xf000u) == 0x7000u) { // ADD #imm,Rn
            c.r[n] += static_cast<std::uint32_t>(sign_extend8(op));
        } else if ((op & 0xf00fu) == 0x400cu) { // SHAD Rm,Rn
            const auto shift = static_cast<std::int32_t>(c.r[m]);
            if (shift >= 0) c.r[n] <<= (static_cast<unsigned>(shift) & 31u);
            else {
                const auto amount = static_cast<unsigned>(-static_cast<std::int64_t>(shift)) & 31u;
                c.r[n] = amount == 0 ? ((c.r[n] & 0x80000000u) ? 0xffffffffu : 0u)
                                     : static_cast<std::uint32_t>(static_cast<std::int32_t>(c.r[n]) >> amount);
            }
        } else if ((op & 0xf00fu) == 0x400du) { // SHLD Rm,Rn
            const auto shift = static_cast<std::int32_t>(c.r[m]);
            if (shift >= 0) c.r[n] <<= (static_cast<unsigned>(shift) & 31u);
            else {
                const auto amount = static_cast<unsigned>(-static_cast<std::int64_t>(shift)) & 31u;
                c.r[n] = amount == 0 ? 0u : c.r[n] >> amount;
            }
        } else if ((op & 0xf0ffu) == 0x401bu) { // TAS.B @Rn
            const auto value = c.read8(c.r[n]);
            c.t = value == 0;
            c.write8(c.r[n], static_cast<std::uint8_t>(value | 0x80u));
        } else if ((op & 0xf0ffu) == 0x0083u) { // PREF @Rn
            multiarcaderecomp::naomi::host_pref(c, c.r[n]);
        } else if ((op & 0xf0ffu) == 0x0093u || // OCBI @Rn
                   (op & 0xf0ffu) == 0x00a3u || // OCBP @Rn
                   (op & 0xf0ffu) == 0x00b3u || // OCBWB @Rn
                   (op & 0xf0ffu) == 0x00e3u) { // ICBI @Rn
            const bool instruction_cache_invalidate = (op & 0xf0ffu) == 0x00e3u;
            if (instruction_cache_invalidate) {
                const auto physical = c.r[n] & 0x1fffffffu;
                c.invalidated_aot_lines.insert(physical & ~0x1fu);
            }
            // The Naomi memory map is write-through in this prototype, so
            // OCB* has no separate data-cache payload. Keep the callback seam
            // for board models that need writeback/invalidation side effects.
            if (c.cache_operation_handler &&
                !c.cache_operation_handler(c, op, c.r[n])) {
                handled = false;
                unhandled_reason = MULTIARCADERECOMP_MEMORY_ACCESS;
                unhandled_detail = "board rejected SH-4 cache operation";
            }
        } else if ((op & 0xf0ffu) == 0x00c3u) { // MOVCA.L R0,@Rn
            c.write32(c.r[n], c.r[0]);
        } else if ((op & 0xff00u) == 0xc300u) { // TRAPA #imm
            const auto event = 0x160u;
            const auto tra = (op & 0xffu) << 2;
            multiarcaderecomp::naomi::host_raise_exception(c, event, instruction_pc + 2u,
                                                     0x100u, tra);
        } else if ((op & 0xf00fu) == 0x400fu) { // MAC.W @Rm+,@Rn+
            if (c.sr & 0x00000002u) {
                handled = false;
                unhandled_reason = MULTIARCADERECOMP_UNSUPPORTED_VARIANT;
                unhandled_detail = "Flycast MAC.W handler rejects SR.S != 0";
            } else {
                const auto lhs = static_cast<std::int16_t>(c.read16(c.r[n]));
                const auto rhs = static_cast<std::int16_t>(c.read16(c.r[m] + (n == m ? 2u : 0u)));
                c.r[n] += 2;
                c.r[m] += 2;
                const auto acc = (static_cast<std::uint64_t>(c.mach) << 32) | c.macl;
                const auto product = static_cast<std::int32_t>(lhs) * rhs;
                const auto result = acc + static_cast<std::uint64_t>(static_cast<std::int64_t>(product));
                c.macl = static_cast<std::uint32_t>(result);
                c.mach = static_cast<std::uint32_t>(result >> 32);
            }
        } else if (op == 0x0019u) { // DIV0U
            c.t = c.q = c.m_bit = false;
        } else if ((op & 0xf00fu) == 0x2007u) { // DIV0S Rm,Rn
            c.q = (c.r[n] >> 31) != 0;
            c.m_bit = (c.r[m] >> 31) != 0;
            c.t = c.q != c.m_bit;
        } else if ((op & 0xff00u) == 0xc800u) { // TST #imm,R0
            c.t = (c.r[0] & (op & 0xffu)) == 0;
        } else if ((op & 0xff00u) == 0xc900u) { // AND #imm,R0
            c.r[0] &= op & 0xffu;
        } else if ((op & 0xff00u) == 0xca00u) { // XOR #imm,R0
            c.r[0] ^= op & 0xffu;
        } else if ((op & 0xff00u) == 0xcb00u) { // OR #imm,R0
            c.r[0] |= op & 0xffu;
        } else if ((op & 0xff00u) == 0xcc00u) { // TST.B #imm,@(R0,GBR)
            c.t = (c.read8(c.gbr + c.r[0]) & (op & 0xffu)) == 0;
        } else if ((op & 0xff00u) == 0xcd00u) { // AND.B #imm,@(R0,GBR)
            const auto address = c.gbr + c.r[0];
            c.write8(address, static_cast<std::uint8_t>(c.read8(address) & (op & 0xffu)));
        } else if ((op & 0xff00u) == 0xce00u) { // XOR.B #imm,@(R0,GBR)
            const auto address = c.gbr + c.r[0];
            c.write8(address, static_cast<std::uint8_t>(c.read8(address) ^ (op & 0xffu)));
        } else if ((op & 0xff00u) == 0xcf00u) { // OR.B #imm,@(R0,GBR)
            const auto address = c.gbr + c.r[0];
            c.write8(address, static_cast<std::uint8_t>(c.read8(address) | (op & 0xffu)));
        } else if ((op & 0xf00fu) == 0x6002u) { // MOV.L @Rm,Rn
            c.r[n] = c.read32(c.r[m]);
        } else if ((op & 0xf00fu) == 0x2002u) { // MOV.L Rm,@Rn
            c.write32(c.r[n], c.r[m]);
        } else if ((op & 0xf00fu) == 0x0004u) { // MOV.B Rm,@(R0,Rn)
            c.write8(c.r[0] + c.r[n], static_cast<std::uint8_t>(c.r[m]));
        } else if ((op & 0xf00fu) == 0x0005u) { // MOV.W Rm,@(R0,Rn)
            c.write16(c.r[0] + c.r[n], static_cast<std::uint16_t>(c.r[m]));
        } else if ((op & 0xf00fu) == 0x0006u) { // MOV.L Rm,@(R0,Rn)
            c.write32(c.r[0] + c.r[n], c.r[m]);
        } else if ((op & 0xf00fu) == 0x000cu) { // MOV.B @(R0,Rm),Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.read8(c.r[0] + c.r[m]))));
        } else if ((op & 0xf00fu) == 0x000du) { // MOV.W @(R0,Rm),Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(c.r[0] + c.r[m]))));
        } else if ((op & 0xf00fu) == 0x000eu) { // MOV.L @(R0,Rm),Rn
            c.r[n] = c.read32(c.r[0] + c.r[m]);
        } else if ((op & 0xf00fu) == 0x6000u) { // MOV.B @Rm,Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.read8(c.r[m]))));
        } else if ((op & 0xf00fu) == 0x6001u) { // MOV.W @Rm,Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(c.r[m]))));
        } else if ((op & 0xf00fu) == 0x6004u) { // MOV.B @Rm+,Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.read8(c.r[m]))));
            if (n != m) ++c.r[m];
        } else if ((op & 0xf00fu) == 0x6005u) { // MOV.W @Rm+,Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(c.r[m]))));
            if (n != m) c.r[m] += 2;
        } else if ((op & 0xf00fu) == 0x2000u) { // MOV.B Rm,@Rn
            c.write8(c.r[n], static_cast<std::uint8_t>(c.r[m]));
        } else if ((op & 0xf00fu) == 0x2001u) { // MOV.W Rm,@Rn
            c.write16(c.r[n], static_cast<std::uint16_t>(c.r[m]));
        } else if ((op & 0xf00fu) == 0x2004u) { // MOV.B Rm,@-Rn
            const auto value = static_cast<std::uint8_t>(c.r[m]);
            --c.r[n]; c.write8(c.r[n], value);
        } else if ((op & 0xf00fu) == 0x2005u) { // MOV.W Rm,@-Rn
            const auto value = static_cast<std::uint16_t>(c.r[m]);
            c.r[n] -= 2; c.write16(c.r[n], value);
        } else if ((op & 0xf00fu) == 0x6006u) { // MOV.L @Rm+,Rn
            c.r[n] = c.read32(c.r[m]);
            if (n != m) c.r[m] += 4;
        } else if ((op & 0xf00fu) == 0x2006u) { // MOV.L Rm,@-Rn
            const auto value = c.r[m];
            c.r[n] -= 4; c.write32(c.r[n], value);
        } else if ((op & 0xf000u) == 0x1000u) { // MOV.L Rm,@(disp,Rn)
            c.write32(c.r[n] + ((op & 0xfu) * 4u), c.r[m]);
        } else if ((op & 0xf000u) == 0x5000u) { // MOV.L @(disp,Rm),Rn
            c.r[n] = c.read32(c.r[m] + ((op & 0xfu) * 4u));
        } else if ((op & 0xff00u) == 0x8000u) { // MOV.B R0,@(disp,Rm)
            c.write8(c.r[m] + (op & 0xfu), static_cast<std::uint8_t>(c.r[0]));
        } else if ((op & 0xff00u) == 0x8100u) { // MOV.W R0,@(disp,Rm)
            c.write16(c.r[m] + ((op & 0xfu) * 2u), static_cast<std::uint16_t>(c.r[0]));
        } else if ((op & 0xff00u) == 0x8400u) { // MOV.B @(disp,Rm),R0
            c.r[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.read8(c.r[m] + (op & 0xfu)))));
        } else if ((op & 0xff00u) == 0x8500u) { // MOV.W @(disp,Rm),R0
            c.r[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(c.r[m] + ((op & 0xfu) * 2u)))));
        } else if ((op & 0xf000u) == 0x9000u) { // MOV.W @(disp,PC),Rn
            c.r[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(instruction_pc + 4 + ((op & 0xffu) * 2u)))));
        } else if ((op & 0xf000u) == 0xd000u) { // MOV.L @(disp,PC),Rn
            const auto address = ((instruction_pc + 4u) & ~3u) + ((op & 0xffu) * 4u);
            c.r[n] = c.read32(address);
        } else if ((op & 0xff00u) == 0xc000u) { // MOV.B R0,@(disp,GBR)
            c.write8(c.gbr + (op & 0xffu), static_cast<std::uint8_t>(c.r[0]));
        } else if ((op & 0xff00u) == 0xc100u) { // MOV.W R0,@(disp,GBR)
            c.write16(c.gbr + ((op & 0xffu) * 2u), static_cast<std::uint16_t>(c.r[0]));
        } else if ((op & 0xff00u) == 0xc200u) { // MOV.L R0,@(disp,GBR)
            c.write32(c.gbr + ((op & 0xffu) * 4u), c.r[0]);
        } else if ((op & 0xff00u) == 0xc400u) { // MOV.B @(disp,GBR),R0
            c.r[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(c.read8(c.gbr + (op & 0xffu)))));
        } else if ((op & 0xff00u) == 0xc500u) { // MOV.W @(disp,GBR),R0
            c.r[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(c.read16(c.gbr + ((op & 0xffu) * 2u)))));
        } else if ((op & 0xff00u) == 0xc600u) { // MOV.L @(disp,GBR),R0
            c.r[0] = c.read32(c.gbr + ((op & 0xffu) * 4u));
        } else if ((op & 0xf000u) == 0xc000u && ((op >> 8) & 0xfu) == 0x7u) { // MOVA
            c.r[0] = ((instruction_pc + 4u) & ~3u) + ((op & 0xffu) * 4u);
        // Floating-point opcodes use the same state helpers as the existing
        // SH-4 AOT backend, including FR/XF bank selection and SZ transfers.
        } else if ((op & 0xf00fu) == 0xf000u) { // FADD FRm,FRn / DRm,DRn
            fpu::fadd(c, n, m);
        } else if ((op & 0xf00fu) == 0xf001u) { // FSUB
            fpu::fsub(c, n, m);
        } else if ((op & 0xf00fu) == 0xf002u) { // FMUL
            fpu::fmul(c, n, m);
        } else if ((op & 0xf00fu) == 0xf003u) { // FDIV
            fpu::fdiv(c, n, m);
        } else if ((op & 0xf00fu) == 0xf004u) { // FCMP/EQ
            fpu::fcmp_eq(c, n, m);
        } else if ((op & 0xf00fu) == 0xf005u) { // FCMP/GT
            fpu::fcmp_gt(c, n, m);
        } else if ((op & 0xf00fu) == 0xf006u) { // FMOV @(R0,Rm),FRn
            multiarcaderecomp::naomi::host_fmov_load(c, n, c.r[0] + c.r[m]);
        } else if ((op & 0xf00fu) == 0xf007u) { // FMOV FRm,@(R0,Rn)
            multiarcaderecomp::naomi::host_fmov_store(c, c.r[0] + c.r[n], m);
        } else if ((op & 0xf00fu) == 0xf008u) { // FMOV @Rm,FRn
            multiarcaderecomp::naomi::host_fmov_load(c, n, c.r[m]);
        } else if ((op & 0xf00fu) == 0xf009u) { // FMOV @Rm+,FRn
            multiarcaderecomp::naomi::host_fmov_load_postinc(c, n, m);
        } else if ((op & 0xf00fu) == 0xf00au) { // FMOV FRm,@Rn
            multiarcaderecomp::naomi::host_fmov_store(c, c.r[n], m);
        } else if ((op & 0xf00fu) == 0xf00bu) { // FMOV FRm,@-Rn
            multiarcaderecomp::naomi::host_fmov_store_predec(c, n, m);
        } else if ((op & 0xf00fu) == 0xf00cu) { // FMOV FRm,FRn
            multiarcaderecomp::naomi::host_fmov_reg(c, n, m);
        } else if ((op & 0xf0ffu) == 0xf00du) { // FSTS FPUL,FRn
            multiarcaderecomp::naomi::host_fsts_fpul(c, n);
        } else if ((op & 0xf0ffu) == 0xf01du) { // FLDS FRn,FPUL
            multiarcaderecomp::naomi::host_flds_fpul(c, n);
        } else if ((op & 0xf0ffu) == 0xf02du) { // FLOAT FPUL,FRn/DRn
            multiarcaderecomp::naomi::host_float_fpul(c, n);
        } else if ((op & 0xf0ffu) == 0xf03du) { // FTRC FRn/DRn,FPUL
            multiarcaderecomp::naomi::host_ftrc_fpul(c, n);
        } else if ((op & 0xf0ffu) == 0xf04du) { // FNEG
            fpu::fneg(c, n);
        } else if ((op & 0xf0ffu) == 0xf05du) { // FABS
            fpu::fabs(c, n);
        } else if ((op & 0xf0ffu) == 0xf06du) { // FSQRT
            fpu::fsqrt(c, n);
        } else if ((op & 0xf0ffu) == 0xf07du) { // FSRRA
            fpu::fsrra(c, n);
        } else if ((op & 0xf0ffu) == 0xf08du) { // FLDI0
            multiarcaderecomp::naomi::host_fldi(c, n, 0.0f);
        } else if ((op & 0xf0ffu) == 0xf09du) { // FLDI1
            multiarcaderecomp::naomi::host_fldi(c, n, 1.0f);
        } else if ((op & 0xf0ffu) == 0xf0adu) { // FCNVSD FPUL,DRn
            multiarcaderecomp::naomi::host_fcnvsd(c, n);
        } else if ((op & 0xf0ffu) == 0xf0bdu) { // FCNVDS DRn,FPUL
            multiarcaderecomp::naomi::host_fcnvds(c, n);
        } else if ((op & 0xf00fu) == 0xf00eu) { // FMAC FR0,FRm,FRn
            fpu::fmac(c, n, m);
        } else if ((op & 0xf0ffu) == 0xf0edu) { // FIPR FV_M,FV_N
            const unsigned packed = n;
            fpu::fipr(c, (packed & 3u) << 2, packed & 12u);
        } else if ((op & 0xf1ffu) == 0xf0fdu) { // FSCA FPUL,DRn
            fpu::fsca(c, n);
        } else if ((op & 0xf3ffu) == 0xf1fdu) { // FTRV XMTRX,FVn
            fpu::ftrv(c, n);
        } else if (op == 0xf3fdu) { // FSCHG
            fpu::toggle_sz(c);
        } else if (op == 0xfbfdu) {
            fpu::toggle_fr(c);
        } else if ((op & 0xf000u) == 0x8000u && ((op >> 8) & 0xfu) == 0x8u) { // CMP/EQ #imm,R0
            c.t = c.r[0] == static_cast<std::uint32_t>(sign_extend8(op));
        } else if (op == 0x000b || op == 0x002b) {
            handled = false;
        } else {
            handled = false;
        }
    }

    c.delay_slot = old_delay_slot;
    if (!handled) {
        c.pc = instruction_pc;
        const bool delayed_branch_opcode =
            (op & 0xf000u) == 0xa000u || (op & 0xf000u) == 0xb000u ||
            (op & 0xff00u) == 0x8900u || (op & 0xff00u) == 0x8b00u ||
            (op & 0xff00u) == 0x8d00u || (op & 0xff00u) == 0x8f00u ||
            (op & 0xf0ffu) == 0x402bu || (op & 0xf0ffu) == 0x400bu ||
            (op & 0xf0ffu) == 0x0023u || (op & 0xf0ffu) == 0x0003u ||
            op == 0x000bu || op == 0x002bu;
        if (delay_slot && delayed_branch_opcode) {
            const auto event = 0x1a0u; // SlotIllegalInstr
            multiarcaderecomp::naomi::host_raise_exception(c, event,
                instruction_pc - 2u, 0x100u);
            multiarcaderecomp_interpreter_trace_step(
                "SH-4", "Flycast-interpreter", MULTIARCADERECOMP_EXCEPTION_BOUNDARY,
                instruction_pc, op, 16, 0, 1, c.pc,
                "control transfer in delay slot");
            return !c.execution_stopped;
        }
        if (unhandled_reason == MULTIARCADERECOMP_UNSUPPORTED_OPCODE) {
            const auto event = delay_slot ? 0x1a0u : 0x180u;
            const auto exception_pc = delay_slot ? instruction_pc - 2u : instruction_pc;
            std::fprintf(stderr,
                         "[SH-4 interpreter] PC=%08x OPCODE=%04x CATEGORY=UNSUPPORTED_OPCODE event=%03x\n",
                         instruction_pc, op, event);
            multiarcaderecomp::naomi::host_raise_exception(c, event, exception_pc, 0x100u);
            multiarcaderecomp_interpreter_trace_step(
                "SH-4", "Flycast-interpreter", MULTIARCADERECOMP_EXCEPTION_BOUNDARY,
                instruction_pc, op, 16, 0, 1, c.pc, "illegal instruction exception");
            return !c.execution_stopped;
        }
        std::fprintf(stderr,
                     "[SH-4 interpreter] PC=%08x OPCODE=%04x CATEGORY=%s\n",
                     instruction_pc, op, multiarcaderecomp_fallback_reason_name(unhandled_reason));
        multiarcaderecomp_interpreter_trace_step(
            "SH-4", "Flycast-interpreter", unhandled_reason,
            instruction_pc, op, 16, 0, 0, instruction_pc, unhandled_detail);
        return false;
    }
    multiarcaderecomp_interpreter_trace_step(
        "SH-4", "Flycast-interpreter", MULTIARCADERECOMP_UNCOVERED_ENTRY,
        instruction_pc, op, 16, 0, 1, c.pc, "Flycast SH-4 LLE");
    return true;
}

} // namespace

bool step(multiarcaderecomp::naomi::Sh4Context& context) {
    return step_instruction(context, context.delay_slot);
}

} // namespace multiarcaderecomp::cpus::sh4::flycast
