#pragma once

#include <scene/scene.hpp>

#include <filesystem>

namespace scene {

Scene loadGltf(const std::filesystem::path&);

} // namespace scene
