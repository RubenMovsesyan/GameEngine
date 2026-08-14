#pragma once

#include <gpu/device.hpp>

namespace graphics {

// A render target that is either the window (swapchain-backed, present-only) or an
// offscreen texture (render-target + sampled, so a later pass can bind it as input).
// Either way, the same object: something to render into, or to draw with as a texture.
class Surface {
    public:
        static Surface fromSwapchainImage(const gpu::Device&, nvrhi::TextureHandle, VkExtent2D);
        static Surface createOffscreen(const gpu::Device&, VkExtent2D, nvrhi::Format format = nvrhi::Format::BGRA8_UNORM);

        nvrhi::ITexture* texture() const { return m_texture; }
        nvrhi::IFramebuffer* framebuffer() const { return m_framebuffer; }
        VkExtent2D extent() const { return m_extent; }

    private:
        Surface() = default;

        nvrhi::TextureHandle m_texture;
        nvrhi::FramebufferHandle m_framebuffer;
        VkExtent2D m_extent{};
};

} // namespace graphics
