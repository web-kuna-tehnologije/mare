# Todo list - issue folders 4 to 12 (assets/issues/issue4-12; 2026-10-31/11-01, worktree oak, 0.22.7-0.22.15)

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
| 15 | P1 | Water impacts: ship bow foam only; real waves generated | [x] 0.22.11: round shot splash where they fall (ballistics events -> `particles_splash`: a spout and a foam ring); the stern wave and the diffusing wake are the generated waves |
| 16 | P2 | Debris (procedural models now, authored later) | [ ] after hull/mast damage exists |
| 17 | P1 | Sail effects / damage presentation (a char or a file to pick the look) | [x] 0.22.11: `"sails": mixed|new|used` per class in `ship_classes.json` (war/great carrack new, fusta used, the rest per ship). A *torn* state waits on sail damage (P1.3) |
| 18 | P2 | Sinking effects | [ ] after flooding/sinking state (P1.3) |
| 19 | P1 | Weather effects in battle | [x] 0.22.11: spray torn off every stem in a fresh breeze and carried over the decks (`particles_spray`), on top of the inherited rain, haze, mist and overcast and the open-sea fog. Lightning is not done |
| 20 | P1 | Rigging and masts: increase height and thickness so they are proportional (screenshot) | [x] 0.22.8: every mast keeps the main's girth (the radial slim is divided by the height ratio), fore and mizzen heights raised in `ship_classes.json` (carrack fore 0.80 -> 0.88, war carrack 0.82/0.94/0.82 -> 0.90/0.95/0.86, hulk 0.80/0.78 -> 0.88/0.84) |
| 21 | P1 | Issue sails (screenshot): the lateens read dark brown and the foam blobs under the hulls are flat discs | [x] 0.22.11: the foam under a hull is the wake map's churn now (0.22.9-11), no discs; the dark lateens were the fusta's *used* suit, which the class file now declares (todo 17) so it reads as intended rather than as a lighting fault |

## Strategic map

| # | Pri | Item | Status |
|---|-----|------|--------|
| 22 | P1 | Fix the coasts of Amsterdam, the Lowlands, England and similar to match real-world data (screenshot "fix coast to be realistic lowlands") | [x] 0.22.8: the cliff class and the cliff profile are decided by the data's land height behind the shore, not the 7 km base cell's (which carries the highest ground of the cell to the waterline); the ridge noise in the coastal fringe only where the data is high |
| 23 | P1 | Reduce terrain height near the coast (Dalmatia is near 0 m, not 500) | [x] 0.22.8: inside the shore band the relief is the data's own height (held just above the waterline); the base cell's height only returns past the band (`terrain_rings.cc`) |
| 24 | P1 | Zoom in a little more on the strategic map, but block moving through terrain | [x] 0.22.8: `CAM_MIN_DIST` 2.5 -> 1.6 world units x scale; the look-at point rides the ground under it when the camera is close (main.cc), the eye floor over land already existed |
| 25 | P1 | Dense pine forests | [x] 0.22.8: tree share 0.62 -> 0.80 of forest texels, pines 17% taller, a second pine per texel on full-cover conifer ground, conifers from 1.4 units / 46 degrees north |
| 26 | P1 | Vineyards and 3D crops visible on the strategic map; sheep | [x] 0.22.11: vines in contour rows, every grain parcel full of standing wheat, every parcel fenced round, woods a closed canopy - all 3D props within the near ring (40 units), none casting shadows. Past the near ring the painted land-use carries them, as designed |
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

## issue6 (assets/issues/issue6, 0.22.9)

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 35 | P0 | Add a turn-off wind arrows button | [x] 0.22.9: a Battle panel with Wind arrows, stop/run, the four speeds and Leave; N still works |
| 36 | P0 | Rigging/masts priority; coming close to the rigging makes the whole screen invisible - hide parts too close to the camera, add transparency | [x] 0.22.9: everything within ~1.15x the near plane is dropped and the band above it dissolves on its own Bayer grid (`dither_clip_at`), in the mesh, cloth and cordage pipelines |
| 37 | P0 | Fix the sails as in the folder's image (`front lantent issue sail.png`) | [x] 0.22.9: the spritsail was cut tall and narrow with its head above the bowsprit and its foot in the sea; it is now a shallow square sail nearly twice as wide as deep, bent under the bowsprit, foot clear of the water |
| 38 | P0 | Foaming on the waves a ship produces | [x] 0.22.9: the wake map's foam channel, broken up by the torn-streak tile, over the existing bow wave and hull break |
| 39 | P0 | A trail of foam after the ship that fades away, leaving the waves as she creates them | [x] 0.22.9: `gfx/wake.cc` - a world-space map each ship paints into and which fades over ~50 s; it curves where she turned, and the sea's vertex stage flattens the water she has broken |
| 40 | P0 | The shock more transparent, still the actual sea mesh, and only on the side she is firing | [x] 0.22.9: the flat discs are gone; the ring is displaced and whitened in the sea's own surface, 8% strength to windward against 100% abeam of the guns, and the screen-space air shock is a third as strong |
| 41 | P0 | A shock around the ship in wave form: a light transparent white circle, more opaque on the firing side, not below the cannon | [x] the same ring: a ripple in the vertex stage, a light white in the fragment stage, with a hole over the water directly under the guns |
| 42 | P0 | Cut the textures in `assets/ships/FOAM` to make the ship slicing the sea realistic | [x] 0.22.9 `tools/foam_tiles.py`. Note: those sheets are **sailcloth**, not foam. The tool keeps only their tears - high-pass, de-stripe (the weave and seams were tiling across the sea as quilting), threshold to the brightest quarter, blur - and writes `foam_wake.png` / `foam_streak.png` |
| 43 | P0 | Make the smoke more grey and dense, more of it, not a perfect shape | [x] 0.22.9: core shade 0.96 -> 0.80, plume 0.80 -> 0.66, four balls a layer instead of three, each off the gun's line, and the silhouette warped by a second coarse noise |

## issue7 (assets/issues/issue7, 0.22.10)

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 44 | P0 | More fire and greyer smoke | [x] 0.22.10: bigger, longer flash, two tongues of flame per gun, twice the embers; core 0.72, plume 0.56, haze 0.52 |
| 45 | P0 | Cannons fire not all at once but with random timeouts (13, 25, 968 ms ...) | [x] 0.22.10: every gun is queued with its own delay (a fifth at once, the rest squared over up to ~1.7 s) and fires when due |
| 46 | P0 | The new foam texture in `assets/ships/foam/new` | [x] 0.22.10: both masks cut (the checkerboard they were flattened onto is thresholded away) into `foam_wake` / `foam_streak`, read by the wake and the hull break |
| 47 | P0 | Foam on top of the collapsing waves, more real | [x] 0.22.10: the foam photograph (`foam_crest`) textures the whitecaps and the crest band at two scales; the band is wider and weighs more |
| 48 | P0 | A little more tessellation / geometry sharpness, like the picture | [x] 0.22.10: sea 16 -> 24 per edge, terrain 32 -> 40 with finer cells; the stipple in the picture itself was the near-camera dissolve, now tight and staged-scenes-only |
| 49 | P0 | The FOAM folder's images were corrected | [x] 0.22.10: `tools/foam_tiles.py` rewritten for the real foam: the photograph -> `foam_crest`, the sheet's streak panel -> `foam_lane` |
| 50 | P0 | The lantina and the sail should act with the ship as if tied with rope | [x] 0.22.10: roll and pitch applied to the whole ship about one pivot in all three vertex stages; parts no longer slide against the hull |
| 51 | P0 | Build the game and make a 45 s, 60 fps trailer of a battle in foggy weather with 40+ ships, as an MP4 | [x] 0.22.10: `MARE_RECORD`, `MARE_BATTLE_SHIPS`, the keyed trailer camera, `tools-bat\trailer.bat`, Blender encoding; `trailer\mare_battle_trailer.mp4` |

## issue8 (assets/issues/issue8, 0.22.11)

| # | Pri | Item (the file names) | Status |
|---|-----|-----------------------|--------|
| 52 | P0 | A little bigger wave amplitude but less foam, more dense and concentrated - this looks artificial | [x] 0.22.11: staged swell 0.45 -> 0.60; the wake stamp narrower and denser, the trail thresholded so only solid churn shows |
| 53 | P0 | Add the ship battle as the start menu, but blurred | [x] 0.22.11: a staged battle behind the title with the trailer camera on a loop, blurred four times at quarter size in the tonemap (`MARE_NO_PREVIEW=1` for the plain menu) |
| 54 | P0 | The Alps look too pointy; thicker in width, less pointy, but a good start | [x] 0.22.11: ridge crest power 2 -> 1.35 at two thirds the amplitude, peak boost 0.28 -> 0.22 with a soft knee over 2600 m |
| 55 | P0 | Good start but add fading out and cone spreading and disappearing; add real generated waves from stern and bow | [x] 0.22.11: the wake map diffuses as it decays (a cone that thins to nothing); a transverse stern wave and a short train down the lane join the bow wave |
| 56 | P0 | Missing crest and foam on top of the waves | [x] 0.22.11: a floor under the foam colour (it vanished under a dark sky), whitecaps from a smaller swell |
| 57 | P0 | Oars on the wrong side | [x] 0.22.11: the bank's heading sign was flipped - the blades pointed inboard and lay across the deck |
| 58 | P0 | Water looks weird, like an oil spill; resolve the flickering (video) | [x] 0.22.11: a third of the tile's tonal darkening, roughness floor 0.26 -> 0.36 close in, the glitter lobe widened and tied to the specular AA (the per-pixel sparkle was the flicker), SSR damped and off within a few units of the eye. The video's last seconds were the camera inside a hull on the map: the near-camera dissolve is back there as a tight band |
| 59 | P0 | When a ship fires she gets a backward force that moves her sideways and turns her (Newton, torque) | [x] 0.22.11: `nphys_impulse` per gun at the gun's position as it fires (recoil to leeward, yaw from the lever arm), x6 for legibility |
| 60 | P0 | Real forests, not a tree here and there | [x] 0.22.11: every forest texel a tree plus up to three more on full cover; the canopy closes |
| 61 | P0 | Fence the fields like in real life, not random placements | [x] 0.22.11: a panel on every cell of all four parcel edges, on the parcel grid |
| 62 | P0 | Vineyards, wheat and similar in 3D, no shadow casting for now | [x] 0.22.11: vines in contour rows, wheat on a jittered grid over every grain parcel; the scatter was already main-pass only (no shadows) |

## issue9 (assets/issues/issue9, 0.22.12)

| # | Pri | Item (as written) | Status |
|---|-----|-------------------|--------|
| 63 | P0 | Recoil: move the ship ~5 cm, not rotate five times; rotation about the x axis, a couple of percent of a degree per shot, by gun strength | [x] 0.22.12: the ball's real momentum at the hull's centre (no yaw), plus a quarter-degree heel per gun on a damped roll spring; the body's roll and pitch now reach the shader |
| 64 | P0 | Remove the fences (too much); reduce crop sizes and increase density | [x] 0.22.12: fences off; wheat 0.026 -> 0.015 every 0.019 units |
| 65 | P0 | Villages: random non-interactable settlements for the hinterland population, roads to the ports they abide to, mines and fields nearby | [x] the inland settlements, their roads to their port, mills and pitheads exist since 0.22.3-0.22.6; 0.22.12 makes them *read*: wider built discs, fuller villages, and the fields gathered round them |
| 66 | P0 | Fields only near rural or port settlements | [x] 0.22.12: a halo round each settlement and a strip along the roads; nothing elsewhere |
| 67 | P0 | Forests to fill the empty relief | [x] 0.22.12: a clumped wild woodland over every gentle lowland and hillside not crops/bare/snow |
| 68 | P0 | Clouds more uneven (not circles), greyer, slower, longer in the air | [x] 0.22.12: gunsmoke greyer, opaque, 1.6x life, half the wind, oval billboards; the sky's clouds a third the drift, greyer, torn edges |
| 69 | P0 | Masts still not thick; the front lateen too small for its sail and not connected; rope connecting sails to the boat | [x] 0.22.12: MAST_SLIM 0.64; the lantina a tenth longer than the design line; sheets from every clew to the rail / the yard below |
| 70 | P0 | Boat silhouettes visible through clouds, fog and smoke | [x] 0.22.12: the mesh fog mix goes to 100% (was capped at 85%, which kept every rope's outline); smoke opaque and longer-lived |
| 71 | P0 | Zoom a little more on the strategic map to see the landscape | [x] 0.22.12: CAM_MIN_DIST 1.6 -> 1.0 x scale |
| 72 | P0 | Waves from the ship should be directional | [x] 0.22.12: the Kelvin V rooted at the stem, twice the height, half the width, with inboard troughs; the bow's radial ring smaller |
| 73 | P0 | Real forests; crops not needed now; cows and sheep | [x] 0.22.12: wild woodland (67), cattle herds and five times the flocks on the pasture |
| 74 | P0 | Reduce elevation change by 70% | [x] 0.22.12: height scale 16 -> 5, ring noise cut to match, every height threshold rescaled through `u_hscale` / `geochunks_height_scale_rel()` |
| 75 | P0 | Foam on the crests like in the strategic map (screenshot) | [x] 0.22.12: the staged peak term starts lower and at a gentler slope; foam floor 0.30 |
| 76 | P0 | Wakes like wake.png | [x] same as 72: the arms break white, denser and narrower, in the fragment stage and the wake map |

## P1.3 Physics foundation (M5.5) - the dev.txt checklist

| Pri | Item | Status |
|-----|------|--------|
| P1 | Fixed physics timestep independent of render FPS | [x] 0.22.9 `sim/naval_physics.cc`: 120 Hz, accumulator, 8 steps a frame max. `physics_test` runs the same 8 s at 1/30 and at 1/240 and gets 960 sub-steps and the same pose either way |
| P1 | Tactical naval fixed sub-step | [x] 0.22.9: drive, drag, integrate, ship contacts, coast - that order every step; the staged battle runs on it |
| P1 | Stable rigid-body ship transforms | [x] 0.22.9: the previous pose is kept and interpolated by the leftover accumulator; longitudinal and lateral drag, helm authority from her way, heel from the turn |
| P1 | Simplified convex/compound collision hulls | [x] 0.22.9: three discs down the keel, radii from the beam |
| P1 | Ship-to-ship collision | [x] 0.22.9: broadphase, disc pairs, inverse-mass correction, restitution, Coulomb friction, and the yaw the lever arm gives |
| P1 | Ship-to-coast collision | [x] 0.22.9: the staged shore profile as a signed distance; a disc in water shallower than her draft grounds, is pushed seaward and slewed |
| P2 | Wreck collision | [ ] |
| P1 | Multi-point buoyancy sampling | [x] 0.22.11: five points per hull (three keel discs, two bilges) against the caller's surface; `physics_test` checks pitch on a fore-aft wave and roll on a beam wave |
| P1 | Pitch / roll / heave | [x] 0.22.12: the body's roll and pitch (buoyancy + recoil heel) go to the shader through `inst_t.roll_b/pitch_b` (`i_anchor` is a vec4) and turn every part of the ship about the hull's pivot; the canned motion is a third of what it was |
| - | Physics samples the same wave function as the renderer | [x] |
| P2 | Broadphase collision | [ ] |
| P1 | Ballistic ray / swept-sphere projectile model | [x] 0.22.11: `nphys_fire` - swept spheres under gravity against the compound hulls, hits and splashes as events |
| P1 | Cannonball / projectile hits | [x] 0.22.11: a hit gives the hull the ball's momentum and an event the staged scene turns into splinters; no damage yet (next row) |
| P1 | Component / zone damage model | [x] 0.22.17: `nphys_damage_t` - hull, rig, guns, flood, sunk; the zone follows the impact height |
| P1 | Hull damage | [x] 0.22.17 |
| P1 | Mast damage | [x] 0.22.17: a mast at each threshold of rig damage, with an event |
| P1 | Sail damage | [x] 0.22.17: the rig scalar costs her up to 65% of her speed |
| P1 | Gun damage | [~] 0.22.17: the scalar is raised by gun-deck hits; the battery does not yet fire slower |
| P1 | Fire | [ ] |
| P1 | Flooding | [x] 0.22.17: grows with the holes below the line and with itself |
| P1 | Sinking state | [x] 0.22.17: a wreck - dead in the water, no helm, no guns |
| P2 | Constraints only where gameplay-relevant | [ ] |
| P2 | Mast / debris constraints where needed | [ ] |
| P3 | Towing / boarding constraints if needed | [ ] |
| P2 | Multithreaded physics jobs | [ ] the worker pool exists |
| P1 | Tactical results update strategic ship state | [ ] |
| - | Strategic simulation remains discrete: 1 day = 1 tick | [x] |
| P1 | Strategic state includes hull integrity, crew/morale, guns, sails, flooding, fire, sinking | [~] hull/crew/guns exist; flooding/fire/sinking do not |

## issue10 (assets/issues/issue10, 0.22.13) - and what was found on the way

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 77 | P0 | Sort the asset folders and names, one copy of everything, elm/oak: keep the better | [x] 0.22.13: `assets/generated` is the one tree (the other worktree's name for `elm`); `oak`, `elm_lod`, `ships_clean`, the `Meshy_AI_*` hull copies, `rigging`, `oak_terrain`, the 2.8 GB normal map gone; `sea/sheets`, `ships/foam/{photos,masks}`, `reference/`; `assets/README.md`. 24 -> 15 GB |
| 78 | P0 | Increase the elevation to 12 | [x] `GC_HEIGHT_SCALE` 5 -> 12; the ranges show the 2.8 km grid as terraces (row 93) |
| 79 | P0 | Cannons and smoke/fire/foam particle images: add them to the ships and the effects, make volumetric smoke | [x] seven `gun_*` parts at every port of every class (`gun_role_for`, `"gun"` in the class file); RGBA atlases from the sheets (`tools/fx_sprites.py`) sampled by the puff shader - cover, luminance, cover-gradient normal; fire on the flash, foam on the rings and spray. [~] "volumetric" is still lit billboards: a ray-marched slab is row 95 |
| 80 | P0 | The new terrain data (`new_terrain_data_v2`): use it, generate realistic terrain | [x] unpacked and SHA-verified under `assets/data_new/mare-world-terrain`, read as `terrain-v2` (378 chunks). [ ] its rivers (`waterways.geojsonl`) are not wired: row 91 |
| 81 | P0 | The shock from the cannons: less white, waves bigger, maybe less distance | [x] ring to 1.6 L (2.3), lift 0.085 (0.035), white 0.14/0.16 (0.30/0.34) and only in the foam tile's breaks; the tonemap's radial blur halved |
| 82 | P0 | Fix the canopy to be trees only | [x] the painted crowns and their normal gone; woods are instanced trees over the forest colour |
| 83 | P0 | Return the fields, but without props | [x] cropland floor 0.55 over the gentle lowland; wheat off |
| 84 | P0 | Add orchards and vineyards as props | [x] wider orchard rings + patches in the open fields, vines on more of the sunny slopes, scatter from 70/80 (was 90/110), ahead of the woods. [ ] authored trees for them: row 96 |
| 85 | P0 | Lateen and masts and rigging to the top of the mast, not above; the front lateen as wide as the sail and connected to it | [x] rope tops per station from the heel (they ended 0.2 hull heights over every truck, a mizzen's 2/3 of the pole); peak at the masthead; the lantina scaled by its true length to the head and turned to the design rake; square layers under the truck |
| 86 | P0 | Waves like wake.png, as an actual 3D wave | [x] the arms' geometry to 4.4 L (was cut at 1.55 L), crests 0.052 L (0.034), deeper trough, the trail's chop |
| 87 | P0 | The wake's foam should fade out, be less opaque, spread slowly while it fades | [x] `WAKE_TAU` 20 s, `WAKE_DIFF` 3.5 texels/s with a two-ring kernel, white decays before churn; the trail's white follows the map value, ceiling 0.42 (0.62) |
| 88 | P0 | Random villages in the hinterland are missing | [x] hashed hamlets on the workable lowland (2.2-unit cells) with their own built disc; three farm villages per settled region (one per frontier region) |
| 89 | P0 | The video: too much blur | [x] the menu backdrop: half size, one blur pair (was quarter size, four), mixed at 0.7 |
| 90 | P0 | (found) `ship_classes.json` was never applied: `!gjson_parse` on a 0-on-success function | [x] fixed; "9 overridden" in the log, the class data is live |
| 91 | P1 | **Rivers from the package** (`waterways.geojsonl`, 115 chunks): replace the generated network, carve, open the reaches (TODO item 5, roadmap 9) | [ ] the data is in the store now; the reader does not read it yet |
| 92 | P1 | (found) HEAD did not link: `world/geochunks.*` and `gfx/chunkatlas.*` re-added by the WIP merge beside their successors; the two suites including `world.cc` unresolved | [x] removed; stub ratio in the suites; 7/7 |
| 93 | P1 | The ranges at 12 read as terraces (the 2.8 km samples) | [ ] a finer DEM for the home seas (roadmap 1), or a slope-aware interpolation of the chunk samples |
| 94 | P1 | A fleet icon is drawn at the hinterland zoom (po_plain shot: a white ship the size of a village on the plain) | [ ] fade the fleet icons out below the map's icon tier, or draw the hull |
| 95 | P2 | Volumetric smoke proper: a ray-marched density slab per broadside instead of lit billboards | [ ] |
| 96 | P2 | Authored orchard and vine props (Meshy), the mill and the well as assets (TODO 7c) | [ ] |
| 97 | P2 | The guns' muzzles through the hull's *painted* ports: the port positions from the hull mesh, not the class's gun count spread along two thirds of the hull | [ ] |
| 98 | P1 | (found) the spritsail was a 0.31-hull-wide bar across the bow from abeam | [x] half the width, twice the depth |

## issue11 + issue12 (assets/issues/issue11, issue12, 0.22.14)

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 99 | P0 | issue11 "fix this lateen and sail" (the yard horizontal, far above the canvas) | [x] the lantina is a tapered prism built in the sail's mesh on the head's own line (`rig_spar`); the scanned `lateen_yard` is not placed (`MARE_LATEEN_ASSET=1`) |
| 100 | P0 | issue11 "add ship waves and splashing"; "add real waves forming from ships moving" | [x] bow wave 0.030 L (0.012), hull pile-up 0.022 L, wider foam collar; spray from every stem under way, scaling with speed |
| 101 | P0 | issue11 "crests are too uniform, make randomness"; "sea flickers too much, slow it down"; the menu video "continuous flicker" | [x] `capvar` noise field gates caps, collapsing crests and the staged peaks; wider thresholds; staged ripple drift at half rate |
| 102 | P0 | issue11 "make more dense smoke, grey and white, remove ugly particles" | [x] 6 white cores + 6 grey plumes a gun, denser and longer; embers and the gun spout removed. [ ] issue12 "remove the particles, add 3D volumetric smoke with shaders": row 95 |
| 103 | P0 | issue11 "oars need to be rotated"; issue12 "fix oars positions" | [x] squared to the hull (10 degrees aft), sweep 4-13 degrees, blade 0.60 rad down |
| 104 | P0 | issue11 "remove external riggings" (stays running down into the sea) | [x] stays end at the hull model's own bow and stern; the aft stations' shroud and stay feet on the castle |
| 105 | P0 | issue11 "fix this shading wrong" (the lateen canvas: diagonal seams, flat and dark) | [~] the canvas now hangs from its real spar and the seams run with the head as a lateen's cloths do; the dark flat look under a low sun is the two-sided flip on a single quad - a proper back-face material for the sails is open |
| 106 | P0 | issue11 "weird texture over" the hull (purple-brown blotches) | [x] plank modulation finer and a third as strong (`meshes.frag`) |
| 107 | P0 | Reduce the relief to 5-8, see what fits | [x] 5 / 6.5 / 8 compared (Alps, Dalmatia); 7 kept (`GC_HEIGHT_SCALE`) |
| 108 | P0 | Remove the cannons from the ships for now | [x] off by default; `MARE_GUNS=1` stands the parts again; assets kept |
| 109 | P0 | Make cannonballs | [x] `nphys_shot_list`; the staged scene draws every ball in flight as an iron sphere |
| 110 | P0 | Add ropes to the bottom of the sails | [x] bolt-rope along the foot, a sheet from each clew, a clew-line up the leech, in the sail's mesh |
| 111 | P0 | Rename all assets (no "Meshy_AI_..."); a file listing the whole folder structure | [x] renames audited (`assets/RENAMES.md`, by the other session); `tools/tree_listing.py` -> `docs/FOLDER_STRUCTURE.md` |
| 112 | P0 | issue12 "trees in assets/trees: rename, add them, optimise, set up by biome" (todo 96) | [x] 27 `tree_*` props, six LODs; the scatter stands them by latitude band, hemisphere, conifer/broadleaf and the Mediterranean belt; LOD 4 within `PROP_LOD1`, cards beyond |
| 113 | P1 | Rivers from the package (row 91) | [x] `tools/rivers_pack.py` -> `rivers.bin` per chunk; `world/rivers.cc` reads them (1.3M points, 63 chunks) in place of the drainage network; painted, not carved |
| 114 | P1 | Fleet icons at the hinterland zoom (row 94) | [x] fade under 7 units, gone at 4 |
| 115 | P1 | issue12 "rope behaviour to be flexible and physics based" | [ ] the cordage is straight segments in a per-class mesh; a catenary sag per rope and a verlet per sheet is the next rig item |
| 116 | P2 | The guns through the hull's painted ports (row 97) | [-] superseded: the hulls will be re-made without guns and the parts stood then |
| 117 | P1 | Authored trees: LOD 5 ring out to 14 units (cards start at 7 now); the oak/maple in the orchards are the forest species scaled down - a fruit tree set | [ ] |
| 118 | P1 | The rivers: carve the great rivers' lower reaches and reopen navigation on the authored data (`MARE_RIVER_CARVE`); the half-width from the upstream length is a guess | [ ] |

## issue12, the rest (assets/issues/issue12, 0.22.15) - and the todo rows picked up

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 119 | P0 | issue12 "remove the smoke particles; add 3D volumetric smoke with shaders" (row 95) | [x] density blobs + `fx/volsmoke.frag` fullscreen ray-march with self-shadowing; billboards behind `MARE_SMOKE_BILLBOARDS=1` |
| 120 | P0 | issue12 "faster smoke expansion from cannon fire and longer stay in the air" | [x] cubic-eased growth, lives 12-30 s (blobs) |
| 121 | P0 | issue12 "rope behaviour flexible and physics based" (row 115) | [x] eight-segment ropes with a catenary of their slack, blown by the wind, lagging the heel (`rope.vert`). [ ] a verlet chain if the look is wanted closer |
| 122 | P0 | issue12 "sails only and the wind against her: move backwards; with oars, oar speed and roll the sails down" | [x] sternway in the eye of the wind (negative drive); oared ships furl (`furl`) and pull at oar speed |
| 123 | P0 | issue12 "wind direction and sail direction for the movement speed" | [x] per-ship `wind_factor` on her own heading in the staged battle |
| 124 | P0 | issue12 "animate the sun in delta time, not in ticks" | [x] `render_sun_update(G.day + sim_acc)` |
| 125 | P0 | issue12 "in battle mode the wave length by wind strength, not zoom" | [x] fixed Nyquist cell in the staged scenes; wavelength 0.50 + 0.80 s; set rebuilt on scene change |
| 126 | P0 | issue12 "wood, sails, rope: reflectiveness 0" | [x] `sun_shade_matte` for wood, canvas, cordage |
| 127 | P0 | issue12 "don't hide sails with distance in battle view" | [x] the rig's pixel gate bypassed at the staged scale |
| 128 | P1 | Authored trees' far LOD ring to 14 units (row 117, first half) | [x] LOD 5 from 7 to 14 units. [ ] the fruit-tree set for the orchards |
| 129 | P1 | issue12 "add waves like wake.png"; "create waves continuously with the ship moving" | [x] done in 0.22.13/14 (rows 86, 100); nothing further this round |

## issue14 (assets/issues/issue14, 0.22.17) - session elm

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 130 | P0 | "Q AND E IN BATTLE VIEW DONT WORK" | [x] they had never been bound to anything. Q/E now put the player's line under manual steering and swing the ordered course; H hands her back to the line. Each ship answers at her class's rate (`detailview_helm`, `ui/input.cc`) |
| 131 | P0 | "oars again wrong: wide part needs to be in water, pivot in the ship part, movement slowly 30 deg (-15 to 15)" | [x] the blade is the model's -x end (measured: twice the loom's width) and a positive pitch carries it under - the sign had been flipped the wrong way; the oar now turns about its **rowlock on the rail**, not about its own middle hung outboard; the sweep is +-15 degrees at a stroke every 3-5 s |
| 132 | P0 | "other than cities add random small villages (houses in rural areas inland connected with roads to main cities and ports)" | [x] a settled region founds six inland villages (was three) and a frontier two, 2 units apart (was 3). Each is a real settlement, so `world/roads.cc` routes it to its port and `gfx/scatter.cc` gives it houses and a mill |
| 133 | P0 | "still front lanteen and sail isn't fixed" | [x] the lateen rides a seventh of a mast higher (tack/foot/clew) so it no longer cuts the deck, and the yards trim by the bisector rule, so the fore lateen sets to the wind instead of lying across the bow |
| 134 | P0 | "add more elasticity to rope for sails" | [x] half as much sag again, two slow out-of-phase breathing terms, and the running rigging (kind 2) comes up taut under a drawing sail while the standing rigging keeps its sag |
| 135 | P0 | "add texture similar to riggings" (the sails' own ropes) | [x] the foot rope, the sheets, the clew-lines and the lateen's robands were flat cross-quads reading as paper ribbon; they are six-sided lathe-shaded prisms now, like the cordage |
| 136 | P0 | "remove riggings on the stern that lead nowhere" | [x] the stays were picked by array *index*, so every mast stayed to the stem and the foremast ran a stay the whole length of the ship to the taffrail through the other tops. They are picked by *station* now: forward to the next mast ahead or the stem, aft to the next mast astern or (for the aftermost only) the taffrail |

### Carried back after the 0.22.16 round was lost in a commit
| # | Pri | Item | Status |
|---|-----|------|--------|
| 137 | P0 | issue13's video: "texture over ships that is not moving with mesh" | [x] every surface detail sampled in the model's frame (`v_mpos`), not the world's |
| 138 | P0 | issue13: sails trimmed for maximum efficiency | [x] the bisector rule |
| 139 | P1 | issue13: flags like the reference paintings | [x] long tapering swallow-tailed pennants |
| 140 | P1 | issue13/14: turn capability by size | [x] the yaw rate is *taken up* by displacement: 1.7x on a fusta, 0.4x on a great carrack |
| 141 | P1 | issue9's last line, "make fields more visible" | [x] the worked land carries to 340 units (was 220), the paint a quarter stronger, the cropland floor 0.72 (was 0.55) |
| 142 | P1 | todo 105, the lateen canvas' lee side | [x] transmission 0.52 and twice the sky fill |

### Physics - the damage model (P1.3), started this round
| # | Pri | Item | Status |
|---|-----|------|--------|
| 143 | P0 | **(found) No cannon shot had ever hit anything** | [x] the guns fire in world coordinates, the tactical bodies live in the battle's local frame: the broadphase compared a gun at world (1000, -700) against a hull at local (-40, 20) and never matched. Every ball in every battle flew past. `particles_set_origin` takes the shot - and the recoil's lever arm, which had been a kilometre long - into the body's own frame |
| 144 | P0 | (found) The guns were laid *up* from a muzzle above the target's hit box | [x] point-blank is fired level: elevation -1.3 to +0.1 degrees, and the hit box reaches the rail (0.16 L, was 0.11) |
| 145 | P1 | Component / zone damage model | [x] `nphys_damage_t`: hull, rig, guns, flood, sunk, masts_down. Where the ball strikes decides what it breaks - below the waterline she leaks, on the gun deck she loses her battery, aloft her rig |
| 146 | P1 | Hull damage | [x] |
| 147 | P1 | Mast damage | [x] a mast goes by the board at each threshold of rig damage, with an event |
| 148 | P1 | Sail damage | [x] carried by the same rig scalar: a wounded rig costs up to 65% of her speed |
| 149 | P1 | Gun damage | [~] the scalar is kept and raised by hits on the gun deck; the battery does not fire slower for it yet |
| 150 | P1 | Flooding | [x] grows with the holes below her line and with itself, so the end comes quickly |
| 151 | P1 | Sinking state | [x] past full flooding she is a wreck: dead in the water, no helm, no guns, and an event |
| 152 | P1 | Fire | [ ] nothing burns yet |
| 153 | P1 | Tactical results update strategic ship state | [ ] the damage is read back by nothing; the staged exercise has no real fleet behind it |
| 154 | P2 | Broadphase collision | [ ] |
| 155 | P2 | Multithreaded physics jobs | [ ] |

## 0.22.18 (session elm) - the 4x terrain build and the rivers on it

| # | Pri | Item | Status |
|---|-----|------|--------|
| 156 | P0 | Read the new asset in `assets/data-terrain-v2`: the Mare Liberum 4x world (2049 samples a chunk, ~700 m) | [x] the 513-sample ceiling lifted to `GC_MAX_SAMPLES`; the resident budget counts chunks (68 MB each at 4x, against 4.5) rather than a flat 160 MB; reads in flight scale with chunk size |
| 157 | P0 | The 4x chunks on the GPU | [x] the chunk atlas picks its slot grid from the dataset - 3x3 slots of 1025 texels (113 MB) for the 4x build, 4x4 of 513 (50 MB) for the overview - and point-samples finer data down on the shared edges so chunk joins stay seamless. `MARE_CHUNK_ATLAS_SAMPLES=2049` for the full detail |
| 158 | P1 | (found) the geography fold read 17 MB a chunk to throw fifteen sixteenths away: 2.6 s and ~0.5 GB peak against the overview's 0.7 s | [x] it reads every fourth row and column, by seeking over the rest: 1.0 s, identical output. `MARE_GEOCHUNK_DIR` also takes a `;`-separated list now, so an out-of-tree build joins the ones under `assets` |
| 93 | P1 | The ranges read as terraces (the 2.8 km samples) | [x] 0.22.18: the finer DEM was the answer. The Dinaric karst has rock faces and ridges where it had smooth green hills (`shots/terrain_3km.png` against `shots/terrain_4x.png`) |
| 118 | P1 | Carve the great rivers' lower reaches and reopen navigation on the authored data; the half-width from the upstream length is a guess | [x] 0.22.18: the class comes off `from_upstream_m` with thresholds **read off the layer** (150 Mm a merchantman, 60 Mm a shallow draught; the Danube delta measures 338, the Rhine 110, the Po 59). 2,148 nav cells of river open to ships; carving defaults on for authored data and stays off for the generated network |
| 160 | P1 | (found) roads did not bridge the authored rivers | [x] `river_mask` was built only by the *generated* network, so since 0.22.14 a road had been laid over the Po as if it were dry ground. 31,895 cells carry a river for the roads to bridge |
| 159 | P2 | The Seine, the Guadalquivir, the Thames and the Tiber are not navigable | [ ] the first two fall under our 60 Mm threshold, the last two under the export's own 10.9 Mm filter. `from_upstream_m` is length, not discharge, and the layer carries no width (`width_m` is null throughout) - correcting it needs a flow figure the data does not have |
| 161 | P2 | Bridges as props at each `road_bridges[]` point (TODO 10b) | [ ] the points exist over real rivers now, which is what was blocking it |

## issue15 (assets/issues/issue15, 0.22.19) - session elm

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 162 | P0 | "increase sizes of villages assets in strategic view as they are literally dots on map" | [x] a village (settlement size 0) drew at 5 px against a town's 8.5, and at the strategic tier the house meshes are gone, so that diamond *was* the village: 9 px now, and every settlement mark a third larger at the strategic tier |
| 163 | P0 | "add in menu starting options with posibility of changing rendering software and level of graphics details low -> high" | [x] the Options panel opens over the start menu (the backend has to be chosen before a world exists), and a Low/Medium/High/Ultra preset sets the nine knobs at once; moving one reads "Custom". The four buttons and the running renderer are on the menu itself |
| 164 | P0 | "add smoke in battle view to fade slower and be less round, also fade from end to middle" | [x] density `(1-t)^2` instead of `1-t^2` and lives 20-50 s (were 12-30); a low-frequency noise lobe pulls the silhouette out of round; and the visible radius shrinks with age so a puff is eaten from its rim inwards and ends as a small dense knot, instead of keeping its size and going transparent |
| 165 | P0 | "in battle you are not allowed to make and direct changes - add UI with buttons like direct fire to warship / ... tradeships retreat board or fight to death" | [x] two standing orders for the player's side: **Fire at** Nearest/Warships/Traders (by gun count, every time the guns are ready) and **The line** Hold/Board/Retreat/No quarter. Orders override the hand helm and are cleared on entering a new action |
| 166 | P0 | "add real cannonballs ... sizes depending of cannons ... curvature and gravity ... angle of firing based on waves and ship rotation ... air resistance" | [x] calibre per gun (and drawn at it); drag `2.75e-5 v^2/r` - a 32-pounder out-ranges a 6-pounder by 30%, measured; the piece laid on the ship's own roll and pitch, so she shoots where the deck points; a constant sideways drift for the smooth bore's curve; and the ball carries her way off a moving platform |
| 167 | P0 | "ships gliding as in video shown" | [x] map ships carried roll = pitch = 0 and a 2.6-degree sinusoid: they now heel to leeward by the wind's strength times the sine of its angle on her heading (8 degrees on the beam, more in a storm), and the working roll and pitch are doubled |
| 168 | P0 | "add real rivers and traversable and remove blockiness of rivvers add curvatures smooth" | [x] traversable landed in 0.22.18. The blockiness was that the layer is 825k lines of *two points each* and a navigable reach is carved wider than the sticks it follows: the segments are chained by an exact-endpoint walk and Chaikin-smoothed twice - 287,511 wide segments became 1,227 chains - with the width per point, so a river opens out instead of stepping |
| 169 | P0 | Reduce the mountain heights by 1 | [x] `GC_HEIGHT_SCALE` 7 -> 6, exaggeration 4.6x (was 5.4x); the 4x build carries its own relief, so the ranges do not go flat |

## Physics and battle (0.22.19) - the rows this round closed

| # | Pri | Item | Status |
|---|-----|------|--------|
| 153 | P1 | Tactical results update strategic ship state | [x] `ship_t` carries `d_rig`, `d_guns`, `d_flood`; every tactical hull that stands for a real ship writes home, damage taken and never given back, hull points following the tactical damage to a floor of a tenth, and a ship sunk in the scene struck off the fleet. On a 5 s timer and on the way out. `SAVE_VERSION` 6 |
| 152 | P1 | Fire | [x] started by a heavy blow on the gun deck, feeds on itself, burns her rig then her timbers then floods her, costs her way - and can reach the magazine, in which case she goes up at once. 60% of her speed and a mast gone twenty seconds in; lost in a minute or two |
| 149 | P1 | Gun damage with teeth | [x] up to four times slower to fire, fewer pieces bearing, and nothing at all past nine tenths; `fleet_power` reads it, so a wrecked battery does not come back for the next action |
| - | P1 | A sinking that reads | [x] she settles by the head over eight seconds, slow then quick, instead of simply stopping |
| 121b | P1 | A verlet chain for the sheets and braces | [ ] the ropes are a per-class *static* mesh shaped in the vertex shader; a real verlet needs the geometry moved to a per-ship dynamic buffer with the endpoints enumerated per hull. The simulation is cheap, the plumbing is the work - left open rather than faked |
| 154 | P2 | Broadphase collision | [ ] no visible result at the hull counts the staged battle runs today |
| 155 | P2 | Multithreaded physics jobs | [ ] as above |
| 170 | P2 | A yard that repairs what a fleet brings home | [ ] the damage persists now and nothing takes it off again |

## issue16 (assets/issues/issue16, 0.22.20) - session elm

| # | Pri | Item (as requested) | Status |
|---|-----|---------------------|--------|
| 171 | P0 | "make mor realistic fire, you have particles and smoke also" (screenshot: two yellow discs) | [x] a flame particle kind: the fire sheet stretched 2:1 tall, never spinning, tongues born at the deck that climb and die in under a second, white root to red tip, self-lit; embers downwind off a hot fire |
| 172 | P0 | "oars are on fron side wide part needs to be in water and thin part in ship" (third report) | [x] the asset was finally *measured*: the oar is modelled on a -45-degree diagonal with the blade at **+x** (cross-section 0.227 against the loom's 0.090) - every prior fix assumed -x and so mounted the real blade inboard. Side sign flipped, pivot moved to the loom's true end. [~] a close-up verification shot kept missing the moving ship; the fix rests on the measurement |
| 173 | P0 | "add posiblity to retract sails if upwind" | [x] a Take in sails button: the player's side furls; oared ships pull, sailing ships heave to |
| 174 | P0 | "add posibiliti to sunk or to get strayed on beach" | [x] stranding: hard aground (a quarter of her draft out of her depth) she takes ground damage, and past four seconds she is stuck fast - no helm, no way, an event, "(aground)" on her muster card. Sinking has existed since 0.22.17 |
| 175 | P0 | "add damage on collion" | [x] the contact's closing speed above a walking pace costs hull, between wind and water, split by inverse mass - the boat takes the worst of touching a carrack |
| 176 | P0 | "add mesh damage removal dynamically not changing model but actually removing parts of mesh" | [x] a mast that goes by the board is not drawn - with its yards, canvas and lantina, stations falling bow-aft to the physics' count. [ ] the standing rigging still shows its old runs: one per-class mesh, needs the per-ship rig buffer (121b's blocker) |
| 177 | P0 | "add ship icons and list on top ernemy on bottom player and shows stats (hull, water, crew, sails, ammo)" | [x] the muster list: enemy top, player bottom, per ship four bars (hull, sails, guns, water), a flame when burning, "+" lost, "(aground)" stranded. [ ] crew and ammo are not *simulated*, so they are not shown - a bar that never moves is a lie; they belong to the strategic-ship work (178) |
| 178 | P2 | Crew and ammunition as simulated quantities (then their bars) | [ ] |
| 179 | P0 | "villages to small in sizes they need to be same size as cities" (screenshot) | [x] built disc to a small town's footprint, houses a third bigger, drawn to 70 units (was 40) |
| 180 | P0 | issue1.png "circled city with walls and port that looks just bad. you have assets for buildings" | [x] ~90 houses in a size-3 city (was ~40), ring streets with doors to the lane, a tower at every other wall joint. [~] it is a better procedural town, not an authored cityscape - hand-placed landmarks for Venice/Dubrovnik/Constantinople stay on the roadmap (P2.1) |
| 181 | P1 | "connect cities with roads and villages depending where trade is flowing to from hinterlands" | [ ] roads already run settlement -> hub port; weighting by trade flow needs the economy's flows in the road builder - a sim feature, left open rather than faked |
| 121b | P1 | "add rope for then next round if unable this one" | [ ] carried at the issue's own invitation; the blocker is unchanged (the per-ship rope/rig buffer) and the mast removal now wants the same buffer, so it is the next renderer task |

## Order of work (updated 0.22.20)

1. Items 1-8 and 27: the battle scene's clock, wind, coast and landscape from the world. **Done 0.22.7.**
2. Items 10-13: the particle system, wind-blown smoke, muzzle flash. **Done 0.22.7.**
3. Items 22-23: lowland coasts and coastal height from the data. **Done 0.22.8.**
4. Items 20, 33: mast proportions. **Done 0.22.8.** Item 21 (the dark used suit) is open.
5. Items 24-25: strategic zoom floor, denser pines. **Done 0.22.8.** Item 26's far LOD for the fields is open.
6. issue5 items 28-32. **Done 0.22.8** (31 guarded, not reproduced).
7. issue6 items 35-43 and P1.3 items 1-6. **Done 0.22.9.**
8. issue7 items 44-51 and the trailer. **Done 0.22.10.**
9. issue8 items 52-62, todo 15/17/19/21/26, physics buoyancy + ballistics. **Done 0.22.11.**
10. issue9 items 63-76. **Done 0.22.12.** (The body attitude into the shader came with it.)
11. issue10 items 77-90, 92, 98. **Done 0.22.13.** Open from it: 91 (rivers), 93-97.
12. issue11/12 items 99-114. **Done 0.22.14** (105 partly). Rows 91 and 94 closed with it; 95 (volumetric smoke) and 115 (rope physics) are the open issue12 items.
13. issue12's rest, rows 119-129. **Done 0.22.15.** Rows 95, 115 and half of 117 closed with it.
14. issue14 + the lost 0.22.16 work, rows 130-142. **Done 0.22.17** (session elm).
15. The damage model (P1.3), rows 143-151. **Done 0.22.17** - and with it the two bugs that had kept every battle bloodless.
16. The 4x terrain build and its rivers, rows 156-158, 160, and with them 93 and 118. **Done 0.22.18** (session elm).
17. issue15 and the battle rows, 162-169 with 149, 152, 153. **Done 0.22.19** (session elm).
18. issue16, rows 171-180. **Done 0.22.20** (session elm), 181 and 121b carried.
19. Next, in this order: 121b (the per-ship rope/rig buffer - it now unblocks *two* things, the
    verlet rope and cutting a felled mast's rigging), 170 (a yard that repairs), 178 (crew and
    ammunition, so the muster list can show them honestly), 181 (roads by trade flow), 161
    (bridge props), 117b (a fruit-tree set). `docs/TODO_STATUS.md` is the whole list as
    checkboxes:
   1. **The damage model** (P1.3): the hits exist and carry their momentum; give each hull zones (hull, masts, sails, guns) with hit points, let a hit on a mast bring it down and a hit on canvas tear it (the `torn` sail state), and write the tactical result back to the strategic ship. This is the biggest single piece left and everything below hangs on it.
   2. **Flooding, fire and sinking** (P1.3 + issue4 items 14, 16, 18): a holed hull takes water and settles (the buoyancy knows the waterline), a fire from a hit spreads and smokes through the particle system, a sunk ship goes down by the head or the stern and leaves debris.
   3. **Heave into the shader**: the body's heave is computed but the hull still rides the shader's own wave sample; with the attitude channel in place the heave is one more float.
   4. **True sternway** (item 9): a sail force by point of sail instead of a speed command, now that the body can be pushed backwards by a head wind.
   5. **A settlement generator proper** (roadmap P2.1): the hinterland reads now; the next step is street layouts and the authored house assets in the villages, and hand-placed landmarks in Venice, Dubrovnik and Constantinople.
   6. **Broadphase and physics jobs** once the staged battle has more than ~60 hulls; **lightning** in a storm.
