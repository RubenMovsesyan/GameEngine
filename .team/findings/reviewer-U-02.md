# Finding: reviewer on U-02
verdict: pass
range: b56a3d9..ffbd8b4

## Summary
U-02 renames `graphics::Renderer` to `graphics::DeferredRenderer`, turns
`renderer.hpp` into a one-requirement `Renderer` concept, reshapes `endFrame()`
into `sg_view render(int,int)` (no `sg_commit`, FXAA into an owned LDR target),
adds `graphics::Presenter` to own the swapchain blit / upload drain / overlay /
commit, and rewires the sandbox. The refactor is careful: render-target
plumbing is correct with no in-pass aliasing, pass order and all uniform
structs/slots are untouched, and a clean build plus a 12s headless-capable run
against `monkey_light.glb` produced no `[sg][error]`/`VALIDATE_*`/panic. Mode is
read-only, so nothing was changed; only low-severity nits found.

## Changes made
- none (read-only)

## Behaviour changed
no

## Issues (not fixed)
1. Unused `#include <functional>` in deferred_renderer.hpp — severity: low
   src/graphics/deferred_renderer.hpp:17 — the only consumer was the old
   `endFrame(const std::function<void()>&)` signature, now replaced by
   `render(int,int)`. No inline member or declaration in the header uses
   `std::function` anymore (grep for `function` matches only the include). It is
   a leftover from the rename; drop the line.
2. Stale comment references removed method `Renderer::endFrame` — severity: low
   src/app/debug_ui.hpp:34 — comment says "Renderer::endFrame's overlay
   parameter"; that hook is now `Presenter::present`'s `overlay` parameter. File
   is outside this unit's file set, so flagging only; update when that file is
   next owned.
3. Two `beginFrame()` methods with inverted timing in the graphics frame path —
   severity: low
   src/graphics/presenter.cpp:26 / presenter.hpp — `Presenter::beginFrame()` is
   private, does only `Texture::drainUploads()`, and runs from inside
   `present()` near the end of the app frame, whereas
   `DeferredRenderer::beginFrame()` runs at the start. The comment explains it,
   but a name like `drainPendingUploads()` would read straighter. Reasonable
   engineers could differ; noting, not asserting.
4. AC-5 grep wording vs. implementation — severity: low
   src/graphics/deferred_renderer.cpp:233-239 — AC-5 states
   `grep -n 'struct RenderTarget\|makeColorTarget\|makeDepthTarget'
   src/graphics/deferred_renderer.*` "returns nothing", but `rebuildTargets()`
   legitimately *calls* `makeColorTarget`/`makeDepthTarget` (7 hits). The
   definitions live in render_target.cpp as required; this is the same state
   U-01 was verified in, so read as "no definitions / no local struct", the
   criterion holds. Spec text could be tightened to say "no definition of".

## Criteria
- AC-1: met — clean `rm -rf .build && ./build -j` compiles deferred_renderer.cpp,
  presenter.cpp, main.cpp and links `.build/bin/sandbox/model_test`, no warnings.
- AC-2: met — renderer.hpp defines `concept Renderer` under `<concepts>` with
  `requires(T& renderer, int width, int height) { { renderer.render(width,
  height) } -> std::same_as<sg_view>; }` and a comment stating render() must not
  call `sg_commit()` and the returned view stays valid until the next render().
- AC-3: met — `grep -rn 'class Renderer' src/` returns nothing; renderer.cpp is
  git-renamed to deferred_renderer.cpp (diff --stat -M shows the rename);
  `DeferredRenderer` declared in deferred_renderer.hpp / defined in .cpp;
  no `endFrame` in deferred_renderer.*; class exposes `sg_view render(int,int)`;
  `static_assert(Renderer<DeferredRenderer>)` at deferred_renderer.hpp:582,
  compiled by the successful build.
- AC-4: met (structural) / cannot determine (resize + toggle runtime) —
  Presenter declared in presenter.hpp, defined in presenter.cpp; `present` is
  `template <Renderer R> void present(R&, ...)` constrained in the header;
  `grep -rn 'sg_commit\|sglue_swapchain\|Texture::drainUploads' src/ sandbox/`
  shows each called only from presenter.cpp (other hits are comments), none in
  deferred_renderer.cpp. Ran with a display for 12s: scene renders, ImGui
  overlay path active, no `[sg][error]` on stderr. Window resize and F-key
  toggles were not exercised interactively.
- AC-5 (re-check): met — lighting, sky, tonemap, fxaa and shadow-debug passes
  are all issued via `FullscreenPass` / `TonemapPass` / `FxaaPass` in render()
  and buildPipelines(); no `struct RenderTarget`/primitive *definitions* in
  deferred_renderer.*; `./build` links. See issue 4 re: grep literal wording.
- AC-6: met (structural) / AC-6b cannot determine — render() issues shadow →
  G-buffer → lighting → sky → tonemap → FXAA; tonemap targets
  `*m_tonemapped.attachment`, FXAA targets `*m_ldr.attachment` (an owned
  RenderTarget, not `sglue_swapchain()`) and reads `*m_tonemapped.texture` — no
  image is read and written in one pass; the shadow-map debug blit and
  `m_debug_draw.begin()/render()` are inside that FXAA pass; `overlay()` and
  `sg_commit()` are gone from render(). engine.glsl `present_fs` body is exactly
  `out_color = texture(sampler2D(tex_source, smp_source), v_uv);` with
  `@program present fullscreen_vs present_fs`. Uniform structs/slots unchanged.
  DebugDraw's sgl context is now pinned to RGBA8 / depth NONE / sample_count 1,
  which matches the m_ldr target (kLdrFormat = SG_PIXELFORMAT_RGBA8, no depth
  attachment, sample_count unset → 1); DebugDraw is the only sgl context in the
  tree. Visual A/B equivalence has no automated gate here.
- AC-7: met (structural) / cannot determine (per-toggle runtime) — main.cpp
  holds `std::optional<graphics::DeferredRenderer>` + `std::optional<Presenter>`,
  per-frame call is `sandbox.presenter->present(renderer, overlayLambda)`, no
  `endFrame` in main.cpp, no dt passed to any `renderer.*` call (dt goes to
  flyCamera and debugUI().draw only), `cleanup()` does `presenter.reset()`
  before `renderer.reset()`. F1→toggleShadowMapDebug, F2→cycleShadowMapDebugLayer,
  F3→debugDraw().setEnabled, F4→debug-panel visibility, F5→frustum snapshot; the
  panel's two checkboxes call setEnabled / setShadowMapDebugEnabled and the
  slider calls setShadowMapDebugLayer. `./build` links.
- AC-8: met — build.cpp `engine_sources` drops `src/graphics/renderer.cpp`, adds
  `deferred_renderer.cpp` and `presenter.cpp` (render_target/fullscreen_pass/
  post already present from U-01); the generated-shader-header `depends_on` edge
  now covers deferred_renderer.cpp, post.cpp and presenter.cpp — the three TUs
  that include engine.glsl.h.

status:

status: handled — pass. 4 low issues. #1 (unused <functional> include in deferred_renderer.hpp) and #2 (stale endFrame comment in src/app/debug_ui.hpp) will be fixed in the U-02 settle commit. #3 (Presenter::beginFrame naming) rejected: it is the frame-start drain despite running late in the callback; "beginFrame" reads fine against the concept lifecycle. #4 is a clarification, not a defect (AC-5 reads as "no local definitions", which holds).
