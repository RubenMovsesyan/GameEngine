#define RLOG_IMPLEMENTATION
#include <devtools/rlog.h>

#include <gpu/buffer.hpp>
#include <graphics/renderer.hpp>
#include <graphics/window.hpp>

#include <array>
#include <cmath>
#include <cstring>

namespace {

// Smallest thing that fails if the VMA wiring is wrong.
void checkAllocator(const gpu::Device& device) {
    const gpu::Buffer buffer{device, 64, vk::BufferUsageFlagBits::eStorageBuffer, true};
    if (buffer.mapped() == nullptr) {
        RLOG(LL_FATAL, "host-visible buffer was not mapped");
    }
    std::memset(buffer.mapped(), 0xAB, 64);
    if (static_cast<const u8*>(buffer.mapped())[63] != 0xAB) {
        RLOG(LL_FATAL, "buffer readback mismatch");
    }
    RLOG(LL_INFO, "VMA allocator OK");
}

void run() {
    ge::gfx::Window window{1280, 720, "sandbox"};

    const gpu::Instance instance{{.required_extensions = ge::gfx::requiredInstanceExtensions()}};
    const vk::raii::SurfaceKHR surface = window.createSurface(instance);
    const gpu::Device device{instance, {.present_surface = *surface}};

    checkAllocator(device);

    // Declared last, so it is destroyed before the device.
    ge::gfx::Renderer renderer{device, *surface, window.framebufferExtent()};

    for (u32 frame = 0; !window.shouldClose(); frame++) {
        const vk::Extent2D extent = window.framebufferExtent();
        if (extent.width == 0 || extent.height == 0) {
            ge::gfx::Window::waitEvents(); // block while minimised instead of spinning
            continue;
        }
        ge::gfx::Window::poll();
        // Animated, so a frozen frame is distinguishable from a working loop.
        const float t = 0.5f + 0.5f * std::sin(static_cast<float>(frame) * 0.01f);
        renderer.drawFrame(vk::ClearColorValue{std::array{0.1f, t, 0.3f, 1.0f}}, extent);
    }
    device.waitIdle();
}

} // namespace

int main() {
    initLog(64 * 1024);
    try {
        run();
    } catch (const vk::SystemError& e) {
        RLOG(LL_FATAL, "vulkan: %s", e.what());
    } catch (const std::exception& e) {
        RLOG(LL_FATAL, "%s", e.what());
    }
    return 0;
}
