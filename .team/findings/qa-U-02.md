# Finding: qa on U-02
verdict: pass
scope: U-02 — AC-1, AC-3, AC-4, AC-6, AC-6b, AC-7
range: b56a3d9..ffbd8b4

## Summary
Clean-from-scratch build succeeds with zero warnings and produces the artifact; all
six test-asset models run for ~9 s with no `[sg][error]` / `VALIDATE_*` / `[panic]` /
`SOKOL_ASSERT` / core dump. Live window resizes (including a shrink to 400x300 and
regrow) produce no renderer errors. F1-F5 / F4-panel toggles and visual A/B
equivalence (AC-6b) are `cannot determine`: keystroke delivery into the XWayland
window could not be confirmed and there is no screenshot capability here.

## Tests performed
- AC-1 clean build — `rm -rf .build && ./build -j "$(nproc)"` — pass (exit 0, 0 warnings)
- AC-1 artifact present — `ls -l .build/bin/sandbox/model_test` — pass (3407272-byte ELF x86-64 PIE)
- AC-1 incremental rebuild (after a concurrent agent wiped `.build` mid-run) — `./build -j "$(nproc)"` — pass (exit 0)
- Run cube_sun.glb ~9 s, grep stderr — `model_test --model .../cube_sun.glb` — pass
- Run monkey.glb ~9 s, grep stderr — `model_test --model .../monkey.glb` — pass
- Run monkey_light.glb ~9 s, grep stderr — `model_test --model .../monkey_light.glb` — pass
- Run ico_scene.glb ~9 s, grep stderr — `model_test --model .../ico_scene.glb` — pass
- Run cylinder.glb ~9 s, grep stderr — `model_test --model .../cylinder.glb` — pass
- Run smile_cube.gltf ~9 s, grep stderr — `model_test --model .../smile_cube.gltf` — pass
- AC-4 runtime resize — `hyprctl dispatch resizewindowpixel exact {800x600,1280x720,400x300,1600x900}` on the live window — pass (no `[sg][error]`, process stayed alive)
- CLI: no args — `model_test` — pass (prints `[FATAL]: --model <path>.glb|.gltf is required`, no crash)
- CLI: `--model` with no value — `model_test --model` — pass (`[FATAL]: Missing value for --model`)
- CLI: nonexistent model — `model_test --model /nope/nothere.glb` — pass (`[FATAL]: gltf: failed to parse ...`, no crash)
- CLI: `--help` — `model_test --help` — pass (`[FATAL]: Unknown argument: --help`, no crash)
- CLI: bogus flag — `model_test --frobnicate` — pass (`[FATAL]: Unknown argument: --frobnicate`, no crash)
- Regression watch F3/F1/F2/F1/F3/F4/F5 (debug lines, shadow-map blit, cascade cycle, ImGui panel, frustum snapshot) — window focused via `hyprctl dispatch focuswindow`, `wtype -k F3` … — cannot determine (no error/crash, but key delivery unconfirmable — see issue #1)
- AC-6b visual A/B vs pre-refactor — n/a — cannot determine (no screenshot capability; user's manual A/B)
- (phase B) DebugDraw sgl context format vs its render target — code inspection: `ctx_desc.color_format=SG_PIXELFORMAT_RGBA8, depth_format=NONE, sample_count=1` vs `m_ldr` = `kLdrFormat` (`SG_PIXELFORMAT_RGBA8`, colour-only) — pass (formats consistent; runtime path not reachable, see #1)
- (phase B) lazy render-target build / zero-size first frame — code inspection of `DeferredRenderer::render` + `rebuildTargets` — see issue #2
- (phase B) `Texture::drainUploads()` moved to `Presenter::beginFrame()` ordering vs `submit()` — code inspection — pass (drain still precedes all texture sampling, which happens in `render()`)
- (phase B) `static_assert(Renderer<DeferredRenderer>)` / `present` constrained by `Renderer` concept / `cleanup()` releases presenter before renderer — code inspection + build compiles it — pass

## Issues
1. F1-F5 and ImGui-panel toggles could not be exercised at runtime — severity: low
   steps: launch `.build/bin/sandbox/model_test --model .../monkey_light.glb`;
     `hyprctl dispatch focuswindow address:<win>`; `wtype -k F3` (also F1/F2/F4/F5);
     4x `hyprctl dispatch resizewindowpixel`.
   observed: process stays alive, stderr never grows past the initial
     `"[INFO]: Starting Game Engine"` + `"[sapp][warning] ... LINUX_X11_QUERY_SYSTEM_DPI_FAILED"`;
     no `[sg][error]` / panic. The F-key handlers emit no log, so "no new output" does
     not prove the keys were delivered and handled. Session is Wayland/Hyprland and the
     app is XWayland; `wtype` virtual-keyboard events into that surface are not
     confirmable from here.
   expected: the regression-watch step of the task and the runtime halves of AC-4/AC-7
     want each toggle confirmed to draw with no `[sg][error]`. That confirmation is
     `cannot determine` in this environment, not `met`. Resize (which needs no keyboard)
     was exercised and is clean.
   affects: AC-4, AC-7 (runtime half only)

2. `render()` has no guard if the first frame's size is 0x0 — severity: low (phase B)
   steps: not reproducible on this platform (window always opens at a real size; 6
     models + 4 resizes down to 400x300 all clean). By inspection:
     `DeferredRenderer()` no longer builds targets; `render(w,h)` builds them only when
     `w != m_width || h != m_height`; `rebuildTargets` returns early on `w<=0 || h<=0`
     without setting `m_width/m_height`. A first `render(0,0)` would then fall through to
     `*m_hdr.attachment` / `*m_ldr.texture` on empty `std::optional`s.
   observed: n/a (not triggered).
   expected: a renderer that pulls no size from `sokol_app` should tolerate a degenerate
     first size the way the pre-refactor constructor path effectively did (it built from
     `sapp_width/height`, non-zero at startup). Risk is largely relocated rather than
     newly introduced, and unreachable in practice here.
   affects: AC-4

## Leftover state
none — `./build` (a git-ignored bootstrap artifact that pre-existed this session) was
deleted and immediately rebuilt from `build.cpp`; byte size is identical (620952),
only mtime differs. `.build/` was removed per the task instructions. `git status
--porcelain` matches the clean start-of-session snapshot. Note: concurrent team agents
modified `.team/plan.md` and added `docs/graphics/render_target.html` and
`.team/findings/test-writer-U-02.md` partway through this run; those are not mine and
the tree was clean again by the end.

## Criteria affected
- AC-4: #1, #2
- AC-7: #1

status:

status: handled — pass. Issue #1 (F1-F5/panel toggles not runtime-verifiable here) is environmental, not a defect: cannot determine for the AC-4/AC-7 runtime halves. Issue #2 (render() would deref null target handles if first frame is 0x0) accepted as low but worth a cheap guard: to be fixed in the U-02 settle commit (once the lock releases) with an early-out in render() + an invalid-view guard in Presenter. QA confirms it is not newly introduced (old ctor/beginFrame path had the same 0x0 fragility) and not reachable on this X11 platform.
