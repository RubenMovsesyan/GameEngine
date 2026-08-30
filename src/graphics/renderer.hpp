#pragma once

#include <graphics/camera.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/material.hpp>
#include <graphics/mesh.hpp>
#include <graphics/post_process_chain.hpp>
#include <graphics/skybox.hpp>
#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <mutex>
#include <vector>

namespace graphics {

// Drives its own swapchain-present loop, meant to run off a worker thread.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();

        template <Mesh T>
        void renderMesh(T& mesh, const Material& material, const Eigen::Matrix4f& model, const Camera&);

        // Call between beginFrame() and endFrame().
        void setLights(const LightManager&);

        // Unpacks the Skybox into the sky pass's stashed params. Defaults render a gradient sky.
        void setSkybox(const Skybox&);

        // Brightness multiplier applied before the tonemap curve; scene-dependent, tune per scene.
        void setExposure(f32);

        // Flat ambient irradiance added to every lit surface (pre-tonemap).
        void setAmbient(const Eigen::Vector3f&);

        // FXAA post-pass; on by default.
        void setFxaaEnabled(bool);

        void endFrame();

    private:
        void rebuildSwapchainSurfaces();
        void rebuildGBuffer();
        void rebuildPostProcessTargets();
        void updateFxaaPushConstants();
        // Pass binding sets only change on resize (G-buffer/HDR handles) or, for lighting, on setLights().
        void rebuildLightingBindingSet();
        void rebuildSkyBindingSet();

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;
        std::unique_lock<std::recursive_mutex> m_frame_lock; // held beginFrame() to endFrame() -- see docs

        gpu::Swapchain* m_swapchain;
        std::vector<Surface> m_swapchain_surfaces;

        // Geometry pass writes albedo, normal, and packed metallic-roughness (+ depth).
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
        nvrhi::BindingLayoutHandle m_gbuffer_texture_binding_layout;
        // Vertex-stage view_projection -- same for every draw, so it lives here, not in the push constant.
        nvrhi::BufferHandle m_camera_buffer;
        bool m_camera_uploaded = false; // reset each beginFrame; written on the frame's first renderMesh
        nvrhi::SamplerHandle m_gbuffer_sampler;
        // Bound whenever a Material has no texture for a slot -- a null texture handle isn't valid there.
        nvrhi::TextureHandle m_white_texture;

        nvrhi::GraphicsPipelineHandle m_lighting_pipeline;
        nvrhi::BindingLayoutHandle m_lighting_binding_layout;
        nvrhi::SamplerHandle m_lighting_sampler;
        nvrhi::BindingLayoutHandle m_lighting_push_constant_layout;
        nvrhi::BindingSetHandle m_lighting_push_constant_binding_set;
        nvrhi::BindingSetHandle m_lighting_binding_set; // rebuilt on resize / setLights()

        // Dedicated fullscreen sky pass -- runs after lighting, before post-process.
        nvrhi::GraphicsPipelineHandle m_sky_pipeline;
        nvrhi::BindingLayoutHandle m_sky_binding_layout;
        nvrhi::SamplerHandle m_sky_sampler;
        nvrhi::BindingLayoutHandle m_sky_push_constant_layout;
        nvrhi::BindingSetHandle m_sky_push_constant_binding_set;
        nvrhi::BindingSetHandle m_sky_binding_set; // rebuilt on resize (depends only on m_depth)
        // Seeded from Skybox{} in the ctor -- the sole default lives in skybox.hpp.
        Eigen::Vector3f m_sky_zenith;
        Eigen::Vector3f m_sky_ground;
        f32 m_sky_intensity;
        u32 m_sky_kind; // 0 = gradient; 1 = cubemap (not built yet)

        // Bound at slot 3 whenever no lights are set -- a null buffer handle isn't valid there.
        nvrhi::BufferHandle m_empty_light_buffer;

        // !ok means beginFrame's acquire failed; endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
        Eigen::Vector3f m_view_position{0.0f, 0.0f, 0.0f};
        Eigen::Matrix4f m_inverse_view_projection = Eigen::Matrix4f::Identity();
        // Non-owning -- the LightManager passed to setLights() keeps the real handle alive.
        nvrhi::IBuffer* m_light_buffer = nullptr;
        u32 m_light_count = 0;

        // Scaled to survive the ~0.003 tonemap exposure; tune per scene alongside the skybox.
        Eigen::Vector3f m_ambient{300.0f, 300.0f, 300.0f};

        Surface m_hdr_color; // lighting pass's HDR output; m_post_process narrows it to LDR

        PostProcessChain m_post_process;
        u32 m_tonemap_pass;
        u32 m_fxaa_pass;
        bool m_fxaa_enabled = true;
};

} // namespace graphics
