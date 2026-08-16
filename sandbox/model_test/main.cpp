#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <graphics/camera.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/renderer.hpp>
#include <scene/gltf_loader.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <variant>

// Loads a glTF/glb model given at construction; WASD + Space/Ctrl fly the camera.
void renderLoop(app::Application& app, graphics::Renderer& renderer, const std::filesystem::path& model_path) {
    RLOG(LL_INFO, "Starting Render Thread");

    scene::Scene scene = scene::loadGltf(app.device(), model_path);
    RLOG(LL_DEBUG, scene.display());

    // Convert every part to InstancedMesh and populate a 10x10 grid on the XZ plane, 2 units apart.
    constexpr u32 kGridSize = 10;
    constexpr f32 kGridSpacing = 3.0f;
    for (scene::Model& model : scene.mutableModels()) {
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
            instanced.uploadIfDirty(app.device()); // must run before the frame loop -- see Renderer docs
        }
    }

    graphics::LightManager light_manager{app.device()};
    for (const graphics::Light& light : scene.lights()) {
	RLOG(LL_DEBUG, "Added light: " + light.display());
        light_manager.add(light);
    }
    light_manager.build();

    VkExtent2D extent = app.swapchain().extent();
    RLOG(LL_DEBUG, "Extent: " + std::to_string(extent.width) + " by " + std::to_string(extent.height));
    graphics::Camera camera{
        1.0472f, static_cast<f32>(extent.width) / static_cast<f32>(extent.height), 0.1f, 100.0f,
        {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}
    };
    Eigen::Matrix4f world_transform = Eigen::Matrix4f::Identity();

    const Eigen::Vector3f world_up{0.0f, 1.0f, 0.0f};
    constexpr f32 kMoveSpeed = 5.0f; // units/sec
    constexpr f32 kLookSensitivity = 0.15f; // radians per pixel of mouse delta, per second
    constexpr f32 kPitchLimit = 1.5533f; // ~89 degrees; avoids forward/world_up going parallel

    // yaw=0, pitch=0 matches the camera's initial direction (0,0,-1) below, so
    // mouse-look starts from exactly where the camera is already pointed.
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;

    auto last_time = std::chrono::steady_clock::now();

    while (app.running.load()) {
        auto now = std::chrono::steady_clock::now();
        f32 dt = std::chrono::duration<f32>(now - last_time).count();
        last_time = now;

        app.input().beginFrame();

        if (app.input().isKeyPressed(GLFW_KEY_ESCAPE)) {
            app.toggleCursorCapture();
        }

        Eigen::Vector3f forward = camera.direction().normalized();
        Eigen::Vector3f right = forward.cross(world_up).normalized();

        if (app.input().isKeyDown(GLFW_KEY_W)) {
            *camera.mutablePosition() += forward * kMoveSpeed * dt;
        }
        if (app.input().isKeyDown(GLFW_KEY_S)) {
            *camera.mutablePosition() -= forward * kMoveSpeed * dt;
        }
        if (app.input().isKeyDown(GLFW_KEY_D)) {
            *camera.mutablePosition() += right * kMoveSpeed * dt;
        }
        if (app.input().isKeyDown(GLFW_KEY_A)) {
            *camera.mutablePosition() -= right * kMoveSpeed * dt;
        }
        if (app.input().isKeyDown(GLFW_KEY_SPACE)) {
            *camera.mutablePosition() += world_up * kMoveSpeed * dt;
        }
        if (app.input().isKeyDown(GLFW_KEY_LEFT_CONTROL)) {
            *camera.mutablePosition() -= world_up * kMoveSpeed * dt;
        }

        Eigen::Vector2f mouse_delta = app.input().mouseDelta();
        if (app.isCursorCaptured() && (mouse_delta.x() != 0.0f || mouse_delta.y() != 0.0f)) {
            yaw += mouse_delta.x() * kLookSensitivity * dt;
            pitch = std::clamp(pitch - mouse_delta.y() * kLookSensitivity * dt, -kPitchLimit, kPitchLimit);

            *camera.mutableDirection() = Eigen::Vector3f{
                std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                -std::cos(pitch) * std::cos(yaw),
            };
        }

        renderer.beginFrame();
        renderer.setLights(light_manager);
        scene.render(renderer, world_transform, camera);
        renderer.endFrame();
    }
}

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting Game Engine");

    app::CliParser cli{argc, argv};
    cli.defineArg<std::string>("model");
    cli.parse();

    std::optional<std::string> model_arg = cli.arg<std::string>("model");
    if (!model_arg.has_value() || std::filesystem::path(*model_arg).extension() != ".glb") {
        RLOG(LL_FATAL, "--model <path>.glb is required");
    }
    const std::filesystem::path model_path = *model_arg;

    app::Application app;
    app.addWorker(
        [model_path](app::Application& app, graphics::Renderer& renderer) { renderLoop(app, renderer, model_path); },
        app.device(), app.swapchain()
    );
    app.run();

    return 0;
}
