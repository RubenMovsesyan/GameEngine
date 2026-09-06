#include <graphics/fullscreen_pass.hpp>

namespace graphics {

FullscreenPass FullscreenPass::create(const sg_shader_desc* shader_desc, sg_pixel_format output_format, const char* label) {
    sg_pipeline_desc desc = {};
    desc.shader = sg_make_shader(shader_desc);
    desc.color_count = 1;
    desc.colors[0].pixel_format = output_format;
    // An offscreen target has no depth attachment, so the pipeline must declare depth NONE.
    // A swapchain-resolved pipeline (output_format left default) inherits the swapchain's
    // real depth format instead, so its depth format must stay unset for sokol to fill in.
    if (output_format != kSwapchainColorFormat) {
        desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    }
    desc.label = label;

    FullscreenPass pass;
    pass.m_pipeline = sg_make_pipeline(&desc);
    return pass;
}

void FullscreenPass::draw(const sg_bindings& bindings, const std::function<void()>& apply_uniforms) const {
    sg_apply_pipeline(m_pipeline);
    sg_apply_bindings(&bindings);
    if (apply_uniforms) {
        apply_uniforms();
    }
    sg_draw(0, 3, 1);
}

} // namespace graphics
