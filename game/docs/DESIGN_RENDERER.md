# Design: modern renderer

Status: **in progress**. The renderer is being modernised one step at a time, in this order.
Each step lands fully working on all platforms before the next starts.

1. [x] **RHI** — API-agnostic renderer abstraction (`gfx/rhi.h`), GL3 backend (`gfx/rhi_gl.c`),
       chosen at compile time in `mare.c`. Prepares for Vulkan / DX12 backends.
2. [x] **HDR rendering** — render the 3D scene into an RGBA16F offscreen target, tonemap to the
       backbuffer (adds render-target support to the RHI).
3. [x] **Linear / sRGB pipeline** — all lighting math in linear space, sRGB conversion at the
       tonemap stage; author colours converted once on input.
4. [x] **PBR materials** — metallic/roughness model for ships, towns and terrain (vertex
       material parameters first; textures come with normal maps).
5. [x] **Directional lighting** — one sun light evaluated consistently by every pass (replaces
       the per-shader ad-hoc diffuse+specular), day cycle ready.
6. [x] **Shadows** — cascaded shadow map from the sun (adds depth-target rendering and sampler
       comparison to the RHI).
7. [ ] **Normal maps** — tangent-space normals for sea detail and building/ship surfaces.
8. [ ] **Frustum culling** — generalise the terrain chunk culling into a shared helper used by
       ships, towns and future meshes (bounding spheres on instance lists).
9. [ ] **Instancing** — formalised in the RHI (done as part of step 1: `rhi_geometry_instances`),
       extend with per-mesh instance buffers reused across passes and draw-call stats.
10. [ ] **Profiling hooks** — CPU (exists: `prof_*` in core/diag), GPU timer queries per pass,
        VRAM (buffer/texture allocation tracking in the RHI), process RAM; all surfaced in the
        dashboard and metrics CSV.

## Steps 3-6: linear/sRGB, PBR, directional lighting, shadows (done)

Shared GLSL building blocks live in `glsl.h` (`GLSL_COLOR`, `GLSL_PBR`, `GLSL_LIGHT`), so every
pass shades the same way.

**Linear / sRGB.** Author colours (vertex and instance colours, nation colours, biome palettes,
the sky) are sRGB and are converted with `srgb_to_linear()` before any lighting. Shaders write
linear radiance into the RGBA16F target; `hdr.c` is the single place that encodes back to sRGB,
after a Reinhard-style luminance shoulder. The HDR target's clear colour is converted on the CPU
(`srgb_to_linear3`), and colours handed to v3's debug draw go through `rgb_linear()` since those
lines are written straight into the linear target.

**PBR.** `pbr_direct()` is Cook-Torrance GGX + Smith + Schlick with a Lambert diffuse;
`pbr_ambient()` is a hemispheric sky/ground term. The mesh vertex grew to 12 floats: roughness
and metalness are *builder state* (`mb_material()`), so the existing `mb_tri/quad/box` calls did
not change. Presets: `MB_WOOD`, `MB_PAINT`, `MB_CLOTH`, `MB_STONE`, `MB_TILE`, `MB_METAL`.
Terrain derives roughness from slope and snow; the sea is a smooth dielectric, so its sun glint
is a real GGX lobe and its sky reflection a Fresnel term.

Because the meshes are flat-shaded and drawn without backface culling, and some faces (sails,
flags, roof slopes) are single quads with inconsistent winding, the mesh shader flips the normal
towards the viewer. Without that, those faces fall to the ambient floor and read as black.

**Directional light.** One sun for every pass. `render_light_update()` derives the linear sun
radiance and the hemispheric ambient from `RF.sun`/`RF.daylight`; `light_bind()` uploads them to
any pipeline built with `GLSL_LIGHT`. This replaced the per-shader ad-hoc diffuse+specular terms.

**Shadows.** Three cascades fitted to the camera frustum slices, packed side by side in one
depth atlas (`lighting.c`), with texel snapping to stop shimmer, a normal-offset plus
slope-scaled bias, and 3x3 PCF. Two decisions worth remembering, both learned the hard way:

* The cascades are fitted to the slice's **ground footprint**, not the raw slice. This camera
  looks down at a world that lives within a few units of y=0, so a box fitted to the frustum
  slice is nearly all empty air and pushes the ground out of the light's depth range, where it
  is clipped and stops casting.
* **Only objects cast** (ships, houses, roofs, towers, fort walls); the terrain receives only.
  The terrain's 374 chunks are coarse, LOD-switched and skirted, and as occluders they produced
  LOD-mismatch bands, stripes along every chunk edge from the skirts, and broad self-shadow acne
  — while adding almost nothing, since terrain relief is already shaded accurately per pixel by
  the N.L term from the heightmap normals. Mountains therefore do not shadow valleys.

`MARE_SHADOWDUMP=path.pgm` writes the depth atlas (with `MARE_AUTOQUIT_S`) and `shadow_enabled`
/ `--no-shadows` turns the feature off; both exist because diagnosing a cascade problem from the
final frame alone is guesswork.

## Step 2: HDR rendering (done)

* RHI gains offscreen render targets: `rhi_target_t` (colour + optional depth texture),
  `rhi_target_make/free/begin`, `rhi_backbuffer`, `rhi_read_pixels`.
* `gfx/hdr.c`: the 3D passes (terrain, meshes, sea, debug draw, icons) render into an RGBA16F
  target bound by `hdr_begin`; `hdr_resolve` tonemaps to the backbuffer with a fullscreen
  triangle; microui labels/panels draw on top in backbuffer space. The target follows window
  resizes. `hdr_exposure` is a plain multiplier (default 1).
* Tonemap is deliberately conservative for this step: identity below 1.0, hue-preserving
  normalisation above, so the game looks unchanged until the linear/sRGB and lighting steps
  start emitting real HDR values.
* `MARE_SCREENSHOT=path.ppm` (with `MARE_AUTOQUIT_S`) captures the tonemapped backbuffer via
  `rhi_read_pixels` for smoke tests.

## Step 1: RHI (done)

* `gfx/rhi.h` — opaque `rhi_texture_t`, `rhi_pipeline_t`, `rhi_geometry_t`; enums for formats
  (already includes sRGB / RGBA16F / depth for later steps), filtering, blending.
* Pipelines bake in depth test/write and blend state at creation, Vulkan/DX12 style; binding a
  pipeline applies its state, so no loose `glEnable` switching exists outside the backend.
* Geometry objects bundle vertex buffer + optional u32 index buffer + optional streamed instance
  buffer (a VAO on GL; plain buffer bindings on future backends). `rhi_geometry_instances`
  streams per-frame instance data and grows the buffer as needed.
* Uniforms are set by cached location while a pipeline is bound (`rhi_uniform` + `rhi_set_*`);
  a future backend maps these calls onto a per-pipeline uniform block.
* `rhi_gl.c` is the only game file calling GL. The backend is selected by the include in
  `mare.c`. v3's own GL (debug draw, microui) remains the framework's business; the backend
  resets its VAO cache on pipeline bind to stay correct around it.
* `gpu.c` keeps the mesh builder and the `mesh_t`/`inst_t` instancing API, now on the RHI.

Framework note: `ext` headers carried unconditional MSVC `#pragma comment(lib, ...)` lines that
break clang-based Linux builds (lld fails on dependent-library specifiers); they are now guarded
with `#if defined _MSC_VER`.
