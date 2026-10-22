#pragma once

#include <gpu/handle.hpp>
#include <graphics/primitives/pass/fullscreen_pass.hpp>
#include <graphics/renderer.hpp>

#include <sokol_gfx.h>

#include <functional>

namespace graphics {

// The whole "graphics engine" seam: takes whatever a Renderer produced this frame and puts
// it on screen. It owns the swapchain pass, the fullscreen blit, the once-per-frame texture
// upload drain, the app overlay hook, and the frame commit -- nothing else. Swap the
// Renderer for a different implementation and this class does not change.
//
// sokol_gfx is single-threaded: call present() from the Application frame callback.
class Presenter {
    public:
        Presenter();

        // Drain pending texture uploads, ask the renderer for this frame's image at the
        // current window size, blit it to the swapchain, run `overlay` (ImGui, etc.) on top
        // inside that same pass, then commit. `overlay` is opaque -- the Presenter never
        // knows what draws into it.
        template <Renderer R>
        void present(R& renderer, const std::function<void()>& overlay = {}) {
            beginFrame();
            const sg_view image = renderer.render(targetWidth(), targetHeight());
            presentImage(image, overlay);
        }

        // The two halves of present(), for a caller that renders the image itself -- e.g.
        // graphics::Recorder, which renders at the video's size and captures the image before
        // showing it. Call beginFrame() before rendering (it drains texture uploads), then
        // presentImage() with the result; the image is scaled to the window.
        void beginFrame();
        void presentImage(sg_view image, const std::function<void()>& overlay = {});

    private:
        int targetWidth() const;
        int targetHeight() const;

        primitives::pass::FullscreenPass m_blit;
        gpu::SamplerHandle m_sampler;
};

} // namespace graphics
