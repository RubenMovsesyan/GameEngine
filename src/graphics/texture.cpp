#include <graphics/texture.hpp>

namespace graphics {

Texture Texture::create(const gpu::Device& device, std::span<const u8> pixels, u32 width, u32 height, nvrhi::Format format) {
    nvrhi::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.isShaderResource = true;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "texture";

    usize row_pitch = static_cast<usize>(width) * nvrhi::getFormatInfo(format).bytesPerBlock;

    Texture texture;
    texture.m_texture = device.createTexture(desc, pixels.data(), row_pitch);
    texture.m_width = width;
    texture.m_height = height;
    return texture;
}

} // namespace graphics
