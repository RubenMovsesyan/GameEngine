#pragma once

#include <graphics/camera.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/mesh.hpp>
#include <graphics/post_process_chain.hpp>
#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <vector>

namespace graphics {

// Drives its own swapchain-present loop, meant to run off a worker thread.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();

        template <Mesh T>
        void renderMesh(T& mesh, const Eigen::Matrix4f& model, const Camera&);

        // Call between beginFrame() and endFrame().
        void setLights(const LightManager&);

        // Brightness multiplier applied before the tonemap curve; scene-dependent, tune per scene.
        void setExposure(f32);

        void endFrame();

    private:
        void rebuildSwapchainSurfaces();
        void rebuildGBuffer();
        void rebuildPostProcessTargets();

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;

        gpu::Swapchain* m_swapchain;
        std::vector<Surface> m_swapchain_surfaces;

        // Geometry pass writes albedo+normal+depth; material unused until it writes more than color+normal.
        nvrhi::TextureHandle m_albedo;   // SRGBA8_UNORM
        nvrhi::TextureHandle m_normal;   // RGBA32_FLOAT
        nvrhi::TextureHandle m_material; // RGBA8_UNORM
        nvrhi::TextureHandle m_depth;    // D32
        nvrhi::FramebufferHandle m_gbuffer_framebuffer;

        nvrhi::InputLayoutHandle m_mesh_input_layout;
        nvrhi::GraphicsPipelineHandle m_gbuffer_pipeline;
        nvrhi::InputLayoutHandle m_instanced_mesh_input_layout;
        nvrhi::GraphicsPipelineHandle m_gbuffer_instanced_pipeline;
        nvrhi::BindingSetHandle m_gbuffer_push_constant_binding_set;

        nvrhi::GraphicsPipelineHandle m_lighting_pipeline;
        nvrhi::BindingLayoutHandle m_lighting_binding_layout;
        nvrhi::SamplerHandle m_lighting_sampler;
        nvrhi::BindingLayoutHandle m_lighting_push_constant_layout;
        nvrhi::BindingSetHandle m_lighting_push_constant_binding_set;

        // Bound at slot 3 whenever no lights are set -- a null buffer handle isn't valid there.
        nvrhi::BufferHandle m_empty_light_buffer;

        // !ok means beginFrame's acquire failed; endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
        Eigen::Vector3f m_view_position{0.0f, 0.0f, 0.0f};
        Eigen::Matrix4f m_inverse_view_projection = Eigen::Matrix4f::Identity();
        // Non-owning -- the LightManager passed to setLights() keeps the real handle alive.
        nvrhi::IBuffer* m_light_buffer = nullptr;
        u32 m_light_count = 0;

        Surface m_hdr_color; // lighting pass's HDR output; m_post_process narrows it to LDR

        PostProcessChain m_post_process;
        u32 m_tonemap_pass;
};

} // namespace graphics
