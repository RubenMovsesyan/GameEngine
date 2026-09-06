#pragma once

#include <common.h>
#include <gpu/handle.hpp>

#include <optional>

namespace gpu {

// An sg_buffer whose Vulkan device memory is a dedicated, exportable allocation, so another
// API on the same GPU (CUDA here) can map the exact same bytes with no copy. Owns the
// sg_buffer; owns an OS handle (opaque FD on Linux) duplicated from the allocation.
//
// Vulkan-only: create() returns std::nullopt on any other backend. Relies on the
// gameengine/vk-interop sokol fork (sg_buffer_desc.vk_external_memory + sg_vk_query_*).
class ExternalBuffer {
    public:
        ExternalBuffer() = default;
        ~ExternalBuffer();

        ExternalBuffer(ExternalBuffer&&) noexcept;
        ExternalBuffer& operator=(ExternalBuffer&&) noexcept;
        ExternalBuffer(const ExternalBuffer&) = delete;
        ExternalBuffer& operator=(const ExternalBuffer&) = delete;

        // `bytes` is the requested size; the backing allocation may be larger (see
        // allocationSize()). The buffer is created with vertex + storage usage so it can be
        // drawn as instances and written by a compute/CUDA kernel.
        static std::optional<ExternalBuffer> create(usize bytes, const char* label);

        sg_buffer buffer() const { return m_buffer ? *m_buffer : sg_buffer{}; }
        BufferHandle handle() const { return m_buffer; }
        usize sizeBytes() const { return m_size; }

        // Size of the VkDeviceMemory allocation -- what cuImportExternalMemory must be told.
        usize allocationSize() const { return m_alloc_size; }
        // Duplicated opaque FD for the allocation. Caller does NOT own it (closed in the
        // destructor); dup() it before handing to an API that takes ownership.
        int memoryFd() const { return m_fd; }

    private:
        BufferHandle m_buffer;
        usize m_size = 0;
        usize m_alloc_size = 0;
        int m_fd = -1;
};

} // namespace gpu
