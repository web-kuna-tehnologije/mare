# Todo list - issue folders 4 and 5 (assets/issues/issue4, issue5; 2026-10-31, worktree oak, 0.22.7-0.22.8)

Every line of the `issues.txt` files and every screenshot in the folders, as written, with a priority.
P0 = this round, in order; P1 = next round; P2 = after the physics foundation exists;
P3 = waits on data, assets or a design decision. `[x]` done, `[~]` partly, `[ ]` open.

## Battle view (the staged scene)

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 1 | P0 | Wind arrows in battle mode | [x] 0.22.7: arrows over the water, from the real wind at the battle's position |
| 2 | P0 | Slowing time in battle mode down to a stop; a separate battle time | [x] 0.22.7: battle clock (Space, 1-4, -/+), strategic clock untouched |
| 3 | P0 | Wind speed at sea battles | [x] 0.22.7: knots and compass point in the battle HUD, read by the ships |
| 4 | P0 | Foam and waves crashing on the coast in battle mode | [x] 0.22.7: the staged sea has a real shore profile, so the breaker, surf and swash code runs on it |
| 5 | P0 | Camera moves freely up and down in battle, not one-directional | [x] 0.22.7: full pitch range in the staged scenes (middle-drag / PageUp / PageDown), eye held above the water |
| 6 | P0 | A little more landscape behind the battle coast | [x] 0.22.7: heightfield coast with hills, a beach, trees in the forest biomes, built from the world at the battle's position |
| 7 | P0 | Battle in the open sea: fog of war in the distance, no coast | [x] 0.22.7: no land within ~60 km -> no stage coast, denser haze at the horizon |
| 8 | P0 | Battle near a desert: desert background, etc. | [x] 0.22.7: the stage's biome comes from the terrain package (bare -> sand and dunes, trees -> wooded, crops/grass -> green) |
| 9 | P1 | Wind against the ship moves it backwards and rotates it (weight, sails, underwater part), per ship | [~] leeway + falling off exists on the map (0.22.6); in the staged battle each side's line now sails at the pace the point-of-sail curve gives its heading (0.22.7); true sternway with weight, sail area and underwater body is the rigid-body ship of P1.3 |
| 10 | P0 | Gunsmoke puffs and muzzle flash; no projectile VFX | [x] 0.22.7: muzzle flash + a plume per gun, from the class's gun count |
| 11 | P0 | Volumetric cannon smoke that flows with the wind | [x] 0.22.7: the particle system advects every puff with the battle wind; 3D-noise eroded billboards (`fx/smoke.frag`) |
| 12 | P0 | Particle system; explosion particles by ship size and gun count, different particle sizes | [x] 0.22.7: `gfx/particles.cc` (instanced billboards, wind, drag, buoyancy, size over life); puff count and size scale with the class's guns and length |
| 13 | P1 | Particle system support "instanced puffs only" (dev.txt) -> real system | [x] same as 12 |
| 14 | P1 | Fire and smoke, 3D volumetric | [ ] needs the damage model (P1.3 fire) to have something to burn; the particle system is the vehicle |
| 15 | P1 | Water impacts: ship bow foam only; real waves generated | [~] bow wave + Kelvin wake exist (0.22.6); shot splashes need the ballistic model (P1.3) |
| 16 | P2 | Debris (procedural models now, authored later) | [ ] after hull/mast damage exists |
| 17 | P1 | Sail effects / damage presentation (a char or a file to pick the look) | [ ] `assets/ships/ship_classes.json` is the place: a `sail_state` field (new/used/torn) per ship; the textures are in (0.22.6) |
| 18 | P2 | Sinking effects | [ ] after flooding/sinking state (P1.3) |
| 19 | P1 | Weather effects in battle | [~] rain, haze, mist and storm overcast exist on the map (weather.cc) and the staged scene inherits them; 0.22.7 adds the open-sea haze; no spray, no lightning, no rain squalls as particles yet |
| 20 | P1 | Rigging and masts: increase height and thickness so they are proportional (screenshot) | [x] 0.22.8: every mast keeps the main's girth (the radial slim is divided by the height ratio), fore and mizzen heights raised in `ship_classes.json` (carrack fore 0.80 -> 0.88, war carrack 0.82/0.94/0.82 -> 0.90/0.95/0.86, hulk 0.80/0.78 -> 0.88/0.84) |
| 21 | P1 | Issue sails (screenshot): the lateens read dark brown and the foam blobs under the hulls are flat discs | [ ] the "used" suit's albedo is too dark in shadow; the hull foam disc wants the wake shape |

## Strategic map

| # | Pri | Item | Status |
|---|-----|------|--------|
| 22 | P1 | Fix the coasts of Amsterdam, the Lowlands, England and similar to match real-world data (screenshot "fix coast to be realistic lowlands") | [x] 0.22.8: the cliff class and the cliff profile are decided by the data's land height behind the shore, not the 7 km base cell's (which carries the highest ground of the cell to the waterline); the ridge noise in the coastal fringe only where the data is high |
| 23 | P1 | Reduce terrain height near the coast (Dalmatia is near 0 m, not 500) | [x] 0.22.8: inside the shore band the relief is the data's own height (held just above the waterline); the base cell's height only returns past the band (`terrain_rings.cc`) |
| 24 | P1 | Zoom in a little more on the strategic map, but block moving through terrain | [x] 0.22.8: `CAM_MIN_DIST` 2.5 -> 1.6 world units x scale; the look-at point rides the ground under it when the camera is close (main.cc), the eye floor over land already existed |
| 25 | P1 | Dense pine forests | [x] 0.22.8: tree share 0.62 -> 0.80 of forest texels, pines 17% taller, a second pine per texel on full-cover conifer ground, conifers from 1.4 units / 46 degrees north |
| 26 | P1 | Vineyards and 3D crops visible on the strategic map; sheep | [~] vines, corn, orchards and sheep (pasture round the villages, `scatter.cc`) exist as props to 40 units; the props stop before the strategic zoom - a far card LOD for the fields is what is left |
| 27 | P0 | Keep the `mare_{workdir}_{timestamp}` root directory style, delete the others | [x] 0.22.7: `C:\projects\mare_oak_20261031_171857`; the old `mare_oak_*` and `snap_oak_*` folders removed (other sessions' folders untouched) |

## issue5 (assets/issues/issue5, 0.22.8)

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 28 | P0 | "Again issue with masts: remove the lantina from the mast, separate objects" | [x] checked in Blender: `mast_mizzen` is parts 1-6 of the split import (148k tris), `mast_lateen_tall` parts 1-4 - the yards (parts 0, 5, 7) are *not* in them; the lantina is the separate `lateen_yard` asset, swung on its own. What read as a second spar in the screenshot is the stays and the procedural far-LOD spar overlapping; 0.22.8 guards both spar paths (below) |
| 29 | P0 | The sail and cloth textures: use them for the ropes too | [x] 0.22.8: the Meshy cordage tiles (`rope_premium`, `rope_used`) are laid along every rope by the geometry stage (across the rope, distance along it in diameters), new hemp or tarred by the ship's condition, with a procedural twist where the texels are gone |
| 30 | P0 | More smoke: 3-4 layers of smoke clouds; the shock on water and air (a blur at the muzzle flash) | [x] 0.22.8: three smoke layers per gun (white cumulus core, grey plume, lingering haze) + flash + embers; a ring of flattened water and a sheet of spray under a low gun; a shockwave per three guns that the tonemap bends and radially blurs the picture with for a third of a second (`hdr/tonemap.frag`) |
| 31 | P0 | "When zoomed in very close to the ship everything falls apart" (issue5.png: two spars the size of the fleet lying across the sea) | [~] 0.22.8: a yard, lantina or bowsprit is placed only when its asset is loaded with real bounds (a streaming part has zero bounds and a scale computed from them is that giant spar), and a spar longer than the hull is suppressed and logged. Not reproduced in pinned-camera runs at 6-14 units; if it returns, the log line names the asset |
| 32 | P1 | 800k models with high texture quality through Blender, battle mode only | [x] 0.22.8: the eight hull classes re-imported with LOD 0 at 800,000 triangles and the base colour baked from 2048 px (`tools/meshy_import.py --lod0 800000 --tex 2048`; `.meshy.json` records it). LOD 0 is only reached in the battle and city tiers (`scene.cc`), so the map never pays for it. In this renderer "texture quality" is vertex colour density: 800k vertices on a 47 m hull is one colour sample every 6 cm. The mast parts keep 150k |
| 33 | P1 | issue7.png: fix the size of the rigging and masts, the lantina thickness | [x] same as 20; the lantina is 15% slimmer than 0.22.5 already and now stands beside a mizzen of the main's girth |
| 34 | P1 | issue6.png (the fusta under one lateen in the haze) | [x] nothing wrong found in it: one mast, one lantina, the oars; filed as the reference for the lateen rig |

## P1.3 Physics foundation (M5.5) - the dev.txt checklist

| Pri | Item | Status |
|-----|------|--------|
| P1 | Fixed physics timestep independent of render FPS | [ ] the strategic sim already steps at SIM_STEP; the staged battle steps per frame |
| P1 | Tactical naval fixed sub-step | [ ] |
| P1 | Stable rigid-body ship transforms | [ ] |
| P1 | Simplified convex/compound collision hulls | [ ] |
| P1 | Ship-to-ship collision | [ ] |
| P1 | Ship-to-coast collision | [ ] |
| P2 | Wreck collision | [ ] |
| P1 | Multi-point buoyancy sampling | [ ] `Sea::height_at` is the function to sample |
| P1 | Pitch / roll / heave | [~] visual only, in the mesh shader; no rigid body |
| - | Physics samples the same wave function as the renderer | [x] |
| P2 | Broadphase collision | [ ] |
| P1 | Ballistic ray / swept-sphere projectile model | [ ] |
| P1 | Cannonball / projectile hits | [ ] |
| P1 | Component / zone damage model | [ ] |
| P1 | Hull damage | [ ] |
| P1 | Mast damage | [ ] |
| P1 | Sail damage | [ ] |
| P1 | Gun damage | [ ] |
| P1 | Fire | [ ] |
| P1 | Flooding | [ ] |
| P1 | Sinking state | [ ] |
| P2 | Constraints only where gameplay-relevant | [ ] |
| P2 | Mast / debris constraints where needed | [ ] |
| P3 | Towing / boarding constraints if needed | [ ] |
| P2 | Multithreaded physics jobs | [ ] the worker pool exists |
| P1 | Tactical results update strategic ship state | [ ] |
| - | Strategic simulation remains discrete: 1 day = 1 tick | [x] |
| P1 | Strategic state includes hull integrity, crew/morale, guns, sails, flooding, fire, sinking | [~] hull/crew/guns exist; flooding/fire/sinking do not |

## Order of work (updated 0.22.8)

1. Items 1-8 and 27: the battle scene's clock, wind, coast and landscape from the world. **Done 0.22.7.**
2. Items 10-13: the particle system, wind-blown smoke, muzzle flash. **Done 0.22.7.**
3. Items 22-23: lowland coasts and coastal height from the data. **Done 0.22.8.**
4. Items 20, 33: mast proportions. **Done 0.22.8.** Item 21 (the dark used suit) is open.
5. Items 24-25: strategic zoom floor, denser pines. **Done 0.22.8.** Item 26's far LOD for the fields is open.
6. issue5 items 28-32. **Done 0.22.8** (31 guarded, not reproduced).
7. Next, in this order:
   1. P1.3 fixed timestep + multi-point buoyancy on `Sea::height_at` for the staged ships (the hulls stop sliding over the swell and start riding it; the base for everything below).
   2. Ship-to-ship and ship-to-coast collision in the staged battle (the lines can then close to boarding range without passing through each other).
   3. The ballistic model + hits -> hull/mast/sail damage; then items 14 (fire), 15 (shot splashes), 16 (debris), 18 (sinking) through the particle system.
   4. Item 17: `sail_state` per ship in `ship_classes.json` / per fleet, so a battered ship shows it.
   5. Item 21 (used canvas too dark in shadow), item 26 (field cards at the strategic zoom), item 9 (true sternway once the rigid body exists).
   6. Textured assets proper (UVs through the mesh layout) if 800k vertex colours are still not enough "texture quality" at battle zoom.
