#include <graphics/renderer.hpp>

#include <cstddef>
#include <optional>

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
    Eigen::Matrix4f view_projection;
};

struct LightingPushConstants {
    Eigen::Matrix4f inverse_view_projection;
    Eigen::Vector4f view_position;
    Eigen::Vector4f ambient; // rgb = flat ambient irradiance (pre-tonemap)
    u32 light_count;
};

struct SkyPushConstants {
    Eigen::Matrix4f inverse_view_projection;
    Eigen::Vector4f view_position;
    Eigen::Vector4f zenith_intensity; // rgb = zenith, a = intensity
    Eigen::Vector4f ground;           // rgb = ground
    u32 kind;                         // 0 = gradient, 1 = cubemap (unused today)
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
                                                  .addColorFormat(nvrhi::Format::RGBA32_FLOAT)
                                                  .addColorFormat(nvrhi::Format::RGBA8_UNORM)
                                                  .setDepthFormat(nvrhi::Format::D32);

    nvrhi::RenderState gbuffer_state{};
    // Matches camera.cpp's projection parity (no Y-flip there).
    gbuffer_state.rasterState.setFrontCounterClockwise(true);

    nvrhi::BindingLayoutHandle gbuffer_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Vertex).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GBufferPushConstants)))
    );

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
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GBufferPushConstants))), gbuffer_push_constant_layout
    );

    // Instanced geometry pass: same shader I/O plus a per-instance mat4 at buffer slot 1.
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
            .setElementStride(sizeof(Eigen::Matrix4f))
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
                                                                        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));

    m_lighting_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Pixel).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(LightingPushConstants)))
    );

    nvrhi::FramebufferInfo lighting_fb_info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA16_FLOAT);

    m_lighting_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(lighting_vs)
            .setPixelShader(lighting_fs)
            .setRenderState(opaqueNoDepth())
            .addBindingLayout(m_lighting_binding_layout)
            .addBindingLayout(m_lighting_push_constant_layout),
        lighting_fb_info
    );

    m_lighting_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());
    m_lighting_push_constant_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(LightingPushConstants))), m_lighting_push_constant_layout
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
    m_normal = createGBufferTexture(*m_device, extent, nvrhi::Format::RGBA32_FLOAT, nvrhi::ResourceStates::ShaderResource, "gbuffer normal");
    m_material = createGBufferTexture(*m_device, extent, nvrhi::Format::RGBA8_UNORM, nvrhi::ResourceStates::ShaderResource, "gbuffer material");
    m_depth = createGBufferTexture(*m_device, extent, nvrhi::Format::D32, nvrhi::ResourceStates::DepthWrite, "gbuffer depth");

    m_gbuffer_framebuffer = m_device->nvrhi()->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_albedo).addColorAttachment(m_normal).addColorAttachment(m_material).setDepthAttachment(m_depth)
    );
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
    m_acquired = m_swapchain->acquire();
    if (!m_acquired.ok) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
        rebuildPostProcessTargets();
        return;
    }

    m_frame_lock = m_device->lockSubmit();
    m_cmd->open();

    // Must clear every frame -- an uncleared depth buffer fails the depth test against stale VRAM contents.
    m_cmd->clearTextureFloat(m_albedo, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
    m_cmd->clearTextureFloat(m_normal, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_cmd->clearTextureFloat(m_material, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_cmd->clearDepthStencilTexture(m_depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
}

template <Mesh T>
void Renderer::renderMesh(T& mesh, const Material& material, const Eigen::Matrix4f& model, const Camera& camera) {
    if constexpr (requires { mesh.uploadIfDirty(*m_device); }) {
        mesh.uploadIfDirty(*m_device);
    }

    if (!m_acquired.ok) {
        return;
    }

    m_view_position = camera.position();
    m_inverse_view_projection = camera.inverseViewProjectionMatrix();
    GBufferPushConstants push{model, camera.viewProjectionMatrix()};

    VkExtent2D extent = m_swapchain->extent();

    nvrhi::GraphicsState state{};
    state.framebuffer = m_gbuffer_framebuffer;
    state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    if constexpr (requires { mesh.instanceBuffer(); }) {
        state.pipeline = m_gbuffer_instanced_pipeline;
        state.vertexBuffers = {
            nvrhi::VertexBufferBinding().setBuffer(mesh.vertexBuffer()).setSlot(0).setOffset(0),
            nvrhi::VertexBufferBinding().setBuffer(mesh.instanceBuffer()).setSlot(1).setOffset(0),
        };
    } else {
        state.pipeline = m_gbuffer_pipeline;
        state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(mesh.vertexBuffer()).setSlot(0).setOffset(0)};
    }
    state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(mesh.indexBuffer()).setFormat(nvrhi::Format::R32_UINT).setOffset(0);

    const std::optional<Texture>& base_color = material.baseColorTexture();
    const std::optional<Texture>& metallic_roughness = material.metallicRoughnessTexture();
    nvrhi::BindingSetHandle texture_binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, base_color ? base_color->texture() : m_white_texture.Get()))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, metallic_roughness ? metallic_roughness->texture() : m_white_texture.Get()))
            .addItem(nvrhi::BindingSetItem::Sampler(2, m_gbuffer_sampler))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, material.factorsBuffer())),
        m_gbuffer_texture_binding_layout
    );
    state.bindings = {texture_binding_set, m_gbuffer_push_constant_binding_set};

    m_cmd->setGraphicsState(state);
    m_cmd->setPushConstants(&push, sizeof(push));
    m_cmd->drawIndexed(nvrhi::DrawArguments().setVertexCount(mesh.indexCount()).setInstanceCount(mesh.instanceCount()));
}

template void Renderer::renderMesh<StandardMesh>(StandardMesh&, const Material&, const Eigen::Matrix4f&, const Camera&);
template void Renderer::renderMesh<InstancedMesh>(InstancedMesh&, const Material&, const Eigen::Matrix4f&, const Camera&);

void Renderer::setLights(const LightManager& lights) {
    m_light_buffer = lights.buffer();
    m_light_count = lights.count();
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

void Renderer::endFrame() {
    if (!m_acquired.ok) {
        return; // beginFrame already recreated the swapchain; nothing to submit
    }

    const Surface& target = m_swapchain_surfaces[m_acquired.image_index];

    nvrhi::BindingSetHandle lighting_binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_albedo))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_normal))
            .addItem(nvrhi::BindingSetItem::Sampler(2, m_lighting_sampler))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_light_buffer ? m_light_buffer : m_empty_light_buffer.Get()))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(4, m_depth))
            .addItem(nvrhi::BindingSetItem::Texture_SRV(5, m_material)),
        m_lighting_binding_layout
    );

    VkExtent2D extent = m_swapchain->extent();

    nvrhi::GraphicsState state{};
    state.pipeline = m_lighting_pipeline;
    state.framebuffer = m_hdr_color.framebuffer();
    state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    state.bindings = {lighting_binding_set, m_lighting_push_constant_binding_set};

    m_cmd->setGraphicsState(state);
    LightingPushConstants lighting_push{
        m_inverse_view_projection,
        Eigen::Vector4f(m_view_position.x(), m_view_position.y(), m_view_position.z(), 1.0f),
        Eigen::Vector4f(m_ambient.x(), m_ambient.y(), m_ambient.z(), 0.0f),
        m_light_count
    };
    m_cmd->setPushConstants(&lighting_push, sizeof(lighting_push));
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));

    // Sky fills the far-plane pixels of m_hdr_color; the shader discards where geometry was drawn.
    nvrhi::BindingSetHandle sky_binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_depth))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_sky_sampler)),
        m_sky_binding_layout
    );

    nvrhi::GraphicsState sky_state{};
    sky_state.pipeline = m_sky_pipeline;
    sky_state.framebuffer = m_hdr_color.framebuffer();
    sky_state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    sky_state.bindings = {sky_binding_set, m_sky_push_constant_binding_set};

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

    m_cmd->close();

    nvrhi::vulkan::IDevice* vk_device = m_device->vulkanDevice();
    vk_device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->imageAvailable(), 0);
    vk_device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->renderFinished(m_acquired.image_index), 0);
    m_device->nvrhi()->executeCommandList(m_cmd);

    vkDeviceWaitIdle(m_device->handle()); // no frames-in-flight pacing yet

    if (!m_swapchain->present(m_acquired.image_index)) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
        rebuildPostProcessTargets();
    }

    m_frame_lock.unlock();
}

} // namespace graphics
