#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <graphics/camera.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>

#include <chrono>
#include <cmath>

// Hardcoded cube + fixed camera -- renderMesh() is __DEV_ONLY__, so this whole call
// site is scaffolding until a real scene/model system replaces it.
void renderLoop(app::Application& app, graphics::Renderer& renderer) {
    RLOG(LL_INFO, "Starting Render Thread");

    // Unit cube, corners only (8 verts) -- winding is CCW per face as seen from outside,
    // matching this pipeline's default back-face culling.
    const graphics::Vertex vertices[] = {
        {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},  // 0: front bottom left  (red)
        {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},   // 1: front bottom right (green)
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // 2: front top right    (blue)
        {{-0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},   // 3: front top left     (yellow)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 4: back bottom left   (magenta)
        {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},  // 5: back bottom right  (cyan)
        {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},   // 6: back top right     (white)
        {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},  // 7: back top left      (gray)
    };
    const u32 indices[] = {
        0, 1, 2, 0, 2, 3, // front
        4, 6, 5, 4, 7, 6, // back
        1, 5, 6, 1, 6, 2, // right
        0, 3, 7, 0, 7, 4, // left
        3, 2, 6, 3, 6, 7, // top
        0, 5, 1, 0, 4, 5, // bottom
    };
    graphics::Mesh cube = graphics::Mesh::create(app.device(), vertices, indices);

    VkExtent2D extent = app.swapchain().extent();
    RLOG(LL_DEBUG, "Extent: " + std::to_string(extent.width) + " by " + std::to_string(extent.height));
    graphics::Camera camera{
        1.0472f, static_cast<f32>(extent.width) / static_cast<f32>(extent.height), 0.1f, 100.0f,
        {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
    };
    Eigen::Matrix4f model = Eigen::Matrix4f::Identity();

    while (app.running.load()) {
        f32 t = std::chrono::duration<f32>(std::chrono::steady_clock::now().time_since_epoch()).count();

        constexpr f32 kOrbitRadius = 5.0f;
        Eigen::Vector3f* position = camera.mutablePosition();
        position->x() = kOrbitRadius * std::cos(t);
        position->z() = kOrbitRadius * std::sin(t);
        camera.setLookAt({0.0f, 0.0f, 0.0f});

        renderer.beginFrame();
        renderer.renderMesh(cube, model, camera);
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
