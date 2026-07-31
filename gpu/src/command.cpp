#include <gpu/command.hpp>

namespace gpu {

void imageBarrier(
    const vk::raii::CommandBuffer& cmd,
    vk::Image image,
    vk::ImageLayout old_layout,
    vk::ImageLayout new_layout,
    vk::PipelineStageFlags2 src_stage,
    vk::AccessFlags2 src_access,
    vk::PipelineStageFlags2 dst_stage,
    vk::AccessFlags2 dst_access
) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void submit(const Device& device, const vk::raii::Queue& queue, const SubmitInfo& info) {
    const vk::CommandBufferSubmitInfo cmd{.commandBuffer = **info.cmd};
    const vk::SemaphoreSubmitInfo wait{.semaphore = info.wait, .stageMask = info.wait_stage};
    const vk::SemaphoreSubmitInfo signal{.semaphore = info.signal, .stageMask = info.signal_stage};

    if (info.fence) {
        device->resetFences(info.fence);
    }
    queue.submit2(vk::SubmitInfo2{
        .waitSemaphoreInfoCount = info.wait ? 1u : 0u,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd,
        .signalSemaphoreInfoCount = info.signal ? 1u : 0u,
        .pSignalSemaphoreInfos = &signal,
    }, info.fence);
}

} // namespace gpu
