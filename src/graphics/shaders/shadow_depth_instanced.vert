#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in mat4 inInstanceTransform; // minimal shadow layout: POSITION then INSTANCE_TRANSFORM at 1-4

layout(push_constant) uniform PushConstants {
    mat4 light_view_proj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.light_view_proj * pc.model * inInstanceTransform * vec4(inPosition, 1.0);
}
