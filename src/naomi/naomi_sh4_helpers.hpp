#pragma once

#include "naomi_sh4_context.hpp"

#include <cstdint>

namespace mamerecomp::naomi {

void load_sr(Sh4Context& ctx, std::uint32_t value);
std::uint32_t host_store_sr(const Sh4Context& ctx);
void host_rte(Sh4Context& ctx);
void host_div1(Sh4Context& ctx, unsigned n, unsigned m);
void host_load_fpscr(Sh4Context& ctx, std::uint32_t value);
void host_frchg(Sh4Context& ctx);
void host_fschg(Sh4Context& ctx);
void host_fldi(Sh4Context& ctx, unsigned n, float value);
void host_fsts_fpul(Sh4Context& ctx, unsigned n);
void host_flds_fpul(Sh4Context& ctx, unsigned n);
void host_fmov_reg(Sh4Context& ctx, unsigned n, unsigned m);
void host_fmov_load(Sh4Context& ctx, unsigned n, std::uint32_t address);
void host_fmov_store(Sh4Context& ctx, std::uint32_t address, unsigned m);
void host_fmov_load_postinc(Sh4Context& ctx, unsigned n, unsigned m);
void host_fmov_store_predec(Sh4Context& ctx, unsigned n, unsigned m);
void host_fadd(Sh4Context& ctx, unsigned n, unsigned m);
void host_fsub(Sh4Context& ctx, unsigned n, unsigned m);
void host_fmul(Sh4Context& ctx, unsigned n, unsigned m);
void host_fdiv(Sh4Context& ctx, unsigned n, unsigned m);
void host_fcmp_eq(Sh4Context& ctx, unsigned n, unsigned m);
void host_fcmp_gt(Sh4Context& ctx, unsigned n, unsigned m);
void host_fneg(Sh4Context& ctx, unsigned n);
void host_fabs(Sh4Context& ctx, unsigned n);
void host_fsqrt(Sh4Context& ctx, unsigned n);
void host_float_fpul(Sh4Context& ctx, unsigned n);
void host_ftrc_fpul(Sh4Context& ctx, unsigned n);
void host_fcnvsd(Sh4Context& ctx, unsigned n);
void host_fcnvds(Sh4Context& ctx, unsigned n);
void host_fmac(Sh4Context& ctx, unsigned n, unsigned m);
void host_fipr(Sh4Context& ctx, unsigned m, unsigned n);
void host_ftrv(Sh4Context& ctx, unsigned n);
void host_fsrra(Sh4Context& ctx, unsigned n);
void host_fsca(Sh4Context& ctx, unsigned n);
void host_pref(Sh4Context& ctx, std::uint32_t address);
void host_trapa(Sh4Context& ctx, std::uint32_t immediate);

} // namespace mamerecomp::naomi
