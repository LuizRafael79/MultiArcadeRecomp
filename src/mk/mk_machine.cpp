#include "mk/mk_machine.hpp"
#include "mk/williams_adpcm.hpp"

#include <memory>

namespace multiarcaderecomp::mk {

MKMachine::MKMachine(std::span<const std::uint8_t> main_rom,
                     std::span<const std::uint8_t> graphics_rom,
                     std::span<const std::uint8_t> sound_cpu_rom,
                     std::span<const std::uint8_t> oki_rom)
    : m_sound_board(std::make_unique<WilliamsAdpcmBoard>(sound_cpu_rom,
                                                         oki_rom)),
      m_tunit(main_rom, graphics_rom) {
    m_tunit.attach_sound_board(m_sound_board.get());
}

MKMachine::~MKMachine() = default;

TUnit& MKMachine::tunit() noexcept { return m_tunit; }
const TUnit& MKMachine::tunit() const noexcept { return m_tunit; }
WilliamsAdpcmBoard& MKMachine::sound_board() noexcept { return *m_sound_board; }
const WilliamsAdpcmBoard& MKMachine::sound_board() const noexcept {
    return *m_sound_board;
}

std::uint8_t MKMachine::sound_cpu_read(std::uint16_t address) noexcept {
    return m_sound_board->sound_cpu_read(address);
}
void MKMachine::sound_cpu_write(std::uint16_t address,
                                std::uint8_t value) noexcept {
    m_sound_board->sound_cpu_write(address, value);
}
std::uint8_t MKMachine::sound_cpu_bank() const noexcept {
    return m_sound_board->cpu_bank();
}
bool MKMachine::sound_cpu_reset_asserted() const noexcept {
    return m_sound_board->reset_asserted();
}
bool MKMachine::sound_cpu_irq_asserted() const noexcept {
    return m_sound_board->irq_asserted();
}
bool MKMachine::sound_cpu_firq_asserted() const noexcept {
    return m_sound_board->firq_asserted();
}
void MKMachine::advance_sound_cycles(std::uint32_t cycles) noexcept {
    m_sound_board->advance_sound_cycles(cycles);
}
std::vector<std::int16_t> MKMachine::take_audio_samples() noexcept {
    return m_sound_board->take_audio_samples();
}
AudioDiagnostics MKMachine::audio_diagnostics() const noexcept {
    const auto stats = m_sound_board->audio_diagnostics();
    return {stats.ym_writes, stats.oki_writes, stats.dac_writes,
            stats.generated_sample_frames, stats.nonzero_sample_frames,
            stats.peak, stats.ym_peak, stats.oki_peak, stats.dac_peak};
}

} // namespace multiarcaderecomp::mk
