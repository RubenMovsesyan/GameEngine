# GameEngine

A game engine built as independent modules under `src/`, each its own namespace and
directory; `sandbox/` holds the executables that exercise them.

## Layout

```
buildcpp/   git submodule, the build system (build.hpp)
build.cpp   the build script, C++23
external/   third-party dependencies, one submodule per directory (see external/AGENTS.md)
docs/       system overview + the stylesheet every module's docs share
roadmap/    scratch backlog of planned work, pre project-management-system
src/gpu/    the sokol_gfx context, resource handles, and the native-handle seam
src/graphics/  the graphics engine -- the Renderer concept + Presenter at the root, then:
  primitives/scene/  pipeline-agnostic scene types (Mesh, Material, Texture, Camera, Frustum, Skybox, Light, PointInstance)
  primitives/pass/   GPU-pass building blocks (RenderTarget, FullscreenPass, Tonemap/Fxaa, DebugDraw, shadow cascades, LightManager)
  renderers/         whole rendering strategies (DeferredRenderer, PointRenderer)
src/scene/  glTF loading and the scene/model tree
src/app/    window, input, frame loop (sokol_app)
src/utils/  small shared helpers
sandbox/    test executables
```

`src/gpu/` is a thin ownership layer over sokol, not a framework — no base class to
subclass, no singleton, no `shared_ptr` graph beyond the resource handles themselves.
`src/graphics/` drives it and calls into it; `src/gpu/` never calls back.

- **No virtual functions in `gpu/` or `graphics/`.** A whole rendering strategy is a
  *plugin*: any type satisfying the `graphics::Renderer` concept (one requirement —
  `sg_view render(int, int)`, recording every pass for the frame and returning the final
  image) is a renderer. `graphics::renderers::DeferredRenderer` (meshes) and
  `graphics::renderers::PointRenderer` (emissive points, wiring only so far) are the two
  built-in ones. `graphics::Presenter` (at the `graphics/` root, alongside the concept)
  takes a renderer's output and blits it to the swapchain, and is the only thing that
  touches the swapchain or calls `sg_commit()`. Shared building blocks live under
  `graphics::primitives::` — `scene::` for pipeline-agnostic scene types, `pass::` for
  GPU-pass mechanics (`RenderTarget`, `FullscreenPass`, `TonemapPass`/`FxaaPass`, …) — and
  a renderer composes them. A renderer also carries its own `submit(...)` and setup calls,
  shaped for its input; the concept can't express that, so it stays a convention. To add a
  strategy (a compute raymarcher, …) add a type under `renderers/` with a `render()` and
  hand it to a `Presenter` — no base class, no change to anything existing.
- **`src/gpu/sokol.cpp` is the only translation unit that defines `SOKOL_IMPL`.** It also
  picks the backend (`SOKOL_VULKAN`) and sets `SOKOL_NO_ENTRY` so we keep our own `main`.
  Note the include order inside it: `sokol_app.h` must come first, because it is the header
  that defines `VK_USE_PLATFORM_XLIB_KHR` before pulling in `<vulkan/vulkan.h>`.

**sokol_gfx is single-threaded and sokol_app owns the frame loop.** Every `sg_*` call must
happen on the thread that runs the `app::Application` callbacks. `app::Worker` exists for
CPU work only — a worker must never touch the GPU. Work that needs both (texture loading)
decodes on a worker and uploads from the frame callback; see `graphics::Texture::drainUploads`.

**Every module gets a `docs/` page** — an HTML page (dark mode) explaining how that module
works, with real code examples pulled from the module itself. Link the shared stylesheet at
`../style.css` rather than restyling. Start from `docs/index.html`, which is the system
overview and links every page. Update a module's page in the same change as the code — a
docs page that describes an older design is worse than none.

## Building

Needs `vulkan-headers`, `vulkan-validation-layers`, and X11 development headers
(`libx11`, `libxi`, `libxcursor`).

```sh
clang++ -std=c++23 build.cpp -o build   # once, to bootstrap
./build                                 # every time after
./run --model <path>.glb                # build, then launch the model sandbox
./run point_test                        # the emissive-point sandbox (./run <name> picks it)
```

`./build` rebuilds itself when `build.cpp` or `buildcpp/build.hpp` changes, and writes
`compile_commands.json`. Artifacts land in `.build/`.

Adding a source file means adding a line to `engine_sources` in `build.cpp` — there is no
globbing.

## Shaders

All shader programs live in one file, `src/graphics/shaders/engine.glsl`, written in
sokol-shdc's annotated GLSL. `build.cpp` runs `external/sokol-tools-bin/bin/linux/sokol-shdc`
over it to generate `.build/shaders/engine.glsl.h`, which carries the SPIR-V for every
program plus generated uniform-block structs and bind-slot constants. Only four TUs
include it: `renderers/deferred_renderer.cpp`, `renderers/point_renderer.cpp`,
`primitives/pass/post.cpp`, and `presenter.cpp`.

Bind slots are three separate spaces, each declared explicitly in the GLSL:

- `layout(binding=N) uniform { ... }`, N in 0..7 → `sg_apply_uniforms(N, ...)`
- textures and storage buffers, N in 0..31 → `sg_bindings.views[N]`
- `layout(binding=N) uniform sampler`, N in 0..11 → `sg_bindings.samplers[N]`

Resources sharing a name across programs must share a binding. Use the generated
`UB_*`, `VIEW_*`, `SMP_*` and `ATTR_*` constants rather than writing slot numbers by hand.

## Choices

- C++23, clang. The build script is C++23 because `build.hpp` is a C++ header.
- sokol_gfx for rendering, sokol_app for windowing and input. Vulkan backend on Linux;
  the same code targets Metal, D3D11, GL and WebGPU without changes.
- Eigen for math. Column-major, matching GLSL.
- Deferred shading (`graphics::renderers::DeferredRenderer`, one implementation of the
  `graphics::Renderer` concept): G-buffer → lighting → sky → tonemap → FXAA, with a
  directional cascaded shadow-map pass ahead of the G-buffer. Its output is an LDR
  target that `graphics::Presenter` blits to the swapchain.
- Emissive points (`graphics::renderers::PointRenderer`, the second implementation):
  instanced view-space billboards, additively blended into HDR, then tonemapped. No
  lighting. Wiring only so far — the galaxy simulator's first dependency; see
  `roadmap/galaxy-simulation.md`.

## Gotchas

- `SG_RANGE(x)` expands to a temporary in C++, so it cannot be passed by address. Name the
  range first, or use one of the local `applyUniforms` helpers (in
  `renderers/deferred_renderer.cpp`, `renderers/point_renderer.cpp`, and
  `primitives/pass/post.cpp`).
- sokol's native-handle accessors (`sg_gl_query_buffer_info` and friends) exist for every
  backend **except** Vulkan. `gpu::nativeHandle()` returns `std::nullopt` there. See
  `roadmap/compute-ray-tracer.md` before wiring CUDA/SYCL interop.
- A build step's output must not share a name with a top-level source directory. Object
  files mirror source paths into `.build/`.
