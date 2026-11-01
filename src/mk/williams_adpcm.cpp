// Williams ADPCM board behavior adapted from MAME's
// src/mame/shared/williamssound.cpp. The upstream implementation is
// BSD-3-Clause; this file reworks its address map and banking for MameRecomp.

#include "mk/williams_adpcm.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>

namespace mamerecomp::mk {
namespace {
constexpr std::array<std::uint32_t, 8> kOkiBankOffsets{
    0x40000, 0x40000, 0x20000, 0x00000,
    0xe0000, 0xc0000, 0xa0000, 0x80000,
};
constexpr std::uint32_t kCpuBankBase = 0x10000;
constexpr std::uint32_t kCpuBankSize = 0x8000;
constexpr std::uint32_t kCpuFixedBase = 0x10000 + 0x4000 + 7 * 0x8000;
constexpr std::uint64_t kSoundCpuClock = 2'000'000;
constexpr std::uint64_t kYmClock = 3'579'545;
constexpr std::uint32_t kYmClocksPerSample = 64;
constexpr std::uint64_t kOkiClock = 1'000'000;
constexpr std::array<int, 49> kOkiStepTable{
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
    80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
    307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552,
};
constexpr std::array<int, 8> kOkiIndexShift{-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 16> kOkiVolume{32, 22, 16, 11, 8, 6, 4, 3,
                                          2, 0, 0, 0, 0, 0, 0, 0};
}

WilliamsAdpcmBoard::WilliamsAdpcmBoard(
    std::span<const std::uint8_t> sound_cpu_rom,
    std::span<const std::uint8_t> oki_rom)
    : WilliamsAdpcmBoard(sound_cpu_rom, oki_rom, ChipBus{}) {}

WilliamsAdpcmBoard::WilliamsAdpcmBoard(
    std::span<const std::uint8_t> sound_cpu_rom,
    std::span<const std::uint8_t> oki_rom, ChipBus chips)
    : m_cpu_rom(sound_cpu_rom.begin(), sound_cpu_rom.end()),
      m_oki_rom(oki_rom.begin(), oki_rom.end()), m_ym(*this), m_chips(chips) {
    m_cpu_rom.resize(0x50000, 0xff);
    m_oki_rom.resize(0x100000, 0xff);
    m_ym.reset();
}

std::uint8_t WilliamsAdpcmBoard::sound_cpu_read(
    std::uint16_t address) noexcept {
    if (address < 0x2000) return m_ram[address];
    if ((address & 0xfc00) == 0x2400)
        return m_chips.ym_read ? m_chips.ym_read(m_chips.opaque, address & 1)
                               : m_ym.read(address & 1);
    if ((address & 0xfc00) == 0x2c00) {
        if (m_chips.oki_read) return m_chips.oki_read(m_chips.opaque);
        std::uint8_t status = 0xf0;
        for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice)
            if (m_oki_voices[voice].playing)
                status |= static_cast<std::uint8_t>(1u << voice);
        return status;
    }
    if ((address & 0xfc00) == 0x3000) {
        m_irq_release_cycles = 20; // MAME holds IRQ for 10 us after this read.
        return m_command;
    }
    if (address >= 0x4000 && address < 0xc000) {
        const std::uint32_t offset = kCpuBankBase +
            static_cast<std::uint32_t>(m_cpu_bank) * kCpuBankSize +
            (address - 0x4000);
        return offset < m_cpu_rom.size() ? m_cpu_rom[offset] : 0xff;
    }
    if (address >= 0xc000) {
        if (address >= 0xfb9c && address <= 0xfbc6)
            return m_hidden_ram[address - 0xfb9c];
        const std::uint32_t offset = kCpuFixedBase + (address - 0xc000);
        return offset < m_cpu_rom.size() ? m_cpu_rom[offset] : 0xff;
    }
    return 0xff;
}

void WilliamsAdpcmBoard::sound_cpu_write(std::uint16_t address,
                                          std::uint8_t value) noexcept {
    if (address < 0x2000) {
        m_ram[address] = value;
    } else if ((address & 0xfc00) == 0x2000) {
        m_cpu_bank = value & 7;
    } else if ((address & 0xfc00) == 0x2400) {
        ++m_audio_diagnostics.ym_writes;
        if (m_chips.ym_write) {
            m_chips.ym_write(m_chips.opaque, address & 1, value);
        } else {
            m_ym_address = value;
            m_ym.write(address & 1, value);
        }
    } else if ((address & 0xfc00) == 0x2800) {
        ++m_audio_diagnostics.dac_writes;
        m_dac_value = value;
        if (m_chips.dac_write) m_chips.dac_write(m_chips.opaque, value);
    } else if ((address & 0xfc00) == 0x2c00) {
        ++m_audio_diagnostics.oki_writes;
        if (m_chips.oki_write) m_chips.oki_write(m_chips.opaque, value);
        else oki_write(value);
    } else if ((address & 0xfc00) == 0x3400) {
        m_oki_bank = value & 7;
    } else if ((address & 0xfc00) == 0x3c00) {
        m_talkback = value;
    } else if (address >= 0xfb9c && address <= 0xfbc6) {
        m_hidden_ram[address - 0xfb9c] = value;
    }
}

void WilliamsAdpcmBoard::write_main_command(std::uint8_t value) noexcept {
    m_command = value;
    m_fake_status_reads = 128;
    if (!m_reset) m_irq = true;
}

void WilliamsAdpcmBoard::reset_write(bool asserted) noexcept {
    m_reset = asserted;
    if (asserted) {
        m_cpu_bank = 0;
        m_irq = false;
        m_irq_release_cycles = 0;
        m_ym.reset();
        m_oki_voices = {};
        m_pending_oki_sample = -1;
    }
}

void WilliamsAdpcmBoard::advance_sound_cycles(std::uint32_t cycles) noexcept {
    if (m_irq_release_cycles) {
        if (cycles >= m_irq_release_cycles) {
            m_irq = false;
            m_irq_release_cycles = 0;
        } else {
            m_irq_release_cycles -= cycles;
        }
    }
    m_sound_cycles += cycles;
    render_audio_to(m_sound_cycles);
}

bool WilliamsAdpcmBoard::irq_asserted() const noexcept { return m_irq; }
bool WilliamsAdpcmBoard::firq_asserted() const noexcept { return m_firq; }
bool WilliamsAdpcmBoard::reset_asserted() const noexcept { return m_reset; }
std::uint8_t WilliamsAdpcmBoard::talkback() const noexcept { return m_talkback; }
std::uint8_t WilliamsAdpcmBoard::cpu_bank() const noexcept { return m_cpu_bank; }
std::uint8_t WilliamsAdpcmBoard::oki_bank() const noexcept { return m_oki_bank; }

std::uint8_t WilliamsAdpcmBoard::oki_rom_read(std::uint32_t address) const noexcept {
    std::uint32_t offset;
    if (address < 0x20000) {
        offset = kOkiBankOffsets[m_oki_bank] + address;
    } else if (address < 0x40000) {
        offset = 0x60000 + (address - 0x20000);
    } else {
        return 0xff;
    }
    return offset < m_oki_rom.size() ? m_oki_rom[offset] : 0xff;
}

std::span<const std::uint8_t> WilliamsAdpcmBoard::ram() const noexcept {
    return m_ram;
}

std::span<const std::int16_t> WilliamsAdpcmBoard::audio_samples() const noexcept {
    return m_audio;
}

std::vector<std::int16_t> WilliamsAdpcmBoard::take_audio_samples() noexcept {
    std::vector<std::int16_t> samples;
    samples.swap(m_audio);
    return samples;
}

WilliamsAdpcmBoard::AudioDiagnostics
WilliamsAdpcmBoard::audio_diagnostics() const noexcept {
    return m_audio_diagnostics;
}

void WilliamsAdpcmBoard::ymfm_set_timer(std::uint32_t timer,
                                        std::int32_t duration) {
    if (timer >= m_timer_active.size()) return;
    if (duration < 0) {
        m_timer_active[timer] = false;
        return;
    }
    m_timer_active[timer] = true;
    m_timer_deadlines[timer] = m_ym_clock + static_cast<std::uint32_t>(duration);
}

void WilliamsAdpcmBoard::ymfm_set_busy_end(std::uint32_t clocks) {
    m_busy_until = m_ym_clock + clocks;
}

bool WilliamsAdpcmBoard::ymfm_is_busy() {
    return m_ym_clock < m_busy_until;
}

void WilliamsAdpcmBoard::ymfm_update_irq(bool asserted) {
    m_firq = asserted;
}

void WilliamsAdpcmBoard::ymfm_external_write(ymfm::access_class,
                                              std::uint32_t, std::uint8_t) { }

void WilliamsAdpcmBoard::oki_write(std::uint8_t value) noexcept {
    if (m_pending_oki_sample >= 0) {
        const unsigned voices = value >> 4;
        const std::uint32_t table = static_cast<std::uint32_t>(m_pending_oki_sample) * 8;
        for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice) {
            if (!(voices & (1u << voice)) || m_oki_voices[voice].playing) continue;
            const std::uint32_t start = oki_sample_address(table) & 0x3ffff;
            const std::uint32_t end = oki_sample_address(table + 3) & 0x3ffff;
            if (start >= end || end >= 0x40000) continue;
            m_oki_voices[voice] = {true, start, 0, (end - start + 1) * 2,
                                   0, 0, static_cast<std::uint8_t>(value & 15)};
        }
        m_pending_oki_sample = -1;
    } else if (value & 0x80) {
        m_pending_oki_sample = value & 0x7f;
    } else {
        const unsigned voices = (value >> 3) & 15;
        for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice)
            if (voices & (1u << voice)) m_oki_voices[voice].playing = false;
    }
}

std::uint32_t WilliamsAdpcmBoard::oki_sample_address(
    std::uint32_t offset) const noexcept {
    return (std::uint32_t(oki_rom_read(offset)) << 16) |
           (std::uint32_t(oki_rom_read(offset + 1)) << 8) |
           std::uint32_t(oki_rom_read(offset + 2));
}

void WilliamsAdpcmBoard::clock_oki_nibble() noexcept {
    for (auto& voice : m_oki_voices) {
        if (!voice.playing) continue;
        if (voice.nibble >= voice.nibble_count) {
            voice.playing = false;
            continue;
        }
        const std::uint8_t byte = oki_rom_read(voice.start + voice.nibble / 2);
        const unsigned nibble = (voice.nibble & 1) ? byte & 15 : byte >> 4;
        const int step = kOkiStepTable[voice.step];
        int delta = ((2 * static_cast<int>(nibble & 7) + 1) * step) >> 3;
        if (nibble & 8) delta = -delta;
        voice.signal = std::clamp(voice.signal + delta, -2048, 2047);
        voice.step = std::clamp(voice.step + kOkiIndexShift[nibble & 7], 0, 48);
        if (++voice.nibble >= voice.nibble_count) voice.playing = false;
    }
}

void WilliamsAdpcmBoard::advance_oki_clock() noexcept {
    m_oki_clock_fraction += kYmClocksPerSample * kOkiClock;
    while (m_oki_clock_fraction >= kYmClock) {
        m_oki_clock_fraction -= kYmClock;
        if (++m_oki_divider_count >= 132) {
            m_oki_divider_count = 0;
            clock_oki_nibble();
        }
    }
}

int WilliamsAdpcmBoard::oki_output() const noexcept {
    int mix = 0;
    for (const auto& voice : m_oki_voices)
        if (voice.playing) mix += voice.signal * kOkiVolume[voice.volume] / 32;
    return std::clamp(mix, -2048, 2047);
}

void WilliamsAdpcmBoard::render_audio_to(std::uint64_t sound_cycle) noexcept {
    const std::uint64_t target_ym_clock = sound_cycle * kYmClock / kSoundCpuClock;
    while ((m_generated_samples + 1) * kYmClocksPerSample <= target_ym_clock) {
        const std::uint64_t clock = (++m_generated_samples) * kYmClocksPerSample;
        m_ym_clock = clock;
        for (unsigned timer = 0; timer < m_timer_active.size(); ++timer) {
            if (m_timer_active[timer] && m_timer_deadlines[timer] <= clock) {
                m_timer_active[timer] = false;
                m_engine->engine_timer_expired(timer);
            }
        }
        ymfm::ym2151::output_data output{};
        m_ym.generate(&output, 1);
        output.clamp16();
        advance_oki_clock();
        const int ym_left = output.data[0] / 10;
        const int ym_right = output.data[1] / 10;
        const int dac = (static_cast<int>(m_dac_value) - 128) * 16;
        const int oki = oki_output() * 3;
        const int left = std::clamp(ym_left + dac + oki, -32768, 32767);
        const int right = std::clamp(ym_right + dac + oki, -32768, 32767);
        const auto magnitude = [](int value) {
            return static_cast<std::uint16_t>(std::abs(value));
        };
        ++m_audio_diagnostics.generated_sample_frames;
        if (left != 0 || right != 0)
            ++m_audio_diagnostics.nonzero_sample_frames;
        m_audio_diagnostics.peak = std::max<std::uint16_t>(
            m_audio_diagnostics.peak, std::max(magnitude(left), magnitude(right)));
        m_audio_diagnostics.ym_peak = std::max<std::uint16_t>(
            m_audio_diagnostics.ym_peak,
            std::max(magnitude(ym_left), magnitude(ym_right)));
        m_audio_diagnostics.oki_peak = std::max<std::uint16_t>(
            m_audio_diagnostics.oki_peak, magnitude(oki));
        m_audio_diagnostics.dac_peak = std::max<std::uint16_t>(
            m_audio_diagnostics.dac_peak, magnitude(dac));
        m_audio.push_back(static_cast<std::int16_t>(left));
        m_audio.push_back(static_cast<std::int16_t>(right));
    }
    m_ym_clock = target_ym_clock;
}

} // namespace mamerecomp::mk
