#pragma once

#include <gpu/instance.hpp>

#include <span>

struct GLFWwindow;

namespace app {

class Application;

// Initialises GLFW and returns its required Vulkan instance extensions.
std::span<const char* const> requiredInstanceExtensions();

// Strictly the window: GLFW, the surface, input polling.
class Window {
    public:
        Window(Application& app, int width, int height, const char* title);
        ~Window();

        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        Application& application() const { return m_app; }

        VkSurfaceKHR createSurface(const gpu::Instance&) const;

        bool shouldClose() const;
        VkExtent2D framebufferExtent() const;
        static void poll();

    private:
        Application& m_app;
        GLFWwindow* m_window = nullptr;
};

} // namespace app
