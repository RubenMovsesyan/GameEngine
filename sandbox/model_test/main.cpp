#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <graphics/camera.hpp>
#include <graphics/renderer.hpp>
#include <model/gltf_loader.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>

// Loads a glTF/glb model given at construction and orbits a fixed camera around it.
void renderLoop(app::Application& app, graphics::Renderer& renderer, const std::filesystem::path& model_path) {
    RLOG(LL_INFO, "Starting Render Thread");

    model::Model scene = model::loadGltf(app.device(), model_path);

    VkExtent2D extent = app.swapchain().extent();
    RLOG(LL_DEBUG, "Extent: " + std::to_string(extent.width) + " by " + std::to_string(extent.height));
    graphics::Camera camera{
        1.0472f, static_cast<f32>(extent.width) / static_cast<f32>(extent.height), 0.1f, 100.0f,
        {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
    };
    Eigen::Matrix4f world_transform = Eigen::Matrix4f::Identity();

    while (app.running.load()) {
        f32 t = std::chrono::duration<f32>(std::chrono::steady_clock::now().time_since_epoch()).count();

        constexpr f32 kOrbitRadius = 5.0f;
        Eigen::Vector3f* position = camera.mutablePosition();
        position->x() = kOrbitRadius * std::cos(t);
        position->z() = kOrbitRadius * std::sin(t);
        camera.setLookAt({0.0f, 0.0f, 0.0f});

        renderer.beginFrame();
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
