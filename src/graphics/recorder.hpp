#pragma once

#include <gpu/buffer_readback.hpp>
#include <gpu/handle.hpp>
#include <graphics/presenter.hpp>
#include <graphics/renderer.hpp>
#include <utils/video_encoder.hpp>

#include <common.h>

#include <sokol_gfx.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace graphics {

// A Presenter that also records: it renders each frame at the video's size, shows it in the
// window (scaled, with the overlay on top), and writes it to a video file. Any graphics::Renderer
// can be recorded unchanged -- the Recorder only sees the image render() returns.
//
// Frames are a fixed 1 / fps apart in the video however long they take to render, so drive
// your simulation by frameTime() instead of the wall clock while recording, and the video
// plays back in real time even if rendering is far slower than that.
//
// Per frame (Recorder::present):
//   1. render at Config::width x height
//   2. a compute pass packs the image into a storage buffer as RGBA8 (capture_cs)
//   3. the internal Presenter shows the image and commits the frame
//   4. gpu::BufferReadback copies the storage buffer to the CPU, a few frames behind
//   5. utils::VideoEncoder (ffmpeg) encodes the frames as they arrive
// The overlay is drawn on the window only, never into the video.
//
// Vulkan only (gpu::BufferReadback); needs ffmpeg installed (checked at construction, fatal if
// missing). Construct and use on the sokol thread, like Presenter.
class Recorder {
    public:
        struct Config {
                u32 width = 1920;  // video size; must be even
                u32 height = 1080;
                u32 fps = 60;
                std::string path = "recording.mp4";
                u32 crf = 16; // x264 quality; see utils::VideoEncoder::Config
        };

        explicit Recorder(Config config);
        // Writes out every frame still in flight and closes the video (finish()).
        ~Recorder();

        Recorder(const Recorder&) = delete;
        Recorder& operator=(const Recorder&) = delete;
        Recorder(Recorder&&) = delete;
        Recorder& operator=(Recorder&&) = delete;

        // Renders one frame of the video with `renderer` and shows it in the window. Takes the
        // place of Presenter::present while recording.
        template <Renderer R>
        void present(R& renderer, const std::function<void()>& overlay = {}) {
            m_presenter.beginFrame();
            const sg_view image = renderer.render(static_cast<int>(m_config.width), static_cast<int>(m_config.height));
            captureAndPresent(image, overlay);
        }

        // Seconds between frames in the video: advance the simulation by this per present().
        f64 frameTime() const { return 1.0 / static_cast<f64>(m_config.fps); }

        // Frames presented so far; the video's length is framesPresented() * frameTime().
        u64 framesPresented() const { return m_frames_presented; }

        // Encodes every frame still being read back and closes the file. No more frames are
        // recorded after this (present() still shows them). Safe to call twice.
        void finish();
        bool finished() const { return m_finished; }

        const Config& config() const { return m_config; }

    private:
        void captureAndPresent(sg_view image, const std::function<void()>& overlay);
        void encode(std::span<const u8> rgba);

        Config m_config;
        Presenter m_presenter; // shows each frame in the window and owns the frame commit
        utils::VideoEncoder m_encoder;
        std::optional<gpu::BufferReadback> m_readback;

        sg_pipeline m_capture_pipeline = {};
        gpu::SamplerHandle m_capture_sampler;
        // One storage-buffer view per readback slot buffer, made on first use.
        std::vector<std::pair<u32, gpu::ViewHandle>> m_capture_views;

        u64 m_frames_presented = 0;
        bool m_finished = false;
};

} // namespace graphics
