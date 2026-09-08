// galaxy_sim gravity kernels. Compiled to PTX by build.cpp (nvcc --ptx), JIT-loaded via
// cuModuleLoad, launched with cuLaunchKernel (driver API, no libcudart).
//
// Two force paths:
//   accel   -- direct O(N^2), kept as ground truth (--exact / --verify)
//   Barnes-Hut O(N log N):
//     bounds        -- global AABB
//     morton        -- 30-bit Z-order key per body
//     radix_*       -- LSD radix sort (key, body-index) by Morton key, 4 x 8-bit passes
//     karras        -- binary radix tree over the sorted keys (Karras 2012)
//     com_init      -- leaf nodes = their body
//     com_rollup    -- bottom-up centre-of-mass + mass per internal node
//     bh_force      -- warp-cooperative stackless traversal with the s/d < theta test
//
// Node id space [0, 2N-1): leaf k = id k; internal j = id N+j; root = internal 0 = id N.
// A child id < N is a leaf, >= N an internal node.
//
// Units (body.h): length = 1 kpc, mass = 1 solar mass. `g` is the scaled G'.

#include "body.h"

// ---------------------------------------------------------------------------------------
// direct O(N^2)  (kept)
// ---------------------------------------------------------------------------------------

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
        const f32 s = b[j].mass * inv * inv * inv;
        ax += dx * s;
        ay += dy * s;
        az += dz * s;
    }
    acc[i * 3u + 0u] = g * ax;
    acc[i * 3u + 1u] = g * ay;
    acc[i * 3u + 2u] = g * az;
}

// ---------------------------------------------------------------------------------------
// integrate + pack  (kept)
// ---------------------------------------------------------------------------------------

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

// Body -> PointInstance (8 floats): [0..2] pos  [3] size  [4..7] rgba. Cosmetic.
extern "C" __global__ void pack(const Body* b, f32* out, u32 n) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    f32* o = out + static_cast<usize>(i) * 8u;
    o[0] = b[i].pos[0];
    o[1] = b[i].pos[1];
    o[2] = b[i].pos[2];

    const f32 t = __saturatef((b[i].mass - 0.08f) / (100.0f - 0.08f));
    const f32 ts = powf(t, 0.35f);
    o[3] = 0.04f + 0.22f * ts;
    o[4] = 1.00f - 0.30f * ts;
    o[5] = 0.75f + 0.15f * ts;
    o[6] = 0.55f + 0.45f * ts;
    o[7] = 0.25f + 2.50f * powf(t, 0.7f);
}

// ---------------------------------------------------------------------------------------
// Barnes-Hut: bounds + Morton
// ---------------------------------------------------------------------------------------

// Single block. bounds must be pre-set to {+inf,+inf,+inf,-inf,-inf,-inf} (host memset).
extern "C" __global__ void bounds(const Body* b, u32 n, f32* out) {
    __shared__ f32 s_min[3][256];
    __shared__ f32 s_max[3][256];
    const u32 t = threadIdx.x;

    f32 lo[3] = {1e30f, 1e30f, 1e30f};
    f32 hi[3] = {-1e30f, -1e30f, -1e30f};
    for (u32 i = t; i < n; i += blockDim.x) {
        for (u32 a = 0; a < 3; a++) {
            const f32 v = b[i].pos[a];
            lo[a] = fminf(lo[a], v);
            hi[a] = fmaxf(hi[a], v);
        }
    }
    for (u32 a = 0; a < 3; a++) {
        s_min[a][t] = lo[a];
        s_max[a][t] = hi[a];
    }
    __syncthreads();

    for (u32 stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (t < stride) {
            for (u32 a = 0; a < 3; a++) {
                s_min[a][t] = fminf(s_min[a][t], s_min[a][t + stride]);
                s_max[a][t] = fmaxf(s_max[a][t], s_max[a][t + stride]);
            }
        }
        __syncthreads();
    }
    if (t == 0) {
        for (u32 a = 0; a < 3; a++) {
            out[a] = s_min[a][0];
            out[3 + a] = s_max[a][0];
        }
    }
}

// spread the low 10 bits of v across bits 0,3,6,... (one octree axis)
__device__ __forceinline__ u32 part1by2(u32 v) {
    v &= 0x000003ffu;
    v = (v ^ (v << 16)) & 0xff0000ffu;
    v = (v ^ (v << 8)) & 0x0300f00fu;
    v = (v ^ (v << 4)) & 0x030c30c3u;
    v = (v ^ (v << 2)) & 0x09249249u;
    return v;
}

extern "C" __global__ void morton(const Body* b, u32 n, const f32* bnd, u32* keys, u32* idx) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    u32 q[3];
    for (u32 a = 0; a < 3; a++) {
        const f32 range = fmaxf(bnd[3 + a] - bnd[a], 1e-6f);
        f32 f = (b[i].pos[a] - bnd[a]) / range;
        f = fminf(fmaxf(f, 0.0f), 1.0f);
        q[a] = min((u32)(f * 1023.0f), 1023u);
    }
    keys[i] = part1by2(q[0]) | (part1by2(q[1]) << 1) | (part1by2(q[2]) << 2);
    idx[i] = i;
}

// ---------------------------------------------------------------------------------------
// Barnes-Hut: LSD radix sort of (key, idx) by key, 4 x 8-bit passes, nb blocks
// ---------------------------------------------------------------------------------------

extern "C" __global__ void radix_hist(const u32* keys, u32 n, u32 pass, u32 nb, u32* hist) {
    __shared__ u32 s_hist[256];
    const u32 t = threadIdx.x;
    s_hist[t] = 0;
    __syncthreads();

    const u32 per = (n + nb - 1u) / nb;
    const u32 begin = blockIdx.x * per;
    const u32 end = min(begin + per, n);
    for (u32 e = begin + t; e < end; e += blockDim.x) {
        const u32 d = (keys[e] >> (pass * 8u)) & 0xffu;
        atomicAdd(&s_hist[d], 1u);
    }
    __syncthreads();
    hist[blockIdx.x * 256u + t] = s_hist[t];
}

// Single block, 256 threads. start[b*256 + d] = global stable base for block b's digit d.
extern "C" __global__ void radix_scan(const u32* hist, u32 nb, u32* start) {
    __shared__ u32 s_total[256];
    __shared__ u32 s_base[256];
    const u32 d = threadIdx.x;

    u32 total = 0;
    for (u32 b = 0; b < nb; b++) {
        total += hist[b * 256u + d];
    }
    s_total[d] = total;
    __syncthreads();

    if (d == 0) {
        u32 acc = 0;
        for (u32 dd = 0; dd < 256; dd++) {
            const u32 v = s_total[dd];
            s_base[dd] = acc;
            acc += v;
        }
    }
    __syncthreads();

    u32 acc = s_base[d];
    for (u32 b = 0; b < nb; b++) {
        start[b * 256u + d] = acc;
        acc += hist[b * 256u + d];
    }
}

// One sequential thread per block keeps the sort stable. keys_out/idx_out are the alt buffers.
extern "C" __global__ void radix_scatter(const u32* keys, const u32* idx, u32 n, u32 pass, u32 nb, const u32* start,
                                         u32* keys_out, u32* idx_out) {
    __shared__ u32 s_cur[256];
    if (threadIdx.x < 256) {
        s_cur[threadIdx.x] = start[blockIdx.x * 256u + threadIdx.x];
    }
    __syncthreads();

    if (threadIdx.x != 0) {
        return;
    }
    const u32 per = (n + nb - 1u) / nb;
    const u32 begin = blockIdx.x * per;
    const u32 end = min(begin + per, n);
    for (u32 e = begin; e < end; e++) {
        const u32 key = keys[e];
        const u32 d = (key >> (pass * 8u)) & 0xffu;
        const u32 pos = s_cur[d]++;
        keys_out[pos] = key;
        idx_out[pos] = idx[e];
    }
}

// ---------------------------------------------------------------------------------------
// Barnes-Hut: Karras binary radix tree
// ---------------------------------------------------------------------------------------

// shared-prefix length of keys[i], keys[j]; -1 if j is out of range; index tie-break on equal keys
__device__ __forceinline__ int kdelta(const u32* keys, u32 n, int i, int j) {
    if (j < 0 || j >= (int)n) {
        return -1;
    }
    const u32 ki = keys[i];
    const u32 kj = keys[j];
    if (ki == kj) {
        return 32 + __clz((u32)(i ^ j));
    }
    return __clz(ki ^ kj);
}

extern "C" __global__ void karras(const u32* keys, u32 n, const f32* bnd, int* node_left, int* node_right,
                                  int* node_parent, f32* node_size2) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n - 1u) { // n-1 internal nodes
        return;
    }
    const int ii = (int)i;

    // determine the key range [first, last] this internal node covers
    int first;
    int last;
    if (ii == 0) {
        first = 0;
        last = (int)n - 1;
    } else {
        const int dl = kdelta(keys, n, ii, ii - 1);
        const int dr = kdelta(keys, n, ii, ii + 1);
        const int dir = (dr > dl) ? 1 : -1;
        const int dmin = min(dl, dr);

        int lmax = 2;
        while (kdelta(keys, n, ii, ii + lmax * dir) > dmin) {
            lmax <<= 1;
        }
        int l = 0;
        for (int t = lmax >> 1; t > 0; t >>= 1) {
            if (kdelta(keys, n, ii, ii + (l + t) * dir) > dmin) {
                l += t;
            }
        }
        const int j = ii + l * dir;
        first = min(ii, j);
        last = max(ii, j);
    }

    // split: largest gamma in [first,last) whose prefix with `first` exceeds the node prefix
    const int node_prefix = kdelta(keys, n, first, last);
    int split = first;
    int stride = last - first;
    do {
        stride = (stride + 1) >> 1;
        const int cand = split + stride;
        if (cand < last && kdelta(keys, n, first, cand) > node_prefix) {
            split = cand;
        }
    } while (stride > 1);

    const int left = (split == first) ? split : ((int)n + split);
    const int right = (split + 1 == last) ? (split + 1) : ((int)n + split + 1);
    node_left[i] = left;
    node_right[i] = right;
    node_parent[left] = (int)n + (int)i;
    node_parent[right] = (int)n + (int)i;
    if (i == 0) {
        node_parent[(int)n] = -1; // root
    }

    // node "size" from the Morton level: 3 key bits per octree level
    const int level = min(node_prefix / 3, 20);
    f32 ext = 0.0f;
    for (u32 a = 0; a < 3; a++) {
        ext = fmaxf(ext, bnd[3 + a] - bnd[a]);
    }
    const f32 edge = ext / (f32)(1u << level);
    node_size2[i] = edge * edge;
}

// ---------------------------------------------------------------------------------------
// Barnes-Hut: centre-of-mass rollup
// ---------------------------------------------------------------------------------------

extern "C" __global__ void com_init(const Body* b, const u32* idx, u32 n, f32* node_com, f32* node_mass) {
    const u32 k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) {
        return;
    }
    const u32 bi = idx[k];
    node_com[k * 3u + 0u] = b[bi].pos[0];
    node_com[k * 3u + 1u] = b[bi].pos[1];
    node_com[k * 3u + 2u] = b[bi].pos[2];
    node_mass[k] = b[bi].mass;
}

// One thread per leaf walks to the root; the second child to reach an internal node combines it.
extern "C" __global__ void com_rollup(u32 n, const int* node_parent, const int* node_left, const int* node_right,
                                      f32* node_com, f32* node_mass, u32* node_visit) {
    const u32 k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) {
        return;
    }
    int nid = (int)k;
    for (;;) {
        const int p = node_parent[nid];
        if (p < 0) {
            break;
        }
        __threadfence(); // publish this thread's writes to node p's already-combined child
        const u32 arrived = atomicAdd(&node_visit[p - (int)n], 1u);
        if (arrived == 0u) {
            break; // first child; the sibling will finish node p
        }
        __threadfence(); // acquire: the sibling's child-subtree writes are now visible
        const int lc = node_left[p - (int)n];
        const int rc = node_right[p - (int)n];
        const f32 ml = node_mass[lc];
        const f32 mr = node_mass[rc];
        const f32 m = ml + mr;
        const f32 inv = (m > 0.0f) ? (1.0f / m) : 0.0f;
        for (u32 a = 0; a < 3; a++) {
            node_com[p * 3u + a] = (ml * node_com[lc * 3u + a] + mr * node_com[rc * 3u + a]) * inv;
        }
        node_mass[p] = m;
        nid = p;
    }
}

// ---------------------------------------------------------------------------------------
// Barnes-Hut: warp-cooperative stackless force traversal
// ---------------------------------------------------------------------------------------

#define BH_BLOCK 256
#define BH_WARPS (BH_BLOCK / 32)
#define BH_STACK 64

extern "C" __global__ void bh_force(const Body* b, const u32* idx, u32 n, f32 g, f32 soft2, f32 theta2,
                                    const int* node_left, const int* node_right, const f32* node_size2,
                                    const f32* node_com, const f32* node_mass, f32* acc) {
    __shared__ int s_stack[BH_WARPS][BH_STACK];

    const u32 raw = blockIdx.x * BH_BLOCK + threadIdx.x;
    const u32 lane = threadIdx.x & 31u;
    int* stack = s_stack[threadIdx.x >> 5];

    const bool active = raw < n;
    const u32 bi = active ? raw : 0u;
    const f32 px = b[bi].pos[0];
    const f32 py = b[bi].pos[1];
    const f32 pz = b[bi].pos[2];

    f32 ax = 0.0f;
    f32 ay = 0.0f;
    f32 az = 0.0f;

    int sp = 0;
    if (lane == 0u) {
        stack[sp] = (int)n; // root
    }
    sp = 1;
    __syncwarp();

    while (sp > 0) {
        sp--;
        const int node = stack[sp];
        __syncwarp();

        const f32 cx = node_com[node * 3 + 0];
        const f32 cy = node_com[node * 3 + 1];
        const f32 cz = node_com[node * 3 + 2];
        const f32 cm = node_mass[node];
        const f32 dx = cx - px;
        const f32 dy = cy - py;
        const f32 dz = cz - pz;
        const f32 d2 = dx * dx + dy * dy + dz * dz;

        if (node < (int)n) {
            // leaf: exact softened pull, minus self
            if (active && idx[node] != bi) {
                const f32 r2 = d2 + soft2;
                const f32 s = cm * rsqrtf(r2) * rsqrtf(r2) * rsqrtf(r2);
                ax += dx * s;
                ay += dy * s;
                az += dz * s;
            }
            continue;
        }

        const f32 sz2 = node_size2[node - (int)n];
        const bool need = active && (sz2 >= theta2 * d2);
        if (__any_sync(0xffffffffu, need) && sp <= BH_STACK - 3) {
            if (lane == 0u) {
                stack[sp] = node_left[node - (int)n];
                stack[sp + 1] = node_right[node - (int)n];
            }
            sp += 2;
            __syncwarp();
        } else {
            // far enough (or stack full): one pseudo-body at the node COM
            const f32 r2 = d2 + soft2;
            const f32 s = cm * rsqrtf(r2) * rsqrtf(r2) * rsqrtf(r2);
            ax += dx * s;
            ay += dy * s;
            az += dz * s;
        }
    }

    if (active) {
        acc[bi * 3u + 0u] = g * ax;
        acc[bi * 3u + 1u] = g * ay;
        acc[bi * 3u + 2u] = g * az;
    }
}

// ---------------------------------------------------------------------------------------
// --verify: relative acceleration error of BH vs exact
// ---------------------------------------------------------------------------------------

// err must be pre-zeroed. err[0] += |a_bh - a_ex|^2 ; err[1] += |a_ex|^2 (host reports the
// RMS relative error sqrt(err[0]/err[1]) -- the standard N-body metric, robust to
// individual near-force-null particles). err[2] = max per-particle relative error with a
// floored denominator, for a worst-case feel.
extern "C" __global__ void verify(const f32* a_bh, const f32* a_ex, u32 n, f32* err, f32 a_floor) {
    const u32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) {
        return;
    }
    const f32 dx = a_bh[i * 3u + 0u] - a_ex[i * 3u + 0u];
    const f32 dy = a_bh[i * 3u + 1u] - a_ex[i * 3u + 1u];
    const f32 dz = a_bh[i * 3u + 2u] - a_ex[i * 3u + 2u];
    const f32 d2 = dx * dx + dy * dy + dz * dz;
    const f32 ex2 = a_ex[i * 3u + 0u] * a_ex[i * 3u + 0u] + a_ex[i * 3u + 1u] * a_ex[i * 3u + 1u] +
                    a_ex[i * 3u + 2u] * a_ex[i * 3u + 2u];

    atomicAdd(&err[0], d2);
    atomicAdd(&err[1], ex2);
    const f32 rel = sqrtf(d2) / fmaxf(sqrtf(ex2), a_floor);
    atomicMax((int*)&err[2], __float_as_int(rel)); // rel >= 0 -> int order matches float order
}
