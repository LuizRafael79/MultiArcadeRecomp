#pragma once

#include "boards/sega/common/board_family.hpp"
#include "boards/sega/common/audio_sink.hpp"
#include "chips/ymfm_timer/ymfm_timer.hpp"
#include "callback_bus.hpp"
#include "upd7759.hpp"
#include "ymfm_opm.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <span>
#include <vector>

namespace multiarcaderecomp {

class System16BSound final : private ymfm::ymfm_interface {
public:
    System16BSound(std::span<const std::uint8_t> sound_region, SegaAudioSink audio_sink = {},
        std::uint8_t master_volume = 100, System16BBoard board = System16BBoard::Sega5797,
        bool stereo_spread = false, std::uint8_t muted_ym_channels = 0,
        bool suppress_slot0_channel2 = false, bool suppress_slot3_channel5 = false,
        bool suppress_slot4_channel6 = false, bool suppress_slot5_channel7 = false,
        std::function<bool(std::uint8_t)> sound_command_observer = {});
    ~System16BSound();
    System16BSound(const System16BSound&) = delete;
    System16BSound& operator=(const System16BSound&) = delete;
    void advance_to_main_cycles(std::uint64_t main_cycles);
    void write_sound_latch(std::uint8_t value, std::uint64_t maincpu_cycle = 0,
        std::uint32_t maincpu_pc = 0);
    void trace_inputs(std::uint8_t service, std::uint8_t p1, std::uint8_t p2);
    bool sound_trace_enabled() const { return static_cast<bool>(m_sound_trace); }
    std::uint64_t z80_cycles() const { return m_cpu.cycles; }
    std::uint64_t z80_instructions() const { return m_z80_instructions; }
    std::uint64_t z80_aot_instructions() const { return m_cpu.nativeInstructions; }
    std::uint64_t z80_fallback_instructions() const { return m_cpu.interpreted; }
    std::uint16_t z80_pc() const { return m_cpu.pc; }
    std::uint64_t ym2151_writes() const { return m_ym2151_writes; }
    std::uint64_t ym2151_pan_overrides() const { return m_ym2151_pan_overrides; }
    const std::array<std::uint8_t, 8>& ym2151_pan_by_channel() const { return m_ym2151_pan_by_channel; }
    std::uint64_t audio_samples() const { return m_audio_samples; }
    std::uint16_t audio_peak() const { return m_audio_peak; }
    std::uint16_t stereo_difference_peak() const { return m_stereo_difference_peak; }
    std::uint64_t upd7759_writes() const { return m_upd7759_writes; }
    std::uint8_t upd7759_last_byte() const { return m_upd7759_fifo; }
    std::uint64_t upd7759_nmi_edges() const { return m_upd_nmi_edges; }
    std::uint64_t z80_nmi_serviced() const { return m_z80_nmi_serviced; }
    std::uint64_t ym2151_key_ons() const { return m_ym2151_key_ons; }
    std::uint64_t ym2151_suppressed_voice_writes() const { return m_ym2151_suppressed_voice_writes; }
private:
    std::uint8_t memory_read(std::uint16_t);
    void memory_write(std::uint16_t, std::uint8_t);
    std::uint8_t port_read(std::uint8_t);
    void port_write(std::uint8_t, std::uint8_t);
    void synchronize_ym2151(std::uint64_t);
    void flush_audio_samples();
    void ymfm_set_timer(std::uint32_t, std::int32_t) override;
    void ymfm_set_busy_end(std::uint32_t) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool) override;
    void ymfm_external_write(ymfm::access_class, std::uint32_t, std::uint8_t) override;

    std::vector<std::uint8_t> m_rom;
    std::array<std::uint8_t, 0x800> m_ram{};
    z80::CallbackBus m_z80_bus;
    z80::Z80 m_cpu;
    ymfm::ym2151 m_ym2151;
    std::uint8_t m_sound_latch = 0xff, m_bank = 0, m_ym_address = 0, m_upd7759_fifo = 0xff;
    std::uint8_t m_master_volume = 100;
    std::uint64_t m_z80_instructions = 0, m_ym2151_writes = 0, m_ym_clock = 0;
    std::uint64_t m_ym2151_key_ons = 0, m_ym2151_suppressed_voice_writes = 0;
    std::uint64_t m_ym2151_pan_overrides = 0;
    std::array<std::uint8_t, 8> m_ym2151_pan_by_channel{};
    std::uint64_t m_generated_ym_samples = 0, m_upd7759_writes = 0, m_audio_samples = 0;
    std::uint64_t m_upd_nmi_edges = 0, m_z80_nmi_serviced = 0;
    std::uint32_t m_upd_clock_fraction = 0;
    audio::Upd7759 m_upd7759{audio::Upd7759::Mode::Fifo};
    chips::YmfmTimerBank m_ym_timers;
    bool m_ym2151_irq = false, m_md = false, m_upd_reset_released = false;
    bool m_stereo_spread = false;
    bool m_suppress_slot0_channel2 = false, m_suppress_slot3_channel5 = false;
    bool m_suppress_slot4_channel6 = false, m_suppress_slot5_channel7 = false;
    bool m_suppress_chardecide_slots = false;
    std::uint8_t m_muted_ym_channels = 0;
    bool m_arranged_filtering_active = false;
    bool m_restore_audio_after_deathadder_fade = false;
    bool m_waiting_for_deathadder_fade_steps = false;
    std::ofstream m_sound_trace;
    std::ofstream m_sound_command_trace;
    std::uint64_t m_sound_trace_rows = 0;
    std::array<std::uint8_t, 3> m_last_trace_inputs{0xff, 0xff, 0xff};
    bool m_trace_inputs_valid = false;
    System16BBoard m_rom_board = System16BBoard::Sega5797;
    std::uint16_t m_audio_peak = 0;
    std::uint16_t m_stereo_difference_peak = 0;
    std::vector<std::int16_t> m_audio_buffer;
    SegaAudioSink m_audio_sink;
    std::function<bool(std::uint8_t)> m_sound_command_observer;
};

} // namespace multiarcaderecomp
