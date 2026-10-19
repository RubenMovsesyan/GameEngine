#pragma once

#include <assets/asset_library.hpp>

namespace assets::loaders {

// Built-in loaders:
//   GltfAsset                    <- .glb / .gltf
//   scene::Model, vector<Light>  <- split from GltfAsset (root models merged into one Model)
//   Texture                      <- image file (stb_image formats), sRGB, async decode
void registerDefaultLoaders(AssetLibrary& library);

} // namespace assets::loaders
