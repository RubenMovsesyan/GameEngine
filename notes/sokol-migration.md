# NVRHI -> sokol_gfx migration

Working notes for the graphics-API replacement. Written during phase 1 so the reasoning
survives past the commits that carry it out.

## Why

The renderer was NVRHI + raw Vulkan + GLFW: Linux-only, 26 vendored NVRHI `.cpp` files in the
build, and an explicit binding-layout/binding-set model that accounted for most of the volume in
`renderer.cpp` (818 lines, 205 `nvrhi::` references).

sokol_gfx replaces it for three reasons, in order of how much they actually mattered:

1. **Portability.** Backends for Vulkan, Metal, D3D11, GL and WebGPU. sokol_app on top of that
   replaces GLFW *and* our own instance/device/swapchain code.
2. **Compute + storage buffers.** The path to a 4D renderer that generates its geometry on the GPU.
   `sg_buffer_usage` has independent `vertex_buffer` and `storage_buffer` flags, so one buffer can
   be compute-written and vertex-read.
3. **Volume.** sokol-shdc generates the whole `sg_shader_desc` from reflection, which deletes every
   hand-written binding layout we had.

What sokol explicitly does *not* buy us: it sits at the same abstraction layer NVRHI did. The
deferred G-buffer, cascaded shadows, PBR lighting, tonemap and FXAA are still ours to write and
maintain. This was a port, not an offload.

## Facts worth not re-deriving

Verified against the vendored headers, not from memory:

- `SG_MAX_COLOR_ATTACHMENTS = 8`, of which 4 are "portable". The 3-MRT G-buffer is comfortable.
- `sg_view_desc.depth_stencil_attachment` is `sg_image_view_desc { image, mip_level, slice }`, so a
  per-cascade view into the D32 array maps 1:1 onto the old
  `TextureSubresourceSet().setArraySlices(c, 1)`.
- Samplers carry `.compare`, so the PCF comparison sampler ported directly.
- Cross-backend uniform blocks must be `SG_UNIFORMLAYOUT_STD140` and may only contain
  `float`, `vec2`, `vec3`, `vec4`, `int*` and `mat4`; arrays only of `vec4`, `int4`, `mat4`.
  sokol-shdc generates conforming structs, which is the main reason to use it over hand-rolling.
- `SG_NUM_INFLIGHT_FRAMES = 2` — the same depth our own `kFramesInFlight` event-query pacing used,
  so deleting that machinery cost no latency.
- Linux link line for sokol_app + Vulkan: `X11 Xi Xcursor dl pthread m vulkan`.

## Gotchas hit during bring-up

- **Header order matters.** `sokol_app.h` is the header that defines `VK_USE_PLATFORM_XLIB_KHR`
  before including `<vulkan/vulkan.h>`; `sokol_gfx.h` includes vulkan.h without it. Whichever gets
  there first decides whether `vkCreateXlibSurfaceKHR` exists. `sokol_app.h` must be included first
  in the `SOKOL_IMPL` translation unit — see `src/gpu/sokol.cpp`.
- **`SOKOL_NO_ENTRY`.** By default sokol_app defines `main()` and expects you to supply
  `sokol_main()`. We parse argv before we want a window, so we keep our own `main` and call
  `sapp_run()` from it.

## Threading

sokol_gfx is single-threaded and sokol_app owns the main loop. This forced the biggest structural
change in the migration:

- The renderer moved off the worker thread into the sokol_app frame callback.
- `app::Worker` survives, but only for CPU work. Nothing off the main thread may call `sg_*`.
- Texture loading split in two: `stb_image` decode stays on a detached thread, the upload moved to a
  queue drained at the top of the frame callback where `sg_make_image` is legal.
- `gpu::Device::lockSubmit()`, its recursive submit mutex, and `Renderer::m_frame_lock` all deleted —
  they existed only to serialise cross-thread submits that can no longer happen.

## Things that changed shape, not just API

- **`PostProcessChain` is gone**, folded into `Renderer`. It existed to register arbitrary
  SPIR-V blobs with opaque push-constant blobs at runtime. Under sokol-shdc each pass has a
  generated, typed `sg_shader_desc` and its own uniform struct, so a generic chain could no
  longer be generic -- it would have needed a `std::function` per pass to apply uniforms.
  Two fixed passes (tonemap, FXAA) inline in `Renderer::endFrame` are smaller and clearer.
- **`graphics::Surface` is gone.** It bundled a texture, a framebuffer and an extent; sokol's
  `sg_image` + `sg_view` + `sg_attachments` cover it. `RenderTarget` in `renderer.hpp` is the
  replacement and additionally carries the sampling view.
- **`Material` no longer owns GPU memory.** Its factors were a 32-byte per-material storage
  buffer; they are per-draw constants, so they moved into the G-buffer uniform block.
- **Mouse delta is now accumulated from `sapp_event.mouse_dx/dy`** rather than derived from
  absolute cursor positions. sokol_app documents `mouse_x/y` as invalid while the mouse is
  locked; the old GLFW code only worked because `CURSOR_DISABLED` fakes an unbounded cursor.
- **Frames-in-flight pacing deleted.** The `nvrhi::EventQuery` ring and `kFramesInFlight`
  are gone; sokol paces internally at `SG_NUM_INFLIGHT_FRAMES = 2`, the same depth.

## Known losses

Recorded, not solved. None of these are used by the engine today.

- **No indirect draw.** sokol_gfx has none. Future compute-generated 4D geometry will have to draw a
  fixed maximum primitive count and degenerate the unused ones, or stage a count readback.
- **No geometry shaders**, no tessellation. 4D slicing goes through compute, which is the better
  design regardless.
- **No ray tracing, mesh shaders, or bindless.** NVRHI exposed all three.

## The CUDA/SYCL interop hole

Vulkan is the one backend with no native-handle API. There is no `sg_vulkan_query_buffer_info`, and
`sg_buffer_desc` has no `vulkan_buffer` injection field — unlike `gl_buffers`, `mtl_buffers`,
`d3d11_buffer` and `wgpu_buffer`, which all have both directions.

So on the Vulkan backend you cannot get a `VkBuffer` out of sokol, nor hand one in. That blocks
CUDA and SYCL interop, which additionally need the buffer created with
`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD` at creation time — a flag sokol never sets.

`gpu::nativeHandle()` in `src/gpu/handle.hpp` is the seam. It returns the backend handle where sokol
exposes one and `std::nullopt` on Vulkan. When interop actually gets wired, the choice is:

1. **Patch the vendored header** — add `sg_vulkan_query_buffer_info` plus external-memory creation
   flags. Roughly 60 lines against sokol's internal `_sg.vk` structs, upstreamable, but a fork to
   maintain.
2. **Run the interop build on the GL backend**, where `sg_gl_query_buffer_info` already hands back a
   `GLuint` that `cudaGraphicsGLRegisterBuffer` accepts. Costs the Vulkan backend; GL needs 4.3+ for
   the storage buffers the light buffer uses.

Neither is urgent until there is compute work to hand off.

## Still to verify after the port

The migration is a mechanical port, so the things most likely to be wrong are the ones where
NVRHI and sokol disagree about conventions rather than about API:

- **Clip-space Y direction.** `reconstructWorldPosition` and `sampleCascade` both negate or
  flip Y with comments referring to "the NVRHI viewport flip". sokol reports its convention
  in `sg_features.origin_top_left` (true on Vulkan/D3D/Metal, false on GL). If the lighting
  is mirrored vertically or the shadows land in the wrong place, this is the first suspect.
- **sRGB handling.** The albedo target is `SG_PIXELFORMAT_SRGB8A8` and the swapchain is
  whatever sokol_app picked; a double-encode or a missing one shows up as a washed-out or
  overly dark image.
- **Depth sampled as a texture.** The G-buffer depth is bound with `unfilterable_float` and a
  non-filtering sampler so it can be read as raw depth rather than compared. Validation will
  complain here first if the pairing is wrong.
- **Exposure and ambient calibration.** Both carry over unchanged (`~0.003` exposure, ambient
  at 300) but they were tuned against the old pipeline's sRGB and tonemap behaviour.
