#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mamerecomp::chips::sega3155196 {

inline constexpr int kFrameWidth = 320;
inline constexpr int kFrameHeight = 224;

// Rasterize the System 16B sprite ASIC output into palette/priority words.
// The output uses 0xffff for transparent pixels. sprite_ram is mutable because
// the MAME device model writes running address/zoom scratch values back into
// descriptors. This models that board-visible RAM behavior without assuming
// how the physical custom IC implements it internally.
std::vector<std::uint16_t> rasterize_sprites(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks = {}, bool flip = false);

} // namespace mamerecomp::chips::sega3155196
