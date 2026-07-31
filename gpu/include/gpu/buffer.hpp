#pragma once

#include <gpu/device.hpp>

struct VmaAllocation_T;

namespace gpu {

// Thin VMA wrapper. The one hand-written destructor in the module, because VMA is C.
class Buffer {
    public:
        Buffer() = default;
        Buffer(const Device&, vk::DeviceSize size, vk::BufferUsageFlags usage, bool host_visible = false);
        ~Buffer();

        Buffer(Buffer&&) noexcept;
        Buffer& operator=(Buffer&&) noexcept;
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        vk::Buffer handle() const { return m_buffer; }
        vk::DeviceSize size() const { return m_size; }
        void* mapped() const { return m_mapped; } // non-null iff host_visible

    private:
        VmaAllocator_T* m_allocator = nullptr;
        VmaAllocation_T* m_allocation = nullptr;
        vk::Buffer m_buffer{};
        vk::DeviceSize m_size = 0;
        void* m_mapped = nullptr;
};

} // namespace gpu
