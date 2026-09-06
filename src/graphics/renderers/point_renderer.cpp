#include <graphics/renderers/point_renderer.hpp>

#include <common.h>

#include <Eigen/Dense>

#include <sokol_gfx.h>

// Generated shader header: its @ctype-mapped uniform structs need Eigen visible first.
#include "engine.glsl.h"

namespace graphics::renderers {

namespace {

constexpr sg_pixel_format kHdrFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kLdrFormat = SG_PIXELFORMAT_RGBA8;

// SG_RANGE expands to a temporary in C++, which can't be passed by address, so name it first.
template <typename T>
void applyUniforms(int slot, const T& value) {
    const sg_range range{&value, sizeof(T)};
    sg_apply_uniforms(slot, &range);
}

} // namespace

PointRenderer::PointRenderer() {
    sg_pipeline_desc desc = {};
    desc.shader = sg_make_shader(points_shader_desc(sg_query_backend()));
    desc.color_count = 1;
    desc.colors[0].pixel_format = kHdrFormat;

    // Additive, order-independent: no depth buffer, no sort. Every point just adds radiance.
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;

    // Offscreen HDR target has no depth attachment, so the pipeline must declare depth NONE.
    desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    desc.cull_mode = SG_CULLMODE_NONE;

    // One vertex buffer at slot 0, stepped per instance: a tightly packed PointInstance.
    desc.layout.buffers[0].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    desc.layout.attrs[ATTR_points_inst_position].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[ATTR_points_inst_position].offset = offsetof(primitives::scene::PointInstance, position);
    desc.layout.attrs[ATTR_points_inst_size].format = SG_VERTEXFORMAT_FLOAT;
    desc.layout.attrs[ATTR_points_inst_size].offset = offsetof(primitives::scene::PointInstance, size);
    desc.layout.attrs[ATTR_points_inst_color].format = SG_VERTEXFORMAT_FLOAT4;
    desc.layout.attrs[ATTR_points_inst_color].offset = offsetof(primitives::scene::PointInstance, color);
    desc.label = "point renderer pipeline";
    m_pipeline = sg_make_pipeline(&desc);

    m_tonemap.emplace(kLdrFormat);

    sg_sampler_desc sampler = {};
    sampler.min_filter = SG_FILTER_LINEAR;
    sampler.mag_filter = SG_FILTER_LINEAR;
    sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler.label = "point renderer tonemap sampler";
    m_sampler = gpu::makeSampler(sampler);
}

void PointRenderer::beginFrame(const primitives::scene::Camera& camera) {
    m_view = camera.transformationMatrix();
    m_projection = camera.projectionMatrix();
}

void PointRenderer::submit(std::span<const primitives::scene::PointInstance> points) {
    m_point_count = static_cast<u32>(points.size());
    if (points.empty()) {
        m_instances = nullptr;
        return;
    }

    const usize byte_size = points.size_bytes();

    sg_buffer_desc desc = {};
    desc.size = byte_size;
    desc.usage.vertex_buffer = true;
    desc.data = sg_range{points.data(), byte_size};
    desc.label = "point renderer instances";
    // Recreated every frame. A streaming instance buffer is roadmap 1a, out of scope here;
    // assigning through the handle destroys the previous buffer.
    m_instances = gpu::makeBuffer(desc);
}

void PointRenderer::rebuildTargets(int width, int height) {
    m_width = width;
    m_height = height;
    m_hdr = primitives::pass::makeColorTarget(width, height, kHdrFormat, "point renderer hdr");
    m_ldr = primitives::pass::makeColorTarget(width, height, kLdrFormat, "point renderer ldr");
}

sg_view PointRenderer::render(int width, int height) {
    if (width <= 0 || height <= 0) {
        return sg_view{};
    }
    if (width != m_width || height != m_height) {
        rebuildTargets(width, height);
    }

    // Additive point pass -> HDR.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
        pass.attachments.colors[0] = *m_hdr.attachment;
        pass.label = "point hdr pass";
        sg_begin_pass(&pass);

        if (m_point_count > 0 && m_instances) {
            sg_apply_pipeline(m_pipeline);

            sg_bindings bindings = {};
            bindings.vertex_buffers[0] = *m_instances;
            sg_apply_bindings(&bindings);

            points_params_t params = {};
            params.view = m_view;
            params.projection = m_projection;
            applyUniforms(UB_points_params, params);

            // 6 vertices (two triangles) per instance; the vertex shader builds the quad.
            sg_draw(0, 6, static_cast<int>(m_point_count));
        }

        sg_end_pass();
    }

    // Tonemap HDR -> LDR.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.attachments.colors[0] = *m_ldr.attachment;
        pass.label = "point tonemap pass";
        sg_begin_pass(&pass);
        m_tonemap->draw(*m_hdr.texture, *m_sampler, m_exposure);
        sg_end_pass();
    }

    return *m_ldr.texture;
}

} // namespace graphics::renderers
