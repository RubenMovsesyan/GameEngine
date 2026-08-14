#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tSource;
layout(binding = 1) uniform sampler sSource;

void main() {
    outColor = texture(sampler2D(tSource, sSource), vUv);
}
