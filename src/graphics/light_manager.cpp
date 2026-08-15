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

LightManager::LightManager(const gpu::Device& device) : m_device(&device) {}

void LightManager::add(const Light& light) {
    m_lights.push_back(&light);
}

void LightManager::build() {
    if (m_lights.empty()) {
        m_buffer = nullptr;
        return;
    }

    std::vector<GpuLight> gpu_lights;
    gpu_lights.reserve(m_lights.size());
    for (const Light* light : m_lights) {
        gpu_lights.push_back(toGpuLight(*light));
    }

    nvrhi::BufferDesc desc{};
    desc.byteSize = gpu_lights.size() * sizeof(GpuLight);
    desc.structStride = sizeof(GpuLight);
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "light buffer";

    m_buffer = m_device->createBuffer(desc, gpu_lights.data(), gpu_lights.size() * sizeof(GpuLight));
}

} // namespace graphics
