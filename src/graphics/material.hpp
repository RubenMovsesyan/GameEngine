#pragma once

#include <graphics/texture.hpp>
#include <gpu/device.hpp>

#include <Eigen/Dense>

#include <optional>

namespace graphics {

// glTF metallic-roughness material: two optional textures plus the factors that always apply.
class Material {
    public:
        static Material create(const gpu::Device&, std::optional<Texture> base_color_texture, std::optional<Texture> metallic_roughness_texture,
                                const Eigen::Vector4f& base_color_factor, f32 metallic_factor, f32 roughness_factor);

        const std::optional<Texture>& baseColorTexture() const { return m_base_color_texture; }
        const std::optional<Texture>& metallicRoughnessTexture() const { return m_metallic_roughness_texture; }
        nvrhi::IBuffer* factorsBuffer() const { return m_factors_buffer; }

        // Cached g-buffer texture binding set. Rebuilt only when a texture's underlying handle
        // changes (async placeholder -> real swap); sampler/layout/fallback are Renderer-stable.
        nvrhi::IBindingSet* gbufferBindingSet(nvrhi::IDevice*, nvrhi::IBindingLayout*, nvrhi::ITexture* white_fallback,
                                              nvrhi::ISampler*) const;

    private:
        Material() = default;

        std::optional<Texture> m_base_color_texture;
        std::optional<Texture> m_metallic_roughness_texture;
        nvrhi::BufferHandle m_factors_buffer;

        mutable nvrhi::BindingSetHandle m_gbuffer_binding_set;
        mutable nvrhi::ITexture* m_built_base = nullptr; // handles the cached set was built against
        mutable nvrhi::ITexture* m_built_mr = nullptr;
};

} // namespace graphics
