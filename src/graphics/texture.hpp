#pragma once

#include <gpu/device.hpp>

#include <functional>
#include <memory>
#include <mutex>
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
        static Texture create(const gpu::Device&, std::span<const u8> pixels, u32 width, u32 height,
                               nvrhi::Format format = nvrhi::Format::RGBA8_UNORM);
        static Texture createPlaceholder(const gpu::Device&); // 2x2 fuchsia/black checkerboard, synchronous

        // Returns createPlaceholder() immediately; the real load happens on a detached thread.
        static Texture loadAsyncThread(const gpu::Device&, std::function<DecodedPixels()> decode,
                                        nvrhi::Format format = nvrhi::Format::RGBA8_UNORM);

        nvrhi::ITexture* texture() const;
        u32 width() const;
        u32 height() const;
        bool isReady() const; // false while still showing the placeholder

    private:
        struct State {
            std::shared_mutex mutex;
            nvrhi::TextureHandle texture_handle;
            u32 width = 0;
            u32 height = 0;
            bool ready = false;
        };

        Texture();

        std::shared_ptr<State> m_state;
};

} // namespace graphics
