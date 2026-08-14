#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <graphics/renderer.hpp>

void renderLoop(app::Application& app, graphics::Renderer& renderer) {
    while (app.running.load()) {
        renderer.beginFrame();
        renderer.endFrame();
    }
}

int main() {
    initLog(1024 * 1024);

    app::Application app;
    app.addWorker(renderLoop, app.device(), app.swapchain());
    app.run();

    return 0;
}
