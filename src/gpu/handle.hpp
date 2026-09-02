#pragma once

#include <common.h>

#include <sokol_gfx.h>

#include <memory>
#include <optional>

namespace gpu {

// sokol resources are plain 32-bit ids with no refcount of their own, but meshes share
// vertex/index buffers, so ownership is shared and the last holder destroys the resource.
using BufferHandle = std::shared_ptr<const sg_buffer>;
using ImageHandle = std::shared_ptr<const sg_image>;
using SamplerHandle = std::shared_ptr<const sg_sampler>;
using ViewHandle = std::shared_ptr<const sg_view>;

// All four must be called on the thread that owns the sokol_gfx context.
BufferHandle makeBuffer(const sg_buffer_desc&);
ImageHandle makeImage(const sg_image_desc&);
SamplerHandle makeSampler(const sg_sampler_desc&);
ViewHandle makeView(const sg_view_desc&);

// A resource's backend-native GPU handle, for handing it to CUDA / Metal / SYCL.
struct NativeHandle {
        const void* object = nullptr; // ID3D11Buffer* / id<MTLBuffer> / WGPUBuffer / ...
        u32 gl_name = 0;              // GL object name; only set on the GL backends
};

// std::nullopt on the Vulkan backend -- sokol exposes no VkBuffer/VkImage accessor and no
// way to inject one, so there is nothing to hand out. See notes/sokol-migration.md for the
// two ways out when interop actually needs wiring.
std::optional<NativeHandle> nativeHandle(sg_buffer);
std::optional<NativeHandle> nativeHandle(sg_image);

} // namespace gpu
