// All engine shader programs, in sokol-shdc's annotated GLSL. One file so the fullscreen
// vertex stage is written once and shared by every fullscreen pass.
//
// Binding spaces are separate and each is per-program: uniform blocks 0..7 map to
// sg_apply_uniforms(N), textures/storage-buffers 0..31 map to sg_bindings.views[N], and
// samplers 0..11 map to sg_bindings.samplers[N]. Resources that share a name across
// programs must share a binding.

@ctype mat4 Eigen::Matrix4f
@ctype vec4 Eigen::Vector4f
@ctype vec2 Eigen::Vector2f

//------------------------------------------------------------------------------
// Shared blocks
//------------------------------------------------------------------------------

@block oct_encode
// Octahedral unit-vector encoding: 3-channel unit normal -> 2 channels, range [-1, 1].
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}
vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return n.z >= 0.0 ? n.xy : octWrap(n.xy);
}
@end

@block oct_decode
// Inverse of octEncode: 2 channels -> unit normal.
vec3 octDecode(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
@end

// Fullscreen triangle from the vertex index alone, no vertex buffer needed.
@vs fullscreen_vs
out vec2 v_uv;
void main() {
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // sokol's Vulkan backend rasterises through a negative-height viewport, so clip +Y is the
    // top of the screen while texture v=0 is the top of the image. Emitting 1 - 2v (rather than
    // 2v - 1) keeps v_uv a top-left-origin screen uv, which makes a fullscreen pass an identity
    // copy -- otherwise every one of them flips, and the odd number of them lands inverted.
    gl_Position = vec4(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0, 0.0, 1.0);
}
@end

//------------------------------------------------------------------------------
// G-buffer
//------------------------------------------------------------------------------

@vs gbuffer_vs
layout(binding=0) uniform gbuffer_vs_params {
    mat4 view_projection;
    mat4 model;
    mat4 normal_matrix; // inverse-transpose of mat3(model) -- correct under non-uniform scale
};

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;

out vec3 v_normal;
out vec2 v_uv;

void main() {
    gl_Position = view_projection * model * vec4(position, 1.0);
    v_normal = mat3(normal_matrix) * normal;
    v_uv = texcoord;
}
@end

@vs gbuffer_instanced_vs
layout(binding=0) uniform gbuffer_vs_params {
    mat4 view_projection;
    mat4 model;
    mat4 normal_matrix;
};

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in mat4 inst_transform;
layout(location=7) in mat4 inst_normal_matrix; // inverse-transpose of inst_transform

out vec3 v_normal;
out vec2 v_uv;

void main() {
    mat4 world = model * inst_transform;
    gl_Position = view_projection * world * vec4(position, 1.0);
    // normal-matrix of a product == product of the normal-matrices
    v_normal = mat3(normal_matrix) * mat3(inst_normal_matrix) * normal;
    v_uv = texcoord;
}
@end

@fs gbuffer_fs
@include_block oct_encode

// Material factors used to live in a per-material storage buffer; they are per-draw
// constants, so they ride in this uniform block instead.
layout(binding=1) uniform gbuffer_fs_params {
    vec4 base_color_factor;
    vec4 metallic_roughness_factor; // x = metallic, y = roughness, zw unused
};

layout(binding=0) uniform texture2D tex_albedo;
layout(binding=1) uniform texture2D tex_metallic_roughness;
layout(binding=0) uniform sampler smp_material;

in vec3 v_normal;
in vec2 v_uv;

layout(location=0) out vec4 out_albedo;
layout(location=1) out vec2 out_normal; // octahedral-encoded world normal (RG16F)
layout(location=2) out vec4 out_material;

void main() {
    out_albedo = texture(sampler2D(tex_albedo, smp_material), v_uv) * base_color_factor;

    vec4 mr = texture(sampler2D(tex_metallic_roughness, smp_material), v_uv);
    // R reserved for occlusion later (unused), G = roughness, B = metallic, per glTF's packing.
    out_material = vec4(mr.r, mr.g * metallic_roughness_factor.y, mr.b * metallic_roughness_factor.x, 1.0);

    out_normal = octEncode(normalize(v_normal));
}
@end

@program gbuffer gbuffer_vs gbuffer_fs
@program gbuffer_instanced gbuffer_instanced_vs gbuffer_fs

//------------------------------------------------------------------------------
// Shadow depth
//------------------------------------------------------------------------------

@vs shadow_vs
layout(binding=0) uniform shadow_vs_params {
    mat4 light_view_proj;
    mat4 shadow_model;
};

layout(location=0) in vec3 position;

void main() {
    gl_Position = light_view_proj * shadow_model * vec4(position, 1.0);
}
@end

@vs shadow_instanced_vs
layout(binding=0) uniform shadow_vs_params {
    mat4 light_view_proj;
    mat4 shadow_model;
};

// Attribute slots must be contiguous, so the transform starts at 1 here -- the shadow pass
// has no normal or uv attribute to leave room for, unlike the G-buffer pass.
layout(location=0) in vec3 position;
layout(location=1) in mat4 inst_transform;

void main() {
    gl_Position = light_view_proj * shadow_model * inst_transform * vec4(position, 1.0);
}
@end

// Depth-only pass: no color attachment, so nothing to write.
@fs shadow_fs
void main() {}
@end

@program shadow_depth shadow_vs shadow_fs
@program shadow_depth_instanced shadow_instanced_vs shadow_fs

//------------------------------------------------------------------------------
// Deferred lighting
//------------------------------------------------------------------------------

@fs lighting_fs
@include_block oct_decode

// Every G-buffer read is a 1:1 texel fetch, so a non-filtering sampler is both correct and
// sidesteps the depth target not being filterable.
@image_sample_type tex_gbuffer_albedo unfilterable_float
@image_sample_type tex_gbuffer_normal unfilterable_float
@image_sample_type tex_gbuffer_depth unfilterable_float
@image_sample_type tex_gbuffer_material unfilterable_float
@sampler_type smp_gbuffer nonfiltering

layout(binding=0) uniform texture2D tex_gbuffer_albedo;
layout(binding=1) uniform texture2D tex_gbuffer_normal;
layout(binding=2) uniform texture2D tex_gbuffer_depth;
layout(binding=3) uniform texture2D tex_gbuffer_material;
layout(binding=4) uniform texture2DArray tex_shadow;
layout(binding=0) uniform sampler smp_gbuffer;
layout(binding=1) uniform sampler smp_shadow;

struct Light {
    vec4 color_intensity;  // xyz = color, w = intensity
    vec4 position_range;   // xyz = position, w = range
    vec4 direction_type;   // xyz = direction, w = LightType (0 = Directional, 1 = Point)
    vec4 cone_angles;
};

layout(binding=5) readonly buffer light_buffer {
    Light lights[];
};

layout(binding=0) uniform lighting_params {
    mat4 inverse_view_projection; // see reconstructWorldPosition()
    mat4 view;                    // world -> view; for cascade selection by depth
    mat4 cascade_view_proj[4];
    vec4 view_position;
    vec4 ambient;                 // rgb = flat ambient irradiance (pre-tonemap), a unused
    vec4 cascade_split_depth;     // x..w = view-space far edge of each cascade
    vec4 shadow_params;           // x = depth bias, y = normal offset, z = 1 when shadows are on
    int light_count;
};

in vec2 v_uv;
out vec4 out_color;

const int kCascadeCount = 4;
const float kPi = 3.14159265359;
const vec3 kDielectricF0 = vec3(0.04); // glTF's fixed non-metal reflectance

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), depth, 1.0); // Y negated: the viewport is flipped
    vec4 world = inverse_view_projection * ndc;
    return world.xyz / world.w;
}

// 3x3 PCF against one cascade slice. Returns 1.0 = fully lit, 0.0 = fully shadowed.
float sampleCascade(int cascade, vec3 world_pos, vec3 N) {
    vec4 lp = cascade_view_proj[cascade] * vec4(world_pos + N * shadow_params.y, 1.0);
    lp.xyz /= lp.w;

    vec2 uv = lp.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // undo the viewport Y-flip, matching reconstructWorldPosition

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || lp.z > 1.0) {
        return 1.0;
    }

    float ref = lp.z - shadow_params.x;
    vec2 texel = 1.0 / vec2(textureSize(sampler2DArrayShadow(tex_shadow, smp_shadow), 0).xy);

    float sum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            sum += texture(sampler2DArrayShadow(tex_shadow, smp_shadow), vec4(uv + vec2(x, y) * texel, float(cascade), ref));
        }
    }
    return sum / 9.0;
}

float sampleShadow(vec3 world_pos, vec3 N) {
    if (shadow_params.z < 0.5) {
        return 1.0;
    }

    float view_depth = -(view * vec4(world_pos, 1.0)).z;

    int cascade = kCascadeCount - 1;
    for (int c = 0; c < kCascadeCount; c++) {
        if (view_depth < cascade_split_depth[c]) {
            cascade = c;
            break;
        }
    }

    float visibility = sampleCascade(cascade, world_pos, N);

    // Blend into the next cascade over the last 15% of this one's range to hide the seam.
    float split = cascade_split_depth[cascade];
    float prev = cascade == 0 ? 0.0 : cascade_split_depth[cascade - 1];
    float band = 0.15 * (split - prev);
    if (cascade < kCascadeCount - 1 && view_depth > split - band) {
        float t = (view_depth - (split - band)) / band;
        visibility = mix(visibility, sampleCascade(cascade + 1, world_pos, N), clamp(t, 0.0, 1.0));
    }
    return visibility;
}

// KHR_lights_punctual point-light falloff; range <= 0 means infinite range.
float pointAttenuation(float dist, float range) {
    float inv_sq = 1.0 / max(dist * dist, 1e-4);
    if (range <= 0.0) {
        return inv_sq;
    }
    float falloff = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
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
    vec4 albedo = texture(sampler2D(tex_gbuffer_albedo, smp_gbuffer), v_uv);
    vec3 normal = octDecode(texture(sampler2D(tex_gbuffer_normal, smp_gbuffer), v_uv).rg);
    vec3 world_position = reconstructWorldPosition(v_uv, texture(sampler2D(tex_gbuffer_depth, smp_gbuffer), v_uv).r);

    vec4 material = texture(sampler2D(tex_gbuffer_material, smp_gbuffer), v_uv);
    float roughness = clamp(material.g, 0.04, 1.0); // floor avoids a degenerate zero-area highlight
    float metallic = material.b;

    vec3 view_dir = normalize(view_position.xyz - world_position);
    vec3 f0 = mix(kDielectricF0, albedo.rgb, metallic);
    vec3 diffuse_color = albedo.rgb * (1.0 - metallic); // metals have no diffuse lobe
    float ndotv = max(dot(normal, view_dir), 1e-4);

    vec3 accumulated = vec3(0.0);
    for (int i = 0; i < light_count; i++) {
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
                continue; // outside the light volume -- attenuation is 0 here anyway
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

    out_color = vec4(diffuse_color * ambient.rgb + accumulated, albedo.a);
}
@end

@program lighting fullscreen_vs lighting_fs

//------------------------------------------------------------------------------
// Sky
//------------------------------------------------------------------------------

@fs sky_fs
@image_sample_type tex_sky_depth unfilterable_float
@sampler_type smp_sky nonfiltering

layout(binding=2) uniform texture2D tex_sky_depth;
layout(binding=2) uniform sampler smp_sky;

layout(binding=1) uniform sky_params {
    mat4 sky_inverse_view_projection;
    vec4 sky_view_position;
    vec4 zenith_intensity; // rgb = zenith colour, a = intensity
    vec4 ground;           // rgb = ground colour
    vec4 sky_kind;         // x = 0 gradient, 1 cubemap (not built yet)
};

in vec2 v_uv;
out vec4 out_color;

void main() {
    // Only shade pixels the G-buffer never wrote -- anything nearer is real geometry.
    float depth = texture(sampler2D(tex_sky_depth, smp_sky), v_uv).r;
    if (depth < 1.0) {
        discard;
    }

    vec4 ndc = vec4(v_uv.x * 2.0 - 1.0, -(v_uv.y * 2.0 - 1.0), 1.0, 1.0);
    vec4 world = sky_inverse_view_projection * ndc;
    vec3 dir = normalize(world.xyz / world.w - sky_view_position.xyz);

    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 colour = mix(ground.rgb, zenith_intensity.rgb, t) * zenith_intensity.a;
    out_color = vec4(colour, 1.0);
}
@end

@program sky fullscreen_vs sky_fs

//------------------------------------------------------------------------------
// Post-process
//------------------------------------------------------------------------------

@fs tonemap_fs
layout(binding=0) uniform texture2D tex_source;
layout(binding=0) uniform sampler smp_source;

layout(binding=3) uniform tonemap_params {
    vec4 exposure; // x = exposure, yzw unused
};

in vec2 v_uv;
out vec4 out_color;

// ACES filmic approximation (Narkowicz).
vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(tex_source, smp_source), v_uv).rgb * exposure.x;
    out_color = vec4(acesFilm(hdr), 1.0);
}
@end

@program tonemap fullscreen_vs tonemap_fs

@fs fxaa_fs
layout(binding=0) uniform texture2D tex_source;
layout(binding=0) uniform sampler smp_source;

layout(binding=4) uniform fxaa_params {
    vec4 rcp_frame; // xy = 1/resolution, z = enabled
};

in vec2 v_uv;
out vec4 out_color;

const float kEdgeMin = 1.0 / 128.0;
const float kEdgeMinReduce = 1.0 / 8.0;
const float kSpanMax = 8.0;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 rcp = rcp_frame.xy;
    vec3 rgb_m = texture(sampler2D(tex_source, smp_source), v_uv).rgb;

    if (rcp_frame.z < 0.5) {
        out_color = vec4(rgb_m, 1.0);
        return;
    }

    vec3 rgb_nw = texture(sampler2D(tex_source, smp_source), v_uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 rgb_ne = texture(sampler2D(tex_source, smp_source), v_uv + vec2(1.0, -1.0) * rcp).rgb;
    vec3 rgb_sw = texture(sampler2D(tex_source, smp_source), v_uv + vec2(-1.0, 1.0) * rcp).rgb;
    vec3 rgb_se = texture(sampler2D(tex_source, smp_source), v_uv + vec2(1.0, 1.0) * rcp).rgb;

    float luma_nw = luma(rgb_nw);
    float luma_ne = luma(rgb_ne);
    float luma_sw = luma(rgb_sw);
    float luma_se = luma(rgb_se);
    float luma_m = luma(rgb_m);

    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));

    vec2 dir = vec2(-((luma_nw + luma_ne) - (luma_sw + luma_se)), ((luma_nw + luma_sw) - (luma_ne + luma_se)));

    float dir_reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * 0.25 * kEdgeMinReduce, kEdgeMin);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, vec2(-kSpanMax), vec2(kSpanMax)) * rcp;

    vec3 rgb_a = 0.5 * (texture(sampler2D(tex_source, smp_source), v_uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                        texture(sampler2D(tex_source, smp_source), v_uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgb_b = rgb_a * 0.5 + 0.25 * (texture(sampler2D(tex_source, smp_source), v_uv + dir * -0.5).rgb +
                                       texture(sampler2D(tex_source, smp_source), v_uv + dir * 0.5).rgb);

    float luma_b = luma(rgb_b);
    out_color = vec4((luma_b < luma_min || luma_b > luma_max) ? rgb_a : rgb_b, 1.0);
}
@end

@program fxaa fullscreen_vs fxaa_fs

//------------------------------------------------------------------------------
// Shadow-map debug blit
//------------------------------------------------------------------------------

@fs shadow_debug_fs
@image_sample_type tex_shadow_debug unfilterable_float
@sampler_type smp_shadow_debug nonfiltering

layout(binding=6) uniform texture2DArray tex_shadow_debug;
layout(binding=3) uniform sampler smp_shadow_debug;

layout(binding=5) uniform shadow_debug_params {
    vec4 layer; // x = cascade slice to show
};

in vec2 v_uv;
out vec4 out_color;

void main() {
    float depth = texture(sampler2DArray(tex_shadow_debug, smp_shadow_debug), vec3(v_uv, layer.x)).r;
    out_color = vec4(vec3(depth), 1.0);
}
@end

@program shadow_debug fullscreen_vs shadow_debug_fs
