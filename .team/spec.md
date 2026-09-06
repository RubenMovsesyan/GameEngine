# Spec: renderer "plugin" seam — concept + Presenter + post primitives

## Goal
Refactor the monolithic `graphics::Renderer` (deferred shading, one hardcoded
`endFrame()`) into a plugin model: a small C++20 `concept` defines the only thing
a renderer must expose (produce this frame's finished image), a thin
`graphics::Presenter` takes that image and displays it (blit to swapchain + ImGui
overlay + commit), and the shared building blocks (render targets, fullscreen
pass, tonemap, FXAA) become reusable `graphics::` primitives. The existing
deferred renderer is renamed to `graphics::DeferredRenderer` and rebuilt on those
primitives. **No new pipelines. The deferred renderer's visual output must be
unchanged.**

## Non-goals
- Any new renderer (emissive/point, ray tracing, compute).
- Bloom, SSAO, TAA, or any new post effect.
- A frame graph, render-target registry, or multi-renderer compositing.
- Any change to the deferred pipeline's shading, pass order, lighting, shadows,
  or sky — output is byte-for-byte the same modulo an added swapchain blit hop.
- Gamepad, audio, animation, or anything else from the broader engine roadmap.

## How to build / run / test
```sh
cd /home/rmovs/Work/GameEngine
# bootstrap the build binary if missing, then build:
[ -x ./build ] || clang++ -std=c++23 build.cpp -o build
./build -j "$(nproc)"
# artifact:
ls -l .build/bin/sandbox/model_test
# run (needs an X11 display + Vulkan; best-effort where headless):
./run --model sandbox/model_test/test_assets/monkey_light.glb
# in-app manual checks: F1/F2 shadow-map debug blit, F3 debug lines,
# F4 ImGui panel (checkboxes + cascade slider drive the renderer), window resize.
```
Primary automated gate is a clean `./build`. The running app is a Vulkan/X11 GUI;
where no display is available, verify by build success + reading the diff, and
note the visual A/B (against `main`) as the user's manual step.

## Acceptance criteria
- AC-1: `./build -j "$(nproc)"` completes with no compile or link errors and
  produces `.build/bin/sandbox/model_test`. — status: unverified
- AC-2: `src/graphics/renderer.hpp` defines a `graphics::Renderer` C++20 concept
  requiring exactly `{ r.render(width, height) } -> std::same_as<sg_view>` (int
  width, int height), with a comment stating the contract (no `sg_commit()` in
  `render()`; returned view valid until the next `render()`). — status: unverified
- AC-3: The old class is renamed, not duplicated: `graphics::DeferredRenderer`
  lives in `src/graphics/deferred_renderer.{hpp,cpp}`; no `graphics::Renderer`
  *class* remains; `DeferredRenderer` has no `endFrame` method and exposes
  `sg_view render(int width, int height)`; `static_assert(graphics::Renderer<
  graphics::DeferredRenderer>)` is present and compiles. — status: unverified
- AC-4: `graphics::Presenter` (`src/graphics/presenter.{hpp,cpp}`) is templated on
  the `Renderer` concept and its `present()` is the only place that calls
  `Texture::drainUploads()`, opens the swapchain pass, blits the renderer's
  returned view fullscreen, invokes the optional overlay callback inside that
  pass, and calls `sg_commit()`. `DeferredRenderer::render()` and
  `engine.glsl` contain no `sg_commit` and no `sglue_swapchain`. — status: unverified
- AC-5: Reusable primitives exist in `graphics/` and `DeferredRenderer` composes
  them: `RenderTarget` + `makeColorTarget`/`makeDepthTarget` in
  `render_target.{hpp,cpp}`; `FullscreenPass` in `fullscreen_pass.{hpp,cpp}` used
  to build/run every fullscreen pass in `deferred_renderer.cpp` (lighting, sky,
  tonemap, fxaa, shadow-debug); `TonemapPass` + `FxaaPass` in `post.{hpp,cpp}`
  used for the tonemap and FXAA passes. No `RenderTarget` struct or
  `makeColorTarget`/`makeDepthTarget` definition remains in
  `deferred_renderer.{hpp,cpp}`. — status: unverified
- AC-6: The deferred pipeline is behaviourally unchanged: in `engine.glsl` the
  `gbuffer*`, `shadow_depth*`, `lighting`, `sky`, `tonemap`, `fxaa`,
  `shadow_debug` programs are unchanged in body; the only shader addition is a
  `present` program that is a pure copy (`out_color = texture(sampler2D(
  tex_source, smp_source), v_uv)`, no tonemap/gamma). Pass order in `render()` is
  shadow → G-buffer → lighting → sky → tonemap → FXAA, and the FXAA pass now
  targets the LDR `RenderTarget` (not the swapchain), with the shadow-map debug
  blit and `DebugDraw::render()` still issued in that same pass. — status: unverified
- AC-7: `sandbox/model_test/main.cpp` uses the new API: it constructs a
  `graphics::DeferredRenderer` and a `graphics::Presenter`, and per frame calls
  `presenter.present(renderer, overlayLambda)` in place of
  `renderer.endFrame(overlayLambda)`; no delta-time value is passed into any
  renderer method; cleanup releases the presenter before the renderer. All F1–F5
  keybindings and the ImGui panel keep working (verified by build + code read
  where no display). — status: unverified
- AC-8: `build.cpp`'s `engine_sources` replaces `src/graphics/renderer.cpp` with
  `src/graphics/deferred_renderer.cpp` and adds `render_target.cpp`,
  `fullscreen_pass.cpp`, `post.cpp`, `presenter.cpp`; the generated-shader-header
  `depends_on` edge is attached to both `deferred_renderer.cpp` and
  `presenter.cpp` (the two TUs that include `engine.glsl.h`). — status: unverified
- AC-9: Docs and conventions are caught up per `.claude/skills/update-docs`: new
  self-contained pages `docs/graphics/{render_target,fullscreen_pass,post,presenter}.html`,
  `docs/graphics/renderer.html` reworked for `DeferredRenderer` + the concept,
  `docs/index.html` nav wired to the new pages; `AGENTS.md` "No virtual
  functions / extension point" and "Deferred shading" notes rewritten for the
  concept-based plugin model. — status: unverified
