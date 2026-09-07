# Galaxy simulation

Two backlog items: the simulator (main) and the renderer plugin it needs (dependency).

## 1a — Emissive point renderer plugin (dependency, do first)

A `graphics::Renderer` implementation for self-luminous point objects. No G-buffer,
no lighting pass, no shadows — every element emits, nothing is lit.

**Status: wiring landed.** `graphics::renderers::PointRenderer` +
`graphics::primitives::scene::PointInstance` + the `points` shader + `sandbox/point_test`
(`./run point_test`). Additive view-space billboards → HDR → tonemap → LDR, satisfies the
concept, blits through `Presenter`. Still to do, in rough order:
- **bloom** in shared post (core), toggleable — both this and the deferred renderer want
  it. Without it a star is a clamped 1–2 px dot.
- **streaming instance buffer** — `PointRenderer::submit()` recreates the buffer every
  frame today. Needs `sg_update_buffer` on a fixed-capacity buffer (or a point-cloud
  primitive that owns one).
- exposure recalibration / auto-exposure for a near-black frame.
- blackbody colour ramp (temperature → RGB, pure math).
- optional depth test against other geometry (billboards already write no depth); coarse
  spatial-cell culling when flying inside the disc.

### Design notes

- Unlit forward pass into an HDR target, then tonemap via the `primitives::pass`
  primitives. *(done)*
- Instanced billboards: one quad, N instances, 32 B per instance (`vec3 pos`,
  `float size`, `vec4 color`), vertex shader expands the quad in view space. Not
  `SG_PRIMITIVETYPE_POINTS` (capped, no WebGPU point size). *(done)*
- Additive blending `ONE`/`ONE`, order-independent, no sort, no depth buffer. *(done —
  `PointRenderer` is the first pipeline in the engine to set blend state.)*
- Streaming: `sg_update_buffer` once per frame on a fixed-capacity buffer, not
  destroy+recreate. `PointRenderer::submit()` still does destroy+recreate, same as
  `InstancedMesh::uploadIfDirty` — fine for a proof, unusable for a live sim.

## 1 — Galaxy simulator (main)

- Thousands to millions of stars. **GPU** integration on a CUDA `app::Worker` via
  `gpu::SharedInstanceBuffer` in `Sync::SimPriority` mode: the sim owns a private buffer,
  runs free, publishes snapshots into a ring the `PointRenderer` draws — the render thread
  never stalls the sim. (CPU-on-a-Worker orbital integration is the fallback if a machine
  has no CUDA.) See `docs/gpu/interop.html`.
- Disable/replace the gradient sky (black, or a faint nebula billboard).
- Recalibrate exposure for a near-black frame; auto-exposure is a nice-to-have.
- Scale/precision: bounded coordinates (±~1000 units) is fine for a first version;
  real parsec scale later needs reversed-Z / log depth / floating origin.
- Optional polish: blackbody colour ramp (temperature → RGB, pure math), coarse
  spatial-cell culling only if flying inside the disc.
