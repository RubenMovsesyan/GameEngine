# Finding: spec-checker on spec
verdict: fail
mode: spec-review
range: n/a

## Summary
The build/run/test block works verbatim on a clean `main`: bootstrap, `./build -j
"$(nproc)"`, the artifact check, and `./run --model .../monkey_light.glb` all
succeed (a display is available in this environment, so the GUI launches with only
a benign DPI warning). AC-1, AC-3 and AC-8 are fully observable and checkable.
AC-2 is acceptable. AC-4, AC-5, AC-6, AC-7 and AC-9 lean on code-structure
judgments or unbounded phrases ("keep working", "reworked", "behaviourally
unchanged", "per the skill") and need tightening, and no criterion gives an
observable gate for the Goal's central promise that the deferred renderer's
visual output is unchanged.

## Spec issues
- AC-1: ok
- AC-2: ok
- AC-3: ok
- AC-4: rewrite: `graphics::Presenter` lives in `src/graphics/presenter.{hpp,cpp}`,
  is a template whose parameter is constrained by the `graphics::Renderer`
  concept (`static_assert` or `requires` visible in the header), and its
  `present(renderer, overlay)` calls `sg_commit()` exactly once. `grep -rn
  'sg_commit\|sglue_swapchain\|Texture::drainUploads' src/ sandbox/` shows
  `sg_commit`, `sglue_swapchain` and `drainUploads` each called from
  `presenter.cpp` only and from no other translation unit (in particular not in
  `deferred_renderer.cpp`). Observable behaviour: running `./run --model
  .../monkey_light.glb` renders the scene with the ImGui panel composited on top
  and survives a window resize without validation errors on stderr. (Drop the
  "no sglue_swapchain in engine.glsl" half — GLSL never contains that C symbol.)
- AC-5: rewrite: `render_target.{hpp,cpp}` define `graphics::RenderTarget` plus
  `makeColorTarget` and `makeDepthTarget`; `fullscreen_pass.{hpp,cpp}` define
  `graphics::FullscreenPass`; `post.{hpp,cpp}` define `graphics::TonemapPass` and
  `graphics::FxaaPass`. In `deferred_renderer.cpp` each of the five fullscreen
  passes (lighting, sky, tonemap, fxaa, shadow-debug) is constructed via
  `FullscreenPass` (the tonemap and fxaa passes via `TonemapPass`/`FxaaPass`),
  and `grep -n 'struct RenderTarget\|makeColorTarget\|makeDepthTarget'
  src/graphics/deferred_renderer.*` returns nothing. `./build` links.
- AC-6: rewrite: `git diff <base>..<head> -- src/graphics/shaders/engine.glsl`
  shows no textual change to the `gbuffer*`, `shadow_depth*`, `lighting`, `sky`,
  `tonemap`, `fxaa` and `shadow_debug` program bodies, and its only additions are
  a `present` fragment shader and `@program present fullscreen_vs present_fs`
  whose body is exactly `out_color = texture(sampler2D(tex_source, smp_source),
  v_uv);` with no exposure/tonemap/gamma. In `deferred_renderer.cpp` the passes
  in `render()` run in the order shadow -> G-buffer -> lighting -> sky -> tonemap
  -> FXAA, the FXAA pass writes the LDR `RenderTarget` (not the swapchain), and
  the shadow-map debug blit and `DebugDraw::render()` are issued in that same
  FXAA pass. Add a separate criterion for the Goal's visual promise, e.g.
  AC-6b: with `renderer.setFxaaEnabled` unchanged, a screenshot of `./run
  --model .../monkey_light.glb` at a fixed camera is pixel-identical (or within a
  stated tolerance) to the same screenshot built from `<base>`; where no display
  or capture is available this is recorded as the user's manual A/B and the
  criterion is `cannot determine`, not `met`.
- AC-7: rewrite: `sandbox/model_test/main.cpp` constructs a
  `graphics::DeferredRenderer` and a `graphics::Presenter`; the per-frame call is
  `presenter.present(renderer, overlayLambda)` and `grep -n endFrame
  sandbox/model_test/main.cpp` returns nothing; no delta-time argument is passed
  to any `renderer.*` call; `cleanup()` destroys/releases the presenter before
  the renderer. Each key handler still drives its feature: F1 ->
  `setShadowMapDebugEnabled`/`toggleShadowMapDebug`, F2 ->
  `cycleShadowMapDebugLayer`, F3 -> `debugDraw().setEnabled`, F4 -> debug-panel
  toggle, F5 -> frustum snapshot; the ImGui panel's two checkboxes and cascade
  slider still call the same renderer setters. `./build` links. (Where a display
  is present, confirm each key visibly toggles its effect; where absent, record
  `cannot determine` for the runtime half.)
- AC-8: ok
- AC-9: rewrite: `docs/graphics/render_target.html`,
  `docs/graphics/fullscreen_pass.html`, `docs/graphics/post.html` and
  `docs/graphics/presenter.html` exist and each links `../style.css`;
  `docs/graphics/renderer.html` mentions `DeferredRenderer` and the
  `graphics::Renderer` concept and no longer documents an `endFrame` method;
  `docs/index.html` contains `href` links to all four new pages; in `AGENTS.md`
  the "No virtual functions" bullet names the `graphics::Renderer` concept and
  `Presenter` as the extension seam, and the "Deferred shading" line still lists
  the shadow -> G-buffer -> lighting -> sky -> tonemap -> FXAA chain. (State the
  checks inline rather than deferring to `.claude/skills/update-docs`.)

## Run instructions
worked — from `/home/rmovs/Work/GameEngine` on a clean `main`
(`rm -rf .build build`): `clang++ -std=c++23 build.cpp -o build` (exit 0);
`./build -j "$(nproc)"` compiled every TU and linked (exit 0, ~9 s);
`ls -l .build/bin/sandbox/model_test` shows the 3.3 MB executable;
`./run --model sandbox/model_test/test_assets/monkey_light.glb` launched the
window and ran until the test timeout, logging only
`LINUX_X11_QUERY_SYSTEM_DPI_FAILED ... assuming default 96.0`.

## Issues
1. Goal's core promise has no observable acceptance gate — severity: high
   The Goal requires the deferred renderer's visual output be "unchanged" /
   "byte-for-byte the same modulo an added swapchain blit hop", but every AC only
   checks structure (shader diff, pass order, FXAA target). Visual equivalence is
   pushed entirely to "the user's manual A/B step", so the most important
   property of the refactor cannot be marked `met` by the spec-checker. Add an
   explicit screenshot/pixel-diff criterion (see AC-6 rewrite) or state plainly
   that visual equivalence is user-verified and out of the automated gate.
2. Several criteria are verifiable only by reading code, not by observing an
   outcome — severity: medium
   AC-4 ("opens the swapchain pass", "blits the returned view fullscreen",
   "invokes the overlay inside that pass"), AC-5 ("used to build/run every
   fullscreen pass"), and AC-7 ("keybindings keep working") describe internal
   structure. Per the evidence standard a code-reading argument is never `met`.
   The rewrites above pair each with a grep-able fact and/or a runtime
   observation and mark the runtime half `cannot determine` when headless.
3. Unbounded / judgment-call wording — severity: medium
   AC-6 "behaviourally unchanged" and "unchanged in body"; AC-7 "keep working";
   AC-9 "reworked", "rewritten for the concept-based plugin model", "caught up
   per `.claude/skills/update-docs`". Each hides a pass/fail judgment two
   reviewers could disagree on. Rewrites replace them with concrete diffs,
   enumerated handlers, and named substrings.
4. Duplicated / vacuous sub-clauses — severity: low
   "no `sg_commit` in `render()` / `engine.glsl`" appears in both AC-4 and AC-6;
   the Presenter's existence and build wiring are split across AC-4 and AC-8;
   "no `sglue_swapchain` in `engine.glsl`" is vacuous (GLSL has no such symbol).
   The "shadow-map debug blit + `DebugDraw::render()` in the FXAA pass" clause in
   AC-6 overlaps AC-7's "F1/F2/F3 keep working". Consolidate so each fact is
   gated in exactly one criterion.
5. AC-2 "C++20 concept" vs a C++23 codebase — severity: low
   Not wrong (concepts are C++20), and the Goal uses the same phrasing, but
   consider "a `concept` (compiled under `-std=c++23`)" to avoid a reviewer
   flagging the standard mismatch.

status: handled — spec.md AC-4/5/6/7/9 rewritten to grep-able facts + runtime observations; AC-6b added for the visual promise (recorded as user manual A/B, never auto-met); AC-8 widened to "every TU that includes engine.glsl.h".
