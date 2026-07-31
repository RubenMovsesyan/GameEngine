#pragma once

#include <gpu/instance.hpp>

#include <span>

struct GLFWwindow;

namespace ge::gfx {

// Initialises GLFW and returns its required Vulkan instance extensions.
std::span<const char* const> requiredInstanceExtensions();

class Window {
    public:
        Window(int width, int height, const char* title);
        ~Window();

        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        vk::raii::SurfaceKHR createSurface(const gpu::Instance&) const;

        bool shouldClose() const;
        vk::Extent2D framebufferExtent() const;
        static void poll();
        static void waitEvents();

    private:
        GLFWwindow* m_window = nullptr;
};

} // namespace ge::gfx
