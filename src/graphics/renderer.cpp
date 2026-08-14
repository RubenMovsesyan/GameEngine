#include <graphics/renderer.hpp>

#include <cstddef>

namespace {

constexpr u32 kGBufferVertSpv[] =
#include "gbuffer.vert.h"
    ;
constexpr u32 kGBufferFragSpv[] =
#include "gbuffer.frag.h"
    ;
constexpr u32 kLightingVertSpv[] =
#include "lighting.vert.h"
    ;
constexpr u32 kLightingFragSpv[] =
#include "lighting.frag.h"
    ;

// lighting.vert derives a full-screen-covering triangle purely from gl_VertexIndex (0,
// 1, 2) -- no vertex/index buffer, so this is the entire "mesh" for that pass.
constexpr u32 kFullscreenTriangleVertexCount = 3;

// model kept separate from view_projection -- normals need model's rotation, not a
// clip-space-projected matrix. view_position isn't needed here; it moved to the
// lighting pass instead (see LightingPushConstants).
struct GBufferPushConstants {
    Eigen::Matrix4f model;
    Eigen::Matrix4f view_projection;
};

// Not read by the lighting math yet -- reserved for view-dependent shading (specular)
// once that exists.
struct LightingPushConstants {
    Eigen::Vector4f view_position;
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

Renderer::Renderer(const gpu::Device& device, gpu::Swapchain& swapchain) : m_device(&device), m_swapchain(&swapchain) {
    m_cmd = device.nvrhi()->createCommandList();
    rebuildSwapchainSurfaces();
    rebuildGBuffer();

    nvrhi::IDevice* nvrhi_device = device.nvrhi();

    // Geometry (g_buffer) pass: writes albedo + depth from a Mesh's vertex/index buffers.
    nvrhi::VertexAttributeDesc mesh_attrs[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, position)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, color)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, normal)).setElementStride(sizeof(Vertex)),
    };

    nvrhi::ShaderHandle gbuffer_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kGBufferVertSpv, std::size(kGBufferVertSpv));
    nvrhi::ShaderHandle gbuffer_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kGBufferFragSpv, std::size(kGBufferFragSpv));
    m_mesh_input_layout = nvrhi_device->createInputLayout(mesh_attrs, static_cast<u32>(std::size(mesh_attrs)), gbuffer_vs);

    nvrhi::FramebufferInfo gbuffer_fb_info = nvrhi::FramebufferInfo()
                                                  .addColorFormat(nvrhi::Format::SRGBA8_UNORM)
                                                  .addColorFormat(nvrhi::Format::RGBA32_FLOAT)
                                                  .setDepthFormat(nvrhi::Format::D32);

    nvrhi::RenderState gbuffer_state{};
    // Real mesh data with real winding, unlike the old single hardcoded triangle -- back-face culling is correct here.

    nvrhi::BindingLayoutHandle gbuffer_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Vertex).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GBufferPushConstants)))
    );

    m_gbuffer_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setInputLayout(m_mesh_input_layout)
            .setVertexShader(gbuffer_vs)
            .setPixelShader(gbuffer_fs)
            .setRenderState(gbuffer_state)
            .addBindingLayout(gbuffer_push_constant_layout),
        gbuffer_fb_info
    );
    m_gbuffer_push_constant_binding_set = nvrhi_device->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GBufferPushConstants))), gbuffer_push_constant_layout
    );

    // Lighting pass: fullscreen triangle, samples albedo+normal, N.L against a
    // hardcoded light direction.
    nvrhi::ShaderHandle lighting_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kLightingVertSpv, std::size(kLightingVertSpv));
    nvrhi::ShaderHandle lighting_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kLightingFragSpv, std::size(kLightingFragSpv));

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it so the
    // shader's plain `binding = 2` matches (see graphics::Surface-era discovery of this).
    m_lighting_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                        .setVisibility(nvrhi::ShaderType::Pixel)
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                                                                        .addItem(nvrhi::BindingLayoutItem::Sampler(2))
                                                                        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));

    m_lighting_push_constant_layout = nvrhi_device->createBindingLayout(
        nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Pixel).addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(LightingPushConstants)))
    );

    nvrhi::FramebufferInfo lighting_fb_info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::BGRA8_UNORM);

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
        nvrhi::FramebufferDesc().addColorAttachment(m_albedo).addColorAttachment(m_normal).setDepthAttachment(m_depth)
    );
}

void Renderer::beginFrame() {
    m_acquired = m_swapchain->acquire();
    if (!m_acquired.ok) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
        return;
    }

    m_cmd->open();

    // The G-buffer is never cleared otherwise -- without this, the depth test runs
    // against whatever garbage was already in that VRAM allocation, which can fail
    // every fragment and leave albedo untouched (a black screen with no errors to show
    // for it).
    m_cmd->clearTextureFloat(m_albedo, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
    m_cmd->clearTextureFloat(m_normal, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_cmd->clearDepthStencilTexture(m_depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
}

void Renderer::renderMesh(const Mesh& mesh, const Eigen::Matrix4f& model, const Camera& camera) {
    if (!m_acquired.ok) {
        return;
    }

    m_view_position = camera.position();
    GBufferPushConstants push{model, camera.viewProjectionMatrix()};

    VkExtent2D extent = m_swapchain->extent();

    nvrhi::GraphicsState state{};
    state.pipeline = m_gbuffer_pipeline;
    state.framebuffer = m_gbuffer_framebuffer;
    state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(mesh.vertexBuffer()).setSlot(0).setOffset(0)};
    state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(mesh.indexBuffer()).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
    state.bindings = {m_gbuffer_push_constant_binding_set};

    m_cmd->setGraphicsState(state);
    m_cmd->setPushConstants(&push, sizeof(push));
    m_cmd->drawIndexed(nvrhi::DrawArguments().setVertexCount(mesh.indexCount()));
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
            .addItem(nvrhi::BindingSetItem::Sampler(2, m_lighting_sampler)),
        m_lighting_binding_layout
    );

    VkExtent2D extent = m_swapchain->extent();

    nvrhi::GraphicsState state{};
    state.pipeline = m_lighting_pipeline;
    state.framebuffer = target.framebuffer();
    state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
    state.bindings = {lighting_binding_set, m_lighting_push_constant_binding_set};

    m_cmd->setGraphicsState(state);
    LightingPushConstants lighting_push{Eigen::Vector4f(m_view_position.x(), m_view_position.y(), m_view_position.z(), 1.0f)};
    m_cmd->setPushConstants(&lighting_push, sizeof(lighting_push));
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));

    m_cmd->close();

    nvrhi::vulkan::IDevice* vk_device = m_device->vulkanDevice();
    vk_device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->imageAvailable(), 0);
    vk_device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->renderFinished(m_acquired.image_index), 0);
    m_device->nvrhi()->executeCommandList(m_cmd);

    // ponytail: no frames-in-flight pacing yet -- see .old/gpu/frame.hpp for the
    // fence-per-slot ring that replaces this once perf matters.
    vkDeviceWaitIdle(m_device->handle());

    if (!m_swapchain->present(m_acquired.image_index)) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        rebuildGBuffer();
    }
}

} // namespace graphics
