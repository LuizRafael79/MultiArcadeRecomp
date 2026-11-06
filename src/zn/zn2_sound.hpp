#pragma once

#include "zn_romset.hpp"
#include "z80.h"
#include "native_z80.h"
#include <qsound.h>
#include <array>
#include <vector>

namespace mamerecomp::zn {

class Zn2SoundCpu final : private mamerecomp::z80::Z80Bus {
public:
    explicit Zn2SoundCpu(CapcomZn2Board &board,
                         const mamerecomp::z80::Z80Code *aot_code = nullptr);
    void run_to_main_cycles(uint64_t psx_cycles);
    uint64_t next_main_event_cycle() const;
    const mamerecomp::z80::Z80 &cpu() const { return cpu_; }
    uint64_t audio_samples() const { return generated_samples_; }
    uint32_t audio_sample_rate() const { return 60'000'000u / 2u / 1248u; }
    uint32_t audio_peak() const { return audio_peak_; }
    uint64_t profile_runtime_ns() const { return profile_runtime_ns_; }
    std::vector<int16_t> take_audio();
private:
    uint8_t Read(uint16_t address) override;
    void Write(uint16_t address, uint8_t value) override;
    uint8_t In(uint16_t port) override;
    void Out(uint16_t port, uint8_t value) override;
    bool ChangesMapping(uint16_t address) override { return address == 0xd003; }
    uint32_t Mapping() override { return board_.sound_bank(); }
    void SetMapping(uint32_t mapping) override { board_.set_sound_bank(static_cast<uint8_t>(mapping)); }
    void sync_qsound(uint64_t sound_cycles);
    CapcomZn2Board &board_;
    mamerecomp::z80::Z80 cpu_;
    const mamerecomp::z80::Z80Code *aot_code_ = nullptr;
    qsound_chip qsound_{};
    std::vector<uint8_t> samples_;
    std::array<int16_t, 512> qsound_left_{}, qsound_right_{};
    std::vector<int16_t> audio_queue_;
    uint64_t generated_samples_ = 0;
    uint32_t audio_peak_ = 0;
    uint64_t next_irq_ = 32000;
    bool profile_timing_ = false;
    uint64_t profile_runtime_ns_ = 0;
};

} // namespace mamerecomp::zn
