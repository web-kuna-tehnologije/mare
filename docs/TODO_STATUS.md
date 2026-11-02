# Where we stand — the whole list, one line each

`[x]` done · `[~]` partly done, the rest named · `[ ]` open · `[-]` dropped, with the reason.

Updated **2026-11-02, 0.22.20**, session `elm`. The numbers in brackets are the row in
`docs/todo_list.md` or the item in `docs/TODO.md`, so every line here can be traced back to the
request it came from. Nothing is listed here that is not tracked there.

**184 done · 9 partly · 28 open · 2 dropped, of 223 tracked — about 83% done.** The battle is
now a commanded, visible, costly thing: orders, a muster list, fire with real flames, masts
that fall off the mesh, collisions and groundings that cost timber. The clear next renderer
task is the **per-ship rope/rig buffer** (121b), which now unblocks two separate asks; the
clear next sim tasks are a yard that repairs, and crew/ammunition.

---

## This round (0.22.20) — issue16

- [x] Realistic fire: flame tongues off the fire sheet (2:1 tall, climbing, white root to red
      tip, self-lit), embers downwind; the explosion uses them too [171]
- [~] Oars, third report — the asset finally *measured*: blade at **+x** on a −45° diagonal
      (cross-section 0.227 vs the loom's 0.090); every prior fix assumed −x and mounted the
      real blade inboard. Side sign and pivot fixed. A close-up verification shot kept missing
      the moving ship, so this rests on the measurement [172]
- [x] Take in sails: a battle-panel button; oared ships pull, sailing ships heave to [173]
- [x] Stranding: hard aground takes ground damage and after a few seconds she is stuck fast —
      no helm, no way, "(aground)" on her card [174]
- [x] Collision damage: closing speed above a walking pace costs hull, split by inverse mass [175]
- [x] Mesh damage removal: a felled mast is gone from the drawn ship, with its yards, canvas
      and lantina; the standing rigging still shows its runs (121b's buffer is the blocker) [176]
- [x] The muster list: enemy top, player bottom, four honest bars per ship (hull, sails, guns,
      water), flame / "+" / "(aground)" marks; crew and ammo not shown because not simulated [177]
- [ ] Crew and ammunition as simulated quantities, then their bars [178]
- [x] Villages at a town's footprint, bigger houses, drawn to 70 units [179]
- [~] Cities that look like cities: ~90 houses on ring streets inside towered walls — a better
      procedural town; authored landmarks for the great cities stay on the roadmap [180]
- [ ] Roads weighted by trade flow — needs the economy's flows in the road builder [181]
- [ ] The rope, carried at the issue's own invitation ("add rope for the next round if unable
      this one") — and its buffer now also unblocks cutting a felled mast's rigging [121b]

## Last round (0.22.19) — issue15, and the battle grows teeth

### issue15
- [x] Villages bigger in the strategic view: 9 px, and every settlement mark a third larger at
      the strategic tier, where the house meshes are gone and the mark *is* the village [162]
- [x] Start-menu graphics: the Options panel opens over the menu (the backend must be chosen
      before a world exists) and a Low/Medium/High/Ultra preset sets the nine knobs at once [163]
- [x] Smoke fades slower (density `(1-t)^2`, lives 20-50 s), is less round (a low-frequency
      noise lobe), and fades from its rim inwards to a dense knot instead of going transparent
      at full size [164]
- [x] Battle orders: **Fire at** Nearest / Warships / Traders, and **The line** Hold / Board /
      Retreat / No quarter [165]
- [x] Real shot: calibre per gun, air resistance (`2.75e-5 v²/r` — a 32-pounder out-ranges a
      6-pounder by 30%, measured), the piece laid on the rolling deck, a smooth bore's drift,
      and the ball carrying the ship's way [166]
- [x] Ships no longer glide on the map: they heel to leeward with the wind and work twice as
      hard in a seaway [167]
- [x] Rivers smoothed: 287,511 wide segments chained and Chaikin'd into 1,227 curves; the
      blockiness was 825k two-point sticks carved wider than they were long [168]
- [x] Mountain heights down by 1 (`GC_HEIGHT_SCALE` 7 → 6, exaggeration 4.6x) [169]

### Physics and battle
- [x] **The butcher's bill goes home** — the biggest item on the list, open since the damage
      model was written. `ship_t` carries rig, gun and flood damage; a ship sunk in the scene
      is struck off the fleet; damage is taken and never given back [153]
- [x] **Fire**: started on the gun deck, feeds on itself, takes her rig then her timbers then
      floods her, and can reach the magazine [152]
- [x] **Gun damage with teeth**: up to four times slower, fewer pieces bearing, nothing past
      nine tenths — and `fleet_power` reads it [149]
- [x] **A sinking that reads**: down by the head over eight seconds, slow then quick
- [ ] **A verlet chain for the sheets and braces** — the one item of this round left open. The
      ropes are a per-class *static* mesh shaped in the vertex shader; a real verlet needs the
      geometry on a per-ship dynamic buffer with the endpoints enumerated per hull. The
      simulation is cheap; the plumbing is a new subsystem [121b]
- [ ] Broadphase collision [154] and multithreaded physics jobs [155] — no visible result at
      the hull counts the staged battle runs today
- [ ] A yard that repairs what a fleet brings home: the damage persists now, and nothing takes
      it off again [170]

## Last round (0.22.18) — the 4x world and its rivers

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
- [ ] A verlet chain for the sheets and braces [row 121b] - needs the rope geometry off the
      per-class static mesh first
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
- [x] Gun damage: up to four times slower, fewer pieces bearing, nothing past nine tenths [row 149]
- [~] Strategic state holds hull, crew, guns, rig and flooding (0.22.19); fire is not carried
      home - it either took her or it did not, and a strategic fire needs a strategic fire model [P1.3]
- [x] **Fire**: she burns, loses her rig and her way, and can take the magazine with her [row 152]
- [x] **Tactical results update the strategic ship** (0.22.19): rig, guns and flooding come home
      and a ship sunk in the scene is struck off the fleet [row 153]
- [x] A sinking that reads: she settles by the head over eight seconds (0.22.19)
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

1. **The per-ship rope/rig buffer** [121b]. It now unblocks two things at once: the verlet
   sheets and braces, and cutting away a felled mast's shrouds and stays (the mast itself
   already goes). This is the renderer work the last three rounds have been circling.
2. **Crew and ammunition** [178]: the muster list has two honest gaps where they belong, and
   the damage model has nowhere to send casualties.
3. **A yard that repairs** [170]: the damage persists and nothing takes it off.
4. **Roads by trade flow** [181], **bridge props** [161], **a fruit-tree set** [117b].
