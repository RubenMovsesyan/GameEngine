#pragma once

#include <gpu/handle.hpp>
#include <graphics/primitives/pass/post.hpp>
#include <graphics/primitives/pass/render_target.hpp>
#include <graphics/primitives/scene/camera.hpp>
#include <graphics/primitives/scene/point.hpp>
#include <graphics/renderer.hpp>

#include <Eigen/Dense>

#include <optional>
#include <span>
#include <utility>

namespace graphics::renderers {

// An implementation of the graphics::Renderer concept for self-luminous points -- stars,
// sparks, particles. No G-buffer, no lighting, no shadows: every point emits, nothing is
// lit. Points are drawn as instanced view-space billboards with additive blending into an
// HDR target, which is then tonemapped to an LDR target for the Presenter to blit.
//
// Wiring only, the first dependency of the galaxy simulator (roadmap item 1a).
// Deliberately absent for now, and tracked in roadmap/galaxy-simulation.md: bloom,
// auto-exposure, a blackbody colour ramp, a streaming instance buffer (the buffer is
// recreated every submit()), and spatial culling.
//
// sokol_gfx is single-threaded: call every method from the Application frame callback.
class PointRenderer {
    public:
        PointRenderer();

        // Stashes the view and projection used to expand the billboards. Call once per
        // frame, before submit() / render().
        void beginFrame(const primitives::scene::Camera& camera);

        // The frame's points. Shaped for this renderer the way DeferredRenderer::submit is
        // shaped for meshes -- the Renderer concept only fixes render(). The span just has
        // to outlive the call; its contents are uploaded here.
        void submit(std::span<const primitives::scene::PointInstance> points);

        // Draw an externally-owned instance buffer as-is: no upload, no ownership. The
        // buffer must hold `count` tightly-packed PointInstance records and stay alive
        // until the next render(). Interop test hook for the CUDA-written shared buffer
        // (see sandbox/cuda_points_test) -- not the roadmap-1 first-class GPU-resident path.
        void adoptInstances(gpu::BufferHandle buffer, u32 count) {
            m_instances = std::move(buffer);
            m_point_count = count;
        }

        // Brightness multiplier applied before the ACES tonemap curve. Default 1.
        void setExposure(f32 exposure) { m_exposure = exposure; }

        // Records the additive point pass and the tonemap pass at width x height pixels and
        // returns a view of the final LDR image. Does not call sg_commit(); the returned
        // view stays valid until the next render(). See the graphics::Renderer concept.
        sg_view render(int width, int height);

    private:
        void rebuildTargets(int width, int height);

        int m_width = 0;
        int m_height = 0;

        primitives::pass::RenderTarget m_hdr; // additive point accumulation (RGBA16F)
        primitives::pass::RenderTarget m_ldr; // tonemapped output; render() returns this view

        sg_pipeline m_pipeline = {};
        gpu::BufferHandle m_instances; // per-instance PointInstance data; recreated each submit()
        u32 m_point_count = 0;

        std::optional<primitives::pass::TonemapPass> m_tonemap; // needs an output format, so emplaced in the ctor
        gpu::SamplerHandle m_sampler;                            // linear clamp, for the tonemap's HDR read

        Eigen::Matrix4f m_view = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f m_projection = Eigen::Matrix4f::Identity();
        f32 m_exposure = 1.0f;
};

static_assert(graphics::Renderer<PointRenderer>);

} // namespace graphics::renderers
