#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform texture2D tMetallicRoughness;
layout(binding = 2) uniform sampler sLinear;

struct MaterialFactors {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
};
layout(std430, binding = 3) readonly buffer MaterialFactorsBuffer {
    MaterialFactors factors;
};

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec2 outNormal; // octahedral-encoded world normal (RG16F)
layout(location = 2) out vec4 outMaterial;

// Octahedral unit-vector encoding: 3-channel unit normal -> 2 channels, range [-1, 1].
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return n.z >= 0.0 ? n.xy : octWrap(n.xy);
}

void main() {
    outAlbedo = texture(sampler2D(tAlbedo, sLinear), vUV) * factors.base_color_factor;

    vec4 mr = texture(sampler2D(tMetallicRoughness, sLinear), vUV);
    // R reserved for occlusion later (unused), G = roughness, B = metallic, per glTF's packing.
    outMaterial = vec4(mr.r, mr.g * factors.roughness_factor, mr.b * factors.metallic_factor, 1.0);

    outNormal = octEncode(normalize(vNormal));
}
