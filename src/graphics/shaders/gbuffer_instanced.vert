#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in mat4 inInstanceTransform;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view_projection;
} pc;

void main() {
    mat4 world = pc.model * inInstanceTransform;
    gl_Position = pc.view_projection * world * vec4(inPosition, 1.0);
    vNormal = mat3(world) * inNormal;
    vUV = inUV;
}
