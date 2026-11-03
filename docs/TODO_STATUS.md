# Mare Liberum — DONE and LEFT

Two lists, as issue19 asked: everything finished, and everything still open. Numbers in
brackets are the row in `docs/todo_list.md` (or the item in `docs/TODO.md`), so every line
here traces back to the request it came from.

Updated **2026-11-03, 0.22.25**, session `elm`.

**213 done · 9 partly · 27 open · 4 dropped, of 253 tracked — about 84%.** (The count of
open items went *up*: issue20 and the request with it added twelve new rows, listed first
below.)

---

# LEFT

Short list, honestly ordered. This is what a next session should pick up.

## issue20 and the 0.22.25 request — the next session's list, in order
- [ ] **The 22 asset LOD-extent warnings** — `mast_carrack`, `mast_mizzen`, `bowsprit`, the
      yards: "LOD n extent differs from LOD 0 — wrong node?". This is the *same class of fault*
      the oars turned out to be (a part not being what the code believes it is), so it is first [206]
- [ ] Rivers are missing again; make them better; **3D/depth channels even where not
      traversable**, and more traversable reaches [209]
- [ ] The map: the Po valley and the lowlands read as sand and should not [210]
- [ ] Smoke: much more of it, untextured particle smoke, massive particle counts, better
      cannon smoke, all of it denser and slower to clear [208]
- [ ] Ships fire only when they have a line of fire [214]
- [ ] Per-ship sail quality shown by texture in battle [213]
- [ ] Hinterland villages as ordinary small settlements, **not selectable** [211]
- [ ] Bridges across rivers are too large — reduce [212]
- [ ] Lateen behaviour with the wind, especially angled sails running downwind [207]
- [ ] Build warnings: C4005 `_CRT_SECURE_NO_WARNINGS`, cimgui C5287 [205]

## Renderer / art
- [ ] **Per-rope verlet geometry** — 0.22.23 gave every ship a real four-node verlet chain that
      her whole rig hangs from (below). What is *not* done is a chain **per rope**: that needs
      the rig moved off the single per-class mesh onto per-ship dynamic geometry. Same buffer
      would let a felled mast take its own shrouds and stays with it (today the whole rig is
      dropped only when the last mast goes) [121b]
- [ ] Authored orchard/vine props, the mill and the well as assets [96, 7c]
- [ ] Road upgrade visuals: class 1 paler and wider with a ditch, class 2 cobbled, milestones [6b]
- [ ] Rocks on canyon rims and sea cliffs; pebble cards on scree [8c]
- [ ] Micro-detail: stone walls, footpaths, rutted tracks [9a, TODO 9]
- [ ] A beach profile for the islands `raise_small_islands` lifts — they still read as plateaux
- [ ] A proper back-face material for the lateen's lee side [105]
- [ ] The dark used suit of sails [21]
- [ ] Far LOD for the fields [26]
- [ ] Scatter framework proper: one deterministic hash per ring texel, 3 LODs, paged [8a]
- [ ] Wild woodland as instances rather than canopy texture [8b]

## World / simulation
- [ ] Hinterland settlements: street layouts, authored houses, hand-placed landmarks in Venice,
      Dubrovnik, Constantinople [TODO 7, roadmap P2.1]
- [ ] Deltas and lakes as basins from the authored river layer [TODO 5]
- [ ] The Seine, Guadalquivir, Thames and Tiber are not navigable — the first two fall under our
      threshold, the last two under the export's own filter; `from_upstream_m` is length, not
      discharge [159]
- [ ] **Pack the rivers in the delivered terrain** — one command, and it needs write access to
      `assets/`, which a session does not have:
      `python3 tools/rivers_pack.py assets/data-terrain-v2/mare-liberum-world-terrain-4x/mare-world-terrain 10.9e6` [182b]

## Battle / physics
- [ ] True sternway: a sail force by point of sail on the rigid body [9]
- [ ] Constraints where gameplay-relevant; mast and debris constraints [P2]
- [ ] Towing and boarding constraints [P3]
- [ ] Heave into the shader — computed, but the hull still rides the shader's own wave sample
- [ ] Lightning in a storm

## Engine
- [ ] One real pass end to end on Vulkan: GLSL→SPIR-V as a build step, a pipeline from
      `rhi_pipeline_desc_t`, uniforms in a descriptor set, rendered offscreen beside the GL frame
- [ ] Finish T9: callers onto the class APIs, `game_t` injected, the C-ABI globals retired
- [ ] The UI decision: RmlUi against the classed-up kit
- [ ] Mast far-LOD budgets; the bowsprit steeve

## Dropped, with the reason
- [-] **Broadphase collision [154]** and **multithreaded physics jobs [155]** — measured in
      0.22.23 before writing either: the whole tactical world costs **0.05 ms a frame with 40
      bodies and 39 shot in the air**. Optimising it would be measuring noise. Reopen if the
      hull count ever goes past a few hundred.
- [-] The guns through the hull's *painted* ports [97, 116] — superseded: the hulls are to be
      re-made without guns and the parts stood then.

---

# DONE

## 0.22.25 — the oars, root cause
- [x] **The oars, finally understood.** The loader re-fits every part: `galley_oar` loads at
      0.26 units, not the raw GLB's 2.583 — so 0.22.21's sizing drew each sweep at 0.43 units
      on an 11-unit hull (invisible), and the probe I added measured the *raw* model, so it
      reported a 4.2-unit oar that was never drawn. Four rounds of "verified" numbers were
      fiction. Restored the pre-claim mount; probe rebuilt on the loaded bounds: **oar 21% of
      the hull, blade 2.72 outboard and in the water, loom 0.77 inboard over the deck** [203]
- [x] `MARE_DV_MINDIST` relaxes the staged camera's 6-unit floor — the other reason four rounds
      of screenshots settled nothing [204]

## 0.22.24 — the oars put back, and three rows off the list
- [x] **Oars restored** to the geometry verified in 0.22.21. The 0.22.23 rowing cycle feathered
      the blade by moving `fx2` — and the rowlock mount solves the oar's *placement* from
      `fx2`, so the whole bank shifted and lifted as the stroke ran. The numeric check agrees
      exactly again (blade 4.20 outboard of a 1.25 rail, 0.33 under water) [202]
- [x] **Stone bridges** where roads cross rivers — abutments, an eight-voussoir arch, deck and
      parapets, laid along the road and scaled to the span. 16 in one ring, of 218 crossings [161]
- [x] **Roads graded by trade** — the old test read `traded_today`, which is zeroed every day,
      so every road was a track (195/0/0). Now graded by `region.development` and the smaller
      of the two towns a link joins: **183 tracks, 4 roads, 8 highways** [181]
- [x] **A fruit-tree set** — apple, cherry and plum, pruned shapes, one per grove cell, in
      place of forest oak and maple scaled down [117b]

## 0.22.23 — issue19
- [x] **Sea foam was erasing the wake** — every source of white was summed then clamped, so once
      the whitecaps reached 1.0 the water was solid foam and the wake, the colour, the
      reflection and the refraction all vanished inside it. The sources now combine by *screen*
      so nothing clips, and a ship's wash **damps the chop it runs through**, which is what a
      wake physically does — so the trail reads as its own thing
- [x] **A mast now falls.** It heels over the side, quickening as a falling spar does, drops as
      its heel tears out of the step and is gone when it is in the water (2.6 s). Its canvas
      goes over with it, collapsing as the spar swings down, instead of blinking out
- [x] **Visible battle damage on the planking** — shot scars where balls struck, pale splintered
      wood around them, and smoke-blacking low on the topsides, spreading as she takes more
- [x] A ship with every mast gone no longer trails a full suit of standing rigging
- [x] **Oars row.** A real stroke: a short hard drive with the blade buried, a longer feathered
      recovery clear of the water. The asymmetry is what reads as rowing
- [x] **Fire is licks, not streams** — three lobes at different widths fanning apart as they
      rise, each on its own noise phase, with a finer noise eating the top so the flame breaks
      into separate licks instead of tapering to a ribbon. The root is held solid and bright
- [x] **Villages are the size of towns on the map** — one mark size for every settlement (what a
      size-2 town draws at) instead of scaling by population, which is what made them specks;
      their built ground matches a town's too
- [x] **This DONE / LEFT list**
- [x] **A verlet chain for the cordage** [121b, partly — see LEFT]. Four nodes per ship, hung
      from her masthead in her own frame, position-Verlet with three constraint relaxation
      passes, driven by apparent gravity under her heel, the angular whip of her roll and the
      wind across her deck. Her whole rig takes its shape from it, so the rigging lags her roll,
      overshoots and damps down on its own — which no closed form could do

## 0.22.22 — issue18
- [x] Smoke: stiffer drag then a non-decaying buoyancy, so it leaves hard, checks and climbs;
      4 volumes a call at twice the rate, lives 34–52 s, plus a billboard skirt [191]
- [x] Fire: tongues 5 + 10·heat, sparks 4 + 11·heat [192]
- [x] **Ten real classes in three roles** — Caravel/Fluyt/Hulk/Merchantman,
      Sloop/Xebec/Frigate, Galleon/War Galleon/Ship of the Line [193]
- [x] **Crew and ammunition** simulated: balls kill men, a half-manned ship is slow, ~26
      broadsides to a dry locker, a dry ship falls silent; six bars on the muster [178]
- [x] **A yard that repairs** — rig, battery and flooding mend with the planking, men at a
      quarter the rate, lockers filled outright [170]
- [x] (found) every body started with no crew and no powder — `nphys_add` memsets [194]

## 0.22.21 — issue17
- [x] **The delivered 4x map was never being read** — the probe scanned one directory level and
      the zip extracts two [182]
- [x] **Oars to scale** — they were drawn at the hull's import scale, sweeps as long as the ship [183]
- [x] Upright cylinder-billboard flames, sparks, black/grey/white smoke [184]
- [x] Fine terrain stops streaming past a 6° view radius [185]
- [x] FPS limiter in Options, default 60 [186]
- [x] Authored tree LOD 5 to 30 units [187]; species by biome already standing [188]
- [x] Canvas burns away top-down; no hovering flag over a felled mainmast [189]
- [x] Wave foam rotated 90° and thinned [190]

## 0.22.20 — issue16
- [x] Realistic fire as flame tongues; sparks; the explosion uses them [171]
- [x] Oars measured from the GLB: blade at +x on a −45° diagonal [172]
- [x] Take in sails [173] · stranding [174] · collision damage [175]
- [x] Mesh damage removal: a felled mast is gone from the drawn ship [176]
- [x] The muster list [177] · villages at town footprint [179] · ring-street towns with
      towered walls [180]

## 0.22.19 — issue15 and the battle's teeth
- [x] Villages bigger in the strategic view [162] · start-menu graphics and quality presets [163]
- [x] Smoke slower, less round, fading rim-inwards [164] · battle orders [165]
- [x] Real shot: calibre, drag, the rolling deck, drift [166] · ships heel instead of gliding [167]
- [x] Rivers chained and smoothed [168] · mountain heights down by 1 [169]
- [x] **The butcher's bill goes home** [153] · fire [152] · gun damage with teeth [149] ·
      a sinking that reads

## 0.22.18 — the 4x world
- [x] Read the Mare Liberum 4x build; budget by chunks; reads in flight by size [156]
- [x] The GPU chunk atlas picks its grid from the dataset [157]
- [x] The geography fold reads a finer build at a stride [158]
- [x] The ranges no longer read as terraces [93]
- [x] River carving and navigation on authored data [118]; roads bridge them [160]

## Earlier (0.22.7 – 0.22.17), in brief
- [x] Terrain-v2 chunks, biome material masks, natural coasts, map view modes [TODO 1–4]
- [x] Rivers from the package [TODO 5, 91, 113] · roads to hub ports [TODO 6]
- [x] The battle scene's clock, wind, coast and landscape [1–8, 27]
- [x] Muzzle flash, gunsmoke, volumetric smoke [10–13, 95, 119]
- [x] Wake as a 3D wave; foam that fades and spreads [86, 87, 100, 129]
- [x] Oars, lateens, stays by station, rope as hemp prisms with catenary sag [99, 104, 121, 131, 133–136]
- [x] Q/E helm [130] · authored trees, 27 species, six LODs [112, 128]
- [x] Buoyancy, ballistics, recoil [52–62] · **the guns had never hit anything** [143, 144]
- [x] The damage model: hull, rig, guns, flood, sunk, masts down [145–148, 150, 151]
- [x] Fields, vineyards, orchards as texture and props [7a, 7b]
- [x] Vulkan memory/command/queue/fence layers; DX12 device at 12.2
