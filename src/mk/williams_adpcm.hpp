#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "ymfm_opm.h"

namespace mamerecomp::mk {

// Williams ADPCM sound-board bus used by T-unit Mortal Kombat. This models
// the MC6809E address map and latches; CPU execution and chip synthesis are
// attached through the supplied callbacks.
class WilliamsAdpcmBoard final : private ymfm::ymfm_interface {
public:
    struct AudioDiagnostics {
        std::uint64_t ym_writes{};
        std::uint64_t oki_writes{};
        std::uint64_t dac_writes{};
        std::uint64_t generated_sample_frames{};
        std::uint64_t nonzero_sample_frames{};
        std::uint16_t peak{};
        std::uint16_t ym_peak{};
        std::uint16_t oki_peak{};
        std::uint16_t dac_peak{};
    };

    struct ChipBus {
        void* opaque{};
        std::uint8_t (*ym_read)(void*, std::uint8_t port){};
        void (*ym_write)(void*, std::uint8_t port, std::uint8_t value){};
        std::uint8_t (*oki_read)(void*){};
        void (*oki_write)(void*, std::uint8_t value){};
        void (*dac_write)(void*, std::uint8_t value){};
    };

    WilliamsAdpcmBoard(std::span<const std::uint8_t> sound_cpu_rom,
                       std::span<const std::uint8_t> oki_rom);
    WilliamsAdpcmBoard(std::span<const std::uint8_t> sound_cpu_rom,
                       std::span<const std::uint8_t> oki_rom, ChipBus chips);

    std::uint8_t sound_cpu_read(std::uint16_t address) noexcept;
    void sound_cpu_write(std::uint16_t address, std::uint8_t value) noexcept;
    void write_main_command(std::uint8_t value) noexcept;
    void reset_write(bool asserted) noexcept;
    void advance_sound_cycles(std::uint32_t cycles) noexcept;

    bool irq_asserted() const noexcept;
    bool firq_asserted() const noexcept;
    bool reset_asserted() const noexcept;
    std::uint8_t talkback() const noexcept;
    std::uint8_t cpu_bank() const noexcept;
    std::uint8_t oki_bank() const noexcept;
    std::uint8_t oki_rom_read(std::uint32_t address) const noexcept;
    std::span<const std::uint8_t> ram() const noexcept;
    std::span<const std::int16_t> audio_samples() const noexcept;
    std::vector<std::int16_t> take_audio_samples() noexcept;
    AudioDiagnostics audio_diagnostics() const noexcept;

private:
    struct OkiVoice {
        bool playing{};
        std::uint32_t start{};
        std::uint32_t nibble{};
        std::uint32_t nibble_count{};
        int signal{};
        int step{};
        std::uint8_t volume{};
    };

    void ymfm_set_timer(std::uint32_t timer, std::int32_t duration) override;
    void ymfm_set_busy_end(std::uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class type, std::uint32_t address,
                             std::uint8_t data) override;
    void oki_write(std::uint8_t value) noexcept;
    std::uint32_t oki_sample_address(std::uint32_t offset) const noexcept;
    void clock_oki_nibble() noexcept;
    void advance_oki_clock() noexcept;
    int oki_output() const noexcept;
    void render_audio_to(std::uint64_t sound_cycle) noexcept;

    std::vector<std::uint8_t> m_cpu_rom;
    std::vector<std::uint8_t> m_oki_rom;
    std::array<std::uint8_t, 0x2000> m_ram{};
    std::array<std::uint8_t, 43> m_hidden_ram{};
    std::array<OkiVoice, 4> m_oki_voices{};
    std::array<std::uint64_t, 2> m_timer_deadlines{};
    std::array<bool, 2> m_timer_active{};
    std::vector<std::int16_t> m_audio;
    ymfm::ym2151 m_ym;
    ChipBus m_chips{};
    std::uint8_t m_command{};
    std::uint8_t m_talkback{};
    std::uint8_t m_cpu_bank{};
    std::uint8_t m_oki_bank{};
    std::uint8_t m_fake_status_reads{};
    std::uint32_t m_irq_release_cycles{};
    std::uint64_t m_sound_cycles{};
    std::uint64_t m_generated_samples{};
    AudioDiagnostics m_audio_diagnostics{};
    std::uint64_t m_ym_clock{};
    std::uint64_t m_busy_until{};
    std::uint64_t m_oki_clock_fraction{};
    std::uint32_t m_oki_divider_count{};
    std::uint8_t m_ym_address{};
    int m_pending_oki_sample{-1};
    std::uint8_t m_dac_value{0x80};
    bool m_irq{};
    bool m_firq{};
    bool m_reset{true};
};

} // namespace mamerecomp::mk
