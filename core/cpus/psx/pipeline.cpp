// license:BSD-3-Clause
// copyright-holders:smf
// Adapted from the R3000A MULT/DIV and HI/LO behavior in MAME's
// src/devices/cpu/psx/psx.cpp.
#include "psx_aot_abi.h"

#include <cstdint>

namespace {
void complete_muldiv(PsxCpuState *state)
{
    const uint32_t lhs = state->multiplier_operand1;
    const uint32_t rhs = state->multiplier_operand2;
    switch (state->multiplier_operation) {
    case PSX_MULDIV_SIGNED: {
        const int64_t product = static_cast<int64_t>(static_cast<int32_t>(lhs)) *
                                static_cast<int64_t>(static_cast<int32_t>(rhs));
        const uint64_t bits = static_cast<uint64_t>(product);
        state->lo = static_cast<uint32_t>(bits);
        state->hi = static_cast<uint32_t>(bits >> 32);
        break;
    }
    case PSX_MULDIV_UNSIGNED: {
        const uint64_t product = static_cast<uint64_t>(lhs) * rhs;
        state->lo = static_cast<uint32_t>(product);
        state->hi = static_cast<uint32_t>(product >> 32);
        break;
    }
    case PSX_DIV_SIGNED:
        if (lhs == 0x80000000u && rhs == 0xffffffffu) {
            state->lo = 0x80000000u;
            state->hi = 0;
        } else if (rhs == 0) {
            state->lo = static_cast<int32_t>(lhs) < 0 ? 1u : 0xffffffffu;
            state->hi = lhs;
        } else {
            state->lo = static_cast<uint32_t>(static_cast<int32_t>(lhs) /
                                              static_cast<int32_t>(rhs));
            state->hi = static_cast<uint32_t>(static_cast<int32_t>(lhs) %
                                              static_cast<int32_t>(rhs));
        }
        break;
    case PSX_DIV_UNSIGNED:
        if (rhs == 0) {
            state->lo = 0xffffffffu;
            state->hi = lhs;
        } else {
            state->lo = lhs / rhs;
            state->hi = lhs % rhs;
        }
        break;
    default:
        break;
    }
    state->multiplier_operation = PSX_MULDIV_IDLE;
}
}

extern "C" void psx_aot_advance(PsxCpuState *state)
{
    if (state->branch_pending) {
        if (state->branch_taken)
            state->pc = state->branch_target;
        else
            state->pc += 4;
        state->branch_pending = 0;
        state->branch_taken = 0;
        return;
    }

    if (state->delayed_load_pending) {
        const unsigned reg = state->delayed_load_reg;
        if (reg != 0 && reg < 32)
            state->r[reg] = state->delayed_load_value;
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }

    state->pc += 4;
}

extern "C" void psx_aot_schedule_load(PsxCpuState *state, unsigned reg,
                                       uint32_t value)
{
    // MAME delayed_load() cancels a previous same-register load before
    // advancing the pipeline. This is observable for consecutive loads.
    if (state->delayed_load_pending && state->delayed_load_reg == reg) {
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }

    psx_aot_advance(state);

    if (reg != 0 && reg < 32) {
        state->delayed_load_reg = (uint8_t)reg;
        state->delayed_load_pending = 1;
        state->delayed_load_value = value;
    }
}

extern "C" void psx_aot_schedule_branch(PsxCpuState *state, uint32_t target,
                                         bool taken)
{
    // The caller has already advanced to the delay-slot instruction. Keep
    // taken separately because MAME exposes it in Cause.BT on delay exceptions.
    state->branch_target = taken ? target : state->pc + 4;
    state->branch_pending = 1;
    state->branch_taken = taken ? 1 : 0;
}

extern "C" void psx_aot_muldiv_start(PsxCpuState *state,
                                      PsxMulDivOperation operation,
                                      uint32_t lhs, uint32_t rhs)
{
    state->multiplier_operand1 = lhs;
    state->multiplier_operand2 = rhs;
    state->multiplier_operation = static_cast<uint8_t>(operation);
    state->lo = lhs;
    if (operation == PSX_DIV_SIGNED || operation == PSX_DIV_UNSIGNED)
        state->hi = 0;
}

extern "C" uint32_t psx_aot_muldiv_result(PsxCpuState *state, bool high)
{
    if (state->multiplier_operation != PSX_MULDIV_IDLE)
        complete_muldiv(state);
    return high ? state->hi : state->lo;
}

extern "C" void psx_aot_write_hilo(PsxCpuState *state, bool high,
                                    uint32_t value)
{
    state->multiplier_operation = PSX_MULDIV_IDLE;
    if (high) state->hi = value;
    else state->lo = value;
}

extern "C" void psx_aot_retire(PsxCpuState *state, uint32_t, uint32_t)
{
    ++state->cycles;
}
