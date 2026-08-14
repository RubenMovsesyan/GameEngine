#include <graphics/renderer.hpp>

#include <gpu/command.hpp>

#include <array>
#include <span>
#include <utility>

namespace {

// glslc -mfmt=c emits a bare brace-enclosed word list, no declaration.
constexpr u32 kTriangleVertSpv[] =
#include "triangle.vert.h"
    ;
constexpr u32 kTriangleFragSpv[] =
#include "triangle.frag.h"
    ;

vk::raii::ShaderModule loadShader(const gpu::Device& device, std::span<const u32> spirv) {
    return vk::raii::ShaderModule{
        *device, vk::ShaderModuleCreateInfo{.codeSize = spirv.size_bytes(), .pCode = spirv.data()}
    };
}

// One hardcoded triangle: no vertex input, no descriptors, dynamic viewport/scissor so
// the pipeline never needs rebuilding when the swapchain resizes.
std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> buildTrianglePipeline(
    const gpu::Device& device, vk::Format color_format
) {
    const vk::raii::ShaderModule vert = loadShader(device, kTriangleVertSpv);
    const vk::raii::ShaderModule frag = loadShader(device, kTriangleFragSpv);

    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = *vert, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = *frag, .pName = "main"},
    };

    constexpr vk::PipelineVertexInputStateCreateInfo vertex_input{};
    constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly{.topology = vk::PrimitiveTopology::eTriangleList};
    constexpr vk::PipelineViewportStateCreateInfo viewport_state{.viewportCount = 1, .scissorCount = 1};
    constexpr vk::PipelineRasterizationStateCreateInfo rasterization{
        .cullMode = vk::CullModeFlagBits::eNone, // winding doesn't matter for one test triangle
        .lineWidth = 1.0f,
    };
    constexpr vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
    constexpr vk::PipelineColorBlendAttachmentState blend_attachment{
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blend{.attachmentCount = 1, .pAttachments = &blend_attachment};
    constexpr std::array<vk::DynamicState, 2> dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()), .pDynamicStates = dynamic_states.data()
    };
    const vk::PipelineRenderingCreateInfo rendering_info{.colorAttachmentCount = 1, .pColorAttachmentFormats = &color_format};

    vk::raii::PipelineLayout layout{*device, vk::PipelineLayoutCreateInfo{}};
    vk::raii::Pipeline pipeline{
        *device, nullptr,
        vk::GraphicsPipelineCreateInfo{
            .pNext = &rendering_info,
            .stageCount = static_cast<u32>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pColorBlendState = &color_blend,
            .pDynamicState = &dynamic_state,
            .layout = *layout,
        },
    };
    return {std::move(layout), std::move(pipeline)};
}

} // namespace

namespace ge::gfx {

Renderer::Renderer(const gpu::Device& device, vk::SurfaceKHR surface, vk::Extent2D extent)
    : m_device(&device), m_swapchain(device, surface, extent), m_frames(device, device.families().graphics) {
    std::tie(m_pipeline_layout, m_pipeline) = buildTrianglePipeline(device, m_swapchain.format());
}

void Renderer::drawFrame(vk::ClearColorValue clear, vk::Extent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return; // minimised
    }
    if (m_needs_recreate || extent != m_swapchain.extent()) {
        m_device->waitIdle();
        m_swapchain.recreate(extent);
        m_needs_recreate = false;
    }

    const gpu::FrameRing::Frame frame = m_frames.begin();

    const auto acquired = m_swapchain.acquire(frame.semaphore);
    if (!acquired.ok) {
        m_needs_recreate = true; // fence is still signalled, so the slot stays usable
        return;
    }
    const vk::Image image = m_swapchain.image(acquired.image_index);

    frame.cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Contents are garbage and loadOp=eClear overwrites them, so eUndefined is correct.
    gpu::imageBarrier(
        frame.cmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eTopOfPipe, {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite
    );

    const vk::RenderingAttachmentInfo color{
        .imageView = m_swapchain.view(acquired.image_index),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    // Nothing is drawn yet: the loadOp is the frame.
    frame.cmd.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, m_swapchain.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    });
    frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    frame.cmd.setViewport(
        0, vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f}
    );
    frame.cmd.setScissor(0, vk::Rect2D{{0, 0}, extent});
    frame.cmd.draw(3, 1, 0, 0);
    frame.cmd.endRendering();

    // No dstAccess: the present engine's visibility comes from the semaphore.
    gpu::imageBarrier(
        frame.cmd, image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eBottomOfPipe, {}
    );

    frame.cmd.end();

    gpu::submit(*m_device, m_device->graphicsQueue(), {
        .cmd = &frame.cmd,
        .wait = frame.semaphore,
        .wait_stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .signal = m_swapchain.renderFinished(acquired.image_index),
        .fence = frame.fence,
    });

    if (!m_swapchain.present(m_device->presentQueue(), acquired.image_index)) {
        m_needs_recreate = true;
    }
    m_frames.advance();
}

} // namespace ge::gfx
