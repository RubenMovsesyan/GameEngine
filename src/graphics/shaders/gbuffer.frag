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
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;

void main() {
    outAlbedo = texture(sampler2D(tAlbedo, sLinear), vUV) * factors.base_color_factor;

    vec4 mr = texture(sampler2D(tMetallicRoughness, sLinear), vUV);
    // R reserved for occlusion later (unused), G = roughness, B = metallic, per glTF's packing.
    outMaterial = vec4(mr.r, mr.g * factors.roughness_factor, mr.b * factors.metallic_factor, 1.0);

    outNormal = vec4(normalize(vNormal), 0.0);
}
