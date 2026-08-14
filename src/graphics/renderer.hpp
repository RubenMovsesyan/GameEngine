#pragma once

#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <vector>

namespace graphics {

// Self-sufficient enough to drive its own swapchain-present loop -- meant to run
// entirely off a worker thread, no Window involved past construction. beginFrame()
// acquires the next swapchain image and opens the command list; endFrame() records
// whatever this frame does, closes, submits, and presents.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();
        void endFrame();

    private:
        void rebuildSwapchainSurfaces();

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;

        gpu::Swapchain* m_swapchain;
        std::vector<Surface> m_swapchain_surfaces;

        // Stashed between beginFrame() and endFrame(). !ok means beginFrame's acquire
        // failed (swapchain already recreated) -- endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
};

} // namespace graphics
