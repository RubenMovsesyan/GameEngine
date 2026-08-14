#pragma once

#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <vector>

namespace graphics {

// Self-sufficient enough to drive its own swapchain-present loop -- meant to run
// entirely off a worker thread, no Window involved past construction. beginFrame()
// acquires the next swapchain image and opens the command list; endFrame() records the
// render pipeline, closes, submits, and presents. That's the whole public surface a
// render loop needs.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();
        void endFrame();

    private:
        void rebuildSwapchainSurfaces();
        void renderTriangle(const Surface& target);
        void renderTexturedQuad(const Surface& target, nvrhi::ITexture* source);

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;

        nvrhi::GraphicsPipelineHandle m_triangle_pipeline;

        nvrhi::GraphicsPipelineHandle m_quad_pipeline;
        nvrhi::BindingLayoutHandle m_quad_binding_layout;
        nvrhi::SamplerHandle m_sampler;

        gpu::Swapchain* m_swapchain;
        std::vector<Surface> m_swapchain_surfaces;
        // Proves the Surface primitive: an offscreen render sampled into the main pass
        // instead of blitted, the same mechanism a portal or mirror would use.
        Surface m_offscreen;

        // Stashed between beginFrame() and endFrame(). !ok means beginFrame's acquire
        // failed (swapchain already recreated) -- endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
};

} // namespace graphics
