# Compute ray tracer (CUDA + Metal)

A ray tracer as a compute pass, exposed as another `graphics::Renderer` plugin
(writes its final image into a target the `Presenter` blits — same contract as any renderer).

## Backends
- **CUDA** — Linux / NVIDIA.
- **Metal compute** — macOS (needs the Metal backend, see metal-backend.md).
- Share the kernel/tracing logic across both where feasible; only the dispatch +
  buffer-interop glue differs.

## The interop problem (main blocker)
The kernel needs to read/write GPU buffers that sokol owns, or hand its own buffer
to sokol for display.
- **Vulkan backend: blocked.** sokol exposes no `VkBuffer` accessor and no injection
  field, and never sets `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD`. `gpu::nativeHandle()`
  returns `nullopt` there. Options: (a) patch the vendored sokol Vulkan header
  (~60 lines, upstreamable, a fork to maintain), or (b) run the CUDA build on sokol's
  GL backend, where `sg_gl_query_buffer_info` gives a `GLuint` that
  `cudaGraphicsGLRegisterBuffer` accepts (GL 4.3+ for the storage buffers).
- **Metal backend: fine.** `sg_mtl_query_*` accessors exist; a CUDA/Metal buffer and an
  `id<MTLBuffer>` can be bridged directly.

## Notes
- sokol_gfx also has its own compute (`sg_dispatch`) — a pure in-sokol compute
  raymarcher needs no interop at all and could be the first milestone before
  wiring CUDA/Metal.
- No hardware RT (RTX / Metal ray tracing API) — this is software tracing in a
  compute kernel. Hardware RT would mean going around sokol entirely.
