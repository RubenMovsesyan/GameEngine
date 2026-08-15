#pragma once

#include <gpu/device.hpp>

#include <vector>

namespace gpu {

// Takes a bare VkSurfaceKHR, so it never learns what a window is.
class Swapchain {
    public:
        Swapchain(const Device&, VkSurfaceKHR, VkExtent2D want);
        ~Swapchain();

        Swapchain(Swapchain&&) = delete;
        Swapchain& operator=(Swapchain&&) = delete;
        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;

        struct Acquired {
                u32 image_index = 0;
                bool ok = false; // false => out of date, recreate and skip the frame
        };

        [[nodiscard]] Acquired acquire();
        [[nodiscard]] bool present(u32 image_index);

        void recreate(VkExtent2D want) { build(want); }

        nvrhi::TextureHandle texture(u32 i) const { return m_textures[i]; }
        u32 imageCount() const { return static_cast<u32>(m_textures.size()); }
        VkExtent2D extent() const { return m_extent; }
        VkFormat format() const { return m_format; }

        // The semaphore acquire() just signaled -- valid until the next acquire() call.
        VkSemaphore imageAvailable() const { return m_image_available[m_frame]; }
        // Per image, not per frame: nothing on the CPU says when a present is done with it.
        VkSemaphore renderFinished(u32 image_index) const { return m_render_finished[image_index]; }

    private:
        void build(VkExtent2D want);
        void destroySwapchain();
        void destroySemaphores();

        const Device* m_device = nullptr;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        std::vector<nvrhi::TextureHandle> m_textures{};
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VkExtent2D m_extent{};

        // image_available is keyed by frame (image unknown until acquire() returns); render_finished by image index.
        std::vector<VkSemaphore> m_image_available{};
        std::vector<VkSemaphore> m_render_finished{};
        u32 m_frame = 0;
};

} // namespace gpu
