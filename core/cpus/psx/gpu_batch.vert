#version 430 core

layout(location = 0) in ivec2 aPosition;
layout(location = 1) in uint aColor;
layout(location = 2) in float aDepth;
layout(location = 3) in uint aSetMask;
layout(location = 4) in uint aGouraud;

uniform int uVramHeight;
out vec3 vColor;
flat out uint vSetMask;
flat out uint vFlatColor;
flat out uint vGouraud;

void main() {
    vec2 xy = vec2(aPosition);
    gl_Position = vec4((xy.x / 512.0) - 1.0,
                       (xy.y * 2.0 / float(uVramHeight)) - 1.0, aDepth, 1.0);
    vColor = vec3(aColor & 255u, (aColor >> 8) & 255u,
                  (aColor >> 16) & 255u);
    vSetMask = aSetMask;
    vFlatColor = aColor;
    vGouraud = aGouraud;
}
