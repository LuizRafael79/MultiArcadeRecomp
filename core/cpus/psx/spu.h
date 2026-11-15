// PSX SPU register, RAM, DMA and IRQ state shared by ZN and System 12.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace multiarcaderecomp::psx {

class Spu {
public:
    using Irq = std::function<void(bool)>;
    explicit Spu(Irq irq = {});
    uint32_t read(uint32_t byte_offset, unsigned size);
    void write(uint32_t byte_offset, unsigned size, uint32_t value);
    void dma_write(std::span<const uint32_t> words);
    std::vector<uint32_t> dma_read(size_t words);
    void advance(uint64_t cpu_cycles, uint64_t cpu_clock_hz);
    uint64_t cycles_until_sample(uint64_t cpu_clock_hz) const;
    bool irq_timing_active() const;
    std::vector<int16_t> take_audio();
    const std::vector<uint8_t> &ram() const { return ram_; }

private:
    uint16_t read_register(unsigned index) const;
    void write_register(unsigned index, uint16_t value);
    void write_ram_word(uint16_t value);
    uint16_t read_ram_word();
    void update_irq(bool active);
    void advance_voice_samples(uint64_t samples);
    void decode_voice_block(unsigned voice);

    std::array<uint16_t, 256> registers_{};
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(512 * 1024);
    std::array<uint32_t, 24> voice_address_{};
    std::array<uint32_t, 24> voice_fraction_{};
    std::array<uint8_t, 24> voice_sample_in_block_{};
    std::array<uint32_t, 24> voice_block_address_{};
    std::array<std::array<int16_t, 28>, 24> voice_decoded_{};
    std::array<int32_t, 24> voice_history_1_{};
    std::array<int32_t, 24> voice_history_2_{};
    std::array<float, 24> voice_envelope_{};
    std::array<float, 24> voice_envelope_delta_{};
    std::array<uint8_t, 24> voice_envelope_stage_{};
    std::vector<int16_t> audio_;
    uint32_t transfer_byte_address_ = 0;
    uint64_t elapsed_cpu_cycles_ = 0;
    uint64_t cycle_remainder_ = 0;
    bool irq_active_ = false;
    Irq irq_;
};

} // namespace multiarcaderecomp::psx
