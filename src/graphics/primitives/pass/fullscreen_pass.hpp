#pragma once

#include <sokol_gfx.h>

#include <functional>

namespace graphics::primitives::pass {

// Pass this as FullscreenPass::create's output_format to leave the pipeline's colour format
// unset, so sokol resolves it against the swapchain at pipeline-creation time. Distinct from
// SG_PIXELFORMAT_NONE, which means "no attachment".
inline constexpr sg_pixel_format kSwapchainColorFormat = static_cast<sg_pixel_format>(0);

// The pipeline for one fullscreen-triangle fragment shader: no vertex buffer, no depth, a
// single colour attachment. Shared by every screen-space pass -- deferred lighting, sky,
// tonemap, FXAA, the shadow-map debug blit, and the Presenter's swapchain blit.
//
// Deliberately does NOT own the sg pass: a pass legitimately holds more than one draw (the
// final deferred pass runs FXAA, then the debug blit, then DebugDraw, then the app overlay).
// Open the pass yourself, call draw() one or more times, close the pass yourself.
class FullscreenPass {
    public:
        FullscreenPass() = default;

        // shader_desc comes from a generated <program>_shader_desc(sg_query_backend()); the
        // program's vertex stage must be the shared fullscreen_vs (index-only, no buffer).
        // output_format left kSwapchainColorFormat is resolved against the swapchain; pass an
        // explicit format for an offscreen attachment.
        static FullscreenPass create(const sg_shader_desc* shader_desc, sg_pixel_format output_format, const char* label);

        // An sg pass must already be open. Binds the pipeline and `bindings`, runs
        // `apply_uniforms` if set (call sg_apply_uniforms inside it), then draws the triangle.
        void draw(const sg_bindings& bindings, const std::function<void()>& apply_uniforms = {}) const;

    private:
        sg_pipeline m_pipeline{};
};

} // namespace graphics::primitives::pass
