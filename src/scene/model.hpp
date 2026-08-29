#pragma once

#include <graphics/camera.hpp>
#include <graphics/material.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>

#include <Eigen/Dense>

#include <variant>
#include <vector>

namespace scene {

struct ModelPart {
    std::variant<graphics::StandardMesh, graphics::InstancedMesh> mesh;
    Eigen::Matrix4f transform; // local to the model's own origin
    graphics::Material material;

    // Shares the existing vertex/index buffers -- no re-upload. Warns and no-ops if already that kind.
    void convertToStandard();
    // Lossy: an InstancedMesh's per-instance transforms are dropped, not carried over.
    void convertToInstanced();
};

// A flat collection of meshes, each with its own local transform.
class Model {
    public:
        template <graphics::Mesh T>
        void addPart(T mesh, Eigen::Matrix4f transform, graphics::Material material);
        void render(graphics::Renderer&, const Eigen::Matrix4f& world_transform, const graphics::Camera&);

        u32 partCount() const { return static_cast<u32>(m_parts.size()); }
        std::vector<ModelPart>& mutableParts() { return m_parts; }

    private:
        std::vector<ModelPart> m_parts;
};

} // namespace scene
