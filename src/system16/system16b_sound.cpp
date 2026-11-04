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
constexpr int kUpdStep[16][16] = {
 {0,0,1,2,3,5,7,10,0,0,-1,-2,-3,-5,-7,-10},{0,1,2,3,4,6,8,13,0,-1,-2,-3,-4,-6,-8,-13},
 {0,1,2,4,5,7,10,15,0,-1,-2,-4,-5,-7,-10,-15},{0,1,3,4,6,9,13,19,0,-1,-3,-4,-6,-9,-13,-19},
 {0,2,3,5,8,11,15,23,0,-2,-3,-5,-8,-11,-15,-23},{0,2,4,7,10,14,19,29,0,-2,-4,-7,-10,-14,-19,-29},
 {0,3,5,8,12,16,22,33,0,-3,-5,-8,-12,-16,-22,-33},{1,4,7,10,15,20,29,43,-1,-4,-7,-10,-15,-20,-29,-43},
 {1,4,8,13,18,25,35,53,-1,-4,-8,-13,-18,-25,-35,-53},{1,6,10,16,22,31,43,64,-1,-6,-10,-16,-22,-31,-43,-64},
 {2,7,12,19,27,37,51,76,-2,-7,-12,-19,-27,-37,-51,-76},{2,9,16,24,34,46,64,96,-2,-9,-16,-24,-34,-46,-64,-96},
 {3,11,19,29,41,57,79,117,-3,-11,-19,-29,-41,-57,-79,-117},{4,13,24,36,50,69,96,143,-4,-13,-24,-36,-50,-69,-96,-143},
 {4,16,29,44,62,85,118,175,-4,-16,-29,-44,-62,-85,-118,-175},{6,20,36,54,76,104,144,214,-6,-20,-36,-54,-76,-104,-144,-214}};
constexpr int kUpdStateDelta[16] = {-1,-1,0,0,1,2,2,3,-1,-1,0,0,1,2,2,3};
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
    if ((p & 0xc0) == 0x80) return m_upd_state == UpdState::Idle ? 0x80 : 0;
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
        if (m_upd_reset_released && !released) {
            m_upd_state = UpdState::Idle; m_upd_drq = false; m_upd_clocks_left = 0;
            m_upd_sample = 0; m_upd_adpcm_state = 0; m_upd_first_valid_header = false;
        }
        if (m_upd_state == UpdState::Idle && released && old_md && !m_md) {
            m_upd_state = UpdState::Start;
            m_upd_clocks_left = 1;
        }
        m_upd_reset_released = released;
    } else if (page == 0x80) {
        // The uPD7759 FIFO endpoint is captured here; its DRQ/NMI sequencer is
        // a separate device implementation, not an YM register alias.
        m_upd7759_fifo = v;
        ++m_upd7759_writes;
    }
}
void System16BSound::advance_to_main_cycles(std::uint64_t c) {
    const auto target = main_to_z80(c);
    while (m_cpu.cycles < target) {
        if (m_cpu.NmiPending()) ++m_z80_nmi_serviced;
        m_cpu.cycles += m_cpu.Step();
        ++m_z80_instructions;
        synchronize_ym2151(z80_to_ym(m_cpu.cycles));
    }
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
        while (m_upd_clock_fraction >= 25) { m_upd_clock_fraction -= 25; advance_upd7759_clock(); }
        ymfm::ym2151::output_data o{}; m_ym2151.generate(&o, 1); o.clamp16();
        // Golden Axe's System16B mix routes YM2151 outputs at 0.43.
        const int upd_pcm = int(m_upd_sample) * 128 * 48 / 100;
        const auto l = static_cast<std::int16_t>(std::clamp(o.data[0] * 43 / 100 + upd_pcm, -32768, 32767));
        const auto r = static_cast<std::int16_t>(std::clamp(o.data[1] * 43 / 100 + upd_pcm, -32768, 32767));
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(std::max(std::abs(int(l)), std::abs(int(r)))));
        if (m_audio_stream) { m_audio_buffer.push_back(std::int16_t(int(l) * m_master_volume / 100)); m_audio_buffer.push_back(std::int16_t(int(r) * m_master_volume / 100)); if (m_audio_buffer.size() >= kFlushSamples * 2) flush_audio_samples(); }
        ++m_audio_samples;
    }
    m_ym_clock = target;
}
void System16BSound::request_upd7759_byte() {
    m_upd_drq = true;
    m_cpu.RaiseNmi();
    ++m_upd_nmi_edges;
}
void System16BSound::update_upd7759_adpcm(std::uint8_t nibble) {
    static constexpr std::uint8_t index[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    m_upd_sample = static_cast<std::int16_t>(m_upd_sample + kUpdStep[m_upd_adpcm_state][nibble & 15]);
    const int state = std::clamp(int(m_upd_adpcm_state) + kUpdStateDelta[index[nibble & 15]], 0, 15);
    m_upd_adpcm_state = static_cast<std::uint8_t>(state);
}
void System16BSound::advance_upd7759_clock() {
    if (m_upd_state == UpdState::Idle) return;
    if (m_upd_clocks_left) {
        if (--m_upd_clocks_left) return;
    }
    if (m_upd_drq) {
        m_upd_drq = false;
        m_upd_clocks_left = m_upd_post_clocks;
        m_upd_state = m_upd_post_state;
        return;
    }
    auto request = [&](UpdState state, std::uint32_t clocks) {
        m_upd_post_state = state;
        m_upd_post_clocks = clocks > 21 ? clocks - 21 : 0;
        m_upd_state = state;
        m_upd_clocks_left = 21;
        request_upd7759_byte();
    };
    switch (m_upd_state) {
    case UpdState::Idle: return;
    case UpdState::Start:
        m_upd_req_sample = 0x10;
        m_upd_state = UpdState::FirstRequest;
        m_upd_clocks_left = 70;
        break;
    case UpdState::FirstRequest: request(UpdState::LastSample, 44); break;
    case UpdState::LastSample:
        m_upd_last_sample = m_upd7759_fifo;
        request(m_upd_req_sample > m_upd_last_sample ? UpdState::Idle : UpdState::Dummy1, 28); break;
    case UpdState::Dummy1: request(UpdState::AddressMsb, 32); break;
    case UpdState::AddressMsb: m_upd_offset = std::uint32_t(m_upd7759_fifo) << 9; request(UpdState::AddressLsb, 44); break;
    case UpdState::AddressLsb: m_upd_offset |= std::uint32_t(m_upd7759_fifo) << 1; request(UpdState::Dummy2, 36); break;
    case UpdState::Dummy2:
        ++m_upd_offset;
        m_upd_first_valid_header = false;
        request(UpdState::Header, 36);
        break;
    case UpdState::Header:
        if (m_upd_repeat_count) { --m_upd_repeat_count; m_upd_offset = m_upd_repeat_offset; }
        m_upd_header = m_upd7759_fifo;
        switch (m_upd_header & 0xc0) {
        case 0x00: {
            m_upd_sample = 0;
            m_upd_adpcm_state = 0;
            const bool end_marker = m_upd_header == 0 && m_upd_first_valid_header;
            const auto next_state = end_marker ? UpdState::Idle : UpdState::Header;
            request(next_state, 1024U * ((m_upd_header & 0x3f) + 1));
            break;
        }
        case 0x40: m_upd_sample_rate = (m_upd_header & 0x3f) + 1; m_upd_nibbles_left = 256; request(UpdState::NibbleMsb, 36); break;
        case 0x80: m_upd_sample_rate = (m_upd_header & 0x3f) + 1; request(UpdState::Count, 36); break;
        case 0xc0: m_upd_repeat_count = (m_upd_header & 7) + 1; m_upd_repeat_offset = m_upd_offset; request(UpdState::Header, 36); break;
        }
        if (m_upd_header != 0) m_upd_first_valid_header = true;
        break;
    case UpdState::Count: m_upd_nibbles_left = std::uint32_t(m_upd7759_fifo) + 1; request(UpdState::NibbleMsb, 36); break;
    case UpdState::NibbleMsb:
        m_upd_adpcm_data = m_upd7759_fifo; update_upd7759_adpcm(m_upd_adpcm_data >> 4);
        m_upd_clocks_left = m_upd_sample_rate * 4;
        m_upd_state = (--m_upd_nibbles_left == 0) ? UpdState::Header : UpdState::NibbleLsb;
        request(m_upd_state, m_upd_sample_rate * 4); break;
    case UpdState::NibbleLsb:
        update_upd7759_adpcm(m_upd_adpcm_data & 15); m_upd_clocks_left = m_upd_sample_rate * 4;
        m_upd_state = (--m_upd_nibbles_left == 0) ? UpdState::Header : UpdState::NibbleMsb;
        break;
    }
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
