#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <app/debug_ui.hpp>
#include <graphics/primitives/scene/camera.hpp>
#include <graphics/primitives/pass/debug_draw.hpp>
#include <graphics/renderers/deferred_renderer.hpp>
#include <graphics/primitives/pass/light_manager.hpp>
#include <graphics/presenter.hpp>
#include <assets/asset_library.hpp>
#include <assets/loaders/defaults.hpp>
#include <scene/model.hpp>

#include <imgui.h>

#include <sokol_app.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace {

constexpr f32 kMoveSpeed = 5.0f;        // units/sec
constexpr f32 kLookSensitivity = 0.15f; // radians per pixel of mouse delta, per second
constexpr f32 kPitchLimit = 1.5533f;    // ~89 degrees; avoids forward/world_up going parallel

constexpr u32 kGridSize = 10;
constexpr f32 kGridSpacing = 3.0f;

const Eigen::Vector3f kWorldUp{0.0f, 1.0f, 0.0f};

// The app's State: app::Application<Sandbox> builds it once the GPU is up and destroys it
// before the GPU goes down.
struct Sandbox {
        std::filesystem::path model_path;

        // All constructed in the constructor, once the GPU and the window size are up.
        std::optional<graphics::renderers::DeferredRenderer> renderer;
        std::optional<graphics::Presenter> presenter;
        std::optional<graphics::primitives::pass::LightManager> lights;
        std::optional<graphics::primitives::scene::Camera> camera;

        // Loaded once, shared: the grid and the standalone copy both draw this one Model.
        assets::AssetLibrary assets;
        std::shared_ptr<const scene::Model> model;
        std::shared_ptr<const std::vector<graphics::primitives::scene::Light>> scene_lights; // keeps LightManager's pointers alive

        // Grid: per-part InstancedMesh sharing the Model's GPU geometry, 10x10 instances.
        struct GridPart {
                graphics::primitives::scene::InstancedMesh mesh;
                const scene::ModelPart* part;
        };
        std::vector<GridPart> grid;

        // Culling-test copy, +10 Y: the same Model, placed by this sandbox (not by the asset).
        std::shared_ptr<const scene::Model> standalone;
        Eigen::Matrix4f standalone_placement = Eigen::Matrix4f::Identity();

        // F5 snapshots the camera's frustum corners here for debug-draw. Drawing the LIVE
        // camera's own frustum through its own view-projection is degenerate -- it's always
        // exactly the NDC unit cube, so it can't respond to rotation and flickers at the
        // near-plane clip boundary. A frozen snapshot, viewed from the still-moving live
        // camera, is a real external vantage on a real static shape.
        std::optional<std::array<Eigen::Vector3f, 8>> frozen_frustum;

        // yaw=0, pitch=0 matches the camera's initial direction (0,0,-1), so mouse-look
        // starts from exactly where the camera is already pointed.
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;

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

// The command line, read once the app is up -- the first thing the constructor does.
void parseCli(app::Context& app, Sandbox& sandbox) {
    app::CliParser cli{app.argc(), app.argv()};
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
    sandbox.model_path = *model_arg;
}

Sandbox::Sandbox(app::Context& app) {
    Sandbox& sandbox = *this;
    parseCli(app, sandbox);

    sandbox.renderer.emplace();
    sandbox.presenter.emplace();

    assets::loaders::registerDefaultLoaders(sandbox.assets);
    sandbox.model = sandbox.assets.load<scene::Model>(sandbox.model_path);
    sandbox.scene_lights = sandbox.assets.load<std::vector<graphics::primitives::scene::Light>>(sandbox.model_path); // split: no re-parse
    RLOG(LL_INFO, "assets: %u part(s), %zu light(s), %zu cached asset(s)", sandbox.model->partCount(), sandbox.scene_lights->size(),
         sandbox.assets.size());

    // 10x10 grid on the XZ plane: instanced views over the shared geometry; the Model is untouched.
    for (const scene::ModelPart& part : sandbox.model->parts()) {
        Sandbox::GridPart grid_part{
            graphics::primitives::scene::InstancedMesh::fromStandard(std::get<graphics::primitives::scene::StandardMesh>(part.mesh)), &part
        };
        for (u32 x = 0; x < kGridSize; x++) {
            for (u32 z = 0; z < kGridSize; z++) {
                Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
                transform(0, 3) = (static_cast<f32>(x) - (kGridSize - 1) / 2.0f) * kGridSpacing;
                transform(2, 3) = (static_cast<f32>(z) - (kGridSize - 1) / 2.0f) * kGridSpacing;
                grid_part.mesh.addInstance(transform);
            }
        }
        grid_part.mesh.uploadIfDirty(); // must run before the frame loop -- see Renderer docs
        sandbox.grid.push_back(std::move(grid_part));
    }

    // Second load of the same path: a cache hit returning the very same object.
    sandbox.standalone = sandbox.assets.load<scene::Model>(sandbox.model_path);
    RLOG(LL_INFO, "assets: second load<Model> shares the first: %s", sandbox.standalone == sandbox.model ? "yes" : "NO");
    sandbox.standalone_placement(1, 3) = 10.0f;

    sandbox.lights.emplace();
    for (const graphics::primitives::scene::Light& light : *sandbox.scene_lights) {
        RLOG(LL_DEBUG, "Added light: " + light.display());
        sandbox.lights->add(light);
    }
    sandbox.lights->build();

    sandbox.renderer->setSkybox(graphics::primitives::scene::Skybox{}); // calibrated default gradient sky

    const f32 aspect = static_cast<f32>(app.width()) / static_cast<f32>(app.height());
    sandbox.camera.emplace(1.0472f, aspect, 0.1f, 100.0f, Eigen::Vector3f{0.0f, 0.0f, 3.0f}, Eigen::Vector3f{0.0f, 0.0f, -1.0f}, kWorldUp);

    sandbox.last_time = std::chrono::steady_clock::now();
}

void flyCamera(app::Context& app, Sandbox& sandbox, f32 dt) {
    graphics::primitives::scene::Camera& camera = *sandbox.camera;

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

// Culls and submits every part of `model` placed at `placement`. This loop -- not Camera or
// Model -- ties a model to a camera for one frame; the Model itself carries no placement here.
void submitModel(graphics::renderers::DeferredRenderer& renderer, const scene::Model& model, const Eigen::Matrix4f& placement,
                 const graphics::primitives::scene::Frustum& frustum) {
    for (const scene::ModelPart& part : model.parts()) {
        const Eigen::Matrix4f world_matrix = placement * part.transform;
        const Eigen::AlignedBox3f world_bounds = scene::worldBounds(world_matrix, part.local_bounds);
        const bool camera_visible = frustum.intersects(world_bounds); // culled parts still cast shadows
        std::visit([&](const auto& mesh) { renderer.submit(mesh, part.material, world_matrix, world_bounds, camera_visible); }, part.mesh);
    }
}

void Sandbox::frame(app::Context& app) {
    Sandbox& sandbox = *this;

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
        graphics::primitives::pass::DebugDraw& debug_draw = sandbox.renderer->debugDraw();
        debug_draw.setEnabled(!debug_draw.isEnabled());
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_F4)) {
        app::DebugUI& debug_ui = app.debugUI();
        debug_ui.setVisible(!debug_ui.isVisible());
        // Opening the panel needs a usable cursor; flyCamera already gates mouse-look on
        // isCursorCaptured(), so uncapturing here is the only change needed to stop the
        // camera fighting the panel. Closing does not auto-recapture -- Escape still does.
        if (debug_ui.isVisible()) {
            app.setCursorCaptured(false);
        }
    }
    if (app.input().isKeyPressed(SAPP_KEYCODE_F5)) {
        sandbox.frozen_frustum = sandbox.camera->frustum().corners();
    }

    flyCamera(app, sandbox, dt);

    graphics::renderers::DeferredRenderer& renderer = *sandbox.renderer;
    renderer.beginFrame(*sandbox.camera);
    renderer.setLights(*sandbox.lights);

    const graphics::primitives::scene::Frustum& frustum = sandbox.camera->frustum();
    for (const Sandbox::GridPart& g : sandbox.grid) {
        // Never culled: per-instance bounds aren't tracked, so one part's AABB can't stand in for the grid.
        renderer.submit(g.mesh, g.part->material, g.part->transform, scene::worldBounds(g.part->transform, g.part->local_bounds), true);
    }
    submitModel(renderer, *sandbox.standalone, sandbox.standalone_placement, frustum);

    // Example calls -- establishes the pattern later features (shadow cascades, light
    // ranges) can copy. No-op unless F3 has turned debug draw on.
    for (const scene::ModelPart& part : sandbox.standalone->parts()) {
        const Eigen::Matrix4f world_matrix = sandbox.standalone_placement * part.transform;
        renderer.debugDraw().aabb(scene::worldBounds(world_matrix, part.local_bounds), Eigen::Vector4f(1.0f, 0.2f, 0.2f, 1.0f));
    }
    // F5 freezes a snapshot (see the Sandbox field's comment for why the live frustum is
    // never drawn directly); nothing draws until the first freeze.
    if (sandbox.frozen_frustum) {
        renderer.debugDraw().frustum(*sandbox.frozen_frustum, Eigen::Vector4f(0.2f, 1.0f, 0.2f, 1.0f));
    }

    sandbox.presenter->present(renderer, [&]() {
        app.debugUI().draw(app.width(), app.height(), dt, sapp_dpi_scale(), [&]() {
            if (ImGui::Begin("Debug")) {
                bool debug_draw = renderer.debugDraw().isEnabled();
                if (ImGui::Checkbox("Debug Draw (F3)", &debug_draw)) {
                    renderer.debugDraw().setEnabled(debug_draw);
                }

                bool shadow_debug = renderer.isShadowMapDebugEnabled();
                if (ImGui::Checkbox("Shadow Map Debug (F1)", &shadow_debug)) {
                    renderer.setShadowMapDebugEnabled(shadow_debug);
                }

                int layer = static_cast<int>(renderer.shadowMapDebugLayer());
                if (ImGui::SliderInt("Cascade Layer (F2)", &layer, 0, 3)) {
                    renderer.setShadowMapDebugLayer(static_cast<u32>(layer));
                }
            }
            ImGui::End();
        });
    });
}

// Every one of these owns sokol handles, so they have to go before sg_shutdown().
Sandbox::~Sandbox() {
    Sandbox& sandbox = *this;

    sandbox.grid.clear();
    sandbox.model.reset();
    sandbox.standalone.reset();
    sandbox.scene_lights.reset();
    sandbox.assets.clear();
    sandbox.lights.reset();
    sandbox.presenter.reset();
    sandbox.renderer.reset();
    sandbox.camera.reset();
}

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting Game Engine");

    app::Application<Sandbox> app{{}, argc, argv};
    app.run();

    return 0;
}
