#pragma once

#include <gpu/instance.hpp>

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include <vulkan/vulkan.h>

namespace gpu {

struct DeviceConfig {
    VkSurfaceKHR present_surface = VK_NULL_HANDLE; // null => headless, no swapchain support
    bool validation = true;
};

class Device {
    public:
        explicit Device(const Instance&, const DeviceConfig& = {});
        ~Device();

        Device(Device&&) = delete;
        Device& operator=(Device&&) = delete;
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        nvrhi::IDevice* nvrhi() const { return m_nvrhi_device; }
        // VK-specific interface, unwrapped by the validation layer.
        nvrhi::vulkan::IDevice* vulkanDevice() const { return m_raw_device; }
        VkPhysicalDevice physical() const { return m_physical; }
        VkDevice handle() const { return m_device; }
        VkQueue graphicsQueue() const { return m_graphics_queue; }
        u32 graphicsQueueFamily() const { return m_graphics_family; }

        // If data is given, uploads it synchronously before returning.
        nvrhi::BufferHandle createBuffer(const nvrhi::BufferDesc& desc, const void* data = nullptr, usize data_size = 0) const;
        // If data is given, uploads it synchronously before returning.
        nvrhi::TextureHandle createTexture(const nvrhi::TextureDesc& desc, const void* data = nullptr, usize row_pitch = 0) const;

    private:
        VkPhysicalDevice m_physical = VK_NULL_HANDLE;
        u32 m_graphics_family = 0;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphics_queue = VK_NULL_HANDLE;
        nvrhi::vulkan::DeviceHandle m_raw_device;
        nvrhi::DeviceHandle m_nvrhi_device;
};

} // namespace gpu
