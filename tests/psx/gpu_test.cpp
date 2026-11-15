#include "gpu.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
void gp0(multiarcaderecomp::psx::Gpu &gpu, uint32_t value)
{
    gpu.write_data(value);
}

uint16_t pixel(const std::vector<uint8_t> &vram, unsigned x, unsigned y)
{
    const size_t offset = (size_t(y) * 1024 + x) * 2;
    return uint16_t(vram[offset] | (uint16_t(vram[offset + 1]) << 8));
}
}

int main()
{
    std::vector<uint8_t> vram(1024 * 512 * 2);
    multiarcaderecomp::psx::Gpu gpu(vram);

    // Upload a 2x2 raw 15bpp texture at the top left of VRAM.
    gp0(gpu, 0xa0000000);
    gp0(gpu, 0);
    gp0(gpu, (2u << 16) | 2u);
    gp0(gpu, 0x7fff7fffu);
    gp0(gpu, 0x7fff7fffu);

    // Set 15bpp texture depth, then draw a variable-size raw textured rect.
    gp0(gpu, 0xe1000100);
    gp0(gpu, 0x65000000);
    gp0(gpu, (10u << 16) | 10u);
    gp0(gpu, 0);
    gp0(gpu, (2u << 16) | 2u);

    assert(pixel(vram, 10, 10) == 0x7fff);
    assert(pixel(vram, 11, 11) == 0x7fff);
    assert(pixel(vram, 12, 10) == 0);

    // GP0(80h) consumes four words and copies the requested VRAM rectangle.
    gp0(gpu, 0x80000000);
    gp0(gpu, (10u << 16) | 10u);
    gp0(gpu, (30u << 16) | 30u);
    gp0(gpu, (2u << 16) | 2u);
    assert(pixel(vram, 30, 30) == 0x7fff);
    assert(pixel(vram, 31, 31) == 0x7fff);
    assert(pixel(vram, 32, 30) == 0);

    // GP0 Gouraud triangle uses the three vertex colors across the primitive.
    gp0(gpu, 0x300000ff);
    gp0(gpu, 20u | (20u << 16));
    gp0(gpu, 0x0000ff00);
    gp0(gpu, 40u | (20u << 16));
    gp0(gpu, 0x00ff0000);
    gp0(gpu, 20u | (40u << 16));
    assert(pixel(vram, 22, 22) != pixel(vram, 32, 22));

    // Textured polygon scan conversion must choose the lesser-X edge per
    // scanline regardless of vertex winding, as MAME's TEXTUREFILL does.
    const auto draw_textured_triangle = [&](bool reverse) {
        gp0(gpu, 0x25000000); // raw textured triangle
        if (!reverse) {
            gp0(gpu, 50u | (50u << 16)); gp0(gpu, 0);
            gp0(gpu, 52u | (50u << 16)); gp0(gpu, 0x100u << 16);
            gp0(gpu, 50u | (52u << 16)); gp0(gpu, 1u | (1u << 8));
        } else {
            gp0(gpu, 50u | (50u << 16)); gp0(gpu, 0);
            gp0(gpu, 50u | (52u << 16)); gp0(gpu, 0x100u << 16);
            gp0(gpu, 52u | (50u << 16)); gp0(gpu, 1u | (1u << 8));
        }
    };
    draw_textured_triangle(false);
    assert(pixel(vram, 50, 51) == 0x7fff);
    gp0(gpu, 0x02000000); gp0(gpu, 50u | (50u << 16));
    gp0(gpu, (3u << 16) | 3u);
    draw_textured_triangle(true);
    assert(pixel(vram, 50, 51) == 0x7fff);

    // MAME rejects polygons whose signed-11-bit edge crosses more than 1023
    // pixels; they must not draw a spurious line across the visible area.
    gp0(gpu, 0x200000ff);
    gp0(gpu, 0u | (100u << 16));
    gp0(gpu, 1020u | (100u << 16));
    gp0(gpu, 0x7f6u | (110u << 16)); // x = -10 after sign extension
    assert(pixel(vram, 500, 101) == 0);

    // Texture window origin is applied to the VRAM page in packed-word units
    // (GP0 E2 x=8 means +2 words for 4bpp), not added to the UV texel index.
    gp0(gpu, 0xa0000000); gp0(gpu, 2u); gp0(gpu, 0x00010001); gp0(gpu, 1);
    gp0(gpu, 0xa0000000); gp0(gpu, 1u << 16); gp0(gpu, 0x00010002);
    gp0(gpu, 0x03e00000); // CLUT index 1 is green at (1,1)
    gp0(gpu, 0xe1000000); // 4bpp texture page
    gp0(gpu, 0xe2000400); // texture window origin U=8, no mask
    gp0(gpu, 0x65000000); // raw, variable-size textured rectangle
    gp0(gpu, 80u | (80u << 16));
    gp0(gpu, 0x00400000); // CLUT row 1, UV (0,0)
    gp0(gpu, 0x00010001);
    assert(pixel(vram, 80, 80) == 0x03e0);
}
