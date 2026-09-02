#pragma once

#include <graphics/camera.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/material.hpp>
#include <graphics/mesh.hpp>
#include <graphics/post_process_chain.hpp>
#include <graphics/shadow.hpp>
#include <graphics/skybox.hpp>
#include <graphics/surface.hpp>
#include <gpu/swapchain.hpp>

#include <array>
#include <mutex>
#include <vector>

namespace graphics {

// Drives its own swapchain-present loop, meant to run off a worker thread.
class Renderer {
    public:
        Renderer(const gpu::Device&, gpu::Swapchain&);

        void beginFrame();

        // camera_visible only gates the G-buffer pass; shadow casters render regardless (an off-screen
        // object can still cast a shadow into view).
        template <Mesh T>
        void renderMesh(
            T& mesh, const Material& material, const Eigen::Matrix4f& model, const Eigen::AlignedBox3f& world_bounds,
            bool camera_visible, const Camera&
        );

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

        // Camera-space distance covered by the shadow cascades; beyond it, no shadows.
        void setShadowDistance(f32);
        // 0 = uniform cascade splits, 1 = logarithmic (more resolution near the camera).
        void setCascadeSplitLambda(f32);

        // Blit one shadow-map cascade slice to the screen as grayscale depth. Debug bring-up.
        void toggleShadowMapDebug();
        void cycleShadowMapDebugLayer();

        void endFrame();

    private:
        void rebuildSwapchainSurfaces();
        void rebuildGBuffer();
        void rebuildPostProcessTargets();
        void updateFxaaPushConstants();
        // Pass binding sets only change on resize (G-buffer/HDR handles) or, for lighting, on setLights().
        void rebuildLightingBindingSet();
        void rebuildSkyBindingSet();
        void buildShadowResources();
        // Replay m_draw_list. recordShadowPass writes the shadow map, recordGBufferPass the G-buffer.
        void recordShadowPass();
        void recordGBufferPass();

        static constexpr u32 kFramesInFlight = 2;
        static constexpr u32 kCascadeCount = kShadowCascadeCount;
        static constexpr u32 kShadowMapResolution = 2048;

        // One queued draw. Raw pointers are non-owning and valid only for the frame that queued
        // them (the meshes/materials live on scene::ModelPart through beginFrame()..endFrame()).
        struct DrawItem {
            nvrhi::IBuffer* vertex = nullptr;
            nvrhi::IBuffer* index = nullptr;
            nvrhi::IBuffer* instance = nullptr; // null => non-instanced
            u32 index_count = 0;
            u32 instance_count = 1;
            Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
            Eigen::Matrix4f normal_matrix = Eigen::Matrix4f::Identity();
            const Material* material = nullptr;
            // World AABB. For instanced items it covers only the identity instance -- unused until
            // Step E adds instanced cascade culling.
            Eigen::AlignedBox3f world_bounds;
            bool camera_visible = true; // false => shadow-cast only, skipped by the G-buffer pass
        };

        const gpu::Device* m_device;
        nvrhi::CommandListHandle m_cmd;
        std::unique_lock<std::recursive_mutex> m_frame_lock; // held beginFrame() to endFrame() -- see docs

        // GPU pacing: endFrame signals m_frame_done[slot]; beginFrame kFramesInFlight later waits on it.
        std::array<nvrhi::EventQueryHandle, kFramesInFlight> m_frame_done;
        u64 m_frame_counter = 0;

        // Filled by renderMesh() during the frame; replayed by the record*Pass() methods in endFrame().
        std::vector<DrawItem> m_draw_list;

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
        nvrhi::BindingLayoutHandle m_lighting_cb_layout;
        nvrhi::BindingSetHandle m_lighting_cb_binding_set;
        nvrhi::BufferHandle m_lighting_cb; // volatile; written each frame in endFrame with LightingUniforms
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

        // Directional shadow map: D32 array, one slice per cascade. Step D writes only slice 0
        // from a single hardcoded light matrix; Steps C/E fill the rest for real.
        nvrhi::TextureHandle m_shadow_map;
        std::array<nvrhi::FramebufferHandle, kCascadeCount> m_shadow_framebuffers;
        nvrhi::InputLayoutHandle m_shadow_input_layout;
        nvrhi::InputLayoutHandle m_shadow_instanced_input_layout;
        nvrhi::GraphicsPipelineHandle m_shadow_pipeline;
        nvrhi::GraphicsPipelineHandle m_shadow_instanced_pipeline;
        nvrhi::BindingLayoutHandle m_shadow_push_constant_layout;
        nvrhi::BindingSetHandle m_shadow_push_constant_binding_set;
        nvrhi::SamplerHandle m_shadow_cmp_sampler; // comparison sampler for PCF in lighting.frag
        // Recomputed each frame (renderMesh, first mesh) by graphics::computeCascades from the live camera.
        std::array<Eigen::Matrix4f, kCascadeCount> m_cascade_view_proj;
        std::array<f32, kCascadeCount> m_cascade_split_depth{}; // view-space far edge per cascade; consumed by Step F
        Eigen::Vector3f m_sun_direction = Eigen::Vector3f(-0.4f, -1.0f, -0.35f).normalized(); // overridden by setLights
        f32 m_shadow_distance = 60.0f;
        f32 m_cascade_split_lambda = 0.85f;
        f32 m_shadow_depth_bias = 0.0015f;     // starting guess; the bias-tuning step adds setters
        f32 m_shadow_normal_offset = 0.03f;    // world units, along the surface normal
        // Fullscreen debug blit of one shadow-map slice to the screen.
        nvrhi::GraphicsPipelineHandle m_shadow_debug_pipeline;
        nvrhi::BindingLayoutHandle m_shadow_debug_binding_layout;
        nvrhi::BindingSetHandle m_shadow_debug_binding_set;
        nvrhi::SamplerHandle m_shadow_debug_sampler;
        bool m_show_shadow_map_debug = false;
        u32 m_shadow_debug_layer = 0;

        // Bound at slot 3 whenever no lights are set -- a null buffer handle isn't valid there.
        nvrhi::BufferHandle m_empty_light_buffer;

        // !ok means beginFrame's acquire failed; endFrame is a no-op for that frame.
        gpu::Swapchain::Acquired m_acquired;
        Eigen::Vector3f m_view_position{0.0f, 0.0f, 0.0f};
        Eigen::Matrix4f m_inverse_view_projection = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f m_view = Eigen::Matrix4f::Identity(); // stashed in renderMesh; cascade select in the lighting CB
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
