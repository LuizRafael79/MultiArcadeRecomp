#pragma once

#include <cstdint>

namespace multiarcaderecomp::zn {

constexpr std::uint64_t qsound_target_frames(std::uint64_t z80_cycles)
{
    constexpr std::uint64_t z80_clock_hz = 8'000'000;
    constexpr std::uint64_t dsp_clock_hz = 60'000'000;
    constexpr std::uint64_t dsp_clocks_per_frame = 2 * 1248;
    return static_cast<std::uint64_t>(
        (__uint128_t(z80_cycles) * dsp_clock_hz) /
        (z80_clock_hz * dsp_clocks_per_frame));
}

} // namespace multiarcaderecomp::zn
