# Road map after 0.22.7 (worktree oak, 2026-10-31)

Where the renderer and the simulation stand after the four issue folders, and the order the
remaining work should be taken in. The backlog this is drawn from is `assets/dev.txt`
(checked copy: `docs/dev/dev_oak_*.txt`) and `docs/todo_list.md` (issue4, with priorities);
the visual target is `game/docs/DESIGN_VISUAL_TARGET.md`.

## Done to 0.22.7

Renderer foundation, asset runtime, LOD manager, worker pool and async loading, world
streaming, the terrain-v2 world package as both terrain *and* coastline, the ocean (patch
grid, both tessellation stages, screen-adaptive factors, Gerstner sets, normal maps, Fresnel,
glitter, foam, bow wave, trailing wake, screen-space reflections), the authored fleet with
data-driven classes, the painted hinterland with instanced props, roads, map modes, the
staged battle and city scenes. 0.22.7 (issue4): the battle's own clock, the wind shown and
used in the battle, the stage coast read from the world (bearing, land cover, relief) with a
shore the surf breaks on, open-sea haze, a free camera height, and a particle system for the
broadsides (wind-blown smoke, muzzle flash, embers).

## Next, in order

1. **A finer DEM for the Adriatic.** Data only - the reader already takes any `mare-terrain-v2`
   set. 2.8 km is the floor under every "it looks blocky close up" complaint, including the
   remaining spikiness of the ranges. Cut `dalmatia-detail-3x` (0.7 km, already exported) into
   the package layout first, then a 100-300 m set if one can be sourced.
2. **Terrain self-shadow at the regional zoom** (horizon map per ring, 8 directions). This is
   what makes the `assets/example` frames read as relief rather than paint, and it is
   independent of the data resolution.
3. **The lowland coasts from the data (todo_list 22-23).** The Venice screenshot in issue4:
   cliffs and a plateau where the lagoon is at sea level. Gate the rings' procedural ridge and
   the shore mask's cliff class by the data's real relief within ~10 km of the waterline.
4. **Physics foundation (dev.txt P1.3).** Fixed timestep, buoyancy from multi-point sampling of
   the same wave function the renderer uses, ship-to-ship and ship-to-coast collision, then
   the damage model. The wave function is already shared; the rest is new. The staged battle
   now has a clock of its own and a particle system to show the results in.
5. **Naval VFX (P1.5).** Flash, wind-blown smoke and embers exist (0.22.7); fire, water
   impacts, debris, sail damage and sinking wait on the damage model above - the particle
   system is their vehicle.
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
