#include <graphics/primitives/pass/post.hpp>

#include <Eigen/Dense>

#include "engine.glsl.h"

namespace graphics::primitives::pass {

namespace {

// SG_RANGE expands to a temporary in C++, which can't be passed by address, so name it first.
template <typename T>
void applyUniforms(int slot, const T& value) {
    const sg_range range{&value, sizeof(T)};
    sg_apply_uniforms(slot, &range);
}

} // namespace

TonemapPass::TonemapPass(sg_pixel_format output_format)
    : m_pass(FullscreenPass::create(tonemap_shader_desc(sg_query_backend()), output_format, "tonemap pipeline")) {}

void TonemapPass::draw(sg_view hdr, sg_sampler sampler, f32 exposure) const {
    sg_bindings bindings = {};
    bindings.views[VIEW_tex_source] = hdr;
    bindings.samplers[SMP_smp_source] = sampler;

    m_pass.draw(bindings, [&]() {
        const tonemap_params_t params{.exposure = Eigen::Vector4f(exposure, 0.0f, 0.0f, 0.0f)};
        applyUniforms(UB_tonemap_params, params);
    });
}

FxaaPass::FxaaPass(sg_pixel_format output_format)
    : m_pass(FullscreenPass::create(fxaa_shader_desc(sg_query_backend()), output_format, "fxaa pipeline")) {}

void FxaaPass::draw(sg_view source, sg_sampler sampler, int width, int height, bool enabled) const {
    sg_bindings bindings = {};
    bindings.views[VIEW_tex_source] = source;
    bindings.samplers[SMP_smp_source] = sampler;

    m_pass.draw(bindings, [&]() {
        const fxaa_params_t params{
            .rcp_frame = Eigen::Vector4f(
                1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height), enabled ? 1.0f : 0.0f, 0.0f
            ),
        };
        applyUniforms(UB_fxaa_params, params);
    });
}

} // namespace graphics::primitives::pass
