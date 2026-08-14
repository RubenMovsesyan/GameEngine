#pragma once

#include <gpu/device.hpp>

#include <vector>

namespace gpu {

// Takes a bare vk::SurfaceKHR, so it never learns what a window is.
class Swapchain {
    public:
        Swapchain(const Device&, vk::SurfaceKHR, vk::Extent2D want);

        Swapchain(Swapchain&&) = default;
        Swapchain& operator=(Swapchain&&) = default;

        struct Acquired {
                u32 image_index = 0;
                bool ok = false; // false => out of date, recreate and skip the frame
        };

        // Out-of-date and suboptimal are normal control flow, not errors, and never escape.
        [[nodiscard]] Acquired acquire(vk::Semaphore signal);
        [[nodiscard]] bool present(const vk::raii::Queue&, u32 image_index);

        void recreate(vk::Extent2D want) { build(want); }

        vk::Image image(u32 i) const { return m_images[i]; }
        vk::ImageView view(u32 i) const { return *m_views[i]; }
        // Per image, not per frame: nothing on the CPU says when a present is done with it.
        vk::Semaphore renderFinished(u32 i) const { return *m_render_finished[i]; }
        vk::Extent2D extent() const { return m_extent; }
        vk::Format format() const { return m_format; }

    private:
        void build(vk::Extent2D want);

        const Device* m_device = nullptr;
        vk::SurfaceKHR m_surface{};
        vk::raii::SwapchainKHR m_swapchain{nullptr};
        std::vector<vk::Image> m_images{};
        std::vector<vk::raii::ImageView> m_views{};
        std::vector<vk::raii::Semaphore> m_render_finished{};
        vk::Format m_format = vk::Format::eUndefined;
        vk::Extent2D m_extent{};
};

} // namespace gpu
