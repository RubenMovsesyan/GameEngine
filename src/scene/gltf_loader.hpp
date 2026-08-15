#pragma once

#include <gpu/device.hpp>
#include <scene/scene.hpp>

#include <filesystem>

namespace scene {

Scene loadGltf(const gpu::Device&, const std::filesystem::path&);

} // namespace scene
