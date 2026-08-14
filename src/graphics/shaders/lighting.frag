#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform texture2D tNormal;
layout(binding = 2) uniform sampler sLinear;

// Not read yet -- reserved for view-dependent shading (specular) once that exists.
layout(push_constant) uniform PushConstants {
    vec4 view_position;
} pc;

// ~30 degrees off straight-up, tilted on two axes, so more than one cube face reads
// differently lit. Hardcoded for now -- becomes a real light source later.
const vec3 kLightDir = normalize(vec3(-0.433, 0.75, 0.5));
const float kAmbient = 0.1;

void main() {
    vec4 albedo = texture(sampler2D(tAlbedo, sLinear), vUv);
    vec3 normal = normalize(texture(sampler2D(tNormal, sLinear), vUv).xyz);

    float ndotl = max(dot(normal, kLightDir), 0.0);
    outColor = vec4(albedo.rgb * (kAmbient + ndotl), albedo.a);
}
