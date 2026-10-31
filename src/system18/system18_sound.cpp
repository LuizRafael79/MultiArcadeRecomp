#include "system18_sound.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
#include <SDL3/SDL.h>
#endif

namespace mamerecomp {
namespace {
constexpr std::uint32_t kMainClock = 10'000'000;
constexpr std::uint32_t kZ80Clock = 8'000'000;
constexpr std::uint32_t kYmClock = 8'000'000;
constexpr std::uint32_t kYmClocksPerSample = 144;
constexpr std::size_t kFlushSamples = 256;
// The RF5C68 samples (notably voices and impact sounds) were subjectively much
// louder than the YM3438 mix on this output path. Keep the chip model at its
// native level and attenuate only its route into the final mixer.
constexpr int kRf5c68RouteGainPercent = 50;
}

System18Sound::System18Sound(std::span<const std::uint8_t> sound_region, bool audio_output,
    std::uint8_t master_volume, bool stereo_spread)
    : m_rom(sound_region.begin(), sound_region.end()),
      m_ym_interface0(*this, 0), m_ym_interface1(*this, 1),
      m_ym0(m_ym_interface0), m_ym1(m_ym_interface1),
      m_master_volume(std::min<std::uint8_t>(master_volume, 100)), m_stereo_spread(stereo_spread) {
    if (m_rom.size() < 0x20000) throw std::invalid_argument("System 18 sound ROM region is too small");
    z80_init(&m_cpu);
    m_cpu.userdata = this;
    m_cpu.read_byte = &read_memory;
    m_cpu.write_byte = &write_memory;
    m_cpu.port_in = &read_port;
    m_cpu.port_out = &write_port;
    m_ym0.reset();
    m_ym1.reset();
    if (!audio_output) return;
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        throw std::runtime_error(std::string("SDL audio init failed: ") + SDL_GetError());
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(kYmClock / kYmClocksPerSample);
    m_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_audio_stream) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error(std::string("SDL playback open failed: ") + SDL_GetError());
    }
    if (!SDL_ResumeAudioStreamDevice(m_audio_stream)) {
        const std::string error = SDL_GetError();
        SDL_DestroyAudioStream(m_audio_stream);
        m_audio_stream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error("SDL playback start failed: " + error);
    }
    m_audio_buffer.reserve(kFlushSamples * 2);
#else
    throw std::runtime_error("--audio requires an SDL3-enabled build");
#endif
}

System18Sound::~System18Sound() {
    try { flush_audio_samples(); } catch (...) { }
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (m_audio_stream) {
        SDL_DestroyAudioStream(m_audio_stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif
}

std::uint8_t System18Sound::read_memory(void* user, std::uint16_t address) {
    return static_cast<System18Sound*>(user)->memory_read(address);
}
void System18Sound::write_memory(void* user, std::uint16_t address, std::uint8_t value) {
    static_cast<System18Sound*>(user)->memory_write(address, value);
}
std::uint8_t System18Sound::read_port(z80* cpu, std::uint8_t port) {
    return static_cast<System18Sound*>(cpu->userdata)->port_read(port);
}
void System18Sound::write_port(z80* cpu, std::uint8_t port, std::uint8_t value) {
    static_cast<System18Sound*>(cpu->userdata)->port_write(port, value);
}

std::uint8_t System18Sound::memory_read(std::uint16_t address) {
    if (address < 0xa000) return m_rom[address];
    if (address < 0xc000) {
        const auto offset = std::size_t(m_sound_bank) * 0x2000U + (address - 0xa000U);
        return offset < m_rom.size() ? m_rom[offset] : 0xff;
    }
    if (address < 0xd000) return 0xff; // RF5C68 registers are write-only on this board.
    if (address < 0xe000) return m_pcm_ram[m_pcm_write_bank | (address & 0x0fffU)];
    if (address >= 0xe000) return m_ram[address - 0xe000U];
    return 0xff;
}

void System18Sound::memory_write(std::uint16_t address, std::uint8_t value) {
    if (address < 0xd000) write_pcm_register(static_cast<std::uint8_t>(address & 0x0fU), value);
    else if (address < 0xe000) {
        m_pcm_ram[m_pcm_write_bank | (address & 0x0fffU)] = value;
        ++m_pcm_memory_writes;
    } else {
        m_ram[address - 0xe000U] = value;
    }
}

std::uint8_t System18Sound::port_read(std::uint8_t port) {
    const auto decoded = static_cast<std::uint8_t>(port & 0xf3U);
    if ((decoded & 0xfcU) == 0x80U) {
        synchronize_audio(m_cpu.cyc);
        return m_ym0.read(decoded & 3U);
    }
    if ((decoded & 0xfcU) == 0x90U) {
        synchronize_audio(m_cpu.cyc);
        return m_ym1.read(decoded & 3U);
    }
    if ((port & 0xe0U) == 0xc0U) return m_sound_latch;
    return 0xff;
}

void System18Sound::port_write(std::uint8_t port, std::uint8_t value) {
    const auto decoded = static_cast<std::uint8_t>(port & 0xf3U);
    if ((decoded & 0xfcU) == 0x80U || (decoded & 0xfcU) == 0x90U) {
        synchronize_audio(m_cpu.cyc);
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
    while (m_cpu.cyc < target) {
        z80_step(&m_cpu);
        synchronize_audio(m_cpu.cyc);
    }
    synchronize_audio(target);
}

void System18Sound::write_sound_latch(std::uint8_t value) {
    m_sound_latch = value;
    z80_gen_nmi(&m_cpu);
}

void System18Sound::write_pcm_register(std::uint8_t offset, std::uint8_t value) {
    offset &= 0x0fU;
    auto& channel = m_pcm_channels[m_pcm_channel_bank];
    switch (offset) {
    case 0x00: channel.env = value; break;
    case 0x01: channel.pan = value; break;
    case 0x02: channel.step = static_cast<std::uint16_t>((channel.step & 0xff00U) | value); break;
    case 0x03: channel.step = static_cast<std::uint16_t>((channel.step & 0x00ffU) | (std::uint16_t(value) << 8)); break;
    case 0x04: channel.loop = static_cast<std::uint16_t>((channel.loop & 0xff00U) | value); break;
    case 0x05: channel.loop = static_cast<std::uint16_t>((channel.loop & 0x00ffU) | (std::uint16_t(value) << 8)); break;
    case 0x06:
        channel.start = value;
        if (!channel.enabled) channel.address = std::uint32_t(value) << 19;
        break;
    case 0x07:
        m_pcm_enabled = (value & 0x80U) != 0;
        if (value & 0x40U) m_pcm_channel_bank = value & 7U;
        else m_pcm_write_bank = static_cast<std::uint16_t>((value & 0x0fU) << 12);
        break;
    case 0x08:
        for (unsigned index = 0; index < m_pcm_channels.size(); ++index) {
            auto& item = m_pcm_channels[index];
            item.enabled = (value & (1U << index)) == 0;
            if (!item.enabled) item.address = std::uint32_t(item.start) << 19;
        }
        m_pcm_enabled_channels = 0;
        for (unsigned index = 0; index < m_pcm_channels.size(); ++index)
            if (m_pcm_channels[index].enabled) m_pcm_enabled_channels |= static_cast<std::uint8_t>(1U << index);
        break;
    default: break;
    }
}

std::array<std::int16_t, 2> System18Sound::generate_pcm_sample() {
    std::int32_t left = 0;
    std::int32_t right = 0;
    if (m_pcm_enabled) {
        for (auto& channel : m_pcm_channels) {
            if (!channel.enabled) continue;
            std::uint8_t sample = m_pcm_ram[(channel.address >> 11) & 0xffffU];
            if (sample == 0xffU) {
                channel.address = std::uint32_t(channel.loop) << 11;
                sample = m_pcm_ram[(channel.address >> 11) & 0xffffU];
                if (sample == 0xffU) {
                    channel.enabled = false;
                    continue;
                }
            }
            channel.address += channel.step;
            const int magnitude = sample & 0x7fU;
            const int sign = (sample & 0x80U) ? 1 : -1;
            left += sign * magnitude * (channel.pan & 0x0fU) * channel.env / 32;
            right += sign * magnitude * ((channel.pan >> 4) & 0x0fU) * channel.env / 32;
        }
    }
    m_pcm_enabled_channels = 0;
    for (unsigned index = 0; index < m_pcm_channels.size(); ++index)
        if (m_pcm_channels[index].enabled) m_pcm_enabled_channels |= static_cast<std::uint8_t>(1U << index);
    // RF5C68 is a 10-bit DAC. MAME clears the low six bits before routing it.
    left = std::clamp(left, -32768, 32767) & ~0x3f;
    right = std::clamp(right, -32768, 32767) & ~0x3f;
    return {static_cast<std::int16_t>(left), static_cast<std::int16_t>(right)};
}

void System18Sound::synchronize_audio(std::uint64_t target) {
    while ((m_generated_samples + 1U) * kYmClocksPerSample <= target) {
        const auto clock = (++m_generated_samples) * kYmClocksPerSample;
        m_ym_clock = clock;
        for (unsigned chip = 0; chip < 2; ++chip) {
            for (unsigned timer = 0; timer < 2; ++timer) {
                if (m_timer_active[chip * 2U + timer] && m_timer_deadlines[chip * 2U + timer] <= clock) {
                    m_timer_active[chip * 2U + timer] = false;
                    (chip == 0 ? m_ym_interface0 : m_ym_interface1).timer_expired(timer);
                }
            }
        }
        ymfm::ym3438::output_data fm0{}, fm1{};
        m_ym0.generate(&fm0, 1);
        m_ym1.generate(&fm1, 1);
        fm0.clamp16();
        fm1.clamp16();
        // RF5C68 output rate is clock/384; retain fractional clocks against
        // the YM output stream (180 RF clocks per YM sample at these clocks).
        m_pcm_clock_fraction += (10'000'000ULL * kYmClocksPerSample) / kYmClock;
        while (m_pcm_clock_fraction >= 384U) {
            m_pcm_clock_fraction -= 384U;
            m_pcm_output = generate_pcm_sample();
            m_pcm_peak = std::max<std::uint16_t>(m_pcm_peak, static_cast<std::uint16_t>(
                std::max(std::abs(static_cast<int>(m_pcm_output[0])), std::abs(static_cast<int>(m_pcm_output[1])))));
        }
        // MAME routes both YM3438s to mono at 0.40 each. In the optional
        // spread mode retain YMFM's left/right outputs after forcing each OPN
        // channel's pan register to alternating sides.
        const int fm_left = m_stereo_spread
            ? (fm0.data[0] + fm1.data[0]) * 40 / 100
            : (fm0.data[0] + fm0.data[1] + fm1.data[0] + fm1.data[1]) * 40 / 100;
        const int fm_right = m_stereo_spread
            ? (fm0.data[1] + fm1.data[1]) * 40 / 100
            : fm_left;
        const int pcm_left = m_pcm_output[0] * kRf5c68RouteGainPercent / 100;
        const int pcm_right = m_pcm_output[1] * kRf5c68RouteGainPercent / 100;
        const int left = std::clamp(fm_left + pcm_left, -32768, 32767);
        const int right = std::clamp(fm_right + pcm_right, -32768, 32767);
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(
            std::max(std::abs(left), std::abs(right))));
        if (m_audio_stream) {
            m_audio_buffer.push_back(static_cast<std::int16_t>(left * m_master_volume / 100));
            m_audio_buffer.push_back(static_cast<std::int16_t>(right * m_master_volume / 100));
            if (m_audio_buffer.size() >= kFlushSamples * 2U) flush_audio_samples();
        }
        ++m_audio_samples;
    }
    m_ym_clock = target;
}

void System18Sound::flush_audio_samples() {
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (m_audio_stream && !m_audio_buffer.empty()) {
        if (!SDL_PutAudioStreamData(m_audio_stream, m_audio_buffer.data(),
                static_cast<int>(m_audio_buffer.size() * sizeof(std::int16_t))))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        m_audio_buffer.clear();
    }
#endif
}

void System18Sound::YmInterface::ymfm_set_timer(std::uint32_t timer, std::int32_t duration) {
    if (timer >= 2) return;
    const unsigned index = m_chip * 2U + timer;
    if (duration < 0) m_owner.m_timer_active[index] = false;
    else {
        m_owner.m_timer_active[index] = true;
        m_owner.m_timer_deadlines[index] = m_owner.m_ym_clock + static_cast<std::uint32_t>(duration);
    }
}
void System18Sound::YmInterface::ymfm_set_busy_end(std::uint32_t clocks) {
    m_owner.m_busy_until[m_chip] = m_owner.m_ym_clock + clocks;
}
bool System18Sound::YmInterface::ymfm_is_busy() {
    return m_owner.m_ym_clock < m_owner.m_busy_until[m_chip];
}
void System18Sound::YmInterface::ymfm_update_irq(bool asserted) {
    if (m_chip != 0 || m_owner.m_irq[0] == asserted) return;
    m_owner.m_irq[0] = asserted;
    if (asserted) z80_gen_int(&m_owner.m_cpu, 0xff);
    else m_owner.m_cpu.int_pending = false;
}

} // namespace mamerecomp
