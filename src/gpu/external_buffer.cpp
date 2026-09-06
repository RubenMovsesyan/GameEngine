#include <gpu/external_buffer.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

#include <unistd.h> // close()

#include <utility>

namespace gpu {

namespace {

// vkGetMemoryFdKHR is an extension entrypoint -- resolve it once per device.
PFN_vkGetMemoryFdKHR loadGetMemoryFd(VkDevice device) {
    static PFN_vkGetMemoryFdKHR fn = nullptr;
    static VkDevice cached = VK_NULL_HANDLE;
    if (fn == nullptr || cached != device) {
        fn = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
        cached = device;
    }
    return fn;
}

} // namespace

ExternalBuffer::~ExternalBuffer() {
    if (m_fd >= 0) {
        close(m_fd);
    }
}

ExternalBuffer::ExternalBuffer(ExternalBuffer&& other) noexcept
    : m_buffer(std::move(other.m_buffer)), m_size(other.m_size), m_alloc_size(other.m_alloc_size), m_fd(other.m_fd) {
    other.m_fd = -1;
    other.m_size = 0;
    other.m_alloc_size = 0;
}

ExternalBuffer& ExternalBuffer::operator=(ExternalBuffer&& other) noexcept {
    if (this != &other) {
        if (m_fd >= 0) {
            close(m_fd);
        }
        m_buffer = std::move(other.m_buffer);
        m_size = other.m_size;
        m_alloc_size = other.m_alloc_size;
        m_fd = other.m_fd;
        other.m_fd = -1;
        other.m_size = 0;
        other.m_alloc_size = 0;
    }
    return *this;
}

std::optional<ExternalBuffer> ExternalBuffer::create(usize bytes, const char* label) {
    if (sg_query_backend() != SG_BACKEND_VULKAN) {
        RLOG(LL_ERROR, "ExternalBuffer::create: only the Vulkan backend is supported");
        return std::nullopt;
    }

    sg_buffer_desc desc = {};
    desc.size = bytes;
    desc.usage.vertex_buffer = true;  // drawn as per-instance data
    desc.usage.storage_buffer = true; // written by a CUDA / compute kernel
    desc.usage.immutable = true;      // sokol never touches the contents; the external API does
    desc.vk_external_memory = true;
    desc.label = label;

    ExternalBuffer out;
    out.m_buffer = makeBuffer(desc);
    out.m_size = bytes;

    if (sg_query_buffer_state(*out.m_buffer) != SG_RESOURCESTATE_VALID) {
        RLOG(LL_ERROR, "ExternalBuffer::create: sg_make_buffer failed");
        return std::nullopt;
    }

    const std::optional<NativeHandle> native = nativeHandle(*out.m_buffer);
    if (!native || native->vk_memory == 0) {
        RLOG(LL_ERROR, "ExternalBuffer::create: no Vulkan memory handle (fork not applied?)");
        return std::nullopt;
    }
    out.m_alloc_size = native->vk_memory_size;

    const sg_vk_device_info dev = sg_vk_query_device_info();
    VkDevice device = reinterpret_cast<VkDevice>(static_cast<uintptr_t>(dev.device));

    PFN_vkGetMemoryFdKHR getMemoryFd = loadGetMemoryFd(device);
    if (getMemoryFd == nullptr) {
        RLOG(LL_ERROR, "ExternalBuffer::create: vkGetMemoryFdKHR unavailable");
        return std::nullopt;
    }

    VkMemoryGetFdInfoKHR fd_info = {};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = reinterpret_cast<VkDeviceMemory>(static_cast<uintptr_t>(native->vk_memory));
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    if (getMemoryFd(device, &fd_info, &fd) != VK_SUCCESS || fd < 0) {
        RLOG(LL_ERROR, "ExternalBuffer::create: vkGetMemoryFdKHR failed");
        return std::nullopt;
    }
    out.m_fd = fd;

    return out;
}

} // namespace gpu
