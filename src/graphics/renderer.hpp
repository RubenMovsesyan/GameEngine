#pragma once

#include <graphics/camera.hpp>
#include <graphics/mesh.hpp>
#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <vector>

namespace graphics {

// Self-sufficient enough to drive its own swapchain-present loop -- meant to run
// entirely off a worker thread, no Window involved past construction. beginFrame()
// acquires the next swapchain image and opens the command list; endFrame() records the
// lighting pass, closes, submits, and presents. renderMesh() records a geometry-pass
// draw in between the two.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();

        void renderMesh(const Mesh&, const Eigen::Matrix4f& model, const Camera&);

        void endFrame();

    private:
        void rebuildSwapchainSurfaces();
        void rebuildGBuffer();

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;

        gpu::Swapchain* m_swapchain;
        std::vector<Surface> m_swapchain_surfaces;

        // Deferred shading G-buffer. Geometry pass writes albedo+normal+depth; a later
        // lighting pass reads them. material stays allocated but unused until the
        // geometry pass writes more than color+normal.
        nvrhi::TextureHandle m_albedo;   // SRGBA8_UNORM
        nvrhi::TextureHandle m_normal;   // RGBA32_FLOAT
        nvrhi::TextureHandle m_material; // RGBA8_UNORM
        nvrhi::TextureHandle m_depth;    // D32
        // {color0: m_albedo, color1: m_normal, depth: m_depth}
        nvrhi::FramebufferHandle m_gbuffer_framebuffer;

        nvrhi::InputLayoutHandle m_mesh_input_layout;
        nvrhi::GraphicsPipelineHandle m_gbuffer_pipeline;
        // Push constants still need a BindingSet bound alongside the BindingLayout --
        // it carries no data of its own (the bytes go through setPushConstants), so one
        // built once here is reused for every renderMesh() call.
        nvrhi::BindingSetHandle m_gbuffer_push_constant_binding_set;

        nvrhi::GraphicsPipelineHandle m_lighting_pipeline;
        nvrhi::BindingLayoutHandle m_lighting_binding_layout;
        nvrhi::SamplerHandle m_lighting_sampler;
        // Separate from m_lighting_binding_layout, same reason the g_buffer pass keeps
        // its push-constant layout separate from any other bindings it might grow.
        nvrhi::BindingLayoutHandle m_lighting_push_constant_layout;
        nvrhi::BindingSetHandle m_lighting_push_constant_binding_set;

        // Stashed between beginFrame() and endFrame(). !ok means beginFrame's acquire
        // failed (swapchain already recreated) -- endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
        // Stashed by renderMesh() for endFrame()'s lighting pass to read -- only correct
        // when renderMesh() is called once per frame with one camera, same as today.
        Eigen::Vector3f m_view_position{0.0f, 0.0f, 0.0f};
};

} // namespace graphics
