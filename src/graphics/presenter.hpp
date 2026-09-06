#pragma once

#include <gpu/handle.hpp>
#include <graphics/fullscreen_pass.hpp>
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
            blitAndFinish(image, overlay);
        }

    private:
        void beginFrame();
        int targetWidth() const;
        int targetHeight() const;
        void blitAndFinish(sg_view image, const std::function<void()>& overlay);

        FullscreenPass m_blit;
        gpu::SamplerHandle m_sampler;
};

} // namespace graphics
