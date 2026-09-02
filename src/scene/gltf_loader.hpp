#pragma once

#include <graphics/light.hpp>
#include <scene/model.hpp>

#include <filesystem>
#include <vector>

namespace scene {

// One Model per glTF scene root node whose subtree contains a mesh -- a root node with no
// mesh anywhere under it (e.g. a light-only node) contributes no Model. Lights are
// collected flat across the whole file, independent of which root node they're under.
struct LoadedGltf {
    std::vector<Model> models;
    std::vector<graphics::Light> lights;
};

LoadedGltf loadGltf(const std::filesystem::path&);

} // namespace scene
