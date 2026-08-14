#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform sampler sAlbedo;

// Passthrough for now -- real lighting math comes once normal/material are wired in.
void main() {
    outColor = texture(sampler2D(tAlbedo, sAlbedo), vUv);
}
