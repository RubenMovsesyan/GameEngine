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
// timeline semaphores, and hides three ways of keeping the two threads off the bytes at once:
//
//   Sync::Semaphore    one buffer, strict alternation. The compute thread holds the buffer,
//                      runs as many integration sub-steps as it likes, then releases it for
//                      exactly one render frame. Sim rate can exceed the frame rate; latency
//                      is minimal; no extra VRAM.
//   Sync::DoubleBuffer two buffers. The compute thread integrates freely into the back
//                      buffer and flips it to the front when the last frame that read it has
//                      retired. Compute unthrottled; ~1 frame more latency; 2x VRAM.
//   Sync::SimPriority  the compute thread owns a PRIVATE authoritative buffer it hammers
//                      with no render-gated wait, ever. When the render thread asks (an
//                      atomic), the next sim iteration copies the private buffer into a free
//                      slot of a 3-buffer ring the renderer samples. Render stalls only drop
//                      snapshots -- the sim is never blocked. ~3x VRAM (ring only).
//
// Render-thread methods (consume*) run on the sokol frame-callback thread. Compute-thread
// methods (attachCuda / produce* / publishIfRequested / primeSimState) run on the one CUDA
// worker thread.
class SharedInstanceBuffer {
    public:
        enum class Sync { Semaphore, DoubleBuffer, SimPriority };

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
        // The buffer to draw as instance data this frame (plus the queue-side wait, except
        // in DoubleBuffer mode). Pair every call with consumeEnd() after sg_commit().
        BufferHandle consumeBegin();
        void consumeEnd();

#ifdef GAMEENGINE_CUDA
        // --- compute thread, once at start ---
        // Brings up CUDA on the matching GPU and maps the buffer(s) + semaphores (and, in
        // SimPriority mode, allocates the private buffer + a high-priority stream). Call
        // from the compute thread before anything else.
        bool attachCuda();

        // Tears down every CUDA import + the context + the private buffer. The compute
        // thread MUST call this (from itself) before it exits, so the destructor -- which
        // may run on another thread -- only has Vulkan objects left to release.
        void detachCuda();

        CudaInterop& cuda() { return *m_cuda; }

        // --- Semaphore / DoubleBuffer mode, compute thread, per tick ---
        // Blocks (on the default CUDA stream) until the buffer to write is free, then
        // returns its device pointer. Write, then call produceEnd().
        CUdeviceptr produceBegin();
        void produceEnd();
        CUstream stream() const { return m_cuda->stream(); }
        // One-time seeding of buffer `index` (0, or 0/1 in DoubleBuffer) via cuMemcpyHtoD.
        CUdeviceptr rawDevicePtr(u32 index) const { return m_device_ptr[index]; }

        // --- SimPriority mode, compute thread ---
        // The private authoritative buffer the sim writes in place. Never drawn.
        CUdeviceptr simStatePtr() const { return m_sim_state; }
        // The high-priority stream the sim kernels + the publish copy run on.
        CUstream simStream() const { return m_sim_stream; }
        // Seed the private buffer and the first ring slot once, before the loop.
        bool primeSimState(const void* seed, usize bytes);
        // If the render thread asked for a fresh snapshot and a ring slot is free, enqueue
        // (on simStream) a copy of the private buffer into it and publish it. Never blocks;
        // a busy ring just means the request is retried on the next call.
        void publishIfRequested();
#endif

    private:
        Sync m_sync = Sync::Semaphore;
        u32 m_max_instances = 0;
        usize m_instance_bytes = 0;
        usize m_total_bytes = 0;

        std::array<ExternalBuffer, 3> m_buffers{};
        u32 m_buffer_count = 1; // 1 (Semaphore), 2 (DoubleBuffer), 3 (SimPriority)

        std::optional<CrossSemaphore> m_sim_ready;    // Semaphore mode
        std::optional<CrossSemaphore> m_render_done;  // all modes
        std::optional<CrossSemaphore> m_publish_done; // SimPriority mode

        std::atomic<u64> m_frame{0};         // render frames started
        std::atomic<u64> m_published_sim{0}; // Semaphore: newest tick the compute thread signalled
        std::atomic<u32> m_read_index{0};    // DoubleBuffer: which buffer the render thread draws

        // SimPriority render<->compute coupling, all lock-free:
        std::atomic<bool> m_want_snapshot{false};              // render asks; compute clears
        std::atomic<u32> m_ready_slot{0};                      // ring slot render should draw
        std::atomic<u64> m_published_value{0};                 // m_publish_done value that fills m_ready_slot
        std::array<std::atomic<u64>, 3> m_slot_release_frame{}; // frame after which slot i is recyclable

#ifdef GAMEENGINE_CUDA
        std::optional<CudaInterop> m_cuda;
        std::array<CUdeviceptr, 3> m_device_ptr{0, 0, 0};
        int m_sim_ready_sem = -1;
        int m_render_done_sem = -1;
        int m_publish_done_sem = -1;
        u64 m_sim_tick = 0;    // Semaphore, compute-local
        u32 m_write_index = 0; // DoubleBuffer, compute-local
        u64 m_flip_frame = 0;  // DoubleBuffer, compute-local
        CUdeviceptr m_sim_state = 0;    // SimPriority: private authoritative buffer
        CUstream m_sim_stream = nullptr;
        u32 m_last_slot = 0;    // SimPriority, compute-local
        u64 m_publish_value = 0; // SimPriority, compute-local
#endif
};

} // namespace gpu
