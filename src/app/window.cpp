#include <app/window.hpp>

#include <rlog.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace {

void keyCallback(GLFWwindow* handle, int key, int, int action, int) {
    static_cast<app::Window*>(glfwGetWindowUserPointer(handle))->input().handleKey(key, action);
}

void mouseButtonCallback(GLFWwindow* handle, int button, int action, int) {
    static_cast<app::Window*>(glfwGetWindowUserPointer(handle))->input().handleMouseButton(button, action);
}

void cursorPosCallback(GLFWwindow* handle, double x, double y) {
    static_cast<app::Window*>(glfwGetWindowUserPointer(handle))->input().handleCursorPos(x, y);
}

void scrollCallback(GLFWwindow* handle, double x_offset, double y_offset) {
    static_cast<app::Window*>(glfwGetWindowUserPointer(handle))->input().handleScroll(x_offset, y_offset);
}

} // namespace

namespace app {

std::span<const char* const> requiredInstanceExtensions() {
    glfwInit();
    u32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    return {extensions, count};
}

Window::Window(Application& app, InputState& input, int width, int height, const char* title) : m_app(app), m_input(input) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (m_window == nullptr) {
        RLOG(LL_FATAL, "glfwCreateWindow failed");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
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

void Window::setCursorCaptured(bool captured) {
    glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    m_cursor_captured = captured;
}

} // namespace app
