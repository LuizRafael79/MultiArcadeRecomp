#include "boards/sega/common/video_output.hpp"

#include <fstream>
#include <stdexcept>
#include <cstring>

namespace multiarcaderecomp {
namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint8_t palette_channel(unsigned value, bool effect_enabled, bool highlight) {
    static constexpr std::uint8_t normal[32] = {
        0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 91, 99, 107, 115, 123,
        132, 140, 148, 156, 164, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255};
    static constexpr std::uint8_t effect[2][32] = {
        {0, 7, 13, 20, 26, 32, 39, 45, 52, 58, 64, 71, 77, 84, 90, 97,
         103, 110, 116, 123, 129, 136, 142, 149, 155, 161, 168, 174, 181, 187, 193, 200},
        {55, 62, 68, 74, 81, 87, 94, 100, 106, 113, 119, 126, 132, 139, 145, 152,
         158, 165, 171, 178, 184, 191, 197, 203, 210, 216, 223, 229, 235, 242, 248, 255}};
    return effect_enabled ? effect[highlight ? 1 : 0][value & 0x1fU] : normal[value & 0x1fU];
}

} // namespace

std::vector<std::uint8_t> sega_frame_rgb(const SegaVideoFrame& frame,
    std::span<const std::uint8_t> palette_ram) {
    SegaFrameRgbConverter converter;
    const auto rgb = converter.convert(frame, palette_ram);
    return {rgb.begin(), rgb.end()};
}

std::span<const std::uint8_t> SegaFrameRgbConverter::convert(const SegaVideoFrame& frame,
    std::span<const std::uint8_t> palette_ram) {
    constexpr auto pixel_count = static_cast<std::size_t>(SegaVideoFrame::width * SegaVideoFrame::height);
    if (frame.palette_indices.size() != pixel_count || frame.shadow_pixels.size() != pixel_count
        || palette_ram.size() < 0x1000) throw std::invalid_argument("invalid Sega frame or palette RAM");
    if (!m_palette_valid || std::memcmp(m_palette_snapshot.data(), palette_ram.data(), kPaletteBytes) != 0) {
        std::memcpy(m_palette_snapshot.data(), palette_ram.data(), kPaletteBytes);
        for (std::size_t index = 0; index < kPaletteEntries; ++index) {
            const unsigned word = read_be16(palette_ram, index * 2);
            const unsigned r = ((word >> 12) & 1U) | ((word << 1) & 0x1eU);
            const unsigned g = ((word >> 13) & 1U) | ((word >> 3) & 0x1eU);
            const unsigned b = ((word >> 14) & 1U) | ((word >> 7) & 0x1eU);
            const bool highlight = (word & 0x8000U) != 0;
            for (unsigned effect = 0; effect < 2; ++effect) {
                m_colors[index][effect] = (static_cast<std::uint32_t>(palette_channel(r, effect != 0, highlight)) << 16)
                    | (static_cast<std::uint32_t>(palette_channel(g, effect != 0, highlight)) << 8)
                    | palette_channel(b, effect != 0, highlight);
            }
        }
        m_palette_valid = true;
    }
    if (m_rgb.size() != pixel_count * 3) m_rgb.resize(pixel_count * 3);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto index = frame.palette_indices[pixel];
        if (index >= 0x1000U && index < 0x1040U) {
            const auto color = frame.vdp_palette_rgb[index - 0x1000U];
            m_rgb[pixel * 3] = static_cast<std::uint8_t>(color >> 16);
            m_rgb[pixel * 3 + 1] = static_cast<std::uint8_t>(color >> 8);
            m_rgb[pixel * 3 + 2] = static_cast<std::uint8_t>(color);
            continue;
        }
        const bool shadow_palette = index >= 0x800U && index < 0x1000U;
        const auto palette_index = shadow_palette ? static_cast<std::uint16_t>(index - 0x800U) : index;
        if (palette_index >= palette_ram.size() / 2 || palette_index >= kPaletteEntries) {
            m_rgb[pixel * 3] = 0;
            m_rgb[pixel * 3 + 1] = 0;
            m_rgb[pixel * 3 + 2] = 0;
            continue;
        }
        const bool effect = frame.shadow_pixels[pixel] != 0 || shadow_palette;
        const auto color = m_colors[palette_index][effect ? 1U : 0U];
        m_rgb[pixel * 3] = static_cast<std::uint8_t>(color >> 16);
        m_rgb[pixel * 3 + 1] = static_cast<std::uint8_t>(color >> 8);
        m_rgb[pixel * 3 + 2] = static_cast<std::uint8_t>(color);
    }
    return m_rgb;
}

void write_sega_frame_ppm(const SegaVideoFrame& frame, std::span<const std::uint8_t> palette_ram,
    const std::filesystem::path& path) {
    const auto rgb = sega_frame_rgb(frame, palette_ram);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("could not create frame image: " + path.string());
    out << "P6\n" << SegaVideoFrame::width << ' ' << SegaVideoFrame::height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!out) throw std::runtime_error("failed while writing frame image: " + path.string());
}

} // namespace multiarcaderecomp
