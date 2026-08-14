#pragma once

#include <app/window.hpp>
#include <app/worker.hpp>
#include <gpu/device.hpp>
#include <gpu/instance.hpp>
#include <gpu/swapchain.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace app {

// Owns the window, the GPU device and swapchain, and the workers. Window handles
// strictly window concerns and hands events back here. Rendering lives entirely off
// this class -- graphics::Renderer owns the swapchain-present loop itself, run from a
// worker (see main.cpp).
class Application {
    public:
        Application();
        ~Application();

        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        void run();

        std::atomic<bool> running = false;

        gpu::Device& device() { return m_device; }
        gpu::Swapchain& swapchain() { return m_swapchain; }

        // UserData is inferred from work's second parameter; args construct it.
        template <typename F, typename... Args>
        void addWorker(F&& work, Args&&... args) {
            using UserData = typename detail::WorkerCallableTraits<std::decay_t<F>>::UserData;
            m_workers.push_back(std::make_unique<Worker<UserData>>(
                *this, std::function<void(Application&, UserData&)>(std::forward<F>(work)), UserData(std::forward<Args>(args)...)
            ));
        }

    private:
        // Owns the VkSurfaceKHR: destructor order is declaration order reversed, and
        // this must outlive the swapchain built from it but not outlive the instance
        // it was created from -- see the member declaration order below.
        class OwnedSurface {
            public:
                OwnedSurface(VkInstance instance, VkSurfaceKHR surface) : m_instance(instance), m_surface(surface) {}
                ~OwnedSurface();

                OwnedSurface(OwnedSurface&&) = delete;
                OwnedSurface& operator=(OwnedSurface&&) = delete;
                OwnedSurface(const OwnedSurface&) = delete;
                OwnedSurface& operator=(const OwnedSurface&) = delete;

                operator VkSurfaceKHR() const { return m_surface; }

            private:
                VkInstance m_instance;
                VkSurfaceKHR m_surface;
        };

        Window m_window;
        gpu::Instance m_instance;
        OwnedSurface m_surface;
        gpu::Device m_device;
        gpu::Swapchain m_swapchain;

        std::vector<std::unique_ptr<WorkerBase>> m_workers;
};

} // namespace app
