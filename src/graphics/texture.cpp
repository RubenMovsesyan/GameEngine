#include <graphics/texture.hpp>
#include "rlog.h"
#include <thread>

namespace graphics {

namespace {

nvrhi::TextureHandle uploadPixels(const gpu::Device& device, std::span<const u8> pixels, u32 width, u32 height, nvrhi::Format format) {
    nvrhi::TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.isShaderResource = true;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "texture";

    usize row_pitch = static_cast<usize>(width) * nvrhi::getFormatInfo(format).bytesPerBlock;
    return device.createTexture(desc, pixels.data(), row_pitch);
}

} // namespace

Texture::Texture() : m_state(std::make_shared<State>()) {}

Texture Texture::create(const gpu::Device& device, std::span<const u8> pixels, u32 width, u32 height, nvrhi::Format format) {
    Texture texture;
    texture.m_state->texture_handle = uploadPixels(device, pixels, width, height, format);
    texture.m_state->width = width;
    texture.m_state->height = height;
    texture.m_state->ready = true;
    return texture;
}

Texture Texture::createPlaceholder(const gpu::Device& device) {
    constexpr u8 kFuchsia[4] = {255, 0, 255, 255};
    constexpr u8 kBlack[4] = {0, 0, 0, 255};
    constexpr u8 kPixels[2 * 2 * 4] = {
        kFuchsia[0], kFuchsia[1], kFuchsia[2], kFuchsia[3], kBlack[0], kBlack[1], kBlack[2], kBlack[3],
        kBlack[0],   kBlack[1],   kBlack[2],   kBlack[3],   kFuchsia[0], kFuchsia[1], kFuchsia[2], kFuchsia[3],
    };
    Texture placeholder = create(device, kPixels, 2, 2, nvrhi::Format::RGBA8_UNORM);
    placeholder.m_state->ready = false; // it's the debug checkerboard, not the requested texture
    return placeholder;
}

Texture Texture::loadAsyncThread(const gpu::Device& device, std::function<DecodedPixels()> decode, nvrhi::Format format) {
    Texture placeholder = createPlaceholder(device);
    std::shared_ptr<State> state = placeholder.m_state;
    RLOG(LL_DEBUG, "Loading Async Texture");

    std::thread([&device, state, decode = std::move(decode), format]() {
        DecodedPixels decoded = decode();
        nvrhi::TextureHandle handle = uploadPixels(device, decoded.pixels, decoded.width, decoded.height, format);
	RLOG(LL_DEBUG, "Async Texture Loaded");

        std::unique_lock lock(state->mutex);
        state->texture_handle = handle;
        state->width = decoded.width;
        state->height = decoded.height;
        state->ready = true;
    }).detach();

    return placeholder;
}

nvrhi::ITexture* Texture::texture() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->texture_handle;
}

u32 Texture::width() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->width;
}

u32 Texture::height() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->height;
}

bool Texture::isReady() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->ready;
}

} // namespace graphics
