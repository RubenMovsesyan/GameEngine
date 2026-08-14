#pragma once

#include <common.h>

#include <vulkan/vulkan.h>

#include <span>

namespace gpu {

struct InstanceConfig {
    const char* app_name = "GameEngine";
    std::span<const char* const> required_extensions{};
    bool validation = true;
};

class Instance {
    public:
        explicit Instance(const InstanceConfig& cfg = {});
        ~Instance();

        Instance(Instance&&) = delete;
        Instance& operator=(Instance&&) = delete;
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        VkInstance handle() const { return m_instance; }

    private:
        VkInstance m_instance = VK_NULL_HANDLE;
};

} // namespace gpu
