#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <app/debug_ui.hpp>
#include <gpu/shared_instance_buffer.hpp>
#include <graphics/presenter.hpp>
#include <graphics/primitives/scene/camera.hpp>
#include <graphics/primitives/scene/point.hpp>
#include <graphics/renderers/point_renderer.hpp>

#include "body.h" // struct Body (+ <common.h> primitives)

#include <cuda.h>

#include <imgui.h>

#include <sokol_app.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

// Galaxy simulation -- GPU N-body.
//
// A render thread and an independent CUDA sim app::Worker share one instance buffer via
// gpu::SharedInstanceBuffer in Sync::SimPriority: the sim owns a private Body[] buffer it
// integrates at whatever rate it likes; a pack kernel turns a snapshot into the
// PointInstance ring the renderer draws only when the renderer asks (publishIfRequested).
//
// Gravity is Barnes-Hut O(N log N) by default (build a Karras binary radix tree over Morton
// codes each step, traverse it with the s/d < theta opening test); --exact falls back to
// the direct O(N^2) kernel, --verify runs both and reports the acceleration error.
//
// Scaled units (see body.h): length = 1 kpc, mass = 1 solar mass. G' is the scaled
// gravitational constant; the time unit is not fixed yet, so G' and dt are degenerate
// (only their product shapes the trajectory) -- both are CLI knobs.
//
//   ./run galaxy_sim [--count N] [--frames N] [--substeps K] [--tree-every K]
//                    [--dt X] [--softening X] [--omega X] [--gravity X] [--theta X]
//                    [--exact] [--verify]

namespace {

using graphics::primitives::scene::Camera;
using graphics::primitives::scene::PointInstance;

constexpr f32 kMoveSpeed = 20.0f;
constexpr f32 kLookSensitivity = 0.15f;
constexpr f32 kPitchLimit = 1.5533f;
constexpr f32 kTwoPi = 6.28318531f;

constexpr u32 kRadixBlocks = 512; // blocks for the radix histogram/scatter (hist/start are kRadixBlocks*256)

// --- scaled units ------------------------------------------------------------------------
// length: 1 unit = 1 kpc (3.0857e19 m)   mass: 1 unit = 1 solar mass (1.98e30 kg)
// gravity: G' = G_SI * Msun * T^2 / L^3. The time unit T is deferred ("time scaling later"),
// so G' is a free knob -- dt absorbs the remaining freedom. 1.0 corresponds to
// T = sqrt(L^3 / (G_SI*Msun)) ~ 472 Gyr.
constexpr f32 kGravityDefault = 1.0f;
constexpr f32 kDiscRadius = 15.0f;  // kpc
constexpr f32 kMassMin = 0.08f;     // solar masses
constexpr f32 kMassMax = 100.0f;

const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

struct Sandbox {
        u32 count = 1000;
        u32 max_frames = 0; // 0 = run until the window closes
        u32 substeps = 16;
        u32 tree_every = 1; // rebuild the tree every N substeps; refresh COMs on the others

        f32 dt = 0.004f;
        f32 softening = 0.6f;
        f32 gravity = kGravityDefault;
        f32 omega = 0.0f; // CLI: 0 => auto (sqrt(G' * M_tot / R^3)); resolved in generateDisc
        f32 theta = 0.5f; // Barnes-Hut opening angle
        f32 total_mass = 0.0f;

        bool exact = false;  // O(N^2) instead of Barnes-Hut
        bool verify = false; // also run O(N^2) each rate-tick, report the error

        std::optional<graphics::Presenter> presenter;
        std::optional<graphics::renderers::PointRenderer> renderer;
        std::optional<Camera> camera;
        std::unique_ptr<gpu::SharedInstanceBuffer> shared;

        std::vector<Body> seed;

        std::atomic<bool> compute_ok{false};
        std::atomic<u64> sim_iters{0};
        std::atomic<f32> err_max{0.0f};
        std::atomic<f32> err_rms{0.0f};
        u64 render_frames = 0;

        f32 exposure = 0.9f;
        f32 yaw = 0.0f;
        f32 pitch = -0.42f;
        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point rate_mark = std::chrono::steady_clock::now();
        u64 sim_at_mark = 0;
        u64 frames_at_mark = 0;
        f32 sim_rate = 0.0f;
        f32 frame_rate = 0.0f;

        // The app::AppState shape: the constructor is init (GPU up, workers not yet
        // started), frame() runs every frame, the destructor is cleanup (workers joined,
        // GPU still up).
        explicit Sandbox(app::Context& app);
        ~Sandbox();
        void frame(app::Context& app);

        Sandbox(const Sandbox&) = delete;
        Sandbox& operator=(const Sandbox&) = delete;
};

// A disc of stars: uniform-area radial density, thin vertical scatter, uniform-random mass.
// Every star gets a tangential velocity with the SAME angular velocity omega (rigid
// rotation, v = Omega x r), so inner stars start sub-circular and the disc winds up.
void generateDisc(Sandbox& sandbox) {
    std::mt19937 rng{4242};
    std::uniform_real_distribution<f32> unit{0.0f, 1.0f};
    std::normal_distribution<f32> height{0.0f, 0.03f * kDiscRadius};
    std::uniform_real_distribution<f32> mass{kMassMin, kMassMax};

    sandbox.seed.resize(sandbox.count);
    f32 total_mass = 0.0f;

    for (u32 i = 0; i < sandbox.count; i++) {
        const f32 r = std::sqrt(unit(rng)) * kDiscRadius;
        const f32 theta = kTwoPi * unit(rng);

        Body& b = sandbox.seed[i];
        b.pos[0] = r * std::cos(theta);
        b.pos[1] = height(rng);
        b.pos[2] = r * std::sin(theta);
        b.mass = mass(rng);
        b.pad = 0.0f;

        total_mass += b.mass;
    }
    sandbox.total_mass = total_mass;

    if (sandbox.omega <= 0.0f) {
        sandbox.omega = std::sqrt(sandbox.gravity * total_mass / (kDiscRadius * kDiscRadius * kDiscRadius));
    }
    const f32 w = sandbox.omega;

    for (u32 i = 0; i < sandbox.count; i++) {
        Body& b = sandbox.seed[i];
        b.vel[0] = -w * b.pos[2];
        b.vel[1] = 0.0f;
        b.vel[2] = w * b.pos[0];
    }
}

// ---- CUDA kernels + device scratch -----------------------------------------------------

struct Kernels {
        CUfunction accel = nullptr, step = nullptr, pack = nullptr;
        CUfunction bounds = nullptr, morton = nullptr;
        CUfunction radix_hist = nullptr, radix_scan = nullptr, radix_scatter = nullptr;
        CUfunction karras = nullptr, com_init = nullptr, com_rollup = nullptr;
        CUfunction bh_force = nullptr, verify = nullptr;
};

bool loadKernels(CUmodule mod, Kernels& k) {
    struct Ent {
            CUfunction* fn;
            const char* name;
    };
    const Ent ents[] = {
        {&k.accel, "accel"},        {&k.step, "step"},
        {&k.pack, "pack"},          {&k.bounds, "bounds"},
        {&k.morton, "morton"},      {&k.radix_hist, "radix_hist"},
        {&k.radix_scan, "radix_scan"}, {&k.radix_scatter, "radix_scatter"},
        {&k.karras, "karras"},      {&k.com_init, "com_init"},
        {&k.com_rollup, "com_rollup"}, {&k.bh_force, "bh_force"},
        {&k.verify, "verify"},
    };
    for (const Ent& e : ents) {
        if (cuModuleGetFunction(e.fn, mod, e.name) != CUDA_SUCCESS) {
            RLOG(LL_ERROR, "galaxy_sim: missing kernel %s", e.name);
            return false;
        }
    }
    return true;
}

// All device-side working memory for the Barnes-Hut pipeline. Sandbox-owned, one alloc per
// field; freed in freeTree. Node id space [0, 2N): leaf k = k, internal j = N+j, root = N.
struct TreeScratch {
        CUdeviceptr keys = 0, keys_alt = 0, idx = 0, idx_alt = 0; // radix sort of (Morton key, body index)
        CUdeviceptr hist = 0, start = 0;                          // radix histogram + global offsets
        CUdeviceptr bnd = 0;                                      // 6 floats: min xyz, max xyz
        CUdeviceptr node_left = 0, node_right = 0, node_parent = 0, node_size2 = 0;
        CUdeviceptr node_com = 0, node_mass = 0, node_visit = 0;
        CUdeviceptr accel = 0, accel_exact = 0, err = 0;
};

void freeTree(TreeScratch& t) {
    for (CUdeviceptr* p : {&t.keys, &t.keys_alt, &t.idx, &t.idx_alt, &t.hist, &t.start, &t.bnd, &t.node_left,
                           &t.node_right, &t.node_parent, &t.node_size2, &t.node_com, &t.node_mass, &t.node_visit,
                           &t.accel, &t.accel_exact, &t.err}) {
        if (*p != 0) {
            cuMemFree(*p);
            *p = 0;
        }
    }
}

bool allocTree(TreeScratch& t, u32 n, bool verify) {
    const usize u = sizeof(u32);
    const usize f = sizeof(f32);
    const auto A = [](CUdeviceptr& p, usize bytes) { return cuMemAlloc(&p, bytes) == CUDA_SUCCESS; };
    const bool ok =
        A(t.keys, n * u) && A(t.keys_alt, n * u) && A(t.idx, n * u) && A(t.idx_alt, n * u) &&
        A(t.hist, kRadixBlocks * 256u * u) && A(t.start, kRadixBlocks * 256u * u) && A(t.bnd, 8u * f) &&
        A(t.node_left, n * sizeof(int)) && A(t.node_right, n * sizeof(int)) && A(t.node_parent, 2u * n * sizeof(int)) &&
        A(t.node_size2, n * f) && A(t.node_com, 2u * n * 3u * f) && A(t.node_mass, 2u * n * f) &&
        A(t.node_visit, n * u) && A(t.accel, 3u * n * f) && A(t.err, 3u * f) &&
        (!verify || A(t.accel_exact, 3u * n * f));
    if (!ok) {
        freeTree(t);
    }
    return ok;
}

void launch(CUfunction fn, u32 grid, u32 block, CUstream st, void** args) {
    cuLaunchKernel(fn, grid, 1, 1, block, 1, 1, 0, st, args, nullptr);
}

// One Barnes-Hut acceleration into t.accel. `rebuild` = re-sort + re-build the tree;
// otherwise only the centre-of-mass rollup is refreshed on the previous topology.
void bhAccel(const Kernels& k, TreeScratch& t, CUdeviceptr bodies, u32 n, f32 g, f32 soft2, f32 theta2, bool rebuild,
             CUstream st) {
    const u32 B = 256;
    const u32 gridN = (n + B - 1) / B;

    if (rebuild) {
        cuMemsetD32Async(t.bnd, 0x7f800000u, 3, st);                    // +inf
        cuMemsetD32Async(t.bnd + 3u * sizeof(f32), 0xff800000u, 3, st); // -inf
        void* ba[] = {&bodies, &n, &t.bnd};
        launch(k.bounds, 1, 256, st, ba);

        void* ma[] = {&bodies, &n, &t.bnd, &t.keys, &t.idx};
        launch(k.morton, gridN, B, st, ma);

        CUdeviceptr ki = t.keys, ka = t.keys_alt, ii = t.idx, ia = t.idx_alt;
        for (u32 p = 0; p < 4; p++) {
            u32 nb = kRadixBlocks;
            void* ha[] = {&ki, &n, &p, &nb, &t.hist};
            launch(k.radix_hist, nb, 256, st, ha);
            void* sa[] = {&t.hist, &nb, &t.start};
            launch(k.radix_scan, 1, 256, st, sa);
            void* ca[] = {&ki, &ii, &n, &p, &nb, &t.start, &ka, &ia};
            launch(k.radix_scatter, nb, 256, st, ca);
            std::swap(ki, ka);
            std::swap(ii, ia); // 4 passes -> sorted data ends back in t.keys / t.idx
        }

        void* ka2[] = {&t.keys, &n, &t.bnd, &t.node_left, &t.node_right, &t.node_parent, &t.node_size2};
        launch(k.karras, (n - 1 + B - 1) / B, B, st, ka2);
    }

    cuMemsetD32Async(t.node_visit, 0u, n - 1, st);
    void* ci[] = {&bodies, &t.idx, &n, &t.node_com, &t.node_mass};
    launch(k.com_init, gridN, B, st, ci);
    void* cr[] = {&n, &t.node_parent, &t.node_left, &t.node_right, &t.node_com, &t.node_mass, &t.node_visit};
    launch(k.com_rollup, gridN, B, st, cr);

    void* fa[] = {&bodies, &t.idx,      &n,          &g,          &soft2,     &theta2,
                  &t.node_left, &t.node_right, &t.node_size2, &t.node_com, &t.node_mass, &t.accel};
    launch(k.bh_force, gridN, B, st, fa);
}

void exactAccel(const Kernels& k, CUdeviceptr out_accel, CUdeviceptr bodies, u32 n, f32 g, f32 soft2, CUstream st) {
    void* a[] = {&bodies, &out_accel, &n, &g, &soft2};
    launch(k.accel, (n + 255) / 256, 256, st, a);
}

// ---- CUDA simulation worker (own thread; started + joined by Application) --------------

void simWorker(app::Context& app, Sandbox*& sptr) {
    Sandbox& s = *sptr;

    if (!s.shared->attachCuda()) {
        RLOG(LL_ERROR, "galaxy_sim: attachCuda failed");
        return;
    }

    CUmodule module = nullptr;
    Kernels k;
    if (cuModuleLoad(&module, ".build/cuda/galaxy_sim.ptx") != CUDA_SUCCESS || !loadKernels(module, k)) {
        RLOG(LL_ERROR, "galaxy_sim: failed to load galaxy_sim.ptx");
        s.shared->detachCuda();
        return;
    }

    const u32 count = s.count;
    const u32 block = 256;
    const u32 grid = (count + block - 1) / block;
    const bool bh_ok = count >= 2; // Karras needs >= 1 internal node

    TreeScratch tree;
    if (!allocTree(tree, count, s.verify)) {
        RLOG(LL_ERROR, "galaxy_sim: tree scratch alloc failed");
        cuModuleUnload(module);
        s.shared->detachCuda();
        return;
    }

    s.shared->setPublishPack([kp = k.pack, count, grid, block](CUdeviceptr src, CUdeviceptr dst, u32 /*n*/,
                                                               CUstream stream) {
        u32 n = count;
        void* args[] = {&src, &dst, &n};
        cuLaunchKernel(kp, grid, 1, 1, block, 1, 1, 0, stream, args, nullptr);
    });

    if (!s.shared->primeSimState(s.seed.data(), static_cast<usize>(count) * sizeof(Body))) {
        RLOG(LL_ERROR, "galaxy_sim: primeSimState failed");
        freeTree(tree);
        cuModuleUnload(module);
        s.shared->detachCuda();
        return;
    }
    cuCtxSynchronize();
    s.compute_ok.store(true, std::memory_order_release);

    const CUstream stream = s.shared->simStream();

    u64 substep = 0;
    u64 cycle = 0;

    while (!app.workerShouldStop()) {
        CUdeviceptr bodies = s.shared->simStatePtr();
        u32 n = count;                 // non-const: its address goes into cuLaunchKernel arg arrays
        f32 dt = s.dt;                 // non-const: ditto
        const f32 g = s.gravity;
        const f32 soft2 = s.softening * s.softening;
        const f32 theta2 = s.theta * s.theta;
        const u32 tree_every = std::max(s.tree_every, 1u);
        const bool use_bh = bh_ok && !s.exact;

        for (u32 i = 0; i < s.substeps; i++) {
            if (use_bh) {
                bhAccel(k, tree, bodies, n, g, soft2, theta2, (substep % tree_every) == 0, stream);
            } else {
                exactAccel(k, tree.accel, bodies, n, g, soft2, stream);
            }
            void* step_args[] = {&bodies, &tree.accel, &n, &dt};
            launch(k.step, grid, block, stream, step_args);
            substep++;
        }

        s.shared->publishIfRequested();
        s.sim_iters.fetch_add(s.substeps, std::memory_order_relaxed);

        if (s.verify && use_bh) {
            // Re-evaluate BH and exact on the SAME (current) positions -- tree.accel from the
            // substep loop is one `step` stale, which would masquerade as an O(few %) error
            // that never shrinks with theta.
            f32 zero[3] = {0.0f, 0.0f, 0.0f};
            cuMemcpyHtoDAsync(tree.err, zero, sizeof(zero), stream);
            bhAccel(k, tree, bodies, n, g, soft2, theta2, true, stream);
            exactAccel(k, tree.accel_exact, bodies, n, g, soft2, stream);
            f32 a_floor = 0.02f * g * s.total_mass / (kDiscRadius * kDiscRadius); // ~2% of a typical accel
            void* va[] = {&tree.accel, &tree.accel_exact, &n, &tree.err, &a_floor};
            launch(k.verify, grid, block, stream, va);
            f32 err[3] = {0.0f, 0.0f, 0.0f};
            cuMemcpyDtoHAsync(err, tree.err, sizeof(err), stream);
            cuStreamSynchronize(stream);
            s.err_rms.store(std::sqrt(err[0] / std::max(err[1], 1e-30f)), std::memory_order_relaxed);
            s.err_max.store(err[2], std::memory_order_relaxed);
        }

        if (((++cycle) & 0x3F) == 0) {
            s.shared->cuda().sync(stream);
        }
    }

    s.shared->cuda().sync(stream);
    freeTree(tree);
    cuModuleUnload(module);
    s.shared->detachCuda();
}

// ---- render thread -------------------------------------------------------------------

void flyCamera(app::Context& app, Sandbox& s, f32 dt) {
    Camera& camera = *s.camera;
    const Eigen::Vector3f forward = camera.direction().normalized();
    const Eigen::Vector3f right = forward.cross(kWorldUp).normalized();

    if (app.input().isKeyDown(SAPP_KEYCODE_W)) *camera.mutablePosition() += forward * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_S)) *camera.mutablePosition() -= forward * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_D)) *camera.mutablePosition() += right * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_A)) *camera.mutablePosition() -= right * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_SPACE)) *camera.mutablePosition() += kWorldUp * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_LEFT_CONTROL)) *camera.mutablePosition() -= kWorldUp * kMoveSpeed * dt;

    const Eigen::Vector2f md = app.input().mouseDelta();
    if (app.isCursorCaptured() && (md.x() != 0.0f || md.y() != 0.0f)) {
        s.yaw += md.x() * kLookSensitivity * dt;
        s.pitch = std::clamp(s.pitch - md.y() * kLookSensitivity * dt, -kPitchLimit, kPitchLimit);
    }
    *camera.mutableDirection() = Eigen::Vector3f{
        std::cos(s.pitch) * std::sin(s.yaw),
        std::sin(s.pitch),
        -std::cos(s.pitch) * std::cos(s.yaw),
    };
}

// The command line, read once the app is up -- the first thing the constructor does.
void parseCli(app::Context& app, Sandbox& sandbox) {
    app::CliParser cli{app.argc(), app.argv()};
    cli.defineArg<u32>("count");
    cli.defineArg<u32>("frames");
    cli.defineArg<u32>("substeps");
    cli.defineArg<u32>("tree-every");
    cli.defineArg<f32>("dt");
    cli.defineArg<f32>("softening");
    cli.defineArg<f32>("omega");
    cli.defineArg<f32>("gravity");
    cli.defineArg<f32>("theta");
    cli.defineArg<bool>("exact");
    cli.defineArg<bool>("verify");
    cli.parse();

    if (const auto v = cli.arg<u32>("count"); v && *v > 0) {
        sandbox.count = *v;
    }
    if (const auto v = cli.arg<u32>("frames"); v && *v > 0) {
        sandbox.max_frames = *v;
    }
    if (const auto v = cli.arg<u32>("substeps"); v && *v > 0) {
        sandbox.substeps = *v;
    }
    if (const auto v = cli.arg<u32>("tree-every"); v && *v > 0) {
        sandbox.tree_every = *v;
    }
    if (const auto v = cli.arg<f32>("dt"); v && *v > 0.0f) {
        sandbox.dt = *v;
    }
    if (const auto v = cli.arg<f32>("softening"); v && *v > 0.0f) {
        sandbox.softening = *v;
    }
    if (const auto v = cli.arg<f32>("omega"); v && *v > 0.0f) {
        sandbox.omega = *v;
    }
    if (const auto v = cli.arg<f32>("gravity"); v && *v > 0.0f) {
        sandbox.gravity = *v;
    }
    if (const auto v = cli.arg<f32>("theta"); v && *v > 0.0f) {
        sandbox.theta = *v;
    }
    if (const auto v = cli.arg<bool>("exact"); v && *v) {
        sandbox.exact = true;
    }
    if (const auto v = cli.arg<bool>("verify"); v && *v) {
        sandbox.verify = true;
    }
}

Sandbox::Sandbox(app::Context& app) {
    Sandbox& sandbox = *this;
    parseCli(app, sandbox);

    sandbox.presenter.emplace();
    sandbox.renderer.emplace();
    sandbox.renderer->setExposure(sandbox.exposure);

    gpu::SharedInstanceBuffer::Desc desc = {};
    desc.instance_bytes = sizeof(PointInstance);
    desc.max_instances = sandbox.count;
    desc.sync = gpu::SharedInstanceBuffer::Sync::SimPriority;
    sandbox.shared = gpu::SharedInstanceBuffer::create(desc);
    if (!sandbox.shared) {
        RLOG(LL_FATAL, "galaxy_sim: SharedInstanceBuffer::create failed");
    }

    generateDisc(sandbox);

    const f32 aspect = static_cast<f32>(app.width()) / static_cast<f32>(app.height());
    sandbox.camera.emplace(
        1.0472f, aspect, 0.5f, 500.0f, Eigen::Vector3f{0.0f, 26.0f, 46.0f}, Eigen::Vector3f{0.0f, -0.46f, -0.89f}, kWorldUp
    );

    app.addWorker(simWorker, &sandbox);

    RLOG(LL_INFO, "galaxy_sim: %u stars, M_tot=%.0f Msun, omega=%.3f, dt=%.4f, eps=%.2f, G'=%.3f, theta=%.2f, %s",
         sandbox.count, sandbox.total_mass, sandbox.omega, sandbox.dt, sandbox.softening, sandbox.gravity,
         sandbox.theta, sandbox.exact ? "exact O(N^2)" : "Barnes-Hut");

    sandbox.last_time = std::chrono::steady_clock::now();
    sandbox.rate_mark = sandbox.last_time;
}

void Sandbox::frame(app::Context& app) {
    Sandbox& sandbox = *this;

    const auto now = std::chrono::steady_clock::now();
    const f32 dt = std::chrono::duration<f32>(now - sandbox.last_time).count();
    sandbox.last_time = now;

    if (app.height() > 0) {
        sandbox.camera->setAspectRatio(static_cast<f32>(app.width()) / static_cast<f32>(app.height()));
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_ESCAPE)) {
        app.toggleCursorCapture();
    }
    flyCamera(app, sandbox, dt);

    sandbox.render_frames++;
    if (sandbox.max_frames > 0 && sandbox.render_frames >= sandbox.max_frames) {
        app.quit();
    }

    const f32 since_mark = std::chrono::duration<f32>(now - sandbox.rate_mark).count();
    if (since_mark >= 0.5f) {
        const u64 iters = sandbox.sim_iters.load(std::memory_order_relaxed);
        sandbox.sim_rate = static_cast<f32>(iters - sandbox.sim_at_mark) / since_mark;
        sandbox.frame_rate = static_cast<f32>(sandbox.render_frames - sandbox.frames_at_mark) / since_mark;
        sandbox.sim_at_mark = iters;
        sandbox.frames_at_mark = sandbox.render_frames;
        sandbox.rate_mark = now;
    }

    graphics::renderers::PointRenderer& renderer = *sandbox.renderer;
    renderer.beginFrame(*sandbox.camera);
    renderer.setExposure(sandbox.exposure);

    gpu::BufferHandle instances = sandbox.shared->consumeBegin();
    renderer.adoptInstances(instances, sandbox.count);

    sandbox.presenter->present(renderer, [&]() {
        app.debugUI().draw(app.width(), app.height(), dt, sapp_dpi_scale(), [&]() {
            if (ImGui::Begin("galaxy_sim")) {
                ImGui::Text("%u stars   %s", sandbox.count, sandbox.exact ? "O(N^2) direct" : "Barnes-Hut O(N log N)");
                ImGui::Text("sim iters/sec : %.0f", sandbox.sim_rate);
                ImGui::Text("render fps    : %.0f", sandbox.frame_rate);
                if (sandbox.verify && !sandbox.exact) {
                    ImGui::Text("accel err     : rms %.2f%%  max %.1f%%",
                                100.0f * sandbox.err_rms.load(std::memory_order_relaxed),
                                100.0f * sandbox.err_max.load(std::memory_order_relaxed));
                }
                ImGui::Separator();
                ImGui::Text("M_tot  %.0f Msun   omega %.3f", sandbox.total_mass, sandbox.omega);
                if (!sandbox.exact) {
                    ImGui::SliderFloat("theta", &sandbox.theta, 0.05f, 1.5f, "%.2f");
                }
                ImGui::SliderFloat("dt", &sandbox.dt, 0.0002f, 0.03f, "%.4f");
                ImGui::SliderFloat("softening (eps)", &sandbox.softening, 0.05f, 4.0f, "%.2f");
                ImGui::SliderFloat("G'", &sandbox.gravity, 0.05f, 8.0f, "%.2f");
                ImGui::SliderFloat("exposure", &sandbox.exposure, 0.05f, 3.0f, "%.2f");
                if (!sandbox.compute_ok.load(std::memory_order_acquire)) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "sim thread not running");
                }
            }
            ImGui::End();
        });
    });

    sandbox.shared->consumeEnd();
}

Sandbox::~Sandbox() {
    Sandbox& sandbox = *this;

    RLOG(LL_INFO, "galaxy_sim: %llu sim iters over %llu frames (last: %.0f iters/s, %.0f fps)",
         static_cast<unsigned long long>(sandbox.sim_iters.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(sandbox.render_frames), sandbox.sim_rate, sandbox.frame_rate);
    if (sandbox.verify && !sandbox.exact) {
        RLOG(LL_INFO, "galaxy_sim: accel error vs exact -- rms %.3f%%  max %.2f%%",
             100.0f * sandbox.err_rms.load(std::memory_order_relaxed),
             100.0f * sandbox.err_max.load(std::memory_order_relaxed));
    }

    sandbox.presenter.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
    sandbox.shared.reset();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting galaxy_sim");

    app::Application<Sandbox> app{{}, argc, argv};
    app.run();

    return 0;
}
