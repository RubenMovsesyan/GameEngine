#include <gpu/handle.hpp>

#include <rlog.h>

namespace gpu {

namespace {

// One shared_ptr factory per resource kind: allocate the id, hand back a handle whose
// deleter destroys it. sg_make_* never returns a null id -- a failed allocation comes back
// in an invalid state, which the validation layer reports.
template <typename Handle, typename Id, typename Desc, Id (*Make)(const Desc*), void (*Destroy)(Id)>
Handle makeResource(const Desc& desc) {
    Id id = Make(&desc);
    return Handle(new Id(id), [](const Id* owned) {
        Destroy(*owned);
        delete owned;
    });
}

} // namespace

BufferHandle makeBuffer(const sg_buffer_desc& desc) {
    return makeResource<BufferHandle, sg_buffer, sg_buffer_desc, sg_make_buffer, sg_destroy_buffer>(desc);
}

ImageHandle makeImage(const sg_image_desc& desc) {
    return makeResource<ImageHandle, sg_image, sg_image_desc, sg_make_image, sg_destroy_image>(desc);
}

SamplerHandle makeSampler(const sg_sampler_desc& desc) {
    return makeResource<SamplerHandle, sg_sampler, sg_sampler_desc, sg_make_sampler, sg_destroy_sampler>(desc);
}

ViewHandle makeView(const sg_view_desc& desc) {
    return makeResource<ViewHandle, sg_view, sg_view_desc, sg_make_view, sg_destroy_view>(desc);
}

std::optional<NativeHandle> nativeHandle(sg_buffer buffer) {
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE:
        case SG_BACKEND_GLES3: {
            const sg_gl_buffer_info info = sg_gl_query_buffer_info(buffer);
            return NativeHandle{.gl_name = info.buf[info.active_slot]};
        }
        case SG_BACKEND_D3D11:
            return NativeHandle{.object = sg_d3d11_query_buffer_info(buffer).buf};
        case SG_BACKEND_METAL_IOS:
        case SG_BACKEND_METAL_MACOS:
        case SG_BACKEND_METAL_SIMULATOR: {
            const sg_mtl_buffer_info info = sg_mtl_query_buffer_info(buffer);
            return NativeHandle{.object = info.buf[info.active_slot]};
        }
        case SG_BACKEND_WGPU:
            return NativeHandle{.object = sg_wgpu_query_buffer_info(buffer).buf};
        case SG_BACKEND_VULKAN:
        case SG_BACKEND_DUMMY:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<NativeHandle> nativeHandle(sg_image image) {
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE:
        case SG_BACKEND_GLES3: {
            const sg_gl_image_info info = sg_gl_query_image_info(image);
            return NativeHandle{.gl_name = info.tex[info.active_slot]};
        }
        case SG_BACKEND_D3D11:
            return NativeHandle{.object = sg_d3d11_query_image_info(image).res};
        case SG_BACKEND_METAL_IOS:
        case SG_BACKEND_METAL_MACOS:
        case SG_BACKEND_METAL_SIMULATOR: {
            const sg_mtl_image_info info = sg_mtl_query_image_info(image);
            return NativeHandle{.object = info.tex[info.active_slot]};
        }
        case SG_BACKEND_WGPU:
            return NativeHandle{.object = sg_wgpu_query_image_info(image).tex};
        case SG_BACKEND_VULKAN:
        case SG_BACKEND_DUMMY:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace gpu
