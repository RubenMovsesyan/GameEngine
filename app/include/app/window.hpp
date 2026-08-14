#pragma once

#include <gpu/instance.hpp>

#include <span>

struct GLFWwindow;

namespace app {

// Initialises GLFW and returns its required Vulkan instance extensions.
std::span<const char* const> requiredInstanceExtensions();

class Window {
    public:
        Window(int width, int height, const char* title);
        ~Window();

        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        VkSurfaceKHR createSurface(const gpu::Instance&) const;

        bool shouldClose() const;
        VkExtent2D framebufferExtent() const;
        static void poll();

    private:
        GLFWwindow* m_window = nullptr;
};

} // namespace app
