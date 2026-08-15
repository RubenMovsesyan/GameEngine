#pragma once

#include <gpu/device.hpp>
#include <graphics/surface.hpp>

#include <vector>

namespace graphics {

// Ordered chain of full-screen shader passes run after the lighting pass; the last pass writes to the swapchain.
class PostProcessChain {
    public:
        explicit PostProcessChain(const gpu::Device&);

        // Returns an index for setPushConstants(). Must be followed by rebuild() before run().
        u32 addPass(const u32* fragment_spirv, usize word_count, usize push_constant_size);

        // Call once after registering all passes, and again on swapchain resize.
        void rebuild(VkExtent2D extent, nvrhi::Format swapchain_format);

        void setPushConstants(u32 pass_index, const void* data, usize size);

        // Runs every registered pass in order: hdr_input -> ... -> swapchain_target.
        void run(nvrhi::ICommandList*, nvrhi::ITexture* hdr_input, const Surface& swapchain_target);

    private:
        struct PassDesc {
            const u32* fragment_spirv;
            usize word_count;
            usize push_constant_size;
        };
        struct Pass {
            nvrhi::GraphicsPipelineHandle pipeline;
            nvrhi::BindingLayoutHandle push_constant_layout; // only if push_constant_size > 0
            nvrhi::BindingSetHandle push_constant_binding_set;
            usize push_constant_size = 0;
            std::vector<u8> push_constant_data;
        };

        const gpu::Device* m_device;
        nvrhi::ShaderHandle m_fullscreen_vertex_shader;
        nvrhi::SamplerHandle m_sampler;
        nvrhi::BindingLayoutHandle m_input_binding_layout; // shared by every pass: texture @0, sampler @1

        std::vector<PassDesc> m_pass_descs;
        std::vector<Pass> m_passes;
        Surface m_ping_pong[2]; // HDR scratch targets, alternated between non-last passes
};

} // namespace graphics
