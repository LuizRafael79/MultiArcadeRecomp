#pragma once

#include "core/chips/dcs/audio_2k.hpp"
#include "core/chips/adc0844/adc0844.hpp"
#include "core/cpus/tms320c31/tms320c31.hpp"
#include "midway/vunit/core/vunit_video.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mamerecomp::vunit {

struct RuntimeReport {
    std::uint64_t main_cycles{};
    std::uint64_t main_instructions{};
    std::uint64_t audio_cycles{};
    std::uint32_t main_pc{};
    tms320c31::StopReason main_stop{};
    std::uint64_t dma_commands{};
};

// Midway V-Unit board runtime for Cruis'n USA. CPU execution is delegated to
// the shared TMS320C31 and ADSP-2105 backends; this class owns board wiring.
class Machine final {
public:
    Machine(std::span<const std::uint32_t> main_rom_words,
            std::span<const std::uint8_t> dcs_sound_region);
    void reset() noexcept;
    tms320c31::Exit run_cycles(std::uint64_t cycles) noexcept;
    void run_frame() noexcept;
    void set_inputs(std::uint16_t in0, std::uint16_t in1,
                    std::uint16_t dips) noexcept;
    void set_analog(std::uint8_t wheel, std::uint8_t accelerator,
                    std::uint8_t brake) noexcept;
    Video& video() noexcept { return m_video; }
    const Video& video() const noexcept { return m_video; }
    tms320c31::State& cpu() noexcept { return m_cpu_state; }
    const tms320c31::State& cpu() const noexcept { return m_cpu_state; }
    mamerecomp::dcs::DcsAudio2K& dcs() noexcept { return m_dcs; }
    const mamerecomp::dcs::DcsAudio2K& dcs() const noexcept { return m_dcs; }
    std::vector<std::int16_t> take_audio_samples() noexcept;
    std::vector<std::uint8_t> framebuffer_rgb() const;
    RuntimeReport report() const noexcept;
    void set_interpreter_only(bool enabled) noexcept;
    void set_interpreter_modes(bool c31_only, bool adsp_only) noexcept;
    const std::vector<std::uint32_t>& nvram() const noexcept { return m_nvram; }
    void load_nvram(std::span<const std::uint32_t> words) noexcept;

private:
    static std::uint32_t bus_read(void*, std::uint32_t address) noexcept;
    static void bus_write(void*, std::uint32_t address, std::uint32_t value) noexcept;
    static const std::uint32_t* map_code_page(
        void*, std::uint32_t page_word_address) noexcept;
    std::uint32_t read(std::uint32_t address) noexcept;
    void write(std::uint32_t address, std::uint32_t value) noexcept;
    std::uint32_t tms_control_read(std::uint32_t offset) noexcept;
    void tms_control_write(std::uint32_t offset, std::uint32_t value) noexcept;
    void wheel_board_write(std::uint32_t value) noexcept;
    void advance_audio_to(std::uint64_t main_cycle) noexcept;

    std::vector<std::uint32_t> m_main_rom;
    std::vector<std::uint32_t> m_low_ram;
    std::vector<std::uint32_t> m_extra_ram;
    std::vector<std::uint32_t> m_tms_internal_ram;
    std::vector<std::uint32_t> m_nvram;
    std::array<std::uint32_t, 0x80> m_tms_control{};
    std::array<std::uint16_t, 4> m_adc_inputs{0x80, 0x80, 0x80, 0x80};
    Video m_video;
    mamerecomp::chips::ADC0844 m_adc;
    mamerecomp::dcs::DcsAudio2K m_dcs;
    tms320c31::State m_cpu_state{};
    tms320c31::Interpreter m_cpu;
    std::uint16_t m_input0{0xffff};
    // IN1 base controls are active-low, but Cruis'n USA's motion status bits
    // (0xf000) are active-high custom inputs and default inactive in MAME.
    std::uint16_t m_input1{0x007f};
    // MAME crusnusa defaults: master link, upright, motion off, service off,
    // one coin counter, USA-8 coinage.
    std::uint16_t m_dips{0xf9fe};
    std::uint16_t m_control_data{};
    std::uint16_t m_comm_data{};
    std::uint8_t m_comm_flags{};
    std::uint32_t m_wheel_board_last{};
    std::uint32_t m_wheel_board_u8_latch{};
    std::uint8_t m_wheel_board_output{};
    std::string m_galil_input{":"};
    std::size_t m_galil_input_index{};
    std::array<char, 450> m_galil_output{};
    std::size_t m_galil_output_index{};
    std::uint16_t m_analog_result{};
    std::uint16_t m_cmos_protected{1};
    std::uint8_t m_adc_shift{24};
    std::uint64_t m_timer_origin[2]{};
    std::uint64_t m_timer_rate[2]{10000000, 10000000};
    std::uint64_t m_audio_phase{};
    std::uint64_t m_audio_synced_main_cycle{};
    std::uint64_t m_frame_remainder{};
    bool m_audio_reset_asserted{true};
    bool m_c31_interpreter_only{};
    bool m_adsp_interpreter_only{};
};

} // namespace mamerecomp::vunit
