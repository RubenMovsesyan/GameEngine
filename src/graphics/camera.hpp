#pragma once

#include <common.h>
#include <graphics/frustum.hpp>

#include <Eigen/Dense>

namespace graphics {

class Camera {
    public:
        Camera(
            f32 fov_y_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane, const Eigen::Vector3f& position,
            const Eigen::Vector3f& direction, const Eigen::Vector3f& up
        );

        // World space -> camera space.
        const Eigen::Matrix4f& transformationMatrix() const;

        // The combined view-projection matrix -- projection * transformationMatrix().
        const Eigen::Matrix4f& viewProjectionMatrix() const;
        const Eigen::Matrix4f& inverseViewProjectionMatrix() const;
        // World-space view frustum, cached alongside the matrices.
        const Frustum& frustum() const;
        const Eigen::Vector3f& position() const { return m_position; }
        const Eigen::Vector3f& direction() const { return m_direction; }

        // Both unconditionally mark the cached matrices dirty.
        Eigen::Vector3f* mutablePosition() {
            m_dirty = true;
            return &m_position;
        }
        Eigen::Vector3f* mutableDirection() {
            m_dirty = true;
            return &m_direction;
        }

    private:
        void rebuildIfDirty() const;

        f32 m_fov_y;
        f32 m_aspect_ratio;
        f32 m_near_plane;
        f32 m_far_plane;

        Eigen::Vector3f m_position;
        Eigen::Vector3f m_direction;
        Eigen::Vector3f m_up;

        Eigen::Matrix4f m_projection;
        Eigen::Matrix4f m_inverse_projection;

        mutable bool m_dirty = true;
        mutable Eigen::Matrix4f m_view;
        mutable Eigen::Matrix4f m_view_inverse;
        mutable Eigen::Matrix4f m_view_projection;
        mutable Eigen::Matrix4f m_inverse_view_projection;
        mutable Frustum m_frustum;
};

} // namespace graphics
