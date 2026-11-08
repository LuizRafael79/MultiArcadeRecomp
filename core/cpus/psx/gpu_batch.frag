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
};
layout(std430, binding = 2) readonly buffer BatchCommands {
    ShaderCommand commands[];
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

int polygon_next(int point, int count, bool alternate) {
    if (count == 3) return point == 2 ? 0 : point + 1;
    if (alternate) {
        if (point == 0) return 1;
        if (point == 1) return 3;
        if (point == 2) return 0;
        return 2;
    }
    if (point == 0) return 1;
    if (point == 1) return 3;
    if (point == 2) return 0;
    return 2;
}
int polygon_prev(int point, int count, bool alternate) {
    if (count == 3) return point == 0 ? 2 : point - 1;
    if (alternate) {
        if (point == 0) return 0;
        if (point == 1) return 2;
        if (point == 2) return 3;
        return 1;
    }
    if (point == 0) return 2;
    if (point == 1) return 0;
    if (point == 2) return 3;
    return 1;
}
int polygon_x(ShaderCommand command, int point) {
    return command.vertices[point].xyuv.x;
}
int polygon_y(ShaderCommand command, int point) {
    return command.vertices[point].xyuv.y;
}
int polygon_attribute(ShaderCommand command, int point, int field_id) {
    if (field_id == 0) return polygon_x(command, point);
    if (field_id == 1) return command.vertices[point].xyuv.z;
    if (field_id == 2) return command.vertices[point].xyuv.w;
    uint color = command.vertices[point].color;
    if (field_id == 3) return int(color & 255u);
    if (field_id == 4) return int((color >> 8) & 255u);
    return int((color >> 16) & 255u);
}
bool polygon_cull_point(ShaderCommand command, int a, int b) {
    return abs(polygon_x(command, a) - polygon_x(command, b)) > 1023 ||
           abs(polygon_y(command, a) - polygon_y(command, b)) > 1023;
}
bool polygon_cull_triangle(ShaderCommand command, int first) {
    int a = first, b = first + 1, c = first + 2;
    return polygon_cull_point(command, a, b) || polygon_cull_point(command, b, c) ||
           polygon_cull_point(command, c, a);
}
int polygon_edge_fixed(ShaderCommand command, int from, int to, int scan_y,
                       int field_id) {
    int y0 = polygon_y(command, from);
    int dy = polygon_y(command, to) - y0;
    if (dy <= 0) return 0;
    int first = polygon_attribute(command, from, field_id);
    int last = polygon_attribute(command, to, field_id);
    int slope = ((last - first) * 65536) / dy;
    return first * 65536 + slope * (scan_y - y0);
}
bool polygon_scanline_sample(ShaderCommand command, ivec2 pixel,
                             out ivec2 uv, out ivec3 color) {
    int count = int(command.header.y);
    bool alternate = false;
    int first_candidate = 0;
    int left_top = 0;
    if (count == 4) {
        if (polygon_cull_triangle(command, 0)) {
            if (polygon_cull_triangle(command, 1)) return false;
            alternate = true;
            first_candidate = 1;
            left_top = 1;
        } else if (polygon_cull_triangle(command, 1)) {
            count = 3;
        }
    } else if (count != 3 || polygon_cull_triangle(command, 0)) {
        return false;
    }
    for (int point = first_candidate; point < count; ++point) {
        int y = polygon_y(command, point);
        int top_y = polygon_y(command, left_top);
        if (y < top_y || (y == top_y && polygon_x(command, point) <
                          polygon_x(command, left_top)))
            left_top = point;
    }
    int left_from = -1, left_to = -1, right_from = -1, right_to = -1;
    int point = left_top;
    for (int step = 0; step < count; ++step) {
        int next_point = polygon_prev(point, count, alternate);
        if (polygon_y(command, point) <= pixel.y &&
            pixel.y < polygon_y(command, next_point)) {
            left_from = point;
            left_to = next_point;
            break;
        }
        point = next_point;
    }
    point = left_top;
    for (int step = 0; step < count; ++step) {
        int next_point = polygon_next(point, count, alternate);
        if (polygon_y(command, point) <= pixel.y &&
            pixel.y < polygon_y(command, next_point)) {
            right_from = point;
            right_to = next_point;
            break;
        }
        point = next_point;
    }
    if (left_from < 0 || right_from < 0) return false;
    int left_x = polygon_edge_fixed(command, left_from, left_to, pixel.y, 0) >> 16;
    int right_x = polygon_edge_fixed(command, right_from, right_to, pixel.y, 0) >> 16;
    if (left_x == right_x) return false;
    int left_fixed[5], right_fixed[5];
    for (int field_id = 1; field_id < 6; ++field_id) {
        left_fixed[field_id - 1] = polygon_edge_fixed(
            command, left_from, left_to, pixel.y, field_id);
        right_fixed[field_id - 1] = polygon_edge_fixed(
            command, right_from, right_to, pixel.y, field_id);
    }
    bool left_is_first = left_x < right_x;
    int x_start = min(left_x, right_x);
    int x_end = max(left_x, right_x);
    int distance = x_end - x_start;
    if (distance <= 0 || pixel.x < x_start || pixel.x >= x_end) return false;
    for (int field_id = 0; field_id < 5; ++field_id) {
        int start = left_is_first ? left_fixed[field_id] : right_fixed[field_id];
        int end = left_is_first ? right_fixed[field_id] : left_fixed[field_id];
        int delta = (end - start) / distance;
        int value = (start + delta * (pixel.x - x_start)) >> 16;
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
    ivec2 uv_sample = ivec2(vUV);
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
        color = (uint(color_sample.r) >> 3) |
                ((uint(color_sample.g) >> 3) << 5) |
                ((uint(color_sample.b) >> 3) << 10);
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
            uvec3 tex = uvec3(texel & 31u, (texel >> 5) & 31u,
                              (texel >> 10) & 31u);
            uvec3 shade = uvec3(color_sample);
            uvec3 modulated = min(uvec3(31u), tex * shade / 128u);
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
