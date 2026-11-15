#pragma once

#include "core/cpus/adsp2105/adsp2105.hpp"
#include "core/chips/dcs/host_interface.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace multiarcaderecomp::dcs {

struct DcsAudio2KDiagnostics {
    std::uint64_t host_commands{};
    std::uint64_t overwritten_commands{};
    std::uint64_t host_reads{};
    std::uint64_t empty_output_reads{};
    std::uint64_t host_stall_cycles_total{};
    std::uint64_t input_reads{};
    std::uint64_t output_writes{};
    std::uint64_t sound_rom_reads{};
    std::uint64_t sound_rom_nonzero_reads{};
    std::uint64_t sound_rom_ffff_reads{};
    std::uint64_t sound_bank_writes{};
    std::uint64_t sport_irq1_pulses{};
    std::uint16_t timer_scale{};
    std::uint16_t timer_count{};
    std::uint16_t timer_period{};
    std::uint32_t timer_phase{};
    bool timer_enabled{};
    bool timer_irq_pending{};
    std::uint8_t adsp_icntl{};
    std::uint16_t adsp_imask{};
    bool sport_irq1_active{};
    std::uint64_t local_ram_writes{};
    std::uint64_t local_ram_nonzero_words{};
    std::uint64_t shared_ram_writes{};
    std::uint64_t shared_ram_nonzero_words{};
    std::uint64_t stream_buffer_nonzero_words{};
    std::uint64_t stream_buffer_writes{};
    std::uint64_t stream_buffer_nonzero_writes{};
    std::uint16_t stream_buffer_last_write_address{};
    std::uint16_t stream_buffer_last_write_value{};
    std::uint16_t stream_buffer_last_write_pc{};
    std::uint32_t stream_buffer_last_write_opcode{};
    std::uint16_t stream_buffer_last_write_i7{};
    std::int16_t stream_buffer_last_write_m7{};
    std::uint16_t stream_buffer_last_write_l7{};
    std::int16_t stream_buffer_last_write_ax0{};
    std::int16_t stream_buffer_last_write_ay0{};
    std::int16_t stream_buffer_last_write_ar{};
    std::uint64_t shared_ram_program_writes{};
    std::uint64_t shared_ram_data_writes{};
    std::uint16_t stream_buffer_min{};
    std::uint16_t stream_buffer_max{};
    std::uint16_t shared_ram_last_address{};
    std::uint16_t shared_ram_last_value{};
    std::uint16_t shared_ram_last_pc{};
    std::uint16_t local_ram_last_address{};
    std::uint16_t local_ram_last_value{};
    std::uint16_t local_ram_last_pc{};
    std::uint16_t sound_rom_last_address{};
    std::uint16_t sound_rom_last_value{};
    std::uint16_t sound_bank_last_value{};
    std::uint64_t boot_count{};
    std::uint64_t software_reboot_count{};
    std::uint16_t last_software_reboot_pc{};
    std::uint16_t last_software_reboot_value{};
    std::uint64_t syscontrol_writes{};
    std::uint16_t last_syscontrol_pc{};
    std::uint16_t last_syscontrol_value{};
    std::uint64_t sport_words{};
    std::uint64_t nonzero_samples{};
    std::uint16_t sample_peak{};
    std::int16_t last_sample{};
    std::uint16_t stream_ireg{};
    std::uint16_t stream_base{};
    std::uint16_t stream_length{};
    std::uint32_t stream_increment{};
    std::uint16_t stream_address{};
    std::uint16_t stopped_pc{};
    std::uint32_t stopped_opcode{};
    std::uint8_t stop_reason{};
    std::uint16_t sound_bank{};
    std::uint16_t input_latch{};
    std::uint16_t output_latch{};
    bool input_full{};
    bool output_full{};
    bool reset_asserted{};
    bool cpu_stalled{};
};

// Shared Midway DCS Audio 2K runtime. Board maps own how host commands and
// reset reach this device; ADSP instruction semantics stay in
// core/cpus/adsp2105.
class DcsAudio2K final : public multiarcaderecomp::dcs::HostInterface {
public:
    explicit DcsAudio2K(std::span<const std::uint8_t> sound_region);

    std::uint16_t host_status() const noexcept override;
    std::uint16_t host_data_read() noexcept override;
    void host_data_write(std::uint16_t data) noexcept override;
    // Board maps with separate reset and command ports use this entry point.
    void send_command(std::uint8_t command) noexcept;
    std::uint64_t consume_host_stall_cycles() noexcept override;
    void set_reset(bool released) noexcept;

    multiarcaderecomp::adsp2105::State& cpu() noexcept;
    const multiarcaderecomp::adsp2105::State& cpu() const noexcept;
    multiarcaderecomp::adsp2105::Bus cpu_bus() noexcept;
    bool program_word_was_written(std::uint16_t address) const noexcept;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> program_overlay() const;
    multiarcaderecomp::adsp2105::BlockExit run_until(
        std::uint64_t cycle_limit, bool interpreter_only = false,
        bool recompiler_only = false) noexcept;
    std::vector<std::int16_t> take_samples() noexcept;
    void advance_cycles(std::uint64_t cycles) noexcept;
    DcsAudio2KDiagnostics diagnostics() const noexcept;

private:
    static std::uint32_t program_read(void*, std::uint16_t) noexcept;
    static void program_write(void*, std::uint16_t, std::uint32_t) noexcept;
    static std::uint16_t data_read(void*, std::uint16_t) noexcept;
    static void data_write(void*, std::uint16_t, std::uint16_t) noexcept;
    static std::uint16_t io_read(void*, std::uint16_t) noexcept;
    static void io_write(void*, std::uint16_t, std::uint16_t) noexcept;
    static void sport_tx(void*, unsigned, std::uint32_t) noexcept;
    static std::uint32_t sport_rx(void*, unsigned) noexcept;
    static bool mutable_program(void*, std::uint16_t) noexcept;

    std::uint16_t read_data(std::uint16_t address) noexcept;
    void write_data(std::uint16_t address, std::uint16_t value) noexcept;
    std::uint32_t read_program(std::uint16_t address) const noexcept;
    void write_program(std::uint16_t address, std::uint32_t value) noexcept;
    void boot() noexcept;
    void acknowledge_input() noexcept;
    void acknowledge_output() noexcept;

    std::vector<std::uint8_t> m_sound_region;
    std::vector<std::uint32_t> m_internal_program;
    std::vector<std::uint32_t> m_external_program;
    std::array<std::uint64_t, 0x800> m_shared_ram_write_counts{};
    std::array<std::uint64_t, 0x800> m_shared_ram_nonzero_write_counts{};
    std::array<std::uint64_t, 0x200> m_local_ram_write_counts{};
    std::array<std::uint64_t, 0x200> m_local_ram_nonzero_write_counts{};
    std::vector<bool> m_program_written;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> m_boot_program_history;
    std::vector<std::uint16_t> m_local_ram;
    std::vector<std::int16_t> m_samples;
    multiarcaderecomp::adsp2105::State m_cpu{};
    std::uint16_t m_control[32]{};
    std::uint16_t m_sound_bank{};
    std::uint16_t m_input_latch{};
    std::uint16_t m_output_latch{};
    std::uint16_t m_progflags{};
    bool m_input_full{};
    bool m_output_full{};
    bool m_reset_asserted{true};
    bool m_sport1_enabled{};
    bool m_sport1_autobuffer{};
    bool m_dma_active{};
    bool m_cpu_stalled{};
    std::uint8_t m_dma_ireg{};
    std::uint16_t m_dma_length{};
    std::uint16_t m_dma_base{};
    std::uint32_t m_dma_increment{};
    std::uint16_t m_dma_first_address{};
    std::uint16_t m_dma_last_address{};
    std::uint16_t m_stopped_pc{};
    std::uint32_t m_stopped_opcode{};
    std::uint8_t m_stop_reason{};
    std::uint32_t m_dma_sample_count{};
    std::uint64_t m_dma_period_cycles{};
    std::uint64_t m_dma_cycle_phase{};
    std::uint64_t m_dma_activation_cycle{};
    std::uint64_t m_dma_activation_generation{};
    std::uint64_t m_sport_irq1_pulses{};
    std::uint32_t m_sport_irq1_pulse_remaining{};
    std::uint64_t m_sport_irq1_pulse_start_cycle{};
    std::uint64_t m_boot_count{};
    std::uint64_t m_software_reboot_count{};
    std::uint16_t m_last_software_reboot_pc{};
    std::uint16_t m_last_software_reboot_value{};
    std::uint64_t m_syscontrol_writes{};
    std::uint16_t m_last_syscontrol_pc{};
    std::uint16_t m_last_syscontrol_value{};
    std::uint64_t m_host_commands{};
    std::uint64_t m_overwritten_commands{};
    std::uint64_t m_host_reads{};
    std::uint64_t m_empty_output_reads{};
    std::uint64_t m_host_stall_cycles{};
    std::uint64_t m_host_stall_cycles_total{};
    std::uint64_t m_input_reads{};
    std::uint64_t m_output_writes{};
    std::uint64_t m_sound_rom_reads{};
    std::uint64_t m_sound_rom_nonzero_reads{};
    std::uint64_t m_sound_rom_ffff_reads{};
    std::uint64_t m_sound_bank_writes{};
    std::uint16_t m_sound_rom_last_address{};
    std::uint16_t m_sound_rom_last_value{};
    std::uint16_t m_sound_bank_last_value{};
    std::uint64_t m_local_ram_writes{};
    std::uint16_t m_local_ram_last_address{};
    std::uint16_t m_local_ram_last_value{};
    std::uint16_t m_local_ram_last_pc{};
    std::uint64_t m_shared_ram_writes{};
    std::uint64_t m_shared_ram_program_writes{};
    std::uint64_t m_shared_ram_data_writes{};
    std::uint16_t m_shared_ram_last_address{};
    std::uint16_t m_shared_ram_last_value{};
    std::uint16_t m_shared_ram_last_pc{};
    std::uint64_t m_sport_words{};
    std::uint16_t m_stream_last_write_address{};
    std::uint16_t m_stream_last_write_value{};
    std::uint16_t m_stream_last_write_pc{};
    std::uint32_t m_stream_last_write_opcode{};
    std::uint16_t m_stream_last_write_i7{};
    std::int16_t m_stream_last_write_m7{};
    std::uint16_t m_stream_last_write_l7{};
    std::int16_t m_stream_last_write_ax0{};
    std::int16_t m_stream_last_write_ay0{};
    std::int16_t m_stream_last_write_ar{};
    std::uint64_t m_nonzero_samples{};
    std::uint16_t m_sample_peak{};
    std::int16_t m_last_sample{};
};

} // namespace multiarcaderecomp::dcs
