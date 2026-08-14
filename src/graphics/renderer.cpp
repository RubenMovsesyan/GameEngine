#include <graphics/renderer.hpp>

namespace {
// glslc -mfmt=c emits a bare brace-enclosed word list, no declaration.
constexpr u32 kTriangleVertSpv[] =
#include "triangle.vert.h"
    ;
constexpr u32 kTriangleFragSpv[] =
#include "triangle.frag.h"
    ;
constexpr u32 kQuadVertSpv[] =
#include "quad.vert.h"
    ;
constexpr u32 kQuadFragSpv[] =
#include "quad.frag.h"
    ;

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
} // namespace

namespace graphics {

Renderer::Renderer(const gpu::Device& device, gpu::Swapchain& swapchain)
    : m_device(&device), m_swapchain(&swapchain), m_offscreen(Surface::createOffscreen(device, VkExtent2D{512, 512})) {
    nvrhi::IDevice* nvrhi_device = device.nvrhi();
    m_cmd = nvrhi_device->createCommandList();

    // Every Surface in this engine is BGRA8_UNORM (see Surface::createOffscreen's
    // default and gpu::Swapchain), so one FramebufferInfo covers every target.
    nvrhi::FramebufferInfo fb_info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::BGRA8_UNORM);

    nvrhi::ShaderHandle triangle_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kTriangleVertSpv, std::size(kTriangleVertSpv));
    nvrhi::ShaderHandle triangle_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kTriangleFragSpv, std::size(kTriangleFragSpv));
    m_triangle_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc().setVertexShader(triangle_vs).setPixelShader(triangle_fs).setRenderState(opaqueNoDepth()), fb_info
    );

    nvrhi::ShaderHandle quad_vs = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kQuadVertSpv, std::size(kQuadVertSpv));
    nvrhi::ShaderHandle quad_fs = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, kQuadFragSpv, std::size(kQuadFragSpv));

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default (its D3D-style
    // binding model needs the room); zero it so the shader's plain `binding = 1` matches.
    m_quad_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                    .setVisibility(nvrhi::ShaderType::Pixel)
                                                                    .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                    .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                                    .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));
    m_quad_pipeline = nvrhi_device->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(quad_vs)
            .setPixelShader(quad_fs)
            .setRenderState(opaqueNoDepth())
            .addBindingLayout(m_quad_binding_layout),
        fb_info
    );

    m_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());

    rebuildSwapchainSurfaces();
}

void Renderer::rebuildSwapchainSurfaces() {
    m_swapchain_surfaces.clear();
    for (u32 i = 0; i < m_swapchain->imageCount(); ++i) {
        m_swapchain_surfaces.push_back(Surface::fromSwapchainImage(*m_device, m_swapchain->texture(i), m_swapchain->extent()));
    }
}

void Renderer::beginFrame() {
    m_acquired = m_swapchain->acquire();
    if (!m_acquired.ok) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
        return;
    }

    m_cmd->open();
}

void Renderer::renderTriangle(const Surface& target) {
    nvrhi::Viewport viewport{static_cast<f32>(target.extent().width), static_cast<f32>(target.extent().height)};

    nvrhi::GraphicsState state{};
    state.pipeline = m_triangle_pipeline;
    state.framebuffer = target.framebuffer();
    state.viewport.addViewportAndScissorRect(viewport);

    m_cmd->setGraphicsState(state);
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(3));
}

void Renderer::renderTexturedQuad(const Surface& target, nvrhi::ITexture* source) {
    nvrhi::BindingSetHandle binding_set = m_device->nvrhi()->createBindingSet(
        nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::Texture_SRV(0, source)).addItem(nvrhi::BindingSetItem::Sampler(1, m_sampler)),
        m_quad_binding_layout
    );

    nvrhi::Viewport viewport{static_cast<f32>(target.extent().width), static_cast<f32>(target.extent().height)};

    nvrhi::GraphicsState state{};
    state.pipeline = m_quad_pipeline;
    state.framebuffer = target.framebuffer();
    state.viewport.addViewportAndScissorRect(viewport);
    state.bindings = {binding_set};

    m_cmd->setGraphicsState(state);
    m_cmd->draw(nvrhi::DrawArguments().setVertexCount(6));
}

void Renderer::endFrame() {
    if (!m_acquired.ok) {
        return; // beginFrame already recreated the swapchain; nothing to submit
    }

    const Surface& target = m_swapchain_surfaces[m_acquired.image_index];

    renderTriangle(m_offscreen);
    renderTriangle(target);
    renderTexturedQuad(target, m_offscreen.texture());

    m_cmd->close();

    nvrhi::vulkan::IDevice* vk_device = m_device->vulkanDevice();
    vk_device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->imageAvailable(), 0);
    vk_device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_swapchain->renderFinished(m_acquired.image_index), 0);
    m_device->nvrhi()->executeCommandList(m_cmd);

    // ponytail: no frames-in-flight pacing yet -- without it, the CPU can race ahead
    // and resignal a swapchain semaphore whose prior wait hasn't completed on the GPU.
    // Full drain every frame is correct but leaves no overlap; a real fence-per-slot
    // ring (see .old/gpu/frame.hpp for the pattern) is the upgrade once perf matters.
    vkDeviceWaitIdle(m_device->handle());

    if (!m_swapchain->present(m_acquired.image_index)) {
        vkDeviceWaitIdle(m_device->handle());
        m_swapchain->recreate(m_swapchain->extent());
        rebuildSwapchainSurfaces();
    }
}

} // namespace graphics
