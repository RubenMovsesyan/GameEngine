# Galaxy simulation

Two backlog items: the simulator (main) and the renderer plugin it needs (dependency).

## 1a — Emissive point renderer plugin (dependency, do first)

A `graphics::Renderer` implementation for self-luminous point objects. No G-buffer,
no lighting pass, no shadows — every element emits, nothing is lit.

- Unlit forward pass writing straight into an HDR target, then the shared post
  (tonemap/FXAA) via the existing primitives.
- Instanced billboards: one quad, N instances, compact per-instance data
  (`vec3 pos`, `float size`, `vec4 color` — ~32 B). Vertex shader expands the quad
  in view space. Not `SG_PRIMITIVETYPE_POINTS` (capped, no WebGPU point size).
- **Additive blending** (`ONE`/`ONE`), depth test on, depth write off. Order-independent,
  so no sort needed. Blend state has to be exposed — no pipeline in the engine sets it today.
- **Streaming instance buffer**: `sg_update_buffer` once per frame, not destroy+recreate.
  `InstancedMesh::uploadIfDirty` recreates the buffer every change — unusable for
  orbiting stars. Needs a new streaming path (or a purpose-built point-cloud primitive).
- Bloom belongs in shared post (core), toggleable — both this and the deferred
  renderer want it. Without bloom a star is a clamped 1–2 px dot.

## 1 — Galaxy simulator (main)

- Thousands (1k–50k), not billions, of stars. Orbital integration on a `Worker`
  (CPU), writing into the staging array the renderer streams.
- Disable/replace the gradient sky (black, or a faint nebula billboard).
- Recalibrate exposure for a near-black frame; auto-exposure is a nice-to-have.
- Scale/precision: bounded coordinates (±~1000 units) is fine for a first version;
  real parsec scale later needs reversed-Z / log depth / floating origin.
- Optional polish: blackbody colour ramp (temperature → RGB, pure math), coarse
  spatial-cell culling only if flying inside the disc.
