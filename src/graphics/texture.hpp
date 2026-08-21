#pragma once

#include <gpu/device.hpp>

#include <span>

namespace graphics {

// A GPU-resident 2D texture built from already-decoded pixels.
class Texture {
    public:
        static Texture create(const gpu::Device&, std::span<const u8> pixels, u32 width, u32 height,
                               nvrhi::Format format = nvrhi::Format::RGBA8_UNORM);

        nvrhi::ITexture* texture() const { return m_texture; }
        u32 width() const { return m_width; }
        u32 height() const { return m_height; }

    private:
        Texture() = default;

        nvrhi::TextureHandle m_texture;
        u32 m_width = 0;
        u32 m_height = 0;
};

} // namespace graphics
