#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 light_view_proj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.light_view_proj * pc.model * vec4(inPosition, 1.0);
}
