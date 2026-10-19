#pragma once

#include <graphics/primitives/scene/material.hpp>
#include <graphics/primitives/scene/mesh.hpp>

#include <Eigen/Dense>

#include <variant>
#include <vector>

namespace scene {

struct ModelPart {
    std::variant<graphics::primitives::scene::StandardMesh, graphics::primitives::scene::InstancedMesh> mesh;
    Eigen::Matrix4f transform; // local to the model's own origin
    graphics::primitives::scene::Material material;
    Eigen::AlignedBox3f local_bounds; // mesh-space AABB (pre-transform); for frustum culling

    // Shares the existing vertex/index buffers -- no re-upload. Warns and no-ops if already that kind.
    void convertToStandard();
    // Lossy: an InstancedMesh's per-instance transforms are dropped, not carried over.
    void convertToInstanced();
};

// Transform the 8 local corners and re-fit; loose for a rotated box but never smaller than true.
Eigen::AlignedBox3f worldBounds(const Eigen::Matrix4f& world_matrix, const Eigen::AlignedBox3f& local);

// Flat collection of meshes with local transforms. No Camera/Renderer dependency.
class Model {
    public:
        // Shared by every holder of a cached Model -- prefer caller-side placement.
        Eigen::Matrix4f world_transform = Eigen::Matrix4f::Identity();

        template <graphics::primitives::scene::Mesh T>
        void addPart(T mesh, Eigen::Matrix4f transform, graphics::primitives::scene::Material material, Eigen::AlignedBox3f local_bounds);

        u32 partCount() const { return static_cast<u32>(m_parts.size()); }
        const std::vector<ModelPart>& parts() const { return m_parts; }
        std::vector<ModelPart>& mutableParts() { return m_parts; }

    private:
        std::vector<ModelPart> m_parts;
};

} // namespace scene
