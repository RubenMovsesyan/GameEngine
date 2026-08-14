#include <gpu/instance.hpp>

#include <devtools/rlog.h>

#include <cstring>
#include <vector>

namespace gpu {
namespace {

constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

bool hasLayer(const std::vector<vk::LayerProperties>& available, const char* name) {
    for (const auto& l : available) {
        if (std::strcmp(l.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT,
    const vk::DebugUtilsMessengerCallbackDataEXT* data,
    void*
) {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        RLOG(LL_ERROR, "[vk] %s", data->pMessage);
    } else {
        RLOG(LL_WARN, "[vk] %s", data->pMessage);
    }
    return vk::False; // never abort the offending call
}

} // namespace

Instance::Instance(const InstanceConfig& cfg) {
    const vk::ApplicationInfo app{
        .pApplicationName = cfg.app_name,
        .apiVersion = cfg.api_version,
    };

    std::vector<const char*> extensions(cfg.required_extensions.begin(), cfg.required_extensions.end());
    std::vector<const char*> layers;

    const bool validation = cfg.validation && hasLayer(m_context.enumerateInstanceLayerProperties(), VALIDATION_LAYER);
    if (cfg.validation && !validation) {
        RLOG(LL_WARN, "%s not available, continuing without validation", VALIDATION_LAYER);
    }
    if (validation) {
        layers.push_back(VALIDATION_LAYER);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        RLOG(LL_INFO, "Enabled layer %s", VALIDATION_LAYER);
    }

    m_instance = vk::raii::Instance{
        m_context,
        vk::InstanceCreateInfo{
            .pApplicationInfo = &app,
            .enabledLayerCount = static_cast<u32>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<u32>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        },
    };

    if (validation) {
        using Sev = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;
        m_messenger.emplace(
            m_instance,
            vk::DebugUtilsMessengerCreateInfoEXT{
                .messageSeverity = Sev::eWarning | Sev::eError,
                .messageType = Type::eGeneral | Type::eValidation | Type::ePerformance,
                .pfnUserCallback = debugCallback,
            }
        );
    }
}

} // namespace gpu
