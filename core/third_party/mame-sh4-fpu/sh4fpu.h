// license:MIT
// copyright-holders:Stefanos Kornilios Mitsis Poiitidis, R. Belmont
/*****************************************************************************
 *
 *   sh4fpu.h
 *
 *   SH-4 vector and approximation FPU instructions: FIPR, FTRV, FSCA, FSRRA
 *
 *****************************************************************************/

#ifndef MAME_CPU_SH_SH4FPU_H
#define MAME_CPU_SH_SH4FPU_H

#pragma once

#include <cstdint>

namespace sh4fpu {

// exceptions an instruction raises, in the bit order of the FPSCR cause field
enum : std::uint32_t
{
	CAUSE_I = 0x01,     // inexact
	CAUSE_U = 0x02,     // underflow
	CAUSE_O = 0x04,     // overflow
	CAUSE_Z = 0x08,     // division by zero
	CAUSE_V = 0x10,     // invalid operation
	CAUSE_E = 0x20      // FPU error
};

// All values are IEEE-754 single precision bit patterns.
// rz is true when FPSCR.RM selects round to zero, dn is FPSCR.DN.

// inner product of FVm and FVn, which goes to the last element of FVn
std::uint32_t fipr(const std::uint32_t *fvm, const std::uint32_t *fvn,
                   bool rz, bool dn, std::uint32_t &cause);

// fv = xmtrx * fv, xmtrx being XF0-XF15 (column major); returns the exceptions raised
std::uint32_t ftrv(const std::uint32_t *xmtrx, std::uint32_t *fv,
                   bool rz, bool dn);

// reciprocal of the square root; there is no result when an FPU error is raised
std::uint32_t fsrra(std::uint32_t x, bool rz, bool dn, std::uint32_t &cause);

// sine of angle * 2 * pi / 65536 in units of 2^-26, the cosine is the sine of angle + 0x4000
std::int32_t fsca(std::uint32_t angle);

// what FSCA makes of that value
std::uint32_t fsca_round(std::int32_t value, bool rz);

} // namespace sh4fpu

#endif // MAME_CPU_SH_SH4FPU_H
