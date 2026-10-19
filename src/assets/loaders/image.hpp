#pragma once

#include <graphics/primitives/scene/texture.hpp>

#include <filesystem>
#include <vector>

namespace assets::loaders {

// stb_image decode to tightly packed RGBA8. Fatal on failure. Safe off the sokol thread.
graphics::primitives::scene::DecodedPixels decodeImage(std::vector<u8> encoded_bytes);

// Standalone image file -> Texture (sRGB). Returns a placeholder at once; decodes on a
// background thread, swapped in by Texture::drainUploads().
graphics::primitives::scene::Texture loadTexture(const std::filesystem::path& path);

} // namespace assets::loaders
