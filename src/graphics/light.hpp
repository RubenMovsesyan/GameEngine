#pragma once

#include <common.h>
#include <utils/display.hpp>

#include <Eigen/Dense>

#include <string>

namespace graphics {

enum class LightType {
    Directional,
    Point,
    Spot,
};

// Plain data -- KHR_lights_punctual's fields plus the node's world position/direction.
class Light {
    public:
        LightType type;
        Eigen::Vector3f color;      // linear RGB
        f32 intensity;              // raw glTF units: lux (Directional) or candela (Point/Spot)
        Eigen::Vector3f position;   // world space; meaningful for Point/Spot
        Eigen::Vector3f direction;  // world space, normalized; meaningful for Directional/Spot
        f32 range;                 // meters; 0 means infinite range per glTF spec
        f32 spot_inner_cone_angle; // radians; Spot only
        f32 spot_outer_cone_angle; // radians; Spot only

        std::string display() const;
};

static_assert(utils::Display<Light>);

} // namespace graphics
