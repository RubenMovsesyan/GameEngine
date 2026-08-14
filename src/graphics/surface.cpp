#include <graphics/surface.hpp>

namespace graphics {

Surface Surface::fromSwapchainImage(const gpu::Device& device, nvrhi::TextureHandle image, VkExtent2D extent) {
    Surface surface;
    surface.m_texture = image;
    surface.m_framebuffer = device.nvrhi()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(image));
    surface.m_extent = extent;
    return surface;
}

Surface Surface::createOffscreen(const gpu::Device& device, VkExtent2D extent, nvrhi::Format format) {
    nvrhi::TextureDesc desc{};
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = format;
    desc.isRenderTarget = true;
    desc.isShaderResource = true;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "offscreen surface";

    Surface surface;
    surface.m_texture = device.nvrhi()->createTexture(desc);
    surface.m_framebuffer = device.nvrhi()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(surface.m_texture));
    surface.m_extent = extent;
    return surface;
}

} // namespace graphics
