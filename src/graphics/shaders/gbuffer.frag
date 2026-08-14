#version 450

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outAlbedo;

void main() {
    // DEBUG: solid white, to confirm the geometry pass is actually drawing something
    // before caring about per-vertex color.
    outAlbedo = vec4(1.0, 1.0, 1.0, 1.0);
}
