#include "sega_315_5196.hpp"

namespace mamerecomp::chips::sega3155196 {
namespace {
std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}
}

std::vector<std::uint16_t> rasterize_sprites(std::span<const std::uint8_t> sprite_rom,
    std::span<std::uint8_t> sprite_ram, std::span<const std::uint8_t> sprite_banks, bool flip) {
    constexpr std::size_t descriptor_bytes = 16;
    constexpr std::size_t sprite_bank_bytes = 0x20000;
    // The System 16B sprite device's default local origin is (184, 0), or
    // (-184, 0) when flipped. Convert descriptor coordinates into this
    // renderer's visible-frame coordinate space before clipping/writing.
    constexpr int sprite_origin_x = 184;
    if (sprite_ram.size() < descriptor_bytes || sprite_rom.empty() || sprite_rom.size() % sprite_bank_bytes != 0)
        return {};
    const std::size_t bank_count = sprite_rom.size() / sprite_bank_bytes;
    std::vector<std::uint16_t> sprite_pixels(
        static_cast<std::size_t>(kFrameWidth * kFrameHeight), 0xffff);
    for (std::size_t descriptor = 0; descriptor + descriptor_bytes <= sprite_ram.size(); descriptor += descriptor_bytes) {
        const std::uint16_t bottom_top = read_be16(sprite_ram, descriptor);
        int bottom = bottom_top >> 8;
        int top = bottom_top & 0xffU;
        if (read_be16(sprite_ram, descriptor + 4) & 0x8000U) break; // end of list
        const std::uint16_t control_pitch = read_be16(sprite_ram, descriptor + 4);
        const bool hidden = (control_pitch & 0x4000U) != 0;
        const bool hflip = (control_pitch & 0x0100U) != 0;
        const int pitch = static_cast<std::int8_t>(control_pitch & 0xffU);
        const std::uint16_t address_start = read_be16(sprite_ram, descriptor + 6);
        const std::uint16_t bank_color_priority = read_be16(sprite_ram, descriptor + 8);
        const unsigned logical_bank = (bank_color_priority >> 8) & 0xfU;
        const unsigned bank = sprite_banks.empty() ? logical_bank
            : (logical_bank < sprite_banks.size() ? sprite_banks[logical_bank] : 0xffU);
        const unsigned priority_color = ((bank_color_priority & 0xffU) << 4)
            | (((read_be16(sprite_ram, descriptor + 2) >> 9) & 0xfU) << 12);
        sprite_ram[descriptor + 14] = static_cast<std::uint8_t>(address_start >> 8);
        sprite_ram[descriptor + 15] = static_cast<std::uint8_t>(address_start);
        unsigned zoom = read_be16(sprite_ram, descriptor + 10);
        const unsigned vzoom = (zoom >> 5) & 0x1fU;
        const unsigned hzoom = zoom & 0x1fU;
        if (hidden || top >= bottom || bank == 0xffU) continue;
        const std::size_t actual_bank = bank % bank_count;
        if (flip) {
            const int old_top = top;
            top = 224 - bottom;
            bottom = 224 - old_top;
        }
        int xpos = read_be16(sprite_ram, descriptor + 2) & 0x1ffU;
        const int xdelta = flip ? -1 : 1;
        if (flip) xpos = 320 - xpos;
        std::uint16_t address = address_start;
        zoom &= 0x03ffU;
        std::uint16_t zoom_accum = static_cast<std::uint16_t>(zoom);
        sprite_ram[descriptor + 10] = static_cast<std::uint8_t>(zoom_accum >> 8);
        sprite_ram[descriptor + 11] = static_cast<std::uint8_t>(zoom_accum);
        for (int y = top; y < bottom; ++y) {
            address = static_cast<std::uint16_t>(address + pitch);
            zoom_accum = static_cast<std::uint16_t>(zoom_accum + (vzoom << 10));
            if (zoom_accum & 0x8000U) {
                address = static_cast<std::uint16_t>(address + pitch);
                zoom_accum &= 0x7fffU;
            }
            sprite_ram[descriptor + 10] = static_cast<std::uint8_t>(zoom_accum >> 8);
            sprite_ram[descriptor + 11] = static_cast<std::uint8_t>(zoom_accum);
            if (y < 0 || y >= kFrameHeight) continue;
        const bool reverse = hflip;
            int x = xpos;
            unsigned xacc = 4U * hzoom;
            std::uint16_t word_address = reverse ? static_cast<std::uint16_t>(address + 1)
                : static_cast<std::uint16_t>(address - 1);
            for (unsigned words = 0; words < 0x8000U; ++words) {
                word_address = static_cast<std::uint16_t>(word_address + (reverse ? -1 : 1));
                // MAME's 315-5196 indexes a bank as 0x10000 16-bit words
                // (0x20000 bytes). The descriptor address is a full 16-bit
                // word address; masking it to 0x7fff incorrectly folded the
                // upper half of every sprite bank onto its lower half.
                const std::size_t source_offset = actual_bank * sprite_bank_bytes
                    + static_cast<std::size_t>(word_address) * 2U;
                if (source_offset + 1 >= sprite_rom.size()) break;
                const std::uint16_t packed = static_cast<std::uint16_t>((sprite_rom[source_offset] << 8)
                    | sprite_rom[source_offset + 1]);
                unsigned last_pen = 0;
                for (unsigned pixel_in_word = 0; pixel_in_word < 4; ++pixel_in_word) {
                    const unsigned shift = reverse ? pixel_in_word * 4U : 12U - pixel_in_word * 4U;
                    const unsigned pen = (packed >> shift) & 0xfU;
                    last_pen = pen;
                    xacc = (xacc & 0x3fU) + hzoom;
                    if (xacc < 0x40U) {
                        const int screen_x = x - (flip ? -sprite_origin_x : sprite_origin_x);
                        if (pen != 0 && pen != 15 && screen_x >= 0 && screen_x < kFrameWidth) {
                            const std::size_t pixel = static_cast<std::size_t>(y) * kFrameWidth + screen_x;
                            sprite_pixels[pixel] = static_cast<std::uint16_t>(priority_color | pen);
                        }
                        x += xdelta;
                    }
                }
                if (last_pen == 15) break;
                if (((xpos - x) & 0x1ff) == 1) break;
            }
            // MAME's System 16B sprite-device model writes the running address
            // to descriptor word 7 as scratch. Preserve that modeled RAM side effect;
            // this is not a claim about the physical IC's internal implementation.
            sprite_ram[descriptor + 14] = static_cast<std::uint8_t>(word_address >> 8);
            sprite_ram[descriptor + 15] = static_cast<std::uint8_t>(word_address);
        }
    }

    return sprite_pixels;
}


} // namespace mamerecomp::chips::sega3155196
