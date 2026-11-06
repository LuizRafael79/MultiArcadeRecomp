#include "psx_aot_abi.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace {
struct TestMemory {
    std::array<uint8_t, 0x1000> bytes{};
};

bool read_memory(void *opaque, uint32_t address, unsigned size,
                 uint32_t, uint32_t *value)
{
    auto &memory = *static_cast<TestMemory *>(opaque);
    if (address + size > memory.bytes.size()) return false;
    *value = 0;
    for (unsigned i = 0; i < size; ++i)
        *value |= static_cast<uint32_t>(memory.bytes[address + i]) << (i * 8);
    return true;
}

bool write_memory(void *opaque, uint32_t address, unsigned size,
                  uint32_t value, uint32_t)
{
    auto &memory = *static_cast<TestMemory *>(opaque);
    if (address + size > memory.bytes.size()) return false;
    for (unsigned i = 0; i < size; ++i)
        memory.bytes[address + i] = static_cast<uint8_t>(value >> (i * 8));
    return true;
}

PsxBus make_bus(TestMemory &memory)
{
    PsxBus bus{};
    bus.opaque = &memory;
    bus.read = read_memory;
    bus.write = write_memory;
    return bus;
}
}

int main()
{
    {
        PsxCpuState state{};
        psx_aot_reset(&state);
        assert(state.pc == 0xbfc00000u);
        assert(state.cp0[12] == (1u << 22));
        assert(state.cp0[15] == 2);
    }

    {
        PsxCpuState state{};
        state.pc = 0x1000;
        const uint32_t addiu_t0_zero_7 = (9u << 26) | (8u << 16) | 7u;
        assert(psx_aot_interpret_one(&state, nullptr, addiu_t0_zero_7, state.pc) == PSX_AOT_DISPATCH);
        assert(state.r[8] == 7);
        assert(state.pc == 0x1004);
        assert(state.instructions == 1);
    }

    {
        PsxCpuState state{};
        state.pc = 0x1800;
        const uint32_t cache_op = (0x2fu << 26) | (1u << 21) | (8u << 16);
        assert(psx_aot_interpret_one(&state, nullptr, cache_op, state.pc) == PSX_AOT_DISPATCH);
        assert(state.pc == 0x1804);
        assert(state.instructions == 1);
    }

    {
        TestMemory memory;
        memory.bytes[0x200] = 0x78;
        memory.bytes[0x201] = 0x56;
        memory.bytes[0x202] = 0x34;
        memory.bytes[0x203] = 0x12;
        PsxBus bus = make_bus(memory);
        PsxCpuState state{};
        state.pc = 0x2000;
        state.r[1] = 0x200;
        const uint32_t lw_t0_0_t1 = (0x23u << 26) | (1u << 21) | (8u << 16);
        assert(psx_aot_interpret_one(&state, &bus, lw_t0_0_t1, state.pc) == PSX_AOT_DISPATCH);
        assert(state.r[8] == 0);
        assert(state.delayed_load_pending);
        const uint32_t addiu_t2_zero_1 = (9u << 26) | (10u << 16) | 1u;
        assert(psx_aot_interpret_one(&state, &bus, addiu_t2_zero_1, state.pc) == PSX_AOT_DISPATCH);
        assert(state.r[8] == 0x12345678u);
        assert(state.r[10] == 1);
    }

    {
        PsxCpuState state{};
        state.pc = 0x3000;
        state.r[1] = 0x7fffffffu;
        state.r[2] = 1;
        const uint32_t add_t0_t1_t2 = (1u << 21) | (2u << 16) | (8u << 11) | 0x20u;
        assert(psx_aot_interpret_one(&state, nullptr, add_t0_t1_t2, state.pc) == PSX_AOT_EXCEPTION);
        assert(state.pc == 0x80000080u);
        assert(state.cp0[14] == 0x3000u);
        assert(((state.cp0[13] >> 2) & 31) == 12);
        assert((state.cp0[13] & (3u << 28)) == 0);
    }

    {
        PsxCpuState state{};
        state.pc = 0x3800;
        state.r[1] = 0xfffffffeu;
        state.r[2] = 3;
        const uint32_t mult = (1u << 21) | (2u << 16) | 0x18u;
        assert(psx_aot_interpret_one(&state, nullptr, mult, state.pc) == PSX_AOT_DISPATCH);
        assert(state.multiplier_operation == PSX_MULDIV_SIGNED);
        const uint32_t mflo_t0 = (8u << 11) | 0x12u;
        assert(psx_aot_interpret_one(&state, nullptr, mflo_t0, state.pc) == PSX_AOT_DISPATCH);
        assert(state.r[8] == 0xfffffffau);
        assert(state.hi == 0xffffffffu);
        assert(state.multiplier_operation == PSX_MULDIV_IDLE);
    }

    {
        PsxCpuState state{};
        state.pc = 0x3900;
        state.r[1] = 0x12345678u;
        state.r[2] = 0;
        const uint32_t divu = (1u << 21) | (2u << 16) | 0x1bu;
        assert(psx_aot_interpret_one(&state, nullptr, divu, state.pc) == PSX_AOT_DISPATCH);
        const uint32_t mflo_t0 = (8u << 11) | 0x12u;
        assert(psx_aot_interpret_one(&state, nullptr, mflo_t0, state.pc) == PSX_AOT_DISPATCH);
        assert(state.lo == 0xffffffffu);
        assert(state.hi == 0x12345678u);
    }

    {
        PsxCpuState state{};
        state.pc = 0x4000;
        psx_aot_advance(&state);
        psx_aot_schedule_branch(&state, 0x5000, true);
        const uint32_t reserved = 0xffffffffu;
        assert(psx_aot_interpret_one(&state, nullptr, reserved, state.pc) == PSX_AOT_EXCEPTION);
        assert(state.cp0[14] == 0x4000u);
        assert(state.cp0[13] & (1u << 31));
        assert(state.cp0[13] & (1u << 30));
    }

    {
        PsxCpuState state{};
        state.pc = 0x3a00;
        const uint32_t mfc2_t0_d0 = (0x12u << 26) | (8u << 16);
        assert(psx_aot_interpret_one(&state, nullptr, mfc2_t0_d0, state.pc) == PSX_AOT_EXCEPTION);
        assert(((state.cp0[13] >> 2) & 31) == 11);
        assert(((state.cp0[13] >> 28) & 3) == 2);
    }

    {
        TestMemory memory;
        PsxBus bus = make_bus(memory);
        PsxCpuState state{};
        state.pc = 0x6000;
        state.r[1] = 0x201;
        const uint32_t lh = (0x21u << 26) | (1u << 21) | (8u << 16);
        assert(psx_aot_interpret_one(&state, &bus, lh, state.pc) == PSX_AOT_EXCEPTION);
        assert(state.cp0[8] == 0x201u);
        assert(((state.cp0[13] >> 2) & 31) == 4);
    }
}
