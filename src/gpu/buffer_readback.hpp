#pragma once

#include <common.h>
#include <gpu/handle.hpp>

#include <sokol_gfx.h>

#include <optional>
#include <span>
#include <vector>

namespace gpu {

// Copies the contents of sokol storage buffers back to the CPU -- something sokol_gfx can't do
// on any backend. Holds a small ring of slots; each slot is a sokol storage buffer the GPU
// writes this frame (fill it in a pass, e.g. a compute pass) plus a CPU-readable copy of it.
//
// Per frame, on the sokol thread:
//   1. Slot& slot = readback.acquire();      // waits if this slot's previous copy is unread
//   2. write slot.buffer() in a pass, then sg_commit()
//   3. readback.submit();                      // after sg_commit: queue the copy to the CPU
//   4. readback.readOldest(...)                // when the ring is full, or at the end
// With N slots the CPU reads frame k while the GPU works on frames k+1 .. k+N-1, so nothing
// waits unless the CPU falls behind.
//
// Vulkan only, and no sokol changes: the copy is a command buffer of the engine's own,
// submitted on sokol's queue after sokol's frame (like gpu::CrossSemaphore's submits), reading
// the storage buffer through the fork's sg_vk_query_buffer_info. A Metal version would be a
// shared-storage buffer read in place.
class BufferReadback {
    public:
        BufferReadback() = default;
        ~BufferReadback();

        BufferReadback(BufferReadback&&) noexcept;
        BufferReadback& operator=(BufferReadback&&) noexcept;
        BufferReadback(const BufferReadback&) = delete;
        BufferReadback& operator=(const BufferReadback&) = delete;

        // `bytes` per slot, `slots` >= 1. nullopt (logged) off Vulkan or on any Vulkan failure.
        static std::optional<BufferReadback> create(usize bytes, u32 slots, const char* label);

        usize sizeBytes() const { return m_bytes; }

        // The storage buffer of the slot the next frame should write. If that slot still holds
        // a copy the CPU hasn't read, readOldest() is called on it first (into `on_ready`), so
        // nothing is ever overwritten unread.
        template <typename F>
        sg_buffer acquire(F&& on_ready) {
            if (m_slots[m_next].pending) {
                readOldest(on_ready);
            }
            return *m_slots[m_next].storage;
        }

        // After sg_commit(): queues the copy of the slot acquire() returned and moves to the
        // next slot. The barrier in the copy's command buffer orders it after every write the
        // committed frame made to the buffer.
        void submit();

        // Waits for the oldest queued copy, hands its bytes to `on_ready(std::span<const u8>)`
        // (valid only during the call), and frees its slot. False if nothing is queued.
        template <typename F>
        bool readOldest(F&& on_ready) {
            const std::optional<std::span<const u8>> bytes = waitOldest();
            if (!bytes) {
                return false;
            }
            on_ready(*bytes);
            releaseOldest();
            return true;
        }

        // Reads every queued copy, oldest first.
        template <typename F>
        void flush(F&& on_ready) {
            while (readOldest(on_ready)) {
            }
        }

    private:
        struct Slot {
                BufferHandle storage;    // sokol storage buffer the GPU writes
                void* staging = nullptr; // VkBuffer, host-visible copy target
                void* memory = nullptr;  // VkDeviceMemory backing `staging`, persistently mapped
                const u8* mapped = nullptr;
                void* command_buffer = nullptr; // VkCommandBuffer
                void* fence = nullptr;          // VkFence, signalled when the copy has landed
                bool pending = false;           // a copy is queued and not yet read
        };

        std::optional<std::span<const u8>> waitOldest();
        void releaseOldest();
        void destroy();

        usize m_bytes = 0;
        std::vector<Slot> m_slots;
        u32 m_next = 0;   // slot the next frame writes
        u32 m_oldest = 0; // oldest slot with a pending copy (valid when m_pending > 0)
        u32 m_pending = 0;

        void* m_device = nullptr;       // VkDevice (sokol's)
        void* m_queue = nullptr;        // VkQueue (sokol's)
        void* m_command_pool = nullptr; // VkCommandPool, on sokol's queue family
};

} // namespace gpu
