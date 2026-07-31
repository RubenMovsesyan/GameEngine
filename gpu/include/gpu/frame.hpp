#pragma once

#include <gpu/device.hpp>

#include <vector>

namespace gpu {

// N slots of {command buffer, fence, semaphore}. Knows nothing about swapchains, so a
// compute loop uses it unchanged. The only frames-in-flight bookkeeping in the engine.
class FrameRing {
    public:
        struct Frame {
                const vk::raii::CommandBuffer& cmd;
                vk::Semaphore semaphore;
                vk::Fence fence;
        };

        FrameRing(const Device&, u32 queue_family, u32 frames_in_flight = 2);

        FrameRing(FrameRing&&) = default;
        FrameRing& operator=(FrameRing&&) = default;

        // Waits on this slot's fence and resets its command buffer. Deliberately does not
        // reset the fence: submit() does, so bailing out after begin() is safe.
        Frame begin() const;

        void advance() { m_slot = (m_slot + 1) % m_cmds.size(); }

    private:
        const vk::raii::Device* m_device = nullptr;
        vk::raii::CommandPool m_pool{nullptr};
        std::vector<vk::raii::CommandBuffer> m_cmds{};
        std::vector<vk::raii::Fence> m_fences{};
        std::vector<vk::raii::Semaphore> m_semaphores{};
        u32 m_slot = 0;
};

} // namespace gpu
