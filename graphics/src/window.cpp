#include <graphics/window.hpp>

#include <devtools/rlog.h>

#include <GLFW/glfw3.h>

namespace ge::gfx {

std::span<const char* const> requiredInstanceExtensions() {
    if (glfwInit() != GLFW_TRUE) {
        RLOG(LL_FATAL, "glfwInit failed");
    }
    u32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr) {
        RLOG(LL_FATAL, "Vulkan is not available to GLFW");
    }
    return {extensions, count};
}

Window::Window(int width, int height, const char* title) {
    if (glfwInit() != GLFW_TRUE) {
        RLOG(LL_FATAL, "glfwInit failed");
    }
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

vk::raii::SurfaceKHR Window::createSurface(const gpu::Instance& instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result =
        glfwCreateWindowSurface(static_cast<VkInstance>(instance.handle()), m_window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        RLOG(LL_FATAL, "glfwCreateWindowSurface failed: %d", result);
    }
    return instance.adoptSurface(surface);
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window) == GLFW_TRUE; }

vk::Extent2D Window::framebufferExtent() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {static_cast<u32>(width), static_cast<u32>(height)};
}

void Window::poll() { glfwPollEvents(); }

void Window::waitEvents() { glfwWaitEvents(); }

} // namespace ge::gfx
