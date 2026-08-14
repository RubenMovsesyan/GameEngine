#define RLOG_IMPLEMENTATION
#include <devtools/rlog.h>

#include <app/window.hpp>
#include <gpu/device.hpp>
#include <gpu/instance.hpp>
#include <gpu/swapchain.hpp>
#include <graphics/renderer.hpp>
#include <graphics/surface.hpp>

#include <vector>

namespace {

bool sameExtent(VkExtent2D a, VkExtent2D b) {
    return a.width == b.width && a.height == b.height;
}

} // namespace

int main() {
    initLog(1024 * 1024);

    app::Window window{1280, 720, "GameEngine"};

    gpu::InstanceConfig instance_cfg{};
    instance_cfg.required_extensions = app::requiredInstanceExtensions();
    gpu::Instance instance{instance_cfg};

    VkSurfaceKHR surface = window.createSurface(instance);

    gpu::DeviceConfig device_cfg{};
    device_cfg.present_surface = surface;
    gpu::Device device{instance, device_cfg};

    {
        gpu::Swapchain swapchain{device, surface, window.framebufferExtent()};

        std::vector<graphics::Surface> swapchain_surfaces;
        auto rebuildSwapchainSurfaces = [&] {
            swapchain_surfaces.clear();
            for (u32 i = 0; i < swapchain.imageCount(); ++i) {
                swapchain_surfaces.push_back(graphics::Surface::fromSwapchainImage(device, swapchain.texture(i), swapchain.extent()));
            }
        };
        rebuildSwapchainSurfaces();

        graphics::Renderer renderer{device};
        // Proves the Surface primitive: an offscreen render sampled into the main pass
        // instead of blitted, the same mechanism a portal or mirror would use.
        graphics::Surface offscreen = graphics::Surface::createOffscreen(device, VkExtent2D{512, 512});

        while (!window.shouldClose()) {
            app::Window::poll();

            VkExtent2D want = window.framebufferExtent();
            if (want.width == 0 || want.height == 0) {
                continue;
            }

            if (!sameExtent(want, swapchain.extent())) {
                vkDeviceWaitIdle(device.handle());
                swapchain.recreate(want);
                rebuildSwapchainSurfaces();
                continue;
            }

            gpu::Swapchain::Acquired acquired = swapchain.acquire();
            if (!acquired.ok) {
                vkDeviceWaitIdle(device.handle());
                swapchain.recreate(want);
                rebuildSwapchainSurfaces();
                continue;
            }

            const graphics::Surface& target = swapchain_surfaces[acquired.image_index];

            renderer.beginFrame();
            renderer.renderTriangle(offscreen);
            renderer.renderTriangle(target);
            renderer.renderTexturedQuad(target, offscreen.texture());
            renderer.endFrame(swapchain.imageAvailable(), swapchain.renderFinished(acquired.image_index));

            if (!swapchain.present(acquired.image_index)) {
                vkDeviceWaitIdle(device.handle());
                swapchain.recreate(want);
                rebuildSwapchainSurfaces();
            }
        }

        vkDeviceWaitIdle(device.handle());
        // swapchain (and everything built from it) is destroyed at the end of this
        // scope, before the surface it was created from goes away below.
    }

    vkDestroySurfaceKHR(instance.handle(), surface, nullptr);
    return 0;
}
