#include <graphics/renderer.hpp>

#include <graphics/frustum.hpp>
#include <graphics/texture.hpp>

#include <sokol_app.h>

#include <sokol_gfx.h>
#include <sokol_glue.h>

#include "engine.glsl.h"

namespace graphics {

namespace {

constexpr sg_pixel_format kAlbedoFormat = SG_PIXELFORMAT_SRGB8A8;
constexpr sg_pixel_format kNormalFormat = SG_PIXELFORMAT_RG16F;
constexpr sg_pixel_format kMaterialFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;
constexpr sg_pixel_format kHdrFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kLdrFormat = SG_PIXELFORMAT_RGBA8;

constexpr int kFullscreenTriangleVertexCount = 3;

RenderTarget makeColorTarget(int width, int height, sg_pixel_format format, const char* label) {
    sg_image_desc image_desc = {};
    image_desc.usage.color_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = format;
    image_desc.label = label;

    RenderTarget target;
    target.image = gpu::makeImage(image_desc);

    sg_view_desc attachment_desc = {};
    attachment_desc.color_attachment.image = *target.image;
    attachment_desc.label = label;
    target.attachment = gpu::makeView(attachment_desc);

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = *target.image;
    texture_desc.label = label;
    target.texture = gpu::makeView(texture_desc);

    return target;
}

RenderTarget makeDepthTarget(int width, int height, const char* label) {
    sg_image_desc image_desc = {};
    image_desc.usage.depth_stencil_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = kDepthFormat;
    image_desc.label = label;

    RenderTarget target;
    target.image = gpu::makeImage(image_desc);

    sg_view_desc attachment_desc = {};
    attachment_desc.depth_stencil_attachment.image = *target.image;
    attachment_desc.label = label;
    target.attachment = gpu::makeView(attachment_desc);

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = *target.image;
    texture_desc.label = label;
    target.texture = gpu::makeView(texture_desc);

    return target;
}

// Vertex layout shared by the G-buffer and shadow passes: interleaved Vertex at buffer 0.
void setMeshLayout(sg_vertex_layout_state& layout) {
    layout.buffers[0].stride = sizeof(Vertex);
    layout.attrs[ATTR_gbuffer_position].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_gbuffer_position].offset = offsetof(Vertex, position);
    layout.attrs[ATTR_gbuffer_position].buffer_index = 0;
    layout.attrs[ATTR_gbuffer_normal].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_gbuffer_normal].offset = offsetof(Vertex, normal);
    layout.attrs[ATTR_gbuffer_normal].buffer_index = 0;
    layout.attrs[ATTR_gbuffer_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
    layout.attrs[ATTR_gbuffer_texcoord].offset = offsetof(Vertex, uv);
    layout.attrs[ATTR_gbuffer_texcoord].buffer_index = 0;
}

// The shadow pass reads the same interleaved vertex buffer but only consumes position;
// declaring the unused attributes makes Vulkan complain that they go unread.
void setShadowMeshLayout(sg_vertex_layout_state& layout) {
    layout.buffers[0].stride = sizeof(Vertex);
    layout.attrs[ATTR_shadow_depth_position].format = SG_VERTEXFORMAT_FLOAT3;
    layout.attrs[ATTR_shadow_depth_position].offset = offsetof(Vertex, position);
    layout.attrs[ATTR_shadow_depth_position].buffer_index = 0;
}

// Per-instance InstanceData at buffer 1: a mat4 is four consecutive FLOAT4 attributes.
void setInstanceLayout(sg_vertex_layout_state& layout, int first_location, bool include_normal_matrix) {
    layout.buffers[1].stride = sizeof(InstanceData);
    layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;

    for (int i = 0; i < 4; i++) {
        const int slot = first_location + i;
        layout.attrs[slot].format = SG_VERTEXFORMAT_FLOAT4;
        layout.attrs[slot].offset = static_cast<int>(offsetof(InstanceData, transform) + i * sizeof(Eigen::Vector4f));
        layout.attrs[slot].buffer_index = 1;
    }

    if (!include_normal_matrix) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        const int slot = ATTR_gbuffer_instanced_inst_normal_matrix + i;
        layout.attrs[slot].format = SG_VERTEXFORMAT_FLOAT4;
        layout.attrs[slot].offset = static_cast<int>(offsetof(InstanceData, normal_matrix) + i * sizeof(Eigen::Vector4f));
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

Renderer::Renderer() {
    const Skybox defaults{};
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
    rebuildTargets(sapp_width(), sapp_height());
}

void Renderer::buildShadowResources() {
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

void Renderer::buildPipelines() {
    // --- G-buffer: three colour attachments plus depth.
    sg_pipeline_desc gbuffer = {};
    gbuffer.shader = sg_make_shader(gbuffer_shader_desc(sg_query_backend()));
    setMeshLayout(gbuffer.layout);
    gbuffer.index_type = SG_INDEXTYPE_UINT32;
    gbuffer.cull_mode = SG_CULLMODE_BACK;
    // The projection negates Y (Vulkan NDC is Y-down), which reverses screen-space winding --
    // so a glTF counter-clockwise front face rasterises clockwise here.
    gbuffer.face_winding = SG_FACEWINDING_CW;
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
    shadow.face_winding = SG_FACEWINDING_CW; // same Y-flip reasoning as the G-buffer pipeline
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

    // --- Fullscreen passes: no vertex buffer, no depth.
    sg_pipeline_desc lighting = {};
    lighting.shader = sg_make_shader(lighting_shader_desc(sg_query_backend()));
    lighting.color_count = 1;
    lighting.colors[0].pixel_format = kHdrFormat;
    lighting.depth.pixel_format = SG_PIXELFORMAT_NONE;
    lighting.label = "lighting pipeline";
    m_lighting_pipeline = sg_make_pipeline(&lighting);

    sg_pipeline_desc sky = lighting;
    sky.shader = sg_make_shader(sky_shader_desc(sg_query_backend()));
    sky.label = "sky pipeline";
    m_sky_pipeline = sg_make_pipeline(&sky);

    sg_pipeline_desc tonemap = {};
    tonemap.shader = sg_make_shader(tonemap_shader_desc(sg_query_backend()));
    tonemap.color_count = 1;
    tonemap.colors[0].pixel_format = kLdrFormat;
    tonemap.depth.pixel_format = SG_PIXELFORMAT_NONE;
    tonemap.label = "tonemap pipeline";
    m_tonemap_pipeline = sg_make_pipeline(&tonemap);

    // FXAA and the debug blit render straight to the swapchain, so they take its formats.
    sg_pipeline_desc fxaa = {};
    fxaa.shader = sg_make_shader(fxaa_shader_desc(sg_query_backend()));
    fxaa.label = "fxaa pipeline";
    m_fxaa_pipeline = sg_make_pipeline(&fxaa);

    sg_pipeline_desc shadow_debug = {};
    shadow_debug.shader = sg_make_shader(shadow_debug_shader_desc(sg_query_backend()));
    shadow_debug.label = "shadow debug pipeline";
    m_shadow_debug_pipeline = sg_make_pipeline(&shadow_debug);
}

void Renderer::rebuildTargets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    m_width = width;
    m_height = height;

    m_albedo = makeColorTarget(width, height, kAlbedoFormat, "gbuffer albedo");
    m_normal = makeColorTarget(width, height, kNormalFormat, "gbuffer normal (octahedral)");
    m_material = makeColorTarget(width, height, kMaterialFormat, "gbuffer material");
    m_depth = makeDepthTarget(width, height, "gbuffer depth");
    m_hdr = makeColorTarget(width, height, kHdrFormat, "hdr colour");
    m_ldr = makeColorTarget(width, height, kLdrFormat, "tonemapped colour");
}

void Renderer::beginFrame() {
    // Textures decoded on worker threads can only be uploaded from here, on the sokol thread.
    Texture::drainUploads();

    if (sapp_width() != m_width || sapp_height() != m_height) {
        rebuildTargets(sapp_width(), sapp_height());
    }

    m_draw_list.clear();
    m_has_shadow_light = false;
}

void Renderer::setLights(const LightManager& lights) {
    m_light_view = lights.view();
    m_light_count = lights.count();

    if (const Light* directional = lights.directionalLight(); directional != nullptr) {
        m_sun_direction = directional->direction.normalized();
        m_has_shadow_light = true;
    }
}

void Renderer::setSkybox(const Skybox& skybox) {
    if (const GradientSky* gradient = std::get_if<GradientSky>(&skybox.source); gradient != nullptr) {
        m_sky_zenith = gradient->zenith;
        m_sky_ground = gradient->ground;
        m_sky_kind = 0.0f;
    } else {
        m_sky_kind = 1.0f; // cubemap path is not built yet
    }
    m_sky_intensity = skybox.intensity;
}

void Renderer::setExposure(f32 exposure) {
    m_exposure = exposure;
}

void Renderer::setAmbient(const Eigen::Vector3f& ambient) {
    m_ambient = ambient;
}

void Renderer::setFxaaEnabled(bool enabled) {
    m_fxaa_enabled = enabled;
}

void Renderer::setShadowDistance(f32 distance) {
    m_shadow_distance = distance;
}

void Renderer::setCascadeSplitLambda(f32 lambda) {
    m_cascade_split_lambda = lambda;
}

void Renderer::toggleShadowMapDebug() {
    m_show_shadow_map_debug = !m_show_shadow_map_debug;
}

void Renderer::cycleShadowMapDebugLayer() {
    m_shadow_debug_layer = (m_shadow_debug_layer + 1) % kCascadeCount;
}

template <Mesh T>
void Renderer::renderMesh(
    T& mesh, const Material& material, const Eigen::Matrix4f& model, const Eigen::AlignedBox3f& world_bounds, bool camera_visible,
    const Camera& camera
) {
    // The camera is the same for every draw in a frame, so the first one settles it -- and it is
    // also what the cascades are fitted against.
    if (m_draw_list.empty()) {
        m_view_projection = camera.viewProjectionMatrix();
        m_inverse_view_projection = camera.inverseViewProjectionMatrix();
        m_view = camera.transformationMatrix();
        m_view_position = camera.position();

        const CascadeSetup cascades =
            computeCascades(camera, m_sun_direction, m_shadow_distance, m_cascade_split_lambda, static_cast<f32>(kShadowMapResolution));
        m_cascade_view_proj = cascades.view_proj;
        m_cascade_split_depth = cascades.split_view_depth;
    }

    DrawItem item;
    item.vertex = mesh.vertexBuffer();
    item.index = mesh.indexBuffer();
    if constexpr (std::same_as<T, InstancedMesh>) {
        item.instance = mesh.instanceBuffer();
    }
    item.index_count = mesh.indexCount();
    item.instance_count = mesh.instanceCount();
    item.model = model;
    item.normal_matrix = normalMatrix(model);
    item.material = &material;
    item.world_bounds = world_bounds;
    item.camera_visible = camera_visible;

    m_draw_list.push_back(item);
}

template void Renderer::renderMesh<StandardMesh>(
    StandardMesh&, const Material&, const Eigen::Matrix4f&, const Eigen::AlignedBox3f&, bool, const Camera&
);
template void Renderer::renderMesh<InstancedMesh>(
    InstancedMesh&, const Material&, const Eigen::Matrix4f&, const Eigen::AlignedBox3f&, bool, const Camera&
);

void Renderer::recordShadowPass() {
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

        const Frustum light_frustum = Frustum::fromViewProjection(m_cascade_view_proj[cascade]);

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

void Renderer::recordGBufferPass() {
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

        const Material& material = *item.material;
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

void Renderer::endFrame() {
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

        sg_apply_pipeline(m_lighting_pipeline);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_gbuffer_albedo] = *m_albedo.texture;
        bindings.views[VIEW_tex_gbuffer_normal] = *m_normal.texture;
        bindings.views[VIEW_tex_gbuffer_depth] = *m_depth.texture;
        bindings.views[VIEW_tex_gbuffer_material] = *m_material.texture;
        bindings.views[VIEW_tex_shadow] = *m_shadow_texture;
        bindings.views[VIEW_light_buffer] = m_light_count > 0 ? m_light_view : *m_empty_light_view;
        bindings.samplers[SMP_smp_gbuffer] = *m_gbuffer_sampler;
        bindings.samplers[SMP_smp_shadow] = *m_shadow_sampler;
        sg_apply_bindings(&bindings);

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
        params.shadow_params = Eigen::Vector4f(m_shadow_depth_bias, m_shadow_normal_offset, m_has_shadow_light ? 1.0f : 0.0f, 0.0f);
        params.light_count = static_cast<int>(m_light_count);
        applyUniforms(UB_lighting_params, params);

        sg_draw(0, kFullscreenTriangleVertexCount, 1);
        sg_end_pass();
    }

    // --- Sky: fills the pixels the G-buffer never wrote. Loads, does not clear.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        pass.attachments.colors[0] = *m_hdr.attachment;
        pass.label = "sky pass";
        sg_begin_pass(&pass);

        sg_apply_pipeline(m_sky_pipeline);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_sky_depth] = *m_depth.texture;
        bindings.samplers[SMP_smp_sky] = *m_gbuffer_sampler;
        sg_apply_bindings(&bindings);

        sky_params_t params{
            .sky_inverse_view_projection = m_inverse_view_projection,
            .sky_view_position = toVec4(m_view_position, 1.0f),
            .zenith_intensity = toVec4(m_sky_zenith, m_sky_intensity),
            .ground = toVec4(m_sky_ground, 0.0f),
            .sky_kind = Eigen::Vector4f(m_sky_kind, 0.0f, 0.0f, 0.0f),
        };
        applyUniforms(UB_sky_params, params);

        sg_draw(0, kFullscreenTriangleVertexCount, 1);
        sg_end_pass();
    }

    // --- Tonemap: HDR -> LDR.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.attachments.colors[0] = *m_ldr.attachment;
        pass.label = "tonemap pass";
        sg_begin_pass(&pass);

        sg_apply_pipeline(m_tonemap_pipeline);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_source] = *m_hdr.texture;
        bindings.samplers[SMP_smp_source] = *m_material_sampler;
        sg_apply_bindings(&bindings);

        tonemap_params_t params{.exposure = Eigen::Vector4f(m_exposure, 0.0f, 0.0f, 0.0f)};
        applyUniforms(UB_tonemap_params, params);

        sg_draw(0, kFullscreenTriangleVertexCount, 1);
        sg_end_pass();
    }

    // --- FXAA straight to the swapchain, then the optional shadow-map blit over the top.
    {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.swapchain = sglue_swapchain();
        pass.label = "fxaa pass";
        sg_begin_pass(&pass);

        sg_apply_pipeline(m_fxaa_pipeline);

        sg_bindings bindings = {};
        bindings.views[VIEW_tex_source] = *m_ldr.texture;
        bindings.samplers[SMP_smp_source] = *m_material_sampler;
        sg_apply_bindings(&bindings);

        fxaa_params_t params{
            .rcp_frame = Eigen::Vector4f(
                1.0f / static_cast<f32>(m_width), 1.0f / static_cast<f32>(m_height), m_fxaa_enabled ? 1.0f : 0.0f, 0.0f
            ),
        };
        applyUniforms(UB_fxaa_params, params);

        sg_draw(0, kFullscreenTriangleVertexCount, 1);

        if (m_show_shadow_map_debug) {
            sg_apply_pipeline(m_shadow_debug_pipeline);

            sg_bindings debug_bindings = {};
            debug_bindings.views[VIEW_tex_shadow_debug] = *m_shadow_texture;
            debug_bindings.samplers[SMP_smp_shadow_debug] = *m_gbuffer_sampler;
            sg_apply_bindings(&debug_bindings);

            shadow_debug_params_t debug_params{.layer = Eigen::Vector4f(static_cast<f32>(m_shadow_debug_layer), 0.0f, 0.0f, 0.0f)};
            applyUniforms(UB_shadow_debug_params, debug_params);

            sg_draw(0, kFullscreenTriangleVertexCount, 1);
        }

        sg_end_pass();
    }

    sg_commit();
}

} // namespace graphics
