#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in mat4 inInstanceTransform;
layout(location = 7) in mat4 inInstanceNormalMatrix; // inverse-transpose of the instance transform

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normal_matrix; // inverse-transpose of mat3(model)
} pc;

layout(set = 1, binding = 1) uniform Camera {
    mat4 view_projection;
} cam;

void main() {
    mat4 world = pc.model * inInstanceTransform;
    gl_Position = cam.view_projection * world * vec4(inPosition, 1.0);
    // normal-matrix of a product == product of the normal-matrices
    vNormal = mat3(pc.normal_matrix) * mat3(inInstanceNormalMatrix) * inNormal;
    vUV = inUV;
}
