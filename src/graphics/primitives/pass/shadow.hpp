#pragma once

#include <common.h>
#include <graphics/primitives/scene/camera.hpp>

#include <Eigen/Dense>

#include <array>

namespace graphics::primitives::pass {

inline constexpr u32 kShadowCascadeCount = 4;

struct CascadeSetup {
    std::array<Eigen::Matrix4f, kShadowCascadeCount> view_proj;
    // Far edge of each cascade as a view-space distance; the lighting pass picks a cascade by comparing
    // the fragment's view depth against these.
    std::array<f32, kShadowCascadeCount> split_view_depth;
};

// Fits a texel-snapped orthographic light view-projection to each slice of the camera frustum between
// the near plane and shadow_distance. split_lambda in [0, 1] blends uniform (0) and logarithmic (1)
// splits -- higher packs more resolution near the camera.
CascadeSetup computeCascades(
    const scene::Camera& camera, const Eigen::Vector3f& light_direction, f32 shadow_distance, f32 split_lambda,
    f32 shadow_map_resolution
);

} // namespace graphics::primitives::pass
