// Namco C352 PCM device, adapted from MAME's BSD-3-Clause implementation.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mamerecomp::chips {

class C352 {
public:
    explicit C352(std::span<const uint8_t> rom);
    uint16_t read(uint16_t word_offset) const;
    void write(uint16_t word_offset, uint16_t data, uint16_t mem_mask = 0xffff);
    // Returns interleaved front-left/front-right PCM at clock/(divider*2).
    std::vector<int16_t> render(size_t frames);
    uint32_t sample_rate() const { return clock_hz_ / (divider_ * 2); }
    uint64_t samples_rendered() const { return samples_rendered_; }
    unsigned active_voices() const;

private:
    struct Voice {
        uint32_t pos = 0;
        uint32_t counter = 0;
        int16_t sample = 0;
        int16_t last_sample = 0;
        uint16_t vol_front = 0;
        uint16_t vol_rear = 0;
        std::array<uint8_t, 4> current_volume{};
        uint16_t frequency = 0;
        uint16_t flags = 0;
        uint16_t wave_bank = 0;
        uint16_t wave_start = 0;
        uint16_t wave_end = 0;
        uint16_t wave_loop = 0;
    };

    void fetch_sample(Voice& voice);
    static void ramp_volume(Voice& voice, unsigned channel, uint8_t target);
    std::vector<uint8_t> rom_;
    std::array<Voice, 32> voices_{};
    std::array<int16_t, 256> mulaw_{};
    uint32_t clock_hz_ = 25'401'600;
    uint16_t divider_ = 288;
    uint16_t random_ = 0x1234;
    uint16_t control_ = 0;
    uint64_t samples_rendered_ = 0;
};

} // namespace mamerecomp::chips
