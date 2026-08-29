#include <scene/model.hpp>

#include <rlog.h>

namespace scene {

void ModelPart::convertToStandard() {
    if (std::holds_alternative<graphics::StandardMesh>(mesh)) {
        RLOG(LL_WARN, "ModelPart::convertToStandard: already a StandardMesh");
        return;
    }
    mesh = graphics::StandardMesh::fromInstanced(std::get<graphics::InstancedMesh>(mesh));
}

void ModelPart::convertToInstanced() {
    if (std::holds_alternative<graphics::InstancedMesh>(mesh)) {
        RLOG(LL_WARN, "ModelPart::convertToInstanced: already an InstancedMesh");
        return;
    }
    mesh = graphics::InstancedMesh::fromStandard(std::get<graphics::StandardMesh>(mesh));
}

template <graphics::Mesh T>
void Model::addPart(T mesh, Eigen::Matrix4f transform, graphics::Material material) {
    m_parts.push_back(ModelPart{std::move(mesh), std::move(transform), std::move(material)});
}

template void Model::addPart<graphics::StandardMesh>(graphics::StandardMesh, Eigen::Matrix4f, graphics::Material);
template void Model::addPart<graphics::InstancedMesh>(graphics::InstancedMesh, Eigen::Matrix4f, graphics::Material);

void Model::render(graphics::Renderer& renderer, const Eigen::Matrix4f& world_transform, const graphics::Camera& camera) {
    for (ModelPart& part : m_parts) {
        std::visit([&](auto& mesh) { renderer.renderMesh(mesh, part.material, world_transform * part.transform, camera); }, part.mesh);
    }
}

} // namespace scene
