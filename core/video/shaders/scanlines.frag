#version 330 core

// Generic RGB post-process for any board framebuffer. The source need not be
// indexed or tied to a particular arcade video chip.
uniform sampler2D uFrame;
uniform vec2 uSourceSize;
uniform float uStrength; // 0 = off, 1 = full effect
in vec2 vTexCoord;
out vec4 fragColor;

void main() {
    vec3 color = texture(uFrame, vTexCoord).rgb;
    float sourceRow = floor(vTexCoord.y * uSourceSize.y);
    float darkRow = mod(sourceRow, 2.0);
    float gain = mix(1.0, 0.72, darkRow * clamp(uStrength, 0.0, 1.0));
    fragColor = vec4(color * gain, 1.0);
}
