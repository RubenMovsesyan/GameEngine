#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2DArray tShadow;
layout(binding = 1) uniform sampler sPoint;

layout(push_constant) uniform PushConstants {
    uint layer;
} pc;

// Raw shadow-map depth of one cascade slice, as grayscale. Debug only.
void main() {
    float depth = texture(sampler2DArray(tShadow, sPoint), vec3(vUv, float(pc.layer))).r;
    outColor = vec4(vec3(depth), 1.0);
}
