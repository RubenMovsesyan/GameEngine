#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tAlbedo;
layout(binding = 1) uniform texture2D tNormal;
layout(binding = 2) uniform sampler sLinear;
layout(binding = 4) uniform texture2D tDepth;
layout(binding = 5) uniform texture2D tMaterial;
layout(binding = 6) uniform texture2DArray tShadow;
layout(binding = 7) uniform samplerShadow sShadowCmp;

struct Light {
    vec4 color_intensity;  // xyz = color, w = intensity
    vec4 position_range;   // xyz = position, w = range
    vec4 direction_type;   // xyz = direction, w = LightType (0 = Directional, 1 = Point)
    vec4 cone_angles;
};

layout(std430, binding = 3) readonly buffer LightBuffer {
    Light lights[];
};

const int kCascadeCount = 4;

layout(set = 1, binding = 0) uniform LightingUniforms {
    mat4 inverse_view_projection; // see reconstructWorldPosition()
    mat4 view;                    // world -> view; for cascade selection by depth
    mat4 cascade_view_proj[kCascadeCount];
    vec4 view_position;
    vec4 ambient;                 // rgb = flat ambient irradiance (pre-tonemap), a unused
    vec4 cascade_split_depth;     // x..w = view-space far edge of each cascade
    vec4 shadow_params;           // x = depth bias, y = normal offset (world units)
    uint light_count;
} u;

const float kPi = 3.14159265359;
const vec3 kDielectricF0 = vec3(0.04); // glTF's fixed non-metal reflectance

// Inverse of gbuffer.frag's octEncode: 2 channels -> unit normal.
vec3 octDecode(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), depth, 1.0); // Y negated -- see docs (NVRHI viewport flip)
    vec4 world = u.inverse_view_projection * ndc;
    return world.xyz / world.w;
}

// 3x3 PCF against one cascade slice. Returns 1.0 = fully lit, 0.0 = fully shadowed.
float sampleCascade(int cascade, vec3 world_pos, vec3 N) {
    vec4 lp = u.cascade_view_proj[cascade] * vec4(world_pos + N * u.shadow_params.y, 1.0);
    lp.xyz /= lp.w;

    vec2 uv = lp.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // undo the NVRHI viewport Y-flip (matches reconstructWorldPosition)

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || lp.z > 1.0) {
        return 1.0;
    }

    float ref = lp.z - u.shadow_params.x;
    vec2 texel = 1.0 / vec2(textureSize(sampler2DArrayShadow(tShadow, sShadowCmp), 0).xy);

    float sum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            sum += texture(sampler2DArrayShadow(tShadow, sShadowCmp), vec4(uv + vec2(x, y) * texel, float(cascade), ref));
        }
    }
    return sum / 9.0;
}

float sampleShadow(vec3 world_pos, vec3 N) {
    float view_depth = -(u.view * vec4(world_pos, 1.0)).z;

    int cascade = kCascadeCount - 1;
    for (int c = 0; c < kCascadeCount; c++) {
        if (view_depth < u.cascade_split_depth[c]) {
            cascade = c;
            break;
        }
    }

    float visibility = sampleCascade(cascade, world_pos, N);

    // Blend into the next cascade over the last 15% of this one's range to hide the seam.
    float split = u.cascade_split_depth[cascade];
    float prev = cascade == 0 ? 0.0 : u.cascade_split_depth[cascade - 1];
    float band = 0.15 * (split - prev);
    if (cascade < kCascadeCount - 1 && view_depth > split - band) {
        float t = (view_depth - (split - band)) / band;
        visibility = mix(visibility, sampleCascade(cascade + 1, world_pos, N), clamp(t, 0.0, 1.0));
    }
    return visibility;
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
    vec3 normal = octDecode(texture(sampler2D(tNormal, sLinear), vUv).rg);
    vec3 world_position = reconstructWorldPosition(vUv, texture(sampler2D(tDepth, sLinear), vUv).r);

    vec4 material = texture(sampler2D(tMaterial, sLinear), vUv);
    float roughness = clamp(material.g, 0.04, 1.0); // floor avoids a degenerate zero-area highlight
    float metallic = material.b;

    vec3 view_dir = normalize(u.view_position.xyz - world_position);
    vec3 f0 = mix(kDielectricF0, albedo.rgb, metallic);
    vec3 diffuse_color = albedo.rgb * (1.0 - metallic); // metals have no diffuse lobe
    float ndotv = max(dot(normal, view_dir), 1e-4);

    vec3 accumulated = vec3(0.0);
    for (uint i = 0u; i < u.light_count; i++) {
        Light light = lights[i];
        int type = int(round(light.direction_type.w));

        vec3 to_light;
        float attenuation = 1.0;
        float shadow = 1.0;

        if (type == 0) { // Directional -- direction is where light travels, so negate for N.L
            to_light = normalize(-light.direction_type.xyz);
            shadow = sampleShadow(world_position, normal);
        } else if (type == 1) { // Point
            vec3 delta = light.position_range.xyz - world_position;
            float dist = length(delta);
            float range = light.position_range.w;
            if (range > 0.0 && dist >= range) {
                continue; // outside the light volume -- pointAttenuation is 0 here anyway, skip the BRDF
            }
            to_light = delta / max(dist, 1e-4);
            attenuation = pointAttenuation(dist, range);
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
        accumulated += (kd * diffuse_color / kPi + specular) * radiance * ndotl * shadow;
    }

    outColor = vec4(diffuse_color * u.ambient.rgb + accumulated, albedo.a);
}
