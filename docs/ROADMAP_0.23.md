# Road map after 0.22.10 (worktree oak, 2026-10-31)

Where the renderer and the simulation stand after the four issue folders, and the order the
remaining work should be taken in. The backlog this is drawn from is `assets/dev.txt`
(checked copy: `docs/dev/dev_oak_*.txt`) and `docs/todo_list.md` (issue4, with priorities);
the visual target is `game/docs/DESIGN_VISUAL_TARGET.md`.

## Done to 0.22.10

Renderer foundation, asset runtime, LOD manager, worker pool and async loading, world
streaming, the terrain-v2 world package as both terrain *and* coastline, the ocean (patch
grid, both tessellation stages, screen-adaptive factors, Gerstner sets, normal maps, Fresnel,
glitter, foam, bow wave, trailing wake, screen-space reflections), the authored fleet with
data-driven classes, the painted hinterland with instanced props, roads, map modes, the
staged battle and city scenes. 0.22.7 (issue4): the battle's own clock, the wind shown and
used in the battle, the stage coast read from the world (bearing, land cover, relief) with a
shore the surf breaks on, open-sea haze, a free camera height, and a particle system for the
broadsides (wind-blown smoke, muzzle flash, embers). 0.22.8 (issue5): three-layer gunsmoke
with water rings, spray and tonemap shockwaves; the cordage textured; masts in proportion;
lowland coasts and coastal heights from the data; closer map zoom with a ground-riding target;
dense pines; the eight hulls at 800k triangles for the battle view. 0.22.9 (issue6): the wake
map (a trail painted on the water and left there), blast rings drawn in the sea's own surface,
greyer and rattier smoke, the spritsail re-cut, a near-camera dissolve, a Battle panel - and
the **physics foundation** (`sim/naval_physics.cc`): a 120 Hz fixed step with interpolated
rigid-body transforms, compound hulls, ship-to-ship and ship-to-coast collision, under the
staged battle and covered by `physics_test`. 0.22.10 (issue7): rolling broadsides with more
fire and greyer smoke, the real foam tiles on the wake and the whitecaps, the rig moving as one
rigid body with the hull, sharper tessellation, and the recording path (`MARE_RECORD`, the
keyed trailer camera, Blender encoding) that made the first 45-second battle film.

## Next, in order

1. **A finer DEM for the Adriatic.** Data only - the reader already takes any `mare-terrain-v2`
   set. 2.8 km is the floor under every "it looks blocky close up" complaint, including the
   remaining spikiness of the ranges. Cut `dalmatia-detail-3x` (0.7 km, already exported) into
   the package layout first, then a 100-300 m set if one can be sourced.
2. **Terrain self-shadow at the regional zoom** (horizon map per ring, 8 directions). This is
   what makes the `assets/example` frames read as relief rather than paint, and it is
   independent of the data resolution.
3. **The rest of the physics foundation (dev.txt P1.3).** Items 1-6 are done (0.22.9). Next:
   multi-point buoyancy against `Sea::height_at`, so heave, pitch and roll come from the
   bodies instead of the mesh shader; then ballistics and hits against the compound hulls;
   then the component damage model, flooding, fire and sinking, written back to the
   strategic ship.
4. **Naval VFX (P1.5).** Flash, layered smoke, shockwaves, water rings and spray exist
   (0.22.8); fire, shot splashes, debris, sail damage and sinking wait on the damage model
   above - the particle system is their vehicle.
5. **Textured assets proper**: UVs through the mesh layout and the base-colour sheet sampled
   in the fragment stage. 800k vertex colours (0.22.8) is the ceiling of the baked path; this
   is the step after it if the battle zoom still wants more.
6. **Procedural cities and ports (P2.1).** The painted village mask and the house props are a
   placeholder for a real settlement generator with street layout and cultural kits; Venice,
   Dubrovnik and Constantinople want hand-placed landmarks.
7. **Erosion on the rings, canopy displacement** - the two cheap terrain items from
   DESIGN_VISUAL_TARGET that do not need new data.
8. **The science, technology and shipbuilding-economy models (P2.2-P2.12).** All still on
   paper. The shipbuilding contract flow is the biggest single gameplay feature left and the
   one the core loop in dev.txt is built around.
9. **Rivers**, once the authored data lands: replace the generated network, restore carving
   behind it, and open the navigable reaches again.

## Standing constraints

- GL 4 and the v3 framework stay for this phase; Vulkan and DX12 bring their devices up and
  delegate.
- Every height threshold in `terrain.frag` and every prop the scatter places is tied to the
  world's vertical exaggeration, so that number (`GC_HEIGHT_SCALE`, now 16 per 9000 m, tunable
  with `MARE_HEIGHT_SCALE`) is a global decision, not a per-zoom one.
- No mass asset import before the automated asset path is exercised end to end (dev.txt P1.6).
