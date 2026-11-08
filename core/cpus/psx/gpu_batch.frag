#version 430 core

layout(location = 0) out uint oColor;
layout(binding = 0) uniform usampler2D uVram;
layout(binding = 1) uniform usampler2DArray uTextureCache;
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
flat in uint vTextureLayer;

uint vram_word(int x, int y) {
    int pixel_mask = (uVramHeight * 1024) - 1;
    int index = int(uint(y * 1024 + x) & uint(pixel_mask));
    return texelFetch(uVram, ivec2(index & 1023, index >> 10), 0).r;
}

uint blend555(uint back, uint front, uint mode) {
    uvec3 b = uvec3(back & 31u, (back >> 5) & 31u, (back >> 10) & 31u);
    uvec3 f = uvec3(front & 31u, (front >> 5) & 31u, (front >> 10) & 31u);
    uvec3 c;
    if (mode == 0u) c = (b + f) >> 1;
    else if (mode == 1u) c = min(uvec3(31u), b + f);
    else if (mode == 2u) c = uvec3(max(ivec3(b) - ivec3(f), ivec3(0)));
    else c = min(uvec3(31u), b + (f >> 2));
    return c.x | (c.y << 5) | (c.z << 10) | (front & 0x8000u);
}

void main() {
    const bool textured = (vFlags & 2u) != 0u;
    const bool raw_texture = (vFlags & 4u) != 0u;
    uint color = vFlatColor;
    uint texel = 0u;
    if (!textured && vGouraud != 0u) {
        color = (uint(clamp(vColor.r, 0.0, 255.0)) >> 3) |
                ((uint(clamp(vColor.g, 0.0, 255.0)) >> 3) << 5) |
                ((uint(clamp(vColor.b, 0.0, 255.0)) >> 3) << 10);
    }
    if (textured) {
        const int u = int(roundEven(vUV.x)) & vWindowState.z;
        const int v = int(roundEven(vUV.y)) & vWindowState.w;
        texel = texelFetch(uTextureCache,
                           ivec3(u, v, int(vTextureLayer)), 0).r;
        const int depth = vGpuType == 2u
            ? int((vTexturePage >> 7) & 3u)
            : int((vTexturePage >> 9) & 3u);
        if (depth < 2) {
            const int clut_x = int(vClut & 63u) * 16;
            const int clut_y = int((vClut >> 6) & 1023u);
            texel = vram_word(clut_x + int(texel), clut_y);
        }
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
    const ivec2 pixel = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));
    uint old = 0u;
    if ((vFlags & (8u | 32u)) != 0u)
        old = vram_word(pixel.x, pixel.y);
    if ((vFlags & 32u) != 0u && (old & 0x8000u) != 0u) discard;
    if ((vFlags & 8u) != 0u && (!textured || (texel & 0x8000u) != 0u)) {
        const uint abr = vGpuType == 2u
            ? ((vTexturePage >> 5) & 3u) : ((vTexturePage >> 7) & 3u);
        color = blend555(old, color, abr);
    }
    oColor = color | vSetMask;
}
