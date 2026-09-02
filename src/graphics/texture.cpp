#include <graphics/texture.hpp>

#include <rlog.h>

#include <mutex>
#include <thread>

namespace graphics {

namespace {

struct Uploaded {
    gpu::ImageHandle image;
    gpu::ViewHandle view;
};

Uploaded uploadPixels(std::span<const u8> pixels, u32 width, u32 height, sg_pixel_format format) {
    sg_image_desc desc = {};
    desc.width = static_cast<int>(width);
    desc.height = static_cast<int>(height);
    desc.pixel_format = format;
    desc.data.mip_levels[0] = sg_range{pixels.data(), pixels.size_bytes()};
    desc.label = "texture";

    Uploaded uploaded;
    uploaded.image = gpu::makeImage(desc);

    sg_view_desc view_desc = {};
    view_desc.texture.image = *uploaded.image;
    view_desc.label = "texture";
    uploaded.view = gpu::makeView(view_desc);
    return uploaded;
}

// Decoded on a worker thread, uploaded on the sokol_gfx thread by drainUploads().
struct PendingUpload {
    std::shared_ptr<void> state; // type-erased Texture::State; cast back in drainUploads
    DecodedPixels decoded;
    sg_pixel_format format;
};

std::mutex g_pending_mutex;
std::vector<PendingUpload> g_pending;

} // namespace

Texture::Texture() : m_state(std::make_shared<State>()) {}

Texture Texture::create(std::span<const u8> pixels, u32 width, u32 height, sg_pixel_format format) {
    Uploaded uploaded = uploadPixels(pixels, width, height, format);

    Texture texture;
    texture.m_state->image = std::move(uploaded.image);
    texture.m_state->view = std::move(uploaded.view);
    texture.m_state->width = width;
    texture.m_state->height = height;
    texture.m_state->ready = true;
    return texture;
}

Texture Texture::createPlaceholder() {
    constexpr u8 kFuchsia[4] = {255, 0, 255, 255};
    constexpr u8 kBlack[4] = {0, 0, 0, 255};
    constexpr u8 kPixels[2 * 2 * 4] = {
        kFuchsia[0], kFuchsia[1], kFuchsia[2], kFuchsia[3], kBlack[0],   kBlack[1],   kBlack[2],   kBlack[3],
        kBlack[0],   kBlack[1],   kBlack[2],   kBlack[3],   kFuchsia[0], kFuchsia[1], kFuchsia[2], kFuchsia[3],
    };
    Texture placeholder = create(kPixels, 2, 2, SG_PIXELFORMAT_RGBA8);
    placeholder.m_state->ready = false; // it's the debug checkerboard, not the requested texture
    return placeholder;
}

Texture Texture::loadAsyncThread(std::function<DecodedPixels()> decode, sg_pixel_format format) {
    Texture placeholder = createPlaceholder();
    std::shared_ptr<State> state = placeholder.m_state;
    RLOG(LL_DEBUG, "Loading Async Texture");

    std::thread([state, decode = std::move(decode), format]() {
        DecodedPixels decoded = decode();
        RLOG(LL_DEBUG, "Async Texture Decoded");

        std::lock_guard lock(g_pending_mutex);
        g_pending.push_back(PendingUpload{.state = state, .decoded = std::move(decoded), .format = format});
    }).detach();

    return placeholder;
}

void Texture::drainUploads() {
    std::vector<PendingUpload> ready;
    {
        std::lock_guard lock(g_pending_mutex);
        ready.swap(g_pending);
    }

    for (PendingUpload& upload : ready) {
        Uploaded uploaded = uploadPixels(upload.decoded.pixels, upload.decoded.width, upload.decoded.height, upload.format);

        State& state = *static_cast<State*>(upload.state.get());
        std::unique_lock lock(state.mutex);
        state.image = std::move(uploaded.image);
        state.view = std::move(uploaded.view);
        state.width = upload.decoded.width;
        state.height = upload.decoded.height;
        state.ready = true;
    }
}

sg_image Texture::image() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->image ? *m_state->image : sg_image{};
}

sg_view Texture::view() const {
    std::shared_lock lock(m_state->mutex);
    return m_state->view ? *m_state->view : sg_view{};
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
