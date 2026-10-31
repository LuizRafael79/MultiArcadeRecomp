#include "naomi_sh4_helpers.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace mamerecomp::naomi {
namespace {
constexpr std::uint32_t kSrT = 1u << 0;
constexpr std::uint32_t kSrS = 1u << 1;
constexpr std::uint32_t kSrImask = 0x0fu << 4;
constexpr std::uint32_t kSrQ = 1u << 8;
constexpr std::uint32_t kSrM = 1u << 9;
constexpr std::uint32_t kSrFd = 1u << 15;
constexpr std::uint32_t kSrBl = 1u << 28;
constexpr std::uint32_t kSrRb = 1u << 29;
constexpr std::uint32_t kSrMd = 1u << 30;
constexpr std::uint32_t kFpscrPr = 1u << 19;
constexpr std::uint32_t kFpscrSz = 1u << 20;
constexpr std::uint32_t kFpscrFr = 1u << 21;

float& single_register(Sh4Context& ctx, unsigned index) {
    auto& bank = ctx.fpu_bank_x ? ctx.xf : ctx.frf;
    return bank[index & 15u];
}

std::uint32_t register_word(Sh4Context& ctx, unsigned index, bool extended = false) {
    auto& bank = (extended || ctx.fpu_bank_x) ? ctx.xf : ctx.frf;
    return std::bit_cast<std::uint32_t>(bank[index & 15u]);
}

void set_register_word(Sh4Context& ctx, unsigned index, std::uint32_t value,
                       bool extended = false) {
    auto& bank = (extended || ctx.fpu_bank_x) ? ctx.xf : ctx.frf;
    bank[index & 15u] = std::bit_cast<float>(value);
}

double double_register(Sh4Context& ctx, unsigned index) {
    index &= 14u;
    const auto high = static_cast<std::uint64_t>(register_word(ctx, index));
    const auto low = static_cast<std::uint64_t>(register_word(ctx, index + 1));
    return std::bit_cast<double>((high << 32) | low);
}

void set_double_register(Sh4Context& ctx, unsigned index, double value) {
    if (index & 1u) {
        ctx.execution_stopped = true;
        ctx.exception_code = 0x80000001u; // illegal odd DR register
        return;
    }
    const auto bits = std::bit_cast<std::uint64_t>(value);
    set_register_word(ctx, index, static_cast<std::uint32_t>(bits >> 32));
    set_register_word(ctx, index + 1, static_cast<std::uint32_t>(bits));
}

double operand(Sh4Context& ctx, unsigned index) {
    return ctx.fpscr_pr ? double_register(ctx, index) : single_register(ctx, index);
}

void set_operand(Sh4Context& ctx, unsigned index, double value) {
    if (ctx.fpscr_pr)
        set_double_register(ctx, index, value);
    else
        single_register(ctx, index) = static_cast<float>(value);
}

void fpu_fault(Sh4Context& ctx, std::uint32_t code) {
    ctx.execution_stopped = true;
    ctx.exception_code = 0x80000000u | code;
}

std::uint32_t transfer_size(const Sh4Context& ctx) {
    return (ctx.fpscr & kFpscrSz) ? 8u : 4u;
}

bool transfer_extended(const Sh4Context& ctx, unsigned index) {
    return (ctx.fpscr & kFpscrSz) && (index & 1u);
}

std::uint32_t transfer_index(unsigned index) {
    return index & 14u;
}

std::uint64_t read_transfer_pair(Sh4Context& ctx, unsigned index) {
    const bool extended = transfer_extended(ctx, index);
    index = transfer_index(index);
    const auto high = static_cast<std::uint64_t>(register_word(ctx, index, extended));
    const auto low = static_cast<std::uint64_t>(register_word(ctx, index + 1, extended));
    return (high << 32) | low;
}

void write_transfer_pair(Sh4Context& ctx, unsigned index, std::uint64_t value) {
    const bool extended = transfer_extended(ctx, index);
    index = transfer_index(index);
    set_register_word(ctx, index, static_cast<std::uint32_t>(value >> 32), extended);
    set_register_word(ctx, index + 1, static_cast<std::uint32_t>(value), extended);
}

void ftrc_value(Sh4Context& ctx, double value) {
    std::int32_t converted;
    if (std::isnan(value) || value <= -2147483649.0)
        converted = std::numeric_limits<std::int32_t>::min();
    else if (value >= 2147483648.0)
        converted = std::numeric_limits<std::int32_t>::max();
    else
        converted = static_cast<std::int32_t>(value); // SH FTRC truncates toward zero
    ctx.fpul = std::bit_cast<std::uint32_t>(converted);
}
} // namespace

void load_sr(Sh4Context& ctx, std::uint32_t value) {
    constexpr std::uint32_t writable = kSrT | kSrS | kSrImask | kSrQ | kSrM |
                                       kSrFd | kSrBl | kSrRb | kSrMd;
    value &= writable;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_IRQ")) {
        static unsigned trace_events = 0;
        if (trace_events++ < 64)
            std::fprintf(stderr, "SH-4 SR load pc=%08x old=%08x new=%08x\n",
                         ctx.pc, ctx.sr, value);
    }
    if ((ctx.sr ^ value) & kSrRb) {
        for (unsigned i = 0; i < ctx.r_bank.size(); ++i)
            std::swap(ctx.r[i], ctx.r_bank[i]);
    }
    ctx.sr = value;
    ctx.t = (value & kSrT) != 0;
    ctx.q = (value & kSrQ) != 0;
    ctx.m_bit = (value & kSrM) != 0;
}

std::uint32_t host_store_sr(const Sh4Context& ctx) {
    constexpr std::uint32_t writable = kSrT | kSrS | kSrImask | kSrQ | kSrM |
                                       kSrFd | kSrBl | kSrRb | kSrMd;
    auto value = ctx.sr & writable;
    value = (value & ~(kSrT | kSrQ | kSrM)) |
            (ctx.t ? kSrT : 0u) | (ctx.q ? kSrQ : 0u) |
            (ctx.m_bit ? kSrM : 0u);
    return value;
}

void host_rte(Sh4Context& ctx) {
    // The generated RTE path executes its delay slot before arriving here.
    // SSR/SPC are restored only after that slot, as required by SH-4.
    const auto return_pc = ctx.spc;
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_IRQ")) {
        static unsigned trace_events = 0;
        if (trace_events++ < 32)
            std::fprintf(stderr, "SH-4 RTE pc=%08x sr=%08x -> pc=%08x sr=%08x\n",
                         ctx.pc, ctx.sr, return_pc, ctx.ssr);
    }
    load_sr(ctx, ctx.ssr);
    ctx.pc = return_pc;
    // RTE replaces the guest control flow; it is not a normal translated
    // function call. Ask the dispatcher to unwind the current host call chain
    // and resume at the restored SPC from its top-level loop.
    ctx.interrupt_taken = true;
}

void host_div1(Sh4Context& ctx, unsigned n, unsigned m) {
    const bool old_q = ctx.q;
    ctx.q = (ctx.r[n] & 0x80000000u) != 0;
    ctx.r[n] = (ctx.r[n] << 1) | (ctx.t ? 1u : 0u);

    const std::uint32_t before = ctx.r[n];
    const bool subtract = old_q == ctx.m_bit;
    if (subtract) ctx.r[n] -= ctx.r[m];
    else ctx.r[n] += ctx.r[m];

    const bool carry_or_borrow = subtract ? ctx.r[n] > before : ctx.r[n] < before;
    ctx.q = old_q ? (subtract ? (ctx.q == carry_or_borrow)
                              : (ctx.q != carry_or_borrow))
                  : (subtract ? (ctx.q != carry_or_borrow)
                              : (ctx.q == carry_or_borrow));
    ctx.t = (ctx.q == ctx.m_bit);
    ctx.sr = (ctx.sr & ~(kSrT | kSrQ | kSrM)) |
             (ctx.t ? kSrT : 0u) | (ctx.q ? kSrQ : 0u) |
             (ctx.m_bit ? kSrM : 0u);
}

void host_load_fpscr(Sh4Context& ctx, std::uint32_t value) {
    ctx.fpscr = value & 0x003fffffu;
    ctx.fpscr_pr = (ctx.fpscr & kFpscrPr) != 0;
    ctx.fpu_bank_x = (ctx.fpscr & kFpscrFr) != 0;
}

void host_frchg(Sh4Context& ctx) {
    host_load_fpscr(ctx, ctx.fpscr ^ kFpscrFr);
}

void host_fschg(Sh4Context& ctx) {
    host_load_fpscr(ctx, ctx.fpscr ^ kFpscrSz);
}

void host_fldi(Sh4Context& ctx, unsigned n, float value) {
    if (ctx.fpscr_pr) {
        fpu_fault(ctx, 1);
        return;
    }
    single_register(ctx, n) = value;
}

void host_fsts_fpul(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    set_register_word(ctx, n, ctx.fpul);
}

void host_flds_fpul(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    ctx.fpul = register_word(ctx, n);
}

void host_fmov_reg(Sh4Context& ctx, unsigned n, unsigned m) {
    if (transfer_size(ctx) == 8) {
        const auto value = read_transfer_pair(ctx, m);
        write_transfer_pair(ctx, n, value);
    } else {
        const auto value = register_word(ctx, m);
        set_register_word(ctx, n, value);
    }
}

void host_fmov_load(Sh4Context& ctx, unsigned n, std::uint32_t address) {
    if (transfer_size(ctx) == 8) {
        const auto high = ctx.read32(address);
        const auto low = ctx.read32(address + 4);
        write_transfer_pair(ctx, n, (static_cast<std::uint64_t>(high) << 32) | low);
    } else {
        set_register_word(ctx, n, ctx.read32(address));
    }
}

void host_fmov_store(Sh4Context& ctx, std::uint32_t address, unsigned m) {
    if (transfer_size(ctx) == 8) {
        const auto value = read_transfer_pair(ctx, m);
        ctx.write32(address, static_cast<std::uint32_t>(value >> 32));
        ctx.write32(address + 4, static_cast<std::uint32_t>(value));
    } else {
        ctx.write32(address, register_word(ctx, m));
    }
}

void host_fmov_load_postinc(Sh4Context& ctx, unsigned n, unsigned m) {
    const auto address = ctx.r[m];
    host_fmov_load(ctx, n, address);
    ctx.r[m] += transfer_size(ctx);
}

void host_fmov_store_predec(Sh4Context& ctx, unsigned n, unsigned m) {
    ctx.r[n] -= transfer_size(ctx);
    host_fmov_store(ctx, ctx.r[n], m);
}

void host_fadd(Sh4Context& ctx, unsigned n, unsigned m) { set_operand(ctx, n, operand(ctx, n) + operand(ctx, m)); }
void host_fsub(Sh4Context& ctx, unsigned n, unsigned m) { set_operand(ctx, n, operand(ctx, n) - operand(ctx, m)); }
void host_fmul(Sh4Context& ctx, unsigned n, unsigned m) { set_operand(ctx, n, operand(ctx, n) * operand(ctx, m)); }
void host_fdiv(Sh4Context& ctx, unsigned n, unsigned m) { set_operand(ctx, n, operand(ctx, n) / operand(ctx, m)); }
void host_fcmp_eq(Sh4Context& ctx, unsigned n, unsigned m) { ctx.t = operand(ctx, n) == operand(ctx, m); }
void host_fcmp_gt(Sh4Context& ctx, unsigned n, unsigned m) { ctx.t = operand(ctx, n) > operand(ctx, m); }
void host_fneg(Sh4Context& ctx, unsigned n) { set_operand(ctx, n, -operand(ctx, n)); }
void host_fabs(Sh4Context& ctx, unsigned n) { set_operand(ctx, n, std::fabs(operand(ctx, n))); }
void host_fsqrt(Sh4Context& ctx, unsigned n) { set_operand(ctx, n, std::sqrt(operand(ctx, n))); }

void host_float_fpul(Sh4Context& ctx, unsigned n) {
    const auto value = std::bit_cast<std::int32_t>(ctx.fpul);
    if (ctx.fpscr_pr) set_double_register(ctx, n, static_cast<double>(value));
    else single_register(ctx, n) = static_cast<float>(value);
}

void host_ftrc_fpul(Sh4Context& ctx, unsigned n) { ftrc_value(ctx, operand(ctx, n)); }

void host_fcnvsd(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) {
        const float value = std::bit_cast<float>(ctx.fpul);
        set_double_register(ctx, n, static_cast<double>(value));
    } else {
        fpu_fault(ctx, 1);
    }
}

void host_fcnvds(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) {
        const auto value = static_cast<float>(double_register(ctx, n));
        ctx.fpul = std::bit_cast<std::uint32_t>(value);
    } else {
        fpu_fault(ctx, 1);
    }
}

void host_fmac(Sh4Context& ctx, unsigned n, unsigned m) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    single_register(ctx, n) += single_register(ctx, 0) * single_register(ctx, m);
}

void host_fipr(Sh4Context& ctx, unsigned m, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    const unsigned mb = m & 12u;
    const unsigned nb = n & 12u;
    float result = 0.0f;
    for (unsigned i = 0; i < 4; ++i)
        result += single_register(ctx, mb + i) * single_register(ctx, nb + i);
    single_register(ctx, nb + 3) = result;
}

void host_ftrv(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    const unsigned base = n & 12u;
    float result[4]{};
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned col = 0; col < 4; ++col)
            result[row] += ctx.xf[row + col * 4] * single_register(ctx, base + col);
    }
    for (unsigned i = 0; i < 4; ++i)
        single_register(ctx, base + i) = result[i];
}

void host_fsrra(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    single_register(ctx, n) = 1.0f / std::sqrt(single_register(ctx, n));
}

void host_fsca(Sh4Context& ctx, unsigned n) {
    if (ctx.fpscr_pr) { fpu_fault(ctx, 1); return; }
    const auto angle = static_cast<float>(ctx.fpul & 0xffffu) *
                       (6.2831853071795864769f / 65536.0f);
    single_register(ctx, n & 14u) = std::sin(angle);
    single_register(ctx, (n & 14u) + 1) = std::cos(angle);
}

void host_pref(Sh4Context& ctx, std::uint32_t address) {
    if ((address & 0xfc000000u) == 0xe0000000u) {
        if (ctx.store_queue_handler) ctx.store_queue_handler(ctx, address);
        else fpu_fault(ctx, 2); // store queue needs the Holly/PVR endpoint
    }
    // PREF outside the store-queue region is only a cache hint on this host.
}

void host_trapa(Sh4Context& ctx, std::uint32_t immediate) {
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_TRAPA")) {
        static std::uint64_t trace_count = 0;
        ++trace_count;
        std::fprintf(stderr,
                     "SH-4 TRAPA #%llu pc=%08x imm=%02x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x r13=%08x r14=%08x r15=%08x pr=%08x sr=%08x vbr=%08x ssr=%08x spc=%08x\n",
                     static_cast<unsigned long long>(trace_count), ctx.pc,
                     immediate, ctx.r[0], ctx.r[1], ctx.r[2], ctx.r[3],
                     ctx.r[4], ctx.r[5], ctx.r[6], ctx.r[7], ctx.r[8],
                     ctx.r[9], ctx.r[10], ctx.r[11], ctx.r[12], ctx.r[13],
                     ctx.r[14], ctx.r[15], ctx.pr, ctx.sr, ctx.vbr, ctx.ssr,
                     ctx.spc);
    }
    if (ctx.trap_handler) ctx.trap_handler(ctx, immediate);
    else fpu_fault(ctx, 3); // exception vector/service has not been connected
}

} // namespace mamerecomp::naomi
