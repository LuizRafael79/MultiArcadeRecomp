#pragma once

#include "boards/sega/common/video_frame.hpp"

#include <cstdint>
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace mamerecomp {

std::vector<std::uint8_t> sega_frame_rgb(const SegaVideoFrame& frame,
    std::span<const std::uint8_t> palette_ram);

class SegaFrameRgbConverter {
public:
    std::span<const std::uint8_t> convert(const SegaVideoFrame& frame,
        std::span<const std::uint8_t> palette_ram);

private:
    static constexpr std::size_t kPaletteBytes = 0x1000;
    static constexpr std::size_t kPaletteEntries = kPaletteBytes / 2;
    std::vector<std::uint8_t> m_rgb;
    std::array<std::uint8_t, kPaletteBytes> m_palette_snapshot{};
    std::array<std::array<std::uint32_t, 2>, kPaletteEntries> m_colors{};
    bool m_palette_valid = false;
};
void write_sega_frame_ppm(const SegaVideoFrame& frame,
    std::span<const std::uint8_t> palette_ram, const std::filesystem::path& path);

} // namespace mamerecomp
