# TODO (session of 2026-10-30 afternoon, worktree `oak`)

- [x] **Real terrain wired in**: `assets/data/export-global` (GEBCO, 378 chunks of 513x513
      float32 metres - see `assets/data/how_to.txt`) cut by the new
      `tools/terrain_from_gebco.py` into the streamed tile set (40 samples/deg over the
      playable window, land linear to 9000 m, sea sqrt-compressed to 12000 m so shelves stay
      in the band the shallow shading expects). Installed at `assets/elm/terrain/tiles`; the
      AI-map cut is kept in `tiles-aimap`, `MARE_TERRAIN_DIR` switches cuts for A/B. This
      closes the "a real DEM" item below; the *coastline* still comes from data/geo_coast.h
      (GEBCO is ~3 km/sample - too coarse for the Adriatic islands; MARE_TERRAIN_ISLANDS=1
      remains the experiment).
- [x] **Sea floor darkens exponentially** past the shelf (sea.frag): basins and trenches now
      read against the shelf edge instead of saturating at the old `deep` colour.
- [x] **Ship wood material**: grain-driven plank albedo + strake streaks + gloss on rough
      brown surfaces, and a wet, darker, glossier band above the waterline on floating
      instances (meshes.vert/.frag).
- [x] **New mast library** (assets/ships/new_masts, explanation.txt): per-type assemblies
      (mast_cog_single/_two, mast_carrack, mast_great_carrack, mast_mizzen, mast_lateen_tall,
      bowsprit) imported via tools/blender_prepare_part.py (part-segmented GLBs: lateen yards
      excluded from their masts); classes compose their stations from the library with
      fallback to the old split parts. The **lantina swings to the wind**: `lateen_yard` is
      its own asset, instanced with the (shared unit) lateen canvas on the mast pivot,
      trimmed off the apparent wind with a working oscillation (shipyard.c).
- [x] **Lantina visible again at every distance**: the authored lateen_yard / square_yard
      assets lost their far LODs to decimation (a thin diagonal spar decimates to a sliver),
      so the yards vanished beyond ~30 px. The spars are now *part of the unit sail meshes*
      (rig_sail_lateen / rig_sail_square build their own bar, sized to the sail), so yard and
      canvas swing to the wind as one mesh and exist at every LOD; the spar-asset instancing
      is retired (the assets stay on disk, unused). Canvas-to-yard alignment is now exact by
      construction.

## Leftover tasks

- [ ] Bowsprit steeve: the authored spar is horizontal; bake a ~18 degree tilt about the heel
      in blender_prepare_part.py (instances have yaw only) and re-export.
- [ ] Far-LOD budgets for thin-spar *masts* (the "LOD extent differs" warnings): raise the
      LOD4/5 triangle budgets for the mast library or keep LOD3 longer in their descriptors.
- [ ] A floating yard/bowsprit was once seen at water level beside a wake in a staged battle:
      likely a part instanced while its hull was still streaming (shipyard_push draws parts
      only after the hull resolves, so probably the occlusion-query partition) - reproduce
      and pin down.
- [ ] The asset loader fits a `forward: +z` part *after* rotation, so square_yard reported
      size 0.9x1.17x16.7 with scale 17.96 (harmless while shipyard rescaled by bbox; the
      asset is unused now, but the fit-vs-rotation order in asset.cc deserves a look).
- [ ] Retire or repurpose the unused spar assets (lateen_yard, square_yard) and the parallel
      session's whole-assembly imports (carrack_mast, mizzen_mast, ...) from the manifest.
- [ ] Rewrite plan T4/T8: the abstract rhi::Device interface over GlDevice, then the Vulkan
      backend (docs/REWRITE_PLAN.md); T9 folder sweep (physical src/ tree per namespace) and
      the RmlUi decision (T7) after it.
- [ ] Deepen the conversion: the classes still reach the shared C globals (G, RF, cam);
      migrating callers onto the class APIs and injecting game_t is the next honest step.
- [ ] Session coordination: oak/tmp keeps flipping between a symlink to C:\projects\mare\tmp
      and a local directory (two sessions, one worktree); builds now address
      /mnt/c/projects/mare/tmp/elm-cpp explicitly, but the flip-flop should stop.
- [ ] A parallel session imported the same sources whole (carrack_mast, mizzen_mast, ... with
      .meshy.json markers) into the shared assets/elm; both sets coexist in the manifest.

# TODO (sessions of 2026-10-29, worktree `elm` — branded `oak` in the first session)

Reference art for the rigs: `assets/fleet_sails.png` (the whole fleet, one ship per class) and
`assets/fleet_sails_2.png` (war carrack and great carrack: four masts, spritsail, lateen
mizzens; ignore the hull distortion). Both live in `C:\projects\mare\assets`.

## Part 1 — done (0.15.0)

- [x] Masts back (split-part descriptors regenerated into the manifest).
- [x] Rig layout per class from the reference sheet (`gfx/shipyard.c` class table), four-mast
      war/great carrack, spritsails, drawn yards.
- [x] Cloth that answers the wind (`GLSL_CLOTH` in `gfx/meshes.c`): belly from wind pressure,
      bent to the yard, held at the foot, luffing, aback against the mast; flags stream.
- [x] Per-class ship stats (`ship_stats_t`, `shipyard_class_stats()`); the staged battle sails
      at class speed and turn rate.
- [x] Ship bow waves and foam only where the ship is going (`gfx/sea.c`, `MARE_SHIP_WAVES=0`).

## Part 2 — done (0.16.0)

- [x] Photos and tests: battle, city, Adriatic/Aegean/strategic terrain, oars, formations —
      `C:\projects\mare\tmp\elm-fin\shots`; keepers in `docs/screenshots/` (islands-aegean,
      islands-dalmatia, battle-lines). All five ctest suites green (sim_test caught — and
      drove the fix for — islands thinner than the nav grid).
- [x] The dark lattice on the staged sea: isolated to SSAO reading the tessellated sea's
      patch-border derivatives (A/B matrix MARE_SSAO / MARE_SEA_TESS / --no-shadows; edge-step
      metric 46.8k → 4.2k). Fixed by taking the AO pass from the opaque depth before the water
      draws; the surviving diagonal streaks are the rigs' real sun shadows.
- [x] Buoyancy: displacement-derived wave response + roll + deeper waterline per class
      (`shipyard.c`), inherited by masts, rig and oars.
- [x] Battle formations: line ahead on mirrored stadium circuits, station keeping, turning in
      succession, inner legs held a cable apart for passing broadsides (`detailview.c`).
- [x] Oars: side offset reduced, sweep-rate stroke, blade dip on the pull, galley stats eased.
- [x] Strategic waves: bigger in open sea (zoom amp floor 0.14–0.26), damped near every coast
      by a zoom-driven depth gate (`u_shallow`).
- [x] Every island of the Adriatic and the Greek seas: 25 authored island polygons in
      `data/geo_coast.h` (plus a fuller Crete), each ≥2 cells so navigation respects it;
      small land components lifted clear of the water (`raise_small_islands`). The 32768
      height map is folded in for relief and bathymetry only (default on,
      `MARE_TERRAIN_GEO=0` reverts) — it proved to be a loose AI Earth whose own Adriatic is
      half filled in, so its *coastline* stays an experiment (`MARE_TERRAIN_ISLANDS=1`).
- [x] Per-worktree assets: `<root>/assets/elm` and `C:\projects\mare\assets\elm` carry the
      complete generated set (ships, mast parts, oar, buildings, all six LODs, descriptors,
      manifest, sea textures, terrain maps + tiles). The asset root resolves to `assets/elm`
      before `game/assets`; terrain tiles resolve per-worktree first. Builds in `tmp/elm-fin`.

## Part 3 — done (0.17.0)

- [x] World 2x again (WORLD_SCALE 4) with every gameplay distance expressed in the scale; base
      grid 2x (16 cells/degree) with nav/territory/stream geography unchanged.
- [x] Regions: cap 250, 3.2-degree coast segments in the Mediterranean home seas, Istria /
      Dalmatia / Ragusa and 25 more Mediterranean name pins.
- [x] Relief: ridged domain-warped mountains, hills everywhere inland, cliff coasts, a shelf;
      ridged streamed chunk detail; authored tiles add relief only where higher.
- [x] Textured ground from the material atlas (triplanar rock cliffs, scrub, snow, deserts),
      wet rock at the waterline, darker sea floor, warm turquoise shallows.
- [x] Found and fixed the vanished sea (AO pass left its framebuffer bound after the 0.16
      reorder) and three reversed-edge smoothsteps.

## Still open

- [ ] **A real DEM and coastline** (the one thing the terrain still needs): a 16-bit GEBCO /
      ETOPO equirectangular export plus a Natural Earth land-mask raster, dropped into
      `assets/elm/terrain`. The cutter, calibration, bake and rings are all in place; the AI
      map only ever supplies relief. See DESIGN_TERRAIN.md "What would make it real".
- [ ] Mountains are still modest against the reference art; the next lever is erosion (thermal
      / hydraulic passes on the ridged field) and a snow/rock crest treatment in the shader.
- [ ] `MARE_PASSES=sea` with `MARE_SEA_DEBUG=1` dies on an incomplete render target (the
      refraction snapshot with no opaque pass before it) - a debug-only path.
- [ ] The Great Carrack hull is still the war carrack's model stretched ×1.19: generating a
      true 56 m hull (and a four-mast rig to split) needs a new Meshy generation — external.
      The bonaventure reuses the mizzen part deliberately: k-means-splitting the three-mast
      scan into four would cut the mainmast in half.
- [ ] The overhead cloud layer shows hard polygon edges in the staged battle's top-down views
      (visible in battle-lines.png); gunsmoke puffs read faceted at very close range.
- [ ] Normal-map tiles are cut and loadable (`terraintiles_normal`) but nothing consumes them:
      ring lighting differentiates the composed height delta instead. Wire them into the rings
      or drop the cut to save 300 MB.
- [ ] The shared `assets/terrain/tiles` folder is contested between sessions (its height.json
      was overwritten mid-session by the other worktree); everything here now reads the
      per-worktree copy, but the shared folder should eventually be retired.
