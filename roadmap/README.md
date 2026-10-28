# Roadmap

Scratch backlog for planned work. Jot notes here; move them into a real project
management system later. Keep entries short — enough to remember the intent and
the gotchas, not a full spec.

| # | Item | Depends on | File |
|---|------|-----------|------|
| 1 | Galaxy simulator | 1a | [galaxy-simulation.md](galaxy-simulation.md) |
| 1a | Emissive point renderer plugin — *wiring landed; bloom + streaming buffer + exposure still to do* | renderer plugin seam (done) | [galaxy-simulation.md](galaxy-simulation.md) |
| 2 | Darwin / Metal backend — *rendering landed; readback + compute interop next* | — | [metal-backend.md](metal-backend.md) |
| 3 | Compute ray tracer (CUDA + Metal) | 2 (Metal half), renderer plugin seam | [compute-ray-tracer.md](compute-ray-tracer.md) |
| 4 | CUDA ↔ Vulkan image interop — *planned, deferred: the copy it saves is ~0.02 ms* | buffer interop (done) | [cuda-image-interop.md](cuda-image-interop.md) |

Cross-cutting: the `graphics::Renderer` concept + `graphics::Presenter` (landed
in `renderer-plugin-seam`) mean 1a and 3 are new renderer types in their own
files, not forks of `DeferredRenderer`.
