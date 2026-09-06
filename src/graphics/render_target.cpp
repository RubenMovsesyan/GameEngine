#include <graphics/render_target.hpp>

namespace graphics {

RenderTarget makeColorTarget(int width, int height, sg_pixel_format format, const char* label) {
    sg_image_desc image_desc = {};
    image_desc.usage.color_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = format;
    image_desc.label = label;

    RenderTarget target;
    target.image = gpu::makeImage(image_desc);

    sg_view_desc attachment_desc = {};
    attachment_desc.color_attachment.image = *target.image;
    attachment_desc.label = label;
    target.attachment = gpu::makeView(attachment_desc);

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = *target.image;
    texture_desc.label = label;
    target.texture = gpu::makeView(texture_desc);

    return target;
}

RenderTarget makeDepthTarget(int width, int height, const char* label) {
    sg_image_desc image_desc = {};
    image_desc.usage.depth_stencil_attachment = true;
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
    image_desc.label = label;

    RenderTarget target;
    target.image = gpu::makeImage(image_desc);

    sg_view_desc attachment_desc = {};
    attachment_desc.depth_stencil_attachment.image = *target.image;
    attachment_desc.label = label;
    target.attachment = gpu::makeView(attachment_desc);

    sg_view_desc texture_desc = {};
    texture_desc.texture.image = *target.image;
    texture_desc.label = label;
    target.texture = gpu::makeView(texture_desc);

    return target;
}

} // namespace graphics
