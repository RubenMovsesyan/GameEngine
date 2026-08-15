#include <graphics/camera.hpp>

#include <cmath>

namespace graphics {

Camera::Camera(
    f32 fov_y_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane, const Eigen::Vector3f& position,
    const Eigen::Vector3f& target, const Eigen::Vector3f& up
)
    : m_fov_y(fov_y_radians), m_aspect_ratio(aspect_ratio), m_near_plane(near_plane), m_far_plane(far_plane),
      m_position(position), m_target(target), m_up(up), m_projection(Eigen::Matrix4f::Zero()) {
    // Vulkan clip space, depth [0, 1]. No Y-flip on row 1 -- pairs with Renderer's gbuffer frontCounterClockwise=true.
    f32 f = 1.0f / std::tan(m_fov_y / 2.0f);
    m_projection(0, 0) = f / m_aspect_ratio;
    m_projection(1, 1) = f;
    m_projection(2, 2) = m_far_plane / (m_near_plane - m_far_plane);
    m_projection(2, 3) = (m_near_plane * m_far_plane) / (m_near_plane - m_far_plane);
    m_projection(3, 2) = -1.0f;
}

Eigen::Matrix4f Camera::transformationMatrix() const {
    Eigen::Vector3f forward = (m_target - m_position).normalized();
    Eigen::Vector3f right = forward.cross(m_up).normalized();
    Eigen::Vector3f up = right.cross(forward);

    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();

    view(0, 0) = right.x();
    view(0, 1) = right.y();
    view(0, 2) = right.z();
    view(0, 3) = -right.dot(m_position);

    view(1, 0) = up.x();
    view(1, 1) = up.y();
    view(1, 2) = up.z();
    view(1, 3) = -up.dot(m_position);

    view(2, 0) = -forward.x();
    view(2, 1) = -forward.y();
    view(2, 2) = -forward.z();
    view(2, 3) = forward.dot(m_position);

    return view;
}

} // namespace graphics
