# Finding: spec-checker on U-03
verdict: pass
mode: verification
range: 2c497cb..e271286

## Summary
U-03 adds the four new `docs/graphics/` pages, rewrites `renderer.html` for the
concept + `DeferredRenderer` model, wires `docs/index.html` nav, and updates the
two named `AGENTS.md` lines. Every clause of AC-9 was checked by file inspection
against the tree at HEAD and all hold. Note: HEAD is `4e28a96`, one commit past
`e271286`, but that commit touches only `.team/**`, so the inspected docs tree is
identical to `e271286`.

## Criteria
- AC-9: met — All clauses verified:
  1. `docs/graphics/{render_target,fullscreen_pass,post,presenter}.html` all
     exist (`ls -l docs/graphics/`), and each has
     `<link rel="stylesheet" href="../style.css">` at line 7
     (`grep -n 'style.css'` on each).
  2. `docs/graphics/renderer.html` mentions `DeferredRenderer` (title line 6,
     `<h1>` line 49, `<h2>` line 75, code block line 101, `static_assert(
     Renderer<DeferredRenderer>)` line 133) and the `graphics::Renderer`
     concept (line 52 `<h2>The concept</h2>`, line 54 "`graphics::Renderer`
     (`src/graphics/renderer.hpp`) is the entire…", line 58
     `concept Renderer = requires(T& renderer, int width, int height)`).
     `grep -ni 'endframe' docs/graphics/renderer.html` returns nothing
     (exit 1) — no `endFrame` documented.
  3. `docs/index.html` has `href`s to all four new pages:
     line 20 `graphics/fullscreen_pass.html`, line 25 `graphics/post.html`,
     line 26 `graphics/presenter.html`, line 27 `graphics/render_target.html`
     (`grep -n` on index.html).
  4. `AGENTS.md` "No virtual functions in `gpu/` or `graphics/`" bullet now
     reads "any type satisfying the `graphics::Renderer` concept (one
     requirement — `sg_view render(int, int)` …) is a renderer … `graphics::
     Presenter` takes a renderer's output and blits it to the swapchain … To
     add a strategy … write a new type with a `render()` and hand it to a
     `Presenter` — no base class" — names the concept and `Presenter` as the
     extension seam. The "Deferred shading" line reads "Deferred shading
     (`graphics::DeferredRenderer`, one implementation of the `graphics::
     Renderer` concept): G-buffer → lighting → sky → tonemap → FXAA, with a
     directional cascaded shadow-map pass ahead of the G-buffer." — the full
     shadow → G-buffer → lighting → sky → tonemap → FXAA chain is still listed
     (shadow named as the pass ahead of the G-buffer, matching the pre-refactor
     phrasing). Evidence: `git diff 2c497cb..e271286 -- AGENTS.md`.

## Run instructions
worked — verification done by file inspection only, as the criterion permits
("a running app is not needed"). Did not build/run.

## Issues
None.

status:

status: handled — pass, AC-9 met by file inspection. Flipped AC-9 to verified in spec.md.
