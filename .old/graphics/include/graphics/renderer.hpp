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

        // Hardcoded triangle: no vertex input, no descriptors. Viewport/scissor are
        // dynamic pipeline state, so this never needs rebuilding on resize.
        vk::raii::PipelineLayout m_pipeline_layout{nullptr};
        vk::raii::Pipeline m_pipeline{nullptr};
};

} // namespace ge::gfx
