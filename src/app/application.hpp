#pragma once

#include <app/context.hpp>

#include <concepts>
#include <optional>

namespace app {

// What Application<S> needs from an app's State:
//   - explicit S(Context&)    -- runs once the GPU is up; this is the app's init. Build GPU
//                                objects, register workers, read the command line here.
//   - void frame(Context&)    -- runs once per frame on the render thread.
//   - ~S()                    -- runs after the workers are joined and before the GPU is torn
//                                down; this is the app's cleanup. Members holding GPU handles
//                                are released here for free.
// The constructor must be explicit: a Context& must not silently convert into a State.
template <typename S>
concept AppState = std::constructible_from<S, Context&> && !std::convertible_to<Context&, S> &&
                   requires(S& state, Context& context) {
                       { state.frame(context) } -> std::same_as<void>;
                   };

// Owns the window, the sokol_gfx context and the frame loop, and the app's State S for
// exactly as long as the GPU is up:
//
//   run(): window opens -> sg_setup() -> S(context) -> workers start
//          -> S::frame(context) every frame
//          -> workers stop + join -> ~S() -> sg_shutdown() -> run() returns
//
// Everything that is not State-specific lives in the Context base, which is what the State
// receives.
template <AppState S>
class Application : public Context {
    public:
        // argc/argv are only stored, for the State to parse (see Context::argv()).
        explicit Application(Config config, int argc = 0, char** argv = nullptr) : Context(config, argc, argv) {}

        // Blocks until the window closes.
        void run() {
            runLoop(
                [this](Context& context) { m_state.emplace(context); },
                [this](Context& context) { m_state->frame(context); },
                [this](Context&) { m_state.reset(); }
            );
        }

    private:
        std::optional<S> m_state;
};

} // namespace app
