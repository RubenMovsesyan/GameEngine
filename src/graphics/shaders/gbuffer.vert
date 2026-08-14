#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 vColor;

// mvp = view_projection * model, combined on the CPU (see Renderer::renderMesh) --
// Vulkan only guarantees 128 bytes of push-constant space, and model+view_projection+
// view_position together (144B) exceeds that, so model gets folded in before it reaches
// the shader instead of being its own field here.
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 view_position;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vColor = inColor;
}
