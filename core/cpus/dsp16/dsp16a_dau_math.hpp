// license:BSD-3-Clause
// DSP16A DAU bit-level operations used by the interpreter and regression tests.
#pragma once

#include <bit>
#include <cstdint>

namespace mamerecomp::dsp16::detail {

constexpr std::uint64_t acc36_mask = (std::uint64_t{1} << 36) - 1;
constexpr std::uint64_t alu37_mask = (std::uint64_t{1} << 37) - 1;

constexpr std::int64_t sign_extend_alu37(std::uint64_t value)
{
    value &= alu37_mask;
    if (value & (std::uint64_t{1} << 36)) value |= ~alu37_mask;
    return std::bit_cast<std::int64_t>(value);
}

// JTDSP16 RTL F2=1011: add accumulator bit 15 to bits 35:16, then
// clear bits 15:0. The 37-bit RTL path preserves the carry/sign behavior.
constexpr std::int64_t round_accumulator_f2_11(std::int64_t accumulator)
{
    const auto bits = static_cast<std::uint64_t>(accumulator) & alu37_mask;
    const auto upper = (bits >> 16) & 0x1f'ffffU;
    const auto round_bit = (bits >> 15) & 1U;
    const auto rounded_upper = (upper + round_bit) & 0x1f'ffffU;
    return sign_extend_alu37(rounded_upper << 16);
}

// JTDSP16 F2=1001: increment the source accumulator's 21-bit high field,
// then use AUC[destination+4] to choose zero or the source low word. The
// concatenated 37-bit ALU result retains the carry/sign guard bit.
constexpr std::int64_t increment_accumulator_high_f2_9(std::int64_t source,
                                                       std::uint8_t auc,
                                                       unsigned destination)
{
    const auto bits = static_cast<std::uint64_t>(source) & alu37_mask;
    const auto upper = (bits >> 16) & 0x1f'ffffU;
    const auto incremented_upper = (upper + 1U) & 0x1f'ffffU;
    // JTDSP16 wires clr_a0l/clr_a1l from the destination accumulator select.
    const bool preserve_low = (auc & (1U << (4U + (destination & 1U)))) != 0;
    const auto low = preserve_low ? bits & 0xffffU : 0U;
    return sign_extend_alu37((incremented_upper << 16) | low);
}

// AUC[4]/AUC[5] select whether A0L/A1L are preserved when the upper
// accumulator half is loaded (JTDSP16 clr_a0l/clr_a1l wiring).
constexpr std::int64_t load_accumulator_high(std::int64_t accumulator,
                                             std::int16_t high,
                                             bool preserve_low)
{
    const auto low = preserve_low
        ? static_cast<std::uint16_t>(accumulator)
        : std::uint16_t(0);
    const auto bits = (std::uint32_t(static_cast<std::uint16_t>(high)) << 16) | low;
    return static_cast<std::int32_t>(bits);
}

// AUC[1:0] = 2 maps P<<2 onto the 32-bit P bus before sign-extending it
// into the 37-bit accumulator path. Shifting a sign-extended 64-bit P value
// instead leaks discarded upper bits into the accumulator (e.g. 0xc0008000
// must become +0x00020000, not a large negative 64-bit value).
constexpr std::int64_t align_product_left_two(std::int32_t product)
{
    const auto shifted = static_cast<std::uint32_t>(product) << 2;
    return std::bit_cast<std::int32_t>(shifted);
}

constexpr std::int64_t align_product(std::int32_t product, std::uint8_t auc_alignment)
{
    switch (auc_alignment & 3U) {
    case 0: case 3: return product; // RTL treats reserved alignment 3 as 0.
    case 1: return product >> 2;
    case 2: return align_product_left_two(product);
    default: return product;
    }
}

} // namespace mamerecomp::dsp16::detail
