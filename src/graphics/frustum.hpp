#pragma once

#include <common.h>

#include <Eigen/Dense>
#include <Eigen/Geometry>

#include <array>

namespace graphics {

// Six inward-facing bounding planes of a view volume, extracted from an arbitrary world->clip matrix.
class Frustum {
    public:
        enum class Plane { Left, Right, Bottom, Top, Near, Far };
        static constexpr usize kPlaneCount = 6;

        Frustum() = default;

        // Gribb-Hartmann plane extraction from a combined (world -> clip) view-projection matrix.
        static Frustum fromViewProjection(const Eigen::Matrix4f& view_projection);

        // Conservative: never false for a visible box; may return true just past an edge or corner.
        bool intersects(const Eigen::AlignedBox3f& world_box) const;

        const Eigen::Hyperplane<f32, 3>& plane(Plane p) const { return m_planes[static_cast<usize>(p)]; }
        // Eight world-space corners: near {BL, BR, TL, TR} then far {BL, BR, TL, TR}.
        const std::array<Eigen::Vector3f, 8>& corners() const { return m_corners; }

    private:
        std::array<Eigen::Hyperplane<f32, 3>, kPlaneCount> m_planes{};
        std::array<Eigen::Vector3f, 8> m_corners{};
};

} // namespace graphics
