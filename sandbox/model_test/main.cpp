#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <graphics/camera.hpp>
#include <graphics/debug_draw.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/renderer.hpp>
#include <scene/gltf_loader.hpp>

#include <sokol_app.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <variant>

namespace {

constexpr f32 kMoveSpeed = 5.0f;        // units/sec
constexpr f32 kLookSensitivity = 0.15f; // radians per pixel of mouse delta, per second
constexpr f32 kPitchLimit = 1.5533f;    // ~89 degrees; avoids forward/world_up going parallel

constexpr u32 kGridSize = 10;
constexpr f32 kGridSpacing = 3.0f;

const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

// Everything that has to survive between the init and frame callbacks. sokol_app owns the
// loop, so this lives in main() and the callbacks capture it by reference.
struct Sandbox {
        std::filesystem::path model_path;

        // All constructed in init(), once the GPU and the window size are up.
        std::optional<graphics::Renderer> renderer;
        std::optional<scene::Scene> scene;
        std::optional<scene::Scene> standalone_scene;
        std::optional<graphics::LightManager> lights;
        std::optional<graphics::Camera> camera;

        Eigen::Matrix4f world_transform = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f standalone_transform = Eigen::Matrix4f::Identity();

        // yaw=0, pitch=0 matches the camera's initial direction (0,0,-1), so mouse-look
        // starts from exactly where the camera is already pointed.
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;

        std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
};

void init(app::Application& app, Sandbox& sandbox) {
    sandbox.renderer.emplace();

    sandbox.scene = scene::loadGltf(sandbox.model_path);
    RLOG(LL_DEBUG, sandbox.scene->display());

    // Convert every part to InstancedMesh and populate a 10x10 grid on the XZ plane.
    for (scene::Model& model : sandbox.scene->mutableModels()) {
        for (scene::ModelPart& part : model.mutableParts()) {
            part.convertToInstanced();
            graphics::InstancedMesh& instanced = std::get<graphics::InstancedMesh>(part.mesh);
            for (u32 x = 0; x < kGridSize; x++) {
                for (u32 z = 0; z < kGridSize; z++) {
                    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
                    transform(0, 3) = (static_cast<f32>(x) - (kGridSize - 1) / 2.0f) * kGridSpacing;
                    transform(2, 3) = (static_cast<f32>(z) - (kGridSize - 1) / 2.0f) * kGridSpacing;
                    instanced.addInstance(transform);
                }
            }
            instanced.uploadIfDirty(); // must run before the frame loop -- see Renderer docs
        }
    }

    // Second copy of the model, left as StandardMesh (not instanced) and lifted +10 on Y, so
    // flying the camera up/down toggles it in and out of the frustum -- exercises mesh culling.
    sandbox.standalone_scene = scene::loadGltf(sandbox.model_path);
    sandbox.standalone_transform(1, 3) = 10.0f;

    sandbox.lights.emplace();
    for (const graphics::Light& light : sandbox.scene->lights()) {
        RLOG(LL_DEBUG, "Added light: " + light.display());
        sandbox.lights->add(light);
    }
    sandbox.lights->build();

    sandbox.renderer->setSkybox(graphics::Skybox{}); // calibrated default gradient sky

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

// Culls and submits every part of every model in scene against frustum. This loop -- not
// Camera or Model -- is what ties "a scene" to "a camera" for one frame; Renderer::submit
// has no notion of camera, and Model has no notion of Renderer or Camera at all.
void submitScene(graphics::Renderer& renderer, scene::Scene& scene, const Eigen::Matrix4f& world_transform, const graphics::Frustum& frustum) {
    for (scene::Model& model : scene.mutableModels()) {
        for (scene::ModelPart& part : model.mutableParts()) {
            const Eigen::Matrix4f world_matrix = world_transform * part.transform;
            const Eigen::AlignedBox3f world_bounds = scene::worldBounds(world_matrix, part.local_bounds);

            // Camera-frustum cull only decides G-buffer visibility -- a culled object still casts shadows.
            // Instanced parts carry per-instance transforms we don't cull yet, so treat them as visible.
            const bool camera_visible =
                !std::holds_alternative<graphics::StandardMesh>(part.mesh) || frustum.intersects(world_bounds);

            std::visit(
                [&](auto& mesh) { renderer.submit(mesh, part.material, world_matrix, world_bounds, camera_visible); }, part.mesh
            );
        }
    }
}

void frame(app::Application& app, Sandbox& sandbox) {
    const auto now = std::chrono::steady_clock::now();
    const f32 dt = std::chrono::duration<f32>(now - sandbox.last_time).count();
    sandbox.last_time = now;

    // The window is resizable, so keep the projection matching the framebuffer.
    if (app.height() > 0) {
        sandbox.camera->setAspectRatio(static_cast<f32>(app.width()) / static_cast<f32>(app.height()));
    }

    if (app.input().isKeyPressed(SAPP_KEYCODE_ESCAPE)) {
        app.toggleCursorCapture();
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_F1)) {
        sandbox.renderer->toggleShadowMapDebug();
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_F2)) {
        sandbox.renderer->cycleShadowMapDebugLayer();
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_F3)) {
        graphics::DebugDraw& debug_draw = sandbox.renderer->debugDraw();
        debug_draw.setEnabled(!debug_draw.isEnabled());
    }

    flyCamera(app, sandbox, dt);

    graphics::Renderer& renderer = *sandbox.renderer;
    renderer.beginFrame(*sandbox.camera);
    renderer.setLights(*sandbox.lights);

    const graphics::Frustum& frustum = sandbox.camera->frustum();
    submitScene(renderer, *sandbox.scene, sandbox.world_transform, frustum);
    submitScene(renderer, *sandbox.standalone_scene, sandbox.standalone_transform, frustum);

    // Example calls -- establishes the pattern later features (shadow cascades, light
    // ranges) can copy. No-op unless F3 has turned debug draw on.
    for (scene::Model& model : sandbox.standalone_scene->mutableModels()) {
        for (const scene::ModelPart& part : model.mutableParts()) {
            const Eigen::Matrix4f world_matrix = sandbox.standalone_transform * part.transform;
            const Eigen::AlignedBox3f world_bounds = scene::worldBounds(world_matrix, part.local_bounds);
            renderer.debugDraw().aabb(world_bounds, Eigen::Vector4f(1.0f, 0.2f, 0.2f, 1.0f));
        }
    }
    // Traces the live camera's own frustum -- so it draws along the screen edges, not a
    // separate shape. A detached debug vantage needs a second Camera; not worth it just
    // to exercise this call. Confirms the API and wiring; roadmap-nice-debug-draw can
    // upgrade this once something else (e.g. a light) wants a real off-camera frustum.
    renderer.debugDraw().frustum(frustum.corners(), Eigen::Vector4f(0.2f, 1.0f, 0.2f, 1.0f));

    renderer.endFrame();
}

// Every one of these owns sokol handles, so they have to go before sg_shutdown().
void cleanup(Sandbox& sandbox) {
    sandbox.scene.reset();
    sandbox.standalone_scene.reset();
    sandbox.lights.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
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
        [&sandbox](app::Application&) { cleanup(sandbox); },
    };
    app.run();

    return 0;
}
