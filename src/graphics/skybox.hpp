#pragma once

#include <common.h>
#include <graphics/texture.hpp>

#include <Eigen/Dense>

#include <variant>

namespace graphics {

// Procedural vertical gradient: ground color at the horizon fading to zenith overhead.
struct GradientSky {
    Eigen::Vector3f zenith{0.53f, 0.71f, 1.0f}; // skyblue
    Eigen::Vector3f ground{0.15f, 0.15f, 0.15f}; // dark gray
};

// Names the variant slot; the cubemap loader is not built yet (see skybox plan).
struct CubemapSky {
    Texture cubemap;
};

// intensity is shared by both variants; it scales sky color before the tonemap.
struct Skybox {
    std::variant<GradientSky, CubemapSky> source = GradientSky{};
    f32 intensity = 1.0f;
};

} // namespace graphics
