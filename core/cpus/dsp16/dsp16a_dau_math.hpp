// license:BSD-3-Clause
// DSP16A DAU bit-level operations used by the interpreter and regression tests.
#pragma once

#include <bit>
#include <cstdint>

namespace mamerecomp::dsp16::detail {

// JTDSP16 RTL F2=1011: add accumulator bit 15 to bits 35:16, then
// clear bits 15:0. The 37-bit RTL path preserves the carry/sign behavior.
constexpr std::int64_t round_accumulator_f2_11(std::int64_t accumulator)
{
    const std::int64_t upper = accumulator >> 16;
    const auto round_bit = (static_cast<std::uint64_t>(accumulator) >> 15) & 1U;
    return (upper + static_cast<std::int64_t>(round_bit)) * 0x1'0000;
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
