#pragma once

#include <graphics/primitives/pass/render_target.hpp>
#include <graphics/renderer.hpp>

#include <sokol_gfx.h>

namespace graphics::renderers {

// The smallest implementation of the graphics::Renderer concept: one pass that clears an
// LDR target to a solid colour, nothing drawn. For bring-up -- proving a window, the GPU
// context and the Presenter are all wired -- and as the template for a new renderer.
//
// sokol_gfx is single-threaded: call every method from the Application frame callback.
class ClearRenderer {
    public:
        // `color` is RGBA in [0, 1], written as-is to an RGBA8 target.
        explicit ClearRenderer(sg_color color = {0.0f, 0.0f, 0.0f, 1.0f}) : m_color(color) {}

        void setColor(sg_color color) { m_color = color; }

        // Records the clear pass at width x height pixels and returns a view of the cleared
        // image. Does not call sg_commit(); the view stays valid until the next render().
        sg_view render(int width, int height);

    private:
        int m_width = 0;
        int m_height = 0;
        sg_color m_color;
        primitives::pass::RenderTarget m_target;
};

static_assert(graphics::Renderer<ClearRenderer>);

} // namespace graphics::renderers
