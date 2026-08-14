#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <graphics/camera.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>

// Hardcoded triangle + fixed camera -- renderMesh() is __DEV_ONLY__, so this whole call
// site is scaffolding until a real scene/model system replaces it.
void renderLoop(app::Application& app, graphics::Renderer& renderer) {
    RLOG(LL_INFO, "Starting Render Thread");

    const graphics::Vertex vertices[] = {
        {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const u32 indices[] = {0, 1, 2};
    graphics::Mesh triangle = graphics::Mesh::create(app.device(), vertices, indices);

    VkExtent2D extent = app.swapchain().extent();
    RLOG(LL_DEBUG, "Extent: " + std::to_string(extent.width) + " by " + std::to_string(extent.height));
    graphics::Camera camera{
        1.0472f, static_cast<f32>(extent.width) / static_cast<f32>(extent.height), 0.1f, 100.0f,
        {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
    };
    Eigen::Matrix4f model = Eigen::Matrix4f::Identity();

    while (app.running.load()) {
        renderer.beginFrame();
        renderer.renderMesh(triangle, model, camera);
        renderer.endFrame();
    }
}

int main() {
    initLog(1024 * 1024);
    RLOG(LL_INFO, "Starting Game Engine");

    app::Application app;
    app.addWorker(renderLoop, app.device(), app.swapchain());
    app.run();

    return 0;
}
