#include <app/window.hpp>

#include <rlog.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace app {

std::span<const char* const> requiredInstanceExtensions() {
    glfwInit();
    u32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    return {extensions, count};
}

Window::Window(Application& app, int width, int height, const char* title) : m_app(app) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (m_window == nullptr) {
        RLOG(LL_FATAL, "glfwCreateWindow failed");
    }
}

Window::~Window() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

VkSurfaceKHR Window::createSurface(const gpu::Instance& instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance.handle(), m_window, nullptr, &surface) != VK_SUCCESS) {
        RLOG(LL_FATAL, "glfwCreateWindowSurface failed");
    }
    return surface;
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

VkExtent2D Window::framebufferExtent() const {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {static_cast<u32>(width), static_cast<u32>(height)};
}

void Window::poll() {
    glfwPollEvents();
}

} // namespace app
