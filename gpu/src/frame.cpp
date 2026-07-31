#include <gpu/frame.hpp>

namespace gpu {

FrameRing::FrameRing(const Device& device, u32 queue_family, u32 frames_in_flight)
    : m_device(&*device),
      m_pool(*device, vk::CommandPoolCreateInfo{
                          .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                          .queueFamilyIndex = queue_family,
                      }) {
    m_cmds = vk::raii::CommandBuffers{
        *device,
        vk::CommandBufferAllocateInfo{
            .commandPool = *m_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = frames_in_flight,
        },
    };
    for (u32 i = 0; i < frames_in_flight; i++) {
        m_fences.emplace_back(*device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        m_semaphores.emplace_back(*device, vk::SemaphoreCreateInfo{});
    }
}

FrameRing::Frame FrameRing::begin() const {
    // Deliberately no resetFences here: submit() does it, so bailing out is safe.
    while (m_device->waitForFences(*m_fences[m_slot], vk::True, UINT64_MAX) == vk::Result::eTimeout) {
    }
    m_cmds[m_slot].reset();
    return {m_cmds[m_slot], *m_semaphores[m_slot], *m_fences[m_slot]};
}

} // namespace gpu
