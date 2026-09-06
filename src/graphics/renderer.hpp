#pragma once

#include <sokol_gfx.h>

#include <concepts>

namespace graphics {

// The whole contract between a renderer and graphics::Presenter.
//
// A renderer is any type that, given a target size, records every GPU pass for one frame
// and hands back a texture view of the finished, presentable image. It owns everything
// else -- its own render targets, pipelines, tonemap, post, debug overlays, resize handling
// -- so a deferred renderer, an emissive point renderer, or a ray tracer are all just
// different implementations of this one concept. The Presenter never reaches inside; it
// only calls render() and blits the result to the swapchain.
//
// Contract that the concept cannot express, but every implementation must honour:
//   - render() must NOT call sg_commit() -- the Presenter owns the frame boundary.
//   - the returned sg_view must stay valid until the next render() call.
template <typename T>
concept Renderer = requires(T& renderer, int width, int height) {
    { renderer.render(width, height) } -> std::same_as<sg_view>;
};

} // namespace graphics
