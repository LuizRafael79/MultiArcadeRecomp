#version 430 core
struct DrawVertex {
    ivec4 xyuv;
    uint color;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(std430, binding = 0) readonly buffer CommandBuffer {
    uvec4 header;
    ivec4 draw_area;
    ivec4 offset_window;
    ivec4 window_flip;
    uvec4 texture;
    DrawVertex vertices[4];
    ivec4 bounds;
} command;
layout(binding = 0) uniform usampler2D uSource;
layout(location = 0) out uint oColor;

uniform int uVramHeight;
in vec2 vUV;
in vec3 vColor;

uint vram_word(int x, int y) {
    int pixel_mask = (uVramHeight * 1024) - 1;
    int index = int(uint(y * 1024 + x) & uint(pixel_mask));
    return texelFetch(uSource, ivec2(index & 1023, index >> 10), 0).r;
}

uint sample_texture(int u, int v) {
    uint tpage = command.texture.x;
    uint gpu_type = command.texture.z;
    int depth = gpu_type == 2u ? int((tpage >> 7) & 3u)
                               : int((tpage >> 9) & 3u);
    if (depth == 3) return 0u;
    u &= command.window_flip.x;
    v &= command.window_flip.y;
    int ppw = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    int page_x = int(tpage & 15u) * 64;
    int page_y = gpu_type == 2u
        ? int(((tpage & 0x10u) << 4) | ((tpage & 0x800u) >> 2))
        : int(tpage & 0x60u) << 3;
    int base_x = page_x + command.offset_window.z / ppw;
    int base_y = page_y + command.offset_window.w;
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
    int clut_x = int(command.texture.y & 63u) * 16;
    int clut_y = int((command.texture.y >> 6) & 1023u);
    return vram_word(clut_x + int(index), clut_y);
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

int dither_offset(ivec2 pixel) {
    const int matrix[16] = int[16](
        -4, 0, -3, 1, 2, -2, 3, -1,
        -3, 1, -4, 0, 3, -1, 2, -2);
    return matrix[(pixel.y & 3) * 4 + (pixel.x & 3)];
}

uint quantize_dithered(int value, int offset) {
    return uint(clamp((value + offset) >> 3, 0, 31));
}

void main() {
    ivec2 pixel = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));
    if (pixel.x < command.draw_area.x || pixel.y < command.draw_area.y ||
        pixel.x > command.draw_area.z || pixel.y > command.draw_area.w)
        discard;

    uint flags = command.header.z;
    bool textured = (flags & 2u) != 0u;
    bool raw_texture = (flags & 4u) != 0u;
    bool gouraud = (flags & 1u) != 0u;
    uint result = command.header.w;
    if (gouraud) {
        const ivec3 shade = ivec3(clamp(vColor, vec3(0.0), vec3(255.0)));
        const int offset = (flags & 64u) != 0u ? dither_offset(pixel) : 0;
        result = quantize_dithered(shade.r, offset) |
                 (quantize_dithered(shade.g, offset) << 5) |
                 (quantize_dithered(shade.b, offset) << 10);
    }
    uint texel = 0u;
    if (textured) {
        // Raster fragments sample at pixel centers. A flipped rectangle can
        // interpolate to a negative half-texel at the far edge; GLSL's int
        // conversion truncates that toward zero, while PSX UVs wrap modulo
        // 256 after selecting the preceding texel. Floor before wrapping so
        // U/V 0 correctly continue at 255 instead of repeating texel zero.
        const ivec2 uv_sample = ivec2(floor(vUV));
        texel = sample_texture(uv_sample.x, uv_sample.y);
        if (texel == 0u) discard;
        if (raw_texture) {
            result = texel;
        } else {
            uvec3 tex = uvec3(texel & 31u, (texel >> 5) & 31u,
                              (texel >> 10) & 31u);
            // Gouraud textured primitives keep the interpolated 8-bit
            // command color for modulation. `result` above is its packed
            // RGB555 destination value and cannot be unpacked as RGB888.
            const ivec3 shade = ivec3(clamp(vColor, vec3(0.0), vec3(255.0)));
            const int offset = (flags & 64u) != 0u ? dither_offset(pixel) : 0;
            const uvec3 modulated = uvec3(
                quantize_dithered((int(tex.r) * shade.r) >> 4, offset),
                quantize_dithered((int(tex.g) * shade.g) >> 4, offset),
                quantize_dithered((int(tex.b) * shade.b) >> 4, offset));
            result = modulated.x | (modulated.y << 5) | (modulated.z << 10) |
                     (texel & 0x8000u);
        }
    }

    uint old = texelFetch(uSource, pixel, 0).r;
    if ((flags & 32u) != 0u && (old & 0x8000u) != 0u) discard;
    if ((flags & 8u) != 0u && (!textured || (texel & 0x8000u) != 0u))
        result = blend555(old, result, command.texture.w & 3u);
    if ((flags & 16u) != 0u) result |= 0x8000u;
    oColor = result;
}
