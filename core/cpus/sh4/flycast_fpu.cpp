// SH-4 FPU interpreter operations adapted from Flycast's
// core/hw/sh4/interpr/sh4_fpu.cpp and sh4_rom.cpp.
// The AOT path continues to use its existing host_f* helpers.

#include "flycast_fpu.hpp"

#include "naomi/naomi_sh4_context.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace multiarcaderecomp::cpus::sh4::flycast::fpu {
namespace {

constexpr std::array<std::uint32_t, 0x8000> kSinCoefficients = {
#include "../../third_party/flycast-sh4/fsca-table.h"
};

float flycast_sine(std::uint32_t index) {
    index &= 0xffffu;
    auto bits = kSinCoefficients[index & 0x7fffu];
    if (index & 0x8000u) bits ^= 0x80000000u;
    return std::bit_cast<float>(bits);
}

auto& active_float_bank(multiarcaderecomp::naomi::Sh4Context& context) {
    return context.fpu_bank_x ? context.xf : context.frf;
}

float flush_denormal(float value, bool enabled) {
    return enabled && std::fpclassify(value) == FP_SUBNORMAL
        ? std::copysign(0.0f, value) : value;
}

double flush_denormal(double value, bool enabled) {
    return enabled && std::fpclassify(value) == FP_SUBNORMAL
        ? std::copysign(0.0, value) : value;
}

double read_double(multiarcaderecomp::naomi::Sh4Context& context, unsigned index) {
    auto& bank = active_float_bank(context);
    index &= 14u;
    const std::uint64_t high = std::bit_cast<std::uint32_t>(bank[index]);
    const std::uint64_t low = std::bit_cast<std::uint32_t>(bank[index + 1]);
    return std::bit_cast<double>((high << 32) | low);
}

void write_double(multiarcaderecomp::naomi::Sh4Context& context, unsigned index,
                  double value) {
    auto& bank = active_float_bank(context);
    index &= 14u;
    const auto bits = std::bit_cast<std::uint64_t>(value);
    bank[index] = std::bit_cast<float>(static_cast<std::uint32_t>(bits >> 32));
    bank[index + 1] = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
}

template <typename FloatOperation, typename DoubleOperation>
void binary_float(multiarcaderecomp::naomi::Sh4Context& context, unsigned n,
                  unsigned m, FloatOperation float_operation,
                  DoubleOperation double_operation) {
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    if (context.fpscr_pr) {
        const auto lhs = flush_denormal(read_double(context, n), flush);
        const auto rhs = flush_denormal(read_double(context, m), flush);
        write_double(context, n,
                     flush_denormal(double_operation(lhs, rhs), flush));
    } else {
        auto& fr = active_float_bank(context);
        const auto lhs = flush_denormal(fr[n & 15u], flush);
        const auto rhs = flush_denormal(fr[m & 15u], flush);
        fr[n & 15u] = flush_denormal(float_operation(lhs, rhs), flush);
    }
}

} // namespace

void fadd(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    binary_float(context, n, m, [](float a, float b) { return a + b; },
                  [](double a, double b) { return a + b; });
}

void fsub(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    binary_float(context, n, m, [](float a, float b) { return a - b; },
                  [](double a, double b) { return a - b; });
}

void fmul(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    binary_float(context, n, m, [](float a, float b) { return a * b; },
                  [](double a, double b) { return a * b; });
}

void fdiv(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    binary_float(context, n, m, [](float a, float b) { return a / b; },
                  [](double a, double b) { return a / b; });
}

void fcmp_eq(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    if (context.fpscr_pr) {
        context.t = flush_denormal(read_double(context, n), flush) ==
                    flush_denormal(read_double(context, m), flush);
    }
    else {
        auto& fr = active_float_bank(context);
        context.t = flush_denormal(fr[n & 15u], flush) ==
                    flush_denormal(fr[m & 15u], flush);
    }
}

void fcmp_gt(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    if (context.fpscr_pr) {
        context.t = flush_denormal(read_double(context, n), flush) >
                    flush_denormal(read_double(context, m), flush);
    }
    else {
        auto& fr = active_float_bank(context);
        context.t = flush_denormal(fr[n & 15u], flush) >
                    flush_denormal(fr[m & 15u], flush);
    }
}

void fneg(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    auto& fr = active_float_bank(context);
    const unsigned index = n & 15u;
    auto bits = std::bit_cast<std::uint32_t>(fr[index]);
    bits ^= 0x80000000u;
    fr[index] = std::bit_cast<float>(bits);
}

void fabs(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    auto& fr = active_float_bank(context);
    const unsigned index = context.fpscr_pr ? (n & 14u) : (n & 15u);
    auto bits = std::bit_cast<std::uint32_t>(fr[index]);
    bits &= 0x7fffffffu;
    fr[index] = std::bit_cast<float>(bits);
}

void fsqrt(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    if (context.fpscr_pr) {
        const auto input = flush_denormal(read_double(context, n), flush);
        write_double(context, n, flush_denormal(std::sqrt(input), flush));
    }
    else {
        auto& fr = active_float_bank(context);
        const auto input = flush_denormal(fr[n & 15u], flush);
        fr[n & 15u] = flush_denormal(std::sqrt(input), flush);
    }
}

void fmac(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m) {
    if (context.fpscr_pr) return; // Flycast marks the DR form unimplemented.
    auto& fr = active_float_bank(context);
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    const auto a = flush_denormal(fr[0], flush);
    const auto b = flush_denormal(fr[m & 15u], flush);
    const auto c = flush_denormal(fr[n & 15u], flush);
    fr[n & 15u] = flush_denormal(std::fma(a, b, c), flush);
}

void load_fpscr(multiarcaderecomp::naomi::Sh4Context& context, std::uint32_t value) {
    // Flycast's Sh4Context::UpdateFPSCR clears the cause field when FPSCR is
    // loaded or one of the FR/SZ mode bits is changed.
    context.fpscr = (value & 0x003fffffu) & ~0x0003f000u;
    context.fpscr_pr = (context.fpscr & (1u << 19)) != 0;
    context.fpu_bank_x = (context.fpscr & (1u << 21)) != 0;
}

void toggle_fr(multiarcaderecomp::naomi::Sh4Context& context) {
    load_fpscr(context, context.fpscr ^ (1u << 21));
}

void toggle_sz(multiarcaderecomp::naomi::Sh4Context& context) {
    load_fpscr(context, context.fpscr ^ (1u << 20));
}

void fipr(multiarcaderecomp::naomi::Sh4Context& context, unsigned m, unsigned n) {
    if (context.fpscr_pr) return; // Flycast does not implement PR=1 FIPR.
    auto& fr = active_float_bank(context);
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    const auto mb = m & 12u;
    const auto nb = n & 12u;
    double result = static_cast<double>(flush_denormal(fr[nb], flush)) *
                    flush_denormal(fr[mb], flush);
    result += static_cast<double>(flush_denormal(fr[nb + 1], flush)) *
              flush_denormal(fr[mb + 1], flush);
    result += static_cast<double>(flush_denormal(fr[nb + 2], flush)) *
              flush_denormal(fr[mb + 2], flush);
    result += static_cast<double>(flush_denormal(fr[nb + 3], flush)) *
              flush_denormal(fr[mb + 3], flush);
    fr[nb + 3] = flush_denormal(static_cast<float>(result), flush);
}

void ftrv(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    if (context.fpscr_pr) return; // Flycast does not implement PR=1 FTRV.
    auto& fr = active_float_bank(context);
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    const auto base = n & 12u;
    const auto x = context.xf;
    const double v0 = flush_denormal(fr[base], flush);
    const double v1 = flush_denormal(fr[base + 1], flush);
    const double v2 = flush_denormal(fr[base + 2], flush);
    const double v3 = flush_denormal(fr[base + 3], flush);
    const double result[4] = {
        static_cast<double>(flush_denormal(x[0], flush)) * v0 + static_cast<double>(flush_denormal(x[4], flush)) * v1 +
            static_cast<double>(flush_denormal(x[8], flush)) * v2 + static_cast<double>(flush_denormal(x[12], flush)) * v3,
        static_cast<double>(flush_denormal(x[1], flush)) * v0 + static_cast<double>(flush_denormal(x[5], flush)) * v1 +
            static_cast<double>(flush_denormal(x[9], flush)) * v2 + static_cast<double>(flush_denormal(x[13], flush)) * v3,
        static_cast<double>(flush_denormal(x[2], flush)) * v0 + static_cast<double>(flush_denormal(x[6], flush)) * v1 +
            static_cast<double>(flush_denormal(x[10], flush)) * v2 + static_cast<double>(flush_denormal(x[14], flush)) * v3,
        static_cast<double>(flush_denormal(x[3], flush)) * v0 + static_cast<double>(flush_denormal(x[7], flush)) * v1 +
            static_cast<double>(flush_denormal(x[11], flush)) * v2 + static_cast<double>(flush_denormal(x[15], flush)) * v3,
    };
    for (unsigned i = 0; i < 4; ++i)
        fr[base + i] = flush_denormal(static_cast<float>(result[i]), flush);
}

void fsrra(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    if (context.fpscr_pr) return; // Flycast does not implement PR=1 FSRRA.
    const bool flush = (context.fpscr & (1u << 18)) != 0;
    auto& fr = active_float_bank(context);
    const auto input = flush_denormal(fr[n & 15u], flush);
    fr[n & 15u] = flush_denormal(1.0f / std::sqrt(input), flush);
}

void fsca(multiarcaderecomp::naomi::Sh4Context& context, unsigned n) {
    if (context.fpscr_pr) return; // Flycast does not implement PR=1 FSCA.
    auto& fr = active_float_bank(context);
    const auto base = n & 14u;
    const auto index = context.fpul & 0xffffu;
    fr[base] = flycast_sine(index);
    fr[base + 1] = flycast_sine(index + 0x4000u);
}

} // namespace multiarcaderecomp::cpus::sh4::flycast::fpu
