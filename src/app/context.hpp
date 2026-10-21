#pragma once

#include <app/debug_ui.hpp>
#include <app/input_state.hpp>
#include <app/worker.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct sapp_event;

namespace app {

// Everything an app can reach while it runs: the window, input, the debug UI, workers, and
// the command line. The non-template half of Application<S> -- the State, workers and
// helpers all take a Context&, so none of them need to know the State's type.
//
// sokol_gfx is single-threaded and sokol_app owns the loop, so the State's constructor,
// frame() and destructor all run on the thread that called run() -- the ONLY thread that
// renders. A Worker runs off that thread and must never call sg_* (see addWorker); it may
// drive CUDA / other GPU compute.
class Context {
    public:
        struct Config {
                int width = 1280;
                int height = 720;
                const char* title = "GameEngine";
        };

        ~Context();

        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        // Asks for a clean shutdown; the current frame still finishes.
        void quit();

        InputState& input() { return m_input; }
        // Constructed before the State (needs sg_setup() to already be up). Owned here, not
        // by a renderer -- a debug panel toggling engine-wide features isn't tied to one
        // render target.
        DebugUI& debugUI() { return *m_debug_ui; }

        // The command line Application was given, for the State to parse with CliParser.
        // argc is 0 and argv null when none was passed.
        int argc() const { return m_argc; }
        char** argv() const { return m_argv; }

        // Framebuffer size in pixels. Only meaningful once the State exists.
        int width() const;
        int height() const;

        bool isCursorCaptured() const;
        void setCursorCaptured(bool captured);
        void toggleCursorCapture() { setCursorCaptured(!isCursorCaptured()); }

        // Registers `work` to run on its own thread. Call from the State's constructor; every
        // worker starts once the constructor returns and, on shutdown, workerShouldStop()
        // goes true and they are all joined before the State is destroyed (so a worker's own
        // teardown finishes while sokol/Vulkan are still up).
        //
        // `work` has signature void(Context&, UserData&). It must never call sg_* --
        // rendering stays on the frame thread -- but driving CUDA or another GPU compute API
        // from here is fine (see gpu::SharedInstanceBuffer). UserData is inferred from the
        // second parameter; args construct it.
        template <typename F, typename... Args>
        void addWorker(F&& work, Args&&... args) {
            using UserData = typename detail::WorkerCallableTraits<std::decay_t<F>>::UserData;
            m_workers.push_back(std::make_unique<Worker<UserData>>(
                *this, std::function<void(Context&, UserData&)>(std::forward<F>(work)), UserData(std::forward<Args>(args)...)
            ));
        }

        // A worker's loop should poll this and exit when it returns true.
        bool workerShouldStop() const { return m_workers_should_stop.load(std::memory_order_acquire); }

    protected:
        using Callback = std::function<void(Context&)>;

        // Only Application<S> builds a Context.
        Context(Config config, int argc, char** argv);

        // Blocks until the window closes. `init` runs once after the GPU is up, `frame` once
        // per presented frame, `cleanup` once after the workers are joined and before the GPU
        // is torn down.
        void runLoop(Callback init, Callback frame, Callback cleanup);

    private:
        static void initCallback(void* user_data);
        static void frameCallback(void* user_data);
        static void eventCallback(const sapp_event* event, void* user_data);
        static void cleanupCallback(void* user_data);

        Config m_config;
        int m_argc;
        char** m_argv;
        Callback m_init;
        Callback m_frame;
        Callback m_cleanup;
        InputState m_input;
        std::optional<DebugUI> m_debug_ui;

        std::vector<std::unique_ptr<WorkerBase>> m_workers;
        std::atomic<bool> m_workers_should_stop{false};
};

} // namespace app
