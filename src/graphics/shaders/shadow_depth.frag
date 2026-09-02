#version 450

// Depth-only pass -- no color output. Present so NVRHI gets a real pixel shader
// instead of a null handle (and it's where alpha-masked shadow discard plugs in later).
void main() {}
