#pragma once

#include <assets/asset_library.hpp>

namespace assets::loaders {

// GltfAsset, Model + vector<Light> (split from GltfAsset), Texture (image file, sRGB).
void registerDefaultLoaders(AssetLibrary& library);

} // namespace assets::loaders
