#pragma once

#include "callback_bus.hpp"
#include "ymfm_opn.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

struct SDL_AudioStream;

namespace mamerecomp {

class System18Sound {
public:
    System18Sound(std::span<const std::uint8_t> sound_region, bool audio_output,
        std::uint8_t master_volume = 100, bool stereo_spread = false);
    ~System18Sound();
    System18Sound(const System18Sound&) = delete;
    System18Sound& operator=(const System18Sound&) = delete;

    void advance_to_main_cycles(std::uint64_t main_cycles);
    void write_sound_latch(std::uint8_t value);
    void set_sound_reply_callback(std::function<void(std::uint8_t)> callback) {
        m_sound_reply_callback = std::move(callback);
    }
    std::uint64_t z80_cycles() const { return m_cpu.cycles; }
    std::uint64_t ym_writes(unsigned chip) const { return m_ym_writes[chip & 1U]; }
    std::uint64_t pcm_memory_writes() const { return m_pcm_memory_writes; }
    std::uint8_t pcm_enabled_channels() const { return m_pcm_enabled_channels; }
    std::uint16_t pcm_peak() const { return m_pcm_peak; }
    std::uint64_t audio_samples() const { return m_audio_samples; }
    std::uint16_t audio_peak() const { return m_audio_peak; }

private:
    class YmInterface final : public ymfm::ymfm_interface {
    public:
        YmInterface(System18Sound& owner, unsigned chip) : m_owner(owner), m_chip(chip) { }
        void timer_expired(std::uint32_t timer) { m_engine->engine_timer_expired(timer); }
        void ymfm_set_timer(std::uint32_t, std::int32_t) override;
        void ymfm_set_busy_end(std::uint32_t) override;
        bool ymfm_is_busy() override;
        void ymfm_update_irq(bool) override;
    private:
        System18Sound& m_owner;
        unsigned m_chip;
    };

    std::uint8_t memory_read(std::uint16_t);
    void memory_write(std::uint16_t, std::uint8_t);
    std::uint8_t port_read(std::uint8_t);
    void port_write(std::uint8_t, std::uint8_t);
    void synchronize_audio(std::uint64_t ym_clocks);
    std::array<std::int16_t, 2> generate_pcm_sample();
    void write_pcm_register(std::uint8_t offset, std::uint8_t value);
    void flush_audio_samples();

    std::vector<std::uint8_t> m_rom;
    std::array<std::uint8_t, 0x2000> m_ram{};
    std::array<std::uint8_t, 0x10000> m_pcm_ram{};
    struct PcmChannel {
        std::uint8_t env = 0, pan = 0, start = 0;
        std::uint32_t address = 0;
        std::uint16_t step = 0, loop = 0;
        bool enabled = false;
    };
    std::array<PcmChannel, 8> m_pcm_channels{};
    z80::CallbackBus m_z80_bus;
    z80::Z80 m_cpu;
    YmInterface m_ym_interface0;
    YmInterface m_ym_interface1;
    ymfm::ym3438 m_ym0;
    ymfm::ym3438 m_ym1;
    std::array<std::uint64_t, 2> m_timer_deadlines{};
    std::array<bool, 2> m_timer_active{};
    std::array<std::uint64_t, 2> m_busy_until{};
    std::array<bool, 2> m_irq{};
    std::array<std::uint64_t, 2> m_ym_writes{};
    std::array<std::array<std::uint8_t, 2>, 2> m_ym_address{};
    std::uint8_t m_sound_latch = 0xff;
    std::function<void(std::uint8_t)> m_sound_reply_callback;
    std::uint8_t m_sound_bank = 0;
    std::uint8_t m_pcm_channel_bank = 0;
    std::uint16_t m_pcm_write_bank = 0;
    std::uint64_t m_pcm_clock_fraction = 0;
    std::uint64_t m_pcm_memory_writes = 0;
    std::uint8_t m_pcm_enabled_channels = 0;
    bool m_pcm_enabled = false;
    std::array<std::int16_t, 2> m_pcm_output{};
    std::uint8_t m_last_ym_port = 0;
    std::uint8_t m_master_volume = 100;
    bool m_stereo_spread = false;
    std::uint64_t m_ym_clock = 0;
    std::uint64_t m_generated_samples = 0;
    std::uint64_t m_audio_samples = 0;
    std::uint16_t m_audio_peak = 0;
    std::uint16_t m_pcm_peak = 0;
    std::vector<std::int16_t> m_audio_buffer;
    SDL_AudioStream* m_audio_stream = nullptr;
};

} // namespace mamerecomp
