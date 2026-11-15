#include "at28c16.hpp"

#include <algorithm>

namespace multiarcaderecomp::devices {

At28c16::At28c16(uint64_t clock_hz, std::span<const uint8_t> image)
    : write_delay_cycles_(std::max<uint64_t>(1, clock_hz / 5000))
{
    cells_.fill(0xff);
    if (!image.empty()) load(image);
}

bool At28c16::load(std::span<const uint8_t> image)
{
    if (image.size() != kDataBytes && image.size() != kNvramBytes) return false;
    cells_.fill(0xff);
    std::copy(image.begin(), image.end(), cells_.begin());
    last_write_ = -1;
    ready_cycle_ = 0;
    return true;
}

void At28c16::advance(uint64_t cycles)
{
    if (last_write_ >= 0 && cycles >= ready_cycle_) last_write_ = -1;
}

uint8_t At28c16::read(size_t offset, uint64_t cycles)
{
    advance(cycles);
    if (last_write_ >= 0) return static_cast<uint8_t>(last_write_ ^ 0x80);
    return offset < kDataBytes ? cells_[offset] : 0xff;
}

void At28c16::write(size_t offset, uint8_t value, uint64_t cycles)
{
    advance(cycles);
    if (last_write_ >= 0 || offset >= kDataBytes || cells_[offset] == value) return;
    cells_[offset] = value;
    last_write_ = value;
    ready_cycle_ = cycles + write_delay_cycles_;
}

} // namespace multiarcaderecomp::devices
