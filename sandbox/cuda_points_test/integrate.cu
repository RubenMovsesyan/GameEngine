// Compiled to PTX by build.cpp (nvcc --ptx) and JIT-loaded at runtime via cuModuleLoad.
// One kernel: advance every point one sub-step along a circular orbit about +Y and tint it.
//
// Each record is a graphics::primitives::scene::PointInstance == 8 floats, tightly packed:
//   [0..2] position (xyz)   [3] size   [4..7] color (rgba, .a = intensity multiplier)

extern "C" __global__ void integrate(float* data, unsigned count, float dt) {
    const unsigned i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    float* p = data + (size_t)i * 8u;

    const float x = p[0];
    const float z = p[2];
    const float r = sqrtf(x * x + z * z) + 1.0e-3f;

    // Inner points sweep faster (toy Keplerian falloff), so the disc shears over time.
    const float omega = 6.0f / (r + 4.0f);
    const float a = omega * dt;
    const float ca = cosf(a);
    const float sa = sinf(a);

    p[0] = x * ca - z * sa;
    p[2] = x * sa + z * ca;

    // Warmer / brighter toward the core.
    const float core = 1.0f - fminf(1.0f, r / 45.0f);
    p[4] = 0.85f + 0.30f * core;         // r
    p[5] = 0.80f + 0.10f * core;         // g
    p[6] = 0.70f + 0.05f * core;         // b
    p[7] = 0.35f + 2.20f * core * core;  // intensity
}
