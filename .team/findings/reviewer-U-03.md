# Finding: reviewer on U-03
verdict: fail
range: 2c497cb..e271286

## Summary
U-03 is a documentation-only unit: it reworks `docs/graphics/renderer.html`, adds
four new `docs/graphics/` pages (render_target, fullscreen_pass, post, presenter),
rewires `docs/index.html`'s nav, and updates two AGENTS.md bullets to describe the
`graphics::Renderer` concept + `Presenter` plugin seam. Overall the pages are
accurate against HEAD, self-contained (only the inert SVG `xmlns` URL), and every
`href` in the six touched pages resolves. One "Worth knowing" claim in
`renderer.html` miscounts the LDR targets, which is a factual error the task asks
to be flagged as non-low.

## Changes made
- none (read-only)

## Behaviour changed
no

## Issues (not fixed)
1. `renderer.html` "Three LDR targets" claim is wrong — severity: medium
   `docs/graphics/renderer.html:196` — the "Worth knowing" bullet is headed
   **"Three LDR targets, to avoid read-after-write."** The renderer has exactly
   two LDR targets: `m_tonemapped` and `m_ldr` (both `kLdrFormat` =
   `SG_PIXELFORMAT_RGBA8` in `deferred_renderer.cpp:19`). The third screen-sized
   post target, `m_hdr`, is `kHdrFormat` = `SG_PIXELFORMAT_RGBA16F` (HDR) — the
   same page's diagram and body label it HDR. The bullet body only ever names the
   two LDR targets, so the "Three" in the heading points a reader at a target that
   does not exist. Fix: "Two LDR targets" (or "Three screen-sized post targets,
   two of them LDR" if the HDR buffer is meant to be counted).

2. `renderer.html` "a G-buffer pass per submitted mesh" — severity: low
   `docs/graphics/renderer.html:77-78` — the intro paragraph says "then a G-buffer
   pass per submitted mesh". `DeferredRenderer::recordGBufferPass()` opens one
   `sg_begin_pass`/`sg_end_pass` (deferred_renderer.cpp:387/429) and issues one
   `sg_draw` per `DrawItem` inside it — one pass, N draws, not N passes. The
   adjacent one-frame diagram is correct (single "G-buffer" box, subtitle
   "per submit()"), so a reader is unlikely to be misled, but the prose should say
   "a G-buffer draw per submitted mesh" or "one G-buffer pass replaying every
   submit()".

## Criteria
- AC-9: met — `docs/graphics/{render_target,fullscreen_pass,post,presenter}.html`
  all exist and each links `../style.css` (line 7 of each). `renderer.html`
  mentions `graphics::Renderer` (concept snippet at lines 57-60, matches
  `src/graphics/renderer.hpp` verbatim) and `DeferredRenderer`, and contains no
  `endFrame` (grep clean). `docs/index.html` nav has `href`s to all four new
  pages (`graphics/render_target.html`, `graphics/fullscreen_pass.html`,
  `graphics/post.html`, `graphics/presenter.html`), all resolving. In `AGENTS.md`
  the "No virtual functions" bullet now names the `graphics::Renderer` concept
  (one requirement `sg_view render(int, int)`) and `graphics::Presenter` as the
  swapchain/`sg_commit()` owner and the extension seam; the "Deferred shading"
  line still lists G-buffer → lighting → sky → tonemap → FXAA with the directional
  cascaded shadow-map pass ahead of the G-buffer. All API signature blocks
  (`DeferredRenderer`, `Presenter`, `FullscreenPass`, `TonemapPass`/`FxaaPass`,
  `RenderTarget` + `makeColorTarget`/`makeDepthTarget`) match the headers at HEAD;
  the one-frame diagram matches `render()`'s pass order and the 4-cascade count
  (`kShadowCascadeCount = 4`). Self-containment holds: `grep 'https\?://'` over the
  six pages returns only the inert `<svg xmlns=...>` in `renderer.html`; no CDN,
  script, or font loads. Marked met despite issue 1 because the AC-9 checklist
  items are all satisfied; the miscount is a separate accuracy defect.

Notes (not faulted, per task scope): `docs/index.html:91` and other class pages
(`debug_draw.html`, `light_manager.html`) still reference `endFrame()` / the
pre-sokol model — pre-existing lag explicitly out of this unit's scope. Build not
run: the change touches zero source files.

status:

status: handled — fail accepted, both issues fixed in docs/graphics/renderer.html in commit "U-03: fixes": #1 (medium) heading corrected to "Two LDR targets" + body now names m_hdr/RGBA16F explicitly; #2 (low) "one G-buffer pass with a draw per submitted mesh". Reviewer excluded from recheck per lite (engineer applied its issues). AC-9 (spec-checker) was already met and is unaffected.
