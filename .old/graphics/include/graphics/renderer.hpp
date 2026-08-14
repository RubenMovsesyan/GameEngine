#pragma once

#include <gpu/frame.hpp>
#include <gpu/swapchain.hpp>

namespace ge::gfx {

class Renderer {
    public:
        Renderer(const gpu::Device&, vk::SurfaceKHR, vk::Extent2D extent);

        void drawFrame(vk::ClearColorValue clear, vk::Extent2D extent);

    private:
        const gpu::Device* m_device = nullptr;
        gpu::Swapchain m_swapchain;
        gpu::FrameRing m_frames;
        bool m_needs_recreate = false;
};

} // namespace ge::gfx
