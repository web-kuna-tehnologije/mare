# Todo list - issue folder 4 (assets/issues/issue4, 2026-10-31, worktree oak, 0.22.7)

Every line of `issues.txt` and every screenshot in the folder, as written, with a priority.
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
| 20 | P1 | Rigging and masts: increase height and thickness so they are proportional (screenshot) | [ ] fore and mizzen masts of the carracks read thin and short beside the main; raise `MAST_SLIM` for the lesser masts and their heights in `ship_classes.json` |
| 21 | P1 | Issue sails (screenshot): the lateens read dark brown and the foam blobs under the hulls are flat discs | [ ] the "used" suit's albedo is too dark in shadow; the hull foam disc wants the wake shape |

## Strategic map

| # | Pri | Item | Status |
|---|-----|------|--------|
| 22 | P1 | Fix the coasts of Amsterdam, the Lowlands, England and similar to match real-world data (screenshot "fix coast to be realistic lowlands") | [ ] the Venice shot shows cliffs and a 500 m plateau where the lagoon is at sea level: the ring's procedural ridge noise and the shore mask's cliff class must be gated by the data's real relief near the coast |
| 23 | P1 | Reduce terrain height near the coast (Dalmatia is near 0 m, not 500) | [ ] a coastal taper on the ring height: the data's own height within ~10 km of the waterline, no procedural lift |
| 24 | P1 | Zoom in a little more on the strategic map, but block moving through terrain | [ ] lower `CAM_MIN_DIST` on the map and a camera floor from the terrain height under the eye (the sea floor exists, the land floor does not) |
| 25 | P1 | Dense pine forests | [~] `MARE_FOREST_DENSITY` and pine stands exist (0.22.6); a denser default and taller pines above the tree line are a tuning pass |
| 26 | P1 | Vineyards and 3D crops visible on the strategic map; sheep | [~] vines, corn, orchards exist as props to 40 units; sheep are not placed; the props stop before the strategic zoom |
| 27 | P0 | Keep the `mare_{workdir}_{timestamp}` root directory style, delete the others | [x] 0.22.7: `C:\projects\mare_oak_20261031_171857`; the old `mare_oak_*` and `snap_oak_*` folders removed (other sessions' folders untouched) |

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

## Order of work

1. Items 1-8 and 27: the battle scene's clock, wind, coast and landscape from the world. **Done 0.22.7.**
2. Items 10-13: the particle system, wind-blown smoke, muzzle flash. **Done 0.22.7.**
3. Items 22-23: lowland coasts and coastal height from the data (the Venice screenshot).
4. Items 20-21: mast proportions, the dark lateens.
5. Items 24-26: strategic zoom floor, denser pines, sheep.
6. P1.3 from the top: fixed step, buoyancy sampling, collision, then the damage model; items 14-18 ride on it.
