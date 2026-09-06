#pragma once

#include <common.h>

#include <optional>

namespace gpu {

// vulkan.h is pulled in only by the .cpp -- keep the header light. Mirrors
// VkPipelineStageFlags (a uint32 bitmask); callers pass VK_PIPELINE_STAGE_* values.
using PipelineStage = u32;

// An exportable Vulkan timeline semaphore, shared with another API (CUDA) by an opaque FD.
// Monotonic u64 value: no signal-before-wait hazard, unlike binary semaphores.
//
// The queue-side ops run empty vkQueueSubmit calls on sokol's graphics queue -- pure sync
// points that bracket sokol's own per-frame submit. They MUST be called only from the
// thread that runs the sokol frame callback (the same thread sokol submits on).
class CrossSemaphore {
    public:
        CrossSemaphore() = default;
        ~CrossSemaphore();

        CrossSemaphore(CrossSemaphore&&) noexcept;
        CrossSemaphore& operator=(CrossSemaphore&&) noexcept;
        CrossSemaphore(const CrossSemaphore&) = delete;
        CrossSemaphore& operator=(const CrossSemaphore&) = delete;

        static std::optional<CrossSemaphore> create(const char* label);

        // Empty submit on the graphics queue that blocks later queue work until the timeline
        // reaches `value`. `stage` is the VkPipelineStageFlags the wait applies at.
        void queueWait(u64 value, PipelineStage stage) const;
        // Empty submit that sets the timeline to `value` once prior queue work completes.
        void queueSignal(u64 value) const;

        u64 hostValue() const;                          // vkGetSemaphoreCounterValue
        bool hostWait(u64 value, u64 timeout_ns) const; // vkWaitSemaphores; false on timeout

        // Duplicated opaque FD for cuImportExternalSemaphore. Not owned by the caller.
        int fd() const { return m_fd; }

    private:
        // opaque, avoids leaking vulkan.h into the header
        void* m_semaphore = nullptr; // VkSemaphore
        void* m_device = nullptr;    // VkDevice
        void* m_queue = nullptr;     // VkQueue
        int m_fd = -1;
};

} // namespace gpu
