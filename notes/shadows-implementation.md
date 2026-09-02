# Directional Shadow Mapping — Implementation Review

Companion doc for reviewing the shadow-mapping work (`roadmap-req-shadows`, Phase 1:
directional cascaded shadow maps). Written to be read alongside the code. Line
numbers are hints as of this writing — trust the symbol names; grep if a number
drifted.

Roadmap memory: `roadmap-req-shadows` (grep `roadmap-` in the auto-memory dir).
Plan file used during implementation: `~/.claude/plans/snazzy-splashing-gray.md`
(overwritten per step; only the last step's plan survives).

---

## 1. What exists now / what doesn't

**Done (Phase 1, directional CSM):**

- 4-slice `D32` cascaded shadow map, texel-snapped, logarithmic-weighted splits.
- Depth-only caster pass replaying a per-frame draw list into each cascade.
- `lighting.frag` samples the cascades: view-depth cascade select, 3×3 PCF via a
  hardware comparison sampler, cross-cascade blend band, applied to the directional
  light only.
- Sun direction pulled from the scene's first shadow-casting directional light
  (`graphics::Light::casts_shadows`), falling back to a hardcoded direction.
- Off-screen objects still cast (camera-frustum culling gates the G-buffer pass
  only, never the shadow pass).
- Debug: `F1` toggles a fullscreen blit of one cascade slice, `F2` cycles which slice.

**Not done (later steps / phases):**

- **Bias tuning** — `m_shadow_depth_bias` / `m_shadow_normal_offset` are fixed
  starting guesses; no slope-scaled depth bias on the pipeline, no runtime setters,
  no sandbox keybinds. Expect some acne / peter-panning until this lands.
- **Instanced cascade culling** — instanced parts are submitted to every cascade
  unconditionally (`DrawItem::world_bounds` for an instanced part only covers the
  identity instance).
- **Spot shadows** (Phase 2), **point-light cube shadows** (Phase 3).
- **`docs/graphics/shadows.html`** + nav wiring (the self-contained HTML doc set).
- **Cascade-fit width** — the ortho boxes are fit tight to the camera frustum
  slices + a depth-only caster margin; a caster far outside the view frustum
  (laterally / vertically) can fall outside every cascade box and cast nothing.
- **Per-cascade resolution** (near 2048 / far 512) — all slices are 2048².

---

## 2. Implementation order (as built)

The work was phased so each step built + ran on its own. The order was **D → A → E
→ C → F → off-screen-caster fix**. Reading in that order shows how each piece
slotted in; reading the final code, the natural order is A → C/shadow.cpp → D/E
(pipelines + passes) → F (lighting.frag).

### Step D — shadow depth target + pipeline + one cascade (bring-up)

Goal: render scene depth from a hardcoded sun matrix into a texture, see it via a
debug key. Throwaway feed: a shadow draw was temporarily wedged into
`renderMesh()`.

Added:

- `Renderer::kCascadeCount` (later `= kShadowCascadeCount`), `kShadowMapResolution
  = 2048` — `renderer.hpp:74-75`.
- `m_shadow_map` (`D32` `Texture2DArray`, 4 slices), 4 per-slice framebuffers,
  `m_shadow_pipeline` / `m_shadow_instanced_pipeline` (depth-only), the
  `ShadowPushConstants` struct, `m_shadow_debug_*` (fullscreen blit),
  `m_show_shadow_map_debug`.
- `Renderer::buildShadowResources()` — `renderer.cpp:393`.
- Shaders: `shadow_depth.vert`, `shadow_depth_instanced.vert`, `shadow_depth.frag`
  (empty), `shadow_debug.frag`. Registered in `build.cpp`'s `shader_names` +
  `#include`d in `renderer.cpp`.
- `Renderer::toggleShadowMapDebug()` + `F1` in `sandbox/model_test/main.cpp`.
- Shadow map cleared each frame in `beginFrame()` — `renderer.cpp:551`.

### Step A — draw-list refactor

Goal: `renderMesh()` stops recording immediately; it appends a `DrawItem` to
`m_draw_list`. `endFrame()` replays the list once per pass, so the shadow pass and
G-buffer pass see the same geometry.

Added:

- `Renderer::DrawItem` — `renderer.hpp:79-92`.
- `std::vector<DrawItem> m_draw_list;` — cleared at the top of `beginFrame()`.
- `Renderer::recordShadowPass()` / `recordGBufferPass()` — `renderer.cpp:607`,
  `renderer.cpp:644`. Both loop `m_draw_list`; `if constexpr` mesh-kind branch
  became a runtime `item.instance != nullptr` check.
- `endFrame()` calls `recordShadowPass(); recordGBufferPass();` first, after the
  `!m_acquired.ok` guard — `renderer.cpp:733-734`.
- `renderMesh` gained a `const Eigen::AlignedBox3f& world_bounds` param;
  `scene::Model::render()` computes it once per part (reusing the existing
  `worldBounds()` helper in `src/scene/model.cpp`) and passes it through.
- The temp shadow draw was deleted from `renderMesh` and moved into
  `recordShadowPass()`.

Output is pixel-identical to before — same draws, same submission order.

### Step E — per-cascade shadow pass + per-cascade cull

Goal: `recordShadowPass()` loops all 4 slices; each gets its own matrix and its own
frustum cull. Matrices still identical (all seeded from the Step-D hardcoded one)
until Step C.

Added:

- `m_shadow_view_proj` (single `mat4`) → `std::array<Eigen::Matrix4f, kCascadeCount>
  m_cascade_view_proj` — `renderer.hpp:161`.
- `recordShadowPass()` outer `for (c in kCascadeCount)`: bind
  `m_shadow_framebuffers[c]`, build `Frustum::fromViewProjection(m_cascade_view_proj[c])`,
  per item skip if `item.instance == nullptr && !light_frustum.intersects(item.world_bounds)`.
- Debug per-slice: `shadow_debug.frag` gained a `uint layer` push constant;
  `m_shadow_debug_binding_layout` / `_binding_set` gained a `PushConstants(0, sizeof(u32))`
  item; `m_shadow_debug_layer` + `Renderer::cycleShadowMapDebugLayer()` + `F2`.

### Step C — real cascade splits / fit / texel-snap

Goal: compute the 4 cascade matrices per frame from the live camera + sun.

Added:

- **`src/graphics/shadow.{hpp,cpp}`** — new file. `kShadowCascadeCount`,
  `struct CascadeSetup`, `computeCascades(...)`. `lookAtRH()` / `orthographic()`
  moved here from `renderer.cpp`'s anon namespace (nothing else used them after
  this step). See §6.
- `Renderer` includes `graphics/shadow.hpp`; `kCascadeCount = kShadowCascadeCount`.
- Members: `m_sun_direction` (default `(-0.4,-1,-0.35)` normalized),
  `m_shadow_distance = 60`, `m_cascade_split_lambda = 0.85`, `m_cascade_split_depth[4]`
  — `renderer.hpp:161-167`.
- `setLights()` — `if (const Light* sun = lights.directionalLight()) m_sun_direction = sun->direction;`
  — `renderer.cpp:677-681`.
- `renderMesh()` first-mesh block (the `!m_camera_uploaded` guard) now also calls
  `computeCascades(...)` and copies into `m_cascade_view_proj` /
  `m_cascade_split_depth` — `renderer.cpp:571-582`. This is the only place with the
  live camera before `recordShadowPass()` runs in `endFrame()`.
- `buildShadowResources()` — hardcoded matrix tail deleted; `m_cascade_view_proj.fill(Identity)`
  as a placeholder — `renderer.cpp:502`.
- `Renderer::setShadowDistance()` / `setCascadeSplitLambda()` — `renderer.cpp:712-718`.
- `graphics::Light::casts_shadows` (`light.hpp`), set in `gltf_loader.cpp`
  `appendLight()` as `type != LightType::Point`. `light.cpp` `display()` prints it.
- `LightManager::directionalLight()` — first `m_lights` entry with
  `type == Directional && casts_shadows`, else `nullptr` (`light_manager.cpp`).
- `Camera::nearPlane()` / `farPlane()` inline getters (`camera.hpp`).
- `build.cpp` — `"src/graphics/shadow.cpp"` added to `engine_sources`.

### Step F — sample the shadow map in the lighting pass

Goal: first on-screen shadows.

The blocker: `LightingPushConstants` was ~112 B; 4 cascade `mat4`s alone are 256 B,
over the 128 B push-constant limit. So the lighting uniforms moved to a volatile
constant buffer.

Added:

- `struct LightingUniforms` (anon namespace, std140-matched) — `renderer.cpp:63`.
  `static_assert(kShadowCascadeCount == 4)` guards the `vec4 cascade_split_depth`
  packing.
- `m_lighting_cb` volatile CB (`maxVersions = kFramesInFlight + 2`).
  `m_lighting_push_constant_layout` / `_binding_set` → `m_lighting_cb_layout` /
  `m_lighting_cb_binding_set` — a **separate** binding layout / descriptor set (b0),
  same split as the g-buffer's `gbuffer_push_constant_layout`, so `b0` doesn't
  collide with the SRVs' `t0`. `renderer.hpp:129-134`, `renderer.cpp:253-296`.
- `m_lighting_binding_layout` gained `Texture_SRV(6)` (shadow array) + `Sampler(7)`
  (comparison). `rebuildLightingBindingSet()` binds `m_shadow_map` +
  `m_shadow_cmp_sampler` there — `renderer.cpp:370-380`.
- `m_shadow_cmp_sampler` — `SamplerDesc().setReductionType(SamplerReductionType::Comparison)`
  — `renderer.cpp:478`. NVRHI's Vulkan backend maps that to
  `compareEnable = true, compareOp = eLess`.
- **Ctor reorder:** `buildShadowResources()` moved to *before*
  `rebuildLightingBindingSet()` so `m_shadow_map` / `m_shadow_cmp_sampler` exist
  when that set is built — `renderer.cpp:340`.
- Caster acne: shadow pipelines' raster state switched `setCullNone()` →
  `setCullFront()` — `renderer.cpp` in `buildShadowResources()`.
- `renderMesh()` stashes `m_view = camera.transformationMatrix()` — `renderer.cpp:567`.
- `endFrame()` builds a `LightingUniforms`, `m_cmd->writeBuffer(m_lighting_cb, ...)`,
  `state.bindings = {m_lighting_binding_set, m_lighting_cb_binding_set}`, drops
  `setPushConstants` — `renderer.cpp:741-756`.
- `lighting.frag` — `push_constant` block → `layout(set = 1, binding = 0) uniform
  LightingUniforms {...} u;`; every `pc.` → `u.`; new `tShadow` (binding 6) +
  `samplerShadow sShadowCmp` (binding 7); `sampleCascade()` + `sampleShadow()`;
  directional branch multiplied by the visibility. See §5.
- Members `m_shadow_depth_bias = 0.0015`, `m_shadow_normal_offset = 0.03`
  (fed into `shadow_params`; setters deferred to the bias-tuning step).

### Off-screen caster fix (post-F)

Bug: `scene::Model::render()` frustum-culled `StandardMesh` parts before calling
`renderMesh`, so a camera-culled object was never added to `m_draw_list` and thus
never cast a shadow — even though an off-screen object can cast into view.

Fix:

- `DrawItem::camera_visible` (`renderer.hpp:91`).
- `renderMesh` gained a `bool camera_visible` param (`renderer.hpp:26-32`,
  `renderer.cpp:555-556`), stored on the item (`renderer.cpp:596`).
- `Model::render()` no longer `continue`s on the cull — it always submits, with
  `camera_visible = !holds<StandardMesh> || frustum.intersects(world_bounds)`
  (`src/scene/model.cpp`).
- `recordGBufferPass()` skips `!item.camera_visible` (`renderer.cpp:647`).
  `recordShadowPass()` ignores the flag — every draw-list item is a caster.

Remaining limitation: `recordShadowPass()` still culls standard casters per-cascade
against that cascade's ortho box, and the boxes are fit to the camera frustum
slices. An object far outside the frustum can be outside every box → no shadow.
That's a cascade-fit-width knob, not a culling bug (see §1 "not done").

---

## 3. Frame flow with shadows

```
beginFrame()
  m_camera_uploaded = false
  m_draw_list.clear()
  wait m_frame_done[slot] if CPU is kFramesInFlight ahead
  acquire swapchain image  (on fail: waitIdle + recreate + rebuild*, return)
  m_cmd->open()
  clear: albedo, normal, material, depth (gbuffer)  +  m_shadow_map (all 4 slices)

renderMesh(mesh, material, model, world_bounds, camera_visible, camera)   [per part, from Scene::render -> Model::render]
  uploadIfDirty (instanced only)
  stash m_view_position / m_inverse_view_projection / m_view
  first mesh only:
    writeBuffer(m_camera_buffer, camera.viewProjectionMatrix())    // g-buffer vertex CB
    m_cascade_view_proj / m_cascade_split_depth = computeCascades(camera, m_sun_direction, ...)
  push a DrawItem { vB, iB, counts, instance, model, normal_matrix, material*, world_bounds, camera_visible }

endFrame()
  if !acquired: return
  recordShadowPass()          // for c in 4:  Frustum(m_cascade_view_proj[c]); per item (light-frustum cull, standard only) -> m_shadow_framebuffers[c], push {cascade_vp, model}, drawIndexed
  recordGBufferPass()         // per item: skip !camera_visible; pick gbuffer pipeline by item.instance; bind material set; push {model, normal_matrix}; drawIndexed
  build LightingUniforms; writeBuffer(m_lighting_cb, ...)
  lighting pass  -> m_hdr_color   (bindings: m_lighting_binding_set [SRVs 0-6 + samplers 2,7], m_lighting_cb_binding_set [b0])
  sky pass       -> m_hdr_color
  m_post_process.run(m_hdr_color -> swapchain target)   // tonemap, FXAA
  if m_show_shadow_map_debug:  fullscreen blit m_shadow_map[m_shadow_debug_layer] -> swapchain target
  m_cmd->close(); submit; setEventQuery(m_frame_done[slot]); present
```

The shadow map is single-buffered like the G-buffer — fully overwritten each frame
before it's read, protected by the same `m_frame_done` event-query fence, so no
frames-in-flight interaction.

---

## 4. Key data structures

### `Renderer::DrawItem` — `renderer.hpp:79-92`

| field | source | consumed by |
|---|---|---|
| `nvrhi::IBuffer* vertex` | `mesh.vertexBuffer()` | both passes |
| `nvrhi::IBuffer* index` | `mesh.indexBuffer()` | both passes |
| `nvrhi::IBuffer* instance` | `mesh.instanceBuffer()` if the type has it, else `nullptr` | pipeline selection in both passes |
| `u32 index_count` | `mesh.indexCount()` | `drawIndexed` |
| `u32 instance_count` | `mesh.instanceCount()` (1 for `StandardMesh`) | `drawIndexed` |
| `Eigen::Matrix4f model` | call-site world matrix | push constant (both passes) |
| `Eigen::Matrix4f normal_matrix` | `graphics::normalMatrix(model)` (`mesh.hpp`) | G-buffer push constant only |
| `const Material* material` | `&material` | G-buffer pass (`gbufferBindingSet`) |
| `Eigen::AlignedBox3f world_bounds` | `worldBounds(model, part.local_bounds)` in `Model::render` | `recordShadowPass` per-cascade cull (standard only) |
| `bool camera_visible` | camera frustum test in `Model::render` (always `true` for instanced) | `recordGBufferPass` skips `false` |

Raw pointers are non-owning, valid only within one `beginFrame()`/`endFrame()` —
the meshes/materials live on `scene::ModelPart` through the frame.

### `graphics::CascadeSetup` — `shadow.hpp:14-19`

```cpp
struct CascadeSetup {
    std::array<Eigen::Matrix4f, kShadowCascadeCount> view_proj;      // world -> light clip, per cascade
    std::array<f32, kShadowCascadeCount>            split_view_depth; // far edge of each cascade, VIEW-SPACE DISTANCE
};
```

`split_view_depth[c]` is a positive distance (`-view_z`), used by `lighting.frag`
to pick the cascade for a fragment.

### `LightingUniforms` (C++, `renderer.cpp:63-73`) ↔ `LightingUniforms` (GLSL, `lighting.frag:27-38`)

std140. Both sides column-major mat4. `mat4[4]` std140 stride 64 = tight, matches
`std::array<Eigen::Matrix4f,4>`.

```
mat4  inverse_view_projection;   // reconstructWorldPosition
mat4  view;                      // world->view; view-space depth for cascade select
mat4  cascade_view_proj[4];      // = m_cascade_view_proj
vec4  view_position;             // xyz = camera world pos
vec4  ambient;                   // rgb = flat ambient (pre-tonemap)
vec4  cascade_split_depth;       // x..w = m_cascade_split_depth[0..3]
vec4  shadow_params;             // x = m_shadow_depth_bias, y = m_shadow_normal_offset
uint  light_count;  (+ 3 pad u32 on the C++ side)
```

### `ShadowPushConstants` — `renderer.cpp:84-87`

```cpp
struct ShadowPushConstants {
    Eigen::Matrix4f light_view_proj;  // m_cascade_view_proj[c]
    Eigen::Matrix4f model;            // per-item; exactly 128 bytes total (guaranteed push-constant min)
};
```

Both `shadow_depth.vert` and `shadow_depth_instanced.vert` read this push block;
the instanced VS additionally multiplies in a per-instance transform vertex
attribute.

---

## 5. Shaders

### `shadow_depth.vert` (12 lines)

```glsl
layout(location = 0) in vec3 inPosition;
layout(push_constant) uniform PushConstants { mat4 light_view_proj; mat4 model; } pc;
void main() { gl_Position = pc.light_view_proj * pc.model * vec4(inPosition, 1.0); }
```

Position-only. No fragment output (`shadow_depth.frag` is `void main() {}` — present
so NVRHI gets a real PS rather than a null handle; also the seam where alpha-masked
shadow discard plugs in later).

### `shadow_depth_instanced.vert` (13 lines)

```glsl
layout(location = 0) in vec3 inPosition;
layout(location = 1) in mat4 inInstanceTransform; // minimal shadow layout: POSITION then INSTANCE_TRANSFORM at 1-4
...
gl_Position = pc.light_view_proj * pc.model * inInstanceTransform * vec4(inPosition, 1.0);
```

**Attribute-location gotcha:** the shadow instanced input layout
(`buildShadowResources`, `renderer.cpp:~420`) is `[POSITION, INSTANCE_TRANSFORM
(arraySize 4)]`. NVRHI's Vulkan backend assigns `VkVertexInputAttributeDescription.location`
by a running counter over the attribute array, expanding `arraySize` — so POSITION
= 0, INSTANCE_TRANSFORM = 1,2,3,4. Hence `location = 1` here, **not** `location = 3`
like the g-buffer instanced VS (whose layout has NORMAL + UV in between).

### `shadow_debug.frag` (17 lines)

Fullscreen blit of one cascade slice as grayscale depth. `uint layer` push constant
selects the slice. Debug only.

### `lighting.frag` — the shadow additions

Bindings: `layout(binding = 6) uniform texture2DArray tShadow;`
`layout(binding = 7) uniform samplerShadow sShadowCmp;` — set 0 (implicit), sampler
offset zeroed in `m_lighting_binding_layout`.

`sampleCascade(int cascade, vec3 world_pos, vec3 N)` — `lighting.frag:57-78`:

```glsl
vec4 lp = u.cascade_view_proj[cascade] * vec4(world_pos + N * u.shadow_params.y, 1.0);
lp.xyz /= lp.w;
vec2 uv = lp.xy * 0.5 + 0.5;
uv.y = 1.0 - uv.y;                        // undo the NVRHI viewport Y-flip (see §7)
if (uv out of [0,1] || lp.z > 1.0) return 1.0;
float ref = lp.z - u.shadow_params.x;    // depth bias
// 3x3 PCF, offsets * (1.0 / textureSize(...))
sum += texture(sampler2DArrayShadow(tShadow, sShadowCmp), vec4(uv + off, float(cascade), ref));
return sum / 9.0;
```

Comparison sampler is `compareOp = eLess`, so `texture(...)` returns
`ref < stored ? 1 : 0`, filtered → `[0,1]`. `ref = lp.z - bias`: smaller ref ⇒ more
likely `< stored` ⇒ more likely lit ⇒ bias reduces acne. `+ N * normal_offset`
before projecting nudges the sample point off the surface along the normal — a
second acne lever independent of depth bias.

`sampleShadow(vec3 world_pos, vec3 N)` — `lighting.frag:80-102`:

```glsl
float view_depth = -(u.view * vec4(world_pos, 1.0)).z;       // positive, into the scene
int cascade = 3;
for c in 0..3: if (view_depth < u.cascade_split_depth[c]) { cascade = c; break; }
float visibility = sampleCascade(cascade, world_pos, N);
// blend into the next cascade over the last 15% of this one's range:
float split = u.cascade_split_depth[cascade];
float prev  = cascade == 0 ? 0.0 : u.cascade_split_depth[cascade - 1];
float band  = 0.15 * (split - prev);
if (cascade < 3 && view_depth > split - band) {
    float t = (view_depth - (split - band)) / band;
    visibility = mix(visibility, sampleCascade(cascade + 1, world_pos, N), clamp(t, 0, 1));
}
return visibility;
```

Applied only in the directional branch of the light loop:

```glsl
if (type == 0) { to_light = normalize(-light.direction_type.xyz); shadow = sampleShadow(world_position, normal); }
...
accumulated += (kd * diffuse_color / kPi + specular) * radiance * ndotl * shadow;
```

Point/spot unchanged (`shadow` stays `1.0`).

---

## 6. `computeCascades()` — `shadow.cpp:45-118`

Signature: `computeCascades(const Camera& camera, const Eigen::Vector3f&
light_direction, f32 shadow_distance, f32 split_lambda, f32 shadow_map_resolution)`.

Step by step:

1. `near = camera.nearPlane()`, `far = min(shadow_distance, camera.farPlane())`.
   Cascades cover `[near, far]`, not the full camera range.

2. **Split boundaries** `split[0..4]`, `split[0] = near`:
   ```
   p = i / 4
   uniform     = near + (far - near) * p
   logarithmic = near * pow(far / near, p)
   split[i] = logarithmic * lambda + uniform * (1 - lambda)
   ```
   `lambda = 0.85` (`m_cascade_split_lambda`) leans logarithmic → cascade 0 covers a
   tiny near range (sharp), cascade 3 a huge far range (coarse). This is the
   "exponential detail falloff" — the discrete version of a warped/fisheye shadow
   map, but robust and shimmer-free.

3. **Slice corners.** `camera.frustum().corners()` (`graphics::Frustum`,
   `frustum.hpp`) returns 8 world-space corners: near `{BL,BR,TL,TR}` then far
   `{BL,BR,TL,TR}`. For cascade `c`:
   ```
   t_near = (split[c]   - near) / (far - near)
   t_far  = (split[c+1] - near) / (far - near)
   slice[k]   = corners[k] + t_near * (corners[k+4] - corners[k])   // k in 0..3
   slice[k+4] = corners[k] + t_far  * (corners[k+4] - corners[k])
   ```
   Exact for a perspective frustum — edges are straight lines from the eye, and
   parameter `t` along an edge maps linearly to view depth.

4. **Bounding sphere** of the 8 slice corners: `center` = mean, `radius` = max
   distance to a corner, then `radius = ceil(radius * 16) / 16` (quantized). A
   sphere (not a tight AABB) means camera *rotation* doesn't change the footprint →
   far less edge shimmer.

5. **Light view.** `up = |light_dir.y| > 0.99 ? (0,0,1) : (0,1,0)` (avoids a
   degenerate look-at when the sun is near-vertical). `eye = center - light_dir *
   (radius + kCasterMargin)` (`kCasterMargin = 50`, `shadow.cpp:41`).
   `view = lookAtRH(eye, center, up)` (`shadow.cpp:12`) — right-handed, `-Z`
   forward, **no Y-flip**, matching `camera.cpp`'s view convention.

6. **Texel snap.** Transform `center` into light space; snap its x/y to a texel
   grid: `texels_per_unit = resolution / (2 * radius)`, `x = floor(x * tpu) / tpu`.
   Ortho x/y bounds = snapped center ± `radius`. Without this the shadow edges crawl
   texel-by-texel as the camera moves.

7. **Depth range.** Transform the 8 slice corners into light space; forward is `-Z`
   so a corner's distance in front of the light is `-z`. `n_dist = max(0.05,
   nearest - kCasterMargin)` (pull the near plane toward the light so casters
   *between* the light and the slice still render), `f_dist = farthest`.

8. `view_proj[c] = orthographic(l, r, b, t, n_dist, f_dist) * view`;
   `split_view_depth[c] = split[c+1]`.

`orthographic()` — `shadow.cpp:28`: Vulkan clip, x/y in `[-1,1]`, **z in `[0,1]`**
(not GL's `[-1,1]`), no Y-flip. The `m(2,2) = -1/(f-n)`, `m(2,3) = -n/(f-n)`
derivation is for a `-Z`-forward view mapping view-z `∈ [-n,-f]` → NDC z `∈ [0,1]`.

---

## 7. Coordinate conventions (the part most likely to bite)

- **Projection has no Y-flip** (`camera.cpp` — `m_projection(1,1) = +f`). Paired with
  the g-buffer raster state `setFrontCounterClockwise(true)`. Same convention used
  by `orthographic()` in `shadow.cpp`.
- **NVRHI's Vulkan backend flips Y at the viewport level** for every draw (negative
  viewport height). Rasterizing and sampling a render target both go through that
  viewport, so they stay self-consistent automatically — *but pure math that
  bypasses the viewport must undo the flip by hand.*
- `lighting.frag` `reconstructWorldPosition()` does this: `ndc.y = -(uv.y * 2 - 1)`.
- **Shadow sampling must match.** The shadow map was rendered through the flipped
  viewport. `sampleCascade()` projects `world -> light clip` with pure math, so
  after `uv = lp.xy * 0.5 + 0.5` it applies `uv.y = 1.0 - uv.y`. **If shadows appear
  mirrored (falling toward the sun instead of away), this line is the first
  suspect.**
- Depth: everything is Vulkan `[0,1]`. `lp.z` after the perspective divide is
  directly comparable to the stored `D32` value; the `lp.z > 1.0` early-out in
  `sampleCascade` rejects fragments beyond the cascade's far plane.

---

## 8. Descriptor / binding layout

### Lighting pass — two descriptor sets

`m_lighting_pipeline` has `.addBindingLayout(m_lighting_binding_layout)` (set 0) then
`.addBindingLayout(m_lighting_cb_layout)` (set 1). NVRHI assigns descriptor-set
index by `addBindingLayout` call order.

- **Set 0** (`m_lighting_binding_layout`, `renderer.cpp:253`): `Texture_SRV` 0
  (albedo), 1 (normal), 4 (depth), 5 (material), 6 (shadow array); `Sampler` 2
  (linear), 7 (comparison); `StructuredBuffer_SRV` 3 (lights).
  `.setBindingOffsets(VulkanBindingOffsets().setSamplerOffset(0))` — NVRHI otherwise
  offsets sampler bindings by 128, so this makes GLSL `binding = 2` / `= 7` land on
  Vulkan bindings 2 / 7.
- **Set 1** (`m_lighting_cb_layout`, `renderer.cpp:267`): `VolatileConstantBuffer(0)`.
  `.setBindingOffsets(VulkanBindingOffsets().setConstantBufferOffset(0))` — NVRHI
  otherwise offsets CB registers by 256. GLSL side: `layout(set = 1, binding = 0)`.

Kept separate (rather than one merged layout) so CB `b0` and SRV `t0` don't collide
— exactly how the g-buffer splits `gbuffer_push_constant_layout` (push constant +
vertex CB) from `m_gbuffer_texture_binding_layout` (textures).

`m_lighting_binding_set` is rebuilt in `rebuildLightingBindingSet()` on resize and
on `setLights()` when the light buffer handle changes. It now also binds
`m_shadow_map` + `m_shadow_cmp_sampler` — **hence the ctor reorder**:
`buildShadowResources()` must run before `rebuildLightingBindingSet()`
(`renderer.cpp:340`).

`m_lighting_cb` is volatile with `maxVersions = kFramesInFlight + 2` — NVRHI
auto-versions each `writeBuffer` so an in-flight frame keeps reading the version it
was recorded with. Same pattern as `m_camera_buffer`.

### Shadow caster pass

`m_shadow_pipeline` / `m_shadow_instanced_pipeline`: framebuffer info = depth-only
`D32`; render state = depth test + write on, `depthFunc = Less`, **`setCullFront()`**
(Step F — front-face cull in the caster pass is a cheap acne reducer),
`setFrontCounterClockwise(true)`. One binding layout: `PushConstants(0,
sizeof(ShadowPushConstants))`, vertex visibility.

Per-slice framebuffers: `FramebufferDesc().setDepthAttachment(m_shadow_map,
TextureSubresourceSet().setArraySlices(c, 1))` — `renderer.cpp:410-412`.

### Shadow map texture

`renderer.cpp:396-407`: `D32`, `dimension = Texture2DArray`, `arraySize =
kCascadeCount`, 2048², `isRenderTarget | isShaderResource`, `initialState =
DepthWrite`, `keepInitialState`. Same recipe as the g-buffer `m_depth` (also a `D32`
sampled as an SRV), so a plain (non-comparison) SRV of it is known to work; the
comparison SRV is the lighting-pass one.

---

## 9. Known limitations / TODO

| item | where | note |
|---|---|---|
| Bias not tuned | `m_shadow_depth_bias` / `m_shadow_normal_offset`, `shadow.frag` raster state | fixed guesses; no slope-scaled `depthBias` on the pipeline, no setters, no keybinds. Expect mild acne / peter-panning. **Next step.** |
| Instanced cascade cull | `recordShadowPass` skips the cull for `item.instance != nullptr` | instanced parts render into all 4 cascades unconditionally. Needs a per-`InstancedMesh` union AABB. |
| Cascade-fit width | `computeCascades` step 4-7 | ortho boxes fit tight to camera frustum slices + depth-only caster margin. A caster far outside the frustum (laterally / vertically) casts nothing. Widen `radius` or add a lateral caster margin if it matters. |
| Per-cascade resolution | `kShadowMapResolution` single value | all 4 slices 2048². Near 2048 / far 512 would save VRAM for near-identical quality. |
| `m_camera_uploaded` naming | `renderer.hpp:117`, `renderer.cpp:571` | flag now also gates the per-frame cascade compute, not just the camera CB write. Comment explains; a rename to `m_frame_setup_done` would be clearer. |
| Spot / point shadows | — | Phase 2 / Phase 3. |
| `docs/graphics/shadows.html` | `docs/` | self-contained HTML doc + nav wiring, plus a comment sweep of the touched sources per `.claude/skills/update-docs`. Final step. |
| `RGBA32_FLOAT` comment on `m_normal` | `renderer.hpp:104` | stale (it's `RG16_FLOAT` since the octahedral change) — pre-existing, unrelated, worth fixing in the docs pass. |

---

## 10. Debug controls (sandbox)

`sandbox/model_test/main.cpp`, next to the `GLFW_KEY_ESCAPE` handler:

- **F1** — `renderer.toggleShadowMapDebug()` — fullscreen grayscale blit of one
  cascade slice over the final image.
- **F2** — `renderer.cycleShadowMapDebugLayer()` — `(layer + 1) % 4`.

The blit runs in `endFrame()` after `m_post_process.run(...)`, into the swapchain
target, gated by `m_show_shadow_map_debug` (`renderer.cpp:781-789`). It samples with
a plain (non-comparison) sampler `m_shadow_debug_sampler`. The debug view may look
vertically mirrored vs the main scene — that's the viewport flip and doesn't matter
for eyeballing coverage.

---

## 11. Review checklist (suggested reading order)

1. **`src/scene/model.cpp`** `Model::render()` — the draw submission loop:
   `world_bounds`, `camera_visible`, `renderer.renderMesh(...)`.
2. **`src/graphics/renderer.hpp`** — `DrawItem` (79-92), the `kCascade*` /
   `kShadowMapResolution` constants, the `m_shadow_*` / `m_cascade_*` / `m_lighting_cb*`
   members, `renderMesh` signature (26-32), `recordShadowPass` / `recordGBufferPass`
   / `buildShadowResources` decls.
3. **`src/graphics/renderer.cpp`**:
   - `LightingUniforms` / `ShadowPushConstants` structs (63, 84) — std140 match.
   - ctor: lighting binding layout + CB layout + `m_lighting_cb` (253-296);
     **ctor order** around `buildShadowResources()` / `rebuildLightingBindingSet()`
     (338-341).
   - `rebuildLightingBindingSet()` (370-380) — SRV 6 + sampler 7.
   - `buildShadowResources()` (393-503) — texture, per-slice framebuffers, both
     caster pipelines (`setCullFront`), `m_shadow_cmp_sampler`, debug pipeline.
   - `beginFrame()` shadow-map clear (551).
   - `renderMesh()` (555-597) — `m_view` stash, first-mesh `computeCascades`,
     `DrawItem` fill.
   - `recordShadowPass()` (607-642) — cascade loop, per-cascade `Frustum` cull,
     pipeline/vertex-buffer selection, `ShadowPushConstants`.
   - `recordGBufferPass()` (644-676) — `camera_visible` skip.
   - `setLights()` (677-681) — sun direction pull.
   - `endFrame()` (728-789) — `LightingUniforms` fill + `writeBuffer`, lighting
     bindings, debug blit.
4. **`src/graphics/shadow.hpp` / `shadow.cpp`** — `CascadeSetup`,
   `computeCascades()` (the whole file, §6), `lookAtRH` / `orthographic` /
   `kCasterMargin`.
5. **`src/graphics/shaders/lighting.frag`** — the `LightingUniforms` block (27-38),
   `sampleCascade()` (57-78), `sampleShadow()` (80-102), the directional-branch
   apply (~150, `* shadow`).
6. **`src/graphics/shaders/shadow_depth.vert` / `shadow_depth_instanced.vert` /
   `shadow_depth.frag` / `shadow_debug.frag`** — small; check the instanced
   attribute location (§5).
7. **Supporting:** `src/graphics/camera.hpp` (`nearPlane` / `farPlane`),
   `src/graphics/light.hpp` (`casts_shadows`),
   `src/graphics/light_manager.cpp` (`directionalLight()`),
   `src/scene/gltf_loader.cpp` (`appendLight` sets `casts_shadows`),
   `build.cpp` (`shader_names` + `shadow.cpp`).

Things to actively check while reading:

- std140 layout: does `LightingUniforms` (C++) byte-for-byte match the GLSL block?
- Y-flip: `uv.y = 1.0 - uv.y` in `sampleCascade` — correct sign vs
  `reconstructWorldPosition`?
- Cascade select: `split_view_depth` is a positive distance; `view_depth = -(view *
  world).z` is positive — units consistent?
- `recordShadowPass` per-cascade cull: `Frustum::fromViewProjection` on an *ortho*
  matrix — is the plane/`intersects` math still valid? (Gribb-Hartmann is general;
  worth confirming it doesn't reject visible casters.)
- Ctor ordering: any `rebuild*` that references `m_shadow_map` /
  `m_shadow_cmp_sampler` before `buildShadowResources()` ran?
- Resize path (`beginFrame` acquire-fail, `endFrame` present-fail): `m_shadow_map`
  is fixed-size and *not* rebuilt on resize — intentional (it's not screen-sized).
  Confirm nothing assumes it tracks the swapchain extent.
