#pragma once

#include <gpu/instance.hpp>

#include <memory>
#include <span>

// Forward-declared so vk_mem_alloc.h stays out of every TU.
struct VmaAllocator_T;

namespace gpu {

struct QueueFamilies {
        u32 graphics = 0;
        u32 compute = 0;  // dedicated family if one exists, else == graphics
        u32 transfer = 0; // dedicated family if one exists, else == graphics
        u32 present = 0;
        bool has_present = false;
};

struct DeviceConfig {
        // Null => headless: no present queue, no swapchain extension. The compute path.
        vk::SurfaceKHR present_surface{};
};

class Device {
    public:
        explicit Device(const Instance&, const DeviceConfig& = {});

        Device(Device&&) = default;
        Device& operator=(Device&&) = default;
        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        const vk::raii::Device& operator*() const { return m_device; }
        const vk::raii::Device* operator->() const { return &m_device; }
        const vk::raii::PhysicalDevice& physical() const { return m_physical; }
        const QueueFamilies& families() const { return m_families; }

        const vk::raii::Queue& graphicsQueue() const { return m_graphics_queue; }
        const vk::raii::Queue& computeQueue() const { return m_compute_queue; }
        const vk::raii::Queue& presentQueue() const { return m_present_queue; }

        VmaAllocator_T* allocator() const { return m_allocator.get(); }
        void waitIdle() const { m_device.waitIdle(); }

    private:
        vk::raii::PhysicalDevice m_physical{nullptr};
        QueueFamilies m_families{};
        vk::raii::Device m_device{nullptr};
        vk::raii::Queue m_graphics_queue{nullptr};
        vk::raii::Queue m_compute_queue{nullptr};
        vk::raii::Queue m_present_queue{nullptr};
        // Declared last so it is destroyed first: vmaDestroyAllocator must run before
        // vkDestroyDevice. Do not reorder.
        std::unique_ptr<VmaAllocator_T, void (*)(VmaAllocator_T*)> m_allocator{nullptr, nullptr};
};

} // namespace gpu
