#include <graphics/primitives/scene/frustum.hpp>

namespace graphics::primitives::scene {

Frustum Frustum::fromViewProjection(const Eigen::Matrix4f& view_projection) {
    Frustum frustum;

    // Gribb-Hartmann: column-vector convention here, so each plane is a combination of the matrix rows.
    const Eigen::RowVector4f row_w = view_projection.row(3);
    const Eigen::RowVector4f row_x = view_projection.row(0);
    const Eigen::RowVector4f row_y = view_projection.row(1);
    const Eigen::RowVector4f row_z = view_projection.row(2);

    const auto assign = [&](Plane p, const Eigen::RowVector4f& coeffs) {
        Eigen::Hyperplane<f32, 3>& plane = frustum.m_planes[static_cast<usize>(p)];
        plane.coeffs() = coeffs.transpose(); // (a, b, c, d), normal points inward
        plane.normalize();                   // divide all 4 by |normal| -> signedDistance() is a true distance
    };

    assign(Plane::Left, row_w + row_x);
    assign(Plane::Right, row_w - row_x);
    assign(Plane::Bottom, row_w + row_y);
    assign(Plane::Top, row_w - row_y);
    assign(Plane::Near, row_z); // Vulkan clip volume is 0 <= z <= w, so Near is row_z alone (not row_w + row_z)
    assign(Plane::Far, row_w - row_z);

    // Corners: unproject the NDC cube. Vulkan clip is x,y in [-1,1], z in [0,1].
    const Eigen::Matrix4f inverse = view_projection.inverse();
    static constexpr f32 kNdcXY[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}}; // BL, BR, TL, TR
    usize corner = 0;
    for (f32 z : {0.0f, 1.0f}) { // near plane, then far plane
        for (const auto& xy : kNdcXY) {
            const Eigen::Vector4f h = inverse * Eigen::Vector4f(xy[0], xy[1], z, 1.0f);
            frustum.m_corners[corner++] = h.head<3>() / h.w();
        }
    }

    return frustum;
}

bool Frustum::intersects(const Eigen::AlignedBox3f& world_box) const {
    // Positive-vertex test against each inward plane; conservative near frustum edges (see docs).
    for (const Eigen::Hyperplane<f32, 3>& plane : m_planes) {
        const Eigen::Vector3f& n = plane.normal();
        const Eigen::Vector3f positive_vertex = (n.array() >= 0.0f).select(world_box.max(), world_box.min());
        if (n.dot(positive_vertex) + plane.offset() < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace graphics::primitives::scene
