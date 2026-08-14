#version 450

layout(location = 0) in vec3 vNormal;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
    // No texture/material system yet -- flat placeholder until one exists.
    outAlbedo = vec4(0.5, 0.5, 0.5, 1.0);
    outNormal = vec4(normalize(vNormal), 0.0);
}
