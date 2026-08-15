#include <graphics/post_process_chain.hpp>

#include <cstring>

namespace {

constexpr u32 kFullscreenTriangleVertSpv[] =
#include "fullscreen_triangle.vert.h"
    ;

constexpr u32 kFullscreenTriangleVertexCount = 3;

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

PostProcessChain::PostProcessChain(const gpu::Device& device)
    : m_device(&device),
      // Placeholder 1x1; rebuild() replaces these once an extent is known.
      m_ping_pong{
          Surface::createOffscreen(device, VkExtent2D{1, 1}, nvrhi::Format::RGBA16_FLOAT),
          Surface::createOffscreen(device, VkExtent2D{1, 1}, nvrhi::Format::RGBA16_FLOAT)
      } {
    nvrhi::IDevice* nvrhi_device = device.nvrhi();

    m_fullscreen_vertex_shader = loadShader(nvrhi_device, nvrhi::ShaderType::Vertex, kFullscreenTriangleVertSpv, std::size(kFullscreenTriangleVertSpv));
    m_sampler = nvrhi_device->createSampler(nvrhi::SamplerDesc());

    // NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it.
    m_input_binding_layout = nvrhi_device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                                     .setVisibility(nvrhi::ShaderType::Pixel)
                                                                     .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                                     .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                                     .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0)));
}

u32 PostProcessChain::addPass(const u32* fragment_spirv, usize word_count, usize push_constant_size) {
    m_pass_descs.push_back(PassDesc{fragment_spirv, word_count, push_constant_size});
    return static_cast<u32>(m_pass_descs.size() - 1);
}

void PostProcessChain::rebuild(VkExtent2D extent, nvrhi::Format swapchain_format) {
    nvrhi::IDevice* nvrhi_device = m_device->nvrhi();

    m_ping_pong[0] = Surface::createOffscreen(*m_device, extent, nvrhi::Format::RGBA16_FLOAT);
    m_ping_pong[1] = Surface::createOffscreen(*m_device, extent, nvrhi::Format::RGBA16_FLOAT);

    m_passes.clear();
    m_passes.reserve(m_pass_descs.size());

    for (usize i = 0; i < m_pass_descs.size(); i++) {
        const PassDesc& desc = m_pass_descs[i];
        bool is_last = (i == m_pass_descs.size() - 1);

        nvrhi::ShaderHandle fragment_shader = loadShader(nvrhi_device, nvrhi::ShaderType::Pixel, desc.fragment_spirv, desc.word_count);

        nvrhi::FramebufferInfo fb_info = nvrhi::FramebufferInfo().addColorFormat(is_last ? swapchain_format : nvrhi::Format::RGBA16_FLOAT);

        Pass pass;
        pass.push_constant_size = desc.push_constant_size;
        pass.push_constant_data.resize(desc.push_constant_size);

        nvrhi::GraphicsPipelineDesc pipeline_desc = nvrhi::GraphicsPipelineDesc()
                                                         .setVertexShader(m_fullscreen_vertex_shader)
                                                         .setPixelShader(fragment_shader)
                                                         .setRenderState(opaqueNoDepth())
                                                         .addBindingLayout(m_input_binding_layout);

        if (desc.push_constant_size > 0) {
            pass.push_constant_layout = nvrhi_device->createBindingLayout(
                nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::Pixel).addItem(nvrhi::BindingLayoutItem::PushConstants(0, desc.push_constant_size))
            );
            pass.push_constant_binding_set = nvrhi_device->createBindingSet(
                nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, desc.push_constant_size)), pass.push_constant_layout
            );
            pipeline_desc.addBindingLayout(pass.push_constant_layout);
        }

        pass.pipeline = nvrhi_device->createGraphicsPipeline(pipeline_desc, fb_info);
        m_passes.push_back(std::move(pass));
    }
}

void PostProcessChain::setPushConstants(u32 pass_index, const void* data, usize size) {
    Pass& pass = m_passes[pass_index];
    if (pass.push_constant_size == 0) {
        return;
    }
    std::memcpy(pass.push_constant_data.data(), data, size);
}

void PostProcessChain::run(nvrhi::ICommandList* cmd, nvrhi::ITexture* hdr_input, const Surface& swapchain_target) {
    VkExtent2D extent = swapchain_target.extent();

    for (usize i = 0; i < m_passes.size(); i++) {
        Pass& pass = m_passes[i];
        bool is_last = (i == m_passes.size() - 1);

        nvrhi::ITexture* input = (i == 0) ? hdr_input : m_ping_pong[(i - 1) % 2].texture();
        nvrhi::IFramebuffer* output_fb = is_last ? swapchain_target.framebuffer() : m_ping_pong[i % 2].framebuffer();

        nvrhi::BindingSetHandle input_binding_set = m_device->nvrhi()->createBindingSet(
            nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::Texture_SRV(0, input)).addItem(nvrhi::BindingSetItem::Sampler(1, m_sampler)),
            m_input_binding_layout
        );

        nvrhi::GraphicsState state{};
        state.pipeline = pass.pipeline;
        state.framebuffer = output_fb;
        state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<f32>(extent.width), static_cast<f32>(extent.height)));
        state.bindings = {input_binding_set};
        if (pass.push_constant_size > 0) {
            state.bindings.push_back(pass.push_constant_binding_set);
        }

        cmd->setGraphicsState(state);
        if (pass.push_constant_size > 0) {
            cmd->setPushConstants(pass.push_constant_data.data(), pass.push_constant_size);
        }
        cmd->draw(nvrhi::DrawArguments().setVertexCount(kFullscreenTriangleVertexCount));
    }
}

} // namespace graphics
