#version 430 core

layout(location = 0) out uint oColor;
in vec3 vColor;
flat in uint vSetMask;
flat in uint vFlatColor;
flat in uint vGouraud;

void main() {
    uint color = vFlatColor;
    if (vGouraud != 0u) {
        color = (uint(clamp(vColor.r, 0.0, 255.0)) >> 3) |
                ((uint(clamp(vColor.g, 0.0, 255.0)) >> 3) << 5) |
                ((uint(clamp(vColor.b, 0.0, 255.0)) >> 3) << 10);
    }
    oColor = color | vSetMask;
}
