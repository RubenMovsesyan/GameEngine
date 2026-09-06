#pragma once

#include <graphics/primitives/scene/texture.hpp>

#include <Eigen/Dense>

#include <optional>

namespace graphics::primitives::scene {

// glTF metallic-roughness material: two optional textures plus the factors that always apply.
//
// The factors used to live in a per-material storage buffer. They are 24 bytes of per-draw
// constant data, so they now ride along in the G-buffer uniform block instead -- one less
// buffer per material and one less binding.
class Material {
    public:
        static Material create(std::optional<Texture> base_color_texture, std::optional<Texture> metallic_roughness_texture,
                                const Eigen::Vector4f& base_color_factor, f32 metallic_factor, f32 roughness_factor);

        const std::optional<Texture>& baseColorTexture() const { return m_base_color_texture; }
        const std::optional<Texture>& metallicRoughnessTexture() const { return m_metallic_roughness_texture; }

        const Eigen::Vector4f& baseColorFactor() const { return m_base_color_factor; }
        f32 metallicFactor() const { return m_metallic_factor; }
        f32 roughnessFactor() const { return m_roughness_factor; }

    private:
        Material() = default;

        std::optional<Texture> m_base_color_texture;
        std::optional<Texture> m_metallic_roughness_texture;

        Eigen::Vector4f m_base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        f32 m_metallic_factor = 1.0f;
        f32 m_roughness_factor = 1.0f;
};

} // namespace graphics::primitives::scene
