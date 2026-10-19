#include <assets/loaders/image.hpp>

#include <rlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fstream>
#include <iterator>

namespace assets::loaders {

graphics::primitives::scene::DecodedPixels decodeImage(std::vector<u8> bytes) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        RLOG(LL_FATAL, "image: failed to decode (%s)", stbi_failure_reason());
    }
    graphics::primitives::scene::DecodedPixels result{
        std::vector<u8>(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4), static_cast<u32>(width), static_cast<u32>(height)
    };
    stbi_image_free(pixels);
    RLOG(LL_DEBUG, "image: decoded %ux%u", result.width, result.height);
    return result;
}

graphics::primitives::scene::Texture loadTexture(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RLOG(LL_FATAL, "image: failed to open %s", path.c_str());
    }
    std::vector<u8> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto decode = [bytes = std::move(bytes)]() mutable { return decodeImage(std::move(bytes)); };
    return graphics::primitives::scene::Texture::loadAsyncThread(std::move(decode), SG_PIXELFORMAT_SRGB8A8);
}

} // namespace assets::loaders
