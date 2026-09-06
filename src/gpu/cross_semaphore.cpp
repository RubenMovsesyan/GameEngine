#include <gpu/cross_semaphore.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

#include <unistd.h> // close()

#include <utility>

namespace gpu {

namespace {

PFN_vkGetSemaphoreFdKHR loadGetSemaphoreFd(VkDevice device) {
    static PFN_vkGetSemaphoreFdKHR fn = nullptr;
    static VkDevice cached = VK_NULL_HANDLE;
    if (fn == nullptr || cached != device) {
        fn = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
        cached = device;
    }
    return fn;
}

// 64-bit build: Vulkan dispatchable and (default) non-dispatchable handles are pointers.
VkSemaphore sem(void* p) { return reinterpret_cast<VkSemaphore>(p); }
VkDevice dev(void* p) { return reinterpret_cast<VkDevice>(p); }
VkQueue que(void* p) { return reinterpret_cast<VkQueue>(p); }

} // namespace

CrossSemaphore::~CrossSemaphore() {
    if (m_semaphore != nullptr && m_device != nullptr) {
        vkDestroySemaphore(dev(m_device), sem(m_semaphore), nullptr);
    }
    if (m_fd >= 0) {
        close(m_fd);
    }
}

CrossSemaphore::CrossSemaphore(CrossSemaphore&& o) noexcept
    : m_semaphore(o.m_semaphore), m_device(o.m_device), m_queue(o.m_queue), m_fd(o.m_fd) {
    o.m_semaphore = nullptr;
    o.m_device = nullptr;
    o.m_queue = nullptr;
    o.m_fd = -1;
}

CrossSemaphore& CrossSemaphore::operator=(CrossSemaphore&& o) noexcept {
    if (this != &o) {
        if (m_semaphore != nullptr && m_device != nullptr) {
            vkDestroySemaphore(dev(m_device), sem(m_semaphore), nullptr);
        }
        if (m_fd >= 0) {
            close(m_fd);
        }
        m_semaphore = o.m_semaphore;
        m_device = o.m_device;
        m_queue = o.m_queue;
        m_fd = o.m_fd;
        o.m_semaphore = nullptr;
        o.m_device = nullptr;
        o.m_queue = nullptr;
        o.m_fd = -1;
    }
    return *this;
}

std::optional<CrossSemaphore> CrossSemaphore::create(const char* label) {
    if (sg_query_backend() != SG_BACKEND_VULKAN) {
        RLOG(LL_ERROR, "CrossSemaphore::create: Vulkan backend only");
        return std::nullopt;
    }

    const sg_vk_device_info info = sg_vk_query_device_info();
    VkDevice device = reinterpret_cast<VkDevice>(static_cast<uintptr_t>(info.device));
    VkQueue queue = reinterpret_cast<VkQueue>(static_cast<uintptr_t>(info.queue));

    VkSemaphoreTypeCreateInfo type_info = {};
    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type_info.initialValue = 0;

    VkExportSemaphoreCreateInfo export_info = {};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    export_info.pNext = &type_info;

    VkSemaphoreCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create_info.pNext = &export_info;

    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &create_info, nullptr, &semaphore) != VK_SUCCESS) {
        RLOG(LL_ERROR, "CrossSemaphore::create: vkCreateSemaphore failed");
        return std::nullopt;
    }

    PFN_vkGetSemaphoreFdKHR getFd = loadGetSemaphoreFd(device);
    if (getFd == nullptr) {
        RLOG(LL_ERROR, "CrossSemaphore::create: vkGetSemaphoreFdKHR unavailable");
        vkDestroySemaphore(device, semaphore, nullptr);
        return std::nullopt;
    }

    VkSemaphoreGetFdInfoKHR fd_info = {};
    fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    fd_info.semaphore = semaphore;
    fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    if (getFd(device, &fd_info, &fd) != VK_SUCCESS || fd < 0) {
        RLOG(LL_ERROR, "CrossSemaphore::create: vkGetSemaphoreFdKHR failed");
        vkDestroySemaphore(device, semaphore, nullptr);
        return std::nullopt;
    }

    CrossSemaphore out;
    out.m_semaphore = reinterpret_cast<void*>(semaphore);
    out.m_device = reinterpret_cast<void*>(device);
    out.m_queue = reinterpret_cast<void*>(queue);
    out.m_fd = fd;
    if (label != nullptr) {
        // best-effort debug label; ignore if the ext fn isn't present
        auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
        if (setName != nullptr) {
            VkDebugUtilsObjectNameInfoEXT name = {};
            name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name.objectType = VK_OBJECT_TYPE_SEMAPHORE;
            name.objectHandle = reinterpret_cast<uint64_t>(semaphore);
            name.pObjectName = label;
            setName(device, &name);
        }
    }
    return out;
}

void CrossSemaphore::queueWait(u64 value, PipelineStage stage) const {
    VkSemaphore s = sem(m_semaphore);
    const VkPipelineStageFlags wait_stage = stage;

    VkTimelineSemaphoreSubmitInfo tl = {};
    tl.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tl.waitSemaphoreValueCount = 1;
    tl.pWaitSemaphoreValues = &value;

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = &tl;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &s;
    submit.pWaitDstStageMask = &wait_stage;
    vkQueueSubmit(que(m_queue), 1, &submit, VK_NULL_HANDLE);
}

void CrossSemaphore::queueSignal(u64 value) const {
    VkSemaphore s = sem(m_semaphore);

    VkTimelineSemaphoreSubmitInfo tl = {};
    tl.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tl.signalSemaphoreValueCount = 1;
    tl.pSignalSemaphoreValues = &value;

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = &tl;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &s;
    vkQueueSubmit(que(m_queue), 1, &submit, VK_NULL_HANDLE);
}

u64 CrossSemaphore::hostValue() const {
    uint64_t v = 0;
    vkGetSemaphoreCounterValue(dev(m_device), sem(m_semaphore), &v);
    return v;
}

bool CrossSemaphore::hostWait(u64 value, u64 timeout_ns) const {
    VkSemaphore s = sem(m_semaphore);
    const uint64_t wait_value = value;
    VkSemaphoreWaitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &s;
    info.pValues = &wait_value;
    return vkWaitSemaphores(dev(m_device), &info, timeout_ns) == VK_SUCCESS;
}

} // namespace gpu
