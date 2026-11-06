#pragma once

#include "system16.hpp"
#include "callback_bus.hpp"
#include "upd7759.hpp"
#include "ymfm_opm.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct SDL_AudioStream;

namespace mamerecomp {

class System16BSound final : private ymfm::ymfm_interface {
public:
    System16BSound(std::span<const std::uint8_t> sound_region, bool audio_output, std::uint8_t master_volume = 100,
        System16BBoard board = System16BBoard::Sega5797, bool stereo_spread = false);
    ~System16BSound();
    System16BSound(const System16BSound&) = delete;
    System16BSound& operator=(const System16BSound&) = delete;
    void advance_to_main_cycles(std::uint64_t main_cycles);
    void write_sound_latch(std::uint8_t value);
    std::uint64_t z80_cycles() const { return m_cpu.cycles; }
    std::uint64_t z80_instructions() const { return m_z80_instructions; }
    std::uint64_t z80_aot_instructions() const { return m_cpu.nativeInstructions; }
    std::uint64_t z80_fallback_instructions() const { return m_cpu.interpreted; }
    std::uint16_t z80_pc() const { return m_cpu.pc; }
    std::uint64_t ym2151_writes() const { return m_ym2151_writes; }
    std::uint64_t audio_samples() const { return m_audio_samples; }
    std::uint16_t audio_peak() const { return m_audio_peak; }
    std::uint64_t upd7759_writes() const { return m_upd7759_writes; }
    std::uint8_t upd7759_last_byte() const { return m_upd7759_fifo; }
    std::uint64_t upd7759_nmi_edges() const { return m_upd_nmi_edges; }
    std::uint64_t z80_nmi_serviced() const { return m_z80_nmi_serviced; }
    std::uint64_t ym2151_key_ons() const { return m_ym2151_key_ons; }
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
    std::uint64_t m_ym2151_key_ons = 0;
    std::uint64_t m_generated_ym_samples = 0, m_busy_until = 0, m_upd7759_writes = 0, m_audio_samples = 0;
    std::uint64_t m_upd_nmi_edges = 0, m_z80_nmi_serviced = 0;
    std::uint32_t m_upd_clock_fraction = 0;
    audio::Upd7759 m_upd7759{audio::Upd7759::Mode::Fifo};
    std::array<std::uint64_t, 2> m_timer_deadlines{};
    std::array<bool, 2> m_timer_active{};
    bool m_ym2151_irq = false, m_md = false, m_upd_reset_released = false;
    bool m_stereo_spread = false;
    System16BBoard m_rom_board = System16BBoard::Sega5797;
    std::uint16_t m_audio_peak = 0;
    std::vector<std::int16_t> m_audio_buffer;
    SDL_AudioStream* m_audio_stream = nullptr;
};

} // namespace mamerecomp
