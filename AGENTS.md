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
src/gpu/    the sokol_gfx context, resource handles, the native-handle seam, CUDA interop,
            and BufferReadback (GPU -> CPU copies of storage buffers, Vulkan)
src/graphics/  the graphics engine -- the Renderer concept, Presenter and Recorder at the root, then:
  primitives/scene/  pipeline-agnostic scene types (Mesh, Material, Texture, Camera, Frustum, Skybox, Light, PointInstance)
  primitives/pass/   GPU-pass building blocks (RenderTarget, FullscreenPass, Tonemap/Fxaa, DebugDraw, shadow cascades, LightManager)
  renderers/         whole rendering strategies (DeferredRenderer, PointRenderer, ClearRenderer)
src/scene/  Model (meshes + local transforms + materials)
src/assets/ AssetLibrary (load once, share by path) + built-in loaders in loaders/ (glTF, image)
src/app/    Application<State> (owns the State + frame loop) over Context (window, input, workers)
src/utils/  small shared helpers (Display concept, VideoEncoder: frames -> ffmpeg)
sandbox/    test executables
```

`src/gpu/` is a thin ownership layer over sokol, not a framework — no base class to
subclass, no singleton, no `shared_ptr` graph beyond the resource handles themselves.
`src/graphics/` drives it and calls into it; `src/gpu/` never calls back.

- **No virtual functions in `gpu/` or `graphics/`.** A whole rendering strategy is a
  *plugin*: any type satisfying the `graphics::Renderer` concept (one requirement —
  `sg_view render(int, int)`, recording every pass for the frame and returning the final
  image) is a renderer. `graphics::renderers::DeferredRenderer` (meshes),
  `graphics::renderers::PointRenderer` (emissive points, wiring only so far) and
  `graphics::renderers::ClearRenderer` (one clear pass, for bring-up; the template for a
  new renderer) are the built-in ones. `graphics::Presenter` (at the `graphics/` root, alongside the concept)
  takes a renderer's output and blits it to the swapchain, and is the only thing that
  touches the swapchain or calls `sg_commit()`. Shared building blocks live under
  `graphics::primitives::` — `scene::` for pipeline-agnostic scene types, `pass::` for
  GPU-pass mechanics (`RenderTarget`, `FullscreenPass`, `TonemapPass`/`FxaaPass`, …) — and
  a renderer composes them. A renderer also carries its own `submit(...)` and setup calls,
  shaped for its input; the concept can't express that, so it stays a convention. To add a
  strategy (a compute raymarcher, …) add a type under `renderers/` with a `render()` and
  hand it to a `Presenter` — no base class, no change to anything existing.
- **`src/gpu/sokol.cpp` is the only translation unit that defines `SOKOL_IMPL`.** It also
  picks the backend (`SOKOL_METAL` when `build.cpp` defines `GAMEENGINE_METAL` on macOS,
  `SOKOL_VULKAN` otherwise; on macOS it is compiled through `sokol_metal.mm` as
  Objective-C++) and sets `SOKOL_NO_ENTRY` so we keep our own `main`.
  Note the include order inside it: `sokol_app.h` must come first, because it is the header
  that defines `VK_USE_PLATFORM_XLIB_KHR` before pulling in `<vulkan/vulkan.h>`.

**sokol_gfx is single-threaded and sokol_app owns the frame loop.** Every `sg_*` call must
happen on the thread that runs the State's constructor, `frame()` and destructor — that is
the *only* thread that renders. `app::Worker` runs everything else off that thread: pure-CPU work, and GPU
compute that goes through its own API (e.g. a CUDA simulation thread). **A worker must never
call `sg_*`.** It may drive CUDA and share a buffer with the renderer through the
`gpu::SharedInstanceBuffer` handshake, but the sokol rendering path stays in `frame()`.
`Application` starts workers after the State's constructor and stops + joins them (via
`workerShouldStop()`) before destroying the State, so a worker's GPU teardown finishes while
the context is still up. Work that needs both (texture loading) decodes on a worker and
uploads from `frame()`; see `graphics::Texture::drainUploads`.

**An app is a State type handed to `app::Application<S>`.** `S` must satisfy `app::AppState`:
an `explicit S(app::Context&)` (init: GPU up, register workers, parse
`context.argc()/argv()` with `CliParser`) and `void frame(app::Context&)`; its destructor is
cleanup. Application owns the State for exactly as long as the GPU is up, so members that
hold GPU handles are plain members — no `std::optional` + `emplace` dance. The State,
workers and helpers take `app::Context&` (the non-template base: window, input, DebugUI,
workers, quit), never `Application<S>&`. The sandboxes still keep their GPU members in
`std::optional` from before; new code shouldn't.

**Every module gets a `docs/` page** — an HTML page (dark mode) explaining how that module
works, with real code examples pulled from the module itself. Link the shared stylesheet at
`../style.css` rather than restyling. Start from `docs/index.html`, which is the system
overview and links every page. Update a module's page in the same change as the code — a
docs page that describes an older design is worse than none.

## Building

Linux (Vulkan) needs `vulkan-headers`, `vulkan-validation-layers`, and X11 development
headers (`libx11`, `libxi`, `libxcursor`). macOS (Metal) needs only the Xcode command-line
tools; `build.cpp` switches on `__APPLE__`: Metal/MetalKit/QuartzCore/Cocoa frameworks, the
`osx_arm64` sokol-shdc, `metal_macos` shaders, no CUDA. **The Metal build leaves out the
Vulkan-only code** -- `external_buffer`, `cross_semaphore`, `cuda_interop`,
`shared_instance_buffer`, `buffer_readback`, and so `graphics::Recorder` and
`sandbox/recorder_test` -- until their Metal versions land (`roadmap/metal-backend.md`).

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
sokol-shdc's annotated GLSL. `build.cpp` runs the platform's `external/sokol-tools-bin/bin/<os>/sokol-shdc`
over it to generate `.build/shaders/engine.glsl.h`, which carries the SPIR-V (Linux) or
Metal source (macOS) for every program plus generated uniform-block structs and bind-slot constants. Only five TUs
include it: `renderers/deferred_renderer.cpp`, `renderers/point_renderer.cpp`,
`primitives/pass/post.cpp`, `presenter.cpp`, and `recorder.cpp` (the `capture` compute
program). Adding one means adding it to `includes_shader_header` in `build.cpp`.

Bind slots are three separate spaces, each declared explicitly in the GLSL:

- `layout(binding=N) uniform { ... }`, N in 0..7 → `sg_apply_uniforms(N, ...)`
- textures and storage buffers, N in 0..31 → `sg_bindings.views[N]`
- `layout(binding=N) uniform sampler`, N in 0..11 → `sg_bindings.samplers[N]`

Resources sharing a name across programs must share a binding. Use the generated
`UB_*`, `VIEW_*`, `SMP_*` and `ATTR_*` constants rather than writing slot numbers by hand.

## Choices

- C++23, clang. The build script is C++23 because `build.hpp` is a C++ header.
- sokol_gfx for rendering, sokol_app for windowing and input. Vulkan backend on Linux,
  Metal on macOS; the shaders and renderers are unchanged between the two (sokol's Vulkan
  backend flips the viewport, so both share Metal's clip-space and texture-origin
  conventions). D3D11, GL and WebGPU aren't built.
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
- **`external/sokol` is a fork** — `RubenMovsesyan/sokol` (private), branch
  `gameengine/vk-interop` over `floooh/sokol` master. Upstream's Vulkan backend has no
  native-handle accessor; the fork adds `sg_vk_query_buffer_info` /
  `sg_vk_query_device_info`, a `sg_buffer_desc.vk_external_memory` opt-in (exportable
  dedicated memory), `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` on storage buffers (so
  `gpu::BufferReadback` can copy them to the CPU), and the two external-memory/-semaphore
  FD device extensions in `sokol_app.h`. Every change is fenced with a `GameEngine fork` comment. Inside the
  submodule, `origin` is the fork and `upstream` is `floooh/sokol`. **Pivot away the moment
  upstream sokol grows first-class external-memory support** — repoint `.gitmodules` at
  `floooh/sokol`, bump the pin, adapt `src/gpu/`.
- **Recording is `graphics::Recorder`** (docs/graphics/recorder.html): a Presenter that also
  writes every frame to a video. Render image -> `capture_cs` compute pass (RGBA8 into a
  storage buffer) -> internal Presenter shows + commits -> `gpu::BufferReadback` copies it to
  the CPU on sokol's queue after the commit (3 frames in flight) -> `utils::VideoEncoder`
  pipes it to ffmpeg. No CUDA. ffmpeg is checked at construction (`RLOG(LL_FATAL)` if
  missing). Drive the simulation by `frameTime()` while recording so the video is real time
  regardless of render speed. `sandbox/recorder_test` records a known colour sequence.
- **CUDA is optional** (`build.cpp` probes `CUDA_PATH` / `/opt/cuda`). When present it
  defines `GAMEENGINE_CUDA`, links `libcuda` (Driver API only — no `nvcc`, no `libcudart`
  in the engine), and builds `sandbox/cuda_points_test` (its kernel is `nvcc --ptx`'d and
  JIT-loaded at runtime). `src/gpu/cuda_interop.*` is fully `#ifdef GAMEENGINE_CUDA`.
- `gpu::nativeHandle(sg_buffer)` is now filled on Vulkan too (via the fork). See
  `src/gpu/external_buffer.*`, `cross_semaphore.*`, `cuda_interop.*`,
  `shared_instance_buffer.*` for the zero-copy CUDA interop + cross-API timeline-semaphore
  sync, and `roadmap/compute-ray-tracer.md`. `gpu::SharedInstanceBuffer` has three sync
  modes — `Semaphore` / `DoubleBuffer` (render-priority) and `SimPriority` (a private sim
  buffer + published ring; the sim never waits on the renderer). The CUDA thread is an
  `app::Worker`.
- A build step's output must not share a name with a top-level source directory. Object
  files mirror source paths into `.build/`.
