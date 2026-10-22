#define RLOG_IMPLEMENTATION
#include <rlog.h>

#include <app/application.hpp>
#include <app/cli_parser.hpp>
#include <graphics/recorder.hpp>
#include <graphics/renderers/clear_renderer.hpp>

#include <sokol_gfx.h>

#include <optional>
#include <string>

// Exercises graphics::Recorder: records a known sequence -- frame k is a solid colour that
// steps through red, green and blue levels -- to a video, then quits. Decode the video to
// check frame count and colours (the expected colour of frame k is frameColor(k)).
//
//   ./run recorder_test [--out path.mp4] [--frames N] [--fps F] [--width W] [--height H]

namespace {

// Frame k's colour: each channel steps by a different amount so frames are easy to tell apart.
sg_color frameColor(u64 k) {
    return sg_color{
        static_cast<float>((k * 37) % 256) / 255.0f,
        static_cast<float>((k * 91) % 256) / 255.0f,
        static_cast<float>((k * 151) % 256) / 255.0f,
        1.0f,
    };
}

struct Sandbox {
        graphics::renderers::ClearRenderer renderer;
        std::optional<graphics::Recorder> recorder;
        u64 frames = 60;

        explicit Sandbox(app::Context& app) {
            app::CliParser cli{app.argc(), app.argv()};
            cli.defineArg<std::string>("out");
            cli.defineArg<u32>("frames");
            cli.defineArg<u32>("fps");
            cli.defineArg<u32>("width");
            cli.defineArg<u32>("height");
            cli.parse();

            graphics::Recorder::Config config{.width = 640, .height = 360, .fps = 30, .path = ".build/recorder_test.mp4"};
            if (auto v = cli.arg<std::string>("out")) config.path = *v;
            if (auto v = cli.arg<u32>("fps"); v && *v > 0) config.fps = *v;
            if (auto v = cli.arg<u32>("width"); v && *v > 0) config.width = *v;
            if (auto v = cli.arg<u32>("height"); v && *v > 0) config.height = *v;
            if (auto v = cli.arg<u32>("frames"); v && *v > 0) frames = *v;
            recorder.emplace(config);
        }

        void frame(app::Context& app) {
            if (recorder->finished()) {
                app.quit();
                return;
            }
            renderer.setColor(frameColor(recorder->framesPresented()));
            recorder->present(renderer);
            if (recorder->framesPresented() == frames) {
                recorder->finish();
                RLOG(LL_INFO, "recorder_test: recorded %llu frames to %s", static_cast<unsigned long long>(frames), recorder->config().path.c_str());
                app.quit();
            }
        }
};

} // namespace

int main(int argc, char** argv) {
    initLog(1024 * 1024);
    app::Application<Sandbox> app{{.width = 960, .height = 540, .title = "recorder_test"}, argc, argv};
    app.run();
    return 0;
}
