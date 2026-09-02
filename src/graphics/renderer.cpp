#include <graphics/renderer.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace {

constexpr u32 kGBufferVertSpv[] =
#include "gbuffer.vert.h"
    ;
constexpr u32 kGBufferInstancedVertSpv[] =
#include "gbuffer_instanced.vert.h"
    ;
constexpr u32 kGBufferFragSpv[] =
#include "gbuffer.frag.h"
    ;
constexpr u32 kFullscreenTriangleVertSpv[] =
#include "fullscreen_triangle.vert.h"
    ;
constexpr u32 kLightingFragSpv[] =
#include "lighting.frag.h"
    ;
constexpr u32 kTonemapFragSpv[] =
#include "tonemap.frag.h"
    ;
constexpr u32 kSkyFragSpv[] =
#include "sky.frag.h"
    ;
constexpr u32 kFxaaFragSpv[] =
#include "fxaa.frag.h"
    ;
constexpr u32 kShadowDepthVertSpv[] =
#include "shadow_depth.vert.h"
    ;
constexpr u32 kShadowDepthInstancedVertSpv[] =
#include "shadow_depth_instanced.vert.h"
    ;
constexpr u32 kShadowDepthFragSpv[] =
#include "shadow_depth.frag.h"
    ;
constexpr u32 kShadowDebugFragSpv[] =
#include "shadow_debug.frag.h"
    ;

constexpr u32 kFullscreenTriangleVertexCount = 3;

struct TonemapPushConstants {
    f32 exposure;
};

struct FxaaPushConstants {
    Eigen::Vector2f rcp_frame; // 1.0 / screen size
    f32 enabled;               // < 0.5 -> passthrough
};

struct GBufferPushConstants {
    Eigen::Matrix4f model;
    Eigen::Matrix4f normal_matrix; // inverse-transpose of mat3(model); only the 3x3 is read
};

// std140-matched to lighting.frag's LightingUniforms block.
struct LightingUniforms {
    Eigen::Matrix4f inverse_view_projection;
    Eigen::Matrix4f view;
    std::array<Eigen::Matrix4f, graphics::kShadowCascadeCount> cascade_view_proj;
    Eigen::Vector4f view_position;
    Eigen::Vector4f ambient;             // rgb = flat ambient irradiance (pre-tonemap)
    Eigen::Vector4f cascade_split_depth; // x..w = view-space far edge of each cascade
    Eigen::Vector4f shadow_params;       // x = depth bias, y = normal offset
    u32 light_count;
    u32 _pad[3];
};
static_assert(graphics::kShadowCascadeCount == 4, "lighting.frag packs cascade_split_depth as a vec4");

struct SkyPushConstants {
    Eigen::Matrix4f inverse_view_projection;
    Eigen::Vector4f view_position;
    Eigen::Vector4f zenith_intensity; // rgb = zenith, a = intensity
    Eigen::Vector4f ground;           // rgb = ground
    u32 kind;                         // 0 = gradient, 1 = cubemap (unused today)
};

struct ShadowPushConstants {
    Eigen::Matrix4f light_view_proj;
    Eigen::Matrix4f model; // exactly 128 bytes -- the guaranteed push-constant minimum
};

nvrhi::ShaderHandle loadShader(nvrhi::IDevice* device, nvrhi::ShaderType type, const u32* spirv, usize word_count) {
    nvrhi::ShaderDesc desc{};
    desc.shaderType = type;
    return device->createShader(desc, spirv, word_count * sizeof(u32));
}

nvrhi::RenderState opaqueNoDepth() {
    nvrhi::RenderState state{};
    state.rasterState.setCullNone();
    state.depthStencilState.disableDepthTest();
    state.depthStencilState.setDepthWriteEnable(false);
    return state;
}

nvrhi::TextureHandle createGBufferTexture(const gpu::Device& device, VkExtent2D extent, nvrhi::Format format, nvrhi::ResourceStates initial_state, const char* name) {
    nvrhi::TextureDesc desc{};
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = format;
    desc.isRenderTarget = true;
    desc.isShaderResource = true;
    desc.initialState = initial_state;
    desc.keepInitialState = true;
    desc.debugName = name;
    return device.createTexture(desc);
}

} // namespace

namespace graphics {

Renderer::Renderer(const gpu::Device& device, gpu::Swapchain& swapchain)
    : m_device(&device), m_swapchain(&swapchain), m_hdr_color(Surface::createOffscreen(device, swapchain.extent(), nvrhi::Format::RGBA16_FLOAT)),
      m_post_process(device) {
    m_cmd = device.nvrhi()->createCommandList();
    for (nvrhi::EventQueryHandle& query : m_frame_done) {
        query = device.nvrhi()->createEventQuery();
    }
    // A swapchain acquire semaphore must not be re-waited while its submit is still pending.
    assert(m_swapchain->imageCount() > kFramesInFlight);
    rebuildSwapchainSurfaces();
    rebuildGBuffer();

    nvrhi::IDevice* nvrhi_device = device.nvrhi();

    // Geometry (g_buffer) pass: writes albedo + depth from a Mesh's vertex/index buffers.
    nvrhi::VertexAttributeDesc mesh_attrs[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, position)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, normal)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("UV").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, uv)).setElementStride(sizeof(Vertex)),
    };

    nvrhi::ShaderHandle gbuffer_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kGBufferVertSpv, std::size(kGBufferVertSpv));
    nvrhi::ShaderHandle gbuffer_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kGBufferFragSpv, std::size(kGBufferFragSpv));
    m_mesh_input_layout = nvrhi_device->createInputLayout(mesh_attrs, static_cast<u32>(std::size(mesh_attrs)), gbuffer_vs);

    nvrhi::FramebufferInfo gbuffer_fb_info = nvrhi::FramebufferInfo()
                                                  .addColorFormat(nvrhi::Format::SRGBA8_UNORM)
                                                  .addColorFormat(nvrhi::Format::RG16_FLOAT) // octahedral-encoded world normal
                                                  .addColorFormat(nvrhi::Format::RGBA8_UNORM)
                                                  .setDepthFormat(nvrhi::Format::D32);

    nvrhi::RenderState gbuffer_state{};
    // Matches camera.cpp's projection parity (no Y-flip there).
    gbuffer_state.rasterState.setFrontCounterClockwise(true);

    // NVRHI's Vulkan backend offsets constant-buffer registers by 256 by default; zero it so GLSL `binding = 1` matches.
    nvrhi::BindingLayoutHandle gbuffer_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Vertex)
            .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GBufferPushConstants)))
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1)) // m_camera_buffer is volatile
            .setBindingOffsets(nvrhi::VulkanBindingOffsets().setConstantBufferOffset(0))
    );

    // Volatile: NVRHI auto-versions each writeBuffer, so maxVersions only needs to cover outstanding frames.
    nvrhi::BufferDesc camera_buffer_desc{};
    camera_buffer_desc.byteSize = sizeof(Eigen::Matrix4f);
    camera_buffer_desc.isConstantBuffer = true;
    camera_buffer_desc.isVolatile = true;
    camera_buffer_desc.maxVersions = kFramesInFlight + 2;
    camera_buffer_desc.debugName = "gbuffer camera view-projection";
    m_camera_buffer = nvrhi_device->createBuffer(camera_buffer_desc);

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it.
    m_gbuffer_texture_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                              .setVisibility(nvrhi::ShaderType::Pixel)
                                                                              .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                              .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                                                                              .addItem(nvrhi::BindingLayoutItem::Sampler(2))
                                                                              .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3))
                                                                              .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));
    m_gbuffer_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());

    constexpr u8 kWhitePixel[4] = {255, 255, 255, 255};
    nvrhi::TextureDesc white_texture_desc{};
    white_texture_desc.width = 1;
    white_texture_desc.height = 1;
    white_texture_desc.format = nvrhi::Format::RGBA8_UNORM;
    white_texture_desc.isShaderResource = true;
    white_texture_desc.initialState = nvrhi::ResourceStates::ShaderResource;
    white_texture_desc.keepInitialState = true;
    white_texture_desc.debugName = "white placeholder texture";
    m_white_texture = device.createTexture(white_texture_desc, kWhitePixel, 4);

    m_gbuffer_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setInputLayout(m_mesh_input_layout)
            .setVertexShader(gbuffer_vs)
            .setPixelShader(gbuffer_fs)
            .setRenderState(gbuffer_state)
            .addBindingLayout(m_gbuffer_texture_binding_layout)
            .addBindingLayout(gbuffer_push_constant_layout),
        gbuffer_fb_info
    );
    m_gbuffer_push_constant_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GBufferPushConstants)))
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_camera_buffer)),
        gbuffer_push_constant_layout
    );

    // Per-instance InstanceData at buffer slot 1: transform at locations 3-6, normal matrix at 7-10.
    nvrhi::VertexAttributeDesc instanced_mesh_attrs[] = {
        mesh_attrs[0],
        mesh_attrs[1],
        mesh_attrs[2],
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_TRANSFORM")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setArraySize(4)
            .setBufferIndex(1)
            .setOffset(0)
            .setElementStride(sizeof(InstanceData))
            .setIsInstanced(true),
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_NORMAL_MATRIX")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setArraySize(4)
            .setBufferIndex(1)
            .setOffset(sizeof(Eigen::Matrix4f))
            .setElementStride(sizeof(InstanceData))
            .setIsInstanced(true),
    };

    nvrhi::ShaderHandle gbuffer_instanced_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kGBufferInstancedVertSpv, std::size(kGBufferInstancedVertSpv));
    m_instanced_mesh_input_layout =
        nvrhi_device->createInputLayout(instanced_mesh_attrs, static_cast<u32>(std::size(instanced_mesh_attrs)), gbuffer_instanced_vs);

    m_gbuffer_instanced_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setInputLayout(m_instanced_mesh_input_layout)
            .setVertexShader(gbuffer_instanced_vs)
            .setPixelShader(gbuffer_fs)
            .setRenderState(gbuffer_state)
            .addBindingLayout(m_gbuffer_texture_binding_layout)
            .addBindingLayout(gbuffer_push_constant_layout),
        gbuffer_fb_info
    );

    nvrhi::ShaderHandle lighting_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kFullscreenTriangleVertSpv, std::size(kFullscreenTriangleVertSpv));
    nvrhi::ShaderHandle lighting_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kLightingFragSpv, std::size(kLightingFragSpv));

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it.
    m_lighting_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                        .setVisibility(nvrhi::ShaderType::Pixel)
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                                                                        .addItem(nvrhi::BindingLayoutItem::Sampler(2))
                                                                        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3))
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(5))
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(6)) // shadow cascade array
                                                                        .addItem(nvrhi::BindingLayoutItem::Sampler(7))     // comparison sampler
                                                                        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));

    // Separate descriptor set (b0) for the per-frame uniforms -- too big for a push constant with the
    // cascade matrices. Kept apart from the SRV set so b0 doesn't collide with t0, same as the g-buffer.
    m_lighting_cb_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Pixel)
            .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
            .setBindingOffsets(nvrhi::VulkanBindingOffsets().setConstantBufferOffset(0))
    );

    nvrhi::BufferDesc lighting_cb_desc{};
    lighting_cb_desc.byteSize = sizeof(LightingUniforms);
    lighting_cb_desc.isConstantBuffer = true;
    lighting_cb_desc.isVolatile = true;
    lighting_cb_desc.maxVersions = kFramesInFlight + 2;
    lighting_cb_desc.debugName = "lighting uniforms";
    m_lighting_cb = nvrhi_device->createBuffer(lighting_cb_desc);

    nvrhi::FramebufferInfo lighting_fb_info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA16_FLOAT);

    m_lighting_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(lighting_vs)
            .setPixelShader(lighting_fs)
            .setRenderState(opaqueNoDepth())
            .addBindingLayout(m_lighting_binding_layout)
            .addBindingLayout(m_lighting_cb_layout),
        lighting_fb_info
    );

    m_lighting_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());
    m_lighting_cb_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_lighting_cb)), m_lighting_cb_layout
    );

    // Sky pass: fullscreen gradient into m_hdr_color, reading only g-buffer depth (discards on geometry).
    nvrhi::ShaderHandle sky_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kSkyFragSpv, std::size(kSkyFragSpv));

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it.
    m_sky_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                 .setVisibility(nvrhi::ShaderType::Pixel)
                                                                 .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                 .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                                 .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));

    m_sky_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Pixel).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(SkyPushConstants)))
    );

    m_sky_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(lighting_vs) // shared fullscreen-triangle vertex shader
            .setPixelShader(sky_fs)
            .setRenderState(opaqueNoDepth())
            .addBindingLayout(m_sky_binding_layout)
            .addBindingLayout(m_sky_push_constant_layout),
        lighting_fb_info
    );

    m_sky_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());
    m_sky_push_constant_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(SkyPushConstants))), m_sky_push_constant_layout
    );

    // Placeholder for slot 3 -- never read (light_count is 0 whenever this is bound).
    nvrhi::BufferDesc empty_light_desc{};
    empty_light_desc.byteSize = sizeof(Eigen::Vector4f);
    empty_light_desc.structStride = sizeof(Eigen::Vector4f);
    empty_light_desc.initialState = nvrhi::ResourceStates::ShaderResource;
    empty_light_desc.keepInitialState = true;
    empty_light_desc.debugName = "empty light buffer";
    m_empty_light_buffer = device.createBuffer(empty_light_desc);

    m_tonemap_pass = m_post_process.addPass(kTonemapFragSpv, std::size(kTonemapFragSpv), sizeof(TonemapPushConstants));
    m_fxaa_pass = m_post_process.addPass(kFxaaFragSpv, std::size(kFxaaFragSpv), sizeof(FxaaPushConstants));
    rebuildPostProcessTargets(); // also seeds the FXAA push constants for the current extent
    buildShadowResources();      // must precede rebuildLightingBindingSet -- that set binds m_shadow_map
    rebuildLightingBindingSet();
    rebuildSkyBindingSet();

    constexpr f32 kDefaultExposure = 0.003f; // real Blender-exported intensities are huge; expect to tune this per scene
    setExposure(kDefaultExposure);

    setSkybox(Skybox{}); // seed m_sky_* from the canonical default in skybox.hpp
}

void Renderer::rebuildSwapchainSurfaces() {
    m_swapchain_surfaces.clear();
    for (u32 i = 0; i < m_swapchain->imageCount(); ++i) {
        m_swapchain_surfaces.push_back(Surface::fromSwapchainImage(*m_device, m_swapchain->texture(i), m_swapchain->extent()));
    }
}

void Renderer::rebuildGBuffer() {
    VkExtent2D extent = m_swapchain->extent();
    m_albedo = createGBufferTexture(*m_device, extent, nvrhi::Format::SRGBA8_UNORM, nvrhi::ResourceStates::ShaderResource, "gbuffer albedo");
    m_normal = createGBufferTexture(*m_device, extent, nvrhi::Format::RG16_FLOAT, nvrhi::ResourceStates::ShaderResource, "gbuffer normal (octahedral)");
    m_material = createGBufferTexture(*m_device, extent, nvrhi::Format::RGBA8_UNORM, nvrhi::ResourceStates::ShaderResource, "gbuffer material");
    m_depth = createGBufferTexture(*m_device, extent, nvrhi::Format::D32, nvrhi::ResourceStates::DepthWrite, "gbuffer depth");

    m_gbuffer_framebuffer = m_device->nvrhi()->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_albedo).addColorAttachment(m_normal).addColorAttachment(m_material).setDepthAttachment(m_depth)
    );
}

void Renderer::rebuildLightingBindingSet() {
    m_lighting_binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_albedo))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_normal))
            .addItem(nvrhi::BindingSetItem::Sampler(2, m_lighting_sampler))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_light_buffer ? m_light_buffer : m_empty_light_buffer.Get()))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(4, m_depth))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(5, m_material))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(6, m_shadow_map))
            .addItem(nvrhi::BindingSetItem::Sampler(7, m_shadow_cmp_sampler)),
        m_lighting_binding_layout
    );
}

void Renderer::rebuildSkyBindingSet() {
    m_sky_binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_depth))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_sky_sampler)),
        m_sky_binding_layout
    );
}

void Renderer::buildShadowResources() {
    nvrhi::IDevice* nvrhi_device = m_device->nvrhi();

    nvrhi::TextureDesc shadow_desc{};
    shadow_desc.width = kShadowMapResolution;
    shadow_desc.height = kShadowMapResolution;
    shadow_desc.arraySize = kCascadeCount;
    shadow_desc.dimension = nvrhi::TextureDimension::Texture2DArray;
    shadow_desc.format = nvrhi::Format::D32;
    shadow_desc.isRenderTarget = true;
    shadow_desc.isShaderResource = true;
    shadow_desc.initialState = nvrhi::ResourceStates::DepthWrite;
    shadow_desc.keepInitialState = true;
    shadow_desc.debugName = "shadow map (D32 cascade array)";
    m_shadow_map = m_device->createTexture(shadow_desc);

    for (u32 c = 0; c < kCascadeCount; ++c) {
        m_shadow_framebuffers[c] = nvrhi_device->createFramebuffer(
            nvrhi::FramebufferDesc().setDepthAttachment(m_shadow_map, nvrhi::TextureSubresourceSet().setArraySlices(c, 1))
        );
    }

    // Depth-only caster pipelines: one per mesh kind, mirroring the g-buffer split.
    nvrhi::VertexAttributeDesc shadow_attrs[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, position)).setElementStride(sizeof(Vertex)),
    };
    nvrhi::VertexAttributeDesc shadow_instanced_attrs[] = {
        shadow_attrs[0],
        nvrhi::VertexAttributeDesc()
            .setName("INSTANCE_TRANSFORM")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setArraySize(4)
            .setBufferIndex(1)
            .setOffset(0)
            .setElementStride(sizeof(InstanceData))
            .setIsInstanced(true),
    };

    nvrhi::ShaderHandle shadow_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kShadowDepthVertSpv, std::size(kShadowDepthVertSpv));
    nvrhi::ShaderHandle shadow_instanced_vs =
        loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kShadowDepthInstancedVertSpv, std::size(kShadowDepthInstancedVertSpv));
    nvrhi::ShaderHandle shadow_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kShadowDepthFragSpv, std::size(kShadowDepthFragSpv));

    m_shadow_input_layout = nvrhi_device->createInputLayout(shadow_attrs, static_cast<u32>(std::size(shadow_attrs)), shadow_vs);
    m_shadow_instanced_input_layout =
        nvrhi_device->createInputLayout(shadow_instanced_attrs, static_cast<u32>(std::size(shadow_instanced_attrs)), shadow_instanced_vs);

    m_shadow_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Vertex).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ShadowPushConstants)))
    );
    m_shadow_push_constant_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(ShadowPushConstants))), m_shadow_push_constant_layout
    );

    nvrhi::RenderState shadow_state{};
    shadow_state.rasterState.setCullFront(); // cull front faces in the caster pass -- cheap acne reduction
    shadow_state.rasterState.setFrontCounterClockwise(true);
    shadow_state.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(nvrhi::ComparisonFunc::Less);

    nvrhi::FramebufferInfo shadow_fb_info = nvrhi::FramebufferInfo().setDepthFormat(nvrhi::Format::D32);

    m_shadow_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setInputLayout(m_shadow_input_layout)
            .setVertexShader(shadow_vs)
            .setPixelShader(shadow_fs)
            .setRenderState(shadow_state)
            .addBindingLayout(m_shadow_push_constant_layout),
        shadow_fb_info
    );
    m_shadow_instanced_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setInputLayout(m_shadow_instanced_input_layout)
            .setVertexShader(shadow_instanced_vs)
            .setPixelShader(shadow_fs)
            .setRenderState(shadow_state)
            .addBindingLayout(m_shadow_push_constant_layout),
        shadow_fb_info
    );

    // Fullscreen debug blit of slice 0 to the swapchain.
    nvrhi::ShaderHandle fullscreen_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kFullscreenTriangleVertSpv, std::size(kFullscreenTriangleVertSpv));
    nvrhi::ShaderHandle shadow_debug_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kShadowDebugFragSpv, std::size(kShadowDebugFragSpv));

    m_shadow_debug_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());
    m_shadow_cmp_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc().setReductionType(nvrhi::SamplerReductionType::Comparison));
    m_shadow_debug_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                          .setVisibility(nvrhi::ShaderType::Pixel)
                                                                          .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                          .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                                          .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(u32)))
                                                                          .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));

    m_shadow_debug_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(fullscreen_vs)
            .setPixelShader(shadow_debug_fs)
            .setRenderState(opaqueNoDepth())
            .addBindingLayout(m_shadow_debug_binding_layout),
        nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::BGRA8_UNORM)
    );
    m_shadow_debug_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_shadow_map))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_shadow_debug_sampler))
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(u32))),
        m_shadow_debug_binding_layout
    );

    m_cascade_view_proj.fill(Eigen::Matrix4f::Identity()); // real values computed per frame in renderMesh
}

void Renderer::rebuildPostProcessTargets() {
    VkExtent2D extent = m_swapchain->extent();
    m_hdr_color = Surface::createOffscreen(*m_device, extent, nvrhi::Format::RGBA16_FLOAT);
    m_post_process.rebuild(extent, nvrhi::Format::BGRA8_UNORM);
    updateFxaaPushConstants();
}

void Renderer::updateFxaaPushConstants() {
    VkExtent2D extent = m_swapchain->extent();
    FxaaPushConstants push{
        Eigen::Vector2f(1.0f / static_cast<f32>(extent.width), 1.0f / static_cast<f32>(extent.height)),
        m_fxaa_enabled ? 1.0f : 0.0f
    };
    m_post_process.setPushConstants(m_fxaa_pass, &push, sizeof(push));
}

void Renderer::beginFrame() {
    m_camera_uploaded = false;
    m_draw_list.clear();

    // Pace the CPU: block only if it is already kFramesInFlight ahead of the GPU.
    const u32 slot = static_cast<u32>(m_frame_counter % kFramesInFlight);
    if (m_frame_counter >= kFramesInFlight) {
        m_device->nvrhi()->waitEventQuery(m_frame_done[slot]);
    }

    m_acquired = m_swapchain->acquire();
    if (!m_acquired.ok) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
        rebuildPostProcessTargets();
        rebuildLightingBindingSet();
        rebuildSkyBindingSet();
        return;
    }

    m_frame_lock = m_device->lockSubmit();
    m_cmd->open();

    // Must clear every frame -- an uncleared depth buffer fails the depth test against stale VRAM contents.
    m_cmd->clearTextureFloat(m_albedo, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
    m_cmd->clearTextureFloat(m_normal, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_cmd->clearTextureFloat(m_material, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_cmd->clearDepthStencilTexture(m_depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
    m_cmd->clearDepthStencilTexture(m_shadow_map, nvrhi::AllSubresources, true, 1.0f, false, 0);
}

template <Mesh T>
void Renderer::renderMesh(T& mesh, const Material& material, const Eigen::Matrix4f& model, const Eigen::AlignedBox3f& world_bounds,
                          bool camera_visible, const Camera& camera) {
    if constexpr (requires { mesh.uploadIfDirty(*m_device); }) {
        mesh.uploadIfDirty(*m_device);
    }

    if (!m_acquired.ok) {
        return;
    }

    m_view_position = camera.position();
    m_inverse_view_projection = camera.inverseViewProjectionMatrix();
    m_view = camera.transformationMatrix();

    // Per-frame setup, done on the first mesh: the shared view-projection CB and the shadow cascades
    // (both need the live camera, which endFrame's passes don't have).
    if (!m_camera_uploaded) {
        const Eigen::Matrix4f view_projection = camera.viewProjectionMatrix();
        m_cmd->writeBuffer(m_camera_buffer, view_projection.data(), sizeof(view_projection));

        const CascadeSetup cascades = computeCascades(
            camera, m_sun_direction, m_shadow_distance, m_cascade_split_lambda, static_cast<f32>(kShadowMapResolution)
        );
        m_cascade_view_proj = cascades.view_proj;
        m_cascade_split_depth = cascades.split_view_depth;

        m_camera_uploaded = true;
    }

    DrawItem item{};
    item.vertex = mesh.vertexBuffer();
    item.index = mesh.indexBuffer();
    item.index_count = mesh.indexCount();
    item.instance_count = mesh.instanceCount();
    if constexpr (requires { mesh.instanceBuffer(); }) {
        item.instance = mesh.instanceBuffer();
    }
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
    for (u32 c = 0; c < kCascadeCount; ++c) {
        const Eigen::Matrix4f& cascade_vp = m_cascade_view_proj[c];
        const Frustum light_frustum = Frustum::fromViewProjection(cascade_vp);

        for (const DrawItem& item : m_draw_list) {
            // Instanced items have only an identity-instance world_bounds, so they can't be culled yet.
            if (item.instance == nullptr && !light_frustum.intersects(item.world_bounds)) {
                continue;
            }

            nvrhi::GraphicsState state{};
            state.framebuffer = m_shadow_framebuffers[c];
            state.viewport.addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<f32>(kShadowMapResolution), static_cast<f32>(kShadowMapResolution))
            );
            if (item.instance != nullptr) {
                state.pipeline = m_shadow_instanced_pipeline;
                state.vertexBuffers = {
                    nvrhi::VertexBufferBinding().setBuffer(item.vertex).setSlot(0).setOffset(0),
                    nvrhi::VertexBufferBinding().setBuffer(item.instance).setSlot(1).setOffset(0),
                };
            } else {
                state.pipeline = m_shadow_pipeline;
                state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(item.vertex).setSlot(0).setOffset(0)};
            }
            state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(item.index).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
            state.bindings = {m_shadow_push_constant_binding_set};

            ShadowPushConstants push{cascade_vp, item.model};
            m_cmd->setGraphicsState(state);
            m_cmd->setPushConstants(&push, sizeof(push));
            m_cmd->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.index_count).setInstanceCount(item.instance_count));
        }
    }
}

void Renderer::recordGBufferPass() {
    VkExtent2D extent = m_swapchain->extent();
    for (const DrawItem& item : m_draw_list) {
        if (!item.camera_visible) {
            continue; // shadow-cast only
        }

        nvrhi::GraphicsState state{};
        state.framebuffer = m_gbuffer_framebuffer;
        state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
        if (item.instance != nullptr) {
            state.pipeline = m_gbuffer_instanced_pipeline;
            state.vertexBuffers = {
                nvrhi::VertexBufferBinding().setBuffer(item.vertex).setSlot(0).setOffset(0),
                nvrhi::VertexBufferBinding().setBuffer(item.instance).setSlot(1).setOffset(0),
            };
        } else {
            state.pipeline = m_gbuffer_pipeline;
            state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(item.vertex).setSlot(0).setOffset(0)};
        }
        state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(item.index).setFormat(nvrhi::Format::R32_UINT).setOffset(0);

        nvrhi::IBindingSet* texture_binding_set =
            item.material->gbufferBindingSet(m_device->nvrhi(), m_gbuffer_texture_binding_layout, m_white_texture, m_gbuffer_sampler);
        state.bindings = {texture_binding_set, m_gbuffer_push_constant_binding_set};

        GBufferPushConstants push{item.model, item.normal_matrix};
        m_cmd->setGraphicsState(state);
        m_cmd->setPushConstants(&push, sizeof(push));
        m_cmd->drawIndexed(nvrhi::DrawArguments().setVertexCount(item.index_count).setInstanceCount(item.instance_count));
    }
}

void Renderer::setLights(const LightManager& lights) {
    m_light_count = lights.count();
    if (const Light* sun = lights.directionalLight()) {
        m_sun_direction = sun->direction;
    }
    if (lights.buffer() != m_light_buffer) {
        m_light_buffer = lights.buffer();
        rebuildLightingBindingSet(); // light buffer is the one moving resource in that set
    }
}

void Renderer::setSkybox(const Skybox& skybox) {
    m_sky_intensity = skybox.intensity;
    if (const auto* gradient = std::get_if<GradientSky>(&skybox.source)) {
        m_sky_kind = 0;
        m_sky_zenith = gradient->zenith;
        m_sky_ground = gradient->ground;
    } else {
        m_sky_kind = 1; // CubemapSky -- sampler binding not wired yet; gradient stash stays as-is
    }
}

void Renderer::setExposure(f32 exposure) {
    m_post_process.setPushConstants(m_tonemap_pass, &exposure, sizeof(exposure));
}

void Renderer::setAmbient(const Eigen::Vector3f& ambient) {
    m_ambient = ambient;
}

void Renderer::setFxaaEnabled(bool enabled) {
    m_fxaa_enabled = enabled;
    updateFxaaPushConstants();
}

void Renderer::setShadowDistance(f32 distance) {
    m_shadow_distance = distance;
}

void Renderer::setCascadeSplitLambda(f32 lambda) {
    m_cascade_split_lambda = std::clamp(lambda, 0.0f, 1.0f);
}

void Renderer::toggleShadowMapDebug() {
    m_show_shadow_map_debug = !m_show_shadow_map_debug;
}

void Renderer::cycleShadowMapDebugLayer() {
    m_shadow_debug_layer = (m_shadow_debug_layer + 1) % kCascadeCount;
}

void Renderer::endFrame() {
    if (!m_acquired.ok) {
        return; // beginFrame already recreated the swapchain; nothing to submit
    }

    recordShadowPass();
    recordGBufferPass();

    const Surface& target = m_swapchain_surfaces[m_acquired.image_index];

    VkExtent2D extent = m_swapchain->extent();

    LightingUniforms lighting_uniforms{};
    lighting_uniforms.inverse_view_projection = m_inverse_view_projection;
    lighting_uniforms.view = m_view;
    lighting_uniforms.cascade_view_proj = m_cascade_view_proj;
    lighting_uniforms.view_position = Eigen::Vector4f(m_view_position.x(), m_view_position.y(), m_view_position.z(), 1.0f);
    lighting_uniforms.ambient = Eigen::Vector4f(m_ambient.x(), m_ambient.y(), m_ambient.z(), 0.0f);
    lighting_uniforms.cascade_split_depth =
        Eigen::Vector4f(m_cascade_split_depth[0], m_cascade_split_depth[1], m_cascade_split_depth[2], m_cascade_split_depth[3]);
    lighting_uniforms.shadow_params = Eigen::Vector4f(m_shadow_depth_bias, m_shadow_normal_offset, 0.0f, 0.0f);
    lighting_uniforms.light_count = m_light_count;
    m_cmd->writeBuffer(m_lighting_cb, &lighting_uniforms, sizeof(lighting_uniforms));

    nvrhi::GraphicsState state{};
    state.pipeline = m_lighting_pipeline;
    state.framebuffer = m_hdr_color.framebuffer();
    state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    state.bindings = {m_lighting_binding_set, m_lighting_cb_binding_set};

    m_cmd->setGraphicsState(state);
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));

    // Sky fills the far-plane pixels of m_hdr_color; the shader discards where geometry was drawn.
    nvrhi::GraphicsState sky_state{};
    sky_state.pipeline = m_sky_pipeline;
    sky_state.framebuffer = m_hdr_color.framebuffer();
    sky_state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    sky_state.bindings = {m_sky_binding_set, m_sky_push_constant_binding_set};

    m_cmd->setGraphicsState(sky_state);
    SkyPushConstants sky_push{
        m_inverse_view_projection,
        Eigen::Vector4f(m_view_position.x(), m_view_position.y(), m_view_position.z(), 1.0f),
        Eigen::Vector4f(m_sky_zenith.x(), m_sky_zenith.y(), m_sky_zenith.z(), m_sky_intensity),
        Eigen::Vector4f(m_sky_ground.x(), m_sky_ground.y(), m_sky_ground.z(), 0.0f),
        m_sky_kind
    };
    m_cmd->setPushConstants(&sky_push, sizeof(sky_push));
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));

    m_post_process.run(m_cmd, m_hdr_color.texture(), target);

    if (m_show_shadow_map_debug) {
        nvrhi::GraphicsState debug_state{};
        debug_state.pipeline = m_shadow_debug_pipeline;
        debug_state.framebuffer = target.framebuffer();
        debug_state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
        debug_state.bindings = {m_shadow_debug_binding_set};
        m_cmd->setGraphicsState(debug_state);
        m_cmd->setPushConstants(&m_shadow_debug_layer, sizeof(m_shadow_debug_layer));
        m_cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));
    }

    m_cmd->close();

    nvrhi::vulkan::IDevice* vk_device = m_device->vulkanDevice();
    vk_device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->imageAvailable(), 0);
    vk_device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->renderFinished(m_acquired.image_index), 0);
    m_device->nvrhi()->executeCommandList(m_cmd);

    // Signal this slot's fence instead of draining the GPU; beginFrame kFramesInFlight later waits on it.
    const u32 slot = static_cast<u32>(m_frame_counter % kFramesInFlight);
    m_device->nvrhi()->resetEventQuery(m_frame_done[slot]);
    m_device->nvrhi()->setEventQuery(m_frame_done[slot], nvrhi::CommandQueue::Graphics);
    m_frame_counter++;

    if (!m_swapchain->present(m_acquired.image_index)) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
        rebuildPostProcessTargets();
        rebuildLightingBindingSet();
        rebuildSkyBindingSet();
    }

    m_frame_lock.unlock();
}

} // namespace graphics
