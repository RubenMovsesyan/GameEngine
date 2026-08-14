#include <graphics/renderer.hpp>

namespace graphics {

Renderer::Renderer(const gpu::Device& device, gpu::Swapchain& swapchain) : m_device(&device), m_swapchain(&swapchain) {
    m_cmd = device.nvrhi()->createCommandList();
    rebuildSwapchainSurfaces();
}

void Renderer::rebuildSwapchainSurfaces() {
    m_swapchain_surfaces.clear();
    for (u32 i = 0; i < m_swapchain->imageCount(); ++i) {
        m_swapchain_surfaces.push_back(Surface::fromSwapchainImage(*m_device, m_swapchain->texture(i), m_swapchain->extent()));
    }
}

void Renderer::beginFrame() {
    m_acquired = m_swapchain->acquire();
    if (!m_acquired.ok) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        return;
    }

    m_cmd->open();
}

void Renderer::endFrame() {
    if (!m_acquired.ok) {
        return; // beginFrame already recreated the swapchain; nothing to submit
    }

    const Surface& target = m_swapchain_surfaces[m_acquired.image_index];
    m_cmd->clearTextureFloat(target.texture(), nvrhi::AllSubresources, nvrhi::Color(1.0f, 0.0f, 1.0f, 1.0f));

    m_cmd->close();

    nvrhi::vulkan::IDevice* vk_device = m_device->vulkanDevice();
    vk_device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->imageAvailable(), 0);
    vk_device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->renderFinished(m_acquired.image_index), 0);
    m_device->nvrhi()->executeCommandList(m_cmd);

    // ponytail: no frames-in-flight pacing yet -- see .old/gpu/frame.hpp for the
    // fence-per-slot ring that replaces this once perf matters.
    vkDeviceWaitIdle(m_device->handle());

    if (!m_swapchain->present(m_acquired.image_index)) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
    }
}

} // namespace graphics
