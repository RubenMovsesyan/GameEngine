#pragma once

#include <common.h>
#include <graphics/primitives/scene/texture.hpp>

#include <Eigen/Dense>

#include <variant>

namespace graphics::primitives::scene {

// Procedural vertical gradient: ground color at the horizon fading to zenith overhead.
// zenith/ground are linear radiance in the same units as lights; the tonemap applies
// exposure downstream, so these are calibrated to look right at the default exposure.
struct GradientSky {
    Eigen::Vector3f zenith{90.0f, 120.0f, 170.0f}; // slightly unsaturated skyblue
    Eigen::Vector3f ground{25.0f, 25.0f, 25.0f};   // dim ground bounce
};

// Names the variant slot; the cubemap loader is not built yet (see skybox plan).
struct CubemapSky {
    Texture cubemap;
};

// intensity is shared by both variants: a plain artistic multiplier on the (already
// calibrated) sky color. Leave it at 1.0 unless you deliberately want a brighter/dimmer sky.
struct Skybox {
    std::variant<GradientSky, CubemapSky> source = GradientSky{};
    f32 intensity = 1.0f;
};

} // namespace graphics::primitives::scene
