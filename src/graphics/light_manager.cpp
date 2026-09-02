#include <graphics/light_manager.hpp>

namespace graphics {

namespace {

// Every member is a vec4 -- std430-safe with no padding math needed.
struct GpuLight {
    Eigen::Vector4f color_intensity;  // xyz = color, w = intensity
    Eigen::Vector4f position_range;   // xyz = world position, w = range
    Eigen::Vector4f direction_type;   // xyz = world direction, w = LightType
    Eigen::Vector4f cone_angles;      // x = inner, y = outer, zw unused
};

GpuLight toGpuLight(const Light& light) {
    return GpuLight{
        .color_intensity = Eigen::Vector4f(light.color.x(), light.color.y(), light.color.z(), light.intensity),
        .position_range = Eigen::Vector4f(light.position.x(), light.position.y(), light.position.z(), light.range),
        .direction_type =
            Eigen::Vector4f(light.direction.x(), light.direction.y(), light.direction.z(), static_cast<f32>(static_cast<int>(light.type))),
        .cone_angles = Eigen::Vector4f(light.spot_inner_cone_angle, light.spot_outer_cone_angle, 0.0f, 0.0f),
    };
}

} // namespace

void LightManager::add(const Light& light) {
    m_lights.push_back(&light);
}

const Light* LightManager::directionalLight() const {
    for (const Light* light : m_lights) {
        if (light->type == LightType::Directional && light->casts_shadows) {
            return light;
        }
    }
    return nullptr;
}

void LightManager::build() {
    if (m_lights.empty()) {
        m_buffer = nullptr;
        m_view = nullptr;
        return;
    }

    std::vector<GpuLight> gpu_lights;
    gpu_lights.reserve(m_lights.size());
    for (const Light* light : m_lights) {
        gpu_lights.push_back(toGpuLight(*light));
    }

    const usize byte_size = gpu_lights.size() * sizeof(GpuLight);

    sg_buffer_desc desc = {};
    desc.size = byte_size;
    desc.usage.storage_buffer = true;
    desc.data = sg_range{gpu_lights.data(), byte_size};
    desc.label = "light buffer";
    m_buffer = gpu::makeBuffer(desc);

    sg_view_desc view_desc = {};
    view_desc.storage_buffer.buffer = *m_buffer;
    view_desc.label = "light buffer view";
    m_view = gpu::makeView(view_desc);
}

} // namespace graphics
