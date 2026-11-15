#include "chips/sega_315_5012/sega_315_5012.hpp"

namespace mamerecomp::chips::sega3155012 {
namespace {
constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 224;

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}
}

Output rasterize(const sega3155011::Comparator& line_comparator,
    std::span<const std::uint8_t> sprite_rom, std::span<const std::uint8_t> sprite_ram, State state) {
    Output output;
    output.pixels.assign(kWidth * kHeight, kTransparentPixel);
    if (sprite_rom.empty() || sprite_rom.size() % 0x10000 != 0 || sprite_ram.size() < 0x800) return output;
    const unsigned banks = static_cast<unsigned>(sprite_rom.size() / 0x10000);
    const auto sprite_word = [&](unsigned bank, unsigned address) {
        ++output.stats.words_read;
        const std::size_t offset = static_cast<std::size_t>(bank) * 0x10000 + (address & 0x7fffU) * 2;
        return static_cast<std::uint16_t>((sprite_rom[offset] << 8) | sprite_rom[offset + 1]);
    };

    // Eight big-endian words per descriptor; word 7 is renderer scratch in MAME.
    for (std::size_t descriptor = 0; descriptor + 16 <= sprite_ram.size(); descriptor += 16) {
        const unsigned bottom_raw = read_be16(sprite_ram, descriptor) >> 8;
        if (bottom_raw > 0xf0) break;
        const unsigned top_raw = read_be16(sprite_ram, descriptor) & 0xffU;
        int xpos = read_be16(sprite_ram, descriptor + 2) & 0x1ffU;
        const int pitch = static_cast<std::int16_t>(read_be16(sprite_ram, descriptor + 4));
        std::uint16_t address = read_be16(sprite_ram, descriptor + 6);
        const std::uint16_t attributes = read_be16(sprite_ram, descriptor + 8);
        const unsigned bank = (attributes >> 4) & 7U;
        if (top_raw >= bottom_raw || bank >= banks) continue;
        const unsigned color_priority = (((attributes >> 8) & 0x3fU) << 4) | ((attributes & 3U) << 10);
        const auto line_window = line_comparator.compare(top_raw, bottom_raw, state.flip);
        if (!line_window.valid) continue;
        const int top = line_window.first_line;
        const int bottom = line_window.last_line;
        int xdelta = 1;
        int xorigin = 189;
        if (state.flip) {
            xpos = 320 - xpos;
            xdelta = -1;
            xorigin = -189;
        }
        for (int y = top; y < bottom; ++y) {
            address = static_cast<std::uint16_t>(address + pitch);
            const int screen_y = y + 1;
            if (screen_y < 0 || screen_y >= static_cast<int>(kHeight)
                || !line_comparator.active(line_window, y)) continue;
            const bool reverse = (address & 0x8000U) != 0;
            std::uint16_t word_address = reverse ? static_cast<std::uint16_t>(address + 1) : static_cast<std::uint16_t>(address - 1);
            int x = xpos;
            for (; ((xpos - x) & 0x1ff) != 1; ) {
                const std::uint16_t packed = sprite_word(bank, reverse ? --word_address : ++word_address);
                int last_pen = 0;
                for (unsigned nibble = 0; nibble < 4; ++nibble) {
                    ++output.stats.pen_lookups;
                    const unsigned shift = reverse ? nibble * 4 : 12 - nibble * 4;
                    const unsigned pen = (packed >> shift) & 0xfU;
                    last_pen = static_cast<int>(pen);
                    const int screen_x = x - xorigin;
                    if (pen != 0 && pen != 15 && screen_x >= 0 && screen_x < static_cast<int>(kWidth)) {
                        const std::size_t pixel = static_cast<std::size_t>(screen_y) * kWidth + screen_x;
                        output.pixels[pixel] = static_cast<std::uint16_t>(color_priority | pen);
                        ++output.stats.pixels_rasterized;
                    }
                    x += xdelta;
                }
                if (last_pen == 15) break;
            }
        }
    }
    return output;
}

} // namespace mamerecomp::chips::sega3155012
