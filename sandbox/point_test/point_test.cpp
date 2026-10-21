#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <app/debug_ui.hpp>
#include <graphics/presenter.hpp>
#include <graphics/primitives/scene/camera.hpp>
#include <graphics/primitives/scene/point.hpp>
#include <graphics/renderers/point_renderer.hpp>

#include <imgui.h>

#include <sokol_app.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <random>
#include <vector>

// Exercises graphics::renderers::PointRenderer: a slowly rotating disc of self-luminous
// points, drawn through the same graphics::Presenter as the deferred sandbox. No models, no
// lights -- just points in, one LDR image out.
//
//   ./run point_test [--count N] [--exposure X]

namespace {

using graphics::primitives::scene::Camera;
using graphics::primitives::scene::PointInstance;

constexpr f32 kMoveSpeed = 20.0f;       // units/sec
constexpr f32 kLookSensitivity = 0.15f; // radians per pixel per second
constexpr f32 kPitchLimit = 1.5533f;    // ~89 degrees
constexpr f32 kDiscRadius = 45.0f;
constexpr f32 kSpinRate = 0.08f; // radians/sec, whole disc about +Y

const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

struct Sandbox {
        u32 point_count = 20000;
        f32 exposure = 0.8f;

        std::optional<graphics::renderers::PointRenderer> renderer;
        std::optional<graphics::Presenter> presenter;
        std::optional<Camera> camera;

        std::vector<PointInstance> points;    // rest pose, at spin angle 0
        std::vector<PointInstance> spun;      // points rotated by the current angle -- what gets submitted
        f32 spin_angle = 0.0f;

        f32 yaw = 0.0f;
        f32 pitch = -0.32f; // start looking slightly down at the disc
        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();

        // The app::AppState shape: the constructor is init (GPU up, workers not yet
        // started), frame() runs every frame, the destructor is cleanup (workers joined,
        // GPU still up).
        explicit Sandbox(app::Context& app);
        ~Sandbox();
        void frame(app::Context& app);

        Sandbox(const Sandbox&) = delete;
        Sandbox& operator=(const Sandbox&) = delete;
};

void generatePoints(Sandbox& sandbox) {
    std::mt19937 rng{1337};
    std::uniform_real_distribution<f32> angle{0.0f, 6.2831853f};
    std::uniform_real_distribution<f32> unit{0.0f, 1.0f};
    std::normal_distribution<f32> height{0.0f, 1.5f};

    sandbox.points.clear();
    sandbox.points.reserve(sandbox.point_count);
    for (u32 i = 0; i < sandbox.point_count; i++) {
        // sqrt(unit) radius -> roughly uniform area density, denser look toward the centre
        // from the size/intensity falloff below rather than from clustering.
        const f32 r = std::sqrt(unit(rng)) * kDiscRadius;
        const f32 a = angle(rng);
        const f32 central = 1.0f - r / kDiscRadius; // 1 at core, 0 at rim

        PointInstance p{};
        p.position = Eigen::Vector3f{r * std::cos(a), height(rng) * (0.3f + central), r * std::sin(a)};
        p.size = 0.06f + 0.18f * unit(rng);
        // Warm white, a few bright, most dim; brighter toward the core.
        const f32 intensity = (0.25f + 2.5f * unit(rng) * unit(rng)) * (0.4f + central);
        p.color = Eigen::Vector4f{1.0f, 0.93f, 0.82f, intensity};
        sandbox.points.push_back(p);
    }
    sandbox.spun.resize(sandbox.points.size());
}

void flyCamera(app::Context& app, Sandbox& sandbox, f32 dt) {
    Camera& camera = *sandbox.camera;

    const Eigen::Vector3f forward = camera.direction().normalized();
    const Eigen::Vector3f right = forward.cross(kWorldUp).normalized();

    if (app.input().isKeyDown(SAPP_KEYCODE_W)) *camera.mutablePosition() += forward * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_S)) *camera.mutablePosition() -= forward * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_D)) *camera.mutablePosition() += right * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_A)) *camera.mutablePosition() -= right * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_SPACE)) *camera.mutablePosition() += kWorldUp * kMoveSpeed * dt;
    if (app.input().isKeyDown(SAPP_KEYCODE_LEFT_CONTROL)) *camera.mutablePosition() -= kWorldUp * kMoveSpeed * dt;

    const Eigen::Vector2f mouse_delta = app.input().mouseDelta();
    if (app.isCursorCaptured() && (mouse_delta.x() != 0.0f || mouse_delta.y() != 0.0f)) {
        sandbox.yaw += mouse_delta.x() * kLookSensitivity * dt;
        sandbox.pitch = std::clamp(sandbox.pitch - mouse_delta.y() * kLookSensitivity * dt, -kPitchLimit, kPitchLimit);
    }
    *camera.mutableDirection() = Eigen::Vector3f{
        std::cos(sandbox.pitch) * std::sin(sandbox.yaw),
        std::sin(sandbox.pitch),
        -std::cos(sandbox.pitch) * std::cos(sandbox.yaw),
    };
}

// The command line, read once the app is up -- the first thing the constructor does.
void parseCli(app::Context& app, Sandbox& sandbox) {
    app::CliParser cli{app.argc(), app.argv()};
    cli.defineArg<u32>("count");
    cli.defineArg<f32>("exposure");
    cli.parse();

    if (const std::optional<u32> count = cli.arg<u32>("count"); count && *count > 0) {
        sandbox.point_count = *count;
    }
    if (const std::optional<f32> exposure = cli.arg<f32>("exposure"); exposure && *exposure > 0.0f) {
        sandbox.exposure = *exposure;
    }
}

Sandbox::Sandbox(app::Context& app) {
    Sandbox& sandbox = *this;
    parseCli(app, sandbox);

    sandbox.renderer.emplace();
    sandbox.presenter.emplace();
    sandbox.renderer->setExposure(sandbox.exposure);

    generatePoints(sandbox);
    RLOG(LL_INFO, "point_test: %u points", sandbox.point_count);

    const f32 aspect = static_cast<f32>(app.width()) / static_cast<f32>(app.height());
    sandbox.camera.emplace(
        1.0472f, aspect, 0.5f, 500.0f, Eigen::Vector3f{0.0f, 16.0f, 60.0f}, Eigen::Vector3f{0.0f, -0.25f, -1.0f}, kWorldUp
    );

    sandbox.last_time = std::chrono::steady_clock::now();
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

    // Rotate the whole disc about +Y into the scratch buffer, then hand it to the renderer.
    sandbox.spin_angle += kSpinRate * dt;
    const f32 s = std::sin(sandbox.spin_angle);
    const f32 c = std::cos(sandbox.spin_angle);
    for (usize i = 0; i < sandbox.points.size(); i++) {
        const PointInstance& src = sandbox.points[i];
        PointInstance& dst = sandbox.spun[i];
        dst = src;
        dst.position.x() = src.position.x() * c - src.position.z() * s;
        dst.position.z() = src.position.x() * s + src.position.z() * c;
    }

    graphics::renderers::PointRenderer& renderer = *sandbox.renderer;
    renderer.beginFrame(*sandbox.camera);
    renderer.setExposure(sandbox.exposure);
    renderer.submit(sandbox.spun);

    sandbox.presenter->present(renderer, [&]() {
        app.debugUI().draw(app.width(), app.height(), dt, sapp_dpi_scale(), [&]() {
            if (ImGui::Begin("point_test")) {
                ImGui::Text("%u points", sandbox.point_count);
                ImGui::SliderFloat("exposure", &sandbox.exposure, 0.05f, 3.0f, "%.2f");
                ImGui::Text("WASD / Space / Ctrl move, ESC toggles mouse-look");
            }
            ImGui::End();
        });
    });
}

Sandbox::~Sandbox() {
    Sandbox& sandbox = *this;

    sandbox.presenter.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting point_test");

    app::Application<Sandbox> app{{}, argc, argv};
    app.run();

    return 0;
}
