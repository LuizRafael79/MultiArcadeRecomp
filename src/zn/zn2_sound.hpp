#pragma once

#include "zn_romset.hpp"
#include "z80.h"
#include "native_z80.h"
#include "chips/qsound.hpp"
#include <array>
#include <vector>

namespace mamerecomp::zn {

class Zn2SoundCpu final : private mamerecomp::z80::Z80Bus {
public:
    explicit Zn2SoundCpu(CapcomZn2Board &board,
                         const mamerecomp::z80::Z80Code *aot_code = nullptr,
                         mamerecomp::chips::QSoundMode qsound_mode = mamerecomp::chips::QSoundMode::Lle,
                         std::span<const std::uint16_t> qsound_firmware = {});
    void run_to_main_cycles(uint64_t psx_cycles);
    uint64_t next_main_event_cycle() const;
    const mamerecomp::z80::Z80 &cpu() const { return cpu_; }
    uint64_t audio_samples() const { return generated_samples_; }
    uint32_t audio_sample_rate() const { return 60'000'000u / 2u / 1248u; }
    mamerecomp::chips::QSoundMode qsound_mode() const { return qsound_.mode(); }
    uint64_t qsound_dsp_cycles() const { return qsound_.dsp_cycles(); }
    uint64_t qsound_dsp_instructions() const { return qsound_.dsp_instructions(); }
    uint16_t qsound_dsp_pc() const { return qsound_.dsp_pc(); }
    uint64_t qsound_sample_reads() const { return qsound_.sample_reads(); }
    uint64_t qsound_pio_reads() const { return qsound_.pio_reads(); }
    uint64_t qsound_pio_writes() const { return qsound_.pio_writes(); }
    uint64_t qsound_serial_words() const { return qsound_.serial_words(); }
    uint64_t qsound_serial_left_words() const { return qsound_.serial_left_words(); }
    uint64_t qsound_serial_right_words() const { return qsound_.serial_right_words(); }
    uint64_t qsound_pcm_frames() const { return qsound_.pcm_frames_produced(); }
    uint64_t qsound_nonzero_sample_reads() const { return qsound_.nonzero_sample_reads(); }
    uint64_t qsound_unmapped_sample_reads() const { return qsound_.unmapped_sample_reads(); }
    uint64_t qsound_mirrored_sample_reads() const { return qsound_.mirrored_sample_reads(); }
    uint64_t qsound_nonzero_serial_words() const { return qsound_.nonzero_serial_words(); }
    uint16_t qsound_serial_word_peak() const { return qsound_.serial_word_peak(); }
    uint64_t qsound_dsp_ock_edges() const { return qsound_.dsp_ock_rising_edges(); }
    uint64_t qsound_dsp_do_transitions() const { return qsound_.dsp_do_transitions(); }
    uint64_t qsound_dsp_ose_transitions() const { return qsound_.dsp_ose_transitions(); }
    uint64_t qsound_dsp_old_transitions() const { return qsound_.dsp_old_transitions(); }
    uint64_t qsound_dsp_serial_loads() const { return qsound_.dsp_serial_word_loads(); }
    uint64_t qsound_dsp_sdx_writes() const { return qsound_.dsp_sdx_writes(); }
    uint64_t qsound_dsp_nonzero_sdx_writes() const { return qsound_.dsp_nonzero_sdx_writes(); }
    uint64_t qsound_dsp_nonzero_serial_loads() const { return qsound_.dsp_nonzero_serial_loads(); }
    uint16_t qsound_dsp_last_sdx() const { return qsound_.dsp_last_sdx_value(); }
    uint16_t qsound_dsp_last_serial_loaded() const { return qsound_.dsp_last_serial_word_loaded(); }
    uint16_t qsound_dsp_last_sdx_pc() const { return qsound_.dsp_last_sdx_pc(); }
    uint16_t qsound_dsp_last_sdx_opcode() const { return qsound_.dsp_last_sdx_opcode(); }
    int64_t qsound_dsp_last_sdx_a0() const { return qsound_.dsp_last_sdx_a0(); }
    int64_t qsound_dsp_last_sdx_a1() const { return qsound_.dsp_last_sdx_a1(); }
    int16_t qsound_dsp_last_sdx_x() const { return qsound_.dsp_last_sdx_x(); }
    int32_t qsound_dsp_last_sdx_y() const { return qsound_.dsp_last_sdx_y(); }
    int32_t qsound_dsp_last_sdx_p() const { return qsound_.dsp_last_sdx_p(); }
    uint32_t audio_peak() const { return audio_peak_; }
    const std::array<uint32_t, 19> &voice_peaks() const { return voice_peaks_; }
    uint64_t profile_runtime_ns() const { return profile_runtime_ns_; }
    std::vector<int16_t> take_audio();
private:
    uint8_t Read(uint16_t address) override;
    void Write(uint16_t address, uint8_t value) override;
    uint8_t In(uint16_t port) override;
    void Out(uint16_t port, uint8_t value) override;
    void InstructionBoundary(uint64_t cycles) override;
    bool ChangesMapping(uint16_t address) override { return address == 0xd003; }
    uint32_t Mapping() override { return board_.sound_bank(); }
    void SetMapping(uint32_t mapping) override { board_.set_sound_bank(static_cast<uint8_t>(mapping)); }
    void sync_qsound(uint64_t sound_cycles);
    CapcomZn2Board &board_;
    mamerecomp::z80::Z80 cpu_;
    const mamerecomp::z80::Z80Code *aot_code_ = nullptr;
    mamerecomp::chips::QSound qsound_;
    std::vector<uint8_t> samples_;
    std::array<int16_t, 1024> qsound_stereo_{};
    std::vector<int16_t> audio_queue_;
    uint64_t generated_samples_ = 0;
    uint8_t pending_qsound_command_ = 0;
    bool qsound_command_pending_ = false;
    bool sound_nmi_delivered_ = false;
    uint64_t generated_dsp_clocks_ = 0;
    uint32_t audio_peak_ = 0;
    std::array<uint32_t, 19> voice_peaks_{};
    uint64_t next_irq_ = 32000;
    bool profile_timing_ = false;
    uint64_t profile_runtime_ns_ = 0;
};

} // namespace mamerecomp::zn
