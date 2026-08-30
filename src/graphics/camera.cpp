#include <graphics/camera.hpp>

#include <cmath>

namespace graphics {

Camera::Camera(
    f32 fov_y_radians, f32 aspect_ratio, f32 near_plane, f32 far_plane, const Eigen::Vector3f& position,
    const Eigen::Vector3f& direction, const Eigen::Vector3f& up
)
    : m_fov_y(fov_y_radians), m_aspect_ratio(aspect_ratio), m_near_plane(near_plane), m_far_plane(far_plane),
      m_position(position), m_direction(direction), m_up(up), m_projection(Eigen::Matrix4f::Zero()),
      m_inverse_projection(Eigen::Matrix4f::Zero()) {
    // Vulkan clip space, depth [0, 1]. No Y-flip on row 1 -- pairs with Renderer's gbuffer frontCounterClockwise=true.
    f32 f = 1.0f / std::tan(m_fov_y / 2.0f);
    f32 a = m_far_plane / (m_near_plane - m_far_plane);
    f32 b = (m_near_plane * m_far_plane) / (m_near_plane - m_far_plane);

    m_projection(0, 0) = f / m_aspect_ratio;
    m_projection(1, 1) = f;
    m_projection(2, 2) = a;
    m_projection(2, 3) = b;
    m_projection(3, 2) = -1.0f;

    // Closed-form inverse of the sparse projection matrix above -- see camera plan notes.
    m_inverse_projection(0, 0) = m_aspect_ratio / f;
    m_inverse_projection(1, 1) = 1.0f / f;
    m_inverse_projection(2, 3) = -1.0f;
    m_inverse_projection(3, 2) = 1.0f / b;
    m_inverse_projection(3, 3) = a / b;
}

void Camera::rebuildIfDirty() const {
    if (!m_dirty) {
        return;
    }

    Eigen::Vector3f forward = m_direction.normalized();
    Eigen::Vector3f right = forward.cross(m_up).normalized();
    Eigen::Vector3f up = right.cross(forward);

    m_view = Eigen::Matrix4f::Identity();

    m_view(0, 0) = right.x();
    m_view(0, 1) = right.y();
    m_view(0, 2) = right.z();
    m_view(0, 3) = -right.dot(m_position);

    m_view(1, 0) = up.x();
    m_view(1, 1) = up.y();
    m_view(1, 2) = up.z();
    m_view(1, 3) = -up.dot(m_position);

    m_view(2, 0) = -forward.x();
    m_view(2, 1) = -forward.y();
    m_view(2, 2) = -forward.z();
    m_view(2, 3) = forward.dot(m_position);

    // Inverse of a rigid transform: transpose the rotation block, restore the original position as translation.
    m_view_inverse = Eigen::Matrix4f::Identity();
    m_view_inverse.block<3, 3>(0, 0) << right.x(), up.x(), -forward.x(), right.y(), up.y(), -forward.y(), right.z(), up.z(), -forward.z();
    m_view_inverse.block<3, 1>(0, 3) = m_position;

    m_view_projection = m_projection * m_view;
    m_inverse_view_projection = m_view_inverse * m_inverse_projection;
    m_frustum = Frustum::fromViewProjection(m_view_projection);

    m_dirty = false;
}

const Eigen::Matrix4f& Camera::transformationMatrix() const {
    rebuildIfDirty();
    return m_view;
}

const Eigen::Matrix4f& Camera::viewProjectionMatrix() const {
    rebuildIfDirty();
    return m_view_projection;
}

const Eigen::Matrix4f& Camera::inverseViewProjectionMatrix() const {
    rebuildIfDirty();
    return m_inverse_view_projection;
}

const Frustum& Camera::frustum() const {
    rebuildIfDirty();
    return m_frustum;
}

} // namespace graphics
