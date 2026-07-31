#pragma once

#include <gpu/vk.hpp>

#include <optional>
#include <span>

namespace gpu {

struct InstanceConfig {
        const char* app_name = "GameEngine";
        u32 api_version = vk::ApiVersion14;
        // Handed through from the windowing layer. The entire window seam on this side.
        std::span<const char* const> required_extensions{};
        bool validation = true;
};

class Instance {
    public:
        explicit Instance(const InstanceConfig& cfg = {});

        Instance(Instance&&) = default;
        Instance& operator=(Instance&&) = default;
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;

        const vk::raii::Instance& operator*() const { return m_instance; }
        const vk::raii::Instance* operator->() const { return &m_instance; }
        vk::Instance handle() const { return *m_instance; }

        // Adopts a surface created by the windowing layer.
        vk::raii::SurfaceKHR adoptSurface(VkSurfaceKHR raw) const { return {m_instance, raw}; }

    private:
        vk::raii::Context m_context{};
        vk::raii::Instance m_instance{nullptr};
        std::optional<vk::raii::DebugUtilsMessengerEXT> m_messenger{};
};

} // namespace gpu
