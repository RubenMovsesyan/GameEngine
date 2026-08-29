#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform texture2D tNormal;
layout(binding = 2) uniform sampler sLinear;
layout(binding = 4) uniform texture2D tDepth;
layout(binding = 5) uniform texture2D tMaterial;

struct Light {
    vec4 color_intensity;  // xyz = color, w = intensity
    vec4 position_range;   // xyz = position, w = range
    vec4 direction_type;   // xyz = direction, w = LightType (0 = Directional, 1 = Point)
    vec4 cone_angles;
};

layout(std430, binding = 3) readonly buffer LightBuffer {
    Light lights[];
};

layout(push_constant) uniform PushConstants {
    mat4 inverse_view_projection; // see reconstructWorldPosition()
    vec4 view_position;
    uint light_count;
} pc;

const float kAmbient = 0.1;
const float kPi = 3.14159265359;
const vec3 kDielectricF0 = vec3(0.04); // glTF's fixed non-metal reflectance

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), depth, 1.0); // Y negated -- see docs (NVRHI viewport flip)
    vec4 world = pc.inverse_view_projection * ndc;
    return world.xyz / world.w;
}

// KHR_lights_punctual point-light falloff; range <= 0 means infinite range.
float pointAttenuation(float distance, float range) {
    float inv_sq = 1.0 / max(distance * distance, 1e-4);
    if (range <= 0.0) {
        return inv_sq;
    }
    float falloff = clamp(1.0 - pow(distance / range, 4.0), 0.0, 1.0);
    return falloff * falloff * inv_sq;
}

float distributionGGX(float ndoth, float roughness) {
    float a = roughness * roughness; // glTF roughness is perceptual; square it for alpha
    float a2 = a * a;
    float d = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

float geometrySmith(float ndotv, float ndotl, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // direct-lighting k; IBL would use a different one
    float gv = ndotv / (ndotv * (1.0 - k) + k);
    float gl = ndotl / (ndotl * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float vdoth, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
}

void main() {
    vec4 albedo = texture(sampler2D(tAlbedo, sLinear), vUv);
    vec3 normal = normalize(texture(sampler2D(tNormal, sLinear), vUv).xyz);
    vec3 world_position = reconstructWorldPosition(vUv, texture(sampler2D(tDepth, sLinear), vUv).r);

    vec4 material = texture(sampler2D(tMaterial, sLinear), vUv);
    float roughness = clamp(material.g, 0.04, 1.0); // floor avoids a degenerate zero-area highlight
    float metallic = material.b;

    vec3 view_dir = normalize(pc.view_position.xyz - world_position);
    vec3 f0 = mix(kDielectricF0, albedo.rgb, metallic);
    vec3 diffuse_color = albedo.rgb * (1.0 - metallic); // metals have no diffuse lobe
    float ndotv = max(dot(normal, view_dir), 1e-4);

    vec3 accumulated = vec3(0.0);
    for (uint i = 0u; i < pc.light_count; i++) {
        Light light = lights[i];
        int type = int(round(light.direction_type.w));

        vec3 to_light;
        float attenuation = 1.0;

        if (type == 0) { // Directional -- direction is where light travels, so negate for N.L
            to_light = normalize(-light.direction_type.xyz);
        } else if (type == 1) { // Point
            vec3 delta = light.position_range.xyz - world_position;
            float dist = length(delta);
            to_light = delta / max(dist, 1e-4);
            attenuation = pointAttenuation(dist, light.position_range.w);
        } else {
            continue; // Spot still unsupported
        }

        vec3 halfway = normalize(to_light + view_dir);
        float ndotl = max(dot(normal, to_light), 0.0);
        float ndoth = max(dot(normal, halfway), 0.0);
        float vdoth = max(dot(view_dir, halfway), 0.0);

        vec3 fresnel = fresnelSchlick(vdoth, f0);
        vec3 specular =
            (distributionGGX(ndoth, roughness) * geometrySmith(ndotv, ndotl, roughness) * fresnel) / max(4.0 * ndotv * ndotl, 1e-4);
        vec3 kd = (vec3(1.0) - fresnel) * (1.0 - metallic); // energy left over for diffuse

        vec3 radiance = light.color_intensity.rgb * light.color_intensity.a * attenuation;
        accumulated += (kd * diffuse_color / kPi + specular) * radiance * ndotl;
    }

    outColor = vec4(diffuse_color * kAmbient + accumulated, albedo.a);
}
