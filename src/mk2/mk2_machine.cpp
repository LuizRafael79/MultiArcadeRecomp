#include "mk2/mk2_machine.hpp"

namespace mamerecomp::mk2 {

MK2Machine::MK2Machine(std::span<const std::uint8_t> main_rom,
                       std::span<const std::uint8_t> graphics_rom,
                       std::span<const std::uint8_t> dcs_sound_region)
    : m_dcs(dcs_sound_region), m_tunit(main_rom, graphics_rom) {
    // MAME's MK2 DSW defaults: CMOS coinage, POST powerup test disabled,
    // one board, coin counter 2, and the default attract/gameplay switches.
    m_tunit.set_input(3, 0xfc7d);
    m_tunit.attach_dcs_board(&m_dcs);
    m_tunit.attach_protection(&m_protection);
}

mamerecomp::mk::TUnit& MK2Machine::tunit() noexcept { return m_tunit; }
const mamerecomp::mk::TUnit& MK2Machine::tunit() const noexcept { return m_tunit; }
Dcs2K& MK2Machine::dcs() noexcept { return m_dcs; }
const Dcs2K& MK2Machine::dcs() const noexcept { return m_dcs; }
Protection& MK2Machine::protection() noexcept { return m_protection; }
const Protection& MK2Machine::protection() const noexcept { return m_protection; }

} // namespace mamerecomp::mk2
