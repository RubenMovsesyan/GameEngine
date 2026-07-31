#include <gpu/device.hpp>

#include <devtools/rlog.h>
#include <vk_mem_alloc.h>

#include <optional>
#include <set>
#include <vector>

namespace gpu {
namespace {

// First match, then stop. No silent fallback to family 0.
std::optional<u32> pickFamily(
    const std::vector<vk::QueueFamilyProperties>& props,
    vk::QueueFlags required,
    vk::QueueFlags forbidden
) {
    for (u32 i = 0; i < props.size(); i++) {
        const vk::QueueFlags flags = props[i].queueFlags;
        if ((flags & required) == required && !(flags & forbidden)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<QueueFamilies> selectFamilies(const vk::raii::PhysicalDevice& gpu, vk::SurfaceKHR surface) {
    using Q = vk::QueueFlagBits;
    const auto props = gpu.getQueueFamilyProperties();

    const auto graphics = pickFamily(props, Q::eGraphics, {});
    if (!graphics) {
        return std::nullopt;
    }

    QueueFamilies families{
        .graphics = *graphics,
        .compute = pickFamily(props, Q::eCompute, Q::eGraphics).value_or(*graphics),
        .transfer = pickFamily(props, Q::eTransfer, Q::eGraphics | Q::eCompute).value_or(*graphics),
    };

    if (surface) {
        // Prefer the graphics family so submission and present share a queue.
        if (gpu.getSurfaceSupportKHR(*graphics, surface)) {
            families.present = *graphics;
            families.has_present = true;
        } else {
            for (u32 i = 0; i < props.size(); i++) {
                if (gpu.getSurfaceSupportKHR(i, surface)) {
                    families.present = i;
                    families.has_present = true;
                    break;
                }
            }
        }
        if (!families.has_present) {
            return std::nullopt;
        }
    }
    return families;
}

bool supportsRequiredFeatures(const vk::raii::PhysicalDevice& gpu) {
    const auto chain = gpu.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& vk13 = chain.get<vk::PhysicalDeviceVulkan13Features>();
    return vk13.dynamicRendering && vk13.synchronization2;
}

void destroyAllocator(VmaAllocator_T* allocator) { vmaDestroyAllocator(allocator); }

} // namespace

Device::Device(const Instance& instance, const DeviceConfig& cfg) {
    std::optional<QueueFamilies> chosen;
    for (auto& gpu : instance->enumeratePhysicalDevices()) {
        if (!supportsRequiredFeatures(gpu)) {
            continue;
        }
        auto families = selectFamilies(gpu, cfg.present_surface);
        if (!families) {
            continue;
        }
        const bool discrete = gpu.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
        if (!chosen || discrete) {
            m_physical = gpu;
            chosen = families;
            if (discrete) {
                break;
            }
        }
    }
    if (!chosen) {
        RLOG(LL_FATAL, "No Vulkan device with dynamicRendering, synchronization2 and the required queues");
    }
    m_families = *chosen;
    RLOG(LL_INFO, "GPU: %s", m_physical.getProperties().deviceName.data());

    std::set<u32> unique{m_families.graphics, m_families.compute, m_families.transfer};
    if (m_families.has_present) {
        unique.insert(m_families.present);
    }
    const float priority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queue_infos;
    for (u32 family : unique) {
        queue_infos.push_back({.queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority});
    }

    std::vector<const char*> extensions;
    if (m_families.has_present) {
        extensions.push_back(vk::KHRSwapchainExtensionName);
    }

    const vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceVulkan13Features> chain{
        vk::DeviceCreateInfo{
            .queueCreateInfoCount = static_cast<u32>(queue_infos.size()),
            .pQueueCreateInfos = queue_infos.data(),
            .enabledExtensionCount = static_cast<u32>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        },
        vk::PhysicalDeviceVulkan13Features{.synchronization2 = vk::True, .dynamicRendering = vk::True},
    };
    m_device = vk::raii::Device{m_physical, chain.get<vk::DeviceCreateInfo>()};

    // Queues alias when families are equal; vk::raii::Queue destruction is a no-op.
    m_graphics_queue = m_device.getQueue(m_families.graphics, 0);
    m_compute_queue = m_device.getQueue(m_families.compute, 0);
    if (m_families.has_present) {
        m_present_queue = m_device.getQueue(m_families.present, 0);
    }

    VmaVulkanFunctions functions{
        .vkGetInstanceProcAddr = instance->getDispatcher()->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = m_device.getDispatcher()->vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo allocator_info{
        .physicalDevice = static_cast<VkPhysicalDevice>(*m_physical),
        .device = static_cast<VkDevice>(*m_device),
        .pVulkanFunctions = &functions,
        .instance = static_cast<VkInstance>(instance.handle()),
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };
    VmaAllocator allocator = nullptr;
    if (vmaCreateAllocator(&allocator_info, &allocator) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vmaCreateAllocator failed");
    }
    m_allocator = {allocator, &destroyAllocator};
}

} // namespace gpu
