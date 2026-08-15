#pragma once

#include <app/input_state.hpp>
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
        Window(Application& app, InputState& input, int width, int height, const char* title);
        ~Window();

        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        Application& application() const { return m_app; }
        InputState& input() const { return m_input; }

        VkSurfaceKHR createSurface(const gpu::Instance&) const;

        bool shouldClose() const;
        VkExtent2D framebufferExtent() const;
        static void poll();

        void setCursorCaptured(bool captured);
        void toggleCursorCapture() { setCursorCaptured(!m_cursor_captured); }
        bool isCursorCaptured() const { return m_cursor_captured; }

    private:
        Application& m_app;
        InputState& m_input;
        GLFWwindow* m_window = nullptr;
        bool m_cursor_captured = false;
};

} // namespace app
