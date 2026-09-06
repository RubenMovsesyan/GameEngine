# Finding: spec-checker on U-01
verdict: pass
mode: verification
range: e1818e3f27da207823cf481a1f8ef07288bbc2eb..339fb42

## Summary
U-01 extracts the `graphics::RenderTarget`/`makeColorTarget`/`makeDepthTarget`,
`graphics::FullscreenPass`, and `graphics::TonemapPass`/`graphics::FxaaPass`
primitives into three new source pairs and rewires the existing renderer to
compose them. A clean `./build` completes with no errors and produces the
sandbox artifact; all five fullscreen passes in `renderer.cpp` are now issued
through the new primitives; `build.cpp` lists the three new sources and carries
the shader `depends_on` edge on every TU that includes `engine.glsl.h`. The
rename-dependent clauses of AC-5 and AC-8 (`deferred_renderer.*`,
`presenter.cpp`) are out of scope at this head and are noted as pending U-02 per
the task's scope note.

## Criteria
- AC-1: met — from a clean tree: `rm -rf .build && ./build -j "$(nproc)"` exited
  0 with only `[INFO]` lines (compiles `src/graphics/render_target.cpp`,
  `post.cpp`, links `.build/bin/sandbox/model_test`); `ls -l
  .build/bin/sandbox/model_test` -> `-rwxr-xr-x ... 3395632 ... model_test`.
  Objects `.build/src/graphics/{render_target,fullscreen_pass,post}.o` all
  present.
- AC-5: met — `render_target.{hpp,cpp}` define `graphics::RenderTarget` (struct,
  hpp:11), `makeColorTarget` (hpp:19 / cpp:5) and `makeDepthTarget` (hpp:23 /
  cpp:29); `fullscreen_pass.{hpp,cpp}` define `graphics::FullscreenPass`
  (hpp:21, cpp:5/23); `post.{hpp,cpp}` define `graphics::TonemapPass` (hpp:11,
  cpp:20/23) and `graphics::FxaaPass` (hpp:24, cpp:34/37). In
  `src/graphics/renderer.cpp` all five fullscreen passes are issued through the
  primitives: lighting via `m_lighting_pass` (`FullscreenPass`, `.draw` line
  472), sky via `m_sky_pass` (`FullscreenPass`, line 504), tonemap via
  `m_tonemap` (`std::optional<TonemapPass>`, `->draw` line 525), fxaa via
  `m_fxaa` (`std::optional<FxaaPass>`, `->draw` line 537), shadow-map debug via
  `m_shadow_debug_pass` (`FullscreenPass`, `.draw` line 544). `./build` links
  (see AC-1). The clause `grep 'struct RenderTarget\|makeColorTarget\|
  makeDepthTarget' src/graphics/deferred_renderer.*` returns nothing: cannot
  determine (pending U-02) — that file does not exist at this head; the rename
  lands in U-02. `grep` for the same pattern in `src/graphics/renderer.*` shows
  only six legitimate call sites in `rebuildTargets` (renderer.cpp:235-240) and
  no redefinition.
- AC-8: met (U-01 scope) — `git diff` of `build.cpp` adds
  `src/graphics/render_target.cpp`, `src/graphics/fullscreen_pass.cpp`,
  `src/graphics/post.cpp` to `engine_sources`, and broadens the
  generated-shader-header `depends_on` guard to
  `source == "src/graphics/renderer.cpp" || source == "src/graphics/post.cpp"`.
  `grep -rl engine.glsl.h src/ sandbox/` returns exactly `renderer.cpp` and
  `post.cpp` at this head, so every including TU carries the edge. The clauses
  "`engine_sources` no longer lists `renderer.cpp`" and "does list
  `deferred_renderer.cpp`, `presenter.cpp`", and the `depends_on` edge on
  `deferred_renderer.cpp`/`presenter.cpp`: cannot determine (pending U-02) — the
  rename and Presenter are U-02 work and per the plan the renderer is still
  `graphics::Renderer` after U-01.

## Spec issues
(omitted — verification mode)

## Run instructions
worked — `[ -x ./build ] || clang++ -std=c++23 build.cpp -o build` then
`./build -j "$(nproc)"` exited 0; `ls -l .build/bin/sandbox/model_test` shows
the artifact. The `./run` step was not exercised (no criterion in scope for this
unit requires runtime; AC-1/AC-5/AC-8 are build- and grep-based).

## Issues
1. AC-5 not listed under U-02 in plan.md — severity: low
   plan.md maps AC-5 to U-01 only, but the AC-5 `deferred_renderer.*` grep
   clause cannot be verified until the U-02 rename. AC-5 will need a re-check
   during the U-02 verification wave even though the plan does not route it
   there.

status: handled — pass. AC-1 met (flipped to verified in spec.md). AC-5 / AC-8 met for the U-01 scope; the `deferred_renderer.*` grep sub-clauses stay unverified until the U-02 wave re-checks them (U-02 serves both).
