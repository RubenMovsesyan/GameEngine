#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <graphics/camera.hpp>

#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_glue.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>

namespace {

constexpr f32 kMoveSpeed = 5.0f;        // units/sec
constexpr f32 kLookSensitivity = 0.15f; // radians per pixel of mouse delta, per second
constexpr f32 kPitchLimit = 1.5533f;    // ~89 degrees; avoids forward/world_up going parallel

const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

// Everything that has to survive between the init and frame callbacks. sokol_app owns the
// loop, so this lives in main() and the callbacks capture it by reference.
struct Sandbox {
        std::filesystem::path model_path;

        // Constructed in init(), once the window size is known.
        std::optional<graphics::Camera> camera;

        // yaw=0, pitch=0 matches the camera's initial direction (0,0,-1), so mouse-look
        // starts from exactly where the camera is already pointed.
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;

        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
};

void init(app::Application& app, Sandbox& sandbox) {
    RLOG(LL_INFO, "Sokol backend up, model: " + sandbox.model_path.string());

    const f32 aspect = static_cast<f32>(app.width()) / static_cast<f32>(app.height());
    sandbox.camera.emplace(1.0472f, aspect, 0.1f, 100.0f, Eigen::Vector3f{0.0f, 0.0f, 3.0f}, Eigen::Vector3f{0.0f, 0.0f, -1.0f}, kWorldUp);

    sandbox.last_time = std::chrono::steady_clock::now();
}

void flyCamera(app::Application& app, Sandbox& sandbox, f32 dt) {
    graphics::Camera& camera = *sandbox.camera;

    const Eigen::Vector3f forward = camera.direction().normalized();
    const Eigen::Vector3f right = forward.cross(kWorldUp).normalized();

    if (app.input().isKeyDown(SAPP_KEYCODE_W)) {
        *camera.mutablePosition() += forward * kMoveSpeed * dt;
    }
    if (app.input().isKeyDown(SAPP_KEYCODE_S)) {
        *camera.mutablePosition() -= forward * kMoveSpeed * dt;
    }
    if (app.input().isKeyDown(SAPP_KEYCODE_D)) {
        *camera.mutablePosition() += right * kMoveSpeed * dt;
    }
    if (app.input().isKeyDown(SAPP_KEYCODE_A)) {
        *camera.mutablePosition() -= right * kMoveSpeed * dt;
    }
    if (app.input().isKeyDown(SAPP_KEYCODE_SPACE)) {
        *camera.mutablePosition() += kWorldUp * kMoveSpeed * dt;
    }
    if (app.input().isKeyDown(SAPP_KEYCODE_LEFT_CONTROL)) {
        *camera.mutablePosition() -= kWorldUp * kMoveSpeed * dt;
    }

    const Eigen::Vector2f mouse_delta = app.input().mouseDelta();
    if (app.isCursorCaptured() && (mouse_delta.x() != 0.0f || mouse_delta.y() != 0.0f)) {
        sandbox.yaw += mouse_delta.x() * kLookSensitivity * dt;
        sandbox.pitch = std::clamp(sandbox.pitch - mouse_delta.y() * kLookSensitivity * dt, -kPitchLimit, kPitchLimit);

        *camera.mutableDirection() = Eigen::Vector3f{
            std::cos(sandbox.pitch) * std::sin(sandbox.yaw),
            std::sin(sandbox.pitch),
            -std::cos(sandbox.pitch) * std::cos(sandbox.yaw),
        };
    }
}

void frame(app::Application& app, Sandbox& sandbox) {
    const auto now = std::chrono::steady_clock::now();
    const f32 dt = std::chrono::duration<f32>(now - sandbox.last_time).count();
    sandbox.last_time = now;

    if (app.input().isKeyPressed(SAPP_KEYCODE_ESCAPE)) {
        app.toggleCursorCapture();
    }

    flyCamera(app, sandbox, dt);

    // Nothing is rendered yet -- the scene and renderer come back in the next phases.
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.15f, 0.16f, 0.18f, 1.0f};
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting Game Engine");

    app::CliParser cli{argc, argv};
    cli.defineArg<std::string>("model");
    cli.parse();

    std::optional<std::string> model_arg = cli.arg<std::string>("model");
    if (!model_arg.has_value()) {
        RLOG(LL_FATAL, "--model <path>.glb|.gltf is required");
    }
    std::filesystem::path model_ext = std::filesystem::path(*model_arg).extension();
    if (model_ext != ".glb" && model_ext != ".gltf") {
        RLOG(LL_FATAL, "--model <path>.glb|.gltf is required");
    }

    Sandbox sandbox{.model_path = *model_arg};

    app::Application app{
        app::Application::Config{},
        [&sandbox](app::Application& app) { init(app, sandbox); },
        [&sandbox](app::Application& app) { frame(app, sandbox); },
    };
    app.run();

    return 0;
}
