#pragma once

#include <gpu/handle.hpp>
#include <graphics/camera.hpp>
#include <graphics/debug_draw.hpp>
#include <graphics/light_manager.hpp>
#include <graphics/material.hpp>
#include <graphics/mesh.hpp>
#include <graphics/shadow.hpp>
#include <graphics/skybox.hpp>

#include <array>
#include <functional>
#include <vector>

namespace graphics {

// An offscreen render target: the image, the view it is rendered through, and the view it
// is sampled through afterwards.
struct RenderTarget {
    gpu::ImageHandle image;
    gpu::ViewHandle attachment;
    gpu::ViewHandle texture;
};

// Deferred renderer: shadow cascades -> G-buffer -> lighting -> sky -> tonemap -> FXAA.
//
// sokol_gfx is single-threaded, so every method here must be called from the thread that
// owns the sokol context -- that is, from inside the Application frame callback.
class Renderer {
    public:
        Renderer();

        // Seeds view/inverse-view-projection and fits the shadow cascades against this
        // camera. Called once, unconditionally, at the start of the frame -- not inferred
        // from whichever submit() happens to run first.
        void beginFrame(const Camera&);

        // camera_visible only gates the G-buffer pass; shadow casters render regardless (an off-screen
        // object can still cast a shadow into view). Caller decides visibility (e.g. against
        // Camera::frustum()) -- submit() itself has no notion of "camera".
        template <Mesh T>
        void submit(
            T& mesh, const Material& material, const Eigen::Matrix4f& model, const Eigen::AlignedBox3f& world_bounds,
            bool camera_visible
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
        // Direct get/set pair alongside the toggle/cycle above -- a debug-UI checkbox or
        // slider wants to set a value directly, not increment-and-wrap.
        bool isShadowMapDebugEnabled() const { return m_show_shadow_map_debug; }
        void setShadowMapDebugEnabled(bool enabled) { m_show_shadow_map_debug = enabled; }
        u32 shadowMapDebugLayer() const { return m_shadow_debug_layer; }
        void setShadowMapDebugLayer(u32 layer) { m_shadow_debug_layer = layer % kCascadeCount; }

        // Immediate-mode debug lines (AABBs, frustums, axes). Off by default; call
        // debugDraw().setEnabled(true) to turn it on. Calls between beginFrame() and
        // endFrame() are drawn as an unlit overlay, unaffected by tonemap/FXAA.
        DebugDraw& debugDraw() { return m_debug_draw; }

        // overlay runs at the tail of the swapchain-writing pass, right after DebugDraw's
        // own overlay -- an opaque hook so Renderer never has to know what's drawing into
        // it (a debug UI, or anything else window-level that isn't tied to a render target).
        void endFrame(const std::function<void()>& overlay = {});

    private:
        void rebuildTargets(int width, int height);
        void buildPipelines();
        void buildShadowResources();
        void recordShadowPass();
        void recordGBufferPass();

        static constexpr u32 kCascadeCount = kShadowCascadeCount;
        static constexpr int kShadowMapResolution = 2048;

        // One queued draw. The sokol ids are copies, but the Material pointer is non-owning and
        // valid only for the frame that queued it.
        struct DrawItem {
            sg_buffer vertex = {};
            sg_buffer index = {};
            sg_buffer instance = {}; // invalid id => non-instanced
            u32 index_count = 0;
            u32 instance_count = 1;
            Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
            Eigen::Matrix4f normal_matrix = Eigen::Matrix4f::Identity();
            const Material* material = nullptr;
            Eigen::AlignedBox3f world_bounds;
            bool camera_visible = true; // false => shadow-cast only, skipped by the G-buffer pass
        };

        // Filled by submit() during the frame; replayed by the record*Pass() methods in endFrame().
        std::vector<DrawItem> m_draw_list;

        int m_width = 0;
        int m_height = 0;

        // G-buffer: albedo, octahedral normal, packed metallic-roughness, plus depth.
        RenderTarget m_albedo;
        RenderTarget m_normal;
        RenderTarget m_material;
        RenderTarget m_depth;

        RenderTarget m_hdr;  // lighting + sky output, before tonemapping
        RenderTarget m_ldr;  // tonemap output, FXAA input

        // Directional shadow map: one D32 array slice per cascade.
        gpu::ImageHandle m_shadow_image;
        std::array<gpu::ViewHandle, kCascadeCount> m_shadow_attachments;
        gpu::ViewHandle m_shadow_texture;

        gpu::SamplerHandle m_material_sampler; // trilinear, for albedo/MR lookups
        gpu::SamplerHandle m_gbuffer_sampler;  // nearest, 1:1 texel reads in fullscreen passes
        gpu::SamplerHandle m_shadow_sampler;   // comparison, for PCF

        // Bound whenever a Material has no texture for a slot.
        gpu::ImageHandle m_white_image;
        gpu::ViewHandle m_white_texture;
        // Bound whenever no lights are set -- a shader's storage buffer binding must be filled.
        gpu::BufferHandle m_empty_light_buffer;
        gpu::ViewHandle m_empty_light_view;

        sg_pipeline m_gbuffer_pipeline = {};
        sg_pipeline m_gbuffer_instanced_pipeline = {};
        sg_pipeline m_shadow_pipeline = {};
        sg_pipeline m_shadow_instanced_pipeline = {};
        sg_pipeline m_lighting_pipeline = {};
        sg_pipeline m_sky_pipeline = {};
        sg_pipeline m_tonemap_pipeline = {};
        sg_pipeline m_fxaa_pipeline = {};
        sg_pipeline m_shadow_debug_pipeline = {};

        sg_view m_light_view = {};
        u32 m_light_count = 0;

        Eigen::Vector3f m_sky_zenith;
        Eigen::Vector3f m_sky_ground;
        f32 m_sky_intensity;
        f32 m_sky_kind; // 0 = gradient; 1 = cubemap (not built yet)

        // Recomputed each frame from the live camera, by graphics::computeCascades.
        std::array<Eigen::Matrix4f, kCascadeCount> m_cascade_view_proj;
        std::array<f32, kCascadeCount> m_cascade_split_depth{};
        Eigen::Vector3f m_sun_direction = Eigen::Vector3f(-0.4f, -1.0f, -0.35f).normalized(); // overridden by setLights
        bool m_has_shadow_light = false;
        f32 m_shadow_distance = 60.0f;
        f32 m_cascade_split_lambda = 0.85f;
        f32 m_shadow_depth_bias = 0.0015f;
        f32 m_shadow_normal_offset = 0.03f;

        bool m_show_shadow_map_debug = false;
        u32 m_shadow_debug_layer = 0;

        // Non-owning; valid only for the frame that queued it (same lifetime contract as
        // DrawItem::material). endFrame() reads it to fit the shadow cascades, once
        // setLights() has had a chance to set m_sun_direction.
        const Camera* m_pending_camera = nullptr;

        // Stashed once by beginFrame(camera), consumed by endFrame.
        Eigen::Vector3f m_view_position{0.0f, 0.0f, 0.0f};
        Eigen::Matrix4f m_view_projection = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f m_inverse_view_projection = Eigen::Matrix4f::Identity();
        Eigen::Matrix4f m_view = Eigen::Matrix4f::Identity();

        // Scaled to survive the ~0.003 tonemap exposure; tune per scene alongside the skybox.
        Eigen::Vector3f m_ambient{300.0f, 300.0f, 300.0f};
        f32 m_exposure = 0.003f;
        bool m_fxaa_enabled = true;

        DebugDraw m_debug_draw;
};

} // namespace graphics
