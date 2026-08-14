#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
    outAlbedo = vec4(vColor, 1.0);
    outNormal = vec4(normalize(vNormal), 0.0);
}
