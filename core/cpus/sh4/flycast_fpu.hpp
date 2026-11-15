#pragma once

#include <cstdint>

namespace multiarcaderecomp::naomi {
struct Sh4Context;
}

namespace multiarcaderecomp::cpus::sh4::flycast::fpu {

void fadd(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fsub(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fmul(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fdiv(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fcmp_eq(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fcmp_gt(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void fneg(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);
void fabs(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);
void fsqrt(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);
void fmac(multiarcaderecomp::naomi::Sh4Context& context, unsigned n, unsigned m);
void load_fpscr(multiarcaderecomp::naomi::Sh4Context& context, std::uint32_t value);
void toggle_fr(multiarcaderecomp::naomi::Sh4Context& context);
void toggle_sz(multiarcaderecomp::naomi::Sh4Context& context);
void fipr(multiarcaderecomp::naomi::Sh4Context& context, unsigned m, unsigned n);
void ftrv(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);
void fsrra(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);
void fsca(multiarcaderecomp::naomi::Sh4Context& context, unsigned n);

} // namespace multiarcaderecomp::cpus::sh4::flycast::fpu
