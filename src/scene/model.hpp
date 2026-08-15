#pragma once

#include <graphics/camera.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>

#include <Eigen/Dense>

#include <vector>

namespace scene {

struct ModelPart {
    graphics::Mesh mesh;
    Eigen::Matrix4f transform; // local to the model's own origin
};

// A flat collection of meshes, each with its own local transform.
class Model {
    public:
        void addPart(graphics::Mesh mesh, Eigen::Matrix4f transform = Eigen::Matrix4f::Identity());
        void render(graphics::Renderer&, const Eigen::Matrix4f& world_transform, const graphics::Camera&) const;

        u32 partCount() const { return static_cast<u32>(m_parts.size()); }

    private:
        std::vector<ModelPart> m_parts;
};

} // namespace scene
