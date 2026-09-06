#pragma once

#include <gpu/handle.hpp>

#include <sokol_gfx.h>

namespace graphics::primitives::pass {

// An offscreen render target: the image, the view it is rendered through, and the view it
// is sampled through afterwards.
struct RenderTarget {
    gpu::ImageHandle image;
    gpu::ViewHandle attachment;
    gpu::ViewHandle texture;
};

// A colour attachment plus its sampling view. width/height in pixels; `label` is copied
// into every sokol resource for debugging.
RenderTarget makeColorTarget(int width, int height, sg_pixel_format format, const char* label);

// A depth-stencil attachment (SG_PIXELFORMAT_DEPTH) that is also sampleable as a texture --
// the deferred lighting and sky passes read it back.
RenderTarget makeDepthTarget(int width, int height, const char* label);

} // namespace graphics::primitives::pass
