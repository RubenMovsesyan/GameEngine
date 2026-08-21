#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform sampler sLinear;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
    outAlbedo = texture(sampler2D(tAlbedo, sLinear), vUV);
    outNormal = vec4(normalize(vNormal), 0.0);
}
