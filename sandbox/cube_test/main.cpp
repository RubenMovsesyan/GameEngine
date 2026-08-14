#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <graphics/camera.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>
#include <model/model.hpp>

#include <chrono>
#include <cmath>

// Hardcoded cube + fixed camera, wrapped in a one-part Model.
void renderLoop(app::Application& app, graphics::Renderer& renderer) {
    RLOG(LL_INFO, "Starting Render Thread");

    // Unit cube, 4 verts per face (24 total) -- a shared corner can't carry one "correct"
    // normal for all 3 faces it touches, so each face gets its own copy of its corners
    // with that face's flat outward normal. Winding is CCW per face as seen from
    // outside, matching this pipeline's default back-face culling. UV is a plain 0..1
    // quad per face, following that face's own triangle boundary order -- unused until
    // there's a texture to sample, but geometrically correct now rather than later.
    const graphics::Vertex vertices[] = {
        // front (z+), normal (0,0,1)
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // 0
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  // 1
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},   // 2
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},  // 3
        // back (z-), normal (0,0,-1)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}}, // 4
        {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},  // 5
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},   // 6
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},  // 7
        // right (x+), normal (1,0,0)
        {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // 8
        {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}}, // 9
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},  // 10
        {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},   // 11
        // left (x-), normal (-1,0,0)
        {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // 12
        {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},   // 13
        {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},  // 14
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, // 15
        // top (y+), normal (0,1,0)
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},  // 16
        {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},   // 17
        {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},  // 18
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // 19
        // bottom (y-), normal (0,-1,0)
        {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},  // 20
        {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},   // 21
        {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},  // 22
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}}, // 23
    };
    const u32 indices[] = {
        0, 1, 2, 0, 2, 3,     // front
        4, 6, 5, 4, 7, 6,     // back
        8, 9, 10, 8, 10, 11,  // right
        12, 13, 14, 12, 14, 15, // left
        16, 17, 18, 16, 18, 19, // top
        20, 22, 21, 20, 23, 22, // bottom
    };
    model::Model cube_model;
    cube_model.addPart(graphics::Mesh::create(app.device(), vertices, indices));

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
        cube_model.render(renderer, world_transform, camera);
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
