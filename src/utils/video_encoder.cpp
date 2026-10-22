#include <utils/video_encoder.hpp>

#include <rlog.h>

#include <cstdlib>
#include <string>
#include <utility>

namespace utils {

namespace {

// Single-quotes `text` for /bin/sh, so paths with spaces etc. survive popen().
std::string shellQuote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

} // namespace

VideoEncoder::VideoEncoder(Config config) : m_config(std::move(config)) {
    if (std::system("command -v ffmpeg > /dev/null 2>&1") != 0) {
        RLOG(LL_FATAL, "VideoEncoder: ffmpeg is not installed (or not on PATH); it is needed to record video");
    }
    if (m_config.width % 2 != 0 || m_config.height % 2 != 0 || m_config.width == 0 || m_config.height == 0) {
        RLOG(LL_FATAL, "VideoEncoder: width and height must be even and non-zero (got %ux%u)", m_config.width, m_config.height);
    }
    if (m_config.fps == 0) {
        RLOG(LL_FATAL, "VideoEncoder: fps must be > 0");
    }

    // Raw RGBA frames on stdin -> H.264 (x264, constant quality) in yuv420p, which every player
    // understands. -y overwrites; only errors are printed.
    const std::string command = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s " + std::to_string(m_config.width) + "x" +
                                std::to_string(m_config.height) + " -r " + std::to_string(m_config.fps) +
                                " -i - -c:v libx264 -preset slow -crf " + std::to_string(m_config.crf) +
                                " -pix_fmt yuv420p -movflags +faststart " + shellQuote(m_config.path);
    m_pipe = popen(command.c_str(), "w");
    if (m_pipe == nullptr) {
        RLOG(LL_FATAL, "VideoEncoder: could not start ffmpeg");
    }
    RLOG(LL_INFO, "VideoEncoder: recording %ux%u at %u fps to %s", m_config.width, m_config.height, m_config.fps, m_config.path.c_str());
}

VideoEncoder::~VideoEncoder() {
    finish();
}

void VideoEncoder::write(std::span<const u8> rgba) {
    if (m_pipe == nullptr) {
        return;
    }
    const usize expected = static_cast<usize>(m_config.width) * m_config.height * 4;
    if (rgba.size() < expected) {
        RLOG(LL_ERROR, "VideoEncoder::write: frame is %zu bytes, expected %zu", rgba.size(), expected);
        return;
    }
    if (std::fwrite(rgba.data(), 1, expected, m_pipe) != expected) {
        RLOG(LL_ERROR, "VideoEncoder::write: ffmpeg stopped accepting frames");
        return;
    }
    m_frames++;
}

bool VideoEncoder::finish() {
    if (m_pipe == nullptr) {
        return true;
    }
    const int status = pclose(m_pipe);
    m_pipe = nullptr;
    if (status != 0) {
        RLOG(LL_ERROR, "VideoEncoder: ffmpeg exited with status %d; %s may be incomplete", status, m_config.path.c_str());
        return false;
    }
    RLOG(LL_INFO, "VideoEncoder: wrote %llu frames (%.2f s) to %s", static_cast<unsigned long long>(m_frames),
         static_cast<double>(m_frames) / m_config.fps, m_config.path.c_str());
    return true;
}

} // namespace utils
