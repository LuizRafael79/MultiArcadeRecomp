// The opcode semantics and addressing modes below are adapted directly from
// MAME's BSD-3-Clause TMS320C3x core. See MAME_PORT.md for the boundary.
#include "tms320c31.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace multiarcaderecomp::tms320c31 {
namespace util {
constexpr std::int32_t sext(std::uint32_t value, int bits) noexcept {
    const std::uint32_t mask = (std::uint32_t{1} << bits) - 1;
    value &= mask;
    const std::uint32_t sign = std::uint32_t{1} << (bits - 1);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}
}
static constexpr std::int64_t mul_32x32(std::int32_t a, std::int32_t b) noexcept {
    return static_cast<std::int64_t>(a) * b;
}
namespace {
using offs_t = std::uint32_t;
constexpr int ASSERT_LINE = 1;
constexpr int CLEAR_LINE = 0;
constexpr std::uint32_t CHIP_TYPE_TMS320C32 = 2;
constexpr std::uint32_t CFLAG = 0x0001, VFLAG = 0x0002, ZFLAG = 0x0004;
constexpr std::uint32_t NFLAG = 0x0008, UFFLAG = 0x0010, LVFLAG = 0x0020;
constexpr std::uint32_t LUFFLAG = 0x0040, OVMFLAG = 0x0080, RMFLAG = 0x0100;
constexpr std::uint32_t GIEFLAG = 0x2000;
enum : int {
    TMR_R0=0,TMR_R1,TMR_R2,TMR_R3,TMR_R4,TMR_R5,TMR_R6,TMR_R7,
    TMR_AR0,TMR_AR1,TMR_AR2,TMR_AR3,TMR_AR4,TMR_AR5,TMR_AR6,TMR_AR7,
    TMR_DP,TMR_IR0,TMR_IR1,TMR_BK,TMR_SP,TMR_ST,TMR_IE,TMR_IF,TMR_IOF,
    TMR_RS,TMR_RE,TMR_RC,TMR_R8,TMR_R9,TMR_R10,TMR_R11,TMR_TEMP1,TMR_TEMP2,TMR_TEMP3
};
struct DebugMachine { void debug_break() noexcept {} };
void trace_step(char backend, std::uint32_t pc, std::uint32_t opcode) noexcept {
    static const bool enabled = std::getenv("MULTIARCADERECOMP_C31_TRACE_EXEC") != nullptr;
    static std::uint64_t count = 0;
    if (!enabled || count >= 20000) return;
    std::fprintf(stderr, "[C31 step] %c PC=%06x OPCODE=%08x\n", backend, pc, opcode);
    ++count;
}
}

float Register::as_float() const noexcept {
    if (mantissa() == 0 && exponent() == -128) return 0.0f;
    const std::uint32_t sign = mantissa() < 0 ? 0x80000000u : 0;
    const std::uint32_t magnitude = mantissa() < 0
        ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(mantissa()))
        : static_cast<std::uint32_t>(mantissa());
    const std::uint32_t bits = sign |
        (static_cast<std::uint32_t>(static_cast<int>(exponent()) + 127) << 23) |
        ((magnitude >> 8) & 0x007fffffu);
    return std::bit_cast<float>(bits);
}

double Register::as_double() const noexcept {
    if (mantissa() == 0 && exponent() == -128) return 0.0;
    const std::uint64_t sign = mantissa() < 0 ? (std::uint64_t{1} << 63) : 0;
    const std::uint64_t magnitude = mantissa() < 0
        ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(mantissa()))
        : static_cast<std::uint64_t>(mantissa());
    const std::uint64_t bits = sign |
        (static_cast<std::uint64_t>(static_cast<int>(exponent()) + 1023) << 52) |
        ((magnitude >> 11) & 0x000fffffffffffffull);
    return std::bit_cast<double>(bits);
}

void Register::from_double(double value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    const std::uint32_t high = static_cast<std::uint32_t>(bits >> 32);
    const std::uint32_t low = static_cast<std::uint32_t>(bits);
    const std::uint32_t mant = ((high & 0x000fffffu) << 11) |
                               ((low & 0xffe00000u) >> 21);
    const int exp = static_cast<int>((high & 0x7ff00000u) >> 20) - 1023;
    if (exp < -128) { set_mantissa(0); set_exponent(-128); }
    else if (exp > 127) {
        set_mantissa((high & 0x80000000u) ? 0x80000001u : 0x7fffffffu);
        set_exponent(127);
    } else if (!(high & 0x80000000u)) {
        set_mantissa(static_cast<std::int32_t>(mant)); set_exponent(exp);
    } else if (mant) {
        set_mantissa(static_cast<std::int32_t>(0x80000000u | (0u - mant)));
        set_exponent(exp);
    } else {
        set_mantissa(static_cast<std::int32_t>(0x80000000u)); set_exponent(exp - 1);
    }
}

Interpreter::Interpreter(State& state, Bus bus)
    : m_s(state), m_bus(bus), m_aot_word_validated(1u << 18) {}

void Interpreter::invalidate_code_cache() noexcept {
    m_code_page_resolved.fill(false);
    std::fill(m_aot_word_validated.begin(), m_aot_word_validated.end(), 0);
}

void Interpreter::invalidate_code_word(std::uint32_t address) noexcept {
    address &= 0x00ffffffu;
    auto& validation_word = m_aot_word_validated[address >> 6];
    const auto mask = std::uint64_t{1} << (address & 63);
    if (validation_word & mask) validation_word &= ~mask;
}

void Interpreter::mark_stop(StopReason reason, std::uint32_t opcode) noexcept {
    if (m_stopped) return;
    m_s.stop_reason = reason;
    m_s.last_opcode = opcode;
    m_s.last_pc = m_s.pc ? m_s.pc - 1 : 0;
    m_stopped = true;
}

std::uint32_t Interpreter::ROPCODE(std::uint32_t address) noexcept {
    std::uint32_t opcode{};
    if (!read_code_word(address, opcode)) {
        m_bus_fault = true;
        return 0;
    }
    if (m_recording_instruction) {
        m_s.last_pc = address & 0x00ffffffu;
        m_s.last_opcode = opcode;
        ++m_s.instructions;
        ++m_s.interpreted_instructions;
        trace_step('I', address & 0x00ffffffu, opcode);
        if (m_aot_enabled) {
            if (m_next_instruction_aot_mismatch) {
                ++m_s.aot_opcode_mismatches;
                m_s.last_aot_mismatch_pc = address & 0x00ffffffu;
                m_s.last_aot_mismatch_live = opcode;
                static std::uint64_t reported_mismatches = 0;
                if (reported_mismatches++ < 64)
                    std::fprintf(stderr,
                        "[C31 fallback] category=AOT_OPCODE_MISMATCH PC=%06x expected=%08x live=%08x\n",
                        m_s.last_aot_mismatch_pc, m_s.last_aot_mismatch_expected, opcode);
                m_next_instruction_aot_mismatch = false;
            } else if (m_next_instruction_uncovered) {
                ++m_s.uncovered_entry_instructions;
                m_s.last_uncovered_pc = address & 0x00ffffffu;
                m_s.last_uncovered_opcode = opcode;
                static std::uint64_t reported_uncovered = 0;
                if (reported_uncovered++ < 64)
                    std::fprintf(stderr,
                        "[C31 fallback] category=UNCOVERED_ENTRY PC=%06x OPCODE=%08x\n",
                        m_s.last_uncovered_pc, opcode);
                m_next_instruction_uncovered = false;
            }
        }
        static const bool collect_hot = std::getenv("MULTIARCADERECOMP_C31_HOT_SITES") != nullptr;
        if (collect_hot) {
            struct DebugHotSite { std::uint32_t pc{}, opcode{}; std::uint64_t count{}; };
            static std::array<DebugHotSite, 1u << 16> debug_sites{};
            const auto pc = address & 0x00ffffffu;
            const auto first = (pc * 0x9e3779b1u) >> 16;
            std::size_t slot = first;
            std::uint64_t least_count = UINT64_MAX;
            for (unsigned probe = 0; probe < 4; ++probe) {
                const auto candidate = (first + probe) & 0xffffu;
                const auto& site = debug_sites[candidate];
                if (site.count && site.pc == pc) { slot = candidate; break; }
                if (!site.count) { slot = candidate; break; }
                if (site.count < least_count) {
                    least_count = site.count;
                    slot = candidate;
                }
            }
            auto& site = debug_sites[slot];
            if (!site.count || site.pc != pc) {
                site = {pc, opcode, 0};
            } else if (site.opcode != opcode) {
                if (site.count >= 1000)
                    std::fprintf(stderr,
                        "[C31 hot opcode] PC=%06x OPCODE=%08x ST=%08x delayed=%u\n",
                        pc, opcode, m_s.r[TMR_ST].i32[0], m_s.delayed ? 1u : 0u);
                site.opcode = opcode;
                site.count = 0;
            }
            ++site.count;
            if (site.count == 1000) {
                std::fprintf(stderr,
                    "[C31 hot opcode] PC=%06x OPCODE=%08x ST=%08x delayed=%u\n",
                    pc, opcode, m_s.r[TMR_ST].i32[0], m_s.delayed ? 1u : 0u);
            }
        }
    }
    return opcode;
}

bool Interpreter::read_code_word(std::uint32_t address,
                                 std::uint32_t& value) noexcept {
    address &= 0x00ffffffu;
    if (m_bus.map_code_page) {
        const auto page_index = address >> kCodePageShift;
        if (!m_code_page_resolved[page_index]) {
            const auto page_address = address & ~kCodePageMask;
            m_code_pages[page_index] = m_bus.map_code_page(
                m_bus.opaque, page_address);
            m_code_page_resolved[page_index] = true;
        }
        const auto* page = m_code_pages[page_index];
        if (page) {
            value = page[address & kCodePageMask];
        } else if (m_bus.read32) {
            value = m_bus.read32(m_bus.opaque, address);
        } else {
            return false;
        }
    } else if (m_bus.read32) {
        value = m_bus.read32(m_bus.opaque, address);
    } else {
        return false;
    }
    return true;
}

bool Interpreter::aot_begin(std::uint32_t expected_pc,
                            std::uint32_t expected_opcode) noexcept {
    const auto reject = [this, expected_pc, expected_opcode](const char* reason) {
        static const bool debug = std::getenv("MULTIARCADERECOMP_C31_AOT_REJECTS") != nullptr;
        static unsigned count = 0;
        if (debug && expected_pc >= 0x658d && expected_pc <= 0x6590 && count++ < 32) {
            std::uint32_t live{};
            const bool have_live = read_code_word(expected_pc, live);
            std::fprintf(stderr,
                "[C31 AOT reject] reason=%s pc=%06x livepc=%06x op=%08x live=%08x have=%u icount=%lld delayed=%u idle=%u ST=%08x\n",
                reason, expected_pc, m_s.pc & 0xffffffu, expected_opcode, live,
                have_live ? 1u : 0u, static_cast<long long>(m_icount),
                m_s.delayed ? 1u : 0u, m_s.idling ? 1u : 0u, m_s.r[TMR_ST].i32[0]);
        }
        return false;
    };
    if (m_icount <= 0) return reject("budget");
    if ((m_s.pc & 0x00ffffffu) != expected_pc) return reject("pc");
    if (m_s.idling) return reject("idle");
    std::uint32_t live_opcode = expected_opcode;
    const auto guard_mask = std::uint64_t{1} << (expected_pc & 63);
    auto& guard_word = m_aot_word_validated[expected_pc >> 6];
    if (!(guard_word & guard_mask)) {
        if (!read_code_word(expected_pc, live_opcode)) return reject("unmapped");
        if (live_opcode != expected_opcode) {
            m_next_instruction_aot_mismatch = true;
            m_s.last_aot_mismatch_pc = expected_pc;
            m_s.last_aot_mismatch_expected = expected_opcode;
            m_s.last_aot_mismatch_live = live_opcode;
            return reject("opcode");
        }
        guard_word |= guard_mask;
    }
    m_s.last_pc = expected_pc;
    m_s.last_opcode = live_opcode;
    trace_step('A', expected_pc, live_opcode);
    ++m_s.instructions;
    ++m_s.native_instructions;
    m_s.pc = (expected_pc + 1) & 0x00ffffffu;
    burn_cycle(1);
    return true;
}

bool Interpreter::aot_end(std::uint32_t expected_pc) noexcept {
    return m_s.pc == ((expected_pc + 1) & 0x00ffffffu) &&
           !m_s.idling && !m_stopped && !m_bus_fault;
}

bool Interpreter::try_aot_delay_slot() noexcept {
    if (!m_aot_enabled) return false;
    const auto entry = lookup_aot(m_s.pc);
    if (!entry.block) {
        m_next_instruction_uncovered = true;
        return false;
    }
    const auto before = m_s.native_instructions;
    (void)entry.block(*this, entry.offset);
    return m_s.native_instructions != before;
}
std::uint32_t Interpreter::RMEM(std::uint32_t address) noexcept {
    if (!m_bus.read32) { m_bus_fault = true; return 0; }
    return m_bus.read32(m_bus.opaque, address & 0x00ffffffu);
}
void Interpreter::WMEM(std::uint32_t address, std::uint32_t value) noexcept {
    if (!m_bus.write32) { m_bus_fault = true; return; }
    m_bus.write32(m_bus.opaque, address & 0x00ffffffu, value);
}

void Interpreter::reset() noexcept {
    invalidate_code_cache();
    m_s.pc = RMEM(0) & 0x00ffffffu;
    m_s.r[TMR_IE].i32[0] = 0;
    m_s.r[TMR_IF].i32[0] = 0;
    m_s.r[TMR_ST].i32[0] = 0;
    m_s.r[TMR_IOF].i32[0] = 0;
    m_s.r[TMR_IF].i32[0] |= m_s.irq_state & 0x0f;
    m_s.primary_bus_control = 0x000010f8;
    m_s.delayed = m_s.irq_pending = m_s.idling = m_s.low_power = false;
    m_s.stop_reason = StopReason::None;
    m_bus_fault = m_stopped = false;
}

void Interpreter::set_irq(unsigned line, bool asserted) noexcept {
    if (line >= 4) return;
    const auto mask = static_cast<std::uint16_t>(1u << line);
    if (asserted) {
        m_s.irq_state |= mask;
        m_s.r[TMR_IF].i32[0] |= mask;
    } else {
        m_s.irq_state &= static_cast<std::uint16_t>(~mask);
    }
    // The C31 external IRQ inputs are level-sensitive.
    m_s.r[TMR_IF].i32[0] |= m_s.irq_state & 0x0f;
}

void Interpreter::check_irqs() {
    const auto valid = m_s.r[TMR_IF].i32[0] & m_s.r[TMR_IE].i32[0] & 0x0fffu;
    if (!valid || !(m_s.r[TMR_ST].i32[0] & GIEFLAG)) return;
    int trap_number = 0;
    for (int i = 0; i < 12; ++i) if (valid & (1u << i)) { trap_number = i + 1; break; }
    m_s.idling = false;
    if (m_s.delayed) { m_s.irq_pending = true; return; }
    m_s.r[TMR_IF].i32[0] &= ~(1u << (trap_number - 1));
    trap(trap_number);
    m_s.r[TMR_IF].i32[0] |= m_s.irq_state & 0x0f;
}

std::uint64_t Interpreter::current_cycles() const noexcept {
    if (!m_running) return m_s.cycles;
    const auto consumed = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(m_run_cycle_budget) - m_icount);
    return m_run_cycle_base + static_cast<std::uint64_t>(consumed);
}

Exit Interpreter::run(std::uint64_t cycle_budget) noexcept {
    if (m_s.stop_reason == StopReason::MameUnimplemented ||
        m_s.stop_reason == StopReason::IllegalOpcode ||
        m_s.stop_reason == StopReason::BusFault)
        return m_s.stop_reason == StopReason::BusFault ? Exit::BusFault : Exit::Stopped;
    m_s.stop_reason = StopReason::None;
    m_stopped = m_bus_fault = false;
    m_icount = static_cast<std::int64_t>(cycle_budget);
    m_run_cycle_base = m_s.cycles;
    m_run_cycle_budget = cycle_budget;
    m_running = true;
    check_irqs();
    if (m_s.idling) {
        m_s.cycles += cycle_budget;
        m_running = false;
        return Exit::Idle;
    }
    while (m_icount > 0 && !m_stopped && !m_bus_fault) {
        if ((m_s.r[TMR_ST].i32[0] & RMFLAG) &&
            m_s.pc == m_s.r[TMR_RE].i32[0] + 1) {
            if (static_cast<std::int32_t>(--m_s.r[TMR_RC].i32[0]) >= 0)
                m_s.pc = m_s.r[TMR_RS].i32[0];
            else {
                m_s.r[TMR_ST].i32[0] &= ~RMFLAG;
                if (m_s.delayed) {
                    m_s.delayed = false;
                    if (m_s.irq_pending) { m_s.irq_pending = false; check_irqs(); }
                }
            }
            continue;
        }
        // Repeat-loop boundaries must be processed before AOT lookup. Otherwise
        // a translated instruction at RE+1 could execute once without the C31
        // decrementing RC/redirecting PC as MAME does.
        if (m_aot_enabled) {
            m_next_instruction_aot_mismatch = false;
            m_next_instruction_uncovered = false;
            const auto aot_entry = lookup_aot(m_s.pc);
            if (aot_entry.block) {
                const auto before = m_s.native_instructions;
                if (aot_entry.block(*this, aot_entry.offset) ||
                    m_s.native_instructions != before)
                    continue;
            } else {
                m_next_instruction_uncovered = true;
            }
        }
        // Let MAME's execute_one() perform the only opcode fetch. A separate
        // diagnostic prefetch would duplicate reads from mapped code regions.
        if (m_aot_enabled) {
            m_recording_instruction = true;
            execute_one();
            m_recording_instruction = false;
        } else {
            execute_one();
        }
        // Optional bring-up trace for board ports.  Keep it disabled unless
        // explicitly requested because this CPU can execute hundreds of
        // millions of instructions during one run.
        static const bool trace_loop = std::getenv("MULTIARCADERECOMP_C31_TRACE_LOOP") != nullptr;
        static std::uint64_t trace_loop_count = 0;
        if (trace_loop &&
            ((m_s.last_pc >= 0x00004ae6 && m_s.last_pc <= 0x00004b10) ||
             (m_s.last_pc >= 0x00005050 && m_s.last_pc <= 0x00005060) ||
             (m_s.last_pc >= 0x00009fe0 && m_s.last_pc <= 0x0000a030))) {
            ++trace_loop_count;
            if (trace_loop_count <= 128 || (trace_loop_count % 1000000) == 0) {
                std::fprintf(stderr,
                    "[C31 loop] n=%llu pc=%06x op=%08x next=%06x R0=%08x AR0=%08x AR1=%08x "
                    "AR2=%08x AR3=%08x AR4=%08x AR5=%08x AR6=%08x AR7=%08x ST=%08x\n",
                    static_cast<unsigned long long>(trace_loop_count), m_s.last_pc,
                    m_s.last_opcode, m_s.pc, m_s.r[TMR_R0].i32[0],
                    m_s.r[TMR_AR0].i32[0], m_s.r[TMR_AR1].i32[0],
                    m_s.r[TMR_AR2].i32[0], m_s.r[TMR_AR3].i32[0],
                    m_s.r[TMR_AR4].i32[0], m_s.r[TMR_AR5].i32[0],
                    m_s.r[TMR_AR6].i32[0], m_s.r[TMR_AR7].i32[0],
                    m_s.r[TMR_ST].i32[0]);
            }
        }
    }
    const auto used = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
                                  static_cast<std::int64_t>(cycle_budget) - m_icount));
    m_s.cycles += used;
    m_running = false;
    if (m_bus_fault) { m_s.stop_reason = StopReason::BusFault; return Exit::BusFault; }
    if (m_stopped) return Exit::Stopped;
    if (m_s.idling) return Exit::Idle;
    m_s.stop_reason = StopReason::Budget;
    return Exit::Budget;
}

#define tms320c3x_device Interpreter
#define offs_t std::uint32_t
#define m_pc m_s.pc
#define m_r m_s.r
#define m_bkmask m_s.bkmask
#define m_irq_state m_s.irq_state
#define m_delayed m_s.delayed
#define m_irq_pending m_s.irq_pending
#define m_is_idling m_s.idling
#define m_is_lopower m_s.low_power
#define m_primary_bus_control m_s.primary_bus_control
#define m_hold_state m_s.hold_state
#define debugger_enabled() false
#define debugger_instruction_hook(...) ((void)0)
#define logerror(...) ((void)0)
#define fatalerror(...) (mark_stop(StopReason::MameUnimplemented, m_s.last_opcode), (void)0)
#define BIT(value, bit) (((value) >> (bit)) & 1u)
#define machine() (*this)
#include "mame_source/320c3x_ops.ipp"
#include "mame_aot_handler_implementations.inc"
#undef machine
#undef BIT
#undef fatalerror
#undef logerror
#undef debugger_instruction_hook
#undef debugger_enabled
#undef m_hold_state
#undef m_primary_bus_control
#undef m_is_lopower
#undef m_is_idling
#undef m_irq_pending
#undef m_delayed
#undef m_irq_state
#undef m_bkmask
#undef m_r
#undef m_pc
#undef IREG
#undef offs_t
#undef tms320c3x_device

} // namespace multiarcaderecomp::tms320c31
