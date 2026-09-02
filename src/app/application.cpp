#include <app/application.hpp>

#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

namespace app {

Application::Application(Config config, Callback init, Callback frame, Callback cleanup)
    : m_config(config), m_init(std::move(init)), m_frame(std::move(frame)), m_cleanup(std::move(cleanup)) {}

Application::~Application() {
    for (auto& worker : m_workers) {
        worker->join();
    }
}

void Application::initCallback(void* user_data) {
    Application& app = *static_cast<Application*>(user_data);

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    if (app.m_init) {
        app.m_init(app);
    }
}

void Application::frameCallback(void* user_data) {
    Application& app = *static_cast<Application*>(user_data);

    app.m_input.beginFrame();
    if (app.m_frame) {
        app.m_frame(app);
    }
}

void Application::eventCallback(const sapp_event* event, void* user_data) {
    Application& app = *static_cast<Application*>(user_data);

    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            // Auto-repeat would re-fire isKeyPressed for a held key.
            if (!event->key_repeat) {
                app.m_input.handleKey(event->key_code, true);
            }
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            app.m_input.handleKey(event->key_code, false);
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            app.m_input.handleMouseButton(event->mouse_button, true);
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            app.m_input.handleMouseButton(event->mouse_button, false);
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            app.m_input.handleMouseMove(event->mouse_x, event->mouse_y, event->mouse_dx, event->mouse_dy);
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            app.m_input.handleScroll(event->scroll_x, event->scroll_y);
            break;
        default:
            break;
    }
}

void Application::cleanupCallback(void* user_data) {
    Application& app = *static_cast<Application*>(user_data);

    // Release GPU-owning state while sokol is still up; sg_destroy_* asserts after shutdown.
    if (app.m_cleanup) {
        app.m_cleanup(app);
    }
    sg_shutdown();
}

void Application::run() {
    sapp_desc desc = {};
    desc.user_data = this;
    desc.init_userdata_cb = initCallback;
    desc.frame_userdata_cb = frameCallback;
    desc.event_userdata_cb = eventCallback;
    desc.cleanup_userdata_cb = cleanupCallback;
    desc.width = m_config.width;
    desc.height = m_config.height;
    desc.window_title = m_config.title;
    desc.logger.func = slog_func;

    sapp_run(&desc);
}

void Application::quit() {
    sapp_request_quit();
}

int Application::width() const {
    return sapp_width();
}

int Application::height() const {
    return sapp_height();
}

bool Application::isCursorCaptured() const {
    return sapp_mouse_locked();
}

void Application::setCursorCaptured(bool captured) {
    sapp_lock_mouse(captured);
}

} // namespace app
