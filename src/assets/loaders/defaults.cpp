#include <assets/loaders/defaults.hpp>

#include <assets/loaders/gltf.hpp>
#include <assets/loaders/image.hpp>

#include <tuple>
#include <vector>

namespace assets::loaders {

void registerDefaultLoaders(AssetLibrary& library) {
    using graphics::primitives::scene::Light;
    using graphics::primitives::scene::Texture;

    library.setLoader<GltfAsset>(loadGltf);
    library.setSplitLoader<GltfAsset, scene::Model, std::vector<Light>>(
        [](const GltfAsset& gltf) { return std::tuple<scene::Model, std::vector<Light>>{mergeModels(gltf.models), gltf.lights}; }
    );
    library.setLoader<Texture>(loadTexture);
}

} // namespace assets::loaders
