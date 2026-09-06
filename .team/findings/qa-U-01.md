# Finding: qa on U-01
verdict: pass
scope: U-01 — AC-1, AC-5, AC-8
range: e1818e3f27da207823cf481a1f8ef07288bbc2eb..339fb42

## Summary
Clean-from-scratch build succeeds with zero warnings/errors and produces
`.build/bin/sandbox/model_test`. All five test-asset models start and render for
the full run window with no `[sg][error]`, no `VALIDATE_*`, no `[panic]` and no
core dump; the earlier `VALIDATE_APIP_SWAPCHAIN_DEPTH_FORMAT` is gone. The
primitive extraction (RenderTarget / FullscreenPass / TonemapPass / FxaaPass) is
wired through `renderer.cpp` and the shader-staleness build edge covers both TUs
that include the generated header. No high or medium defects found.

## Tests performed
- Clean build `rm -rf .build && clang++ -std=c++23 build.cpp -o build && ./build -j "$(nproc)"` — exit 0, no warnings, no errors — pass (AC-1, AC-8)
- Artifact produced `ls -l .build/bin/sandbox/model_test` — present, 3395632 bytes, executable — pass (AC-1)
- Run monkey_light.glb ~12 s — window opens, renders whole window, killed by timeout (no crash); stderr only `[INFO] Starting Game Engine` + `LINUX_X11_QUERY_SYSTEM_DPI_FAILED` sapp warning — pass
- No `VALIDATE_APIP_SWAPCHAIN_DEPTH_FORMAT` on any run — pass
- cube_sun.glb — `timeout N .build/bin/sandbox/model_test --model .../cube_sun.glb` x2 runs — clean start, 0 error-marker lines both runs — pass
- monkey.glb — x2 runs — clean, 0 error markers — pass
- monkey_light.glb — x3 runs total (12 s, 20 s, 2x short) — clean, 0 error markers — pass
- ico_scene.glb — x2 runs — clean, 0 error markers — pass
- cylinder.glb — x2 runs — clean, 0 error markers — pass
- Interaction F1/F2/F3/F4/F5 — `wtype -k F1..F5` during a 20 s run — keys dispatched (rc=0), app ran full duration, no new stderr, no crash; visible effect not verifiable (Xwayland focus + no screenshot) — cannot determine
- Window resize — no tool available to drive a WM resize — cannot determine
- (phase B) build.cpp engine_sources — adds `render_target.cpp`, `fullscreen_pass.cpp`, `post.cpp`; still lists `renderer.cpp` (rename is U-02) — pass (AC-8)
- (phase B) `grep -rn engine.glsl.h src/ sandbox/` — only `renderer.cpp` and `post.cpp` include it; both are in build.cpp's `depends_on` condition — pass (AC-8)
- (phase B) shader-staleness edge — `touch .build/shaders/engine.glsl.h && ./build -j nproc` — `post.cpp` + `renderer.cpp` (+ `main.cpp` via header dep) recompile, relink, exit 0 — pass (AC-8)
- (phase B) FXAA / shadow-debug pipeline format equivalence — `kSwapchainColorFormat = (sg_pixel_format)0`; `FullscreenPass::create` sets `colors[0].pixel_format = 0` and skips the `depth = SG_PIXELFORMAT_NONE` line for swapchain-format passes, matching the pre-refactor `{}` descriptors that left formats unset; consistent with observed absence of the depth-format validation error — pass (AC-5)
- (phase B) lighting / sky / tonemap pipeline equivalence — `create()` with an explicit non-swapchain format sets `depth = SG_PIXELFORMAT_NONE`, matching the old explicit descs (`kHdrFormat` / `kLdrFormat`, depth NONE) — pass (AC-5)
- (phase B) five fullscreen passes routed through primitives — lighting/sky/shadow-debug via `FullscreenPass`, tonemap via `TonemapPass`, fxaa via `FxaaPass`; `RenderTarget`/`makeColorTarget`/`makeDepthTarget` moved out of `renderer.*` into `render_target.*` — pass (AC-5)
- (phase B) `std::optional` m_tonemap / m_fxaa emplaced in `buildPipelines()` — all five scenes rendered, so init order before `endFrame()` holds — pass
- (phase B) `present_fs` + `@program present` added to engine.glsl — compiled by sokol-shdc, unreferenced in U-01, no runtime effect — pass (see issue 1)

## Issues
1. `present` shader program lands one unit early — severity: low (phase B)
   steps: `git diff e1818e3..339fb42 -- src/graphics/shaders/engine.glsl` shows a new `present_fs` fragment shader and `@program present fullscreen_vs present_fs`
   observed: the `present` program is added in U-01 but nothing references it; `graphics::Presenter` (its only consumer) is scheduled for U-02 per `.team/plan.md`
   expected: harmless — it is exactly the body AC-6 specifies, is compiled cleanly by the shader toolchain, and has no runtime effect until U-02. Flagged only so the engineer knows scope crept forward by one shader program; not a behaviour change.
   affects: none

## Leftover state
none — `.build/` was regenerated (gitignored); temp run logs are under the session scratchpad. `git status --porcelain` shows only untracked findings files (`spec-checker-U-01.md`, `test-writer-U-01.md` from other agents, plus this file). No tracked file touched.

## Criteria affected
- none

status: handled — pass. Clean build, all 5 test models render with no [sg][error]/VALIDATE_/panic, prior depth-format regression confirmed gone. F1-F5/resize cannot-determine (no screenshot); accepted, that is AC-6b/AC-7 territory for U-02 + user manual A/B.
