#include <graphics/renderer.hpp>

#include <gpu/command.hpp>

namespace ge::gfx {

Renderer::Renderer(const gpu::Device& device, vk::SurfaceKHR surface, vk::Extent2D extent)
    : m_device(&device), m_swapchain(device, surface, extent), m_frames(device, device.families().graphics) {}

void Renderer::drawFrame(vk::ClearColorValue clear, vk::Extent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return; // minimised
    }
    if (m_needs_recreate || extent != m_swapchain.extent()) {
        m_device->waitIdle();
        m_swapchain.recreate(extent);
        m_needs_recreate = false;
    }

    const gpu::FrameRing::Frame frame = m_frames.begin();

    const auto acquired = m_swapchain.acquire(frame.semaphore);
    if (!acquired.ok) {
        m_needs_recreate = true; // fence is still signalled, so the slot stays usable
        return;
    }
    const vk::Image image = m_swapchain.image(acquired.image_index);

    frame.cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Contents are garbage and loadOp=eClear overwrites them, so eUndefined is correct.
    gpu::imageBarrier(
        frame.cmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eTopOfPipe, {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite
    );

    const vk::RenderingAttachmentInfo color{
        .imageView = m_swapchain.view(acquired.image_index),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    // Nothing is drawn yet: the loadOp is the frame.
    frame.cmd.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, m_swapchain.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    });
    frame.cmd.endRendering();

    // No dstAccess: the present engine's visibility comes from the semaphore.
    gpu::imageBarrier(
        frame.cmd, image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eBottomOfPipe, {}
    );

    frame.cmd.end();

    gpu::submit(*m_device, m_device->graphicsQueue(), {
        .cmd = &frame.cmd,
        .wait = frame.semaphore,
        .wait_stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .signal = m_swapchain.renderFinished(acquired.image_index),
        .fence = frame.fence,
    });

    if (!m_swapchain.present(m_device->presentQueue(), acquired.image_index)) {
        m_needs_recreate = true;
    }
    m_frames.advance();
}

} // namespace ge::gfx
