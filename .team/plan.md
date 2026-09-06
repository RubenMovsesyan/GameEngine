# Plan

| Unit | Title | Serves | Expected files | Status |
|------|-------|--------|----------------|--------|
| U-01 | Extract `graphics/` primitives (RenderTarget, FullscreenPass, post) and compose them in the renderer | AC-1, AC-5, AC-8 | src/graphics/render_target.hpp, src/graphics/render_target.cpp, src/graphics/fullscreen_pass.hpp, src/graphics/fullscreen_pass.cpp, src/graphics/post.hpp, src/graphics/post.cpp, src/graphics/shaders/engine.glsl, src/graphics/renderer.hpp, src/graphics/renderer.cpp, build.cpp | in-progress |
| U-02 | Rename to DeferredRenderer, add the Renderer concept, `render()`+`Presenter`, rewire the sandbox | AC-1, AC-2, AC-3, AC-4, AC-6, AC-6b, AC-7, AC-8 | src/graphics/deferred_renderer.hpp, src/graphics/deferred_renderer.cpp, src/graphics/renderer.hpp, src/graphics/presenter.hpp, src/graphics/presenter.cpp, sandbox/model_test/main.cpp, build.cpp | todo |
| U-03 | Docs pages + index nav + AGENTS.md for the new model | AC-9 | docs/graphics/render_target.html, docs/graphics/fullscreen_pass.html, docs/graphics/post.html, docs/graphics/presenter.html, docs/graphics/renderer.html, docs/index.html, AGENTS.md | todo |

Status values: todo, in-progress, verifying, verified, needs-fix, attention.
Fix units are named U-xx-fixN and serve the same criteria as U-xx.

Notes:
- U-01 and U-02 both touch `renderer.*`, `engine.glsl`, and `build.cpp`; U-02 is
  implemented only after U-01's lock releases.
- U-03 touches only `docs/**` and `AGENTS.md` — disjoint from U-01/U-02, so it is
  implemented while U-02's verification wave runs.
- U-02 uses `git mv` for the rename so `git diff -M` shows a rename, not
  delete+add.
- After U-01 the renderer is still `graphics::Renderer` with `endFrame()` and
  FXAA still targets the swapchain; U-02 does the rename, the `render()` reshape
  (FXAA → LDR target, no `sg_commit`), and the `Presenter`.
