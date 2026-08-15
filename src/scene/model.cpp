#include <scene/model.hpp>

namespace scene {

void Model::addPart(graphics::Mesh mesh, Eigen::Matrix4f transform) {
    m_parts.push_back(ModelPart{std::move(mesh), std::move(transform)});
}

void Model::render(graphics::Renderer& renderer, const Eigen::Matrix4f& world_transform, const graphics::Camera& camera) const {
    for (const ModelPart& part : m_parts) {
        renderer.renderMesh(part.mesh, world_transform * part.transform, camera);
    }
}

} // namespace scene
