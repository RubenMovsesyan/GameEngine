#include <graphics/presenter.hpp>

#include <graphics/primitives/scene/texture.hpp>

#include <Eigen/Dense>

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>

// Generated shader header: its @ctype-mapped uniform structs need Eigen visible first.
#include "engine.glsl.h"

namespace graphics {

Presenter::Presenter()
    : m_blit(primitives::pass::FullscreenPass::create(present_shader_desc(sg_query_backend()), primitives::pass::kSwapchainColorFormat, "present pipeline")) {
    sg_sampler_desc sampler = {};
    sampler.min_filter = SG_FILTER_LINEAR;
    sampler.mag_filter = SG_FILTER_LINEAR;
    sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler.label = "present sampler";
    m_sampler = gpu::makeSampler(sampler);
}

void Presenter::beginFrame() {
    // Textures decoded on worker threads are uploaded here, on the sokol thread, once per
    // frame -- before any renderer that might sample a freshly-loaded texture runs. Any
    // render path without a Presenter has to call this itself.
    primitives::scene::Texture::drainUploads();
}

int Presenter::targetWidth() const {
    return sapp_width();
}

int Presenter::targetHeight() const {
    return sapp_height();
}

void Presenter::blitAndFinish(sg_view image, const std::function<void()>& overlay) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    pass.swapchain = sglue_swapchain();
    pass.label = "present pass";
    sg_begin_pass(&pass);

    // An invalid view means the renderer had nothing to draw this frame (0x0 target); the
    // pass still opens so the overlay and commit run, but the blit is skipped.
    if (image.id != SG_INVALID_ID) {
        sg_bindings bindings = {};
        bindings.views[VIEW_tex_source] = image;
        bindings.samplers[SMP_smp_source] = *m_sampler;
        m_blit.draw(bindings);
    }

    // Window-level overlay (ImGui, etc.), composited on top inside the swapchain pass.
    if (overlay) {
        overlay();
    }

    sg_end_pass();
    sg_commit();
}

} // namespace graphics
