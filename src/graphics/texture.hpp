#pragma once

#include <gpu/handle.hpp>

#include <functional>
#include <memory>
#include <shared_mutex>
#include <span>
#include <vector>

namespace graphics {

struct DecodedPixels {
    std::vector<u8> pixels; // tightly packed RGBA8
    u32 width = 0;
    u32 height = 0;
};

// A GPU-resident 2D texture, cheap to copy/move (backed by a shared_ptr).
class Texture {
    public:
        static Texture create(std::span<const u8> pixels, u32 width, u32 height, sg_pixel_format format = SG_PIXELFORMAT_RGBA8);
        static Texture createPlaceholder(); // 2x2 fuchsia/black checkerboard, synchronous

        // Returns createPlaceholder() immediately and decodes on a detached thread. sokol_gfx
        // is single-threaded, so the decoded pixels are queued and uploaded by drainUploads().
        static Texture loadAsyncThread(std::function<DecodedPixels()> decode, sg_pixel_format format = SG_PIXELFORMAT_RGBA8);

        // Uploads everything decoded since the last call, swapping each placeholder for the
        // real image. Call once per frame from the thread that owns the sokol_gfx context.
        static void drainUploads();

        sg_image image() const;
        // Texture view, bound straight into sg_bindings.views[].
        sg_view view() const;
        u32 width() const;
        u32 height() const;
        bool isReady() const; // false while still showing the placeholder

    private:
        struct State {
            std::shared_mutex mutex;
            gpu::ImageHandle image;
            gpu::ViewHandle view;
            u32 width = 0;
            u32 height = 0;
            bool ready = false;
        };

        Texture();

        std::shared_ptr<State> m_state;
};

} // namespace graphics
