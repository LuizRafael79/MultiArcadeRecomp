#include "system16b_sound.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
#include <SDL3/SDL.h>
#endif

namespace mamerecomp {
namespace {
constexpr std::uint32_t kYmClock = 4'000'000;
constexpr std::uint32_t kYmClocksPerSample = 64;
constexpr std::size_t kFlushSamples = 256;
std::uint64_t main_to_z80(std::uint64_t c) { return c / 2; }
std::uint64_t z80_to_ym(std::uint64_t c) { return (c / 5) * 4 + (c % 5) * 4 / 5; }
}

System16BSound::System16BSound(std::span<const std::uint8_t> region, bool output, std::uint8_t volume,
    System16BBoard board, bool stereo_spread)
    : m_rom(region.begin(), region.end()),
      m_z80_bus([this](std::uint16_t address) { return memory_read(address); },
          [this](std::uint16_t address, std::uint8_t value) { memory_write(address, value); },
          [this](std::uint16_t port) { return port_read(static_cast<std::uint8_t>(port)); },
          [this](std::uint16_t port, std::uint8_t value) { port_write(static_cast<std::uint8_t>(port), value); }),
      m_cpu(m_z80_bus), m_ym2151(*this), m_master_volume(std::min<std::uint8_t>(volume, 100)),
      m_stereo_spread(stereo_spread), m_rom_board(board) {
    if (m_rom.size() < 0x18000) throw std::invalid_argument("System 16B sound ROM region is too small for fixed and banked ROM");
    m_cpu.Reset();
    m_ym2151.reset();
    m_upd7759.set_request_callback([this] { m_cpu.RaiseNmi(); ++m_upd_nmi_edges; });
    m_z80_bus.SetInstructionBoundary([this](std::uint64_t cycles) {
        synchronize_ym2151(z80_to_ym(cycles));
    });
    if (!output) return;
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw std::runtime_error(std::string("SDL audio init failed: ") + SDL_GetError());
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16; spec.channels = 2; spec.freq = static_cast<int>(m_ym2151.sample_rate(kYmClock));
    m_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_audio_stream) { SDL_QuitSubSystem(SDL_INIT_AUDIO); throw std::runtime_error(std::string("SDL playback open failed: ") + SDL_GetError()); }
    if (!SDL_ResumeAudioStreamDevice(m_audio_stream)) {
        const std::string e = SDL_GetError(); SDL_DestroyAudioStream(m_audio_stream); m_audio_stream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO); throw std::runtime_error("SDL playback start failed: " + e);
    }
    m_audio_buffer.reserve(kFlushSamples * 2);
#else
    throw std::runtime_error("--audio requires an SDL3-enabled build");
#endif
}
System16BSound::~System16BSound() {
    try { flush_audio_samples(); } catch (...) { }
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (m_audio_stream) { SDL_DestroyAudioStream(m_audio_stream); SDL_QuitSubSystem(SDL_INIT_AUDIO); }
#endif
}
std::uint8_t System16BSound::memory_read(std::uint16_t a) {
    if (a < 0x8000) return m_rom[a];
    if (a < 0xe000) {
        const auto span = m_rom.size() - 0x10000;
        return m_rom[0x10000 + (std::size_t(m_bank) * 0x4000 + a - 0x8000) % span];
    }
    if (a == 0xe800) return m_sound_latch;
    if (a >= 0xf800) return m_ram[a - 0xf800];
    return 0xff;
}
void System16BSound::memory_write(std::uint16_t a, std::uint8_t v) { if (a >= 0xf800) m_ram[a - 0xf800] = v; }
std::uint8_t System16BSound::port_read(std::uint8_t p) {
    if ((p & 0xc0) == 0) { synchronize_ym2151(z80_to_ym(m_cpu.BusCallbackCycles())); return (p & 1) ? m_ym2151.read(1) : 0xff; }
    if ((p & 0xc0) == 0x80) return m_upd7759.idle() ? 0x80 : 0;
    if ((p & 0xc0) == 0xc0) return m_sound_latch;
    return 0xff;
}
void System16BSound::port_write(std::uint8_t p, std::uint8_t v) {
    const auto page = p & 0xc0;
    if (page == 0) {
        synchronize_ym2151(z80_to_ym(m_cpu.BusCallbackCycles()));
        if ((p & 1) == 0) { m_ym_address = v; m_ym2151.write(0, v); }
        else {
            auto chip_value = v;
            if (m_ym_address >= 0x20 && m_ym_address <= 0x27 && m_stereo_spread) {
                const auto channel = m_ym_address - 0x20;
                chip_value = static_cast<std::uint8_t>((v & 0x3fU) | ((channel & 1U) ? 0x80U : 0x40U));
            }
            m_ym2151.write(1, chip_value); ++m_ym2151_writes;
            if (m_ym_address == 0x08 && (v & 0x78)) ++m_ym2151_key_ons;
        }
    } else if (page == 0x40) {
        // ROM select/address wiring varies between 171-5358, 171-5521 and 171-5797.
        std::uint32_t offs = 0;
        if (m_rom_board == System16BBoard::Sega5358) {
            if (!(v & 0x04)) offs = 0x00000;
            if (!(v & 0x08)) offs = 0x10000;
            if (!(v & 0x10)) offs = 0x20000;
            if (!(v & 0x20)) offs = 0x30000;
            offs += (v & 3) * 0x4000U;
        } else if (m_rom_board == System16BBoard::Sega5521) {
            offs = ((v & 8) >> 3) * 0x20000U + (v & 7) * 0x4000U;
        } else {
            offs = ((v & 8) >> 3) * 0x40000U + ((v & 0x10) >> 4) * 0x20000U + (v & 7) * 0x4000U;
        }
        offs %= static_cast<std::uint32_t>(m_rom.size() - 0x10000);
        m_bank = static_cast<std::uint8_t>((offs / 0x4000) & 0x1f);
        const bool old_md = m_md;
        m_md = (v & 0x80) == 0;
        const bool released = (v & 0x40) != 0;
        if (m_upd_reset_released && !released) m_upd7759.reset();
        if (m_upd7759.idle() && released && old_md && !m_md) m_upd7759.start();
        m_upd_reset_released = released;
    } else if (page == 0x80) {
        // Supply the shared uPD7759 FIFO port; its DRQ requests raise the Z80 NMI.
        m_upd7759_fifo = v;
        m_upd7759.write_port(v);
        ++m_upd7759_writes;
    }
}
void System16BSound::advance_to_main_cycles(std::uint64_t c) {
    const auto target = main_to_z80(c);
    const auto native_before = m_cpu.nativeInstructions;
    const auto interpreted_before = m_cpu.interpreted;
    const auto nmi_before = m_cpu.nmiServiced;
    m_cpu.Run(target, z80::Z80::Mode::Native);
    m_z80_instructions += (m_cpu.nativeInstructions - native_before) + (m_cpu.interpreted - interpreted_before);
    m_z80_nmi_serviced += m_cpu.nmiServiced - nmi_before;
    synchronize_ym2151(z80_to_ym(target));
}
void System16BSound::write_sound_latch(std::uint8_t v) { m_sound_latch = v; m_cpu.RaiseInterrupt(); }
void System16BSound::synchronize_ym2151(std::uint64_t target) {
    while ((m_generated_ym_samples + 1) * kYmClocksPerSample <= target) {
        const auto clock = (++m_generated_ym_samples) * kYmClocksPerSample;
        m_ym_clock = clock;
        for (std::uint32_t i = 0; i < m_timer_active.size(); ++i)
            if (m_timer_active[i] && m_timer_deadlines[i] <= clock) { m_timer_active[i] = false; m_engine->engine_timer_expired(i); }
        // uPD7759 runs at 640 kHz and updates its ADPCM output every four clocks.
        // One YM output sample represents 10.24 uPD clocks; retain the fraction.
        m_upd_clock_fraction += 256;
        while (m_upd_clock_fraction >= 25) { m_upd_clock_fraction -= 25; m_upd7759.advance(1); }
        ymfm::ym2151::output_data o{}; m_ym2151.generate(&o, 1); o.clamp16();
        // Golden Axe's System16B mix routes YM2151 outputs at 0.43.
        const int upd_pcm = m_upd7759.pcm_sample() * 48 / 100;
        const auto l = static_cast<std::int16_t>(std::clamp(o.data[0] * 43 / 100 + upd_pcm, -32768, 32767));
        const auto r = static_cast<std::int16_t>(std::clamp(o.data[1] * 43 / 100 + upd_pcm, -32768, 32767));
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(std::max(std::abs(int(l)), std::abs(int(r)))));
        if (m_audio_stream) { m_audio_buffer.push_back(std::int16_t(int(l) * m_master_volume / 100)); m_audio_buffer.push_back(std::int16_t(int(r) * m_master_volume / 100)); if (m_audio_buffer.size() >= kFlushSamples * 2) flush_audio_samples(); }
        ++m_audio_samples;
    }
    m_ym_clock = target;
}
void System16BSound::flush_audio_samples() {
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    if (m_audio_stream && !m_audio_buffer.empty()) {
        if (!SDL_PutAudioStreamData(m_audio_stream, m_audio_buffer.data(), static_cast<int>(m_audio_buffer.size()*sizeof(std::int16_t))))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        m_audio_buffer.clear();
    }
#endif
}
void System16BSound::ymfm_set_timer(std::uint32_t t, std::int32_t d) { if(t>=2)return; if(d<0){m_timer_active[t]=false;return;}m_timer_active[t]=true;m_timer_deadlines[t]=m_ym_clock+std::uint32_t(d); }
void System16BSound::ymfm_set_busy_end(std::uint32_t c) { m_busy_until=m_ym_clock+c; }
bool System16BSound::ymfm_is_busy() { return m_ym_clock<m_busy_until; }
void System16BSound::ymfm_update_irq(bool v) { m_ym2151_irq=v; }
void System16BSound::ymfm_external_write(ymfm::access_class, std::uint32_t, std::uint8_t) { }
} // namespace mamerecomp
