# Spec: renderer "plugin" seam — concept + Presenter + post primitives

## Goal
Refactor the monolithic `graphics::Renderer` (deferred shading, one hardcoded
`endFrame()`) into a plugin model: a small `concept` defines the only thing a
renderer must expose (produce this frame's finished image), a thin
`graphics::Presenter` takes that image and displays it (blit to swapchain + ImGui
overlay + commit), and the shared building blocks (render targets, fullscreen
pass, tonemap, FXAA) become reusable `graphics::` primitives. The existing
deferred renderer is renamed to `graphics::DeferredRenderer` and rebuilt on those
primitives. **No new pipelines. The deferred renderer's visual output must be
unchanged** — verified by the user's manual A/B (see AC-6b), outside the
automated gate.

## Non-goals
- Any new renderer (emissive/point, ray tracing, compute).
- Bloom, SSAO, TAA, or any new post effect.
- A frame graph, render-target registry, or multi-renderer compositing.
- Any change to the deferred pipeline's shading, pass order, lighting, shadows,
  or sky.
- Gamepad, audio, animation, or anything else from the broader engine roadmap.

## How to build / run / test
```sh
cd /home/rmovs/Work/GameEngine
# bootstrap the build binary if missing, then build:
[ -x ./build ] || clang++ -std=c++23 build.cpp -o build
./build -j "$(nproc)"
# artifact:
ls -l .build/bin/sandbox/model_test
# run (needs an X11 display + Vulkan):
./run --model sandbox/model_test/test_assets/monkey_light.glb
# in-app manual checks: F1/F2 shadow-map debug blit, F3 debug lines,
# F4 ImGui panel (checkboxes + cascade slider drive the renderer), F5 frustum
# snapshot, window resize. Watch stderr for `[sg][error]` / VALIDATE_* / panic.
```
Primary automated gate is a clean `./build` plus greps on the tree. The running
app is a Vulkan/X11 GUI; where a display is present, run it and confirm no
`[sg][error]`/panic on stderr. Where no display is available, the runtime halves
of AC-4/AC-6b/AC-7 are `cannot determine`, not `met`.

## Acceptance criteria
- AC-1: `./build -j "$(nproc)"` completes with no compile or link errors and
  produces `.build/bin/sandbox/model_test`. — status: verified
- AC-2: `src/graphics/renderer.hpp` defines a `graphics::Renderer` `concept`
  (compiled under `-std=c++23`) requiring exactly
  `{ r.render(width, height) } -> std::same_as<sg_view>` (`int width, int
  height`), with a comment stating the contract: `render()` does not call
  `sg_commit()`, and the returned view stays valid until the next `render()`. —
  status: unverified
- AC-3: The old class is renamed, not duplicated: `grep -rn 'class Renderer\b'
  src/` returns nothing; `graphics::DeferredRenderer` is declared in
  `src/graphics/deferred_renderer.hpp` and defined in `deferred_renderer.cpp`;
  `grep -n 'endFrame' src/graphics/deferred_renderer.*` returns nothing; the
  class exposes `sg_view render(int width, int height)`; a
  `static_assert(graphics::Renderer<graphics::DeferredRenderer>)` is present in
  the codebase and `./build` (which compiles it) succeeds. — status: unverified
- AC-4: `graphics::Presenter` is declared in `src/graphics/presenter.hpp` and
  defined in `presenter.cpp`; its `present` is a template whose type parameter is
  constrained by the `graphics::Renderer` concept (a `requires`-clause or
  `static_assert` visible in the header). `grep -rn
  'sg_commit\|sglue_swapchain\|Texture::drainUploads' src/ sandbox/` shows each of
  the three called only from `presenter.cpp` — in particular none appear in
  `deferred_renderer.cpp`. Runtime (display present): `./run --model
  .../monkey_light.glb` renders the scene with the ImGui panel (F4) composited on
  top and survives a window resize with no `[sg][error]` on stderr; headless:
  `cannot determine` for the runtime half. — status: unverified
- AC-5: `render_target.{hpp,cpp}` define `graphics::RenderTarget`,
  `makeColorTarget` and `makeDepthTarget`; `fullscreen_pass.{hpp,cpp}` define
  `graphics::FullscreenPass`; `post.{hpp,cpp}` define `graphics::TonemapPass` and
  `graphics::FxaaPass`. In `deferred_renderer.cpp` each of the five fullscreen
  passes (lighting, sky, tonemap, fxaa, shadow-map debug) is issued through
  `FullscreenPass` — the tonemap and fxaa passes via `TonemapPass`/`FxaaPass`.
  No *definition* of `RenderTarget` or the target factories remains in
  `deferred_renderer.*`:
  `grep -nE 'struct RenderTarget \{|RenderTarget makeColorTarget\(|RenderTarget makeDepthTarget\('
  src/graphics/deferred_renderer.*` returns nothing (the `makeColorTarget` /
  `makeDepthTarget` call sites inside `rebuildTargets()` are expected and fine).
  `./build` links. — status: unverified
- AC-6: `git diff <base>..<head> -- src/graphics/shaders/engine.glsl` shows no
  textual change to the `gbuffer*`, `shadow_depth*`, `lighting`, `sky`,
  `tonemap`, `fxaa`, `shadow_debug` program bodies; its only additions are a
  `present_fs` fragment shader whose body is exactly `out_color =
  texture(sampler2D(tex_source, smp_source), v_uv);` (no exposure/tonemap/gamma)
  and `@program present fullscreen_vs present_fs`. In `deferred_renderer.cpp`
  `render()` issues passes in the order shadow → G-buffer → lighting → sky →
  tonemap → FXAA; the FXAA pass targets the LDR `RenderTarget` (not
  `sglue_swapchain()`); the shadow-map debug blit and `DebugDraw`'s
  `begin()`/`render()` are issued inside that same FXAA pass. — status: unverified
- AC-6b: Visual equivalence of the deferred output before/after the refactor is
  the user's manual A/B (`./run` at a fixed camera against a build from
  `<baseline>`); there is no automated screenshot gate in this environment, so a
  verifier records this `cannot determine`, never `met`. — status: unverified
- AC-7: `sandbox/model_test/main.cpp` constructs a `graphics::DeferredRenderer`
  and a `graphics::Presenter`; the per-frame call is
  `presenter.present(renderer, overlayLambda)` and `grep -n endFrame
  sandbox/model_test/main.cpp` returns nothing; no delta-time value is passed to
  any `renderer.*` call; `cleanup()` releases the presenter before the renderer.
  The F1–F5 handlers still call, respectively,
  `setShadowMapDebugEnabled`/`toggleShadowMapDebug`, `cycleShadowMapDebugLayer`,
  `debugDraw().setEnabled`, the debug-panel visibility toggle, and the frustum
  snapshot; the ImGui panel's two checkboxes and cascade slider still call the
  same renderer setters. `./build` links. Runtime confirmation of each toggle is
  `cannot determine` when headless. — status: unverified
- AC-8: `build.cpp`'s `engine_sources` no longer lists
  `src/graphics/renderer.cpp` and does list `src/graphics/deferred_renderer.cpp`,
  `render_target.cpp`, `fullscreen_pass.cpp`, `post.cpp`, `presenter.cpp`; the
  generated-shader-header `depends_on` edge is attached to every TU that includes
  `engine.glsl.h` (at least `deferred_renderer.cpp`, `post.cpp`,
  `presenter.cpp`). — status: unverified
- AC-9: `docs/graphics/render_target.html`, `docs/graphics/fullscreen_pass.html`,
  `docs/graphics/post.html`, `docs/graphics/presenter.html` exist and each links
  `../style.css`; `docs/graphics/renderer.html` mentions `DeferredRenderer` and
  the `graphics::Renderer` concept and no longer documents an `endFrame` method;
  `docs/index.html` has `href`s to all four new pages; in `AGENTS.md` the "No
  virtual functions" bullet names the `graphics::Renderer` concept and
  `Presenter` as the extension seam, and the "Deferred shading" line still lists
  the shadow → G-buffer → lighting → sky → tonemap → FXAA chain. — status: verified
