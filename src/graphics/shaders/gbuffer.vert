#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normal_matrix; // inverse-transpose of mat3(model) -- correct under non-uniform scale
} pc;

layout(set = 1, binding = 1) uniform Camera {
    mat4 view_projection;
} cam;

void main() {
    gl_Position = cam.view_projection * pc.model * vec4(inPosition, 1.0);
    vNormal = mat3(pc.normal_matrix) * inNormal;
    vUV = inUV;
}
