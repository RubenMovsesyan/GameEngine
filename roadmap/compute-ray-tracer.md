# Compute ray tracer (CUDA + Metal)

A ray tracer as a compute pass, exposed as another `graphics::Renderer` plugin
(writes its final image into a target the `Presenter` blits — same contract as any renderer).

## Backends
- **CUDA** — Linux / NVIDIA.
- **Metal compute** — macOS (needs the Metal backend, see metal-backend.md).
- Share the kernel/tracing logic across both where feasible; only the dispatch +
  buffer-interop glue differs.

## The interop problem — SOLVED for Vulkan buffers (CPU never in the loop)

Landed: a sokol fork (`external/sokol` @ `gameengine/vk-interop`) + a `gpu/` interop layer.
`sandbox/cuda_points_test` runs a CUDA compute thread integrating a point cloud in place,
faster than vsync, while the render thread draws the *same* `VkBuffer` — no copy.

- **`src/gpu/external_buffer.*`** — `sg_buffer` with exportable dedicated `VkDeviceMemory`
  (fork's `sg_buffer_desc.vk_external_memory`), opaque FD via `vkGetMemoryFdKHR`.
- **`src/gpu/cross_semaphore.*`** — exportable Vulkan timeline semaphore; queue-side
  wait/signal done as empty `vkQueueSubmit`s that bracket sokol's own frame submit (so the
  fork does **not** touch `_sg_vk_submit_frame_command_buffers`).
- **`src/gpu/cuda_interop.*`** — CUDA Driver API: match device by UUID, primary context,
  `cuImportExternalMemory` → mapped `CUdeviceptr`, `cuImportExternalSemaphore`.
- **`src/gpu/shared_instance_buffer.*`** — orchestrator, two modes: `Semaphore` (one
  buffer, strict alternation, sim rate > frame rate via sub-steps) and `DoubleBuffer`
  (compute unthrottled, ~1 frame latency, 2× VRAM).

Still open for the ray tracer proper:
- **Images**, not just buffers — `gpu::nativeHandle(sg_image)` on Vulkan is still
  `nullopt`; a compute raymarcher writing an output image needs the same export path for
  `VkImage`/`VkDeviceMemory` (+ layout handling, unlike buffers).
- **Metal backend: fine.** `sg_mtl_query_*` accessors exist; a CUDA/Metal buffer and an
  `id<MTLBuffer>` can be bridged directly (roadmap item 2).

## Notes
- sokol_gfx also has its own compute (`sg_dispatch`) — a pure in-sokol compute
  raymarcher needs no interop at all and could be the first milestone before
  wiring CUDA/Metal.
- No hardware RT (RTX / Metal ray tracing API) — this is software tracing in a
  compute kernel. Hardware RT would mean going around sokol entirely.
