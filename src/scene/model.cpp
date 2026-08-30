#include <scene/model.hpp>

#include <rlog.h>

namespace scene {

namespace {

// Transform the 8 local corners and re-fit; loose for a rotated box but never smaller than true.
Eigen::AlignedBox3f worldBounds(const Eigen::Matrix4f& world_matrix, const Eigen::AlignedBox3f& local) {
    Eigen::AlignedBox3f out;
    for (int i = 0; i < 8; i++) {
        const Eigen::Vector3f corner = local.corner(static_cast<Eigen::AlignedBox3f::CornerType>(i));
        const Eigen::Vector4f world = world_matrix * corner.homogeneous();
        out.extend(world.head<3>() / world.w());
    }
    return out;
}

} // namespace

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
void Model::addPart(T mesh, Eigen::Matrix4f transform, graphics::Material material, Eigen::AlignedBox3f local_bounds) {
    m_parts.push_back(ModelPart{std::move(mesh), std::move(transform), std::move(material), std::move(local_bounds)});
}

template void Model::addPart<graphics::StandardMesh>(graphics::StandardMesh, Eigen::Matrix4f, graphics::Material, Eigen::AlignedBox3f);
template void Model::addPart<graphics::InstancedMesh>(graphics::InstancedMesh, Eigen::Matrix4f, graphics::Material, Eigen::AlignedBox3f);

void Model::render(graphics::Renderer& renderer, const Eigen::Matrix4f& world_transform, const graphics::Camera& camera) {
    const graphics::Frustum& frustum = camera.frustum();
    for (ModelPart& part : m_parts) {
        const Eigen::Matrix4f world_matrix = world_transform * part.transform;

        // Instanced parts carry per-instance transforms we don't cull yet -- always submit those.
        if (std::holds_alternative<graphics::StandardMesh>(part.mesh) &&
            !frustum.intersects(worldBounds(world_matrix, part.local_bounds))) {
            continue;
        }

        std::visit([&](auto& mesh) { renderer.renderMesh(mesh, part.material, world_matrix, camera); }, part.mesh);
    }
}

} // namespace scene
