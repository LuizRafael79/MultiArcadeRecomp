#version 430 core

struct ShaderVertex {
    ivec4 xyuv;
    uint color;
    uint padding0;
    uint padding1;
    uint padding2;
};
struct ShaderCommand {
    uvec4 header;
    ivec4 draw_area;
    ivec4 offset_window;
    ivec4 window_flip;
    uvec4 texture;
    ShaderVertex vertices[4];
    ivec4 bounds;
    ivec4 batch_scanline;
};
struct BatchScanline {
    ivec4 start0;
    ivec4 start1;
    ivec4 step0;
    ivec4 step1;
    ivec4 span;
};
layout(std430, binding = 2) readonly buffer BatchCommands {
    ShaderCommand commands[];
};
layout(std430, binding = 4) readonly buffer BatchScanlines {
    BatchScanline scanlines[];
};

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
flat in uint vCommandIndex;

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

int dither_offset(ivec2 pixel) {
    const int matrix[16] = int[16](
        -4, 0, -3, 1, 2, -2, 3, -1,
        -3, 1, -4, 0, 3, -1, 2, -2);
    return matrix[(pixel.y & 3) * 4 + (pixel.x & 3)];
}

uint quantize_dithered(int value, int offset) {
    return uint(clamp((value + offset) >> 3, 0, 31));
}

bool polygon_scanline_sample(ShaderCommand command, ivec2 pixel,
                             out ivec2 uv, out ivec3 color) {
    const int row_index = command.batch_scanline.x +
                          (pixel.y - command.batch_scanline.y);
    if (row_index < command.batch_scanline.x ||
        row_index >= command.batch_scanline.x + command.batch_scanline.z)
        return false;
    const BatchScanline row = scanlines[row_index];
    const int x_start = row.span.x;
    const int x_end = row.span.y;
    if (row.span.z == 0 || pixel.x < x_start || pixel.x >= x_end)
        return false;
    const int x_offset = pixel.x - x_start;
    for (int field_id = 0; field_id < 5; ++field_id) {
        const int start = field_id < 4 ? row.start0[field_id] : row.start1.x;
        const int step = field_id < 4 ? row.step0[field_id] : row.step1.x;
        const int value = (start + step * x_offset) >> 16;
        if (field_id == 0) uv.x = value;
        else if (field_id == 1) uv.y = value;
        else color[field_id - 2] = value;
    }
    return true;
}

void main() {
    const ShaderCommand command = commands[vCommandIndex];
    const bool polygon = command.header.x == 0u;
    const ivec2 pixel = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));
    if ((command.header.z & 128u) != 0u &&
        (uint(pixel.y) & 1u) == ((command.texture.w >> 8) & 1u))
        discard;
    // A flipped sprite can interpolate to a negative half-texel at its far
    // edge. Conversion to int truncates toward zero; floor preserves the
    // preceding UV so the later PSX 8-bit wrap selects texel 255.
    ivec2 uv_sample = ivec2(floor(vUV));
    ivec3 color_sample = ivec3(clamp(vColor, vec3(0.0), vec3(255.0)));
    if (polygon) {
        if (pixel.x < command.bounds.x || pixel.y < command.bounds.y ||
            pixel.x > command.bounds.z || pixel.y > command.bounds.w)
            discard;
        if (!polygon_scanline_sample(command, pixel, uv_sample, color_sample))
            discard;
    }
    const bool textured = (vFlags & 2u) != 0u;
    const bool raw_texture = (vFlags & 4u) != 0u;
    uint color = vFlatColor;
    uint texel = 0u;
    if (!textured && vGouraud != 0u) {
        const int offset = (vFlags & 64u) != 0u ? dither_offset(pixel) : 0;
        color = quantize_dithered(color_sample.r, offset) |
                (quantize_dithered(color_sample.g, offset) << 5) |
                (quantize_dithered(color_sample.b, offset) << 10);
    }
    if (textured) {
        const int u = uv_sample.x & vWindowState.z;
        const int v = uv_sample.y & vWindowState.w;
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
            const int offset = (vFlags & 64u) != 0u ? dither_offset(pixel) : 0;
            const ivec3 tex = ivec3(int(texel & 31u), int((texel >> 5) & 31u),
                                    int((texel >> 10) & 31u));
            const uvec3 modulated = uvec3(
                quantize_dithered((tex.r * color_sample.r) >> 4, offset),
                quantize_dithered((tex.g * color_sample.g) >> 4, offset),
                quantize_dithered((tex.b * color_sample.b) >> 4, offset));
            color = modulated.x | (modulated.y << 5) | (modulated.z << 10) |
                    (texel & 0x8000u);
        }
    }
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
