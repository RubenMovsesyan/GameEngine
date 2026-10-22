#pragma once

#include <common.h>

#include <cstdio>
#include <span>
#include <string>

namespace utils {

// Writes RGBA8 frames to a video file by streaming them into an ffmpeg process. No GPU
// involved: frames come from wherever the caller got them (graphics::Recorder reads them back
// from a renderer).
//
// ffmpeg must be installed: the constructor checks for it on PATH and stops the program
// (RLOG LL_FATAL) if it is missing, rather than failing on the first frame.
class VideoEncoder {
    public:
        struct Config {
                u32 width = 1920;  // must be even (H.264 with 4:2:0 chroma)
                u32 height = 1080; // must be even
                u32 fps = 60;
                std::string path = "recording.mp4"; // container from the extension (.mp4, .mkv, ...)
                // x264 constant-quality setting: 0 is lossless, ~18 is visually lossless, higher
                // is smaller and blurrier.
                u32 crf = 16;
        };

        explicit VideoEncoder(Config config);
        ~VideoEncoder();

        VideoEncoder(const VideoEncoder&) = delete;
        VideoEncoder& operator=(const VideoEncoder&) = delete;
        VideoEncoder(VideoEncoder&&) = delete;
        VideoEncoder& operator=(VideoEncoder&&) = delete;

        // One frame: width * height RGBA8 pixels, row 0 at the top. Frames are shown 1 / fps
        // apart in the video regardless of how long they took to produce.
        void write(std::span<const u8> rgba);

        // Closes ffmpeg's input and waits for it to finish writing the file. Called by the
        // destructor; safe to call twice. Returns false if ffmpeg reported an error.
        bool finish();

        u64 framesWritten() const { return m_frames; }
        const Config& config() const { return m_config; }

    private:
        Config m_config;
        std::FILE* m_pipe = nullptr;
        u64 m_frames = 0;
};

} // namespace utils
