---
name: update-docs
description: Updates GameEngine's self-contained HTML docs (docs/) to match the current
  state of src/, and sweeps stale or verbose comments out of source files into those
  docs. Use when the user asks to "update the docs", "refresh the documentation", "sync
  docs with the code", "the docs are out of date", "document the new features", or after
  a chunk of new/changed engine functionality is done and needs the docs caught up.
allowed-tools: Read, Edit, Write, Grep, Glob, Bash
---

# Update docs

`docs/` mirrors `src/` 1:1 — one HTML page per class/concept, module-for-module. See
`references/doc-conventions.md` for the exact template (nav block, style.css, page
shape, comment-trim rule, diagram approach) before writing anything.

1. **Find what changed.** `git log --stat -- src/` (or `git diff <since>...HEAD --
   src/`) since `docs/` was last touched, to scope the work — don't rewrite pages for
   classes that haven't changed.
2. **New or changed class → new or updated doc page.** Follow the per-page shape in
   `references/doc-conventions.md` exactly (role, depends-on/depended-by, real public
   API pulled from the header, non-obvious design decisions, optional diagram only if
   one genuinely earns its weight).
3. **New page → wire it into every nav block**, including `docs/index.html`'s own
   module map/links. `references/doc-conventions.md` has the nav template to copy.
4. **Sweep comments in every touched source file** using the same rule applied
   previously: multi-line rationale/process comments move into the doc page and get
   deleted from source; keep only single-line notes flagging a genuinely non-obvious
   fact at its exact point of use.
5. **Verify.** Clean `./run` build, `git status` shows comment-only diffs on source
   files (no behavior changed), every new/changed doc link resolves, no external
   network references introduced (self-contained, no CDN).
6. **Don't commit.** This repo commits only when explicitly asked (`/commit`) — leave
   the changes staged/unstaged for the user to review.
