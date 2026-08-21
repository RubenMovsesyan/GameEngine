#include <gpu/device.hpp>

#include <rlog.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <vector>

namespace gpu {

namespace {

// Required: NVRHI's validation layer crashes without an errorCB set.
class RlogMessageCallback : public nvrhi::IMessageCallback {
    public:
        void message(nvrhi::MessageSeverity severity, const char* messageText) override {
            switch (severity) {
                case nvrhi::MessageSeverity::Info:
                    RLOG(LL_INFO, "%s", messageText);
                    break;
                case nvrhi::MessageSeverity::Warning:
                    RLOG(LL_WARN, "%s", messageText);
                    break;
                case nvrhi::MessageSeverity::Error:
                    RLOG(LL_ERROR, "%s", messageText);
                    break;
                case nvrhi::MessageSeverity::Fatal:
                    RLOG(LL_FATAL, "%s", messageText);
                    break;
            }
        }
};

RlogMessageCallback& messageCallback() {
    static RlogMessageCallback callback;
    return callback;
}

struct Candidate {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    u32 graphics_family = 0;
    i32 score = -1;
};

bool supportsSwapchain(VkPhysicalDevice physical) {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());

    for (const auto& ext : extensions) {
        if (std::string_view{ext.extensionName} == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            return true;
        }
    }
    return false;
}

Candidate scoreDevice(VkPhysicalDevice physical, VkSurfaceKHR present_surface) {
    Candidate candidate{physical};

    if (present_surface != VK_NULL_HANDLE && !supportsSwapchain(physical)) {
        return candidate;
    }

    u32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());

    for (u32 i = 0; i < family_count; ++i) {
        if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }
        if (present_surface != VK_NULL_HANDLE) {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, present_surface, &present_supported);
            if (!present_supported) {
                continue;
            }
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);

        candidate.graphics_family = i;
        candidate.score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 0;
        return candidate;
    }

    return candidate;
}

VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR present_surface, u32& graphics_family) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        RLOG(LL_FATAL, "no Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    Candidate best;
    for (VkPhysicalDevice physical : devices) {
        Candidate candidate = scoreDevice(physical, present_surface);
        if (candidate.score > best.score) {
            best = candidate;
        }
    }

    if (best.score < 0) {
        RLOG(LL_FATAL, "no Vulkan device with the required graphics/present support found");
    }

    graphics_family = best.graphics_family;
    return best.physical;
}

VkDevice createLogicalDevice(VkPhysicalDevice physical, u32 graphics_family, bool with_swapchain) {
    const f32 priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    // NVRHI requires timeline semaphores, dynamic rendering, and synchronization2.
    VkPhysicalDeviceVulkan13Features features_1_3{};
    features_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features_1_3.dynamicRendering = VK_TRUE;
    features_1_3.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features_1_2{};
    features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features_1_2.timelineSemaphore = VK_TRUE;
    features_1_2.pNext = &features_1_3;

    const char* swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features_1_2;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    if (with_swapchain) {
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = &swapchain_ext;
    }

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &device_info, nullptr, &device) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vkCreateDevice failed");
    }
    return device;
}

} // namespace

Device::Device(const Instance& instance, const DeviceConfig& cfg) {
    m_physical = pickPhysicalDevice(instance.handle(), cfg.present_surface, m_graphics_family);
    m_device = createLogicalDevice(m_physical, m_graphics_family, cfg.present_surface != VK_NULL_HANDLE);
    vkGetDeviceQueue(m_device, m_graphics_family, 0, &m_graphics_queue);

    vk::detail::DynamicLoader dl;
    auto get_instance_proc_addr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    auto get_device_proc_addr = dl.getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.handle(), get_instance_proc_addr, m_device, get_device_proc_addr);

    nvrhi::vulkan::DeviceDesc desc{};
    desc.errorCB = &messageCallback();
    desc.instance = instance.handle();
    desc.physicalDevice = m_physical;
    desc.device = m_device;
    desc.graphicsQueue = m_graphics_queue;
    desc.graphicsQueueIndex = static_cast<int>(m_graphics_family);

    m_raw_device = nvrhi::vulkan::createDevice(desc);
    m_nvrhi_device = cfg.validation ? nvrhi::validation::createValidationLayer(m_raw_device) : m_raw_device.Get();
}

Device::~Device() {
    m_nvrhi_device = nullptr;
    m_raw_device = nullptr;
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
}

nvrhi::BufferHandle Device::createBuffer(const nvrhi::BufferDesc& desc, const void* data, usize data_size) const {
    std::lock_guard<std::recursive_mutex> lock(m_submit_mutex);

    nvrhi::BufferHandle buffer = m_nvrhi_device->createBuffer(desc);

    if (data != nullptr) {
        nvrhi::CommandListHandle cmd = m_nvrhi_device->createCommandList();
        cmd->open();
        cmd->writeBuffer(buffer, data, data_size);
        cmd->close();
        m_nvrhi_device->executeCommandList(cmd);
        vkDeviceWaitIdle(m_device);
    }

    return buffer;
}

nvrhi::TextureHandle Device::createTexture(const nvrhi::TextureDesc& desc, const void* data, usize row_pitch) const {
    std::lock_guard<std::recursive_mutex> lock(m_submit_mutex);

    nvrhi::TextureHandle texture = m_nvrhi_device->createTexture(desc);

    if (data != nullptr) {
        nvrhi::CommandListHandle cmd = m_nvrhi_device->createCommandList();
        cmd->open();
        cmd->writeTexture(texture, 0, 0, data, row_pitch);
        cmd->close();
        m_nvrhi_device->executeCommandList(cmd);
        vkDeviceWaitIdle(m_device);
    }

    return texture;
}

} // namespace gpu
