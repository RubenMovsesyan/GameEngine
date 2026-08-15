# doc-conventions

Detailed conventions for the `update-docs` skill. Load this before touching any file.

## Directory shape

```
docs/
  index.html          landing page: architecture overview, module map, links to every page
  style.css            shared stylesheet — reuse it, don't recreate. Add rules if a
                        page genuinely needs something new; don't fork the file.
  gpu/<name>.html
  graphics/<name>.html
  scene/<name>.html
  app/<name>.html
  utils/<name>.html
```

One page per class/concept, filename matching the class in lowercase snake_case (e.g.
`graphics::LightManager` → `graphics/light_manager.html`). A free function that isn't a
class (e.g. `scene::loadGltf`) still gets its own page, named after the function.

## Self-containment (non-negotiable)

- No CDN links, no external fonts, no `<script src="https://...">`. Everything must
  work opened as a local file with no network access.
- Code blocks are plain `<pre><code>` styled by `style.css` alone — no JS syntax
  highlighter.
- If a page genuinely needs a diagram (a pipeline flow, a data-flow chart — not most
  pages), prefer a **hand-rolled inline `<svg>`** over pulling in a charting library.
  A full Plotly bundle is multiple MB for what's usually a simple box-and-arrow
  diagram; not worth it. See `graphics/renderer.html` and
  `graphics/post_process_chain.html` in this repo for two worked SVG examples.
- After writing/editing pages: grep the touched files for `http://`/`https://` and
  confirm the only matches are inert (e.g. the SVG xmlns URI, which doesn't fetch
  anything) — never a real external resource load.

## Nav block (copy into every page, including new ones)

Every page gets the identical nav, with `class="here"` moved to the current page's own
link. Relative paths: pages one level deep under `docs/` (e.g. `docs/graphics/x.html`)
use `../` to reach `index.html`/`style.css` and other modules; `index.html` itself uses
no prefix. When you add a new page, add its link to this block **in every existing
page**, not just the new one — grep for `<h4>graphics</h4>` (or the relevant module) to
find every copy that needs the new `<a>` line.

```html
<nav>
  <a class="brand" href="../index.html">Game<span>Engine</span></a>
  <span class="tag">C++23 · Vulkan · NVRHI · Linux</span>

  <h4>gpu</h4>
  <a href="../gpu/instance.html">Instance</a>
  <a href="../gpu/device.html">Device</a>
  <a href="../gpu/swapchain.html">Swapchain</a>

  <h4>graphics</h4>
  <a href="../graphics/camera.html">Camera</a>
  <a href="../graphics/light.html">Light</a>
  <a href="../graphics/light_manager.html">LightManager</a>
  <a href="../graphics/mesh.html">Mesh</a>
  <a href="../graphics/post_process_chain.html">PostProcessChain</a>
  <a href="../graphics/renderer.html">Renderer</a>
  <a href="../graphics/surface.html">Surface</a>

  <h4>scene</h4>
  <a href="../scene/model.html">Model</a>
  <a href="../scene/scene.html">Scene</a>
  <a href="../scene/gltf_loader.html">loadGltf</a>

  <h4>app</h4>
  <a href="../app/application.html">Application</a>
  <a href="../app/cli_parser.html">CliParser</a>
  <a href="../app/window.html">Window</a>
  <a href="../app/worker.html">Worker</a>

  <h4>utils</h4>
  <a href="../utils/display.html">Display</a>
</nav>
```
(Add new `<h4>module</h4>` groups / `<a>` entries as new modules/classes appear. Mark
the current page's own `<a>` with `class="here"` instead of a plain `href`-only tag.)

## Per-page shape

Every page (except `index.html`) follows this skeleton — same structure whether it's a
brand new page or an update to an existing one:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>module::ClassName — GameEngine</title>
<link rel="stylesheet" href="../style.css">
</head>
<body>
<div class="wrap">
<nav>...</nav>
<main>

<h1><code>module::ClassName</code></h1>
<p class="lede">One sentence: what it is, in the reader's terms.</p>

<h2>Role</h2>
<p>What it does and why it exists — its job in the architecture, not a method list.</p>

<h2>Depends on / depended on by</h2>
<p>What it's built from, what calls into it. Name the actual classes.</p>

<h2>Public API</h2>
<pre><code>// real signatures, copied from the current header — not paraphrased</code></pre>

<h2>Worth knowing</h2>
<ul>
  <li>Non-obvious design decisions that would otherwise live as source comments —
      this is where that rationale goes once it's stripped from the .hpp/.cpp.</li>
</ul>

<footer>Source: <code>src/module/name.hpp</code>, <code>src/module/name.cpp</code></footer>

</main>
</div>
</body>
</html>
```

Keep "Worth knowing" to real, specific gotchas (unit conversions, sign conventions,
threading caveats, "this used to work differently and here's why it changed") — not
generic filler. If a design decision has a good story (a bug it fixed, a tradeoff it
resolved), tell that story here instead of compressing it to nothing.

## Comment-trim rule (applies to every touched source file, not just new ones)

Delete any comment — regardless of line count — that narrates *process*: why this
approach was chosen over an alternative, what bug it fixed, what an earlier design
iteration established, cross-references like "same idiom X uses". That content moves
into the relevant doc page's "Worth knowing" section instead.

Keep a comment only if it's a **single line** flagging something a reader would
otherwise get wrong at that exact spot — a hardware/API quirk, a units note, a sign
convention, an invariant. If a multi-line comment contains exactly one fact worth
keeping, trim it to one line; never leave a multi-line comment in source.

Example:
```cpp
// before (deleted from source, moved to docs/graphics/light_manager.html):
// The first version of this class built its GPU buffer lazily, the first time
// buffer() was called after a change. That broke in practice: ... [full story]
void build();

// after (source):
void build(); // no leading comment needed here at all — nothing non-obvious at this exact line
```
```cpp
// before:
// NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it so the
// shader's plain `binding = 2` matches (see graphics::Surface-era discovery of this).
m_lighting_binding_layout = ...

// after:
// NVRHI's Vulkan backend offsets sampler bindings by 128 by default; zero it.
m_lighting_binding_layout = ...
```

To check the sweep is complete, scan for any remaining run of 2+ consecutive `//`
comment lines across touched files — anything found should be trimmed or deleted:
```sh
for f in <touched files>; do awk '
  /^\s*\/\// { c++; if (c==1) start=NR; next }
  { if (c>=2) print FILENAME":"start"-"NR-1" ("c" lines)"; c=0 }
  END { if (c>=2) print FILENAME":"start"-"NR" ("c" lines)" }
' "$f"; done
```

## Verification checklist

- `./run` (or `./build -j $(nproc)`) succeeds clean.
- `git status`/`git diff` on every source file touched for comments only — no logic
  changed. If a comment sweep and a real code change happened in the same session,
  make sure they're not tangled in a way that makes this hard to confirm.
- Every `href` in every touched/new page resolves to a real file (a broken nav link is
  the most common mistake when a new page gets added but not wired into every nav
  copy).
- No `http://`/`https://` except inert ones (SVG `xmlns`).
