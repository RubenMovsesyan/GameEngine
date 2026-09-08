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

// Galaxy simulation -- naive direct O(N^2) gravity.
//
// A render thread and an independent CUDA sim app::Worker share one instance buffer via
// gpu::SharedInstanceBuffer in Sync::SimPriority: the sim owns a private Body[] buffer it
// integrates at whatever rate it likes, and a pack kernel turns a snapshot into the
// PointInstance ring the renderer draws only when the renderer asks (publishIfRequested).
//
// Scaled units (see body.h): length = 1 kpc, mass = 1 solar mass. G' is the scaled
// gravitational constant; the time unit is not fixed yet, so G' and dt are degenerate for
// now (only their product shapes the trajectory) -- both are CLI knobs.
//
//   ./run galaxy_sim [--count N] [--frames N] [--substeps K]
//                    [--dt X] [--softening X] [--omega X] [--gravity X]

namespace {

using graphics::primitives::scene::Camera;
using graphics::primitives::scene::PointInstance;

constexpr f32 kMoveSpeed = 20.0f;
constexpr f32 kLookSensitivity = 0.15f;
constexpr f32 kPitchLimit = 1.5533f;
constexpr f32 kTwoPi = 6.28318531f;

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

        f32 dt = 0.004f;
        f32 softening = 0.6f;
        f32 gravity = kGravityDefault;
        f32 omega = 0.0f;   // CLI: 0 => auto (sqrt(G' * M_tot / R^3)); resolved in generateDisc
        f32 total_mass = 0.0f;

        std::optional<graphics::Presenter> presenter;
        std::optional<graphics::renderers::PointRenderer> renderer;
        std::optional<Camera> camera;
        std::unique_ptr<gpu::SharedInstanceBuffer> shared;

        std::vector<Body> seed;

        std::atomic<bool> compute_ok{false};
        std::atomic<u64> sim_iters{0};
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

    // omega: CLI override, else rim ~ circular for the drawn mass.
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

// ---- CUDA simulation worker (own thread; started + joined by Application) --------------

void simWorker(app::Application& app, Sandbox*& sptr) {
    Sandbox& s = *sptr;

    if (!s.shared->attachCuda()) {
        RLOG(LL_ERROR, "galaxy_sim: attachCuda failed");
        return;
    }

    CUmodule module = nullptr;
    CUfunction k_accel = nullptr;
    CUfunction k_step = nullptr;
    CUfunction k_pack = nullptr;
    if (cuModuleLoad(&module, ".build/cuda/galaxy_sim.ptx") != CUDA_SUCCESS ||
        cuModuleGetFunction(&k_accel, module, "accel") != CUDA_SUCCESS ||
        cuModuleGetFunction(&k_step, module, "step") != CUDA_SUCCESS ||
        cuModuleGetFunction(&k_pack, module, "pack") != CUDA_SUCCESS) {
        RLOG(LL_ERROR, "galaxy_sim: failed to load galaxy_sim.ptx");
        s.shared->detachCuda();
        return;
    }

    const u32 count = s.count;
    const u32 block = 256;
    const u32 grid = (count + block - 1) / block;

    // Scratch accelerations, one float3 per body (count * 3 floats). Sandbox-owned.
    CUdeviceptr d_accel = 0;
    if (cuMemAlloc(&d_accel, static_cast<usize>(count) * 3u * sizeof(f32)) != CUDA_SUCCESS) {
        RLOG(LL_ERROR, "galaxy_sim: cuMemAlloc(d_accel) failed");
        cuModuleUnload(module);
        s.shared->detachCuda();
        return;
    }

    // Body -> PointInstance for a ring slot.
    s.shared->setPublishPack([k_pack, count, grid, block](CUdeviceptr sim_state, CUdeviceptr ring_slot, u32 /*n*/,
                                                          CUstream stream) {
        CUdeviceptr src = sim_state;
        CUdeviceptr dst = ring_slot;
        u32 n = count;
        void* args[] = {&src, &dst, &n};
        cuLaunchKernel(k_pack, grid, 1, 1, block, 1, 1, 0, stream, args, nullptr);
    });

    if (!s.shared->primeSimState(s.seed.data(), static_cast<usize>(count) * sizeof(Body))) {
        RLOG(LL_ERROR, "galaxy_sim: primeSimState failed");
        cuMemFree(d_accel);
        cuModuleUnload(module);
        s.shared->detachCuda();
        return;
    }
    cuCtxSynchronize();
    s.compute_ok.store(true, std::memory_order_release);

    const CUstream stream = s.shared->simStream();
    u64 cycle = 0;

    while (!app.workerShouldStop()) {
        CUdeviceptr d_sim = s.shared->simStatePtr();
        u32 n = count;
        f32 g = s.gravity;
        f32 soft2 = s.softening * s.softening;
        f32 dt = s.dt;

        for (u32 k = 0; k < s.substeps; k++) {
            void* accel_args[] = {&d_sim, &d_accel, &n, &g, &soft2};
            cuLaunchKernel(k_accel, grid, 1, 1, block, 1, 1, 0, stream, accel_args, nullptr);
            void* step_args[] = {&d_sim, &d_accel, &n, &dt};
            cuLaunchKernel(k_step, grid, 1, 1, block, 1, 1, 0, stream, step_args, nullptr);
        }

        s.shared->publishIfRequested(); // non-blocking; never waits on the renderer
        s.sim_iters.fetch_add(s.substeps, std::memory_order_relaxed);

        if (((++cycle) & 0x3F) == 0) {
            s.shared->cuda().sync(stream); // bound the sim-stream queue
        }
    }

    s.shared->cuda().sync(stream);
    cuMemFree(d_accel);
    cuModuleUnload(module);
    s.shared->detachCuda();
}

// ---- render thread -------------------------------------------------------------------

void flyCamera(app::Application& app, Sandbox& s, f32 dt) {
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

void init(app::Application& app, Sandbox& sandbox) {
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

    RLOG(LL_INFO, "galaxy_sim: %u stars, M_tot=%.0f Msun, omega=%.3f, dt=%.4f, eps=%.2f, G'=%.3f", sandbox.count,
         sandbox.total_mass, sandbox.omega, sandbox.dt, sandbox.softening, sandbox.gravity);

    sandbox.last_time = std::chrono::steady_clock::now();
    sandbox.rate_mark = sandbox.last_time;
}

void frame(app::Application& app, Sandbox& sandbox) {
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
                ImGui::Text("%u stars   N-body O(N^2) direct", sandbox.count);
                ImGui::Text("sim iters/sec : %.0f", sandbox.sim_rate);
                ImGui::Text("render fps    : %.0f", sandbox.frame_rate);
                ImGui::Separator();
                ImGui::Text("M_tot  %.0f Msun", sandbox.total_mass);
                ImGui::Text("omega  %.3f", sandbox.omega);
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

void cleanup(Sandbox& sandbox) {
    RLOG(LL_INFO, "galaxy_sim: %llu sim iters over %llu frames (last: %.0f iters/s, %.0f fps)",
         static_cast<unsigned long long>(sandbox.sim_iters.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(sandbox.render_frames), sandbox.sim_rate, sandbox.frame_rate);

    // Application has already stopped + joined the worker (detachCuda ran).
    sandbox.presenter.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
    sandbox.shared.reset();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting galaxy_sim");

    app::CliParser cli{argc, argv};
    cli.defineArg<u32>("count");
    cli.defineArg<u32>("frames");
    cli.defineArg<u32>("substeps");
    cli.defineArg<f32>("dt");
    cli.defineArg<f32>("softening");
    cli.defineArg<f32>("omega");
    cli.defineArg<f32>("gravity");
    cli.parse();

    Sandbox sandbox;
    if (const auto v = cli.arg<u32>("count"); v && *v > 0) {
        sandbox.count = *v;
    }
    if (const auto v = cli.arg<u32>("frames"); v && *v > 0) {
        sandbox.max_frames = *v;
    }
    if (const auto v = cli.arg<u32>("substeps"); v && *v > 0) {
        sandbox.substeps = *v;
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

    app::Application app{
        app::Application::Config{},
        [&sandbox](app::Application& app) { init(app, sandbox); },
        [&sandbox](app::Application& app) { frame(app, sandbox); },
        [&sandbox](app::Application&) { cleanup(sandbox); },
    };
    app.run();

    return 0;
}
