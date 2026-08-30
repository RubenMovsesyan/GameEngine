#version 450

// Minimal FXAA (Timothy Lottes' FXAA3 "console" variant): 4-corner luma edge detect + short directional blur, on the tonemapped LDR image.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform texture2D tInput;
layout(binding = 1) uniform sampler sLinear;

layout(push_constant) uniform PushConstants {
    vec2 rcp_frame; // 1.0 / screen size, in UV units
    float enabled;  // < 0.5 -> passthrough
} pc;

const float kEdgeThresholdMin = 1.0 / 128.0; // ignore edges darker than this
const float kEdgeThreshold    = 1.0 / 8.0;   // relative contrast needed to treat as an edge
const float kSpanMax          = 8.0;         // clamp on blur reach, in texels

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    // Cancel the NVRHI viewport Y-flip -- every post pass after tonemap re-introduces it.
    vec2 uv = vec2(vUv.x, 1.0 - vUv.y);

    vec3 rgbM = texture(sampler2D(tInput, sLinear), uv).rgb;
    if (pc.enabled < 0.5) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    vec3 rgbNW = textureLod(sampler2D(tInput, sLinear), uv + vec2(-1.0, -1.0) * pc.rcp_frame, 0.0).rgb;
    vec3 rgbNE = textureLod(sampler2D(tInput, sLinear), uv + vec2( 1.0, -1.0) * pc.rcp_frame, 0.0).rgb;
    vec3 rgbSW = textureLod(sampler2D(tInput, sLinear), uv + vec2(-1.0,  1.0) * pc.rcp_frame, 0.0).rgb;
    vec3 rgbSE = textureLod(sampler2D(tInput, sLinear), uv + vec2( 1.0,  1.0) * pc.rcp_frame, 0.0).rgb;

    float lNW = luma(rgbNW);
    float lNE = luma(rgbNE);
    float lSW = luma(rgbSW);
    float lSE = luma(rgbSE);
    float lM  = luma(rgbM);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    if (lMax - lMin < max(kEdgeThresholdMin, lMax * kEdgeThreshold)) {
        outColor = vec4(rgbM, 1.0); // flat region, leave it
        return;
    }

    // Blur direction is perpendicular to the luma gradient across the 4 corners.
    vec2 dir = vec2(
        -((lNW + lNE) - (lSW + lSE)),
         ((lNW + lSW) - (lNE + lSE))
    );

    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * kEdgeThreshold, kEdgeThresholdMin);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -kSpanMax, kSpanMax) * pc.rcp_frame;

    vec3 rgbA = 0.5 * (
        textureLod(sampler2D(tInput, sLinear), uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb +
        textureLod(sampler2D(tInput, sLinear), uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb
    );
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        textureLod(sampler2D(tInput, sLinear), uv + dir * -0.5, 0.0).rgb +
        textureLod(sampler2D(tInput, sLinear), uv + dir *  0.5, 0.0).rgb
    );

    float lB = luma(rgbB);
    outColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0); // reject overshoot
}
