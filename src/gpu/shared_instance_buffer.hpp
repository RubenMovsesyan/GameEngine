#pragma once

#include <common.h>
#include <gpu/cross_semaphore.hpp>
#include <gpu/external_buffer.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>

#ifdef GAMEENGINE_CUDA
#include <gpu/cuda_interop.hpp>
#endif

namespace gpu {

// A per-instance GPU buffer written by a CUDA compute thread and drawn by the render thread,
// with no CPU copy in the loop. Owns the exportable Vulkan buffer(s) and the cross-API
// timeline semaphores, and hides two ways of keeping the two threads off the bytes at once:
//
//   Sync::Semaphore    one buffer, strict alternation. The compute thread holds the buffer,
//                      runs as many integration sub-steps as it likes, then releases it for
//                      exactly one render frame. Sim rate can exceed the frame rate; latency
//                      is minimal; no extra VRAM.
//   Sync::DoubleBuffer two buffers. The compute thread integrates freely into the back
//                      buffer and flips it to the front when the last frame that read it has
//                      retired. The render thread always draws the current front. Compute is
//                      fully unthrottled; ~1 frame more latency; 2x VRAM.
//
// Render-thread methods (consume*) must run on the sokol frame-callback thread. Compute-
// thread methods (attachCuda / produce*) must run on the one CUDA thread.
class SharedInstanceBuffer {
    public:
        enum class Sync { Semaphore, DoubleBuffer };

        struct Desc {
                usize instance_bytes = 0;
                u32 max_instances = 0;
                Sync sync = Sync::Semaphore;
        };

        SharedInstanceBuffer() = default;
        SharedInstanceBuffer(const SharedInstanceBuffer&) = delete;
        SharedInstanceBuffer& operator=(const SharedInstanceBuffer&) = delete;

        // Heap-allocated: the class holds std::atomic members and is neither copyable nor
        // movable. nullptr on failure.
        static std::unique_ptr<SharedInstanceBuffer> create(const Desc&);

        Sync syncMode() const { return m_sync; }
        u32 maxInstances() const { return m_max_instances; }
        usize instanceBytes() const { return m_instance_bytes; }

        // --- render thread ---
        // The buffer to draw as instance data this frame (plus the queue-side wait in
        // Semaphore mode). Pair every call with consumeEnd() after sg_commit().
        BufferHandle consumeBegin();
        void consumeEnd();

#ifdef GAMEENGINE_CUDA
        // --- compute thread, once at start ---
        // Brings up CUDA on the matching GPU and maps the buffer(s) + semaphores. Call from
        // the compute thread before the first produceBegin().
        bool attachCuda();

        // Tears down every CUDA import + the context. The compute thread MUST call this
        // (from itself) before it exits, so the destructor -- which may run on another
        // thread -- only has Vulkan objects left to release.
        void detachCuda();

        CudaInterop& cuda() { return *m_cuda; }
        // Raw mapped pointer of buffer `index` (0, or 0/1 in DoubleBuffer mode) for one-time
        // seeding via cuMemcpyHtoD. Not gated -- call before the render/compute loop starts.
        CUdeviceptr rawDevicePtr(u32 index) const { return m_device_ptr[index]; }

        // --- compute thread, per tick ---
        // Blocks (on the CUDA stream) until the buffer to write is free, then returns its
        // device pointer. Write, then call produceEnd().
        CUdeviceptr produceBegin();
        void produceEnd();
        CUstream stream() const { return m_cuda->stream(); }
#endif

    private:
        Sync m_sync = Sync::Semaphore;
        u32 m_max_instances = 0;
        usize m_instance_bytes = 0;
        usize m_total_bytes = 0;

        std::array<ExternalBuffer, 2> m_buffers{};
        u32 m_buffer_count = 1;

        // Semaphore mode: compute signals sim_ready(tick); render waits it, then signals
        // render_done(frame). DoubleBuffer mode: only render_done is used.
        std::optional<CrossSemaphore> m_sim_ready;
        std::optional<CrossSemaphore> m_render_done;

        std::atomic<u64> m_frame{0};          // render frames started
        std::atomic<u64> m_published_sim{0};  // newest sim tick the compute thread has signalled
        std::atomic<u32> m_read_index{0};     // DoubleBuffer: which buffer the render thread draws

#ifdef GAMEENGINE_CUDA
        std::optional<CudaInterop> m_cuda;
        std::array<CUdeviceptr, 2> m_device_ptr{0, 0};
        int m_sim_ready_sem = -1;
        int m_render_done_sem = -1;
        u64 m_sim_tick = 0;      // compute-thread local counter
        u32 m_write_index = 0;   // DoubleBuffer: buffer the compute thread writes
        u64 m_flip_frame = 0;    // DoubleBuffer: render frame at the last flip
#endif
};

} // namespace gpu
