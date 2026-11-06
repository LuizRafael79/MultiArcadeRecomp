#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mamerecomp::devices {

// Parallel Atmel AT28C16 (2 KiB x 8) with MAME-compatible write busy status.
class At28c16 {
public:
    static constexpr size_t kDataBytes = 0x800;
    static constexpr size_t kNvramBytes = 0x820;
    explicit At28c16(uint64_t clock_hz, std::span<const uint8_t> image = {});
    uint8_t read(size_t offset, uint64_t cycles);
    void write(size_t offset, uint8_t value, uint64_t cycles);
    void advance(uint64_t cycles);
    bool load(std::span<const uint8_t> image);
    std::span<const uint8_t> image() const { return cells_; }
private:
    std::array<uint8_t, kNvramBytes> cells_{};
    uint64_t write_delay_cycles_;
    uint64_t ready_cycle_ = 0;
    int last_write_ = -1;
};

} // namespace mamerecomp::devices
