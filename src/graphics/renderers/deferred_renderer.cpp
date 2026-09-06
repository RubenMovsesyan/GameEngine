#include <graphics/renderers/deferred_renderer.hpp>

#include <graphics/primitives/scene/frustum.hpp>
#include <graphics/primitives/scene/texture.hpp>

#include <sokol_gfx.h>

#include "engine.glsl.h"

namespace graphics::renderers {

namespace {

constexpr sg_pixel_format kAlbedoFormat = SG_PIXELFORMAT_SRGB8A8;
constexpr sg_pixel_format kNormalFormat = SG_PIXELFORMAT_RG16F;
constexpr sg_pixel_format kMaterialFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;
constexpr sg_pixel_format kHdrFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kLdrFormat = SG_PIXELFORMAT_RGBA8;

// Vertex layout shared by the G-buffer and shadow passes: interleaved Vertex at buffer 0.
void setMeshLayout(sg_vertex_layout_state& layout) {
    layout.buffers[0].stride = sizeof(primitives::scene::Vertex);
    layout.attrs[ATTR_gbuffer_position].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_gbuffer_position].offset = offsetof(primitives::scene::Vertex, position);
    layout.attrs[ATTR_gbuffer_position].buffer_index = 0;
    layout.attrs[ATTR_gbuffer_normal].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_gbuffer_normal].offset = offsetof(primitives::scene::Vertex, normal);
    layout.attrs[ATTR_gbuffer_normal].buffer_index = 0;
    layout.attrs[ATTR_gbuffer_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
    layout.attrs[ATTR_gbuffer_texcoord].offset = offsetof(primitives::scene::Vertex, uv);
    layout.attrs[ATTR_gbuffer_texcoord].buffer_index = 0;
}

// The shadow pass reads the same interleaved vertex buffer but only consumes position;
// declaring the unused attributes makes Vulkan complain that they go unread.
void setShadowMeshLayout(sg_vertex_layout_state& layout) {
    layout.buffers[0].stride = sizeof(primitives::scene::Vertex);
    layout.attrs[ATTR_shadow_depth_position].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_shadow_depth_position].offset = offsetof(primitives::scene::Vertex, position);
    layout.attrs[ATTR_shadow_depth_position].buffer_index = 0;
}

// Per-instance InstanceData at buffer 1: a mat4 is four consecutive FLOAT4 attributes.
void setInstanceLayout(sg_vertex_layout_state& layout, int first_location, bool include_normal_matrix) {
    layout.buffers[1].stride = sizeof(primitives::scene::InstanceData);
    layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;

    for (int i = 0; i < 4; i++) {
        const int slot = first_location + i;
        layout.attrs[slot].format = SG_VERTEXFORMAT_FLOAT4;
        layout.attrs[slot].offset = static_cast<int>(offsetof(primitives::scene::InstanceData, transform) + i * sizeof(Eigen::Vector4f));
        layout.attrs[slot].buffer_index = 1;
    }

    if (!include_normal_matrix) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        const int slot = ATTR_gbuffer_instanced_inst_normal_matrix + i;
        layout.attrs[slot].format = SG_VERTEXFORMAT_FLOAT4;
        layout.attrs[slot].offset = static_cast<int>(offsetof(primitives::scene::InstanceData, normal_matrix) + i * sizeof(Eigen::Vector4f));
        layout.attrs[slot].buffer_index = 1;
    }
}

Eigen::Vector4f toVec4(const Eigen::Vector3f& v, f32 w) {
    return Eigen::Vector4f(v.x(), v.y(), v.z(), w);
}

// SG_RANGE expands to a temporary in C++, which can't be passed by address, so name it first.
template <typename T>
void applyUniforms(int slot, const T& value) {
    const sg_range range{&value, sizeof(T)};
    sg_apply_uniforms(slot, &range);
}

} // namespace

DeferredRenderer::DeferredRenderer() {
    const primitives::scene::Skybox defaults{};
    setSkybox(defaults);

    sg_sampler_desc material_sampler = {};
    material_sampler.min_filter = SG_FILTER_LINEAR;
    material_sampler.mag_filter = SG_FILTER_LINEAR;
    material_sampler.mipmap_filter = SG_FILTER_LINEAR;
    material_sampler.label = "material sampler";
    m_material_sampler = gpu::makeSampler(material_sampler);

    sg_sampler_desc gbuffer_sampler = {};
    gbuffer_sampler.min_filter = SG_FILTER_NEAREST;
    gbuffer_sampler.mag_filter = SG_FILTER_NEAREST;
    gbuffer_sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    gbuffer_sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    gbuffer_sampler.label = "gbuffer sampler";
    m_gbuffer_sampler = gpu::makeSampler(gbuffer_sampler);

    sg_sampler_desc shadow_sampler = {};
    shadow_sampler.min_filter = SG_FILTER_LINEAR;
    shadow_sampler.mag_filter = SG_FILTER_LINEAR;
    shadow_sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    shadow_sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    shadow_sampler.compare = SG_COMPAREFUNC_LESS_EQUAL;
    shadow_sampler.label = "shadow comparison sampler";
    m_shadow_sampler = gpu::makeSampler(shadow_sampler);

    constexpr u8 kWhitePixel[4] = {255, 255, 255, 255};
    sg_image_desc white_desc = {};
    white_desc.width = 1;
    white_desc.height = 1;
    white_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    white_desc.data.mip_levels[0] = sg_range{kWhitePixel, sizeof(kWhitePixel)};
    white_desc.label = "white placeholder texture";
    m_white_image = gpu::makeImage(white_desc);

    sg_view_desc white_view = {};
    white_view.texture.image = *m_white_image;
    white_view.label = "white placeholder texture";
    m_white_texture = gpu::makeView(white_view);

    // A single zeroed light; never read, since light_count is 0 when it is bound.
    const std::array<Eigen::Vector4f, 4> kEmptyLight{};
    sg_buffer_desc empty_desc = {};
    empty_desc.size = sizeof(kEmptyLight);
    empty_desc.usage.storage_buffer = true;
    empty_desc.data = sg_range{kEmptyLight.data(), sizeof(kEmptyLight)};
    empty_desc.label = "empty light buffer";
    m_empty_light_buffer = gpu::makeBuffer(empty_desc);

    sg_view_desc empty_view = {};
    empty_view.storage_buffer.buffer = *m_empty_light_buffer;
    empty_view.label = "empty light buffer";
    m_empty_light_view = gpu::makeView(empty_view);

    buildShadowResources();
    buildPipelines();
    // Screen-sized targets are built lazily on the first render(w, h) -- the window size is
    // something the caller passes in, not something this class reaches out to sokol_app for.
}

void DeferredRenderer::buildShadowResources() {
    sg_image_desc desc = {};
    desc.usage.depth_stencil_attachment = true;
    desc.type = SG_IMAGETYPE_ARRAY;
    desc.width = kShadowMapResolution;
    desc.height = kShadowMapResolution;
    desc.num_slices = static_cast<int>(kCascadeCount);
    desc.pixel_format = kDepthFormat;
    desc.label = "shadow map (D32 cascade array)";
    m_shadow_image = gpu::makeImage(desc);

    for (u32 cascade = 0; cascade < kCascadeCount; cascade++) {
        sg_view_desc attachment = {};
        attachment.depth_stencil_attachment.image = *m_shadow_image;
        attachment.depth_stencil_attachment.slice = static_cast<int>(cascade);
        attachment.label = "shadow cascade";
        m_shadow_attachments[cascade] = gpu::makeView(attachment);
    }

    sg_view_desc texture = {};
    texture.texture.image = *m_shadow_image;
    texture.label = "shadow map";
    m_shadow_texture = gpu::makeView(texture);
}

void DeferredRenderer::buildPipelines() {
    // --- G-buffer: three colour attachments plus depth.
    sg_pipeline_desc gbuffer = {};
    gbuffer.shader = sg_make_shader(gbuffer_shader_desc(sg_query_backend()));
    setMeshLayout(gbuffer.layout);
    gbuffer.index_type = SG_INDEXTYPE_UINT32;
    gbuffer.cull_mode = SG_CULLMODE_BACK;
    gbuffer.face_winding = SG_FACEWINDING_CCW;
    gbuffer.depth.pixel_format = kDepthFormat;
    gbuffer.depth.compare = SG_COMPAREFUNC_LESS;
    gbuffer.depth.write_enabled = true;
    gbuffer.color_count = 3;
    gbuffer.colors[0].pixel_format = kAlbedoFormat;
    gbuffer.colors[1].pixel_format = kNormalFormat;
    gbuffer.colors[2].pixel_format = kMaterialFormat;
    gbuffer.label = "gbuffer pipeline";
    m_gbuffer_pipeline = sg_make_pipeline(&gbuffer);

    sg_pipeline_desc gbuffer_instanced = gbuffer;
    gbuffer_instanced.shader = sg_make_shader(gbuffer_instanced_shader_desc(sg_query_backend()));
    setInstanceLayout(gbuffer_instanced.layout, ATTR_gbuffer_instanced_inst_transform, true);
    gbuffer_instanced.label = "gbuffer instanced pipeline";
    m_gbuffer_instanced_pipeline = sg_make_pipeline(&gbuffer_instanced);

    // --- Shadow: depth only. Front-face culling pushes acne to surfaces facing away from the light.
    sg_pipeline_desc shadow = {};
    shadow.shader = sg_make_shader(shadow_depth_shader_desc(sg_query_backend()));
    setShadowMeshLayout(shadow.layout);
    shadow.index_type = SG_INDEXTYPE_UINT32;
    shadow.cull_mode = SG_CULLMODE_FRONT;
    shadow.face_winding = SG_FACEWINDING_CCW;
    shadow.depth.pixel_format = kDepthFormat;
    shadow.depth.compare = SG_COMPAREFUNC_LESS;
    shadow.depth.write_enabled = true;
    // sokol only treats a pipeline as depth-only when colors[0] is explicitly NONE; a zeroed
    // desc defaults to one colour attachment instead.
    shadow.colors[0].pixel_format = SG_PIXELFORMAT_NONE;
    shadow.color_count = 0;
    shadow.label = "shadow pipeline";
    m_shadow_pipeline = sg_make_pipeline(&shadow);

    sg_pipeline_desc shadow_instanced = shadow;
    shadow_instanced.shader = sg_make_shader(shadow_depth_instanced_shader_desc(sg_query_backend()));
    setInstanceLayout(shadow_instanced.layout, ATTR_shadow_depth_instanced_inst_transform, false);
    shadow_instanced.label = "shadow instanced pipeline";
    m_shadow_instanced_pipeline = sg_make_pipeline(&shadow_instanced);

    // --- Fullscreen passes: no vertex buffer, no depth. Lighting and sky write HDR; tonemap,
    // FXAA and the shadow-map debug blit all write the LDR target -- the Presenter, not this
    // renderer, is what blits to the swapchain.
    m_lighting_pass = primitives::pass::FullscreenPass::create(lighting_shader_desc(sg_query_backend()), kHdrFormat, "lighting pipeline");
    m_sky_pass = primitives::pass::FullscreenPass::create(sky_shader_desc(sg_query_backend()), kHdrFormat, "sky pipeline");
    m_tonemap.emplace(kLdrFormat);
    m_fxaa.emplace(kLdrFormat);
    m_shadow_debug_pass =
        primitives::pass::FullscreenPass::create(shadow_debug_shader_desc(sg_query_backend()), kLdrFormat, "shadow debug pipeline");
}

void DeferredRenderer::rebuildTargets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    m_width = width;
    m_height = height;

    m_albedo = primitives::pass::makeColorTarget(width, height, kAlbedoFormat, "gbuffer albedo");
    m_normal = primitives::pass::makeColorTarget(width, height, kNormalFormat, "gbuffer normal (octahedral)");
    m_material = primitives::pass::makeColorTarget(width, height, kMaterialFormat, "gbuffer material");
    m_depth = primitives::pass::makeDepthTarget(width, height, "gbuffer depth");
    m_hdr = primitives::pass::makeColorTarget(width, height, kHdrFormat, "hdr colour");
    m_tonemapped = primitives::pass::makeColorTarget(width, height, kLdrFormat, "tonemapped colour");
    m_ldr = primitives::pass::makeColorTarget(width, height, kLdrFormat, "final colour");
}

void DeferredRenderer::beginFrame(const primitives::scene::Camera& camera) {
    m_draw_list.clear();
    m_has_shadow_light = false;

    m_view_projection = camera.viewProjectionMatrix();
    m_inverse_view_projection = camera.inverseViewProjectionMatrix();
    m_view = camera.transformationMatrix();
    m_view_position = camera.position();

    // Cascades are fitted in render(), not here: they need m_sun_direction, which
    // setLights() -- called after beginFrame(), per its own contract -- is what sets it.
    m_pending_camera = &camera;
}

void DeferredRenderer::setLights(const primitives::pass::LightManager& lights) {
    m_light_view = lights.view();
    m_light_count = lights.count();

    if (const primitives::scene::Light* directional = lights.directionalLight(); directional != nullptr) {
        m_sun_direction = directional->direction.normalized();
        m_has_shadow_light = true;
    }
}

void DeferredRenderer::setSkybox(const primitives::scene::Skybox& skybox) {
    if (const primitives::scene::GradientSky* gradient = std::get_if<primitives::scene::GradientSky>(&skybox.source); gradient != nullptr) {
        m_sky_zenith = gradient->zenith;
        m_sky_ground = gradient->ground;
        m_sky_kind = 0.0f;
    } else {
        m_sky_kind = 1.0f; // cubemap path is not built yet
    }
    m_sky_intensity = skybox.intensity;
}

void DeferredRenderer::setExposure(f32 exposure) {
    m_exposure = exposure;
}

void DeferredRenderer::setAmbient(const Eigen::Vector3f& ambient) {
    m_ambient = ambient;
}

void DeferredRenderer::setFxaaEnabled(bool enabled) {
    m_fxaa_enabled = enabled;
}

void DeferredRenderer::setShadowDistance(f32 distance) {
    m_shadow_distance = distance;
}

void DeferredRenderer::setCascadeSplitLambda(f32 lambda) {
    m_cascade_split_lambda = lambda;
}

void DeferredRenderer::toggleShadowMapDebug() {
    m_show_shadow_map_debug = !m_show_shadow_map_debug;
}

void DeferredRenderer::cycleShadowMapDebugLayer() {
    m_shadow_debug_layer = (m_shadow_debug_layer + 1) % kCascadeCount;
}

template <primitives::scene::Mesh T>
void DeferredRenderer::submit(
    T& mesh, const primitives::scene::Material& material, const Eigen::Matrix4f& model, const Eigen::AlignedBox3f& world_bounds, bool camera_visible
) {
    DrawItem item;
    item.vertex = mesh.vertexBuffer();
    item.index = mesh.indexBuffer();
    if constexpr (std::same_as<T, primitives::scene::InstancedMesh>) {
        item.instance = mesh.instanceBuffer();
    }
    item.index_count = mesh.indexCount();
    item.instance_count = mesh.instanceCount();
    item.model = model;
    item.normal_matrix = primitives::scene::normalMatrix(model);
    item.material = &material;
    item.world_bounds = world_bounds;
    item.camera_visible = camera_visible;

    m_draw_list.push_back(item);
}

template void DeferredRenderer::submit<primitives::scene::StandardMesh>(primitives::scene::StandardMesh&, const primitives::scene::Material&, const Eigen::Matrix4f&, const Eigen::AlignedBox3f&, bool);
template void DeferredRenderer::submit<primitives::scene::InstancedMesh>(primitives::scene::InstancedMesh&, const primitives::scene::Material&, const Eigen::Matrix4f&, const Eigen::AlignedBox3f&, bool);

void DeferredRenderer::recordShadowPass() {
    for (u32 cascade = 0; cascade < kCascadeCount; cascade++) {
        sg_pass pass = {};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
        // Depth defaults to DONTCARE, which also means sokol never transitions the image into
        // texture access -- and the lighting pass samples this.
        pass.action.depth.store_action = SG_STOREACTION_STORE;
        pass.action.stencil.load_action = SG_LOADACTION_DONTCARE;
        pass.attachments.depth_stencil = *m_shadow_attachments[cascade];
        pass.label = "shadow pass";
        sg_begin_pass(&pass);

        const primitives::scene::Frustum light_frustum = primitives::scene::Frustum::fromViewProjection(m_cascade_view_proj[cascade]);

        for (const DrawItem& item : m_draw_list) {
            const bool instanced = item.instance.id != SG_INVALID_ID;
            // Instanced items carry per-instance transforms the bounds don't cover, so never cull them.
            if (!instanced && !light_frustum.intersects(item.world_bounds)) {
                continue;
            }

            sg_apply_pipeline(instanced ? m_shadow_instanced_pipeline : m_shadow_pipeline);

            sg_bindings bindings = {};
            bindings.vertex_buffers[0] = item.vertex;
            if (instanced) {
                bindings.vertex_buffers[1] = item.instance;
            }
            bindings.index_buffer = item.index;
            sg_apply_bindings(&bindings);

            shadow_vs_params_t params{.light_view_proj = m_cascade_view_proj[cascade], .shadow_model = item.model};
            applyUniforms(UB_shadow_vs_params, params);

            sg_draw(0, static_cast<int>(item.index_count), static_cast<int>(item.instance_count));
        }

        sg_end_pass();
    }
}

void DeferredRenderer::recordGBufferPass() {
    sg_pass pass = {};
    for (int i = 0; i < 3; i++) {
        pass.action.colors[i].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[i].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    // Sampled by the lighting and sky passes, so it has to be stored -- see recordShadowPass.
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.action.stencil.load_action = SG_LOADACTION_DONTCARE;
    pass.attachments.colors[0] = *m_albedo.attachment;
    pass.attachments.colors[1] = *m_normal.attachment;
    pass.attachments.colors[2] = *m_material.attachment;
    pass.attachments.depth_stencil = *m_depth.attachment;
    pass.label = "gbuffer pass";
    sg_begin_pass(&pass);

    for (const DrawItem& item : m_draw_list) {
        if (!item.camera_visible) {
            continue;
        }

        const bool instanced = item.instance.id != SG_INVALID_ID;
        sg_apply_pipeline(instanced ? m_gbuffer_instanced_pipeline : m_gbuffer_pipeline);

        const primitives::scene::Material& material = *item.material;
        const sg_view albedo = material.baseColorTexture() ? material.baseColorTexture()->view() : *m_white_texture;
        const sg_view metallic_roughness =
            material.metallicRoughnessTexture() ? material.metallicRoughnessTexture()->view() : *m_white_texture;

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = item.vertex;
        if (instanced) {
            bindings.vertex_buffers[1] = item.instance;
        }
        bindings.index_buffer = item.index;
        bindings.views[VIEW_tex_albedo] = albedo;
        bindings.views[VIEW_tex_metallic_roughness] = metallic_roughness;
        bindings.samplers[SMP_smp_material] = *m_material_sampler;
        sg_apply_bindings(&bindings);

        gbuffer_vs_params_t vs_params{
            .view_projection = m_view_projection,
            .model = item.model,
            .normal_matrix = item.normal_matrix,
        };
        applyUniforms(UB_gbuffer_vs_params, vs_params);

        gbuffer_fs_params_t fs_params{
            .base_color_factor = material.baseColorFactor(),
            .metallic_roughness_factor = Eigen::Vector4f(material.metallicFactor(), material.roughnessFactor(), 0.0f, 0.0f),
        };
        applyUniforms(UB_gbuffer_fs_params, fs_params);

        sg_draw(0, static_cast<int>(item.index_count), static_cast<int>(item.instance_count));
    }

    sg_end_pass();
}

sg_view DeferredRenderer::render(int width, int height) {
    // Nothing to draw into yet -- minimised window, or a frame before the first real size.
    // Returning an invalid view tells the Presenter to skip its blit for this frame.
    if (width <= 0 || height <= 0) {
        return sg_view{};
    }

    // The target size is an input, not something to pull from sokol_app: (re)build the
    // screen-sized targets here the first time and whenever the window changes size.
    if (width != m_width || height != m_height) {
        rebuildTargets(width, height);
    }

    // Fitted here, not in beginFrame(): needs m_sun_direction, which setLights() -- called
    // after beginFrame() by contract -- is what sets. m_pending_camera is non-owning and
    // only valid for this frame; beginFrame() must have run first to set it.
    const primitives::pass::CascadeSetup cascades = primitives::pass::computeCascades(
        *m_pending_camera, m_sun_direction, m_shadow_distance, m_cascade_split_lambda, static_cast<f32>(kShadowMapResolution)
    );
    m_cascade_view_proj = cascades.view_proj;
    m_cascade_split_depth = cascades.split_view_depth;

    recordShadowPass();
    recordGBufferPass();

    // --- Lighting: resolve the G-buffer into HDR.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
        pass.attachments.colors[0] = *m_hdr.attachment;
        pass.label = "lighting pass";
        sg_begin_pass(&pass);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_gbuffer_albedo] = *m_albedo.texture;
        bindings.views[VIEW_tex_gbuffer_normal] = *m_normal.texture;
        bindings.views[VIEW_tex_gbuffer_depth] = *m_depth.texture;
        bindings.views[VIEW_tex_gbuffer_material] = *m_material.texture;
        bindings.views[VIEW_tex_shadow] = *m_shadow_texture;
        bindings.views[VIEW_light_buffer] = m_light_count > 0 ? m_light_view : *m_empty_light_view;
        bindings.samplers[SMP_smp_gbuffer] = *m_gbuffer_sampler;
        bindings.samplers[SMP_smp_shadow] = *m_shadow_sampler;

        m_lighting_pass.draw(bindings, [&]() {
            lighting_params_t params{};
            params.inverse_view_projection = m_inverse_view_projection;
            params.view = m_view;
            for (u32 cascade = 0; cascade < kCascadeCount; cascade++) {
                params.cascade_view_proj[cascade] = m_cascade_view_proj[cascade];
            }
            params.view_position = toVec4(m_view_position, 1.0f);
            params.ambient = toVec4(m_ambient, 0.0f);
            params.cascade_split_depth = Eigen::Vector4f(
                m_cascade_split_depth[0], m_cascade_split_depth[1], m_cascade_split_depth[2], m_cascade_split_depth[3]
            );
            params.shadow_params =
                Eigen::Vector4f(m_shadow_depth_bias, m_shadow_normal_offset, m_has_shadow_light ? 1.0f : 0.0f, 0.0f);
            params.light_count = static_cast<int>(m_light_count);
            applyUniforms(UB_lighting_params, params);
        });
        sg_end_pass();
    }

    // --- Sky: fills the pixels the G-buffer never wrote. Loads, does not clear.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        pass.attachments.colors[0] = *m_hdr.attachment;
        pass.label = "sky pass";
        sg_begin_pass(&pass);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_sky_depth] = *m_depth.texture;
        bindings.samplers[SMP_smp_sky] = *m_gbuffer_sampler;

        m_sky_pass.draw(bindings, [&]() {
            sky_params_t params{
                .sky_inverse_view_projection = m_inverse_view_projection,
                .sky_view_position = toVec4(m_view_position, 1.0f),
                .zenith_intensity = toVec4(m_sky_zenith, m_sky_intensity),
                .ground = toVec4(m_sky_ground, 0.0f),
                .sky_kind = Eigen::Vector4f(m_sky_kind, 0.0f, 0.0f, 0.0f),
            };
            applyUniforms(UB_sky_params, params);
        });
        sg_end_pass();
    }

    // --- Tonemap: HDR -> LDR.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.attachments.colors[0] = *m_tonemapped.attachment;
        pass.label = "tonemap pass";
        sg_begin_pass(&pass);

        m_tonemap->draw(*m_hdr.texture, *m_material_sampler, m_exposure);
        sg_end_pass();
    }

    // --- FXAA into the final LDR target, then the optional shadow-map blit and the debug
    // line overlay over the top. The Presenter blits this target to the swapchain.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.attachments.colors[0] = *m_ldr.attachment;
        pass.label = "fxaa pass";
        sg_begin_pass(&pass);

        m_fxaa->draw(*m_tonemapped.texture, *m_material_sampler, m_width, m_height, m_fxaa_enabled);

        if (m_show_shadow_map_debug) {
            sg_bindings debug_bindings = {};
            debug_bindings.views[VIEW_tex_shadow_debug] = *m_shadow_texture;
            debug_bindings.samplers[SMP_smp_shadow_debug] = *m_gbuffer_sampler;

            m_shadow_debug_pass.draw(debug_bindings, [&]() {
                shadow_debug_params_t debug_params{
                    .layer = Eigen::Vector4f(static_cast<f32>(m_shadow_debug_layer), 0.0f, 0.0f, 0.0f)
                };
                applyUniforms(UB_shadow_debug_params, debug_params);
            });
        }

        // Unlit overlay, no depth test -- see graphics::primitives::pass::DebugDraw. No-op while disabled.
        m_debug_draw.begin(m_view_projection);
        m_debug_draw.render();

        sg_end_pass();
    }

    return *m_ldr.texture;
}

} // namespace graphics::renderers
