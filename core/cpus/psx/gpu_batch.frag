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
        const int u = int(vUV.x) & vWindowState.z;
        const int v = int(vUV.y) & vWindowState.w;
        uint texel = texelFetch(uTextureCache,
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
    oColor = color | vSetMask;
}
