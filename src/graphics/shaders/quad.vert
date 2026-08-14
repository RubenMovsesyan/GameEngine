#version 450

layout(location = 0) out vec2 vUv;

// A small inset box, not fullscreen, so the sampled offscreen Surface is visibly
// composited into the main pass rather than indistinguishable from a direct draw.
const vec2 kPositions[6] = vec2[](
    vec2(0.1, 0.1), vec2(0.9, 0.1), vec2(0.1, 0.9),
    vec2(0.1, 0.9), vec2(0.9, 0.1), vec2(0.9, 0.9)
);

void main() {
    vec2 pos = kPositions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    vUv = pos * 0.5 + 0.5;
}
