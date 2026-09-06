# GameEngine

A game engine built as independent modules under `src/`, each its own namespace and
directory; `sandbox/` holds the executables that exercise them.

## Layout

```
buildcpp/   git submodule, the build system (build.hpp)
build.cpp   the build script, C++23
external/   third-party dependencies, one submodule per directory (see external/AGENTS.md)
docs/       system overview + the stylesheet every module's docs share
notes/      working notes on in-flight or recently finished work
src/gpu/    the sokol_gfx context, resource handles, and the native-handle seam
src/graphics/  the graphics engine: renderer, meshes, materials, lights, shaders
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
  image) is a renderer. `graphics::DeferredRenderer` is the first one;
  `graphics::Presenter` takes a renderer's output and blits it to the swapchain, and is
  the only thing that touches the swapchain or calls `sg_commit()`. Shared building
  blocks (`RenderTarget`, `FullscreenPass`, `TonemapPass`/`FxaaPass`) are plain
  `graphics/` primitives a renderer composes. To add a strategy (emissive points, a
  compute raymarcher) write a new type with a `render()` and hand it to a `Presenter` —
  no base class, no change to anything existing.
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
./run --model <path>.glb                # build, then launch the sandbox
```

`./build` rebuilds itself when `build.cpp` or `buildcpp/build.hpp` changes, and writes
`compile_commands.json`. Artifacts land in `.build/`.

Adding a source file means adding a line to `engine_sources` in `build.cpp` — there is no
globbing.

## Shaders

All shader programs live in one file, `src/graphics/shaders/engine.glsl`, written in
sokol-shdc's annotated GLSL. `build.cpp` runs `external/sokol-tools-bin/bin/linux/sokol-shdc`
over it to generate `.build/shaders/engine.glsl.h`, which carries the SPIR-V for every
program plus generated uniform-block structs and bind-slot constants. `renderer.cpp` is the
only file that includes it.

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
- Deferred shading (`graphics::DeferredRenderer`, one implementation of the
  `graphics::Renderer` concept): G-buffer → lighting → sky → tonemap → FXAA, with a
  directional cascaded shadow-map pass ahead of the G-buffer. Its output is an LDR
  target that `graphics::Presenter` blits to the swapchain.

## Gotchas

- `SG_RANGE(x)` expands to a temporary in C++, so it cannot be passed by address. Name the
  range first, or use the `applyUniforms` helper in `renderer.cpp`.
- sokol's native-handle accessors (`sg_gl_query_buffer_info` and friends) exist for every
  backend **except** Vulkan. `gpu::nativeHandle()` returns `std::nullopt` there. See
  `notes/sokol-migration.md` before wiring CUDA/SYCL interop.
- A build step's output must not share a name with a top-level source directory. Object
  files mirror source paths into `.build/`.
