#include <graphics/recorder.hpp>

#include <rlog.h>

#include <Eigen/Dense>

#include <sokol_gfx.h>

// Generated shader header: its @ctype-mapped uniform structs need Eigen visible first.
#include "engine.glsl.h"

namespace graphics {

namespace {

// Frames being read back at once: the CPU reads frame k while the GPU renders k+1 and k+2.
constexpr u32 kReadbackSlots = 3;
constexpr u32 kGroupSize = 8; // capture_cs local_size_x / _y

} // namespace

Recorder::Recorder(Config config)
    : m_config(std::move(config)),
      m_encoder(utils::VideoEncoder::Config{.width = m_config.width, .height = m_config.height, .fps = m_config.fps, .path = m_config.path, .crf = m_config.crf}) {
    m_readback = gpu::BufferReadback::create(static_cast<usize>(m_config.width) * m_config.height * 4, kReadbackSlots, "recorder capture");
    if (!m_readback) {
        RLOG(LL_FATAL, "Recorder: can't read frames back from the GPU on this backend (Vulkan only)");
    }

    sg_pipeline_desc pipeline = {};
    pipeline.compute = true;
    pipeline.shader = sg_make_shader(capture_shader_desc(sg_query_backend()));
    pipeline.label = "recorder capture pipeline";
    m_capture_pipeline = sg_make_pipeline(&pipeline);

    // texelFetch ignores filtering; nearest keeps the sampler valid for any source format.
    sg_sampler_desc sampler = {};
    sampler.min_filter = SG_FILTER_NEAREST;
    sampler.mag_filter = SG_FILTER_NEAREST;
    sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler.label = "recorder capture sampler";
    m_capture_sampler = gpu::makeSampler(sampler);
}

Recorder::~Recorder() {
    finish();
    if (sg_isvalid() && m_capture_pipeline.id != SG_INVALID_ID) {
        const sg_shader shader = sg_query_pipeline_desc(m_capture_pipeline).shader;
        sg_destroy_pipeline(m_capture_pipeline);
        sg_destroy_shader(shader);
    }
}

void Recorder::encode(std::span<const u8> rgba) {
    m_encoder.write(rgba);
}

void Recorder::captureAndPresent(sg_view image, const std::function<void()>& overlay) {
    const bool recording = !m_finished && image.id != SG_INVALID_ID;
    if (recording) {
        // The slot this frame writes; if the ring is full, the oldest frame is encoded first.
        const sg_buffer target = m_readback->acquire([this](std::span<const u8> rgba) { encode(rgba); });

        gpu::ViewHandle view;
        for (const auto& [buffer_id, existing] : m_capture_views) {
            if (buffer_id == target.id) {
                view = existing;
            }
        }
        if (!view) {
            sg_view_desc desc = {};
            desc.storage_buffer.buffer = target;
            desc.label = "recorder capture buffer view";
            view = gpu::makeView(desc);
            m_capture_views.emplace_back(target.id, view);
        }

        sg_pass pass = {};
        pass.compute = true;
        pass.label = "recorder capture pass";
        sg_begin_pass(&pass);
        sg_apply_pipeline(m_capture_pipeline);

        sg_bindings bindings = {};
        bindings.views[VIEW_capture_source] = image;
        bindings.views[VIEW_capture_pixels] = *view;
        bindings.samplers[SMP_capture_sampler] = *m_capture_sampler;
        sg_apply_bindings(&bindings);

        capture_params_t params = {};
        params.capture_size = Eigen::Vector4f(static_cast<f32>(m_config.width), static_cast<f32>(m_config.height), 0.0f, 0.0f);
        const sg_range range{&params, sizeof(params)};
        sg_apply_uniforms(UB_capture_params, &range);

        sg_dispatch(static_cast<int>((m_config.width + kGroupSize - 1) / kGroupSize), static_cast<int>((m_config.height + kGroupSize - 1) / kGroupSize), 1);
        sg_end_pass();
    }

    // Shows the frame and commits it -- the capture pass above goes out in the same submit.
    m_presenter.presentImage(image, overlay);

    if (recording) {
        // After the commit: queue this frame's copy to the CPU behind it.
        m_readback->submit();
        m_frames_presented++;
    }
}

void Recorder::finish() {
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (m_readback) {
        m_readback->flush([this](std::span<const u8> rgba) { encode(rgba); });
    }
    m_encoder.finish();
}

} // namespace graphics
