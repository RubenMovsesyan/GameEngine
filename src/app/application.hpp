#pragma once

#include <app/debug_ui.hpp>
#include <app/input_state.hpp>
#include <app/worker.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct sapp_event;

namespace app {

// Owns the window and the sokol_gfx context, and drives the frame loop.
//
// sokol_gfx is single-threaded and sokol_app owns the loop, so the callbacks below all run
// on the thread that called run(). Nothing on a Worker may touch the GPU.
class Application {
    public:
        struct Config {
                int width = 1280;
                int height = 720;
                const char* title = "GameEngine";
        };

        using Callback = std::function<void(Application&)>;

        // init runs once, after the GPU is up; frame runs once per presented frame; cleanup runs
        // once before the GPU is torn down. Anything holding GPU handles must release them in
        // cleanup -- destroying them after sg_shutdown() asserts.
        Application(Config, Callback init, Callback frame, Callback cleanup = {});
        ~Application();

        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        // Blocks until the window closes.
        void run();

        // Asks for a clean shutdown; the frame callback still finishes.
        void quit();

        InputState& input() { return m_input; }
        // Constructed once init() has run (needs sg_setup() to already be up). Owned here,
        // not by Renderer -- a debug panel toggling engine-wide features isn't tied to one
        // render target.
        DebugUI& debugUI() { return *m_debug_ui; }

        // Framebuffer size in pixels. Only meaningful once init has run.
        int width() const;
        int height() const;

        bool isCursorCaptured() const;
        void setCursorCaptured(bool captured);
        void toggleCursorCapture() { setCursorCaptured(!isCursorCaptured()); }

        // UserData is inferred from work's second parameter; args construct it.
        // For CPU work only -- a Worker must never call into the GPU.
        template <typename F, typename... Args>
        void addWorker(F&& work, Args&&... args) {
            using UserData = typename detail::WorkerCallableTraits<std::decay_t<F>>::UserData;
            m_workers.push_back(std::make_unique<Worker<UserData>>(
                *this, std::function<void(Application&, UserData&)>(std::forward<F>(work)), UserData(std::forward<Args>(args)...)
            ));
        }

    private:
        static void initCallback(void* user_data);
        static void frameCallback(void* user_data);
        static void eventCallback(const sapp_event* event, void* user_data);
        static void cleanupCallback(void* user_data);

        Config m_config;
        Callback m_init;
        Callback m_frame;
        Callback m_cleanup;
        InputState m_input;
        std::optional<DebugUI> m_debug_ui;

        std::vector<std::unique_ptr<WorkerBase>> m_workers;
};

} // namespace app
