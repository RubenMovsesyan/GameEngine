#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;

// model kept separate from view_projection (not pre-combined) since the normal needs
// model's rotation, not clip-space projection. Only handles rotation correctly -- a
// non-uniform model scale would need the inverse-transpose of model instead.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view_projection;
} pc;

void main() {
    gl_Position = pc.view_projection * pc.model * vec4(inPosition, 1.0);
    vColor = inColor;
    vNormal = mat3(pc.model) * inNormal;
}
