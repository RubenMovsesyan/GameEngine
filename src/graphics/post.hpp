#pragma once

#include <common.h>
#include <graphics/fullscreen_pass.hpp>

#include <sokol_gfx.h>

namespace graphics {

// ACES filmic tonemap: HDR radiance * exposure -> LDR. Wraps the `tonemap` shader program.
class TonemapPass {
    public:
        explicit TonemapPass(sg_pixel_format output_format);

        // An sg pass targeting the LDR attachment must be open. Samples `hdr` through `sampler`.
        void draw(sg_view hdr, sg_sampler sampler, f32 exposure) const;

    private:
        FullscreenPass m_pass;
};

// FXAA edge anti-aliasing over an LDR image. Wraps the `fxaa` shader program. `enabled ==
// false` still runs the pass, but the shader passes the source through unchanged.
class FxaaPass {
    public:
        explicit FxaaPass(sg_pixel_format output_format);

        // An sg pass must be open. Samples `source` through `sampler`; width/height feed the
        // shader's reciprocal-frame constant.
        void draw(sg_view source, sg_sampler sampler, int width, int height, bool enabled) const;

    private:
        FullscreenPass m_pass;
};

} // namespace graphics
