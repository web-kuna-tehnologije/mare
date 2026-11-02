# Where we stand — the whole list, one line each

`[x]` done · `[~]` partly done, the rest named · `[ ]` open · `[-]` dropped, with the reason.

Updated **2026-11-02, 0.22.18**, session `elm`. The numbers in brackets are the row in
`docs/todo_list.md` or the item in `docs/TODO.md`, so every line here can be traced back to the
request it came from. Nothing is listed here that is not tracked there.

**162 done · 9 partly · 29 open · 2 dropped, of 202 tracked — about 82% done**, and the balance
has shifted: what is left is simulation (fire, the butcher's bill, the campaign loop) and
authored art, not renderer or world work.

---

## This round (0.22.18) — the 4x world and its rivers

- [x] Read the Mare Liberum 4x terrain build (`assets/data-terrain-v2`, 2049 samples a chunk,
      ~700 m a sample) — the loader took 513 samples as a hard ceiling [TODO 1, row 156]
- [x] Size the streaming budget in *chunks* rather than megabytes: a 4x chunk is 68 MB resident
      where the overview's is 4.5, and the flat 160 MB held two of them [row 156]
- [x] Scale the reads in flight by chunk size, so a 30 MB chunk does not stall the IO lane [row 156]
- [x] The GPU chunk atlas picks its slot grid from the dataset: 3x3 slots of 1025 texels for the
      4x build (113 MB) against 4x4 of 513 (50 MB), finer data point-sampled down on the shared
      edges so chunk joins stay seamless; `MARE_CHUNK_ATLAS_SAMPLES=2049` for the full detail [row 157]
- [x] The geography fold reads the finer build at a stride — it was reading 17 MB a chunk to
      throw fifteen sixteenths away (2.6 s and ~0.5 GB peak; now 1.0 s) [row 158]
- [x] `MARE_GEOCHUNK_DIR` takes a `;`-separated list, so an out-of-tree build joins the
      deliveries under `assets` instead of replacing them [row 158]
- [x] **The ranges read as terraces** at the old 2.8 km spacing — the finer DEM was the fix all
      along, and the Dinaric karst now has rock faces and ridges instead of smooth green hills
      [row 93, roadmap "a genuinely finer DEM for the home seas"]
- [x] **River carving and navigation on the authored data**: the class comes off the layer's own
      `from_upstream_m`, thresholds read off the data (150 Mm a merchantman, 60 Mm a shallow
      draught) rather than guessed; 2148 nav cells of river open to ships [row 118, TODO 5]
- [x] **Roads bridge the authored rivers.** `river_mask` was only ever built by the *generated*
      network, so since 0.22.14 a road had been laid over the Po as if it were dry ground [found]
- [x] Carving defaults on for authored rivers and stays off for the generated network — the
      reason it was off was that a drainage network folded from 7 km cells is not a survey
      [row 118]
- [ ] The Seine, the Guadalquivir, the Thames and the Tiber are not navigable: the first two
      fall under our threshold, the last two under the export's own filter. `from_upstream_m` is
      length, not discharge, and the layer carries no width (`width_m` is null throughout) [row 159]
- [ ] Deltas and lakes as basins from the authored layer [TODO 5]

## Terrain and the world

- [x] Terrain-v2 chunks in the world, streamed on the IO/DECODE/MAIN lanes [TODO 1]
- [x] Biome material masks from the export's splat and class maps [TODO 2]
- [x] Natural coasts: shore mask, beaches, cliffs, marsh, the sea breaking on them [TODO 3]
- [x] Map view modes: depth, religion, political, influence, production [TODO 4]
- [x] Rivers from the package, painted, carved and navigable [TODO 5, rows 91/113/118]
- [x] Roads from every inland settlement to its hub port [TODO 6]
- [ ] Hinterland settlements: street layouts, authored houses, hand-placed landmarks in Venice,
      Dubrovnik and Constantinople [TODO 7, roadmap P2.1]
- [ ] LOD streaming for scatter: instanced LOD chains paged with the ring [TODO 8]
- [ ] Procedural micro-detail on the near ground: furrows, stone walls, paths [TODO 9, 9a]
- [ ] Props: the `assets/buildings/city` blends exported (windmill, bakery, well) [TODO 10]

### The hinterland's small items
- [ ] 8a. Scatter framework: one deterministic hash per ring texel, 3 LODs, paged with the ring
- [~] 8b. Trees: the orchards and olive groves have real trees; wild woodland is still canopy
      texture, not instances
- [ ] 8c. Rocks: boulders on canyon rims and sea cliffs, pebble cards on scree
- [x] 7a. Fields as hedged strip parcels, one orientation per district
- [x] 7b. Vineyards and orchards as vine rows and crown grids, with props
- [~] 7c. Villages: the built disc is painted; the asset houses, the mill and the well are not
      all stood yet
- [ ] 10b. Bridges: a stone-arch or timber-beam prop at each `road_bridges[]` point — and the
      points exist over the authored rivers now, which is what was blocking this
- [ ] 6b. Road upgrade visuals: class 1 paler and wider with a ditch, class 2 cobbled with
      milestones
- [ ] Align the field district to the local road; wheat by the region's dominant good [7a]
- [ ] Far LOD for the fields [row 26]
- [ ] A beach profile for the islands `raise_small_islands` lifts: they still read as plateaux

## Ships, rig and sea

- [x] The battle scene's clock, wind, coast and landscape from the world [rows 1-8, 27]
- [x] Muzzle flash, gunsmoke, no projectile VFX [rows 10-13]
- [x] Volumetric smoke: a ray-marched density slab, not billboards [rows 95, 119]
- [x] Wake as a 3D wave rooted at the stem; foam that fades and spreads [rows 86, 87, 100, 129]
- [x] Oars: blade in the water, pivot at the rowlock, ±15° at a stroke every 3-5 s [row 131]
- [x] The lateen: its own spar, the right height, trimmed by the bisector rule [rows 99, 133]
- [x] Stays chosen by station, so none runs the length of the ship to nowhere [row 136]
- [x] Rope as hemp prisms with catenary sag, running rigging taut under a drawing sail [rows 121, 134, 135]
- [x] Q and E steer the player's line in battle; H returns her to it [row 130]
- [x] Authored trees: 27 props, six LODs, stood by biome; far ring to 14 units [rows 112, 128]
- [~] The lateen's lee side: transmission and sky fill help, but a proper back-face material for
      the canvas is still open [row 105]
- [~] True sternway: leeway and falling off exist, and she makes sternway in the eye of the wind;
      a sail force by point of sail on the rigid body does not [row 9]
- [ ] A verlet chain for the sheets and braces, if the look is wanted closer [row 121b]
- [ ] A fruit-tree set: the orchards use the forest species scaled down [rows 117b, 96]
- [ ] The dark used suit of sails [row 21]
- [-] The guns through the hull's *painted* ports — superseded: the hulls are to be re-made
      without guns and the parts stood then [rows 97, 116]

## Physics and battle

- [x] Buoyancy, ballistics, recoil as the ball's real momentum [rows 52-62]
- [x] **The guns had never hit anything**: world coordinates against a local frame, and the
      pieces laid up from above the target. 0 hits in two minutes became 440 [rows 143, 144]
- [x] The damage model: hull, rig, guns, flood, sunk, masts down, by where the ball strikes [rows 145-148, 150, 151]
- [x] Flooding grows with the holes and with itself; past full flooding she is a wreck [rows 150, 151]
- [~] Gun damage: the scalar is kept and raised, but the battery does not fire slower for it [row 149]
- [~] Strategic state holds hull, crew and guns; flooding, fire and sinking are not in it [P1.3]
- [ ] **Fire**: nothing burns yet [row 152]
- [ ] **Tactical results update the strategic ship** — the damage is read by nothing, which makes
      the whole damage model scenery [row 153]
- [ ] A sinking that reads: settle by the head over several seconds, not simply stop
- [ ] Broadphase collision [row 154]
- [ ] Multithreaded physics jobs [row 155]
- [ ] Constraints where gameplay-relevant; mast and debris constraints [P2]
- [ ] Towing and boarding constraints [P3]
- [ ] Heave into the shader: computed, but the hull still rides the shader's own wave sample
- [ ] Lightning in a storm

## Engine

- [x] The export read as chunks on the job pool; the GPU chunk atlas; the clipmap rings
- [x] Vulkan memory, command, queue and fence layers; DX12 device at feature level 12.2
- [ ] One real pass end to end on Vulkan: GLSL to SPIR-V as a build step, a pipeline from
      `rhi_pipeline_desc_t`, uniforms in a descriptor set, rendered offscreen beside the GL frame
- [ ] Finish T9: callers onto the class APIs, `game_t` injected, the C-ABI globals retired
- [ ] The UI decision: RmlUi against the classed-up kit
- [ ] Mast far-LOD budgets; the bowsprit steeve

---

## What to take next, in order

1. **The butcher's bill goes home** [row 153]. The damage model is complete and nothing reads
   it. Write the tactical result back into the strategic ship — hull, crew, guns, sails, sunk —
   so a battle costs a fleet something that outlives the scene. Still the highest-value item on
   the list; everything in the campaign loop hangs off it.
2. **Fire** [row 152]. A hit on a ship already burning or carrying powder starts a fire that
   spreads, smokes through the volumetric system already in place, and ends in flooding or an
   explosion.
3. **Bridges as props** [10b]. The road network now knows where it crosses a real river — 31,895
   land-mask cells of it — so the stone arch and the timber beam are an afternoon's work with a
   visible return.
4. **Gun damage with teeth** [row 149]: the battery scalar exists; let it slow the broadside.
5. **A fruit-tree set** [row 117b] and the authored village props [7c] — the hinterland reads
   well now and these are what is thin in it.
