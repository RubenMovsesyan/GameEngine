# CUDA ↔ Vulkan image interop

Let a CUDA kernel write an `sg_image` directly, so a CUDA renderer can return its output
image to the `Presenter` without a copy pass. Today only **buffers** are shared
(`gpu::ExternalBuffer`), so a CUDA renderer writes pixels into a storage buffer and a
fullscreen pass copies them into an image — ElectronCloud's `ElectronCloudRenderer` does
exactly this.

**Status: not worth doing yet.** The copy it removes costs ~0.02 ms at 1920×1080 (8 MB
read + 8 MB written; bandwidth estimate, not measured), and it roughly doubles the sokol
fork in its most delicate part. Pick it up when something else needs it — CUDA *reading*
textures, a renderer where the copy actually shows in a profile — or when upstream sokol
grows external-image support and the layout work below comes for free.

## Plan

1. **Fork: exportable image memory** (`sokol_gfx.h`, fenced `GameEngine fork` like the
   buffer path). `sg_image_desc.vk_external_memory`: `VkExternalMemoryImageCreateInfo`
   (`OPAQUE_FD`) on `vkCreateImage`, then a dedicated exportable allocation
   (`VkMemoryDedicatedAllocateInfo` with `.image`, `VkExportMemoryAllocateInfo`). Same
   shape as the buffer change in `_sg_vk_create_buffer`.
2. **Fork: pin the layout to `GENERAL`** — the hard part. sokol tracks one access state per
   image and moves the layout with it (`_sg_vk_image_barrier`, `_sg_vk_image_layout`):
   `UNDEFINED` on first use (the driver may discard the contents), `SHADER_READ_ONLY_OPTIMAL`
   after sampling. CUDA has no notion of layouts and writes between sokol's frames, so an
   external image must live in `VK_IMAGE_LAYOUT_GENERAL` for its whole life: transition it
   once at creation, and make every later barrier on it keep `GENERAL` (old and new) while
   still emitting the access/stage masks. Also hand it to sokol in a readable state, as the
   buffer path does with `cur_access`, or the in-pass state check asserts.
3. **Fork: `sg_vk_query_image_info(sg_image)`** → `VkImage`, `VkDeviceMemory`, allocation
   size. Fills `gpu::nativeHandle(sg_image)`, which is `nullopt` on Vulkan today.
4. **Engine: `gpu::ExternalImage`** (`src/gpu/external_image.*`) — the image twin of
   `ExternalBuffer`: owns the `sg_image` + attachment/texture views, dups the memory FD
   (`vkGetMemoryFdKHR`). RGBA8 unorm only to start.
5. **Engine: `CudaInterop::mapImage(const ExternalImage&)`** — `cuImportExternalMemory`
   (dedicated flag set), `cuExternalMemoryGetMappedMipmappedArray` with a matching
   `CUDA_ARRAY3D_DESCRIPTOR` (`CU_AD_FORMAT_UNSIGNED_INT8`, 4 channels, width × height,
   `CUDA_ARRAY3D_SURFACE_LDST`), level 0 via `cuMipmappedArrayGetLevel`, then
   `cuSurfObjectCreate`. Returns the `CUsurfObject`; destroys it and the mipmapped array
   with the context.
6. **Prove it** in a sandbox: a kernel `surf2Dwrite`s a gradient into an `ExternalImage`,
   a trivial renderer returns the image's texture view, run under the validation layer.
   Sync is the existing `CrossSemaphore` pair, unchanged.
7. **Then ElectronCloud**: the kernel `surf2Dwrite`s instead of writing `pixels[]`;
   delete `shaders/electron_cloud.glsl`, the copy pass and its build step.

## Gotchas

- **Format and size must match exactly** between the `VkImage` and the CUDA array
  descriptor; a mismatch is undefined, not an error.
- **Optimal tiling is fine** on NVIDIA for opaque-FD images, but needs the dedicated
  allocation.
- **Queue family ownership.** By the spec, handing an image to an external API means a
  release to `VK_QUEUE_FAMILY_EXTERNAL` and an acquire back. The buffer path skips this and
  works on NVIDIA; try the same for images, but if the validation layer or corruption
  says otherwise, the fork has to emit those barriers too.
- **No resize story yet**: `CudaInterop` can map but not unmap (true for buffers too), so a
  resize leaks the old import until the context goes. Add an `unmap` with this work.
- **clang CUDA is fine**: `surf2Dwrite` compiles to `sust` with clang 23 (checked), so this
  doesn't tie a project to nvcc.
- Every fork line makes "drop the fork when upstream catches up" harder — the cost to weigh
  each time this comes up.
