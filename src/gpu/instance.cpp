#include <gpu/instance.hpp>

#include <rlog.h>

#include <string_view>
#include <vector>

namespace gpu {

namespace {
constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

bool validationLayerAvailable() {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const auto& layer : layers) {
        if (std::string_view{layer.layerName} == VALIDATION_LAYER) {
            return true;
        }
    }
    return false;
}
} // namespace

Instance::Instance(const InstanceConfig& cfg) {
    bool with_validation = cfg.validation && validationLayerAvailable();
    if (cfg.validation && !with_validation) {
        RLOG(LL_WARN, "%s not found, running without it", VALIDATION_LAYER);
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = cfg.app_name;
    app_info.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<u32>(cfg.required_extensions.size());
    create_info.ppEnabledExtensionNames = cfg.required_extensions.data();
    if (with_validation) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
    }

    if (vkCreateInstance(&create_info, nullptr, &m_instance) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vkCreateInstance failed");
    }
}

Instance::~Instance() {
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

} // namespace gpu
