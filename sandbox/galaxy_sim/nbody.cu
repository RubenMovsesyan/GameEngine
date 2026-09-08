// Naive direct O(N^2) gravity for galaxy_sim. Compiled to PTX by build.cpp
// (nvcc --ptx) and JIT-loaded at runtime with cuModuleLoad.
//
// Three kernels. accel + step are split so the update is a true simultaneous step: one
// fused kernel would let thread i overwrite pos[i] while thread k still reads it for its
// own sum. The split needs a scratch acceleration array (count * 3 floats), owned by the
// sandbox.
//
// Units (see body.h): length = 1 kpc, mass = 1 solar mass. `g` is the scaled gravitational
// constant G' = G_SI * Msun * T^2 / L^3; the time unit T is not fixed yet, so `g` and `dt`
// are degenerate for now (only their product shapes the trajectory).

#include "body.h"

// acc_i = g * sum_{j != i}  m_j * (r_j - r_i) / (|r_j - r_i|^2 + soft2)^(3/2)
extern "C" __global__ void accel(const Body* b, f32* acc, u32 n, f32 g, f32 soft2) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }

    const f32 xi = b[i].pos[0];
    const f32 yi = b[i].pos[1];
    const f32 zi = b[i].pos[2];

    f32 ax = 0.0f;
    f32 ay = 0.0f;
    f32 az = 0.0f;

    for (u32 j = 0; j < n; j++) {
        if (j == i) {
            continue;
        }
        const f32 dx = b[j].pos[0] - xi;
        const f32 dy = b[j].pos[1] - yi;
        const f32 dz = b[j].pos[2] - zi;

        const f32 r2 = dx * dx + dy * dy + dz * dz + soft2;
        const f32 inv = rsqrtf(r2);
        const f32 s = b[j].mass * inv * inv * inv; // m_j / (r2)^(3/2)

        ax += dx * s;
        ay += dy * s;
        az += dz * s;
    }

    acc[i * 3u + 0u] = g * ax;
    acc[i * 3u + 1u] = g * ay;
    acc[i * 3u + 2u] = g * az;
}

// Semi-implicit (symplectic) Euler: v += a*dt ; then x += v*dt (uses the updated v).
extern "C" __global__ void step(Body* b, const f32* acc, u32 n, f32 dt) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }

    const f32 vx = b[i].vel[0] + acc[i * 3u + 0u] * dt;
    const f32 vy = b[i].vel[1] + acc[i * 3u + 1u] * dt;
    const f32 vz = b[i].vel[2] + acc[i * 3u + 2u] * dt;

    b[i].vel[0] = vx;
    b[i].vel[1] = vy;
    b[i].vel[2] = vz;

    b[i].pos[0] += vx * dt;
    b[i].pos[1] += vy * dt;
    b[i].pos[2] += vz * dt;
}

// Body -> graphics::primitives::scene::PointInstance (8 floats):
//   [0..2] position   [3] size   [4..7] color rgba (.a = intensity multiplier)
// size / colour derived from mass -- cosmetic, tune freely.
extern "C" __global__ void pack(const Body* b, f32* out, u32 n) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    f32* o = out + static_cast<usize>(i) * 8u;

    o[0] = b[i].pos[0];
    o[1] = b[i].pos[1];
    o[2] = b[i].pos[2];

    // t in [0,1] over the [0.08, 100] solar-mass range; ts is a gentler curve for size/hue.
    const f32 t = __saturatef((b[i].mass - 0.08f) / (100.0f - 0.08f));
    const f32 ts = powf(t, 0.35f);

    o[3] = 0.04f + 0.22f * ts;                 // size

    o[4] = 1.00f - 0.30f * ts;                 // r : reddish (low mass) -> white-blue (high)
    o[5] = 0.75f + 0.15f * ts;                 // g
    o[6] = 0.55f + 0.45f * ts;                 // b
    o[7] = 0.25f + 2.50f * powf(t, 0.7f);      // intensity ~ luminosity, steep with mass
}
