#pragma once

#include <gpu/device.hpp>

namespace gpu {

// Takes vk::raii::CommandBuffer so every command goes through the raii dispatcher.
void imageBarrier(
    const vk::raii::CommandBuffer& cmd,
    vk::Image image,
    vk::ImageLayout old_layout,
    vk::ImageLayout new_layout,
    vk::PipelineStageFlags2 src_stage,
    vk::AccessFlags2 src_access,
    vk::PipelineStageFlags2 dst_stage,
    vk::AccessFlags2 dst_access
);

// The single submission path for graphics and compute.
struct SubmitInfo {
        const vk::raii::CommandBuffer* cmd = nullptr;
        vk::Semaphore wait{};
        vk::PipelineStageFlags2 wait_stage = vk::PipelineStageFlagBits2::eAllCommands;
        vk::Semaphore signal{};
        vk::PipelineStageFlags2 signal_stage = vk::PipelineStageFlagBits2::eAllCommands;
        vk::Fence fence{}; // reset here, immediately before submitting
};

void submit(const Device&, const vk::raii::Queue&, const SubmitInfo&);

} // namespace gpu
