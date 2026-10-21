#include <app/context.hpp>

#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

// Last: Xlib defines macros (None, Bool, Status, ...) that would leak into anything after it.
#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <cstdint>

namespace app {

namespace {

// sokol_app has no "not resizable" option, so tell the window manager directly: minimum and
// maximum size both width x height. Keeps the hints sokol already set (centre gravity).
void lockWindowSize(int width, int height) {
#if defined(__linux__)
    Display* display = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
    const Window window = static_cast<Window>(reinterpret_cast<std::uintptr_t>(sapp_x11_get_window()));
    if (display == nullptr || window == 0) {
        return;
    }

    XSizeHints* hints = XAllocSizeHints();
    long supplied = 0;
    XGetWMNormalHints(display, window, hints, &supplied);
    hints->flags |= PMinSize | PMaxSize;
    hints->min_width = hints->max_width = width;
    hints->min_height = hints->max_height = height;
    XSetWMNormalHints(display, window, hints);
    XFree(hints);

    // The window is already mapped at whatever size the WM gave it; ask for ours back.
    XResizeWindow(display, window, static_cast<unsigned int>(width), static_cast<unsigned int>(height));
    XFlush(display);
#else
    (void)width;
    (void)height;
#endif
}

} // namespace

Context::Context(Config config, int argc, char** argv) : m_config(config), m_argc(argc), m_argv(argv) {}

Context::~Context() {
    for (auto& worker : m_workers) {
        worker->join();
    }
}

void Context::initCallback(void* user_data) {
    Context& app = *static_cast<Context*>(user_data);

    // sokol_app has created and mapped the window by the time it calls init.
    if (!app.m_config.resizable) {
        lockWindowSize(app.m_config.width, app.m_config.height);
    }

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    // Needs sg_setup() to already be up; constructed here rather than in the Context ctor
    // so debugUI() is valid by the time the State's constructor runs.
    app.m_debug_ui.emplace();

    if (app.m_init) {
        app.m_init(app);
    }

    // Workers are registered from the State's constructor; start them now that it is done.
    for (auto& worker : app.m_workers) {
        worker->run();
    }
}

void Context::frameCallback(void* user_data) {
    Context& app = *static_cast<Context*>(user_data);

    app.m_input.beginFrame();
    if (app.m_frame) {
        app.m_frame(app);
    }
}

void Context::eventCallback(const sapp_event* event, void* user_data) {
    Context& app = *static_cast<Context*>(user_data);

    // ImGui-consumed events (a click on the debug panel, etc.) never reach InputState.
    if (app.m_debug_ui && app.m_debug_ui->handleEvent(*event)) {
        return;
    }

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

void Context::cleanupCallback(void* user_data) {
    Context& app = *static_cast<Context*>(user_data);

    // Stop and join workers first: one may hold GPU resources (a CUDA context aliasing
    // Vulkan memory) that its own teardown must release while sokol/Vulkan are still up.
    app.m_workers_should_stop.store(true, std::memory_order_release);
    for (auto& worker : app.m_workers) {
        worker->join();
    }

    // Destroys the State: release GPU-owning state while sokol is still up; sg_destroy_*
    // asserts after shutdown.
    if (app.m_cleanup) {
        app.m_cleanup(app);
    }
    app.m_debug_ui.reset();
    sg_shutdown();
}

void Context::runLoop(Callback init, Callback frame, Callback cleanup) {
    m_init = std::move(init);
    m_frame = std::move(frame);
    m_cleanup = std::move(cleanup);

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

void Context::quit() {
    sapp_request_quit();
}

int Context::width() const {
    return sapp_width();
}

int Context::height() const {
    return sapp_height();
}

bool Context::isCursorCaptured() const {
    return sapp_mouse_locked();
}

void Context::setCursorCaptured(bool captured) {
    sapp_lock_mouse(captured);
}

} // namespace app
