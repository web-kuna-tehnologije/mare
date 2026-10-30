# Design: modern renderer

Status: **all ten steps done**. The renderer was modernised one step at a time, in this order;
each step landed fully working on all platforms before the next started.

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
7. [x] **Normal maps** — tangent-space normals for sea detail and building/ship surfaces.
8. [x] **Frustum culling** — generalise the terrain chunk culling into a shared helper used by
       ships, towns and future meshes (bounding spheres on instance lists).
9. [x] **Instancing** — formalised in the RHI (done as part of step 1: `rhi_geometry_instances`),
       extend with per-mesh instance buffers reused across passes and draw-call stats.
10. [x] **Profiling hooks** — CPU (exists: `prof_*` in core/diag), GPU timer queries per pass,
        VRAM (buffer/texture allocation tracking in the RHI), process RAM; all surfaced in the
        dashboard and metrics CSV.

## 0.19: the device behind the RHI, a geometry stage, dynamic vertices

`gfx/rhi_device.hh` is the abstract `mare::engine::rhi::Device` the rewrite plan's T4 asked
for: every `rhi_*` C function is now a one-line shim in `gfx/rhi.cc` onto the active device,
which is chosen once before `rhi_init` (`--rhi=gl|vulkan|dx12`, `MARE_RHI`). `GlDevice`
(rhi_gl.cc) is the production backend. `VulkanDevice` (rhi_vk.cc) is the first slice of T8: it
loads the loader at run time with hand-written declarations for the few 1.0 structs it needs
(no SDK, no link dependency), creates instance, physical device, logical device and graphics
queue, and reports adapter / API version / device-local memory in `rhi_info()`; every draw is
delegated to the GL device through `DelegatingDevice` until SPIR-V, swapchain and pipeline
objects exist. `Dx12Device` is the API-neutrality stub.

Three capabilities joined the interface for the rig work: pipelines may carry a **geometry
stage** (`rhi_pipeline_desc_t.gs`, `#version 150`, `rhi_geometry_shader_supported`) and take
**line geometry** (`.lines`, drawn as `GL_LINES`), and a geometry's vertices can be
re-uploaded per frame (`rhi_geometry_update`, orphan + refill). The cordage uses the first two
(meshes/rope.geom: a line widened to a screen-facing quad of physical width with a pixel floor),
the simulated flags the third. Tessellation is still the terrain's and the sea's amplification;
the geometry stage is used only where it does what nothing else can - per-primitive screen-space
expansion.

Materials added in the same step: canvas (seams, tablings, weave micro-normal, transmission with
the sun behind the sail, fuzz sheen - `meshes.frag`, keyed on `v_cloth`), water crest scattering
(`sea.frag`), and ground roughness per material with a close-range micro-relief normal
(`terrain.frag`, the grain map on unit 9).

## Step 10: profiling hooks (done)

**GPU timers.** `rhi_gpu_begin/end` wrap each pass (terrain, shadow, meshes, sea, resolve) in an
`ARB_timer_query`. Results are read back four frames late from a small per-scope ring, so the
CPU never blocks on the GPU; `rhi_gpu_collect()` harvests whatever has finished. Backends
without timer queries report 0 and say so in the dashboard instead of showing fake numbers.

**VRAM.** The RHI tracks every allocation it makes - textures, render targets, vertex, index and
instance buffers - in `rhi_memory()`. Render-target images are *moved* out of the texture bucket
into the target bucket rather than counted twice. This is what the renderer asked for, not what
the driver committed, and the dashboard says so.

**RAM.** `diag_process_ram()` returns the process resident set size, with the platform code kept
beside the other platform code in `core/diag.c` (`/proc/self/statm`, `GetProcessMemoryInfo`,
`task_info`).

Everything lands in the Perf tab (per-pass GPU bars next to the CPU scopes, VRAM breakdown,
process RAM, draw/instance/upload counts) and in the metrics CSV as `gpu.*_ms`, `mem.vram_mb`
and `mem.ram_mb`, so a run can be analysed offline.

A worked example, on llvmpipe where the "GPU" is really the CPU: the tonemap resolve costs
~13 ms because `linear_to_srgb` runs a `pow` per pixel per channel. On real hardware that is
free, but it is exactly the kind of thing these hooks exist to surface.

## Steps 7-9: normal maps, frustum culling, instancing (done)

**Normal maps (`detail.c`).** The game ships no texture assets, so two tileable normal maps are
baked at init from wrapping value noise: a ridged-fbm ocean map and a plank/plaster grain map.
The sea samples the wave map at two scrolling scales (swell + chop) and blends the slopes,
replacing the old three hand-written sines; this is also where the sun sparkle now comes from.
The meshes carry no UVs, so the grain map is applied by UDN-style triplanar projection
(`nmap_triplanar` in glsl.h), scaled by material roughness and faded out past ~120 units so
distant silhouettes stay clean.

**Water opacity.** The sea is now genuinely transparent in the shallows: alpha ramps 0.42 → 1.0
over the first ~1.6 units of depth, so the already-rendered seabed shows through, while foam
stays opaque and the map view stays solid parchment.

**Frustum culling (`cam_sphere_visible`).** Ports and fleets are culled per object at build
time against the camera frustum, with an 8-unit margin so casters just outside the view still
drop their shadows into it (the sun is high; shadows are short). Culled counts are in the
dashboard. Terrain keeps its per-chunk AABB cull.

**Instancing.** Instance buffers are streamed once per frame (`mesh_set_instances`, during
`scene_build_*`) and drawn by all four passes (three shadow cascades + main) without
re-uploading — previously every pass re-uploaded every list. The RHI counts draw calls,
instances, triangles and streamed bytes per frame (`rhi_frame_stats`), surfaced in the
dashboard and the metrics CSV (`gfx.draw_calls/instances/upload_kb`).

**Depth precision.** vecmath's projections are D3D-style (clip z in [0,1]), which on GL only
ever used window depth [0.5, 1] — half the buffer. `cam_proj_to_gl()` remaps clip z to [-1,1]
for both the camera and the shadow cascades, doubling effective depth precision against
z-fighting. `mb_quad2` also no longer emits a mirrored twin quad: shading has been two-sided
since step 4, so the copy only added exactly coplanar triangles fighting their originals.

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
