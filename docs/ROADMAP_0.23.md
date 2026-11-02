# Road map after 0.22.17 (session elm, 2026-11-02)

Where the renderer and the simulation stand after the four issue folders, and the order the
remaining work should be taken in. The backlog this is drawn from is `assets/dev.txt`
(checked copy: `docs/dev/dev_oak_*.txt`) and `docs/todo_list.md` (issue4, with priorities);
the visual target is `game/docs/DESIGN_VISUAL_TARGET.md`.

## Done to 0.22.17

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
keyed trailer camera, Blender encoding) that made the first 45-second battle film. 0.22.11
(issue8): recoil, real round shot with splashes and hits, five-point buoyancy, a wake that
spreads into a cone and fades, a stern wave, the oil-slick and flicker fixes, oars outboard, the
Alps widened, a blurred battle behind the menu, closed-canopy forests, fenced parcels, vine
rows and standing wheat, the sail state as class data, bow spray in a breeze. 0.22.12
(issue9): the relief cut by 70% with every height threshold rescaled, fields only round the
settlements and woods over the rest, fuller villages, cattle and flocks, the recoil as a
centimetre of set and a quarter-degree heel per gun with the body's attitude drawn on every
part of the ship, the wake as a V of breaking crests rooted at the stem, greyer slower smoke
and torn clouds, sheets on every sail, the fog no longer leaving silhouettes. 0.22.13 (issue10): the asset
store sorted to one tree, the world package unpacked and read, the relief at 12, seven cannons at
the ports, the Meshy smoke/fire/foam sheets in the particle shader, a smaller and less white blast
ring, the canopy as trees only, fields as paint with orchards and vineyards as props, hamlets and
more farm villages, the rig made fast at the mastheads with the lateen peak at the truck, the
wake's geometry out to four hull lengths with a trail that spreads and fades, the menu blur
softened - and the class data file applied for the first time. 0.22.14 (issue11/12): the lantina as the sail's
own spar, bow waves and spray off every moving stem, whitecaps gated by a random field and no
longer flickering, dense white-and-grey gunsmoke, oars squared, the standing rigging kept
aboard, the cannon parts off and round shot drawn, ropes at the foot of the sails, relief 7, the
package's rivers read in place of the generated network, the 27 authored trees stood by biome,
fleet icons faded near the ground, the asset names readable and the folder listing. 0.22.15 (issue12's rest): volumetric gunsmoke ray-marched from density blobs, cordage that
sags and swings, sternway in the eye of the wind and oared ships furling to pull, the point of
sail on every staged ship, the sun moving in delta time, battle wavelengths from the wind, matte
wood, canvas and rope, sails kept at every distance in battle, the trees' far LOD ring.

 0.22.17 (issue14, session elm): the helm answers Q and E in the
battle - they had never been bound; the oars row with the blade in the water about a rowlock on
the rail at a slow fifteen-degree sweep; six inland villages a region, roaded to their port; the
lateen lifted clear of the deck and the yards trimmed by the bisector; ropes with real elasticity
and round cordage on the canvas; the stays picked by station, so the foremast no longer runs one
the length of the ship to the taffrail. And the **damage model**: zones, masts going by the
board, flooding and sinking - together with the two bugs that had made every battle bloodless,
a world/local frame mismatch that meant no ball had ever struck a hull, and pieces laid upward
from above the target.

## Next, in order

0. **Fire, and the butcher's bill going home** (todo 152-153). The damage model lands this round;
   what it still lacks is fire spreading from a hit and - the one that matters for the campaign -
   the tactical result written back into the strategic ship: hull integrity, crew, guns, sails and
   the sunk flag, so a battle finally costs a fleet something that outlives the scene.

0. **Rope verlet and the lateen back face** (todo 121b, 105): the cordage sags now; a chain per
   sheet and brace that follows the swung sail, and a proper two-sided canvas material.
0b. **Rivers, the second half.** The package's rivers are read and painted (0.22.14); carving the
   great rivers' lower reaches and reopening river navigation on that data is what remains of
   item 9 below.
1. **A finer DEM for the Adriatic.** Data only - the reader already takes any `mare-terrain-v2`
   set. 2.8 km is the floor under every "it looks blocky close up" complaint, including the
   remaining spikiness of the ranges. Cut `dalmatia-detail-3x` (0.7 km, already exported) into
   the package layout first, then a 100-300 m set if one can be sourced.
2. **Terrain self-shadow at the regional zoom** (horizon map per ring, 8 directions). This is
   what makes the `assets/example` frames read as relief rather than paint, and it is
   independent of the data resolution.
3. **The damage model (dev.txt P1.3).** Items 1-9 are done (0.22.9-0.22.11): the fixed step,
   the rigid bodies, collision, buoyancy, ballistics with hits, recoil. Next is what the hits
   *do*: zones with hit points, masts that fall, canvas that tears, flooding, fire and the
   sinking state, written back to the strategic ship - and the VFX that ride on each.
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
  world's vertical exaggeration. Since 0.22.12 they are all expressed relative to the 0.22.6
  scale through `geochunks_height_scale_rel()` / `u_hscale`, so `GC_HEIGHT_SCALE` (now 7 per
  9000 m since 0.22.14 - 5, 6.5 and 8 were compared; `MARE_HEIGHT_SCALE` to tune) can move without the snow line or the tree line moving
  with it - but it is still a global decision, not a per-zoom one.
- No mass asset import before the automated asset path is exercised end to end (dev.txt P1.6).
