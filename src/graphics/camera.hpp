#pragma once

#include <common.h>

#include <Eigen/Dense>

namespace graphics {

class Camera {
    public:
        Camera(
            f32 fov_y_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane, const Eigen::Vector3f& position,
            const Eigen::Vector3f& target, const Eigen::Vector3f& up
        );

        // World space -> camera space. Not combined with m_projection yet.
        Eigen::Matrix4f transformationMatrix() const;

    private:
        f32 m_fov_y;
        f32 m_aspect_ratio;
        f32 m_near_plane;
        f32 m_far_plane;

        Eigen::Vector3f m_position;
        Eigen::Vector3f m_target;
        Eigen::Vector3f m_up;

        Eigen::Matrix4f m_projection;
};

} // namespace graphics
