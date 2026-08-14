#include <devtools/common.h>
#define RLOG_IMPLEMENTATION
#include <devtools/rlog.h>

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

// NVRHI's Vulkan backend always builds against the dynamic dispatch loader (see
// vulkan-backend.h), but only initializes it itself for NVRHI_SHARED_LIBRARY_BUILD.
// Statically linked, that's on us: invoke the storage macro below (exactly one TU) and
// load it before calling nvrhi::vulkan::createDevice, or every vk:: call inside NVRHI
// resolves through a null function pointer.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <cstring>
#include <vector>

namespace {

constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

bool validationLayerAvailable() {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, VALIDATION_LAYER) == 0) {
            return true;
        }
    }
    return false;
}

VkInstance createInstance(bool with_validation) {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "GameEngine NVRHI test";
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    if (with_validation) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vkCreateInstance failed");
    }
    return instance;
}

VkPhysicalDevice pickPhysicalDevice(VkInstance instance) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        RLOG(LL_FATAL, "no Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    return devices[0];
}

u32 pickGraphicsQueueFamily(VkPhysicalDevice physical_device) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());

    for (u32 i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    RLOG(LL_FATAL, "no graphics queue family found");
    return 0;
}

VkDevice createDevice(VkPhysicalDevice physical_device, u32 graphics_family) {
    const f32 priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    // NVRHI tracks queue completion with a timeline semaphore per queue.
    VkPhysicalDeviceVulkan12Features features_1_2{};
    features_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features_1_2.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features_1_2;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vkCreateDevice failed");
    }
    return device;
}

} // namespace

int main() {
    initLog(1024 * 1024);

    if (!nvrhi::verifyHeaderVersion()) {
        RLOG(LL_FATAL, "NVRHI header/library version mismatch");
    }

    bool with_validation = validationLayerAvailable();
    if (!with_validation) {
        RLOG(LL_WARN, "%s not found, running without it", VALIDATION_LAYER);
    }

    VkInstance instance = createInstance(with_validation);
    VkPhysicalDevice physical_device = pickPhysicalDevice(instance);
    u32 graphics_family = pickGraphicsQueueFamily(physical_device);
    VkDevice device = createDevice(physical_device, graphics_family);

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    RLOG(LL_INFO, "GPU: %s", properties.deviceName);

    vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    PFN_vkGetDeviceProcAddr   get_device_proc_addr = dl.getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, get_instance_proc_addr, device, get_device_proc_addr);

    {
        nvrhi::vulkan::DeviceDesc desc{};
        desc.instance = instance;
        desc.physicalDevice = physical_device;
        desc.device = device;
        desc.graphicsQueue = graphics_queue;
        desc.graphicsQueueIndex = static_cast<int>(graphics_family);

        nvrhi::vulkan::DeviceHandle raw_device = nvrhi::vulkan::createDevice(desc);
        nvrhi::DeviceHandle nvrhi_device = nvrhi::validation::createValidationLayer(raw_device);

        RLOG(LL_INFO, "NVRHI device created, graphics API = %d", static_cast<int>(nvrhi_device->getGraphicsAPI()));
        RLOG(LL_INFO, "NVRHI + Vulkan build OK");
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    return 0;
}
