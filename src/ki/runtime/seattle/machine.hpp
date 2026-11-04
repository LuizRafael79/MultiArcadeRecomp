#pragma once

#include <cstdint>
#include <span>

namespace mamerecomp::ki::seattle {

inline constexpr int frame_width = 320;
inline constexpr int frame_height = 240;
inline constexpr double frame_rate = 58.9634;

void power_on() noexcept;
void run_frame() noexcept;
void set_inputs(std::uint32_t p1, std::uint32_t p2,
                std::uint32_t dips = 0xffffffffu) noexcept;
std::uint64_t frame_number() noexcept;
void copy_rgb(std::span<std::uint8_t> output) noexcept;

} // namespace mamerecomp::ki::seattle
