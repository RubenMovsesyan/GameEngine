#include <graphics/light.hpp>

namespace graphics {

namespace {

std::string lightTypeName(LightType type) {
    switch (type) {
        case LightType::Directional:
            return "Directional";
        case LightType::Point:
            return "Point";
        case LightType::Spot:
            return "Spot";
    }
    return "Unknown";
}

std::string vec3String(const Eigen::Vector3f& v) {
    return "(" + std::to_string(v.x()) + ", " + std::to_string(v.y()) + ", " + std::to_string(v.z()) + ")";
}

} // namespace

std::string Light::display() const {
    return "Light{type=" + lightTypeName(type) + ", color=" + vec3String(color) + ", intensity=" + std::to_string(intensity) +
           ", position=" + vec3String(position) + ", direction=" + vec3String(direction) + ", range=" + std::to_string(range) +
           ", spot_inner_cone_angle=" + std::to_string(spot_inner_cone_angle) + ", spot_outer_cone_angle=" +
           std::to_string(spot_outer_cone_angle) + ", casts_shadows=" + (casts_shadows ? "true" : "false") + "}";
}

} // namespace graphics
