#include <gpu/buffer_readback.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <utility>

namespace gpu {

namespace {

// 64-bit build: Vulkan dispatchable and (default) non-dispatchable handles are pointers.
VkDevice dev(void* p) { return reinterpret_cast<VkDevice>(p); }
VkQueue que(void* p) { return reinterpret_cast<VkQueue>(p); }
VkCommandPool pool(void* p) { return reinterpret_cast<VkCommandPool>(p); }
VkCommandBuffer cmd(void* p) { return reinterpret_cast<VkCommandBuffer>(p); }
VkFence fen(void* p) { return reinterpret_cast<VkFence>(p); }
VkBuffer buf(void* p) { return reinterpret_cast<VkBuffer>(p); }
VkDeviceMemory mem(void* p) { return reinterpret_cast<VkDeviceMemory>(p); }

// Host-visible + coherent memory, preferring cached (CPU reads of uncached memory are slow).
std::optional<u32> findReadbackMemoryType(VkPhysicalDevice physical_device, u32 type_bits) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::optional<u32> fallback;
    for (u32 i = 0; i < properties.memoryTypeCount; i++) {
        const VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((type_bits & (1u << i)) == 0 || (flags & required) != required) {
            continue;
        }
        if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
            return i;
        }
        if (!fallback) {
            fallback = i;
        }
    }
    return fallback;
}

} // namespace

BufferReadback::~BufferReadback() {
    destroy();
}

BufferReadback::BufferReadback(BufferReadback&& o) noexcept
    : m_bytes(o.m_bytes), m_slots(std::move(o.m_slots)), m_next(o.m_next), m_oldest(o.m_oldest), m_pending(o.m_pending),
      m_device(o.m_device), m_queue(o.m_queue), m_command_pool(o.m_command_pool) {
    o.m_slots.clear();
    o.m_device = nullptr;
    o.m_queue = nullptr;
    o.m_command_pool = nullptr;
    o.m_pending = 0;
}

BufferReadback& BufferReadback::operator=(BufferReadback&& o) noexcept {
    if (this != &o) {
        destroy();
        m_bytes = o.m_bytes;
        m_slots = std::move(o.m_slots);
        m_next = o.m_next;
        m_oldest = o.m_oldest;
        m_pending = o.m_pending;
        m_device = o.m_device;
        m_queue = o.m_queue;
        m_command_pool = o.m_command_pool;
        o.m_slots.clear();
        o.m_device = nullptr;
        o.m_queue = nullptr;
        o.m_command_pool = nullptr;
        o.m_pending = 0;
    }
    return *this;
}

void BufferReadback::destroy() {
    if (m_device == nullptr) {
        return;
    }
    VkDevice device = dev(m_device);
    // Copies may still be in flight; they reference the staging buffers and command buffers.
    for (Slot& slot : m_slots) {
        if (slot.pending) {
            vkWaitForFences(device, 1, reinterpret_cast<VkFence*>(&slot.fence), VK_TRUE, UINT64_MAX);
        }
        if (slot.fence != nullptr) {
            vkDestroyFence(device, fen(slot.fence), nullptr);
        }
        if (slot.memory != nullptr) {
            vkUnmapMemory(device, mem(slot.memory));
            vkFreeMemory(device, mem(slot.memory), nullptr);
        }
        if (slot.staging != nullptr) {
            vkDestroyBuffer(device, buf(slot.staging), nullptr);
        }
    }
    // Frees the command buffers with it.
    if (m_command_pool != nullptr) {
        vkDestroyCommandPool(device, pool(m_command_pool), nullptr);
    }
    m_slots.clear();
    m_device = nullptr;
    m_queue = nullptr;
    m_command_pool = nullptr;
    m_pending = 0;
}

std::optional<BufferReadback> BufferReadback::create(usize bytes, u32 slots, const char* label) {
    if (sg_query_backend() != SG_BACKEND_VULKAN) {
        RLOG(LL_ERROR, "BufferReadback::create: Vulkan backend only");
        return std::nullopt;
    }
    if (bytes == 0 || slots == 0) {
        RLOG(LL_ERROR, "BufferReadback::create: bytes and slots must be > 0");
        return std::nullopt;
    }

    const sg_vk_device_info info = sg_vk_query_device_info();
    VkDevice device = reinterpret_cast<VkDevice>(static_cast<uintptr_t>(info.device));
    VkPhysicalDevice physical_device = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(info.physical_device));

    BufferReadback out;
    out.m_bytes = bytes;
    out.m_device = reinterpret_cast<void*>(device);
    out.m_queue = reinterpret_cast<void*>(static_cast<uintptr_t>(info.queue));

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = info.queue_family_index;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        RLOG(LL_ERROR, "BufferReadback::create: vkCreateCommandPool failed");
        return std::nullopt;
    }
    out.m_command_pool = reinterpret_cast<void*>(command_pool);

    out.m_slots.resize(slots);
    for (Slot& slot : out.m_slots) {
        // The GPU-side buffer: an ordinary sokol storage buffer, written by any pass.
        sg_buffer_desc storage_desc = {};
        storage_desc.size = bytes;
        // A storage buffer the GPU writes must be immutable (sokol's rule for read/write
        // bindings); it needs no initial data.
        storage_desc.usage.storage_buffer = true;
        storage_desc.usage.immutable = true;
        storage_desc.label = label;
        slot.storage = makeBuffer(storage_desc);
        if (sg_query_buffer_state(*slot.storage) != SG_RESOURCESTATE_VALID) {
            RLOG(LL_ERROR, "BufferReadback::create: storage buffer creation failed");
            return std::nullopt;
        }

        // The CPU-side copy.
        VkBufferCreateInfo staging_info = {};
        staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        staging_info.size = bytes;
        staging_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer staging = VK_NULL_HANDLE;
        if (vkCreateBuffer(device, &staging_info, nullptr, &staging) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkCreateBuffer failed");
            return std::nullopt;
        }
        slot.staging = reinterpret_cast<void*>(staging);

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        const std::optional<u32> type = findReadbackMemoryType(physical_device, requirements.memoryTypeBits);
        if (!type) {
            RLOG(LL_ERROR, "BufferReadback::create: no host-visible coherent memory type");
            return std::nullopt;
        }
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = *type;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkAllocateMemory failed");
            return std::nullopt;
        }
        slot.memory = reinterpret_cast<void*>(memory);
        if (vkBindBufferMemory(device, staging, memory, 0) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkBindBufferMemory failed");
            return std::nullopt;
        }
        void* mapped = nullptr;
        if (vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkMapMemory failed");
            return std::nullopt;
        }
        slot.mapped = static_cast<const u8*>(mapped);

        VkCommandBufferAllocateInfo cmd_info = {};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_info.commandPool = command_pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &cmd_info, &command_buffer) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkAllocateCommandBuffers failed");
            return std::nullopt;
        }
        slot.command_buffer = reinterpret_cast<void*>(command_buffer);

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
            RLOG(LL_ERROR, "BufferReadback::create: vkCreateFence failed");
            return std::nullopt;
        }
        slot.fence = reinterpret_cast<void*>(fence);
    }
    return out;
}

void BufferReadback::submit() {
    Slot& slot = m_slots[m_next];
    VkCommandBuffer command_buffer = cmd(slot.command_buffer);
    const sg_vk_buffer_info source = sg_vk_query_buffer_info(*slot.storage);

    vkResetCommandBuffer(command_buffer, 0);
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin);

    // Make the frame's shader writes to the storage buffer visible to the copy. A pipeline
    // barrier's first scope covers everything earlier in submission order on the queue, so it
    // reaches into sokol's already-submitted frame.
    VkMemoryBarrier before = {};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);

    VkBufferCopy region = {};
    region.size = m_bytes;
    vkCmdCopyBuffer(command_buffer, reinterpret_cast<VkBuffer>(static_cast<uintptr_t>(source.buffer)), buf(slot.staging), 1, &region);

    // And the copy visible to the CPU once the fence signals.
    VkMemoryBarrier after = {};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vkResetFences(dev(m_device), 1, reinterpret_cast<VkFence*>(&slot.fence));
    if (vkQueueSubmit(que(m_queue), 1, &submit_info, fen(slot.fence)) != VK_SUCCESS) {
        RLOG(LL_ERROR, "BufferReadback::submit: vkQueueSubmit failed");
        return;
    }

    slot.pending = true;
    if (m_pending == 0) {
        m_oldest = m_next;
    }
    m_pending++;
    m_next = (m_next + 1) % static_cast<u32>(m_slots.size());
}

std::optional<std::span<const u8>> BufferReadback::waitOldest() {
    if (m_pending == 0) {
        return std::nullopt;
    }
    Slot& slot = m_slots[m_oldest];
    vkWaitForFences(dev(m_device), 1, reinterpret_cast<VkFence*>(&slot.fence), VK_TRUE, UINT64_MAX);
    return std::span<const u8>(slot.mapped, m_bytes);
}

void BufferReadback::releaseOldest() {
    m_slots[m_oldest].pending = false;
    m_pending--;
    m_oldest = (m_oldest + 1) % static_cast<u32>(m_slots.size());
}

} // namespace gpu
