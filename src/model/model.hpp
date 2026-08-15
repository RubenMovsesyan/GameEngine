#pragma once

#include <graphics/camera.hpp>
#include <graphics/light.hpp>
#include <graphics/mesh.hpp>
#include <graphics/renderer.hpp>

#include <Eigen/Dense>

#include <vector>

namespace model {

struct ModelPart {
    graphics::Mesh mesh;
    Eigen::Matrix4f transform; // local to the model's own origin
};

// A renderable asset: a flat collection of meshes, each with its own local transform.
// Rendering a model just means rendering each part individually, so there's no
// node-hierarchy or scene-graph concept here, just what's needed to compose meshes.
class Model {
    public:
        void addPart(graphics::Mesh mesh, Eigen::Matrix4f transform = Eigen::Matrix4f::Identity());
        void render(graphics::Renderer&, const Eigen::Matrix4f& world_transform, const graphics::Camera&) const;

        void addLight(graphics::Light light);
        const std::vector<graphics::Light>& lights() const { return m_lights; }

    private:
        std::vector<ModelPart> m_parts;
        std::vector<graphics::Light> m_lights;
};

} // namespace model
