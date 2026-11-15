#include "cps1_sound_ym_oki.hpp"
#include "cps1_sound_qsound.hpp"
#include "cps1_state.hpp"

#include <neogeorecomp/cpu.h>
#include "native_registry.h"
#include "native_z80.h"
#include "z80.h"
#include "ymfm_opm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <utility>

#ifdef MULTIARCADERECOMP_CPS1_SDL
#include <SDL3/SDL.h>
#endif

namespace multiarcaderecomp::cps1::detail {
namespace {
constexpr std::uint32_t kYmClocksPerSample = 64;
constexpr std::uint32_t kOkiClockHz = 1'000'000;
constexpr std::size_t kFlushFrames = 256;
constexpr std::array<int, 49> kOkiStepTable{
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
    80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
    307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552
};
constexpr std::array<int, 8> kOkiIndexShift{-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 16> kOkiVolume{32, 22, 16, 11, 8, 6, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0};

struct OkiVoice {
    bool playing = false;
    std::uint32_t start = 0;
    std::uint32_t nibble = 0;
    std::uint32_t nibble_count = 0;
    int signal = 0;
    int step = 0;
    std::uint8_t volume = 0;
};

class Cps1SoundDevices final : private ymfm::ymfm_interface {
public:
    Cps1SoundDevices() : m_ym(*this) { }
    ~Cps1SoundDevices() { shutdown(); }

    bool initialize(std::vector<std::uint8_t> samples, bool audio_output) {
        // Some exported workspaces contain only the loaded 0x30000-byte span
        // while the MAME region is 0x40000 bytes. Respect the supplied extent;
        // sample addresses are checked against it before playback.
        if (samples.size() > 0x40000) return false;
        m_samples = std::move(samples);
        m_ym.reset();
        m_oki_voices = {};
        m_pending_sample = -1;
        m_generated_samples = 0;
        m_oki_clock_fraction = 0;
        m_oki_divider_count = 0;
        m_oki_pin7 = true;
        m_ym_clock = 0;
        m_busy_until = 0;
        m_timer_active = {};
        m_timer_deadlines = {};
        m_ym_writes = 0;
        m_oki_writes = 0;
        m_audio_samples = 0;
        m_audio_peak = 0;
        m_audio_buffer.clear();
        m_audio_output = audio_output;
        m_initialized = true;
        if (!audio_output) return true;
#ifdef MULTIARCADERECOMP_CPS1_SDL
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            std::cerr << "[cps1-audio] SDL audio init failed: " << SDL_GetError() << '\n';
            return false;
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(m_ym.sample_rate(g_sound_clock_hz));
        m_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!m_audio_stream) {
            std::cerr << "[cps1-audio] could not open playback device: " << SDL_GetError() << '\n';
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(m_audio_stream)) {
            std::cerr << "[cps1-audio] could not start playback: " << SDL_GetError() << '\n';
            SDL_DestroyAudioStream(m_audio_stream);
            m_audio_stream = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        m_audio_buffer.reserve(kFlushFrames * 2);
        return true;
#else
        std::cerr << "[cps1-audio] this build has no SDL3 audio support; rebuild with MULTIARCADERECOMP_ENABLE_WINDOW=ON\n";
        return false;
#endif
    }

    void shutdown() {
        flush_audio();
#ifdef MULTIARCADERECOMP_CPS1_SDL
        if (m_audio_stream) {
            SDL_DestroyAudioStream(m_audio_stream);
            m_audio_stream = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
#endif
        m_audio_output = false;
        m_initialized = false;
    }

    std::uint8_t ym_read() { return m_ym.read(1); }
    void ym_write_address(std::uint8_t value) { m_ym_address = value; m_ym.write(0, value); }
    void ym_write_data(std::uint8_t value) {
        m_ym.write(1, value);
        ++m_ym_writes;
    }
    std::uint8_t oki_status() const {
        std::uint8_t result = 0xf0;
        for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice)
            if (m_oki_voices[voice].playing) result |= static_cast<std::uint8_t>(1U << voice);
        return result;
    }
    void oki_write(std::uint8_t value) {
        ++m_oki_writes;
        if (m_pending_sample >= 0) {
            const unsigned voice_mask = value >> 4;
            const std::uint32_t table = static_cast<std::uint32_t>(m_pending_sample) * 8;
            for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice) {
                if ((voice_mask & (1U << voice)) == 0 || m_oki_voices[voice].playing) continue;
                const std::uint32_t start = read_sample_address(table) & 0x3ffff;
                const std::uint32_t end = read_sample_address(table + 3) & 0x3ffff;
                if (start >= end || end >= m_samples.size()) continue;
                auto& state = m_oki_voices[voice];
                state = {};
                state.playing = true;
                state.start = start;
                state.nibble_count = (end - start + 1) * 2;
                state.volume = value & 0x0f;
            }
            m_pending_sample = -1;
        } else if (value & 0x80) {
            m_pending_sample = value & 0x7f;
        } else {
            const unsigned voice_mask = (value >> 3) & 0x0f;
            for (unsigned voice = 0; voice < m_oki_voices.size(); ++voice)
                if (voice_mask & (1U << voice)) m_oki_voices[voice].playing = false;
        }
    }
    void set_oki_pin7(std::uint8_t value) {
        m_oki_pin7 = (value & 1) != 0;
        m_oki_divider_count = 0;
    }
    void sync(std::uint64_t target_z80_cycles) {
        if (!m_initialized) return;
        while ((m_generated_samples + 1) * kYmClocksPerSample <= target_z80_cycles) {
            const std::uint64_t sample_clock = (++m_generated_samples) * kYmClocksPerSample;
            m_ym_clock = sample_clock;
            for (unsigned timer = 0; timer < m_timer_active.size(); ++timer) {
                if (m_timer_active[timer] && m_timer_deadlines[timer] <= sample_clock) {
                    m_timer_active[timer] = false;
                    m_engine->engine_timer_expired(timer);
                }
            }
            ymfm::ym2151::output_data output{};
            m_ym.generate(&output, 1);
            output.clamp16();
            advance_oki_clock();
            const int oki = oki_output() * 16 * 30 / 100;
            const int left = std::clamp(output.data[0] * 35 / 100 + oki, -32768, 32767);
            const int right = std::clamp(output.data[1] * 35 / 100 + oki, -32768, 32767);
            m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(
                std::max(std::abs(left), std::abs(right))));
            if (m_audio_output) {
                m_audio_buffer.push_back(static_cast<std::int16_t>(left));
                m_audio_buffer.push_back(static_cast<std::int16_t>(right));
                if (m_audio_buffer.size() >= kFlushFrames * 2) flush_audio();
            }
            ++m_audio_samples;
        }
        m_ym_clock = target_z80_cycles;
    }
    std::uint64_t ym_writes() const { return m_ym_writes; }
    std::uint64_t oki_writes() const { return m_oki_writes; }
    std::uint64_t audio_samples() const { return m_audio_samples; }
    std::uint16_t audio_peak() const { return m_audio_peak; }
    std::uint64_t active_oki_voices() const {
        return std::count_if(m_oki_voices.begin(), m_oki_voices.end(), [](const OkiVoice& v) { return v.playing; });
    }

protected:
    void ymfm_set_timer(std::uint32_t timer, std::int32_t duration) override {
        if (timer >= m_timer_active.size()) return;
        if (duration < 0) { m_timer_active[timer] = false; return; }
        m_timer_active[timer] = true;
        m_timer_deadlines[timer] = m_ym_clock + static_cast<std::uint32_t>(duration);
    }
    void ymfm_set_busy_end(std::uint32_t clocks) override { m_busy_until = m_ym_clock + clocks; }
    bool ymfm_is_busy() override { return m_ym_clock < m_busy_until; }
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class, std::uint32_t, std::uint8_t) override { }

private:
    std::uint32_t read_sample_address(std::uint32_t offset) const {
        if (offset + 2 >= m_samples.size()) return 0x3ffff;
        return (std::uint32_t(m_samples[offset]) << 16)
            | (std::uint32_t(m_samples[offset + 1]) << 8) | m_samples[offset + 2];
    }
    void clock_oki_nibble() {
        for (auto& voice : m_oki_voices) {
            if (!voice.playing) continue;
            const std::uint32_t byte_address = voice.start + voice.nibble / 2;
            if (byte_address >= m_samples.size() || voice.nibble >= voice.nibble_count) {
                voice.playing = false;
                continue;
            }
            const std::uint8_t byte = m_samples[byte_address];
            const unsigned nibble = (voice.nibble & 1) ? (byte & 0x0f) : (byte >> 4);
            const int step = kOkiStepTable[voice.step];
            int difference = ((2 * static_cast<int>(nibble & 7) + 1) * step) >> 3;
            if (nibble & 8) difference = -difference;
            voice.signal = std::clamp(voice.signal + difference, -2048, 2047);
            voice.step = std::clamp(voice.step + kOkiIndexShift[nibble & 7], 0, 48);
            if (++voice.nibble >= voice.nibble_count) voice.playing = false;
        }
    }
    void advance_oki_clock() {
        // One YM output sample spans 64 Z80/YM clocks. Convert that interval
        // into the OKI's independent 1 MHz input clock with an exact remainder.
        m_oki_clock_fraction += std::uint64_t(kYmClocksPerSample) * kOkiClockHz;
        while (m_oki_clock_fraction >= g_sound_clock_hz) {
            m_oki_clock_fraction -= g_sound_clock_hz;
            const unsigned divider = m_oki_pin7 ? 132 : 165;
            if (++m_oki_divider_count >= divider) {
                m_oki_divider_count = 0;
                clock_oki_nibble();
            }
        }
    }
    int oki_output() const {
        int mix = 0;
        for (const auto& voice : m_oki_voices)
            if (voice.playing)
                mix += voice.signal * kOkiVolume[voice.volume] / 32;
        return std::clamp(mix, -2048, 2047);
    }
    void flush_audio() {
#ifdef MULTIARCADERECOMP_CPS1_SDL
        if (m_audio_stream && !m_audio_buffer.empty()) {
            const int bytes = static_cast<int>(m_audio_buffer.size() * sizeof(std::int16_t));
            if (!SDL_PutAudioStreamData(m_audio_stream, m_audio_buffer.data(), bytes))
                std::cerr << "[cps1-audio] playback queue failed: " << SDL_GetError() << '\n';
            m_audio_buffer.clear();
        }
#endif
    }

    std::vector<std::uint8_t> m_samples;
    std::array<OkiVoice, 4> m_oki_voices{};
    std::array<std::uint64_t, 2> m_timer_deadlines{};
    std::array<bool, 2> m_timer_active{};
    ymfm::ym2151 m_ym;
    std::vector<std::int16_t> m_audio_buffer;
    std::uint64_t m_generated_samples = 0;
    std::uint64_t m_oki_clock_fraction = 0;
    std::uint64_t m_ym_clock = 0;
    std::uint64_t m_busy_until = 0;
    std::uint64_t m_ym_writes = 0;
    std::uint64_t m_oki_writes = 0;
    std::uint64_t m_audio_samples = 0;
    std::uint32_t m_oki_divider_count = 0;
    std::uint8_t m_ym_address = 0;
    std::uint16_t m_audio_peak = 0;
    int m_pending_sample = -1;
    bool m_oki_pin7 = true;
    bool m_audio_output = false;
    bool m_initialized = false;
#ifdef MULTIARCADERECOMP_CPS1_SDL
    SDL_AudioStream* m_audio_stream = nullptr;
#endif
};

multiarcaderecomp::z80::Z80* g_z80_cpu_ptr = nullptr;
Cps1SoundDevices g_devices;
std::vector<std::uint8_t> g_fixed_rom, g_banked_rom;
std::array<std::uint8_t, 0x800> g_ram{};
std::uint8_t g_command = 0xff;
std::uint8_t g_timer_command = 0xff;
std::uint8_t g_bank = 0;
bool g_active = false;
extern multiarcaderecomp::z80::Z80 g_cpu;

class Cps1SoundBus final : public multiarcaderecomp::z80::Z80Bus {
public:
    std::uint8_t Read(std::uint16_t address) override {
        if (address < 0x8000) return address < g_fixed_rom.size() ? g_fixed_rom[address] : 0xff;
        if (address < 0xc000) {
            const std::size_t offset = static_cast<std::size_t>(g_bank) * 0x4000 + (address - 0x8000);
            return offset < g_banked_rom.size() ? g_banked_rom[offset] : 0xff;
        }
        if (address >= 0xd000 && address <= 0xd7ff) return g_ram[address - 0xd000];
        if (address == 0xf000) return 0xff;
        if (address == 0xf001 || address == 0xf002) g_devices.sync(g_cpu.BusCallbackCycles());
        if (address == 0xf001) return g_devices.ym_read();
        if (address == 0xf002) return g_devices.oki_status();
        if (address == 0xf008) return g_command;
        if (address == 0xf00a) return g_timer_command;
        return 0xff;
    }
    void Write(std::uint16_t address, std::uint8_t value) override {
        if (address >= 0xd000 && address <= 0xd7ff) g_ram[address - 0xd000] = value;
        else if (address >= 0xf000 && address <= 0xf006) {
            g_devices.sync(g_cpu.BusCallbackCycles());
            if (address == 0xf000) g_devices.ym_write_address(value);
            else if (address == 0xf001) g_devices.ym_write_data(value);
            else if (address == 0xf002) g_devices.oki_write(value);
            else if (address == 0xf004) g_bank = value & 1;
            else if (address == 0xf006) g_devices.set_oki_pin7(value);
        }
    }
    bool ChangesMapping(std::uint16_t address) override { return address == 0xf004; }
    std::uint32_t Mapping() override { return g_bank; }
    void SetMapping(std::uint32_t mapping) override { g_bank = static_cast<std::uint8_t>(mapping & 1); }
};

Cps1SoundBus g_bus;
multiarcaderecomp::z80::Z80 g_cpu(g_bus);

void Cps1SoundDevices::ymfm_update_irq(bool asserted) {
    if (g_z80_cpu_ptr) g_z80_cpu_ptr->SetInterruptLine(asserted);
}
} // namespace

bool initialize_sound(std::vector<std::uint8_t> fixed_rom, std::vector<std::uint8_t> banked_rom,
    std::vector<std::uint8_t> oki_samples, const multiarcaderecomp::z80::Z80Code* code, bool audio_output) {
    if (fixed_rom.size() != 0x8000 || banked_rom.size() != 0x8000) return false;
    g_fixed_rom = std::move(fixed_rom);
    g_banked_rom = std::move(banked_rom);
    g_ram.fill(0);
    g_bank = 0;
    multiarcaderecomp::z80::SetZ80Code(code);
    g_cpu.Reset();
    g_z80_cpu_ptr = &g_cpu;
    if (!g_devices.initialize(std::move(oki_samples), audio_output)) return false;
    g_cpu.recording = true;
    g_active = true;
    return true;
}

void sync_sound_to_main_clock() {
    if (qsound_is_active()) { qsound_sync_to_main_clock(); return; }
    if (!g_active || ng_cycles <= 0) return;
    const std::uint64_t target = static_cast<std::uint64_t>(ng_cycles) * g_sound_clock_hz / g_main_clock_hz;
    if (target > g_cpu.cycles) g_cpu.Run(target, multiarcaderecomp::z80::Z80::Mode::Native);
    g_devices.sync(std::max(target, g_cpu.cycles));
}

void write_sound_command(std::uint8_t value) {
    if (qsound_is_active()) { qsound_write_command(value); return; }
    g_command = value;
}
void write_sound_timer_command(std::uint8_t value) { g_timer_command = value; }
std::uint8_t sound_command() { return g_command; }
std::uint64_t sound_ym_register_count() { return g_devices.ym_writes(); }

void print_sound_summary(std::ostream& out) {
    if (qsound_is_active()) { qsound_print_summary(out); return; }
    if (!g_active) return;
    out << " Z80 native blocks=" << g_cpu.nativeBlocks
        << " Z80 interpreted instructions=" << g_cpu.interpreted
        << " Z80 PC=$" << std::hex << g_cpu.pc << std::dec
        << " YM2151 writes=" << g_devices.ym_writes()
        << " OKI writes=" << g_devices.oki_writes()
        << " OKI voices=" << g_devices.active_oki_voices()
        << " audio samples=" << g_devices.audio_samples()
        << " audio peak=" << g_devices.audio_peak();
}

bool save_sound_artifacts(const std::string& entry_path, const std::string& fallback_path) {
    if (qsound_is_active()) return qsound_save_artifacts(entry_path, fallback_path);
    if (!g_active) return true;
    std::sort(g_cpu.newEntries.begin(), g_cpu.newEntries.end());
    g_cpu.newEntries.erase(std::unique(g_cpu.newEntries.begin(), g_cpu.newEntries.end()), g_cpu.newEntries.end());
    std::ofstream entries(entry_path);
    for (std::uint16_t pc : g_cpu.newEntries)
        entries << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << pc << '\n';
    if (!entries) return false;
    multiarcaderecomp::z80::z80native::ReportInterpreterProfile();
    multiarcaderecomp::z80::z80native::PersistFallbackLog(fallback_path.c_str());
    return true;
}

void shutdown_sound() {
    if (qsound_is_active()) { qsound_shutdown(); return; }
    g_devices.shutdown();
    g_active = false;
}

} // namespace multiarcaderecomp::cps1::detail
