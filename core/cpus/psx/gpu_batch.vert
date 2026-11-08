#version 430 core

layout(location = 0) in ivec2 aPosition;
layout(location = 1) in uint aColor;
layout(location = 2) in float aDepth;
layout(location = 3) in uint aSetMask;
layout(location = 4) in uint aGouraud;
layout(location = 5) in ivec2 aUV;
layout(location = 6) in uint aFlags;
layout(location = 7) in uint aTexturePage;
layout(location = 8) in uint aClut;
layout(location = 9) in uint aGpuType;
layout(location = 10) in ivec4 aWindowState;
layout(location = 11) in uint aTextureLayer;

uniform int uVramHeight;
out vec3 vColor;
out vec2 vUV;
flat out uint vSetMask;
flat out uint vFlatColor;
flat out uint vGouraud;
flat out uint vFlags;
flat out uint vTexturePage;
flat out uint vClut;
flat out uint vGpuType;
flat out ivec4 vWindowState;
flat out uint vTextureLayer;

void main() {
    // Keep 1x GPU_HW interpolation aligned to PSX integer pixel/UV origins.
    vec2 xy = vec2(aPosition) + vec2(0.5);
    gl_Position = vec4((xy.x / 512.0) - 1.0,
                       (xy.y * 2.0 / float(uVramHeight)) - 1.0, aDepth, 1.0);
    vColor = vec3(aColor & 255u, (aColor >> 8) & 255u,
                  (aColor >> 16) & 255u);
    vUV = vec2(aUV);
    vSetMask = aSetMask;
    vFlatColor = aColor;
    vGouraud = aGouraud;
    vFlags = aFlags;
    vTexturePage = aTexturePage;
    vClut = aClut;
    vGpuType = aGpuType;
    vWindowState = aWindowState;
    vTextureLayer = aTextureLayer;
}
