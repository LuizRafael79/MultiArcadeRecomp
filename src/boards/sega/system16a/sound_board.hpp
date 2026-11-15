#pragma once

#include "upd7751.hpp"
#include "boards/sega/common/audio_sink.hpp"
#include "callback_bus.hpp"
#include "chips/i8243/i8243.hpp"
#include "chips/ymfm_timer/ymfm_timer.hpp"
#include "dac.hpp"
#include "ymfm_opm.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace mamerecomp {

class System16ASound final : private ymfm::ymfm_interface {
public:
    explicit System16ASound(std::span<const std::uint8_t> sound_rom,
        std::span<const std::uint8_t> upd7751_rom, std::span<const std::uint8_t> upd7751_data,
        SegaAudioSink audio_sink = {}, bool stereo_spread = false, std::uint8_t master_volume = 100);
    ~System16ASound();
    System16ASound(const System16ASound&) = delete;
    System16ASound& operator=(const System16ASound&) = delete;

    void advance_to_main_cycles(std::uint64_t main_cycles);
    void write_sound_latch(std::uint8_t value);
    void set_nmi_line(bool asserted);
    std::uint8_t read_sound_latch();
    void set_latch_read_callback(std::function<void()> callback) { m_latch_read_callback = std::move(callback); }

    std::uint64_t z80_cycles() const { return m_cpu.cycles; }
    std::uint64_t z80_instructions() const { return m_z80_instructions; }
    std::uint64_t z80_aot_instructions() const { return m_cpu.nativeInstructions; }
    std::uint64_t z80_fallback_instructions() const { return m_cpu.interpreted; }
    std::uint64_t ym2151_writes() const { return m_ym2151_writes; }
    std::uint64_t ym2151_key_ons() const { return m_ym2151_key_ons; }
    const std::array<std::uint64_t, 8>& ym2151_key_ons_by_channel() const { return m_ym2151_key_ons_by_channel; }
    const std::array<std::uint8_t, 8>& ym2151_pan_by_channel() const { return m_ym2151_pan_by_channel; }
    std::uint8_t ym2151_last_register() const { return m_ym2151_address; }
    std::uint8_t ym2151_last_value() const { return m_ym2151_last_value; }
    std::uint64_t upd7751_commands() const { return m_upd7751_commands; }
    std::uint64_t upd7751_instructions() const { return m_upd7751_instructions; }
    std::uint64_t upd7751_p1_writes() const { return m_upd7751_p1_writes; }
    std::uint16_t upd7751_pc() const { return m_upd7751 ? m_upd7751->pc() : 0; }
    std::uint64_t nmi_edges() const { return m_nmi_edges; }
    std::uint64_t sound_latch_reads() const { return m_sound_latch_reads; }
    std::uint8_t sound_latch() const { return m_sound_latch; }
    std::uint8_t upd7751_last_command() const { return m_upd7751_command; }
    std::uint64_t audio_samples() const { return m_audio_samples; }
    std::uint16_t audio_peak() const { return m_audio_peak; }
    std::uint16_t stereo_difference_peak() const { return m_stereo_difference_peak; }
    std::uint16_t z80_pc() const { return m_cpu.pc; }

private:
    std::uint8_t memory_read(std::uint16_t address);
    void memory_write(std::uint16_t address, std::uint8_t value);
    std::uint8_t port_read(std::uint8_t port);
    void port_write(std::uint8_t port, std::uint8_t value);
    void synchronize_ym2151(std::uint64_t z80_cycles);
    void advance_upd7751_to_sound_cycles(std::uint64_t z80_cycles);
    std::uint8_t upd7751_bus_read() const;
    std::uint8_t upd7751_port_read(unsigned port) const;
    void upd7751_port_write(unsigned port, std::uint8_t value);
    void upd7751_prog_write(bool state);
    void update_upd7751_sample_address(unsigned port, std::uint8_t value);
    void flush_audio_samples();

    void ymfm_set_timer(std::uint32_t timer, std::int32_t duration_in_clocks) override;
    void ymfm_set_busy_end(std::uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class type, std::uint32_t address, std::uint8_t data) override;

    std::vector<std::uint8_t> m_rom;
    std::vector<std::uint8_t> m_upd7751_rom;
    std::vector<std::uint8_t> m_upd7751_data;
    std::array<std::uint8_t, 0x800> m_ram{};
    z80::CallbackBus m_z80_bus;
    z80::Z80 m_cpu;
    std::unique_ptr<Upd7751> m_upd7751;
    chips::I8243 m_upd7751_expander;
    ymfm::ym2151 m_ym2151;
    std::uint8_t m_sound_latch = 0xff;
    std::uint8_t m_upd7751_command = 0;
    std::uint8_t m_upd7751_command_index = 0;
    std::uint8_t m_upd7751_control = 0;
    std::uint32_t m_upd7751_rom_address = 0;
    audio::Dac8 m_upd7751_dac{0xff};
    audio::DcBlocker m_upd7751_dac_filter{999, 1000, 6'502};
    std::uint64_t m_upd7751_instructions = 0;
    std::uint64_t m_upd7751_p1_writes = 0;
    std::uint8_t m_ym2151_address = 0;
    std::uint8_t m_ym2151_last_value = 0;
    bool m_nmi_line_asserted = false;
    bool m_ym2151_irq = false;
    bool m_ym_write_in_progress = false;
    bool m_stereo_spread = false;
    std::uint8_t m_master_volume = 100;
    std::uint64_t m_ym_clock = 0;
    std::uint64_t m_generated_ym_samples = 0;
    chips::YmfmTimerBank m_ym_timers;
    std::uint64_t m_z80_instructions = 0;
    std::uint64_t m_ym2151_writes = 0;
    std::uint64_t m_ym2151_key_ons = 0;
    std::array<std::uint64_t, 8> m_ym2151_key_ons_by_channel{};
    std::array<std::uint8_t, 8> m_ym2151_pan_by_channel{0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0};
    std::uint64_t m_upd7751_commands = 0;
    std::uint64_t m_nmi_edges = 0;
    std::uint64_t m_sound_latch_reads = 0;
    std::uint64_t m_audio_samples = 0;
    std::uint16_t m_audio_peak = 0;
    std::uint16_t m_stereo_difference_peak = 0;
    std::vector<std::int16_t> m_audio_buffer;
    std::function<void()> m_latch_read_callback;
    SegaAudioSink m_audio_sink;
};

} // namespace mamerecomp
