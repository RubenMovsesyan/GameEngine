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

        // World space -> camera space.
        Eigen::Matrix4f transformationMatrix() const;

        // The combined view-projection matrix -- projection * transformationMatrix().
        Eigen::Matrix4f viewProjectionMatrix() const { return m_projection * transformationMatrix(); }
        Eigen::Vector3f position() const { return m_position; }

        // For test/control code: modify position in place, or retarget what the camera
        // looks at. up is left alone -- always (0, 1, 0) for now, per the caller.
        Eigen::Vector3f* mutablePosition() { return &m_position; }
        void setLookAt(const Eigen::Vector3f& target) { m_target = target; }

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
