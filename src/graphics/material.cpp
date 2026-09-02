#include <graphics/material.hpp>

namespace graphics {

Material Material::create(std::optional<Texture> base_color_texture, std::optional<Texture> metallic_roughness_texture,
                           const Eigen::Vector4f& base_color_factor, f32 metallic_factor, f32 roughness_factor) {
    Material material;
    material.m_base_color_texture = std::move(base_color_texture);
    material.m_metallic_roughness_texture = std::move(metallic_roughness_texture);
    material.m_base_color_factor = base_color_factor;
    material.m_metallic_factor = metallic_factor;
    material.m_roughness_factor = roughness_factor;
    return material;
}

} // namespace graphics
