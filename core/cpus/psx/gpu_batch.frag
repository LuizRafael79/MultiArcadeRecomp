#version 430 core

layout(location = 0) out uint oColor;
layout(binding = 0) uniform usampler2D uSource;
uniform int uVramHeight;
in vec3 vColor;
in vec2 vUV;
flat in uint vSetMask;
flat in uint vFlatColor;
flat in uint vGouraud;
flat in uint vFlags;
flat in uint vTexturePage;
flat in uint vClut;
flat in uint vGpuType;
flat in ivec4 vWindowState;

uint vram_word(int x, int y) {
    int pixel_mask = (uVramHeight * 1024) - 1;
    int index = int(uint(y * 1024 + x) & uint(pixel_mask));
    return texelFetch(uSource, ivec2(index & 1023, index >> 10), 0).r;
}

uint sample_texture(int u, int v) {
    uint tpage = vTexturePage;
    uint gpu_type = vGpuType;
    int depth = gpu_type == 2u ? int((tpage >> 7) & 3u)
                               : int((tpage >> 9) & 3u);
    if (depth == 3) return 0u;
    u &= vWindowState.z;
    v &= vWindowState.w;
    int ppw = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    int page_x = int(tpage & 15u) * 64;
    int page_y = gpu_type == 2u
        ? int(((tpage & 0x10u) << 4) | ((tpage & 0x800u) >> 2))
        : int(tpage & 0x60u) << 3;
    int base_x = page_x + vWindowState.x / ppw;
    int base_y = page_y + vWindowState.y;
    if (depth == 2) return vram_word(base_x + u, base_y + v);
    int tex_x = u / ppw;
    int tex_y = v;
    if (gpu_type != 2u && (tpage & 0x2000u) != 0u && depth == 0) {
        tex_x = ((u >> 2) & ~0x3c) + ((v << 2) & 0x3c);
        tex_y = (v & ~0xf) + ((u >> 4) & 0xf);
    } else if (gpu_type != 2u && (tpage & 0x2000u) != 0u && depth == 1) {
        tex_x = ((u >> 1) & ~0x78) + ((u << 2) & 0x40) + ((v << 3) & 0x38);
        tex_y = (v & ~0x7) + ((u >> 5) & 0x7);
    }
    uint packed_word = vram_word(base_x + tex_x, base_y + tex_y);
    uint index = depth == 0 ? ((packed_word >> uint((u & 3) * 4)) & 15u)
                            : ((packed_word >> uint((u & 1) * 8)) & 255u);
    int clut_x = int(vClut & 63u) * 16;
    int clut_y = int((vClut >> 6) & 1023u);
    return vram_word(clut_x + int(index), clut_y);
}

void main() {
    const bool textured = (vFlags & 2u) != 0u;
    const bool raw_texture = (vFlags & 4u) != 0u;
    uint color = vFlatColor;
    if (!textured && vGouraud != 0u) {
        color = (uint(clamp(vColor.r, 0.0, 255.0)) >> 3) |
                ((uint(clamp(vColor.g, 0.0, 255.0)) >> 3) << 5) |
                ((uint(clamp(vColor.b, 0.0, 255.0)) >> 3) << 10);
    }
    if (textured) {
        uint texel = sample_texture(int(vUV.x), int(vUV.y));
        if (texel == 0u) discard;
        if (raw_texture) {
            color = texel;
        } else {
            uvec3 tex = uvec3(texel & 31u, (texel >> 5) & 31u,
                              (texel >> 10) & 31u);
            uvec3 shade = uvec3(clamp(vColor, vec3(0.0), vec3(255.0)));
            uvec3 modulated = min(uvec3(31u), tex * shade / 128u);
            color = modulated.x | (modulated.y << 5) | (modulated.z << 10) |
                    (texel & 0x8000u);
        }
    }
    oColor = color | vSetMask;
}
