#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tInput;
layout(binding = 1) uniform sampler sLinear;

layout(push_constant) uniform PushConstants {
    float exposure;
} pc;

// Narkowicz ACES approximation.
vec3 acesApprox(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(tInput, sLinear), vUv).rgb;
    outColor = vec4(acesApprox(hdr * pc.exposure), 1.0);
}
