#include <scene/model.hpp>

#include <rlog.h>

namespace scene {

Eigen::AlignedBox3f worldBounds(const Eigen::Matrix4f& world_matrix, const Eigen::AlignedBox3f& local) {
    Eigen::AlignedBox3f out;
    for (int i = 0; i < 8; i++) {
        const Eigen::Vector3f corner = local.corner(static_cast<Eigen::AlignedBox3f::CornerType>(i));
        const Eigen::Vector4f world = world_matrix * corner.homogeneous();
        out.extend(world.head<3>() / world.w());
    }
    return out;
}

void ModelPart::convertToStandard() {
    if (std::holds_alternative<graphics::primitives::scene::StandardMesh>(mesh)) {
        RLOG(LL_WARN, "ModelPart::convertToStandard: already a StandardMesh");
        return;
    }
    mesh = graphics::primitives::scene::StandardMesh::fromInstanced(std::get<graphics::primitives::scene::InstancedMesh>(mesh));
}

void ModelPart::convertToInstanced() {
    if (std::holds_alternative<graphics::primitives::scene::InstancedMesh>(mesh)) {
        RLOG(LL_WARN, "ModelPart::convertToInstanced: already an InstancedMesh");
        return;
    }
    mesh = graphics::primitives::scene::InstancedMesh::fromStandard(std::get<graphics::primitives::scene::StandardMesh>(mesh));
}

template <graphics::primitives::scene::Mesh T>
void Model::addPart(T mesh, Eigen::Matrix4f transform, graphics::primitives::scene::Material material, Eigen::AlignedBox3f local_bounds) {
    m_parts.push_back(ModelPart{std::move(mesh), std::move(transform), std::move(material), std::move(local_bounds)});
}

template void Model::addPart<graphics::primitives::scene::StandardMesh>(graphics::primitives::scene::StandardMesh, Eigen::Matrix4f, graphics::primitives::scene::Material, Eigen::AlignedBox3f);
template void Model::addPart<graphics::primitives::scene::InstancedMesh>(graphics::primitives::scene::InstancedMesh, Eigen::Matrix4f, graphics::primitives::scene::Material, Eigen::AlignedBox3f);

} // namespace scene
