#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tDepth;
layout(binding = 1) uniform sampler sLinear;
// binding = 2: uniform textureCube tCubemap;   // added with CubemapSky

layout(push_constant) uniform PushConstants {
    mat4 inverse_view_projection; // see reconstructWorldDir()
    vec4 view_position;
    vec4 zenith_intensity;        // rgb = zenith color, a = intensity
    vec4 ground;                  // rgb = ground color
    uint kind;                    // 0 = gradient, 1 = cubemap (unused today)
} pc;

// World-space view ray; Y-negate matches lighting.frag's reconstructWorldPosition (NVRHI viewport flip).
vec3 reconstructWorldDir(vec2 uv) {
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, -(uv.y * 2.0 - 1.0), 1.0, 1.0); // depth 1.0 = far plane
    vec4 world = pc.inverse_view_projection * ndc;
    return normalize(world.xyz / world.w - pc.view_position.xyz);
}

void main() {
    if (texture(sampler2D(tDepth, sLinear), vUv).r < 1.0) {
        discard; // geometry pixel -- leave the lighting result untouched
    }

    vec3 dir = reconstructWorldDir(vUv);

    vec3 color;
    switch (pc.kind) {
        default: // 0 = gradient
            // ground/zenith are linear radiance in the lights' units; tonemap.frag applies exposure.
            color = mix(pc.ground.rgb, pc.zenith_intensity.rgb, smoothstep(-0.05, 0.25, dir.y));
            break;
        // case 1: color = texture(samplerCube(tCubemap, sLinear), dir).rgb; break; // CubemapSky
    }

    outColor = vec4(color * pc.zenith_intensity.a, 1.0);
}
