#pragma once

#ifdef GAMEENGINE_CUDA

#include <common.h>
#include <gpu/cross_semaphore.hpp>
#include <gpu/external_buffer.hpp>

#include <cuda.h>

#include <optional>
#include <vector>

namespace gpu {

// A CUDA Driver-API context bound to the same physical GPU sokol's Vulkan device runs on,
// plus imports of exportable Vulkan buffers (zero-copy device pointers) and timeline
// semaphores. Construct and drive it entirely from the compute thread.
class CudaInterop {
    public:
        CudaInterop() = default;
        ~CudaInterop();

        CudaInterop(CudaInterop&&) noexcept;
        CudaInterop& operator=(CudaInterop&&) noexcept;
        CudaInterop(const CudaInterop&) = delete;
        CudaInterop& operator=(const CudaInterop&) = delete;

        // Picks the CUdevice whose UUID matches sokol's VkPhysicalDevice, retains its
        // primary context, makes it current on the calling thread, and creates a stream.
        static std::optional<CudaInterop> create();

        // Maps `buffer`'s device memory into this context. The returned pointer aliases the
        // exact bytes the renderer draws -- writes are visible to Vulkan once the sync
        // handshake completes. dup()s the buffer's FD internally.
        CUdeviceptr mapBuffer(const ExternalBuffer& buffer);

        // Imports `semaphore`; returns an index used with waitAsync/signalAsync.
        int importSemaphore(const CrossSemaphore& semaphore);

        void waitAsync(int semaphore, u64 value);   // cuWaitExternalSemaphoresAsync on the default stream
        void signalAsync(int semaphore, u64 value); // cuSignalExternalSemaphoresAsync on the default stream
        void waitAsync(int semaphore, u64 value, CUstream stream);
        void signalAsync(int semaphore, u64 value, CUstream stream);
        void sync();                                // cuStreamSynchronize on the default stream
        void sync(CUstream stream);

        // A new stream owned by this CudaInterop (destroyed with it). high_priority asks for
        // the highest stream priority the device reports -- best-effort.
        CUstream makeStream(bool high_priority);

        CUstream stream() const { return m_stream; }
        CUcontext context() const { return m_context; }

    private:
        CUdevice m_device = 0;
        CUcontext m_context = nullptr;
        CUstream m_stream = nullptr;
        std::vector<CUstream> m_extra_streams;
        std::vector<CUexternalMemory> m_external_memory;
        std::vector<CUexternalSemaphore> m_external_semaphores;
};

} // namespace gpu

#endif // GAMEENGINE_CUDA
