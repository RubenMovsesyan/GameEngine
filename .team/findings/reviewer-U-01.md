# Finding: reviewer on U-01
verdict: pass
range: e1818e3f27da207823cf481a1f8ef07288bbc2eb..339fb42

## Summary
U-01 lifts `RenderTarget`/`makeColorTarget`/`makeDepthTarget`, a `FullscreenPass`
pipeline wrapper, and `TonemapPass`/`FxaaPass` out of `renderer.cpp` into new
`graphics/` primitives, and rewires the still-named `Renderer` to compose them;
it also adds a spec-mandated `present` program to `engine.glsl` (unused until
U-02). Mode is read-only, so I made no edits. Quality is good: the extraction is
faithful, pipeline state is provably unchanged, and the five fullscreen passes
bind the same slots and draw the same geometry.

## Changes made
none (read-only)

## Behaviour changed
no

## Issues (not fixed)
1. `applyUniforms` duplicated verbatim — severity: low
   src/graphics/post.cpp:13-18 vs src/graphics/renderer.cpp:74-79 — the same
   4-line `template <typename T> void applyUniforms(int, const T&)` now lives in
   two anonymous namespaces. It is a template, so sharing means a header; a
   natural home is `fullscreen_pass.hpp` (already included by both TUs) or
   `gpu/handle.hpp`. Left as a judgement call rather than fixed.

2. `std::function` callback on the per-frame pass path — severity: low
   src/graphics/fullscreen_pass.hpp:34, fullscreen_pass.cpp:24-30 — `draw()` takes
   `const std::function<void()>&` and the call sites pass `[&]` lambdas capturing
   several references, which exceeds libstdc++'s `std::function` small-buffer size
   and heap-allocates once per fullscreen pass per frame. The pre-U-01 code packed
   uniforms inline with no indirection. Magnitude is negligible (5 tiny allocs per
   frame), but it is a new allocation on the render hot path; a non-owning
   callable (`function_ref`-style) or templating `draw()` would avoid it.

3. Named fullscreen-triangle vertex count dropped — severity: low
   src/graphics/fullscreen_pass.cpp:29 — `kFullscreenTriangleVertexCount` was
   removed from renderer.cpp and `draw()` now calls `sg_draw(0, 3, 1)` with a bare
   literal. A local `static constexpr int kFullscreenTriangleVertices = 3;` keeps
   the intent legible.

4. `FullscreenPass::create` couples output format with depth presence — severity: low
   src/graphics/fullscreen_pass.cpp:11-13 — "if `output_format != kSwapchainColorFormat`,
   set `depth.pixel_format = SG_PIXELFORMAT_NONE`" is correct for all five current
   passes and is documented, but it silently assumes every offscreen fullscreen
   target has no depth attachment. A future offscreen pass that wants depth would
   need to bypass this helper. An explicit `sg_pixel_format depth_format`
   (defaulted to NONE) or `bool has_depth` parameter would make the contract
   visible. Not a defect today.

## Criteria
- AC-1: met — `rm -rf .build && ./build -j "$(nproc)"` completes with no compile
  or link errors and produces `.build/bin/sandbox/model_test`; ran five clean
  parallel builds (all exit 0, one `sokol-shdc` invocation each) and an
  incremental rebuild after touching `engine.glsl` (regenerates the header, then
  recompiles `post.cpp` + `renderer.cpp`, links clean). One earlier failure came
  from a pre-existing stale `.build` cache carried into the session, not
  reproducible from a clean tree.
- AC-5: met (for U-01's partial scope) — `render_target.{hpp,cpp}` define
  `graphics::RenderTarget`, `makeColorTarget`, `makeDepthTarget`;
  `fullscreen_pass.{hpp,cpp}` define `graphics::FullscreenPass`; `post.{hpp,cpp}`
  define `graphics::TonemapPass` and `graphics::FxaaPass`. In `renderer.cpp` all
  five fullscreen passes are issued through these primitives — lighting / sky /
  shadow-map debug via `FullscreenPass`, tonemap / fxaa via
  `TonemapPass`/`FxaaPass` (renderer.cpp:471-533). `struct RenderTarget`,
  `makeColorTarget`, `makeDepthTarget` are deleted from `renderer.{hpp,cpp}`. The
  `Renderer` -> `DeferredRenderer` rename, the `Renderer` concept, and
  `Presenter` are correctly absent (U-02). Pipeline state is unchanged: checked
  against sokol's default resolution in `external/sokol/sokol_gfx.h:25829-25834` —
  fxaa/shadow_debug previously left `color_count` 0 (sokol defaults it to 1) with
  colour and depth formats unset, so they resolved to the swapchain formats;
  `create(kSwapchainColorFormat, ...)` now sets `color_count = 1` explicitly and
  leaves both formats at 0, resolving identically. lighting/sky/tonemap keep an
  explicit HDR/LDR colour format and `depth = SG_PIXELFORMAT_NONE` in both old and
  new code (the old `sky` desc was a copy of `lighting`, same net state). No
  rasteriser/blend/depth field changed. All five passes bind the same view and
  sampler slots, write the same uniform structs into the same `UB_*` slots, and
  draw 3 vertices / 1 instance. `present_fs` shares `tex_source`/`smp_source` at
  `binding=0` exactly as `tonemap_fs`/`fxaa_fs`; the regenerated header compiles
  and `renderer.cpp` still resolves `VIEW_tex_source`/`SMP_smp_source`.
  `std::optional<TonemapPass>/<FxaaPass>` teardown is safe: `FullscreenPass` holds
  a raw `sg_pipeline` with a trivial destructor (consistent with `Renderer`'s
  other raw `sg_pipeline` members — the codebase has no pipeline RAII handle), so
  optional destruction never calls into sokol and cannot race `sg_shutdown()`;
  `buildPipelines()` runs once from the constructor (renderer.cpp:140), so the
  `emplace()` is not a reconstruct-and-leak path.
- AC-8: met (for U-01's partial scope) — `build.cpp`'s `engine_sources` gains
  `render_target.cpp`, `fullscreen_pass.cpp`, `post.cpp`; `renderer.cpp` stays
  (rename is U-02). `grep -rn 'engine.glsl.h' src/ sandbox/` returns exactly
  `renderer.cpp` and `post.cpp`, and build.cpp:95 attaches the shader-header
  `depends_on` to both. Verified the edge works: an incremental build after
  touching `engine.glsl` reran `sokol-shdc` before recompiling `post.cpp`.

status: handled — pass. All 4 issues are low. Deferred per lite; #1 (applyUniforms dup) and #3 (kFullscreenTriangleVertexCount literal) folded into U-02 which rewrites these files anyway. #2 (std::function per-call) and #4 (format<->depth coupling in FullscreenPass::create) deferred: lite — reported, not re-verified; neither is hit by a current caller.
