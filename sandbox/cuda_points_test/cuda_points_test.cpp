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

#include <cuda.h>

#include <imgui.h>

#include <sokol_app.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <thread>
#include <vector>

// Proof for the zero-copy CUDA <-> sokol/Vulkan shared instance buffer. A CUDA sim thread
// (an app::Worker) integrates the points on its own clock; the render thread draws the SAME
// GPU buffer with no copy; gpu::SharedInstanceBuffer keeps them off the bytes at once.
//
//   ./run cuda_points_test [--count N] [--substeps K] [--double | --sim-priority]
//
//   --double        DoubleBuffer sync: compute unthrottled, ~1 frame latency.
//   --sim-priority  the sim owns a private buffer and NEVER waits on the renderer; render
//                   samples published snapshots. Hold P to stall the render thread and
//                   watch the sim rate hold.

namespace {

using graphics::primitives::scene::Camera;
using graphics::primitives::scene::PointInstance;
using Sync = gpu::SharedInstanceBuffer::Sync;

constexpr f32 kMoveSpeed = 25.0f;
constexpr f32 kLookSensitivity = 0.15f;
constexpr f32 kPitchLimit = 1.5533f;
constexpr f32 kDiscRadius = 45.0f;
constexpr f32 kSubstepDt = 0.004f;
const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

const char* syncName(Sync s) {
    switch (s) {
        case Sync::Semaphore:
            return "semaphore";
        case Sync::DoubleBuffer:
            return "double-buffer";
        case Sync::SimPriority:
            return "sim-priority";
    }
    return "?";
}

struct Sandbox {
        u32 count = 200000;
        u32 substeps = 8;
        u32 max_frames = 0; // 0 = run until the window closes; >0 auto-quits (for tests/screens)
        Sync sync = Sync::Semaphore;

        std::optional<graphics::Presenter> presenter;
        std::optional<graphics::renderers::PointRenderer> renderer;
        std::optional<Camera> camera;
        std::unique_ptr<gpu::SharedInstanceBuffer> shared;

        std::vector<PointInstance> seed;

        std::atomic<bool> compute_ok{false};
        std::atomic<u64> sim_steps{0};
        u64 render_frames = 0;

        f32 exposure = 0.8f;
        f32 yaw = 0.0f;
        f32 pitch = -0.35f;
        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point rate_mark = std::chrono::steady_clock::now();
        u64 sim_at_mark = 0;
        u64 frames_at_mark = 0;
        f32 sim_rate = 0.0f;
        f32 frame_rate = 0.0f;
};

void generateSeed(Sandbox& sandbox) {
    std::mt19937 rng{20260906};
    std::uniform_real_distribution<f32> angle{0.0f, 6.2831853f};
    std::uniform_real_distribution<f32> unit{0.0f, 1.0f};
    std::normal_distribution<f32> height{0.0f, 1.2f};

    sandbox.seed.resize(sandbox.count);
    for (u32 i = 0; i < sandbox.count; i++) {
        const f32 r = std::sqrt(unit(rng)) * kDiscRadius;
        const f32 a = angle(rng);
        const f32 core = 1.0f - r / kDiscRadius;

        PointInstance& p = sandbox.seed[i];
        p.position = Eigen::Vector3f{r * std::cos(a), height(rng) * (0.3f + core), r * std::sin(a)};
        p.size = 0.05f + 0.16f * unit(rng);
        p.color = Eigen::Vector4f{1.0f, 0.9f, 0.8f, 0.4f + 2.0f * core * core};
    }
}

// ---- CUDA sim worker (runs on its own thread, started + joined by Application) ---------

void simWorker(app::Application& app, Sandbox*& sptr) {
    Sandbox& s = *sptr;

    if (!s.shared->attachCuda()) {
        RLOG(LL_ERROR, "cuda_points_test: attachCuda failed");
        return;
    }

    CUmodule module = nullptr;
    CUfunction integrate = nullptr;
    if (cuModuleLoad(&module, ".build/cuda/integrate.ptx") != CUDA_SUCCESS ||
        cuModuleGetFunction(&integrate, module, "integrate") != CUDA_SUCCESS) {
        RLOG(LL_ERROR, "cuda_points_test: failed to load integrate.ptx");
        s.shared->detachCuda();
        return;
    }

    const size_t bytes = static_cast<size_t>(s.count) * sizeof(PointInstance);
    const bool sim_priority = s.sync == Sync::SimPriority;

    if (sim_priority) {
        s.shared->primeSimState(s.seed.data(), bytes);
    } else {
        const u32 buffers = s.sync == Sync::DoubleBuffer ? 2u : 1u;
        for (u32 b = 0; b < buffers; b++) {
            cuMemcpyHtoD(s.shared->rawDevicePtr(b), s.seed.data(), bytes);
        }
    }
    cuCtxSynchronize();
    s.compute_ok.store(true, std::memory_order_release);

    const unsigned block = 256;
    const unsigned grid = (s.count + block - 1) / block;
    u64 cycle = 0;

    while (!app.workerShouldStop()) {
        unsigned count = s.count;
        float dt = kSubstepDt;

        if (sim_priority) {
            CUdeviceptr ptr = s.shared->simStatePtr();
            CUstream stream = s.shared->simStream();
            for (u32 k = 0; k < s.substeps; k++) {
                void* args[] = {&ptr, &count, &dt};
                cuLaunchKernel(integrate, grid, 1, 1, block, 1, 1, 0, stream, args, nullptr);
            }
            s.shared->publishIfRequested(); // non-blocking; never waits on the renderer
            s.sim_steps.fetch_add(s.substeps, std::memory_order_relaxed);
            if (((++cycle) & 0x3F) == 0) {
                s.shared->cuda().sync(stream); // bound the sim-stream queue
            }
        } else {
            CUdeviceptr ptr = s.shared->produceBegin();
            CUstream stream = s.shared->stream();
            for (u32 k = 0; k < s.substeps; k++) {
                void* args[] = {&ptr, &count, &dt};
                cuLaunchKernel(integrate, grid, 1, 1, block, 1, 1, 0, stream, args, nullptr);
            }
            s.shared->produceEnd();
            s.sim_steps.fetch_add(s.substeps, std::memory_order_relaxed);
            if (((++cycle) & 0xF) == 0) {
                s.shared->cuda().sync();
            }
        }
    }

    s.shared->cuda().sync();
    if (sim_priority) {
        s.shared->cuda().sync(s.shared->simStream());
    }
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
    desc.sync = sandbox.sync;
    sandbox.shared = gpu::SharedInstanceBuffer::create(desc);
    if (!sandbox.shared) {
        RLOG(LL_FATAL, "cuda_points_test: SharedInstanceBuffer::create failed");
    }

    generateSeed(sandbox);

    const f32 aspect = static_cast<f32>(app.width()) / static_cast<f32>(app.height());
    sandbox.camera.emplace(
        1.0472f, aspect, 0.5f, 500.0f, Eigen::Vector3f{0.0f, 18.0f, 62.0f}, Eigen::Vector3f{0.0f, -0.28f, -1.0f}, kWorldUp
    );

    // Application starts the worker once init() returns and stops + joins it before cleanup.
    app.addWorker(simWorker, &sandbox);

    RLOG(LL_INFO, "cuda_points_test: %u points, %u substeps/cycle, %s sync", sandbox.count, sandbox.substeps,
         syncName(sandbox.sync));

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

    // Hold P to stall the render thread -- in sim-priority mode the sim rate holds.
    const bool stalling = app.input().isKeyDown(SAPP_KEYCODE_P);
    if (stalling) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    sandbox.render_frames++;
    if (sandbox.max_frames > 0 && sandbox.render_frames >= sandbox.max_frames) {
        app.quit();
    }
    const f32 since_mark = std::chrono::duration<f32>(now - sandbox.rate_mark).count();
    if (since_mark >= 0.5f) {
        const u64 steps = sandbox.sim_steps.load(std::memory_order_relaxed);
        sandbox.sim_rate = static_cast<f32>(steps - sandbox.sim_at_mark) / since_mark;
        sandbox.frame_rate = static_cast<f32>(sandbox.render_frames - sandbox.frames_at_mark) / since_mark;
        sandbox.sim_at_mark = steps;
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
            if (ImGui::Begin("cuda_points_test")) {
                ImGui::Text("%u points, zero-copy shared buffer", sandbox.count);
                ImGui::Text("sim steps/sec : %.0f", sandbox.sim_rate);
                ImGui::Text("render fps    : %.0f", sandbox.frame_rate);
                ImGui::Text("ratio         : %.1fx",
                            sandbox.frame_rate > 0.0f ? sandbox.sim_rate / sandbox.frame_rate : 0.0f);
                ImGui::Text("sync mode     : %s", syncName(sandbox.sync));
                if (sandbox.sync == Sync::SimPriority) {
                    ImGui::TextColored(stalling ? ImVec4(1, 0.9f, 0.3f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1),
                                       "hold P to stall the render thread");
                }
                ImGui::SliderFloat("exposure", &sandbox.exposure, 0.05f, 3.0f, "%.2f");
                if (!sandbox.compute_ok.load(std::memory_order_acquire)) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "compute thread not running");
                }
            }
            ImGui::End();
        });
    });

    sandbox.shared->consumeEnd();
}

void cleanup(Sandbox& sandbox) {
    RLOG(LL_INFO, "cuda_points_test: %llu sim steps over %llu frames (last: %.0f steps/s, %.0f fps, %.1fx)",
         static_cast<unsigned long long>(sandbox.sim_steps.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(sandbox.render_frames), sandbox.sim_rate, sandbox.frame_rate,
         sandbox.frame_rate > 0.0f ? sandbox.sim_rate / sandbox.frame_rate : 0.0f);

    // Application has already stopped + joined the worker (and its detachCuda ran) by now,
    // so only Vulkan-owning objects are left to release while sokol is still up.
    sandbox.presenter.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
    sandbox.shared.reset();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting cuda_points_test");

    app::CliParser cli{argc, argv};
    cli.defineArg<u32>("count");
    cli.defineArg<u32>("substeps");
    cli.defineArg<u32>("frames");
    cli.defineArg<bool>("double");
    cli.defineArg<bool>("sim-priority");
    cli.parse();

    Sandbox sandbox;
    if (const auto v = cli.arg<u32>("count"); v && *v > 0) {
        sandbox.count = *v;
    }
    if (const auto v = cli.arg<u32>("substeps"); v && *v > 0) {
        sandbox.substeps = *v;
    }
    if (const auto v = cli.arg<u32>("frames"); v && *v > 0) {
        sandbox.max_frames = *v;
    }
    if (const auto v = cli.arg<bool>("double"); v && *v) {
        sandbox.sync = Sync::DoubleBuffer;
    }
    if (const auto v = cli.arg<bool>("sim-priority"); v && *v) {
        sandbox.sync = Sync::SimPriority;
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
