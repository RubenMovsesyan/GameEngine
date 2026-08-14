#pragma once

#include <graphics/surface.hpp>

namespace graphics {

// Draws into whatever Surface it's given, swapchain- or texture-backed alike. One
// command list per frame: beginFrame(), any number of draws in dependency order (an
// offscreen Surface written by one draw and sampled by a later one just works, NVRHI
// tracks the transition within the command list), endFrame().
class Renderer {
    public:
        explicit Renderer(const gpu::Device&);

        void beginFrame();
        void renderTriangle(const Surface& target);
        void renderTexturedQuad(const Surface& target, nvrhi::ITexture* source);
        // wait/signal are the swapchain's acquire/present semaphores, or VK_NULL_HANDLE
        // for offscreen-only work.
        void endFrame(VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE);

    private:
        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;

        nvrhi::GraphicsPipelineHandle m_triangle_pipeline;

        nvrhi::GraphicsPipelineHandle m_quad_pipeline;
        nvrhi::BindingLayoutHandle m_quad_binding_layout;
        nvrhi::SamplerHandle m_sampler;
};

} // namespace graphics
