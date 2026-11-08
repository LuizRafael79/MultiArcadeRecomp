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

uniform int uVramHeight;
out vec2 vUV;
out vec3 vColor;

void main() {
    const int quad_indices[6] = int[6](0, 1, 2, 2, 1, 3);
    int vertex_index = command.header.y == 4u ? quad_indices[gl_VertexID]
                                               : gl_VertexID;
    ivec4 vertex = command.vertices[vertex_index].xyuv;
    uint color = command.vertices[vertex_index].color;
    vec2 xy = vec2(vertex.xy);
    gl_Position = vec4((xy.x / 512.0) - 1.0,
                       (xy.y * 2.0 / float(uVramHeight)) - 1.0, 0.0, 1.0);
    vUV = vec2(vertex.zw);
    vColor = vec3(color & 255u, (color >> 8) & 255u,
                   (color >> 16) & 255u);
}
