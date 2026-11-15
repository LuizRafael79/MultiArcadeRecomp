#pragma once

#include "zn_romset.hpp"
#include "musashi_runtime.h"
#include "ymf271.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace multiarcaderecomp::zn {

// Raizing ZN-1's separate MC68000 audio CPU, MAME-defined address map and
// clock-synchronized YMF271. A nonzero audio peak still needs verification in
// a game state that actually starts a voice.
class Zn1SoundCpu {
public:
    Zn1SoundCpu(RaizingZn1Board &board, const RomRegions &roms);
    void run(unsigned cycle_budget);
    uint64_t cycles() const { return cycles_; }
    uint64_t instructions() const { return instructions_; }
    uint64_t ymf_writes() const { return ymf_writes_; }
    uint64_t audio_samples() const { return ymf_.generated_samples(); }
    uint32_t audio_peak() const { return ymf_.peak(); }
    uint32_t audio_sample_rate() const { return ymf_.sample_rate(); }
    unsigned active_ymf_slots() const { return ymf_.active_slots(); }
    std::vector<int16_t> take_audio() { return ymf_.take_audio(); }

private:
    static uint8_t read_cb(void *, uint32_t);
    static void write_cb(void *, uint32_t, uint8_t);
    static int irq_ack_cb(void *, unsigned);
    static void instruction_cb(void *, uint32_t);
    uint8_t read(uint32_t address);
    void write(uint32_t address, uint8_t value);
    void sync_ymf_to_cpu_cycles(uint64_t cpu_cycles);

    RaizingZn1Board &board_;
    const std::vector<uint8_t> &rom_;
    Ymf271 ymf_;
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(0x80000);
    std::vector<uint8_t> context_;
    multiarcaderecomp_m68k_bus bus_{};
    uint64_t cycles_ = 0;
    uint64_t run_base_cycles_ = 0;
    uint64_t ymf_master_cycles_ = 0;
    uint64_t instructions_ = 0;
    uint64_t ymf_writes_ = 0;
};

} // namespace multiarcaderecomp::zn
