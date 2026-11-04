#pragma once

#include <cstdint>

namespace mamerecomp::naomi {
struct Sh4Context;
}

namespace mamerecomp::cpus::sh4::flycast::fpu {

void fadd(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fsub(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fmul(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fdiv(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fcmp_eq(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fcmp_gt(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fneg(mamerecomp::naomi::Sh4Context& context, unsigned n);
void fabs(mamerecomp::naomi::Sh4Context& context, unsigned n);
void fsqrt(mamerecomp::naomi::Sh4Context& context, unsigned n);
void fmac(mamerecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void load_fpscr(mamerecomp::naomi::Sh4Context& context, std::uint32_t value);
void toggle_fr(mamerecomp::naomi::Sh4Context& context);
void toggle_sz(mamerecomp::naomi::Sh4Context& context);
void fipr(mamerecomp::naomi::Sh4Context& context, unsigned m, unsigned n);
void ftrv(mamerecomp::naomi::Sh4Context& context, unsigned n);
void fsrra(mamerecomp::naomi::Sh4Context& context, unsigned n);
void fsca(mamerecomp::naomi::Sh4Context& context, unsigned n);

} // namespace mamerecomp::cpus::sh4::flycast::fpu
