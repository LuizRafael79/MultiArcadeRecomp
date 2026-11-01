#pragma once

#include "core/cpus/adsp2105/adsp2105.hpp"
#include "mk/tunit.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace mamerecomp::mk2 {

struct DcsDiagnostics {
    std::uint64_t host_commands{};
    std::uint64_t host_reads{};
    std::uint64_t input_reads{};
    std::uint64_t output_writes{};
    std::uint64_t boot_count{};
    std::uint64_t sport_words{};
    std::uint16_t sound_bank{};
    std::uint16_t input_latch{};
    std::uint16_t output_latch{};
    bool input_full{};
    bool output_full{};
    bool reset_asserted{};
    bool cpu_stalled{};
};

// DCS Audio 2K runtime for the MK2 T-Unit board. This is the board-side
// protocol/memory map; ADSP instruction semantics stay in core/cpus/adsp2105.
class Dcs2K final : public mamerecomp::mk::SoundHostInterface {
public:
    explicit Dcs2K(std::span<const std::uint8_t> sound_region);

    std::uint16_t host_status() const noexcept override;
    std::uint16_t host_data_read() noexcept override;
    void host_data_write(std::uint16_t data) noexcept override;
    void set_reset(bool released) noexcept;

    mamerecomp::adsp2105::State& cpu() noexcept;
    const mamerecomp::adsp2105::State& cpu() const noexcept;
    mamerecomp::adsp2105::Bus cpu_bus() noexcept;
    mamerecomp::adsp2105::BlockExit run_until(std::uint64_t cycle_limit) noexcept;
    std::vector<std::int16_t> take_samples() noexcept;
    void advance_cycles(std::uint64_t cycles) noexcept;
    DcsDiagnostics diagnostics() const noexcept;

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
    std::vector<bool> m_program_written;
    std::vector<std::uint16_t> m_local_ram;
    std::vector<std::int16_t> m_samples;
    mamerecomp::adsp2105::State m_cpu{};
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
    std::uint32_t m_dma_sample_count{};
    std::uint64_t m_dma_period_cycles{};
    std::uint64_t m_dma_cycle_phase{};
    std::uint64_t m_boot_count{};
    std::uint64_t m_host_commands{};
    std::uint64_t m_host_reads{};
    std::uint64_t m_input_reads{};
    std::uint64_t m_output_writes{};
    std::uint64_t m_sport_words{};
};

} // namespace mamerecomp::mk2
