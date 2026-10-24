# Design: modern renderer

Status: **in progress**. The renderer is being modernised one step at a time, in this order.
Each step lands fully working on all platforms before the next starts.

1. [x] **RHI** — API-agnostic renderer abstraction (`gfx/rhi.h`), GL3 backend (`gfx/rhi_gl.c`),
       chosen at compile time in `mare.c`. Prepares for Vulkan / DX12 backends.
2. [ ] **HDR rendering** — render the 3D scene into an RGBA16F offscreen target, tonemap to the
       backbuffer (adds render-target support to the RHI).
3. [ ] **Linear / sRGB pipeline** — all lighting math in linear space, sRGB conversion at the
       tonemap stage; author colours converted once on input.
4. [ ] **PBR materials** — metallic/roughness model for ships, towns and terrain (vertex
       material parameters first; textures come with normal maps).
5. [ ] **Directional lighting** — one sun light evaluated consistently by every pass (replaces
       the per-shader ad-hoc diffuse+specular), day cycle ready.
6. [ ] **Shadows** — cascaded shadow map from the sun (adds depth-target rendering and sampler
       comparison to the RHI).
7. [ ] **Normal maps** — tangent-space normals for sea detail and building/ship surfaces.
8. [ ] **Frustum culling** — generalise the terrain chunk culling into a shared helper used by
       ships, towns and future meshes (bounding spheres on instance lists).
9. [ ] **Instancing** — formalised in the RHI (done as part of step 1: `rhi_geometry_instances`),
       extend with per-mesh instance buffers reused across passes and draw-call stats.
10. [ ] **Profiling hooks** — CPU (exists: `prof_*` in core/diag), GPU timer queries per pass,
        VRAM (buffer/texture allocation tracking in the RHI), process RAM; all surfaced in the
        dashboard and metrics CSV.

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
