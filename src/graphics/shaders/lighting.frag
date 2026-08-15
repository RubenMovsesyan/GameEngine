#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform texture2D tNormal;
layout(binding = 2) uniform sampler sLinear;
layout(binding = 4) uniform texture2D tDepth;

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
    vec4 view_position;           // unused for now, reserved for specular
    uint light_count;
} pc;

const float kAmbient = 0.1;

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
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

void main() {
    vec4 albedo = texture(sampler2D(tAlbedo, sLinear), vUv);
    vec3 normal = normalize(texture(sampler2D(tNormal, sLinear), vUv).xyz);
    vec3 world_position = reconstructWorldPosition(vUv, texture(sampler2D(tDepth, sLinear), vUv).r);

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

        float ndotl = max(dot(normal, to_light), 0.0);
        accumulated += light.color_intensity.rgb * light.color_intensity.a * ndotl * attenuation;
    }

    outColor = vec4(albedo.rgb * (kAmbient + accumulated), albedo.a);
}
