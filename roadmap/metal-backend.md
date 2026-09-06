# Darwin / Metal backend

Run macOS builds on sokol's Metal backend instead of Vulkan-only.

## Why
- Apple Silicon unified memory: skip staging copies in the graphics pipeline where
  the platform allows — upload paths, readbacks, and compute↔render buffer sharing.
- Metal also has sokol native-handle accessors (`sg_mtl_query_*`), unlike the Vulkan
  backend, which has none — matters for interop (see compute-ray-tracer.md).

## Touch points
- `build.cpp`: platform detection; Metal needs `-fobjc-arc`, `-framework Metal
  QuartzCore Cocoa`, a `.metal`/`metallib` shader step (sokol-shdc target `metal_macos`).
- `src/gpu/sokol.cpp`: backend select is hardcoded `SOKOL_VULKAN` + `VK_USE_PLATFORM_XLIB_KHR`.
  Make it `SOKOL_METAL` on Darwin; sokol_app picks Cocoa windowing automatically.
- Shaders: `engine.glsl` already goes through sokol-shdc — add the Metal output target,
  no source changes expected.
- Verify the clip-space / origin conventions the deferred renderer hard-codes
  (viewport Y-flip, depth range) still hold on Metal.

## Not doing yet
- Windows/D3D11, WebGPU. sokol abstracts them but no reason to build them now.
