#include <graphics/shadow.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace graphics {

namespace {

// Right-handed, -Z forward, matching camera.cpp's view convention (no Y-flip).
Eigen::Matrix4f lookAtRH(const Eigen::Vector3f& eye, const Eigen::Vector3f& target, const Eigen::Vector3f& up) {
    const Eigen::Vector3f forward = (target - eye).normalized();
    const Eigen::Vector3f right = forward.cross(up).normalized();
    const Eigen::Vector3f u = right.cross(forward);

    Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
    view.block<1, 3>(0, 0) = right.transpose();
    view.block<1, 3>(1, 0) = u.transpose();
    view.block<1, 3>(2, 0) = -forward.transpose();
    view(0, 3) = -right.dot(eye);
    view(1, 3) = -u.dot(eye);
    view(2, 3) = forward.dot(eye);
    return view;
}

// Vulkan clip: x,y in [-1,1], z in [0,1], +Y down -- row 1 is negated to match Camera.
Eigen::Matrix4f orthographic(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
    Eigen::Matrix4f m = Eigen::Matrix4f::Zero();
    m(0, 0) = 2.0f / (r - l);
    m(1, 1) = -2.0f / (t - b);
    m(2, 2) = -1.0f / (f - n);
    m(0, 3) = -(r + l) / (r - l);
    m(1, 3) = (t + b) / (t - b);
    m(2, 3) = -n / (f - n);
    m(3, 3) = 1.0f;
    return m;
}

// Extra depth pulled toward the light so casters between the light and the slice still render.
constexpr f32 kCasterMargin = 50.0f;

} // namespace

CascadeSetup computeCascades(
    const Camera& camera, const Eigen::Vector3f& light_direction, f32 shadow_distance, f32 split_lambda,
    f32 shadow_map_resolution
) {
    const f32 near = camera.nearPlane();
    const f32 far = std::min(shadow_distance, camera.farPlane());
    const std::array<Eigen::Vector3f, 8>& frustum_corners = camera.frustum().corners();

    // Boundary distances: split[0] = near, split[kShadowCascadeCount] = far.
    std::array<f32, kShadowCascadeCount + 1> split{};
    split[0] = near;
    for (u32 i = 1; i <= kShadowCascadeCount; i++) {
        const f32 p = static_cast<f32>(i) / static_cast<f32>(kShadowCascadeCount);
        const f32 uniform = near + (far - near) * p;
        const f32 logarithmic = near * std::pow(far / near, p);
        split[i] = logarithmic * split_lambda + uniform * (1.0f - split_lambda);
    }

    const Eigen::Vector3f light_dir = light_direction.normalized();
    const Eigen::Vector3f up = std::abs(light_dir.y()) > 0.99f ? Eigen::Vector3f(0.0f, 0.0f, 1.0f) : Eigen::Vector3f(0.0f, 1.0f, 0.0f);

    CascadeSetup out;
    for (u32 c = 0; c < kShadowCascadeCount; c++) {
        const f32 t_near = (split[c] - near) / (far - near);
        const f32 t_far = (split[c + 1] - near) / (far - near);

        std::array<Eigen::Vector3f, 8> slice{};
        for (u32 k = 0; k < 4; k++) {
            const Eigen::Vector3f edge = frustum_corners[k + 4] - frustum_corners[k];
            slice[k] = frustum_corners[k] + t_near * edge;
            slice[k + 4] = frustum_corners[k] + t_far * edge;
        }

        Eigen::Vector3f center = Eigen::Vector3f::Zero();
        for (const Eigen::Vector3f& corner : slice) {
            center += corner;
        }
        center /= 8.0f;

        f32 radius = 0.0f;
        for (const Eigen::Vector3f& corner : slice) {
            radius = std::max(radius, (corner - center).norm());
        }
        radius = std::ceil(radius * 16.0f) / 16.0f; // quantize so tiny per-frame wobble doesn't resize the box

        const Eigen::Vector3f eye = center - light_dir * (radius + kCasterMargin);
        const Eigen::Matrix4f view = lookAtRH(eye, center, up);

        // Snap the light-space centre to a texel grid so shadow edges don't crawl as the camera moves.
        Eigen::Vector3f center_ls = (view * center.homogeneous()).head<3>();
        const f32 texels_per_unit = shadow_map_resolution / (2.0f * radius);
        center_ls.x() = std::floor(center_ls.x() * texels_per_unit) / texels_per_unit;
        center_ls.y() = std::floor(center_ls.y() * texels_per_unit) / texels_per_unit;

        const f32 l = center_ls.x() - radius;
        const f32 r = center_ls.x() + radius;
        const f32 b = center_ls.y() - radius;
        const f32 t = center_ls.y() + radius;

        // Forward is -Z, so a corner's distance in front of the light is -z_lightspace.
        f32 nearest = std::numeric_limits<f32>::max();
        f32 farthest = std::numeric_limits<f32>::lowest();
        for (const Eigen::Vector3f& corner : slice) {
            const f32 dist = -(view * corner.homogeneous()).z();
            nearest = std::min(nearest, dist);
            farthest = std::max(farthest, dist);
        }
        const f32 n_dist = std::max(0.05f, nearest - kCasterMargin);
        const f32 f_dist = farthest;

        out.view_proj[c] = orthographic(l, r, b, t, n_dist, f_dist) * view;
        out.split_view_depth[c] = split[c + 1];
    }
    return out;
}

} // namespace graphics
