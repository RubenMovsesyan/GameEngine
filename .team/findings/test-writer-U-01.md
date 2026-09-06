# Finding: test-writer on U-01
verdict: blocked
range: e1818e3f27da207823cf481a1f8ef07288bbc2eb..339fb42

## Summary
This repo has no unit-test framework, no `tests/`/`test/` directory, no test
runner, and no test target in the bespoke `build.cpp`. Every primitive this unit
adds (`RenderTarget`/`makeColorTarget`/`makeDepthTarget`, `FullscreenPass`,
`TonemapPass`, `FxaaPass`) is a thin wrapper over live `sg_*` / `gpu::` calls
that require an initialized sokol_gfx + Vulkan context; there is no headless GPU
harness to exercise them and standing one up is out of scope for this unit.
Unit tests are not feasible here, so no test files were written. The only
executable checks available — `./build` and `./run` — both pass.

## Test layout
detected: none — no test framework, no `tests/` dir, no runner config
(`pyproject.toml`/`package.json`/`Cargo.toml` absent; build is
`clang++ -std=c++23 build.cpp -o build` then `./build`). `sandbox/model_test`
is a Vulkan/X11 GUI executable, not a test suite. `AGENTS.md` documents no
testing convention.
run with: no unit-test command exists. Fallback executable checks only:
`cd /home/rmovs/Work/GameEngine && ./build -j "$(nproc)" && ls -l .build/bin/sandbox/model_test`
and (display present) `timeout 12 ./run --model sandbox/model_test/test_assets/monkey_light.glb 2>&1 | grep -E '\[sg\]\[error\]|VALIDATE_|panic'` returning nothing.
committed: not committed — no test files created (nothing to commit)

## Tests written
- none — see verdict rationale

## Why no framework was invented
- The unit's public surface is entirely GPU-context-bound:
  `FullscreenPass::create` calls `sg_make_shader` + `sg_make_pipeline`;
  `FullscreenPass::draw` calls `sg_apply_pipeline`/`sg_apply_bindings`/`sg_draw`;
  `makeColorTarget`/`makeDepthTarget` call `gpu::makeImage`/`gpu::makeView`;
  `TonemapPass`/`FxaaPass` ctors call `*_shader_desc(sg_query_backend())`.
  All abort or return invalid handles without `sg_setup()` on the sokol_app
  frame thread.
- The one context-free item, `kSwapchainColorFormat`
  (`static_cast<sg_pixel_format>(0)`), is a one-line constexpr with no behaviour
  to assert beyond `!= SG_PIXELFORMAT_NONE`, which is tautological.
- Per the task brief and spec ("Primary automated gate is a clean `./build`
  plus greps on the tree"), adding a GPU/unit-test harness is not this unit's
  job.

## Executable checks performed (informational, not a substitute for unit tests)
- `./build -j "$(nproc)"` — exit 0, produced `.build/bin/sandbox/model_test`
  (3395632 bytes). Satisfies AC-1's build half.
- `timeout 12 ./run --model sandbox/model_test/test_assets/monkey_light.glb` —
  app started, loaded the model, ran until the timeout kill (exit 124, expected
  for a GUI). stderr scan for `[sg][error]` / `VALIDATE_` / `panic` /
  `SOKOL_ASSERT` / `Assertion`: none found. Only output was a benign
  `LINUX_X11_QUERY_SYSTEM_DPI_FAILED` sokol_app warning.
- AC-5 / AC-8 grep-level claims were not formally verified here (spec-checker's
  domain), but observed consistent with the diff: `build.cpp` `engine_sources`
  now lists `render_target.cpp`, `fullscreen_pass.cpp`, `post.cpp`; `post.cpp`
  is wired to `depends_on(*shader_task)`. Note `renderer.cpp` (not yet renamed
  to `deferred_renderer.cpp` — that is U-02) still includes `engine.glsl.h` and
  keeps its shader dependency edge.

## Existing suite
not present

## Issues
none

## Ambiguities
none

status:

status: handled — expected & terminal for this project: no test framework exists and every U-01 symbol needs a live sokol_gfx/Vulkan context with no headless harness. Spec makes `./build` + greps the gate, not unit tests. Test-writer confirmed clean build + clean `./run` startup. Not re-dispatched (structural blocker, not fixable in this unit); test-writer will be blocked identically on U-02.
