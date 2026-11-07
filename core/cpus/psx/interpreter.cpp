// license:BSD-3-Clause
// Adapted from the PlayStation CPU behavior in MAME's src/devices/cpu/psx/psx.cpp.

#include "machine_memory.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <unordered_set>
#include <cstdlib>
#include <iostream>

namespace {

struct PsxInstructionTrace {
    uint32_t pc;
    uint32_t opcode;
    uint32_t registers[32];
    uint32_t branch_target;
    uint32_t delayed_load_value;
    uint8_t delayed_load_reg;
    uint8_t delayed_load_pending;
    uint8_t branch_pending;
    uint8_t branch_taken;
};

constexpr unsigned kPsxInstructionTraceCapacity = 64;
PsxInstructionTrace g_psx_instruction_trace[kPsxInstructionTraceCapacity]{};
unsigned g_psx_instruction_trace_next = 0;
unsigned g_psx_instruction_trace_count = 0;
const bool g_psx_instruction_trace_enabled =
    std::getenv("MAMERECOMP_ZN_TRACE_EXCEPTION_HISTORY") != nullptr;

bool psx_instruction_trace_matches(uint32_t pc)
{
    struct Range { uint32_t first; uint32_t last; bool active; };
    static const Range range = [] {
        const char *text = std::getenv("MAMERECOMP_ZN_TRACE_EXCEPTION_PC_RANGE");
        if (!text || !*text) return Range{0, 0, false};
        char *end = nullptr;
        const unsigned long first = std::strtoul(text, &end, 0);
        if (!end || (*end != '-' && *end != ':')) return Range{0, 0, false};
        const unsigned long last = std::strtoul(end + 1, &end, 0);
        if (!end || *end || first > UINT32_MAX || last > UINT32_MAX || first > last)
            return Range{0, 0, false};
        return Range{static_cast<uint32_t>(first), static_cast<uint32_t>(last), true};
    }();
    return !range.active || (pc >= range.first && pc <= range.last);
}

void psx_record_instruction_trace(const PsxCpuState *state, uint32_t pc,
                                  uint32_t opcode)
{
    if (!psx_instruction_trace_matches(pc)) return;
    auto &trace = g_psx_instruction_trace[g_psx_instruction_trace_next];
    trace.pc = pc;
    trace.opcode = opcode;
    std::memcpy(trace.registers, state->r, sizeof(trace.registers));
    trace.branch_target = state->branch_target;
    trace.delayed_load_value = state->delayed_load_value;
    trace.delayed_load_reg = state->delayed_load_reg;
    trace.delayed_load_pending = state->delayed_load_pending;
    trace.branch_pending = state->branch_pending;
    trace.branch_taken = state->branch_taken;
    if (std::getenv("MAMERECOMP_ZN_TRACE_EXCEPTION_STEPS")) {
        std::cerr << "PSX step pc=0x" << std::hex << pc << " op=0x" << opcode
                  << " a1=0x" << state->r[5] << " t1=0x" << state->r[9]
                  << " t2=0x" << state->r[10] << " branch=" << std::dec
                  << unsigned(state->branch_pending) << ':'
                  << unsigned(state->branch_taken) << "->0x" << std::hex
                  << state->branch_target << " load=" << std::dec
                  << unsigned(state->delayed_load_pending) << ':'
                  << unsigned(state->delayed_load_reg) << "=0x" << std::hex
                  << state->delayed_load_value << std::dec << '\n';
    }
    g_psx_instruction_trace_next =
        (g_psx_instruction_trace_next + 1) % kPsxInstructionTraceCapacity;
    if (g_psx_instruction_trace_count < kPsxInstructionTraceCapacity)
        ++g_psx_instruction_trace_count;
}

constexpr unsigned EXC_ADEL = 4;
constexpr unsigned EXC_ADES = 5;
constexpr unsigned EXC_DBE = 7;
constexpr unsigned EXC_SYS = 8;
constexpr unsigned EXC_BP = 9;
constexpr unsigned EXC_RI = 10;
constexpr unsigned EXC_CPU = 11;
constexpr unsigned EXC_OVF = 12;

constexpr unsigned CP0_TAR = 6;
constexpr unsigned CP0_BADVADDR = 8;
constexpr unsigned CP0_SR = 12;
constexpr unsigned CP0_CAUSE = 13;
constexpr unsigned CP0_EPC = 14;
constexpr unsigned CP0_PRID = 15;

constexpr uint32_t SR_KUC = 1u << 1;
constexpr uint32_t SR_BEV = 1u << 22;
constexpr uint32_t SR_CU0 = 1u << 28;
constexpr uint32_t SR_CU1 = 1u << 29;
constexpr uint32_t SR_CU2 = 1u << 30;
constexpr uint32_t SR_CU3 = 1u << 31;

constexpr uint32_t CAUSE_EXC = 31u << 2;
constexpr uint32_t CAUSE_IP = 255u << 8;
constexpr uint32_t CAUSE_CE = 3u << 28;
constexpr uint32_t CAUSE_BT = 1u << 30;
constexpr uint32_t CAUSE_BD = 1u << 31;

constexpr uint32_t CP0_WRITE_MASK[16] = {
    0x00000000, 0x00000000, 0x00000000, 0xffffffff,
    0x00000000, 0xffffffff, 0x00000000, 0xff80f03f,
    0x00000000, 0xffffffff, 0x00000000, 0xffffffff,
    0xf04fff3f, 0x00000300, 0x00000000, 0x00000000
};

#if defined(__GNUC__)
#define PSX_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define PSX_ALWAYS_INLINE inline
#endif

// The interpreter retires every guest instruction through these hot-path
// helpers. Keep the same pipeline operations as pipeline.cpp, but inline them
// here so MIPS interpretation does not cross a translation-unit boundary on
// every instruction. Generated code continues to use the shared ABI helpers.
PSX_ALWAYS_INLINE void advance_interpreted_instruction(PsxCpuState *state)
{
    if (state->branch_pending) {
        state->pc = state->branch_taken ? state->branch_target : state->pc + 4;
        state->branch_pending = 0;
        state->branch_taken = 0;
        return;
    }
    if (state->delayed_load_pending) {
        const unsigned reg = state->delayed_load_reg;
        if (reg != 0 && reg < 32) state->r[reg] = state->delayed_load_value;
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }
    state->pc += 4;
}

PSX_ALWAYS_INLINE void retire_interpreted_instruction(PsxCpuState *state)
{
    // r0 remains zero by construction: write_register and delayed loads skip it.
    ++state->cycles;
}

PSX_ALWAYS_INLINE void schedule_interpreted_load(PsxCpuState *state,
                                                  unsigned reg,
                                                  uint32_t value)
{
    if (state->delayed_load_pending && state->delayed_load_reg == reg) {
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }
    advance_interpreted_instruction(state);
    if (reg != 0 && reg < 32) {
        state->delayed_load_reg = static_cast<uint8_t>(reg);
        state->delayed_load_pending = 1;
        state->delayed_load_value = value;
    }
}

PSX_ALWAYS_INLINE void schedule_interpreted_branch(PsxCpuState *state,
                                                    uint32_t target,
                                                    bool taken)
{
    state->branch_target = taken ? target : state->pc + 4;
    state->branch_pending = 1;
    state->branch_taken = taken ? 1 : 0;
}

uint32_t sx16(uint32_t value)
{
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
}

uint32_t branch_target(uint32_t pc, uint32_t op)
{
    return pc + 4 + (sx16(op) << 2);
}

uint32_t gpr(const PsxCpuState *s, unsigned index)
{
    return index == 0 ? 0 : s->r[index & 31];
}

PSX_ALWAYS_INLINE void write_register(PsxCpuState *s, unsigned index,
                                      uint32_t value)
{
    advance_interpreted_instruction(s);
    if (index != 0)
        s->r[index & 31] = value;
}

PSX_ALWAYS_INLINE void finish(PsxCpuState *s, uint32_t op, uint32_t pc)
{
    (void)op;
    (void)pc;
    retire_interpreted_instruction(s);
}

PSX_ALWAYS_INLINE PsxAotExit check_interpreted_interrupt(PsxCpuState *s)
{
    if ((s->cp0[CP0_SR] & 1u) &&
        (s->cp0[CP0_SR] & s->cp0[CP0_CAUSE] & 0x0000ff00u))
        return psx_aot_raise_exception(s, 0, s->pc, 0);
    return PSX_AOT_DISPATCH;
}

PSX_ALWAYS_INLINE bool memory_address_can_update_irq(uint32_t address)
{
    return (address & 0x1fffffffu) >= 0x1f000000u;
}

bool cop0_usable(PsxCpuState *s, uint32_t pc, uint32_t op)
{
    if ((s->cp0[CP0_SR] & SR_KUC) && !(s->cp0[CP0_SR] & SR_CU0)) {
        (void)psx_aot_raise_exception(s, EXC_CPU, pc, op);
        return false;
    }
    return true;
}

bool cop2_usable(PsxCpuState *s, uint32_t pc, uint32_t op)
{
    if (!(s->cp0[CP0_SR] & SR_CU2)) {
        (void)psx_aot_raise_exception(s, EXC_CPU, pc, op);
        return false;
    }
    return true;
}

bool cop2_read(const PsxBus *bus, PsxCpuState *s, uint32_t pc,
               bool control, unsigned index, uint32_t *value)
{
    if (bus && bus->cop2_read)
        return bus->cop2_read(bus->opaque, s, pc, control, index, value);
    *value = control ? s->gte_control[index & 31] : s->gte_data[index & 31];
    return true;
}

bool cop2_write(const PsxBus *bus, PsxCpuState *s, uint32_t pc,
                bool control, unsigned index, uint32_t value)
{
    if (bus && bus->cop2_write)
        return bus->cop2_write(bus->opaque, s, pc, control, index, value);
    (control ? s->gte_control : s->gte_data)[index & 31] = value;
    return true;
}

uint32_t pipeline_register(const PsxCpuState *s, unsigned index)
{
    if (index != 0 && s->delayed_load_pending && s->delayed_load_reg == index)
        return s->delayed_load_value;
    return gpr(s, index);
}

PSX_ALWAYS_INLINE bool direct_main_ram_offset(const PsxBus *bus,
                                              uint32_t address,
                                              unsigned size, size_t *offset)
{
    if (!bus || !offset || !bus->direct_ram_visible_bytes ||
        !bus->direct_ram_size || size == 0)
        return false;
    const uint32_t physical = address & 0x1fffffffu;
    if (physical >= *bus->direct_ram_visible_bytes) return false;
    // Power-of-two RAM is the normal PSX/ZN mapping. Callers validate guest
    // alignment before reaching this helper, and the physical address is
    // already inside the visible RAM window, so the masked offset cannot
    // straddle the allocation. Keep the full span check for unusual sizes.
    if (bus->direct_ram_mask != SIZE_MAX) {
        *offset = size_t(physical) & bus->direct_ram_mask;
        return true;
    }
    const size_t at = size_t(physical) % bus->direct_ram_size;
    if (at > bus->direct_ram_size || size > bus->direct_ram_size - at)
        return false;
    *offset = at;
    return true;
}

PSX_ALWAYS_INLINE uint32_t direct_main_ram_read(const uint8_t *bytes,
                                                size_t offset,
                                                unsigned size)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (size == 4) {
        uint32_t value;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }
    if (size == 2) {
        uint16_t value;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }
    return bytes[offset];
#else
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= uint32_t(bytes[offset + i]) << (i * 8);
    return value;
#endif
}

PSX_ALWAYS_INLINE void direct_main_ram_write(uint8_t *bytes, size_t offset,
                                             unsigned size, uint32_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (size == 4) {
        std::memcpy(bytes + offset, &value, sizeof(value));
        return;
    }
    if (size == 2) {
        const uint16_t half = static_cast<uint16_t>(value);
        std::memcpy(bytes + offset, &half, sizeof(half));
        return;
    }
    bytes[offset] = static_cast<uint8_t>(value);
#else
    for (unsigned i = 0; i < size; ++i)
        bytes[offset + i] = uint8_t(value >> (i * 8));
#endif
}

PSX_ALWAYS_INLINE bool interpreter_memory_read(
    PsxCpuState *s, const PsxBus *bus, uint32_t address, unsigned size,
    uint32_t pc, uint32_t opcode, uint32_t *value)
{
    if ((size == 2 && (address & 1)) || (size == 4 && (address & 3))) {
        s->cp0[CP0_BADVADDR] = address;
        (void)psx_aot_raise_exception(s, EXC_ADEL, pc, opcode);
        return false;
    }
    const bool cache_isolated = (s->cp0[CP0_SR] & (1u << 16)) != 0;
    size_t offset = 0;
    if (!cache_isolated && bus && bus->direct_ram &&
        direct_main_ram_offset(bus, address, size, &offset)) {
        *value = direct_main_ram_read(bus->direct_ram, offset, size);
        return true;
    }
    return psx_aot_memory_read(s, bus, address, size, pc, opcode, value);
}

PSX_ALWAYS_INLINE bool interpreter_memory_write(
    PsxCpuState *s, const PsxBus *bus, uint32_t address, unsigned size,
    uint32_t value, uint32_t pc, uint32_t opcode)
{
    if ((size == 2 && (address & 1)) || (size == 4 && (address & 3))) {
        s->cp0[CP0_BADVADDR] = address;
        (void)psx_aot_raise_exception(s, EXC_ADES, pc, opcode);
        return false;
    }
    const bool cache_isolated = (s->cp0[CP0_SR] & (1u << 16)) != 0;
    size_t offset = 0;
    if (!cache_isolated && bus && bus->direct_ram_write &&
        direct_main_ram_offset(bus, address, size, &offset)) {
        direct_main_ram_write(bus->direct_ram_write, offset, size, value);
        return true;
    }
    return psx_aot_memory_write(s, bus, address, size, value, pc, opcode);
}

PSX_ALWAYS_INLINE bool read_word(const PsxBus *bus, PsxCpuState *s,
                                uint32_t pc, uint32_t opcode,
                                uint32_t address, uint32_t *value)
{
    return interpreter_memory_read(s, bus, address, 4, pc, opcode, value);
}

PSX_ALWAYS_INLINE bool write_byte(const PsxBus *bus, PsxCpuState *s,
                                  uint32_t pc, uint32_t opcode,
                                  uint32_t address, uint8_t value)
{
    return interpreter_memory_write(s, bus, address, 1, value, pc, opcode);
}

} // namespace

extern "C" void psx_aot_reset(PsxCpuState *state)
{
    *state = PsxCpuState{};
    state->pc = 0xbfc00000u;
    state->cp0[CP0_SR] = SR_BEV;
    state->cp0[CP0_PRID] = 2;
}

extern "C" PsxAotExit psx_aot_raise_exception(PsxCpuState *s,
                                                unsigned code,
                                                uint32_t pc,
                                                uint32_t op)
{
    if (std::getenv("MAMERECOMP_ZN_TRACE_EXCEPTIONS")) {
        static unsigned traced = 0;
        if ((code == EXC_ADEL || code == EXC_ADES) && traced++ < 32) {
            const unsigned rs = (op >> 21) & 31;
            std::cerr << "PSX exception code=" << code << " pc=0x" << std::hex << pc
                      << " op=0x" << op << " badvaddr=0x" << s->cp0[CP0_BADVADDR]
                      << " rs=" << std::dec << rs << " rs_value=0x" << std::hex << s->r[rs]
                      << " r5=0x" << s->r[5] << " r9=0x" << s->r[9]
                      << " r10=0x" << s->r[10] << " r25=0x" << s->r[25]
                      << " delay_load=" << std::dec << unsigned(s->delayed_load_pending)
                      << ':' << unsigned(s->delayed_load_reg) << " branch="
                      << unsigned(s->branch_pending) << ':' << unsigned(s->branch_taken)
                      << std::dec << '\n';
        }
    }
    if ((code == EXC_ADEL || code == EXC_ADES) && g_psx_instruction_trace_enabled) {
        std::cerr << "PSX preceding instructions (oldest first):\n";
        const unsigned first = (g_psx_instruction_trace_next + kPsxInstructionTraceCapacity -
                                g_psx_instruction_trace_count) % kPsxInstructionTraceCapacity;
        for (unsigned i = 0; i < g_psx_instruction_trace_count; ++i) {
            const auto &trace = g_psx_instruction_trace[(first + i) % kPsxInstructionTraceCapacity];
            std::cerr << "  pc=0x" << std::hex << trace.pc << " op=0x" << trace.opcode
                      << " a1=0x" << trace.registers[5] << " t0=0x" << trace.registers[8]
                      << " t1=0x" << trace.registers[9] << " t2=0x" << trace.registers[10]
                      << " t3=0x" << trace.registers[11] << " t4=0x" << trace.registers[12]
                      << " t5=0x" << trace.registers[13] << " t6=0x" << trace.registers[14]
                      << " t7=0x" << trace.registers[15]
                      << " t9=0x" << trace.registers[25]
                      << " branch=" << std::dec << unsigned(trace.branch_pending)
                      << ':' << unsigned(trace.branch_taken) << "->0x" << std::hex
                      << trace.branch_target << " load=" << std::dec
                      << unsigned(trace.delayed_load_pending) << ':'
                      << unsigned(trace.delayed_load_reg) << "=0x" << std::hex
                      << trace.delayed_load_value << std::dec << '\n';
        }
    }
    uint32_t cause = s->cp0[CP0_CAUSE] & ~(CAUSE_EXC | CAUSE_BD | CAUSE_BT | CAUSE_CE);
    cause |= (code & 31u) << 2;
    // Cause.CE is only defined for Coprocessor Unusable exceptions. Reserved
    // instructions such as 0xfffffffe must leave CE clear; stale CE bits can
    // steer the BIOS exception dispatcher down the wrong path.
    if (code == EXC_CPU)
        cause |= ((op >> 26) & 3u) << 28;

    if (s->branch_pending) {
        cause |= CAUSE_BD;
        if (s->branch_taken)
            cause |= CAUSE_BT;
        s->cp0[CP0_EPC] = pc - 4;
        s->cp0[CP0_TAR] = s->branch_target;
    } else {
        if (s->delayed_load_pending && s->delayed_load_reg != 0)
            s->r[s->delayed_load_reg] = s->delayed_load_value;
        s->cp0[CP0_EPC] = pc;
    }

    s->branch_pending = 0;
    s->branch_taken = 0;
    s->delayed_load_pending = 0;
    s->delayed_load_reg = 0;
    s->delayed_load_value = 0;
    s->r[0] = 0;
    s->cp0[CP0_CAUSE] = cause;
    s->cp0[CP0_SR] = (s->cp0[CP0_SR] & ~0x3fu) |
                     ((s->cp0[CP0_SR] << 2) & 0x3fu);
    const uint32_t vector = (s->cp0[CP0_SR] & SR_BEV)
        ? (code == EXC_BP ? 0xbfc00140u : 0xbfc00180u)
        : (code == EXC_BP ? 0x80000040u : 0x80000080u);
    s->pc = vector;
    ++s->cycles;
    (void)op;
    return PSX_AOT_EXCEPTION;
}

extern "C" bool psx_aot_memory_read(PsxCpuState *s, const PsxBus *bus,
                                      uint32_t address, unsigned size,
                                      uint32_t pc, uint32_t op,
                                      uint32_t *value)
{
    if ((size == 2 && (address & 1)) || (size == 4 && (address & 3))) {
        s->cp0[CP0_BADVADDR] = address;
        (void)psx_aot_raise_exception(s, EXC_ADEL, pc, op);
        return false;
    }
    const bool cache_isolated = (s->cp0[CP0_SR] & (1u << 16)) != 0;
    size_t ram_offset = 0;
    const bool direct_ram = !cache_isolated && bus && bus->direct_ram &&
        direct_main_ram_offset(bus, address, size, &ram_offset);
    const bool ok = direct_ram
        ? (*value = direct_main_ram_read(bus->direct_ram, ram_offset, size), true)
        : cache_isolated && bus && bus->cache_read
            ? bus->cache_read(bus->opaque, address, size, value)
            : bus && bus->read && bus->read(bus->opaque, address, size, pc, value);
    if (!ok) {
        (void)psx_aot_raise_exception(s, EXC_DBE, pc, op);
        return false;
    }
    return true;
}

extern "C" bool psx_aot_memory_write(PsxCpuState *s, const PsxBus *bus,
                                       uint32_t address, unsigned size,
                                       uint32_t value, uint32_t pc,
                                       uint32_t op)
{
    if ((size == 2 && (address & 1)) || (size == 4 && (address & 3))) {
        s->cp0[CP0_BADVADDR] = address;
        (void)psx_aot_raise_exception(s, EXC_ADES, pc, op);
        return false;
    }
    const bool cache_isolated = (s->cp0[CP0_SR] & (1u << 16)) != 0;
    size_t ram_offset = 0;
    const bool direct_ram = !cache_isolated && bus && bus->direct_ram_write &&
        direct_main_ram_offset(bus, address, size, &ram_offset);
    const bool ok = direct_ram
        ? (direct_main_ram_write(bus->direct_ram_write, ram_offset, size, value), true)
        : cache_isolated && bus && bus->cache_write
            ? bus->cache_write(bus->opaque, address, size, value)
            : bus && bus->write && bus->write(bus->opaque, address, size, value, pc);
    if (!ok) {
        (void)psx_aot_raise_exception(s, EXC_DBE, pc, op);
        return false;
    }
    return true;
}

static PSX_ALWAYS_INLINE PsxAotExit interpret_one_impl(PsxCpuState *s,
                                                        const PsxBus *bus,
                                                        uint32_t op, uint32_t pc)
{
    s->pc = pc;
    if (g_psx_instruction_trace_enabled)
        psx_record_instruction_trace(s, pc, op);
    const unsigned major = op >> 26;
    const unsigned rs = (op >> 21) & 31;
    const unsigned rt = (op >> 16) & 31;
    const unsigned rd = (op >> 11) & 31;
    const unsigned shamt = (op >> 6) & 31;
    const unsigned funct = op & 63;
    const uint32_t imm = op & 0xffff;
    const int32_t simm = static_cast<int16_t>(imm);

    // Keep the primary opcode dispatch as a dense switch. The previous
    // sequence of major-opcode predicates stayed in the hot interpreter path
    // for every instruction, whereas MAME dispatches this field directly.
    switch (major) {
    case 0: {
        switch (funct) {
        case 0x00: write_register(s, rd, gpr(s, rt) << shamt); break;
        case 0x02: write_register(s, rd, gpr(s, rt) >> shamt); break;
        case 0x03: write_register(s, rd, static_cast<uint32_t>(static_cast<int32_t>(gpr(s, rt)) >> shamt)); break;
        case 0x04: write_register(s, rd, gpr(s, rt) << (gpr(s, rs) & 31)); break;
        case 0x06: write_register(s, rd, gpr(s, rt) >> (gpr(s, rs) & 31)); break;
        case 0x07: write_register(s, rd, static_cast<uint32_t>(static_cast<int32_t>(gpr(s, rt)) >> (gpr(s, rs) & 31))); break;
        case 0x08: {
            const uint32_t target = gpr(s, rs);
            advance_interpreted_instruction(s);
            schedule_interpreted_branch(s, target, true);
            break;
        }
        case 0x09: {
            const uint32_t target = gpr(s, rs);
            advance_interpreted_instruction(s);
            if (rd) s->r[rd] = pc + 8;
            schedule_interpreted_branch(s, target, true);
            break;
        }
        case 0x0c: return psx_aot_raise_exception(s, EXC_SYS, pc, op);
        case 0x0d: return psx_aot_raise_exception(s, EXC_BP, pc, op);
        case 0x10:
            write_register(s, rd, psx_aot_muldiv_result(s, true));
            break;
        case 0x11:
            psx_aot_write_hilo(s, true, gpr(s, rs));
            advance_interpreted_instruction(s);
            break;
        case 0x12:
            write_register(s, rd, psx_aot_muldiv_result(s, false));
            break;
        case 0x13:
            psx_aot_write_hilo(s, false, gpr(s, rs));
            advance_interpreted_instruction(s);
            break;
        case 0x18: case 0x19: case 0x1a: case 0x1b:
            psx_aot_muldiv_start(s,
                funct == 0x18 ? PSX_MULDIV_SIGNED :
                funct == 0x19 ? PSX_MULDIV_UNSIGNED :
                funct == 0x1a ? PSX_DIV_SIGNED : PSX_DIV_UNSIGNED,
                gpr(s, rs), gpr(s, rt));
            advance_interpreted_instruction(s);
            break;
        case 0x20: case 0x22: {
            const int64_t a = static_cast<int32_t>(gpr(s, rs));
            const int64_t b = static_cast<int32_t>(gpr(s, rt));
            const int64_t result = funct == 0x20 ? a + b : a - b;
            if (result < INT32_MIN || result > INT32_MAX)
                return psx_aot_raise_exception(s, EXC_OVF, pc, op);
            write_register(s, rd, static_cast<uint32_t>(result));
            break;
        }
        case 0x21: write_register(s, rd, gpr(s, rs) + gpr(s, rt)); break;
        case 0x23: write_register(s, rd, gpr(s, rs) - gpr(s, rt)); break;
        case 0x24: write_register(s, rd, gpr(s, rs) & gpr(s, rt)); break;
        case 0x25: write_register(s, rd, gpr(s, rs) | gpr(s, rt)); break;
        case 0x26: write_register(s, rd, gpr(s, rs) ^ gpr(s, rt)); break;
        case 0x27: write_register(s, rd, ~(gpr(s, rs) | gpr(s, rt))); break;
        case 0x2a: write_register(s, rd, static_cast<int32_t>(gpr(s, rs)) < static_cast<int32_t>(gpr(s, rt))); break;
        case 0x2b: write_register(s, rd, gpr(s, rs) < gpr(s, rt)); break;
        default: return psx_aot_raise_exception(s, EXC_RI, pc, op);
        }
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }

    case 1: case 4: case 5: case 6: case 7: {
        bool taken = false;
        bool link = false;
        if (major == 1 && (rt == 0 || rt == 1 || rt == 16 || rt == 17)) {
            taken = (rt == 0 || rt == 16)
                ? static_cast<int32_t>(gpr(s, rs)) < 0
                : static_cast<int32_t>(gpr(s, rs)) >= 0;
            link = rt == 16 || rt == 17;
        } else if (major == 4) taken = gpr(s, rs) == gpr(s, rt);
        else if (major == 5) taken = gpr(s, rs) != gpr(s, rt);
        else if (major == 6 && rt == 0) taken = static_cast<int32_t>(gpr(s, rs)) <= 0;
        else if (major == 7 && rt == 0) taken = static_cast<int32_t>(gpr(s, rs)) > 0;
        else return psx_aot_raise_exception(s, EXC_RI, pc, op);
        advance_interpreted_instruction(s);
        schedule_interpreted_branch(s, branch_target(pc, op), taken);
        if (link && taken) s->r[31] = pc + 8;
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }

    case 2: case 3: {
        const uint32_t target = ((pc + 4) & 0xf0000000u) | ((op & 0x03ffffffu) << 2);
        advance_interpreted_instruction(s);
        if (major == 3) s->r[31] = pc + 8;
        schedule_interpreted_branch(s, target, true);
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }

    case 8: case 9: case 10: case 11:
    case 12: case 13: case 14: case 15: {
        const uint32_t lhs = gpr(s, rs);
        uint32_t value = 0;
        switch (major) {
        case 8: {
            const int64_t result = static_cast<int32_t>(lhs) + static_cast<int64_t>(simm);
            if (result < INT32_MIN || result > INT32_MAX)
                return psx_aot_raise_exception(s, EXC_OVF, pc, op);
            value = static_cast<uint32_t>(result);
            break;
        }
        case 9: value = lhs + static_cast<uint32_t>(simm); break;
        case 10: value = static_cast<int32_t>(lhs) < simm; break;
        case 11: value = lhs < static_cast<uint32_t>(simm); break;
        case 12: value = lhs & imm; break;
        case 13: value = lhs | imm; break;
        case 14: value = lhs ^ imm; break;
        case 15: value = imm << 16; break;
        }
        write_register(s, rt, value);
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }

    case 0x10: { // COP0
        const unsigned subop = rs;
        bool interrupt_state_changed = false;
        if (subop == 0) {
            if (rd == 0 || rd == 1 || rd == 2 || rd == 4 || rd == 10)
                return psx_aot_raise_exception(s, EXC_RI, pc, op);
            if (!cop0_usable(s, pc, op)) return PSX_AOT_EXCEPTION;
            if (rd < 16) schedule_interpreted_load(s, rt, s->cp0[rd]);
            else advance_interpreted_instruction(s);
        } else if (subop == 4) {
            if (rd == 0 || rd == 1 || rd == 2 || rd == 4 || rd == 10)
                return psx_aot_raise_exception(s, EXC_RI, pc, op);
            if (!cop0_usable(s, pc, op)) return PSX_AOT_EXCEPTION;
            advance_interpreted_instruction(s);
            if (rd < 16)
                s->cp0[rd] = (s->cp0[rd] & ~CP0_WRITE_MASK[rd]) |
                             (gpr(s, rt) & CP0_WRITE_MASK[rd]);
            interrupt_state_changed = rd == CP0_SR || rd == CP0_CAUSE;
        } else if (subop == 8) {
            if (!cop0_usable(s, pc, op)) return PSX_AOT_EXCEPTION;
            const bool condition = ((op >> 16) & 1) != 0;
            const bool taken = !condition; // MAME's current BC0 behavior
            advance_interpreted_instruction(s);
            schedule_interpreted_branch(s, branch_target(pc, op), taken);
        } else if (subop == 16 && (op & 63) == 16) {
            if (!cop0_usable(s, pc, op)) return PSX_AOT_EXCEPTION;
            advance_interpreted_instruction(s);
            s->cp0[CP0_SR] = (s->cp0[CP0_SR] & ~0xfu) |
                             ((s->cp0[CP0_SR] >> 2) & 0xfu);
            interrupt_state_changed = true;
        } else {
            return psx_aot_raise_exception(s, EXC_RI, pc, op);
        }
        finish(s, op, pc);
        if (interrupt_state_changed)
            return check_interpreted_interrupt(s);
        return PSX_AOT_DISPATCH;
    }

    case 0x12: { // COP2/GTE
        if (!cop2_usable(s, pc, op)) return PSX_AOT_EXCEPTION;
        if (rs == 0 || rs == 2 || rs == 4 || rs == 6) {
            const bool control = rs == 2 || rs == 6;
            uint32_t value = 0;
            if (rs == 0 || rs == 2) {
                if (!cop2_read(bus, s, pc, control, rd, &value))
                    return psx_aot_raise_exception(s, EXC_RI, pc, op);
                schedule_interpreted_load(s, rt, value);
            } else {
                if (!cop2_write(bus, s, pc, control, rd, gpr(s, rt)))
                    return psx_aot_raise_exception(s, EXC_RI, pc, op);
                advance_interpreted_instruction(s);
            }
        } else if (rs == 8) {
            const bool flag = (s->gte_control[31] >> 31) != 0;
            const bool branch_on_true = ((op >> 16) & 1) != 0;
            advance_interpreted_instruction(s);
            schedule_interpreted_branch(s, branch_target(pc, op), flag == branch_on_true);
        } else if (rs >= 16) {
            if (!bus || !bus->cop2_command ||
                !bus->cop2_command(bus->opaque, s, pc, op))
                return psx_aot_raise_exception(s, EXC_RI, pc, op);
            advance_interpreted_instruction(s);
        } else {
            return psx_aot_raise_exception(s, EXC_RI, pc, op);
        }
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }
    default:
        break;
    }

    unsigned size = 0;
    bool is_load = false;
    bool is_signed = false;
    switch (major) {
    case 0x20: size = 1; is_load = true; is_signed = true; break; // LB
    case 0x21: size = 2; is_load = true; is_signed = true; break; // LH
    case 0x22: case 0x26: size = 4; is_load = true; break;        // LWL/LWR
    case 0x23: size = 4; is_load = true; break;                  // LW
    case 0x24: size = 1; is_load = true; break;                  // LBU
    case 0x25: size = 2; is_load = true; break;                  // LHU
    case 0x28: size = 1; break;                                  // SB
    case 0x29: size = 2; break;                                  // SH
    case 0x2a: case 0x2e: size = 4; break;                       // SWL/SWR
    case 0x2b: size = 4; break;                                  // SW
    default: break;
    }
    if (size) {
        uint32_t address = gpr(s, rs) + static_cast<uint32_t>(simm);
        if (is_load) {
            uint32_t value = 0;
            if (major == 0x22 || major == 0x26) {
                const uint32_t aligned = address & ~3u;
                if (!read_word(bus, s, pc, op, aligned, &value)) return PSX_AOT_EXCEPTION;
                const uint32_t old = pipeline_register(s, rt);
                const unsigned lane = address & 3;
                if (major == 0x22) {
                    switch (lane) {
                    case 0: value = (old & 0x00ffffffu) | ((value & 0xffu) << 24); break;
                    case 1: value = (old & 0x0000ffffu) | ((value & 0xffffu) << 16); break;
                    case 2: value = (old & 0x000000ffu) | ((value & 0xffffffu) << 8); break;
                    default: break;
                    }
                } else {
                    switch (lane) {
                    case 0: break;
                    case 1: value = (old & 0xff000000u) | ((value & 0xffffff00u) >> 8); break;
                    case 2: value = (old & 0xffff0000u) | ((value & 0xffff0000u) >> 16); break;
                    case 3: value = (old & 0xffffff00u) | ((value & 0xff000000u) >> 24); break;
                    }
                }
            } else if (!interpreter_memory_read(s, bus, address, size, pc, op, &value)) {
                return PSX_AOT_EXCEPTION;
            }
            if (is_signed && size == 1) value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)));
            if (is_signed && size == 2) value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
            schedule_interpreted_load(s, rt, value);
        } else if (major == 0x2a || major == 0x2e) {
            const uint32_t aligned = address & ~3u;
            const uint32_t data = gpr(s, rt);
            const unsigned lane = address & 3;
            unsigned first = 0, last = 3;
            uint32_t shifted = data;
            if (major == 0x2a) {
                last = lane;
                shifted = data >> ((3 - lane) * 8);
            } else {
                first = lane;
                shifted = data << (lane * 8);
            }
            for (unsigned byte = first; byte <= last; ++byte) {
                const unsigned shift = byte * 8;
                if (!write_byte(bus, s, pc, op, aligned + byte,
                                static_cast<uint8_t>(shifted >> shift)))
                    return PSX_AOT_EXCEPTION;
            }
            advance_interpreted_instruction(s);
        } else if (!interpreter_memory_write(s, bus, address, size, gpr(s, rt), pc, op)) {
            return PSX_AOT_EXCEPTION;
        } else {
            advance_interpreted_instruction(s);
        }
        finish(s, op, pc);
        return memory_address_can_update_irq(address)
            ? check_interpreted_interrupt(s) : PSX_AOT_DISPATCH;
    }

    if (major == 0x2f) {
        // CACHE affects the private R3000 instruction/data caches. This runtime
        // has a coherent bus rather than explicit cache arrays, so execute the
        // instruction and treat its maintenance operation as a no-op.
        advance_interpreted_instruction(s);
        finish(s, op, pc);
        return PSX_AOT_DISPATCH;
    }

    if ((major >= 0x30 && major <= 0x33) ||
        (major >= 0x38 && major <= 0x3b)) {
        const bool load = major < 0x38;
        const unsigned cop = major & 3;
        const uint32_t address = gpr(s, rs) + static_cast<uint32_t>(simm);
        const uint32_t cu = 1u << (28 + cop);
        if (!(s->cp0[CP0_SR] & cu))
            return psx_aot_raise_exception(s, EXC_CPU, pc, op);
        if (cop != 2)
            return psx_aot_raise_exception(s, EXC_RI, pc, op);
        if (address & 3) {
            s->cp0[CP0_BADVADDR] = address;
            return psx_aot_raise_exception(s, load ? EXC_ADEL : EXC_ADES, pc, op);
        }
        if (load) {
            uint32_t value = 0;
            if (!read_word(bus, s, pc, op, address, &value)) return PSX_AOT_EXCEPTION;
            advance_interpreted_instruction(s);
            if (!cop2_write(bus, s, pc, false, rt, value))
                return psx_aot_raise_exception(s, EXC_RI, pc, op);
        } else {
            uint32_t value = 0;
            if (!cop2_read(bus, s, pc, false, rt, &value))
                return psx_aot_raise_exception(s, EXC_RI, pc, op);
            if (!interpreter_memory_write(s, bus, address, 4, value, pc, op)) return PSX_AOT_EXCEPTION;
            advance_interpreted_instruction(s);
        }
        finish(s, op, pc);
        return memory_address_can_update_irq(address)
            ? check_interpreted_interrupt(s) : PSX_AOT_DISPATCH;
    }

    return psx_aot_raise_exception(s, EXC_RI, pc, op);
}

extern "C" PsxAotExit psx_aot_interpret_one(PsxCpuState *state,
                                              const PsxBus *bus,
                                              uint32_t opcode, uint32_t pc)
{
    state->pc = pc;
    if (pc & 3u) {
        state->cp0[CP0_BADVADDR] = pc;
        return psx_aot_raise_exception(state, EXC_ADEL, pc, opcode);
    }
    const PsxAotExit result = interpret_one_impl(state, bus, opcode, pc);
    state->instructions = state->cycles;
    return result;
}

namespace mamerecomp::psx {
namespace {
struct AotProfile {
    AotProfile() {
        const char *path = std::getenv("MAMERECOMP_PSX_AOT_PROFILE");
        if (path && *path) {
            output.open(path, std::ios::trunc);
            active = output.is_open();
        }
    }
    void note(uint32_t pc) {
        if (pc < 0x80000000u || pc >= 0x80800000u) return;
        if (seen.insert(pc).second) output << "0x" << std::hex << pc << '\n';
    }
    bool active = false;
    std::ofstream output;
    std::unordered_set<uint32_t> seen;
};

uint32_t read_direct_le32(const uint8_t *bytes)
{
    uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value = __builtin_bswap32(value);
#endif
    return value;
}

PSX_ALWAYS_INLINE PsxAotExit execute_one_body(PsxCpuState *state,
                                               const PsxBus *bus,
                                               const PsxAotEntry *entries,
                                               size_t count,
                                               RunCounters *counters,
                                               uint64_t cycle_limit)
{
    const bool cache_isolated = (state->cp0[12] & (1u << 16)) != 0;
    // MAME's psxcpu_device runs a tight execute_run loop and synchronizes
    // timed devices at scheduler deadlines, not on every opcode.
    if (bus && bus->check_interrupts && state->cycles >= state->next_event_cycle)
        bus->check_interrupts(bus->opaque, state, state->pc);
    constexpr uint32_t sr_ie = 1u;
    if ((state->cp0[12] & sr_ie) &&
        (state->cp0[12] & state->cp0[13] & 0x0000ff00u)) {
        const PsxAotExit result = psx_aot_raise_exception(state, 0, state->pc, 0);
        return result;
    }
    const uint32_t dispatch_pc = state->pc;

    constexpr size_t kPcSlotCount = 1u << 16;
    struct LookupCache {
        const PsxAotEntry *entries = nullptr;
        size_t count = 0;
        size_t next_index = 0;
        uint32_t next_pc = 0;
        struct PcSlot {
            uint32_t pc = 0;
            size_t index = 0;
            PsxAotEntry entry{};
            bool valid = false;
            bool found = false;
        } pc_slots[kPcSlotCount]{};
    };
    static thread_local LookupCache lookup_cache;
    if (lookup_cache.entries != entries || lookup_cache.count != count) {
        lookup_cache.entries = entries;
        lookup_cache.count = count;
        for (auto &slot : lookup_cache.pc_slots) slot.valid = false;
        lookup_cache.next_index = 0;
        lookup_cache.next_pc = 0;
    }
    size_t lo;
    const PsxAotEntry *aot_entry = nullptr;
    // Straight-line execution is the common case. Check the already known
    // adjacent entry before touching the larger direct-mapped PC cache; this
    // keeps the hot R3000A dispatch path in the small sequential working set.
    auto &pc_slot = lookup_cache.pc_slots[(state->pc >> 2) &
                                          (kPcSlotCount - 1)];
    bool remember_pc_slot = false;
    if (lookup_cache.next_pc == state->pc && lookup_cache.next_index < count &&
        entries[lookup_cache.next_index].pc == state->pc) {
        lo = lookup_cache.next_index;
        aot_entry = &entries[lo];
    } else if (pc_slot.valid && pc_slot.pc == state->pc) {
        lo = pc_slot.index;
        aot_entry = pc_slot.found ? &pc_slot.entry : nullptr;
    } else {
        remember_pc_slot = true;
        lo = 0;
        size_t hi = count;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (entries[mid].pc < state->pc) lo = mid + 1; else hi = mid;
        }
        if (lo < count && entries[lo].pc == state->pc)
            aot_entry = &entries[lo];
    }
    if (remember_pc_slot) {
        pc_slot.pc = state->pc;
        pc_slot.index = lo;
        pc_slot.valid = true;
        pc_slot.found = aot_entry != nullptr;
        if (aot_entry) {
            pc_slot.entry = *aot_entry;
            aot_entry = &pc_slot.entry;
        }
    }
    lookup_cache.next_index = lo + 1;
    lookup_cache.next_pc = dispatch_pc + 4;
    uint32_t opcode = 0;
    bool fetched = false;
    if (!cache_isolated && aot_entry && aot_entry->block) {
        bool valid_source = true;
        if (aot_entry->source_opcode_valid) {
            const uint32_t physical_pc = state->pc & 0x1fffffffu;
            const size_t ram_size = bus ? bus->direct_ram_size : 0;
            const size_t ram_offset = bus && bus->direct_ram_mask != SIZE_MAX
                ? (physical_pc & bus->direct_ram_mask) : SIZE_MAX;
            const bool direct_ram_fetch = bus && bus->direct_ram &&
                bus->direct_ram_visible_bytes &&
                physical_pc < *bus->direct_ram_visible_bytes &&
                ram_size && ram_offset != SIZE_MAX && ram_offset + 4 <= ram_size;
            valid_source = direct_ram_fetch
                ? (opcode = read_direct_le32(bus->direct_ram + ram_offset), true)
                : bus && bus->read &&
                    bus->read(bus->opaque, state->pc, 4, state->pc, &opcode);
            valid_source = valid_source && opcode == aot_entry->source_opcode;
            fetched = valid_source;
        }
        if (valid_source) {
        if (g_psx_instruction_trace_enabled)
            psx_record_instruction_trace(state, state->pc,
                aot_entry->source_opcode_valid ? opcode : 0);
        const uint64_t before_cycles = state->cycles;
        static const bool trace_aot_blocks =
            std::getenv("MAMERECOMP_PSX_TRACE_AOT_BLOCKS") != nullptr;
        static unsigned traced_aot_blocks = 0;
        const bool trace_this_block = trace_aot_blocks && traced_aot_blocks < 256;
        if (trace_this_block) {
            ++traced_aot_blocks;
            std::cerr << "PSX AOT enter pc=0x" << std::hex << state->pc
                      << " op=0x" << opcode << std::dec
                      << " offset=" << unsigned(aot_entry->entry_offset)
                      << " cycles=" << before_cycles
                      << " limit=" << cycle_limit
                      << " event=" << state->next_event_cycle << '\n';
        }
        PsxAotExit result = aot_entry->block(state, bus, cycle_limit,
                                              aot_entry->entry_offset);
        bool interpreted_fallback = false;
        if (result == PSX_AOT_INTERPRETER && state->cycles == before_cycles) {
            // A generated block can decline an interior entry (for example,
            // after a memory instruction). Retire that instruction here;
            // returning without progress makes execute_until retry the same
            // AOT entry forever.
            result = interpret_one_impl(state, bus, opcode, state->pc);
            interpreted_fallback = true;
        }
        if (trace_this_block)
            std::cerr << "PSX AOT exit pc=0x" << std::hex << state->pc
                      << std::dec << " cycles=" << state->cycles
                      << " retired=" << (state->cycles - before_cycles)
                      << " result=" << result << '\n';
        const uint64_t retired = state->cycles - before_cycles;
        if (retired && retired <= 16 &&
            state->pc == dispatch_pc + static_cast<uint32_t>(retired * 4) &&
            lo < count && entries[lo].pc == dispatch_pc &&
            lo + static_cast<size_t>(retired) < count &&
            entries[lo + static_cast<size_t>(retired)].pc == state->pc) {
            lookup_cache.next_index = lo + static_cast<size_t>(retired);
            lookup_cache.next_pc = state->pc;
        }
        if (counters) {
            if (aot_entry->native && !interpreted_fallback)
                counters->aot_instructions += retired;
            else
                counters->fallback_instructions += retired;
        }
        if (bus && bus->scheduler_yield_requested &&
            bus->scheduler_yield_requested(bus->opaque))
            return PSX_AOT_DISPATCH;
        return result;
        }
    }
    if (state->pc & 3) {
        state->cp0[8] = state->pc;
        const PsxAotExit result = psx_aot_raise_exception(state, 4, state->pc, 0);
        return result;
    }
    // MAME fetches opcodes through its separate m_instruction program space.
    const uint32_t physical_pc = state->pc & 0x1fffffffu;
    const size_t ram_size = bus ? bus->direct_ram_size : 0;
    const size_t ram_offset = bus && bus->direct_ram_mask != SIZE_MAX
        ? (physical_pc & bus->direct_ram_mask) : SIZE_MAX;
    const bool direct_ram_fetch = bus && bus->direct_ram &&
        bus->direct_ram_visible_bytes &&
        physical_pc < *bus->direct_ram_visible_bytes &&
        ram_size && ram_offset != SIZE_MAX && ram_offset + 4 <= ram_size;
    fetched = fetched || (direct_ram_fetch
        ? (opcode = read_direct_le32(bus->direct_ram +
              ram_offset), true)
        : bus && bus->read &&
              bus->read(bus->opaque, state->pc, 4, state->pc, &opcode));
    if (!fetched) {
        if (counters) ++counters->missing_fetches;
        const PsxAotExit result = psx_aot_raise_exception(state, 6, state->pc, 0);
        return result;
    }
    static AotProfile aot_profile;
    if (aot_profile.active) aot_profile.note(state->pc);
    const uint64_t before_cycles = state->cycles;
    const PsxAotExit result = interpret_one_impl(state, bus, opcode, state->pc);
    const uint64_t retired = state->cycles - before_cycles;
    if (counters) counters->fallback_instructions += retired;
    return result;
}

// Recognize a side-effect-free R3000A load / delay-slot NOP /
// BEQ-or-BNE-to-self / delay-slot NOP polling loop. The read must resolve to
// ordinary direct RAM. Stop at the next scheduler deadline so DMA, interrupts,
// and memory changes are observed before another batch is skipped. This
// runtime idle-loop optimization is independent of game PC and never bypasses
// a device read or device event.
PSX_ALWAYS_INLINE uint64_t skip_direct_ram_poll_loop(
    PsxCpuState *state, const PsxBus *bus, uint64_t target_cycle,
    bool per_instruction)
{
    struct Candidate {
        uint32_t pc = UINT32_MAX;
        unsigned visits = 0;
        unsigned phase = 0;
    };
    static thread_local Candidate candidate;
    static const bool trace_aot_blocks =
        std::getenv("MAMERECOMP_PSX_TRACE_AOT_BLOCKS") != nullptr;
    if (!state) return 0;
    if (per_instruction) {
        if (candidate.pc == UINT32_MAX || candidate.phase == 0) {
            candidate.pc = state->pc;
            candidate.phase = 1;
            candidate.visits = 0;
            return 0;
        }
        if (candidate.phase < 4 &&
            state->pc == candidate.pc + candidate.phase * 4u) {
            ++candidate.phase;
            return 0;
        }
        if (candidate.phase == 4 && state->pc == candidate.pc) {
            candidate.phase = 1;
            if (++candidate.visits < 8) return 0;
            candidate.visits = 0;
        } else {
            candidate.pc = state->pc;
            candidate.phase = 1;
            candidate.visits = 0;
            return 0;
        }
    } else {
        if (candidate.pc != state->pc) {
            if (!bus || !bus->direct_ram || !bus->direct_ram_visible_bytes ||
                state->branch_pending || state->delayed_load_pending ||
                g_psx_instruction_trace_enabled || trace_aot_blocks)
                return 0;
            candidate.pc = state->pc;
            candidate.visits = 1;
            candidate.phase = 0;
            return 0;
        }
        // A block has to return to this exact PC repeatedly before inspecting
        // its instruction words. Straight-line dispatch remains a PC compare.
        if (++candidate.visits < 8) return 0;
        candidate.visits = 0;
    }
    if (!bus || !bus->direct_ram || !bus->direct_ram_visible_bytes ||
        state->branch_pending || state->delayed_load_pending ||
        (state->cp0[CP0_SR] & (1u << 16)) ||
        g_psx_instruction_trace_enabled || trace_aot_blocks)
        return 0;
    if ((state->cp0[CP0_SR] & 1u) &&
        (state->cp0[CP0_SR] & state->cp0[CP0_CAUSE] & 0x0000ff00u))
        return 0;

    constexpr uint32_t kNop = 0;
    const uint32_t pc = state->pc;
    size_t code_offset = 0;
    if (!direct_main_ram_offset(bus, pc, 16, &code_offset)) return 0;
    const uint8_t *code = bus->direct_ram + code_offset;
    const uint32_t load = read_direct_le32(code + 0);
    if (read_direct_le32(code + 4) != kNop ||
        read_direct_le32(code + 12) != kNop)
        return 0;

    const unsigned load_op = load >> 26;
    unsigned load_size = 0;
    bool signed_load = false;
    switch (load_op) {
    case 32: load_size = 1; signed_load = true; break;  // LB
    case 33: load_size = 2; signed_load = true; break;  // LH
    case 35: load_size = 4; break;                     // LW
    case 36: load_size = 1; break;                     // LBU
    case 37: load_size = 2; break;                     // LHU
    default: return 0;
    }
    const unsigned base_reg = (load >> 21) & 31u;
    const unsigned data_reg = (load >> 16) & 31u;
    if (data_reg == 0) return 0;

    const uint32_t branch = read_direct_le32(code + 8);
    const unsigned branch_op = branch >> 26;
    if (branch_op != 4 && branch_op != 5) return 0;     // BEQ / BNE
    const unsigned branch_rs = (branch >> 21) & 31u;
    const unsigned branch_rt = (branch >> 16) & 31u;
    if (!((branch_rs == data_reg && branch_rt == 0) ||
          (branch_rt == data_reg && branch_rs == 0)))
        return 0;
    const int32_t displacement = static_cast<int32_t>(
        static_cast<int16_t>(branch & 0xffffu)) * 4;
    if (pc + 12u + static_cast<uint32_t>(displacement) != pc) return 0;

    const uint32_t address = gpr(state, base_reg) +
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(load)));
    size_t data_offset = 0;
    if ((load_size == 2 && (address & 1u)) ||
        (load_size == 4 && (address & 3u)) ||
        !direct_main_ram_offset(bus, address, load_size, &data_offset))
        return 0;
    uint32_t loaded = direct_main_ram_read(bus->direct_ram, data_offset,
                                           load_size);
    if (signed_load && load_size == 1)
        loaded = static_cast<uint32_t>(static_cast<int32_t>(
            static_cast<int8_t>(loaded)));
    else if (signed_load && load_size == 2)
        loaded = static_cast<uint32_t>(static_cast<int32_t>(
            static_cast<int16_t>(loaded)));
    const bool branch_taken = branch_op == 4 ? loaded == 0 : loaded != 0;
    if (!branch_taken) return 0;

    const uint64_t stop_cycle = std::min(target_cycle, state->next_event_cycle);
    if (stop_cycle <= state->cycles) return 0;
    constexpr uint64_t kLoopInstructions = 4;
    const uint64_t iterations = (stop_cycle - state->cycles) /
                                kLoopInstructions;
    if (!iterations) return 0;

    state->r[data_reg] = loaded;
    state->r[0] = 0;
    const uint64_t skipped = iterations * kLoopInstructions;
    state->cycles += skipped;
    return skipped;
}

PSX_ALWAYS_INLINE PsxAotExit execute_interpreter_until(PsxCpuState *state,
                                                        const PsxBus *bus,
                                                        RunCounters *counters,
                                                        uint64_t target_cycle)
{
    PsxAotExit result = PSX_AOT_DISPATCH;
    const uint64_t cycles_before = state->cycles;
    static AotProfile aot_profile;
    const bool profile_active = aot_profile.active;
    const uint8_t *const direct_ram = bus ? bus->direct_ram : nullptr;
    const size_t direct_ram_size = bus ? bus->direct_ram_size : 0;
    const size_t direct_ram_mask = bus ? bus->direct_ram_mask : SIZE_MAX;
    const size_t *const visible_ram_bytes =
        bus ? bus->direct_ram_visible_bytes : nullptr;
    const bool direct_ram_configured = direct_ram && visible_ram_bytes &&
        direct_ram_size && direct_ram_mask != SIZE_MAX;
    uint64_t idle_loop_instructions = 0;
    while (state->cycles < target_cycle &&
           !(bus && bus->scheduler_yield_requested &&
             bus->scheduler_yield_requested(bus->opaque))) {
        if (bus && bus->check_interrupts &&
            state->cycles >= state->next_event_cycle) {
            bus->check_interrupts(bus->opaque, state, state->pc);
        }
        result = check_interpreted_interrupt(state);
        if (result == PSX_AOT_EXCEPTION)
            continue;

        // Device callbacks are scheduled by cycle deadline, as in MAME. Run
        // the CPU's tight opcode loop up to that deadline without polling the
        // device scheduler again for every instruction.
        uint64_t slice_end = std::min(target_cycle, state->next_event_cycle);
        if (slice_end <= state->cycles)
            slice_end = std::min(target_cycle, state->cycles + 1);
        while (state->cycles < slice_end) {
            const uint64_t skipped = skip_direct_ram_poll_loop(
                state, bus, slice_end, true);
            if (skipped) {
                idle_loop_instructions += skipped;
                continue;
            }
            if (state->pc & 3u) {
                state->cp0[8] = state->pc;
                result = psx_aot_raise_exception(state, 4, state->pc, 0);
            } else {
                uint32_t opcode = 0;
                bool fetched = false;
                const uint32_t physical_pc = state->pc & 0x1fffffffu;
                const size_t ram_offset = direct_ram_configured
                    ? (physical_pc & direct_ram_mask) : 0;
                const bool direct_ram_fetch = direct_ram_configured &&
                    physical_pc < *visible_ram_bytes;
                fetched = direct_ram_fetch
                    ? (opcode = read_direct_le32(direct_ram + ram_offset), true)
                    : bus && bus->read &&
                        bus->read(bus->opaque, state->pc, 4, state->pc, &opcode);
                if (!fetched) {
                    if (counters) ++counters->missing_fetches;
                    result = psx_aot_raise_exception(state, 6, state->pc, 0);
                } else {
                    if (profile_active) aot_profile.note(state->pc);
                    result = interpret_one_impl(state, bus, opcode, state->pc);
                }
            }
            if (bus && bus->scheduler_yield_requested &&
                bus->scheduler_yield_requested(bus->opaque))
                break;
        }
    }
    state->instructions = state->cycles;
    if (counters) {
        const uint64_t retired = state->cycles - cycles_before;
        counters->fallback_instructions += retired - idle_loop_instructions;
        counters->idle_loop_instructions += idle_loop_instructions;
    }
    return result;
}
} // namespace

PsxAotExit execute_one(PsxCpuState *state, const PsxBus *bus,
                       const PsxAotEntry *entries, size_t entry_count,
                       RunCounters *counters)
{
    const uint64_t cycle_limit = state->cycles == UINT64_MAX
        ? UINT64_MAX : state->cycles + 1;
    RunCounters deltas{};
    const PsxAotExit result = execute_one_body(
        state, bus, entries, entry_count, counters ? &deltas : nullptr,
        cycle_limit);
    state->instructions = state->cycles;
    if (counters) {
        counters->aot_instructions += deltas.aot_instructions;
        counters->fallback_instructions += deltas.fallback_instructions;
        counters->idle_loop_instructions += deltas.idle_loop_instructions;
        counters->missing_fetches += deltas.missing_fetches;
    }
    return result;
}

PsxAotExit execute_until(PsxCpuState *state, const PsxBus *bus,
                         const PsxAotEntry *entries, size_t entry_count,
                         RunCounters *counters, uint64_t target_cycle)
{
    if (!entry_count)
        return execute_interpreter_until(state, bus, counters, target_cycle);
    static const uint64_t aot_batch_limit = [] {
        const char *text = std::getenv("MAMERECOMP_PSX_AOT_BATCH_LIMIT");
        if (!text || !*text) return uint64_t{0};
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        return end && *end == '\0' && parsed > 0
            ? static_cast<uint64_t>(parsed) : uint64_t{0};
    }();
    PsxAotExit result = PSX_AOT_DISPATCH;
    RunCounters deltas{};
    while (state->cycles < target_cycle) {
        const uint64_t skipped = skip_direct_ram_poll_loop(
            state, bus, target_cycle, false);
        if (skipped) {
            deltas.idle_loop_instructions += skipped;
            continue;
        }
        const uint64_t start_cycle = state->cycles;
        uint64_t call_cycle_limit = target_cycle;
        if (aot_batch_limit && aot_batch_limit < target_cycle - state->cycles)
            call_cycle_limit = state->cycles + aot_batch_limit;
        if (g_psx_instruction_trace_enabled && state->cycles != UINT64_MAX)
            if (call_cycle_limit > state->cycles + 1)
                call_cycle_limit = state->cycles + 1;
        result = execute_one_body(state, bus, entries, entry_count,
                                  counters ? &deltas : nullptr,
                                  call_cycle_limit);
        if (state->cycles == start_cycle) break;
        if (bus && bus->scheduler_yield_requested &&
            bus->scheduler_yield_requested(bus->opaque))
            break;
    }
    state->instructions = state->cycles;
    if (counters) {
        counters->aot_instructions += deltas.aot_instructions;
        counters->fallback_instructions += deltas.fallback_instructions;
        counters->idle_loop_instructions += deltas.idle_loop_instructions;
        counters->missing_fetches += deltas.missing_fetches;
    }
    return result;
}
} // namespace mamerecomp::psx

#undef PSX_ALWAYS_INLINE
