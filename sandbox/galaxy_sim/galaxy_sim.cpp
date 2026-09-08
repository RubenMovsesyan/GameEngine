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

// Galaxy simulation -- scaffold only.
//
// A render thread and an independent CUDA simulation thread share one instance buffer via
// gpu::SharedInstanceBuffer in Sync::SimPriority mode: the sim owns a private buffer it can
// hammer at whatever rate it likes, and only DtoD-copies a snapshot into the ring the
// renderer draws when the renderer asks (publishIfRequested). The sim is a NO-OP for now --
// the stars just sit in the disc generateDisc() lays out. Drop the integration kernels into
// simWorker's loop when building the actual simulation.
//
//   ./run galaxy_sim [--count N] [--frames N]

namespace {

using graphics::primitives::scene::Camera;
using graphics::primitives::scene::PointInstance;

constexpr f32 kMoveSpeed = 25.0f;
constexpr f32 kLookSensitivity = 0.15f;
constexpr f32 kPitchLimit = 1.5533f;
constexpr f32 kDiscRadius = 45.0f;
const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

struct Sandbox {
        u32 count = 1000;
        u32 max_frames = 0; // 0 = run until the window closes

        std::optional<graphics::Presenter> presenter;
        std::optional<graphics::renderers::PointRenderer> renderer;
        std::optional<Camera> camera;
        std::unique_ptr<gpu::SharedInstanceBuffer> shared;

        std::vector<PointInstance> seed;

        std::atomic<bool> compute_ok{false};
        std::atomic<u64> sim_iters{0};
        u64 render_frames = 0;

        f32 exposure = 0.9f;
        f32 yaw = 0.0f;
        f32 pitch = -0.35f;
        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point rate_mark = std::chrono::steady_clock::now();
        u64 sim_at_mark = 0;
        u64 frames_at_mark = 0;
        f32 sim_rate = 0.0f;
        f32 frame_rate = 0.0f;
};

// A rough disc of stars: uniform-area radial density, a little vertical scatter, warmer and
// brighter toward the core. Same shape as cuda_points_test, just the starting layout.
void generateDisc(Sandbox& sandbox) {
    std::mt19937 rng{424242};
    std::uniform_real_distribution<f32> angle{0.0f, 6.2831853f};
    std::uniform_real_distribution<f32> unit{0.0f, 1.0f};
    std::normal_distribution<f32> height{0.0f, 1.2f};

    sandbox.seed.resize(sandbox.count);
    for (u32 i = 0; i < sandbox.count; i++) {
        const f32 r = std::sqrt(unit(rng)) * kDiscRadius;
        const f32 a = angle(rng);
        const f32 core = 1.0f - r / kDiscRadius;

        PointInstance& star = sandbox.seed[i];
        star.position = Eigen::Vector3f{r * std::cos(a), height(rng) * (0.3f + core), r * std::sin(a)};
        star.size = 0.06f + 0.18f * unit(rng);
        star.color = Eigen::Vector4f{1.0f, 0.9f, 0.8f, 0.4f + 2.0f * core * core};
    }
}

// ---- CUDA simulation worker (own thread; started + joined by Application) --------------

void simWorker(app::Application& app, Sandbox*& sptr) {
    Sandbox& s = *sptr;

    if (!s.shared->attachCuda()) {
        RLOG(LL_ERROR, "galaxy_sim: attachCuda failed");
        return;
    }

    const size_t bytes = static_cast<size_t>(s.count) * sizeof(PointInstance);
    if (!s.shared->primeSimState(s.seed.data(), bytes)) {
        RLOG(LL_ERROR, "galaxy_sim: primeSimState failed");
        s.shared->detachCuda();
        return;
    }
    cuCtxSynchronize();
    s.compute_ok.store(true, std::memory_order_release);

    while (!app.workerShouldStop()) {
        // TODO(galaxy): integrate the star state here --
        //   launch kernels on s.shared->simStream() writing s.shared->simStatePtr(),
        //   as many sub-steps as you want; the renderer never gates this loop.
        // For now the sim is a no-op: the disc just sits where generateDisc() put it.

        s.shared->publishIfRequested(); // DtoD-copies the (unchanged) private buffer on request
        s.sim_iters.fetch_add(1, std::memory_order_relaxed);

        // No real work yet -> don't peg a core. Remove once the loop launches kernels.
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }

    s.shared->cuda().sync(s.shared->simStream());
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
        1.0472f, aspect, 0.5f, 500.0f, Eigen::Vector3f{0.0f, 20.0f, 70.0f}, Eigen::Vector3f{0.0f, -0.28f, -1.0f}, kWorldUp
    );

    // Application starts the worker once init() returns and stops + joins it before cleanup.
    app.addWorker(simWorker, &sandbox);

    RLOG(LL_INFO, "galaxy_sim: %u stars, sim-priority shared buffer, sim is a no-op stub", sandbox.count);

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
                ImGui::Text("%u stars", sandbox.count);
                ImGui::Text("sim iters/sec : %.0f", sandbox.sim_rate);
                ImGui::Text("render fps    : %.0f", sandbox.frame_rate);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "simulation: no-op stub");
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
    RLOG(LL_INFO, "galaxy_sim: %llu sim iters over %llu frames",
         static_cast<unsigned long long>(sandbox.sim_iters.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(sandbox.render_frames));

    // Application has already stopped + joined the worker (detachCuda ran), so only
    // Vulkan-owning objects are left to release while sokol is still up.
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
    cli.parse();

    Sandbox sandbox;
    if (const auto v = cli.arg<u32>("count"); v && *v > 0) {
        sandbox.count = *v;
    }
    if (const auto v = cli.arg<u32>("frames"); v && *v > 0) {
        sandbox.max_frames = *v;
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
