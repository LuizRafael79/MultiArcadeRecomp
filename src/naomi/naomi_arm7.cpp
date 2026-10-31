// ARM7TDMI interpreter adapted from Flycast's AICA ARM core.
// Upstream interpreter implementation: core/third_party/flycast_arm7/arm-new.h
// Source is licensed GPL-2.0-or-later; see the retained upstream notice.

#include "naomi_arm7.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mamerecomp::naomi::arm7 {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

enum Arm7Reg {
    RN_LR = 14, RN_PC = 15, RN_CPSR = 16, RN_SPSR = 17,
    R13_IRQ = 18, R14_IRQ = 19, SPSR_IRQ = 20,
    R13_USR = 26, R14_USR = 27, SPSR_SVC = 30,
    R13_SVC = 28, R14_SVC = 29,
    R13_ABT = 31, R14_ABT = 32, SPSR_ABT = 33,
    R13_UND = 34, R14_UND = 35, SPSR_UND = 36,
    R8_FIQ = 37, R9_FIQ = 38, R10_FIQ = 39, R11_FIQ = 40,
    R12_FIQ = 41, R13_FIQ = 42, R14_FIQ = 43, SPSR_FIQ = 44,
    RN_PSR_FLAGS = 45, R15_ARM_NEXT = 46, INTR_PEND = 47,
    CYCL_CNT = 48, RN_SCRATCH = 49, RN_ARM_REG_COUNT,
};

union RegPair {
    struct { u8 B0, B1, B2, B3; } B;
    struct { u16 W0, W1; } W;
    union {
        struct { u32 : 28; u32 V : 1; u32 C : 1; u32 Z : 1; u32 N : 1; };
        struct { u32 : 28; u32 NZCV : 4; };
    } FLG;
    struct {
        u32 M : 5; u32 : 1; u32 F : 1; u32 I : 1; u32 : 20; u32 NZCV : 4;
    } PSR;
    u32 I;
};

alignas(8) RegPair arm_Reg[RN_ARM_REG_COUNT]{};
std::array<u8, 256> cpuBitsSet{};
Read memory_read;
Write memory_write;
std::uint64_t instruction_count = 0;
std::uint64_t undefined_instruction_count = 0;
int arm7ClockTicks = 0;
bool armIrqEnable = true;
bool armFiqEnable = false;
int armMode = 0x13;
bool Arm7Enabled = false;
bool armFiqLine = false;
u32 current_instruction_pc = 0;

#define reg arm_Reg
#define armNextPC reg[R15_ARM_NEXT].I
#define N_FLAG (reg[RN_PSR_FLAGS].FLG.N)
#define Z_FLAG (reg[RN_PSR_FLAGS].FLG.Z)
#define C_FLAG (reg[RN_PSR_FLAGS].FLG.C)
#define V_FLAG (reg[RN_PSR_FLAGS].FLG.V)

template <typename T>
T readMem(u32 address) {
    address &= 0x00ffffffu;
    if (!memory_read) return 0;
    if (address < 0x00800000u) {
        constexpr u32 lane_mask = static_cast<u32>(sizeof(T) - 1u);
        const u32 aligned = address & (0x007fffffu & ~lane_mask);
        auto value = static_cast<T>(memory_read(aligned, sizeof(T)));
        if constexpr (sizeof(T) == 4) {
            if (address & 3u) {
                const unsigned shift = (address & 3u) * 8u;
                value = static_cast<T>((value >> shift) | (value << (32u - shift)));
            }
        }
        return value;
    }
    return static_cast<T>(memory_read(address, sizeof(T)));
}

template <typename T>
void writeMem(u32 address, T value) {
    address &= 0x00ffffffu;
    if (!memory_write) return;
    if (address < 0x00800000u) {
        constexpr u32 lane_mask = static_cast<u32>(sizeof(T) - 1u);
        address &= (0x007fffffu & ~lane_mask);
    }
    memory_write(address, sizeof(T), static_cast<u32>(value), current_instruction_pc);
}

#define CPUReadMemoryQuick(address) readMem<u32>(address)
#define CPUReadByte readMem<u8>
#define CPUReadMemory readMem<u32>
#define CPUReadHalfWord readMem<u16>
#define CPUReadHalfWordSigned(address) static_cast<s16>(readMem<u16>(address))
#define CPUWriteMemory writeMem<u32>
#define CPUWriteHalfWord writeMem<u16>
#define CPUWriteByte writeMem<u8>
#define CPUUpdateTicksAccessSeq32(address) 1
#define CPUUpdateTicksAccess32(address) 1
#define CPUUpdateTicksAccess16(address) 1
#define verify(condition) assert(condition)

void CPUUpdateCPSR();
void CPUFiq();

void CPUSwap(u32* a, u32* b) {
    const u32 value = *b;
    *b = *a;
    *a = value;
}

void update_armintc() {
    reg[INTR_PEND].I = armFiqLine && armFiqEnable ? 1u : 0u;
}

void CPUUpdateFlags() {
    const u32 cpsr = reg[RN_CPSR].I;
    reg[RN_PSR_FLAGS].FLG.NZCV = reg[RN_CPSR].PSR.NZCV;
    armIrqEnable = (cpsr & 0x80u) == 0;
    armFiqEnable = (cpsr & 0x40u) == 0;
    update_armintc();
}

void CPUUpdateCPSR() {
    RegPair cpsr{};
    cpsr.I = reg[RN_CPSR].I & 0x40u;
    cpsr.PSR.NZCV = reg[RN_PSR_FLAGS].FLG.NZCV;
    if (!armFiqEnable) cpsr.I |= 0x40u;
    if (!armIrqEnable) cpsr.I |= 0x80u;
    cpsr.PSR.M = static_cast<u32>(armMode);
    reg[RN_CPSR].I = cpsr.I;
}

void CPUSwitchMode(int mode, bool saveState) {
    CPUUpdateCPSR();
    switch (armMode) {
    case 0x10: case 0x1f:
        reg[R13_USR].I = reg[13].I; reg[R14_USR].I = reg[14].I;
        reg[RN_SPSR].I = reg[RN_CPSR].I; break;
    case 0x11:
        CPUSwap(&reg[R8_FIQ].I, &reg[8].I); CPUSwap(&reg[R9_FIQ].I, &reg[9].I);
        CPUSwap(&reg[R10_FIQ].I, &reg[10].I); CPUSwap(&reg[R11_FIQ].I, &reg[11].I);
        CPUSwap(&reg[R12_FIQ].I, &reg[12].I); reg[R13_FIQ].I = reg[13].I;
        reg[R14_FIQ].I = reg[14].I; reg[SPSR_FIQ].I = reg[RN_SPSR].I; break;
    case 0x12: reg[R13_IRQ].I = reg[13].I; reg[R14_IRQ].I = reg[14].I;
        reg[SPSR_IRQ].I = reg[RN_SPSR].I; break;
    case 0x13: reg[R13_SVC].I = reg[13].I; reg[R14_SVC].I = reg[14].I;
        reg[SPSR_SVC].I = reg[RN_SPSR].I; break;
    case 0x17: reg[R13_ABT].I = reg[13].I; reg[R14_ABT].I = reg[14].I;
        reg[SPSR_ABT].I = reg[RN_SPSR].I; break;
    case 0x1b: reg[R13_UND].I = reg[13].I; reg[R14_UND].I = reg[14].I;
        reg[SPSR_UND].I = reg[RN_SPSR].I; break;
    }

    const u32 cpsr = reg[RN_CPSR].I;
    const u32 spsr = reg[RN_SPSR].I;
    switch (mode) {
    case 0x10: case 0x1f:
        reg[13].I = reg[R13_USR].I; reg[14].I = reg[R14_USR].I;
        reg[RN_CPSR].I = spsr; break;
    case 0x11:
        CPUSwap(&reg[8].I, &reg[R8_FIQ].I); CPUSwap(&reg[9].I, &reg[R9_FIQ].I);
        CPUSwap(&reg[10].I, &reg[R10_FIQ].I); CPUSwap(&reg[11].I, &reg[R11_FIQ].I);
        CPUSwap(&reg[12].I, &reg[R12_FIQ].I); reg[13].I = reg[R13_FIQ].I;
        reg[14].I = reg[R14_FIQ].I;
        reg[RN_SPSR].I = saveState ? cpsr : reg[SPSR_FIQ].I; break;
    case 0x12:
        reg[13].I = reg[R13_IRQ].I; reg[14].I = reg[R14_IRQ].I;
        reg[RN_CPSR].I = spsr; reg[RN_SPSR].I = saveState ? cpsr : reg[SPSR_IRQ].I; break;
    case 0x13:
        reg[13].I = reg[R13_SVC].I; reg[14].I = reg[R14_SVC].I;
        reg[RN_CPSR].I = spsr; reg[RN_SPSR].I = saveState ? cpsr : reg[SPSR_SVC].I; break;
    case 0x17:
        reg[13].I = reg[R13_ABT].I; reg[14].I = reg[R14_ABT].I;
        reg[RN_CPSR].I = spsr; reg[RN_SPSR].I = saveState ? cpsr : reg[SPSR_ABT].I; break;
    case 0x1b:
        reg[13].I = reg[R13_UND].I; reg[14].I = reg[R14_UND].I;
        reg[RN_CPSR].I = spsr; reg[RN_SPSR].I = saveState ? cpsr : reg[SPSR_UND].I; break;
    default:
        std::fprintf(stderr, "ARM7 unsupported mode=%02x pc=%08x\n", mode, armNextPC);
        Arm7Enabled = false; return;
    }
    armMode = mode;
    CPUUpdateFlags();
    CPUUpdateCPSR();
}

void CPUSoftwareInterrupt(int) {
    const u32 pc = armNextPC + 4;
    CPUSwitchMode(0x13, true);
    reg[14].I = pc;
    armIrqEnable = false;
    armNextPC = 0x08;
}

void CPUUndefinedException() {
    ++undefined_instruction_count;
    std::fprintf(stderr, "ARM7 undefined instruction pc=%08x opcode=%08x\n",
                 armNextPC - 4, reg[RN_SCRATCH].I);
    const u32 pc = armNextPC + 4;
    CPUSwitchMode(0x1b, true);
    reg[14].I = pc;
    armIrqEnable = false;
    armNextPC = 0x04;
}

void CPUFiq() {
    const u32 pc = armNextPC + 4;
    CPUSwitchMode(0x11, true);
    reg[14].I = pc;
    armIrqEnable = false;
    armFiqEnable = false;
    update_armintc();
    armNextPC = 0x1c;
}

void reset() {
    std::memset(arm_Reg, 0, sizeof(arm_Reg));
    armMode = 0x13;
    reg[13].I = 0x03007f00;
    reg[15].I = 0;
    reg[RN_CPSR].I = 0;
    reg[R13_IRQ].I = 0x03007fa0;
    reg[R13_SVC].I = 0x03007fe0;
    armIrqEnable = true;
    armFiqEnable = false;
    reg[RN_CPSR].I |= 0x40u;
    CPUUpdateCPSR();
    armNextPC = reg[15].I;
    reg[15].I += 4;
    arm7ClockTicks = 0;
    undefined_instruction_count = 0;
}

void runInterpreter(u32 cycleCount) {
    if (!Arm7Enabled) return;
    arm7ClockTicks -= static_cast<int>(cycleCount);
    while (arm7ClockTicks < 0 && Arm7Enabled) {
        if (reg[INTR_PEND].I) CPUFiq();
        reg[15].I = armNextPC + 8;
        int& clockTicks = arm7ClockTicks;
        reg[RN_SCRATCH].I = 0;
        current_instruction_pc = armNextPC;
        #include "../../core/third_party/flycast_arm7/arm-new.h"
        ++instruction_count;
    }
}

void init_bit_count() {
    for (std::size_t value = 0; value < cpuBitsSet.size(); ++value) {
        unsigned count = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            count += (value >> bit) & 1u;
        cpuBitsSet[value] = static_cast<u8>(count);
    }
}

} // namespace

void bind_memory(Read read, Write write) {
    memory_read = std::move(read);
    memory_write = std::move(write);
    init_bit_count();
}

void set_enabled(bool enabled) {
    if (!Arm7Enabled && enabled) reset();
    Arm7Enabled = enabled;
}

void set_fiq_line(bool asserted) {
    armFiqLine = asserted;
    update_armintc();
}

void run_cycles(std::uint32_t cycles) {
    runInterpreter(cycles);
}

void unbind_memory() {
    Arm7Enabled = false;
    memory_read = {};
    memory_write = {};
}

std::uint64_t instructions_executed() { return instruction_count; }
std::uint32_t program_counter() { return armNextPC; }
std::uint32_t instruction_address() { return current_instruction_pc; }
std::array<std::uint32_t, 16> register_snapshot() {
    std::array<std::uint32_t, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = arm_Reg[i].I;
    return result;
}
std::uint64_t undefined_instructions() { return undefined_instruction_count; }

} // namespace mamerecomp::naomi::arm7
