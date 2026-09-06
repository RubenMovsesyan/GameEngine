#pragma once

#include <common.h>

#include <Eigen/Dense>

namespace graphics::primitives::scene {

// One self-luminous point, as consumed by graphics::renderers::PointRenderer. Plain data --
// no GPU ownership. The renderer copies a span of these into its own instance buffer each
// frame and expands each into a view-facing billboard.
//
// Layout is fixed at 32 bytes so a span of these maps straight onto a per-instance vertex
// buffer (position + size + color, matching the `points` shader's instance attributes).
struct PointInstance {
    Eigen::Vector3f position;  // world space
    f32 size;                  // world-space radius of the billboard
    Eigen::Vector4f color;     // linear radiance (may exceed 1); .a is an extra intensity multiplier
};

static_assert(sizeof(PointInstance) == 32, "PointInstance must stay tightly packed for the instance buffer");

} // namespace graphics::primitives::scene
