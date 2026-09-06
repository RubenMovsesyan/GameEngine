# Finding: qa on U-03
verdict: pass
scope: U-03 — AC-9
range: 2c497cb..e271286

## Summary
U-03 is a documentation-only change: four new `docs/graphics/` pages, a rewritten
`renderer.html`, updated `docs/index.html` nav, and two revised `AGENTS.md`
bullets. Every concrete check in AC-9 holds: pages exist and link the stylesheet,
`renderer.html` drops `endFrame` and names the concept + `DeferredRenderer`, the
index links all four new pages, `AGENTS.md` names the seam and keeps the deferred
chain, all hrefs in the six changed pages resolve, no external resource loads in
those pages, and `./build` still succeeds. No high or medium issues.

## Tests performed
- Build unchanged sources — `./build -j "$(nproc)"` — pass (exit 0, `.build/bin/sandbox/model_test` linked)
- Four new pages exist — `ls` on render_target/fullscreen_pass/post/presenter.html — pass
- Each new page links `../style.css` — `grep -n style.css` (all at line 7: `<link rel="stylesheet" href="../style.css">`) — pass
- renderer.html mentions `DeferredRenderer` — `grep -c DeferredRenderer` = 6 — pass
- renderer.html mentions `graphics::Renderer` concept — `grep -n 'graphics::Renderer'` (line 54, "is the entire contract") — pass
- renderer.html no longer documents `endFrame` — `grep -n endFrame docs/graphics/renderer.html` (no match, exit 1) — pass
- index.html links all four new pages — `grep -n` for the four filenames (lines 20, 25, 26, 27) — pass
- AGENTS.md "No virtual functions" bullet names `graphics::Renderer` concept + `Presenter` — read lines 26-35 — pass
- AGENTS.md "Deferred shading" line lists shadow-map / G-buffer / lighting / sky / tonemap / FXAA — read lines 92-95 — pass
- All hrefs in the 6 changed pages resolve to a file on disk (nav + inline) — resolved every `href` per page — pass (0 missing)
- No external resource loads in the 6 changed pages — `grep -rn 'https\?://' docs/index.html docs/graphics/{index,renderer,render_target,fullscreen_pass,post,presenter}.html` — pass (only `docs/graphics/renderer.html:137` SVG `xmlns` URI, which is a namespace id not a network load)
- HTML tag balance in all 6 changed pages — parser check — pass (all balanced)
- No TODO/FIXME/placeholder text in the 6 changed pages — `grep -rn -i` — pass
- Render pages in a browser — not done (no HTML renderer available); structural + grep checks only, as AC-9 point 8 permits

## Issues
none

## Leftover state
none of my making. My only write is this finding file. During the run an
untracked file `.team/findings/qa-U-02.md` appeared in `git status --porcelain`
(absent from the step-1 snapshot, which was clean); it was created by a
concurrent agent, not by this task, so I left it in place. No build artifacts
outside `.build/` (gitignored). No processes or ports were started.

## Criteria affected
- none

status:

status: handled — pass, no high/medium. AC-9 sub-checks all hold; links resolve; self-contained. (debug_draw.html GitHub link noted out-of-scope, correct — not an AC-9 page.)
