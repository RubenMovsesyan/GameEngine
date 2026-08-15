#include <app/application.hpp>

namespace app {

Application::OwnedSurface::~OwnedSurface() {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
}

Application::Application()
    : m_window(*this, m_input, 1280, 720, "GameEngine"),
      m_instance(gpu::InstanceConfig{.required_extensions = requiredInstanceExtensions()}),
      m_surface(m_instance.handle(), m_window.createSurface(m_instance)),
      m_device(m_instance, gpu::DeviceConfig{.present_surface = m_surface}),
      m_swapchain(m_device, m_surface, m_window.framebufferExtent()) {}

Application::~Application() {
    for (auto& worker : m_workers) {
        worker->join();
    }
    vkDeviceWaitIdle(m_device.handle());
}

void Application::run() {
    running = true;

    for (auto& worker : m_workers) {
        worker->run();
    }

    while (running.load() && !m_window.shouldClose()) {
        Window::poll();
    }

    running = false;
}

} // namespace app
