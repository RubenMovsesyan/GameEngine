#include <gpu/buffer.hpp>

#include <devtools/rlog.h>
#include <vk_mem_alloc.h>

#include <utility>

namespace gpu {

Buffer::Buffer(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, bool host_visible)
    : m_allocator(device.allocator()), m_size(size) {
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = static_cast<VkBufferUsageFlags>(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info{.usage = VMA_MEMORY_USAGE_AUTO};
    if (host_visible) {
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocationInfo result{};
    if (vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &buffer, &m_allocation, &result) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vmaCreateBuffer failed for %lu bytes", size);
    }
    m_buffer = buffer;
    m_mapped = result.pMappedData;
}

Buffer::~Buffer() {
    if (m_allocation) {
        vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(m_buffer), m_allocation);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, nullptr)),
      m_allocation(std::exchange(other.m_allocation, nullptr)),
      m_buffer(std::exchange(other.m_buffer, nullptr)),
      m_size(std::exchange(other.m_size, 0)),
      m_mapped(std::exchange(other.m_mapped, nullptr)) {}

// Swap, so other's destructor releases whatever we were holding.
Buffer& Buffer::operator=(Buffer&& other) noexcept {
    std::swap(m_allocator, other.m_allocator);
    std::swap(m_allocation, other.m_allocation);
    std::swap(m_buffer, other.m_buffer);
    std::swap(m_size, other.m_size);
    std::swap(m_mapped, other.m_mapped);
    return *this;
}

} // namespace gpu
