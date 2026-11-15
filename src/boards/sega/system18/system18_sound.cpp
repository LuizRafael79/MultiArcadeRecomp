#include "system18_sound.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

namespace multiarcaderecomp {
namespace {
constexpr std::uint32_t kMainClock = 10'000'000;
constexpr std::uint32_t kZ80Clock = 8'000'000;
constexpr std::uint32_t kYmClock = 8'000'000;
// MAME configures the 315-5313 at a guessed 15 MHz; its PSG input is VDP/15.
constexpr std::uint32_t kVdpClock = 15'000'000;
constexpr std::uint32_t kPsgClockDivider = 15;
constexpr std::uint32_t kYmClocksPerSample = 144;
constexpr std::uint32_t kPsgClocksPerSample =
    (kVdpClock / kPsgClockDivider) * kYmClocksPerSample / kYmClock;
constexpr std::size_t kFlushSamples = 256;
}

System18Sound::System18Sound(std::span<const std::uint8_t> sound_region,
    std::uint8_t master_volume, bool stereo_spread, SegaAudioSink audio_sink)
    : m_rom(sound_region.begin(), sound_region.end()),
      m_z80_bus([this](std::uint16_t address) { return memory_read(address); },
          [this](std::uint16_t address, std::uint8_t value) { memory_write(address, value); },
          [this](std::uint16_t port) { return port_read(static_cast<std::uint8_t>(port)); },
          [this](std::uint16_t port, std::uint8_t value) { port_write(static_cast<std::uint8_t>(port), value); }),
      m_cpu(m_z80_bus),
      m_ym_interface0(*this, 0), m_ym_interface1(*this, 1),
      m_ym0(m_ym_interface0), m_ym1(m_ym_interface1),
      m_master_volume(std::min<std::uint8_t>(master_volume, 100)), m_stereo_spread(stereo_spread),
      m_audio_sink(std::move(audio_sink)) {
    if (m_rom.size() < 0x20000) throw std::invalid_argument("System 18 sound ROM region is too small");
    m_cpu.Reset();
    m_psg.reset();
    m_ym0.reset();
    m_ym1.reset();
    m_z80_bus.SetInstructionBoundary([this](std::uint64_t cycles) {
        synchronize_audio(cycles);
    });
    if (m_audio_sink) m_audio_buffer.reserve(kFlushSamples * 2);
}

System18Sound::~System18Sound() {
    try { flush_audio_samples(); } catch (...) { }
}

std::uint8_t System18Sound::memory_read(std::uint16_t address) {
    if (address < 0xa000) return m_rom[address];
    if (address < 0xc000) {
        const auto offset = std::size_t(m_sound_bank) * 0x2000U + (address - 0xa000U);
        return offset < m_rom.size() ? m_rom[offset] : 0xff;
    }
    if (address < 0xd000) return 0xff; // RF5C68 registers are write-only on this board.
    if (address < 0xe000) return m_rf5c68.read_sample_ram(address);
    if (address >= 0xe000) return m_ram[address - 0xe000U];
    return 0xff;
}

void System18Sound::memory_write(std::uint16_t address, std::uint8_t value) {
    if (address < 0xd000) m_rf5c68.write_register(static_cast<std::uint8_t>(address & 0x0fU), value);
    else if (address < 0xe000) {
        m_rf5c68.write_sample_ram(address, value);
    } else {
        m_ram[address - 0xe000U] = value;
    }
}

std::uint8_t System18Sound::port_read(std::uint8_t port) {
    const auto decoded = static_cast<std::uint8_t>(port & 0xf3U);
    if ((decoded & 0xfcU) == 0x80U) {
        synchronize_audio(m_cpu.BusCallbackCycles());
        return m_ym0.read(decoded & 3U);
    }
    if ((decoded & 0xfcU) == 0x90U) {
        synchronize_audio(m_cpu.BusCallbackCycles());
        return m_ym1.read(decoded & 3U);
    }
    if ((port & 0xe0U) == 0xc0U) return m_sound_latch;
    return 0xff;
}

void System18Sound::port_write(std::uint8_t port, std::uint8_t value) {
    const auto decoded = static_cast<std::uint8_t>(port & 0xf3U);
    if ((decoded & 0xfcU) == 0x80U || (decoded & 0xfcU) == 0x90U) {
        synchronize_audio(m_cpu.BusCallbackCycles());
        const unsigned chip = (decoded & 0xfcU) == 0x90U ? 1U : 0U;
        const unsigned reg_port = decoded & 3U;
        std::uint8_t chip_value = value;
        const unsigned bank = reg_port >> 1U;
        if ((reg_port & 1U) == 0) {
            m_ym_address[chip][bank] = value;
        } else if (m_stereo_spread &&
            m_ym_address[chip][bank] >= 0xb4U && m_ym_address[chip][bank] <= 0xb6U) {
            // OPN stereo/pan registers are B4-B6, one per channel in each
            // three-channel bank. Alternate all 12 channels across both chips.
            const unsigned channel = chip * 6U + bank * 3U + (m_ym_address[chip][bank] - 0xb4U);
            const std::uint8_t pan = (channel & 1U) ? 0x80U : 0x40U;
            chip_value = static_cast<std::uint8_t>((value & 0x3fU) | pan);
        }
        (chip == 0 ? m_ym0 : m_ym1).write(reg_port, chip_value);
        ++m_ym_writes[chip];
    } else if ((port & 0xe0U) == 0xa0U) {
        m_sound_bank = value;
    } else if ((port & 0xe0U) == 0xc0U) {
        if (m_sound_reply_callback) m_sound_reply_callback(value);
    }
}

void System18Sound::advance_to_main_cycles(std::uint64_t main_cycles) {
    const std::uint64_t target = main_cycles * kZ80Clock / kMainClock;
    m_cpu.Run(target, z80::Z80::Mode::Native);
    synchronize_audio(target);
}

void System18Sound::write_sound_latch(std::uint8_t value) {
    m_sound_latch = value;
    m_cpu.RaiseNmi();
}

void System18Sound::synchronize_audio(std::uint64_t target) {
    while ((m_generated_samples + 1U) * kYmClocksPerSample <= target) {
        const auto clock = (++m_generated_samples) * kYmClocksPerSample;
        m_ym_clock = clock;
        for (unsigned chip = 0; chip < 2; ++chip) {
            auto& ym_interface = chip == 0 ? m_ym_interface0 : m_ym_interface1;
            m_ym_timers[chip].expire_due(clock, [&ym_interface](std::uint32_t timer) {
                ym_interface.timer_expired(timer);
            });
        }
        ymfm::ym3438::output_data fm0{}, fm1{};
        m_ym0.generate(&fm0, 1);
        m_ym1.generate(&fm1, 1);
        m_psg.advance_clocks(kPsgClocksPerSample);
        const int psg = m_psg.output() / 2; // 315-5313 route gain is 0.5 in the MAME machine config.
        fm0.clamp16();
        fm1.clamp16();
        for (unsigned channel = 0; channel < 2; ++channel) {
            m_ym_peak[0] = std::max<std::uint16_t>(m_ym_peak[0], static_cast<std::uint16_t>(
                std::abs(static_cast<int>(fm0.data[channel]))));
            m_ym_peak[1] = std::max<std::uint16_t>(m_ym_peak[1], static_cast<std::uint16_t>(
                std::abs(static_cast<int>(fm1.data[channel]))));
        }
        // RF5C68 output rate is clock/384; retain fractional clocks against
        // the YM output stream (180 RF clocks per YM sample at these clocks).
        m_rf5c68.advance_clocks((kMainClock * kYmClocksPerSample) / kYmClock);
        const auto pcm_output = m_rf5c68.output();
        // MAME routes both YM3438s to mono at 0.40 each. In the optional
        // spread mode retain YMFM's left/right outputs after forcing each OPN
        // channel's pan register to alternating sides.
        const int fm_left = m_stereo_spread
            ? (fm0.data[0] + fm1.data[0]) * 40 / 100
            : (fm0.data[0] + fm0.data[1] + fm1.data[0] + fm1.data[1]) * 40 / 100;
        const int fm_right = m_stereo_spread
            ? (fm0.data[1] + fm1.data[1]) * 40 / 100
            : fm_left;
        // MAME routes both RF5C68 outputs to the mono speaker at gain 1.0.
        // The optional spread mode retains the chip's separate DAC outputs.
        const int pcm_mono = static_cast<int>(pcm_output[0]) + static_cast<int>(pcm_output[1]);
        const int pcm_left = m_stereo_spread ? pcm_output[0] : pcm_mono;
        const int pcm_right = m_stereo_spread ? pcm_output[1] : pcm_mono;
        const int left = std::clamp(fm_left + pcm_left + psg, -32768, 32767);
        const int right = std::clamp(fm_right + pcm_right + psg, -32768, 32767);
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(
            std::max(std::abs(left), std::abs(right))));
        if (m_audio_sink) {
            m_audio_buffer.push_back(static_cast<std::int16_t>(left * m_master_volume / 100));
            m_audio_buffer.push_back(static_cast<std::int16_t>(right * m_master_volume / 100));
            if (m_audio_buffer.size() >= kFlushSamples * 2U) flush_audio_samples();
        }
        ++m_audio_samples;
    }
    m_ym_clock = target;
}

void System18Sound::flush_audio_samples() {
    if (m_audio_sink && !m_audio_buffer.empty()) {
        m_audio_sink(m_audio_buffer, kYmClock / kYmClocksPerSample);
        m_audio_buffer.clear();
    }
}

void System18Sound::YmInterface::ymfm_set_timer(std::uint32_t timer, std::int32_t duration) {
    m_owner.m_ym_timers[m_chip].set_timer(timer, duration, m_owner.m_ym_clock);
}
void System18Sound::YmInterface::ymfm_set_busy_end(std::uint32_t clocks) {
    m_owner.m_ym_timers[m_chip].set_busy_end(clocks, m_owner.m_ym_clock);
}
bool System18Sound::YmInterface::ymfm_is_busy() {
    return m_owner.m_ym_timers[m_chip].is_busy(m_owner.m_ym_clock);
}
void System18Sound::YmInterface::ymfm_update_irq(bool asserted) {
    if (m_chip != 0 || m_owner.m_irq[0] == asserted) return;
    m_owner.m_irq[0] = asserted;
    m_owner.m_cpu.SetInterruptLine(asserted);
}

} // namespace multiarcaderecomp
