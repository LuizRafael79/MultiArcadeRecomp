#pragma once

#include "mk/tunit.hpp"
#include "mk2/dcs2k.hpp"
#include "mk2/protection.hpp"

#include <cstdint>
#include <span>

namespace mamerecomp::mk2 {

// Mortal Kombat II machine composition. It reuses the shared T-Unit hardware
// while supplying the different DCS Audio 2K sound board used by MK2.
class MK2Machine final {
public:
    MK2Machine(std::span<const std::uint8_t> main_rom,
               std::span<const std::uint8_t> graphics_rom,
               std::span<const std::uint8_t> dcs_sound_region);

    mamerecomp::mk::TUnit& tunit() noexcept;
    const mamerecomp::mk::TUnit& tunit() const noexcept;
    Dcs2K& dcs() noexcept;
    const Dcs2K& dcs() const noexcept;
    Protection& protection() noexcept;
    const Protection& protection() const noexcept;

private:
    Dcs2K m_dcs;
    Protection m_protection;
    mamerecomp::mk::TUnit m_tunit;
};

} // namespace mamerecomp::mk2
