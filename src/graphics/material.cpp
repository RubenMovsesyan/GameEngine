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

nvrhi::IBindingSet* Material::gbufferBindingSet(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout, nvrhi::ITexture* white_fallback,
                                               nvrhi::ISampler* sampler) const {
    nvrhi::ITexture* base = m_base_color_texture ? m_base_color_texture->texture() : white_fallback;
    nvrhi::ITexture* mr = m_metallic_roughness_texture ? m_metallic_roughness_texture->texture() : white_fallback;

    // Pointer compare catches both first build and the async placeholder -> real handle swap.
    if (m_gbuffer_binding_set && base == m_built_base && mr == m_built_mr) {
        return m_gbuffer_binding_set;
    }

    m_gbuffer_binding_set = device->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, base))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, mr))
            .addItem(nvrhi::BindingSetItem::Sampler(2, sampler))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_factors_buffer)),
        layout
    );
    m_built_base = base;
    m_built_mr = mr;
    return m_gbuffer_binding_set;
}

} // namespace graphics
