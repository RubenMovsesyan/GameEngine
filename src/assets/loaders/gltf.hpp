#pragma once

#include <graphics/primitives/scene/light.hpp>
#include <scene/model.hpp>

#include <filesystem>
#include <vector>

namespace assets::loaders {

// One Model per mesh-bearing scene root node; lights flat across the file.
struct GltfAsset {
    std::vector<scene::Model> models;
    std::vector<graphics::primitives::scene::Light> lights;
};

// Fatal on parse/buffer failure or unsupported content.
GltfAsset loadGltf(const std::filesystem::path& path);

// All root models' parts in one Model (copies share GPU buffers -- no re-upload).
scene::Model mergeModels(const std::vector<scene::Model>& models);

} // namespace assets::loaders
