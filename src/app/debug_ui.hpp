#pragma once

#include <functional>

struct sapp_event;

namespace app {

// Dear ImGui, wired to the window (Application), not to any particular Renderer target --
// a debug panel about "toggle engine-wide debug features" isn't tied to one render target,
// and Renderer may draw to more than the swapchain in the future.
//
// Off by default. Every method below no-ops while hidden: handleEvent() still forwards to
// ImGui (see the .cpp for why that's safe), but draw() returns before calling `panels`, so
// nothing constructs an ImGui frame or touches ImGui:: at all unless the panel is visible.
class DebugUI {
    public:
        DebugUI();
        ~DebugUI();

        DebugUI(DebugUI&&) = delete;
        DebugUI& operator=(DebugUI&&) = delete;
        DebugUI(const DebugUI&) = delete;
        DebugUI& operator=(const DebugUI&) = delete;

        void setVisible(bool visible) { m_visible = visible; }
        bool isVisible() const { return m_visible; }

        // Feeds one sokol_app event to ImGui. Returns true if ImGui consumed it -- the
        // caller should skip its own input handling for that event when this is true.
        bool handleEvent(const sapp_event&);

        // No-op while hidden. Call inside an active sg pass targeting the swapchain (see
        // Renderer::endFrame's overlay parameter). `panels` is where the caller's own
        // ImGui::Begin/.../End calls live -- it runs between ImGui's new-frame and render.
        void draw(int width, int height, double delta_time, float dpi_scale, const std::function<void()>& panels);

    private:
        bool m_visible = false;
};

} // namespace app
