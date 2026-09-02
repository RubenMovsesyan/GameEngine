#include <app/debug_ui.hpp>

#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_imgui.h>

namespace app {

DebugUI::DebugUI() {
    simgui_desc_t desc = {};
    simgui_setup(&desc);
}

DebugUI::~DebugUI() {
    simgui_shutdown();
}

bool DebugUI::handleEvent(const sapp_event& event) {
    // Always fed, regardless of m_visible: with nothing drawn there is nothing for ImGui
    // to hover or focus, so it naturally never reports an event as consumed while hidden --
    // and always feeding keeps its input state (deltas, hover) correct for the moment the
    // panel is shown again, rather than resyncing from a blank state on every open.
    return simgui_handle_event(&event);
}

void DebugUI::draw(int width, int height, double delta_time, float dpi_scale, const std::function<void()>& panels) {
    if (!m_visible) {
        return;
    }

    simgui_frame_desc_t frame_desc = {};
    frame_desc.width = width;
    frame_desc.height = height;
    frame_desc.delta_time = delta_time;
    frame_desc.dpi_scale = dpi_scale;
    simgui_new_frame(&frame_desc);

    if (panels) {
        panels();
    }

    simgui_render();
}

} // namespace app
