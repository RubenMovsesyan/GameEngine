# Finding: test-writer on U-02
verdict: blocked
range: b56a3d9..ffbd8b4

## Summary
This repo has no unit-test framework, no `tests/`/`test/` directory, and no test
runner or test target in the bespoke `build.cpp` (U-01's test-writer already
established this; nothing changed here). U-02's entire added surface —
`graphics::Renderer` concept, `DeferredRenderer::render`, `graphics::Presenter`
— is live-GPU-context code: `render()` records sokol_gfx passes and
`Presenter::present` drains texture uploads, opens the swapchain pass and calls
`sg_commit()`, all of which abort without an initialized sokol_gfx + Vulkan
context on the app frame thread, and there is no headless harness. The one
genuinely unit-checkable addition, `static_assert(Renderer<DeferredRenderer>)`,
is already verified by the compiler on every build; there is no separate
assertion framework to add anything to, and standing up a GPU or compile-only
test harness is out of scope for this unit.

## Test layout
detected: none — no test framework, no `tests/` dir, no runner config
(`pyproject.toml`/`package.json`/`Cargo.toml` absent; `go.mod` absent). Build is
`clang++ -std=c++23 build.cpp -o build` then `./build -j "$(nproc)"`.
The only `test`-directories under the tree are vendored
(`external/Eigen/test`, `external/cgltf/test`, `external/stb/tests`,
`external/sokol/tests`) and are not wired into this project's build.
`sandbox/model_test` is a Vulkan/X11 GUI executable, not a suite. `AGENTS.md`
documents no testing convention (its only "test" mention is
"sandbox/ test executables").
run with: no unit-test command exists. Executable checks only:
`cd /home/rmovs/Work/GameEngine && ./build -j "$(nproc)" && ls -l .build/bin/sandbox/model_test`
and (display present)
`timeout 12 ./run --model sandbox/model_test/test_assets/monkey_light.glb 2>&1 | grep -E '\[sg\]\[error\]|VALIDATE_|panic|SOKOL_ASSERT|Assertion'`
returning nothing.
committed: not committed — no test files created (nothing to commit)

## Tests written
- none — see verdict rationale

## Why no framework was invented
- `DeferredRenderer::render(int,int)` replays the queued draw list through
  shadow / G-buffer / lighting / sky / tonemap / FXAA passes — every step is
  `sg_begin_pass` / `sg_apply_pipeline` / `sg_draw` / `sg_end_pass` on a live
  context. Its ctor builds pipelines and shadow resources via `sg_make_*` /
  `gpu::make*`. Nothing returns a testable value without `sg_setup()` on the
  sokol frame thread.
- `Presenter::present` is a header template constrained by `Renderer`; its body
  calls `beginFrame()` (upload drain + swapchain query), `renderer.render(...)`,
  then `blitAndFinish` (fullscreen blit + `overlay()` + `sg_commit()`). All
  context-bound; the `overlay` hook is opaque by design.
- `graphics::Renderer` (renderer.hpp) is a pure `concept`
  (`{ renderer.render(width, height) } -> std::same_as<sg_view>`). The only
  meaningful assertion about it — that `DeferredRenderer` models it — is
  `static_assert(Renderer<DeferredRenderer>)` at deferred_renderer.hpp:197,
  compiled into `deferred_renderer.cpp` and `sandbox/model_test/main.cpp` on
  every build. A hand-rolled compile-only test file asserting the same thing
  (plus a negative case) would duplicate that check and would still need wiring
  into `build.cpp`, which is out of scope and not this project's convention.
- Per the spec's run block ("Primary automated gate is a clean `./build` plus
  greps on the tree"), unit tests are not the gate for this unit.

## Executable checks performed (informational, not a substitute for unit tests)
- `./build -j "$(nproc)"` — exit 0, produced `.build/bin/sandbox/model_test`
  (3407272 bytes). The build compiles `static_assert(Renderer<DeferredRenderer>)`,
  so AC-3's compile-time half and AC-1's build half hold, and AC-2's concept
  shape compiles under `-std=c++23`.
- `timeout 12 ./run --model sandbox/model_test/test_assets/monkey_light.glb`
  (DISPLAY=:0 present) — app started, loaded the model, ran until the timeout
  kill (SIGTERM, expected for a GUI). stderr scan for `[sg][error]` /
  `VALIDATE_` / `panic` / `SOKOL_ASSERT` / `Assertion`: none found.
- Diff-consistency spot checks (spec-checker's domain, not formally verified
  here): `renderer.hpp` now holds only the `concept`; `class Renderer` is gone
  (`grep -rn 'class Renderer\b' src/` returns nothing); `endFrame` is gone from
  the renderer surface (only a stale doc-comment reference remains in
  `src/app/debug_ui.hpp:34`); `build.cpp` `engine_sources` drops
  `renderer.cpp`, adds `deferred_renderer.cpp` and `presenter.cpp`, and the
  generated-shader-header `depends_on` edge now covers `deferred_renderer.cpp`,
  `post.cpp`, `presenter.cpp`.

## Existing suite
not present

## Issues
none

## Ambiguities
none

## Notes
- `.team/plan.md` shows as modified in `git status` at the end of this task; that
  change is not mine (I created only this finding and ran build/run). `.build/`
  is gitignored. No scratch files or caches were left behind.

status:

status: handled — blocked, expected & terminal (same structural reason as test-writer-U-01: no framework, GPU-context code, no headless harness). static_assert(Renderer<DeferredRenderer>) is compiler-checked every build. Executable checks pass. Not re-dispatched.
