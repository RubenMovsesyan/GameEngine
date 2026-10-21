#include <graphics/renderers/clear_renderer.hpp>

namespace graphics::renderers {

sg_view ClearRenderer::render(int width, int height) {
    if (width <= 0 || height <= 0) {
        return sg_view{};
    }
    if (width != m_width || height != m_height) {
        m_width = width;
        m_height = height;
        m_target = primitives::pass::makeColorTarget(width, height, SG_PIXELFORMAT_RGBA8, "clear renderer target");
    }

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = m_color;
    pass.attachments.colors[0] = *m_target.attachment;
    pass.label = "clear pass";
    sg_begin_pass(&pass);
    sg_end_pass();

    return *m_target.texture;
}

} // namespace graphics::renderers
