#include <graphics/material.hpp>

namespace {

// Matches gbuffer.frag's std430 MaterialFactors block layout.
struct MaterialFactorsGPU {
    Eigen::Vector4f base_color_factor;
    f32 metallic_factor;
    f32 roughness_factor;
    f32 _pad[2];
};

} // namespace

namespace graphics {

Material Material::create(const gpu::Device& device, std::optional<Texture> base_color_texture, std::optional<Texture> metallic_roughness_texture,
                           const Eigen::Vector4f& base_color_factor, f32 metallic_factor, f32 roughness_factor) {
    MaterialFactorsGPU factors{base_color_factor, metallic_factor, roughness_factor, {}};

    nvrhi::BufferDesc desc{};
    desc.byteSize = sizeof(MaterialFactorsGPU);
    desc.structStride = sizeof(MaterialFactorsGPU);
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "material factors buffer";

    Material material;
    material.m_base_color_texture = std::move(base_color_texture);
    material.m_metallic_roughness_texture = std::move(metallic_roughness_texture);
    material.m_factors_buffer = device.createBuffer(desc, &factors, sizeof(factors));
    return material;
}

} // namespace graphics
