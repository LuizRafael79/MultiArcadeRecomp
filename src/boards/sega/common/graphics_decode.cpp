#include "boards/sega/common/graphics_decode.hpp"

#include <stdexcept>

namespace multiarcaderecomp {
std::vector<std::uint8_t> decode_sega_tiles_3bpp(std::span<const std::uint8_t> region) {
    if (region.empty() || region.size() % 3 != 0)
        throw std::invalid_argument("Sega 3bpp tile region must contain three equal bitplanes");

    const std::size_t plane_bytes = region.size() / 3;
    const std::size_t tiles_per_plane = (plane_bytes * 8) / 64;
    if (tiles_per_plane == 0 || (plane_bytes * 8) % 64 != 0)
        throw std::invalid_argument("Sega 3bpp tile plane size is not a whole number of 8x8 tiles");

    std::vector<std::uint8_t> pixels(tiles_per_plane * 64, 0);
    const std::size_t plane_offsets[] = {plane_bytes * 16, plane_bytes * 8, 0};
    for (std::size_t tile = 0; tile < tiles_per_plane; ++tile) {
        const std::size_t tile_bit_offset = tile * 64;
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                std::uint8_t pen = 0;
                for (unsigned plane = 0; plane < 3; ++plane) {
                    const std::size_t bit = plane_offsets[plane] + tile_bit_offset + y * 8 + x;
                    const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (bit & 7u));
                    if (region[bit >> 3] & mask) pen |= static_cast<std::uint8_t>(1u << (2u - plane));
                }
                pixels[tile * 64 + y * 8 + x] = pen;
            }
        }
    }
    return pixels;
}

} // namespace multiarcaderecomp
