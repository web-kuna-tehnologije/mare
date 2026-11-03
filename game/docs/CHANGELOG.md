# Changelog

## 0.22.24 — the oars put back, bridges stood, roads graded by trade, an orchard set

Session **elm** (mare liberum: elm), working into
`/mnt/c/projects/mare_sessions/mare_elm_03-11-2026_010247`.

### The oars, reverted

"They were fine 2 turns ago, right before I asked you to add animation" - correct, and the
0.22.23 rowing cycle is what broke them. The fault was not the cycle itself but what it
touched: it feathered the blade by **moving `fx2`**, and `fx2` is not only the blade's pitch -
the rowlock mount solves the oar's *placement* from it. So as the stroke ran, the whole bank
shifted and lifted, and the geometry that had been verified numerically in 0.22.21 was lost.

Reverted to the 0.22.21 sine exactly. The numeric check agrees to the last digit: **blade tip
4.20 units outboard of a 1.25 rail and 0.33 under the water; loom tip 0.19 inboard at 0.79,
over a 0.49 deck** - the same figures 0.22.21 measured. A rowing cycle can come back, but it
has to drive the *swing* only and leave the mount's pitch alone; that is written on the code.

### Then the todo list

- **Bridges where roads cross rivers** (todo 161 / TODO 10b). `road_bridges` has recorded the
  crossings since 0.22.2 - position, the road's direction, the river's half-width - and since
  0.22.20 it is filled for the *authored* rivers too, which is what finally made it worth
  drawing. A stone arch now stands at each: two abutments, an eight-voussoir segmental arch
  springing from them, a deck and a low parapet either side, laid along the road and scaled to
  the span, sitting at the higher bank so it bridges the cut instead of floating over it.
  Measured: **16 bridges standing in one ring, of 218 known crossings** across the map.
- **Roads graded by where trade actually flows** (todo 181). The builder already tested
  `traded_today / throughput` - and `traded_today` is **reset to zero every day** by
  `sim/region.cc`, so whenever the roads were rebuilt it read near nothing and *every road in
  the world came out a track*: the log said "195 roads (195 tracks, 0 roads, 0 highways)". The
  signal has to be a persistent one, and there is one: `region.development`, which region.cc
  drives from the trade that actually moved, smoothed over days. A hub-to-hub link is now
  graded by the **smaller of the two towns it joins** (a road is only as busy as its lesser
  end) plus the development behind them; a village's track by its own output and the same.
  Measured on a fresh map: **183 tracks, 4 roads, 8 highways** where it had been 195 / 0 / 0.
- **A fruit-tree set for the orchards** (todo 117b). Up close an orchard was planted with the
  forest **oak and maple scaled down**, which is exactly what the row complained of. There are
  now three varieties, because a planted tree is a pruned one: an **apple**, low and wide on a
  short bole; a **cherry**, upright with a narrower head; a **plum**, bushy and nearly to the
  ground - each with a second off-centre lobe, so the head reads as two or three boughs rather
  than a ball. One is chosen per grove cell, so a grove is mixed as a real one is. The
  Mediterranean olive groves keep their authored olive.

### Measured

- MSVC build green; **`ctest` 7/7**, run twice.
- Oars: identical to the 0.22.21 figures (above).
- 16 bridges in a ring of 218 crossings; roads 183/4/8.
- 400 simulated days: trade and war healthy (Spain 32 ports, 325k tariffs).
- Shots: `oar_fixed_crop.png`, `bridge.png`, `orchard.png`.

### Note on the session tooling

The session folder must live under **`/mnt/c/projects/mare_sessions`** - a path under
`~/.cache` is WSL-local, and `cmd.exe` cannot reach it ("UNC paths are not supported"), so the
MSVC build cannot run there. Two stray mirrors were created and removed while this was sorted
out.

## 0.22.23 — the nineteenth issues folder: the wake survives the foam, masts fall, the cordage is solved

Session **elm** (mare liberum: elm), working into
`/mnt/c/projects/mare_sessions/mare_elm_03-11-2026_000404`. `assets/issues/issue19` first, then
the engine / physics / rope items it points at.

### issue19

- **"on sea foam is removes wake textures and everything - not sure if only textures not loaded
  or actual issue."** An actual issue, and a one-line cause: every source of white - whitecaps,
  crest foam, shore surf, the ship's own wash - was **summed and then clamped to 1**. Once the
  caps alone reached 1.0 the water was solid foam colour, and the wake, the sea's colour, its
  reflection and its refraction all disappeared inside it. Two fixes, both physical: the
  sources now combine by **screen** (`1-(1-a)(1-b)`), so each still whitens the water but the
  total approaches 1 asymptotically instead of clipping and structure survives; and a ship's
  wash **damps the chop it runs through**, which is what a wake actually is, so the trail reads
  as its own thing against the sea instead of competing with it.
- **"add animation of sails snapping and mast falling and rigging falling."** Since 0.22.20 a
  felled mast simply ceased to exist between one frame and the next. It now **goes by the
  board** over 2.6 s: heeling over the side, quickening as a falling spar does (t²), dropping
  as its heel tears out of the step and sliding forward off it, gone once it is in the water.
  Its canvas goes over with it, collapsing as the spar swings down. And a ship with **every**
  mast gone no longer trails a full suit of standing rigging - the rig is one per-class mesh so
  it cannot be cut station by station, but a bare hulk wearing a complete set of shrouds was
  the worst of it.
- **"add parts of damage to ship visible."** A hit ship is now visibly *pocked*: shot scars
  where balls struck (dark), the wood around them splintered pale, and smoke-blacking low on
  the topsides where it really collects. It is a noise field thresholded against her hull
  damage, so each ship wears her own pattern and it spreads only as she takes more.
- **"oars rowing animation."** The stroke was a sine - symmetric, and therefore not rowing.
  Rowing is two unequal halves: a short, hard **drive** with the blade buried, and a longer,
  easier **recovery** with it feathered clear of the water. That asymmetry, not the swing, is
  what the eye reads. 40% drive / 60% recovery, eased at the catch and the finish, with the
  blade squared and buried through the drive and feathered flat on the way forward.
- **"fire more detail not streams but realistic fire."** The 0.22.21 tongue was one smooth
  exponential lobe wobbled by a single noise - at any distance that is a *ribbon*, which is the
  complaint. A flame is a cluster: **three lobes** at different widths and offsets, fanning
  apart as they rise, each wobbling on its own phase and combined by max so they overlap and
  separate as the noise moves; a **finer noise eats the top third**, so the flame breaks into
  separate licks and embers instead of tapering to a point; and the root is held solid and
  bright, because that is where a fire is densest.
- **"village size on strategic map same like cities."** The third time of asking, and the first
  two were half-measures: 0.22.20 took a village from 5 px to 9 against a size-1 town's 8.5,
  which is "nearly a small town", not "the same as a city". A settlement mark is now **one size
  for every settlement** - whatever a size-2 town draws at - and only the great cities (size 3)
  stand above it. Scaling a village by its population is what made it a speck. Its built ground
  matches a town's too.
- **"also add done list and left form todo."** `docs/TODO_STATUS.md` is now exactly that: a
  **LEFT** list (ordered, grouped, with the dropped items and the reason) and a **DONE** list
  by version.

### "then go start on todo list from engine, physics and then the verlet rope"

- **Physics, measured before touched.** Before writing a broadphase (todo 154) or threading the
  physics jobs (155) I instrumented `nphys_advance`: the whole tactical world costs
  **0.05 ms a frame with 40 bodies, 2 sub-steps and 39 shot in the air**. Optimising that is
  measuring noise, so both rows are **dropped with the number against them** rather than done
  for the sake of the list. They reopen past a few hundred hulls. The timer stays in, logged
  every 120 frames, so the decision can be rechecked instead of re-argued.
- **The verlet cordage** (todo 121b) - asked for in issue16, issue17 and again here, and
  deferred three times because the rig is a single per-class mesh. It is in, and here is
  exactly what it is: a **four-node verlet chain per ship**, hung from her masthead in her own
  frame, integrated with position Verlet and **three relaxation passes** over the distance
  constraints. It is driven by what actually moves cordage - apparent gravity tilted by her
  heel (`g sin roll`), the **angular whip of her roll** (the masthead's lateral acceleration,
  which is why rigging swings *after* a ship rather than with her), and the wind across her
  deck. The solved swing angle and tautness go to the GPU on a new instance channel (`i_rig`),
  and `rope.vert` hangs every bight from them instead of from a pair of sines.
  **It is not a chain per rope** - her whole rig shares the one solved chain, scaled by each
  rope's own slack. That is a deliberate simplification and a defensible one (every rope on a
  ship hangs in the same gravity and swings in the same accelerations), and it is what lets the
  rig stay one mesh. A chain per rope needs per-ship rope geometry, which is now the only thing
  left on that row and is written up as such.

### Measured

- MSVC build green; **`ctest` 7/7** (twice).
- Physics: 0.046–0.066 ms a frame over 120-frame windows, 40 bodies.
- A 60 s staged action: 70 hits, worst hull 1.00, rig 1.00, flood 0.70, thinnest crew 0.14,
  driest lockers 0.73, 17 masts down.
- 200 simulated days: trade and war both healthy.
- Shots: `fire19.png` (clustered licks, wakes clearly visible through the foam),
  `rig19.png` (hanging cordage, a dismasted hull), `villages19.png` (the 4x map and a town).

## 0.22.22 — the eighteenth issues folder: smoke that climbs, ten real classes, crew and powder

Session **elm** (mare liberum: elm), working into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_231936`. `assets/issues/issue18` first, then
the todo list from the bottom, as asked.

### issue18

- **"smoke is awfull ... add more smoke particles by much and let them live longer and move
  slover (fast then slow down and move upward)"** - all three, and the velocity note was the
  important one. The drag on a volume was gentle, so a plume coasted sideways for its whole
  life and read as a smear. The horizontal drag is three times stiffer now, so the initial
  push is spent in about a second; against that, each volume carries its own `rise`, a
  buoyancy that does **not** decay with age - so smoke leaves the fire hard, checks, and then
  goes up in a column. With it: **four volumes a call instead of two, at twice the rate**, and
  lives of **34-52 s against 16-25**; and a skirt of cheap billboard smoke with the same
  three-tone mix rides along with them, which is where the particle *count* comes from without
  putting another dozen spheres through the ray-march.
- **"same with fire add particles"**: flame tongues **5 + 10·heat** (was 2 + 5) and sparks
  **4 + 11·heat** (was 2 + 6).
- **"add real ship classes - 4 trader classes, 3 interceptor classes and 3 ship of the line
  classes (naming conventions)"**. The fleet is **ten classes in three roles**:

  | role | classes |
  |---|---|
  | traders | Caravel · Fluyt · Hulk · Merchantman |
  | interceptors | Sloop · Xebec · Frigate |
  | line | Galleon · War Galleon · Ship of the Line |

  The six that existed keep their enum places, so every save index and all forty-odd `SHIP_*`
  call sites are untouched; the four new ones are appended. `ship_info_t.role` carries the
  grouping, so the AI, the yard list and the muster need no table of special cases - the
  shipyard panel now lists her stock under three headings. Three new *hull* classes
  (`caravel`, `xebec`, `galleon`) are built on hulls that were already authored, the way
  `great_carrack` has been since 0.22.4: a caravel is the coaster's hull drawn out with a
  lateen mizzen, a xebec a lengthened galiot with a real battery, a galleon the armed cog
  stretched past forty metres. **Every authored hull in the tree is now sailed by something.**
  The AI builds across the whole roster - caravels and hulks in the trade fleets, xebecs on
  the wings of a line, war galleons in the battle squadrons.

### Then the todo list, from the bottom

- **Crew and ammunition** (todo 178) - the two gaps the 0.22.20 muster list honestly left
  blank, now simulated rather than drawn. A ball that comes aboard **kills men**, most of them
  at the guns (a gun-deck hit costs most, a hull hit less, aloft least); a ship below half her
  complement loses up to a third of her way, because she cannot work her canvas. Her lockers
  hold about **26 broadsides**, and `nphys_spend_ammo` serves only as many pieces as her people
  can man and her powder can load - **a ship that has shot herself dry falls silent**, which is
  what lets a long action end. Both come home into `ship_t` (casualties are not replaced at sea
  and powder burnt is powder gone), both feed `fleet_power`, and the muster list has **six
  bars** instead of four. A prize is taken with a scratch crew and nearly empty lockers.
- **A yard that repairs** (todo 170) - since 0.22.19 damage has persisted and nothing took it
  off, so a campaign could only grind a fleet down. A refit now mends the rig, the battery and
  the flooding in step with the planking it replaces, finds men at **a quarter** of that rate
  (a mauled squadron stays weak for a season even when her hulls are sound), and fills her
  lockers outright, because powder is a store and not a wound. The port panel's button is a
  **Refit** now and prices all of it.

### Found on the way

- **Every ship in the staged exercise was starting with no crew and no powder**, and so not one
  gun fired - 0 hits over a minute. `nphys_add` `memset`s the body, which quietly defeats a
  default member initialiser on `body_t`. Caught by the battle log the same hour it was
  written, which is the argument for having it.
- The screenshot hook fires *before* the UI pass (by design - it is the backbuffer before the
  overlay), so the muster list cannot be verified from one. The battle log was extended with
  the thinnest crew and driest lockers in the action instead, which is better evidence anyway.

### Measured

- MSVC build green; **`ctest` 7/7**, with a new `test_crew_and_powder`: 26 broadsides of 20
  guns before the lockers are dry, then silence; ten balls on the gun deck leave 67% of her
  people and cost her way.
- A 70-second staged action: **63 hits, worst hull 1.00, rig 1.00, flood 0.89, thinnest crew
  0.34, driest lockers 0.69, 18 masts down, 2 sunk.**
- 300 simulated days with the ten-class roster: trade and war both healthy (Spain 32 ports,
  13 sunk / 17 lost, 170k in tariffs).
- Shots: `fire18.png` (a standing smoke column off a burning hull), `classes.png` (all ten
  classes in line).

## 0.22.21 — the seventeenth issues folder: the map found, the oars measured to scale, fire that stands up

Session **elm**, working from the read-only tree into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_192516`. `assets/issues/issue17` first.

### issue17

- **"take a detailed look at map its bad ... as i delivered [high-res] height maps and
  everything"** - found, and it was not the data's fault and not the renderer's: **the game was
  never reading the delivered build.** A zip extracted as delivered lands its manifest two
  directories down (`data-terrain-v2/mare-liberum-world-terrain-4x/mare-world-terrain/`), and
  the dataset probe scanned one. It scans two now, and the log confirms: `terrain-v2 dataset:
  27x14 chunks of 2049 samples (~699 m)` from `C:\projects\mare\assets\data-terrain-v2`. One
  thing is still missing from the extraction, and it cannot be fixed from a read-only session:
  the river layer needs packing. **Run once:**
  `python3 tools/rivers_pack.py assets/data-terrain-v2/mare-liberum-world-terrain-4x/mare-world-terrain 10.9e6`
  - without it the authored rivers (carved and navigable since 0.22.18) stay silent and the
  generated network stands in, painted only.
- **The oars** (image: the banks standing over the deck like wings). The 0.22.20 measurement
  found the blade's end; this round's *numeric* check found the real fault: **the oar was drawn
  at the hull's import scale.** The oar GLB is normalised like every Meshy asset - it spans ~2
  units, the same as a whole hull - so each sweep came out as long as the ship: the check
  printed the blade 12.5 units outboard of a 1.25-unit rail, and the loom crossing the deck and
  out the far side. That *is* the wings. A fusta's sweep is ~0.38 of her length tip to tip; the
  instance is scaled to that from the model's measured axis (2.583 units, -45 degrees, blade at
  +x,-y), the rowlock pivot is computed on that axis, and the check now prints: blade 4.2
  outboard of the 1.25 rail and 0.33 under the water, loom over the deck at hand height. The
  wings are gone from the screenshots. Knobs for the next argument: `MARE_OAR_SIDE`,
  `MARE_OAR_PITCH`, `MARE_OAR_MIN_PX`, and `MARE_BATTLE_FREEZE=1` to hold a staged battle still.
- **"fix this to look like fire and sparkles and black and grey an whitte smoke dense"** (image:
  the 0.22.20 tongues lying on the deck as orange streaks). The tongues were stretched along the
  *camera's* up, so a battle camera looking down laid them flat. A flame is a cylinder
  billboard now - it turns to face the viewer about the **world's** vertical only, so it stands
  up from every angle. With it: **sparks** (fast, bright, short-lived, flung in a cone and
  carried downwind, more the hotter she burns) and the smoke in **three tones** - black oily
  soot off the seat of the fire, charring grey, steam-white - two denser plumes a call instead
  of one grey. The screenshot shows upright flames on a dismasted, burning hull.
- **"game started lagging as more chunks getting loaded (reduce loaded chunks on zoom out)"**:
  a fine dataset's chunk is a 30 MB read and ~70 MB resident, and zooming out paged a
  continent of them in for detail the camera cannot see. A grid finer than ~1.3 km a sample now
  stops *starting* loads once the view radius passes 6 degrees (map height); what is resident
  stays wanted, so nothing thrashes on the way out and back in.
- **"limit fps to 60 (adsd limiter to options)"**: a frame limit in Options - off / 30 / 60 /
  120, **60 by default**, saved to the file. The remainder of each frame is slept coarsely and
  the last two milliseconds spun, so the cap holds instead of stuttering under OS sleep
  granularity. Off while recording (the film is frame-locked already).
- **"make low poly models for lod of trees not change with stock ones (you created)"**: the
  authored trees' own ~200-triangle LOD 5 now carries to 30 units (was 14), so the stock cards
  are only the stand-in while a species streams, not the far look of every wood.
- **"add all models of trees based on biome"**: already standing since 0.22.13/15 - 27 species
  picked by latitude band, hemisphere, conifer/broadleaf and the Mediterranean belt - and with
  the LOD range above they now stay authored to the edge of the prop ring.
- **"add vidible damage on ships when fighting. sails can burn also"**: a wounded or burning
  rig loses her canvas **from the top down** - topgallants blow out at rig damage 0.20,
  topsails at 0.45, courses at 0.70, lateens at 0.45 - each before the mast itself goes by the
  board (0.22.20). The physics already eats the rig while she burns, so a fire visibly strips
  her sails on its way to felling her masts. And a felled mainmast no longer leaves its **flag**
  hovering in the air, which 0.22.20 did.
- **"rotate by 90 degreed foam from waves texture and make it more transparent"**: the foam
  photograph's streaks ran across the swell; sampled with the axes swapped they lie along it,
  and the cap mask and crest foam are taken at roughly two thirds and three quarters strength.

### Found on the way

- The staged camera pin's fifth field is a pitch *scale*, not degrees - which is why three
  rounds of close-up verification shots missed their ships. `MARE_BATTLE_FREEZE` now holds the
  exercise still, and the numeric oar check exists precisely so a picture is not the only
  evidence.

### Measured

- MSVC build green; **`ctest` 7/7** (run twice, before and after the flag fix).
- The probe finds the user's own extraction: 2049 samples, ~699 m, budget raised to 408 MB.
- The oar check, before: blade lateral 12.55 of a 1.25 rail. After: 4.19, height -0.33 (wet),
  loom +0.19 lateral (inboard) at 0.79 (over the 0.49 deck).
- Shots: `fire17b.png` (upright flames, stripped and dismasted hull), `oar_f.png` (no wings).

## 0.22.20 — the sixteenth issues folder: fire that burns, oars measured at last, towns with streets

Session **elm**, working from `/mnt/c/projects/mare` (read only) into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_160813`. `assets/issues/issue16` resolved
first (issue15 had been closed in 0.22.19), then the carried todo rows.

### issue16

- **"make more realistic fire, you have particles and smoke also"** (the screenshot shows two
  glowing yellow discs on a burning ship). The 0.22.19 fire borrowed the muzzle-flash sprite,
  which is a hot *disc*. Fire is tongues. A new particle kind (`PK_FLAME`) draws the Meshy fire
  sheet stretched **2:1 tall and never spinning**, each tongue born small at the deck, climbing,
  stretching as it rises and gone in under a second - the flicker is many short-lived tongues,
  not one long-lived sprite. In the fragment stage the tongue is a noise-eaten teardrop, white
  at the root through orange to deep red at the tip, self-lit (no fog, no sun), and a hot fire
  throws embers downwind (`PK_EMBER`, already there). The magazine explosion uses them too.
- **"oars are on fron side wide part needs to be in water and thin part in ship"** - the
  *third* report of the same fault, and this time the asset was **measured instead of assumed**:
  the GLB's vertices say the oar is modelled on a *diagonal*, its long axis at -45 degrees in
  its own x-y plane, the blade toward (+x, -y) with a cross-section of 0.23 against the loom's
  0.09. Every earlier fix believed the blade was the **-x** end ("measured", it said), so the
  0.22.11 mount put +x - the real blade - *inboard*: the wide part buried in the hull, the thin
  loom sticking outboard and upward, which is exactly what every screenshot since has shown.
  The mount's side sign is flipped (blade outboard, trailing aft) and the rowlock pivot moved
  to the loom's true end; with the model's own -45 droop the existing pitch brings the blade
  ~20 degrees under the horizontal and the loom level over the deck. *Caveat, honestly made:*
  a close-up verification shot kept missing the moving ship this round, so this fix rests on
  the measurement, not on a picture.
- **"add posiblity to retract sails if upwind"**: a **Take in sails** button on the Battle
  panel. The player's side furls (the canvas is not drawn, spars and cordage stay - the furl
  machinery existed for oared ships since 0.22.15); under bare poles an oared ship pulls at her
  oar speed and a sailing ship is hove to, which is what taking in sail upwind is for.
- **"add posibiliti to sunk or to get strayed on beach"**: sinking has existed since 0.22.17;
  **stranding** is new. A ship driven hard aground - a quarter of her draft out of her depth -
  takes ground damage as the sea works her on the bottom, and after a few seconds of it she is
  **stuck fast**: no helm, no way, an event and a line in the log ("she has taken the ground
  and is stuck fast"), and the muster card reads *(aground)*.
- **"add damage on collion"**: a contact's normal impulse now costs timber. The blow is the
  closing speed at the contact point above a walking pace (board-and-board grinding in a swell
  stoves nothing in), lands between wind and water, and splits by inverse mass - the boat takes
  the worst of touching a carrack, as she must.
- **"add mesh damage removal dynamically ... actually removing parts of mesh"**: a mast that
  goes by the board **is no longer drawn** - not scaled, not greyed, gone, with its yards, its
  canvas and its lantina, stations falling from the bow aft to match the physics' count. The
  ships are assembled from parts per frame, so removal really is removal. (The standing rigging
  is one per-class mesh and still shows its old runs; cutting it per station needs the per-ship
  rig buffer that the verlet rope needs too - same blocker, todo 121b.)
- **"add ship icons and list on top ernemy on bottom player and shows stats"**: the **muster
  list**. The enemy's line across the top of the battle view, the player's across the bottom,
  one card per ship: her class name and four condition bars - hull, sails, guns, and the water
  in her - a flame mark when she burns, "+" when she is lost, "(aground)" when she is stranded.
  Crew and ammunition are **not shown because they are not simulated**; a bar that never moves
  would be a lie. (They are the natural next step of the strategic ship.)
- **"villages to small in sizes they need to be same size as cities"** (screenshot: pink
  smudges beside a walled town): a farm village's built disc now matches a small town's
  footprint (0.34 + 0.11·sqrt(pop), was 0.22 + 0.08·sqrt(pop)), its houses are a third bigger,
  and the house meshes draw to 70 units out instead of 40 - the issue's screenshot is taken
  from about 55, where the village used to show bare painted ground.
- **"you have assets for buildings - use them to create cities that look like cities"**
  (issue1.png: a sparse oval of stones round a handful of houses): three changes. A size-3 city
  now stands **~90 houses instead of ~40** and fills its wall; the houses stand on **ring
  streets** - each on one of 3-5 concentric rings, doors to the lane, instead of strewn at
  random - so streets appear between the rings; and the wall carries a **tower at every other
  joint**, so the ring reads as a rampart rather than a kerb. The authored pieces (town hall,
  church, warehouse, lighthouse) stand as before, now inside a town that deserves them.
- **"connect cities with roads and villages depending where trade is flowing to from
  hinterlands"**: *not done.* The roads already run settlement -> hub port; weighting them by
  trade flow needs the economy's flows plumbed into the road builder, which is a sim feature,
  not an afternoon. Left open as todo 171 rather than faked with cosmetic lines.
- **"add rope for then next round if unable this one"**: taken at its word - carried once more
  (todo 121b), with the same honest blocker: the rope geometry must move off the per-class
  static mesh onto a per-ship dynamic buffer before any simulation can bend it. The mast
  removal above now needs the same buffer, which makes it the clear next renderer task.

### Found on the way

- **The mirror build was linking stale objects.** `rsync -a` preserves source mtimes; when an
  edit's mtime landed *older* than the existing `.obj`, MSBuild skipped the recompile and the
  linker failed - or worse, succeeded - against last round's code. The sync step now touches
  the sources after copying. (Last round's lesson was "grep the linker too"; this is its twin:
  make sure the linker is even looking at your code.)

### Measured

- MSVC build green; **`ctest` 7/7**.
- A 55-second staged battle logged six fires with the new tongues visible as a flame column in
  the screenshot (`shots/fire_new.png`); `shots/venice_city2.png` shows the walled town with
  its corner towers and ring-street houses.
- The oar GLB, measured: long axis at -45.0 degrees in x-y, blade cross-section 0.227 at the
  +x end against 0.090 at -x - the numbers the fix rests on.

## 0.22.19 — the fifteenth issues folder: a battle you can command, and one that can burn

Session **elm**, working from `/mnt/c/projects/mare` (read only) into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_130428`. `assets/issues/issue15` first, as
the skill asks, then the relief, then the physics and battle rows of `docs/todo_list.md`.

### issue15

- **"increase sizes of villages assets in strategic view as they are literally dots on map"**.
  A village is a settlement of size 0 and it drew at 5 px against a small town's 8.5. At the
  strategic tier the house meshes are long gone (`gfx/scatter.cc` stops at `cam.mapness` 0.5),
  so that 5 px diamond *was* the village. Villages draw at 9 px now, and every settlement mark
  grows by a third once the camera reaches the strategic tier, where the map is all there is.
- **"add in menu starting options with posibility of changing rendering software and level of
  graphics details low -> high"**. Both existed as sliders behind a top-bar button that does
  not exist until a game is running - so the one choice that *has* to be made before the world
  is built, the render backend, could not be made from the menu at all. The Options panel now
  opens over the menu, and there is a **graphics detail preset** - Low / Medium / High / Ultra -
  which is a named set of the nine knobs already there (shadows, their distance, anisotropy,
  tessellation, model LOD, waves, ambient occlusion, bloom). Move any one of them and the
  preset reads "Custom", because that is what it is. The menu carries the four buttons and an
  "Options..." button of its own, and a line saying which renderer is running.
- **"add smoke in battle view to fade slower and be less round, also fade from end to middle"**.
  Three separate changes, because they are three separate things:
  *Slower*: the blobs' density fell as `1 - t^2`, already down a quarter at half life - a cloud
  evaporating while it was still rolling. It is `(1 - t)^2` now, which holds through the middle
  of its life, and the lives themselves went from 12-30 s to 20-50.
  *Less round*: a second, much lower-frequency noise lobe pulls the whole silhouette out of
  round, so one puff is tall and ragged and the next squat; the fine dent is half as deep again
  on top of that.
  *From the end to the middle*: a puff used to thin everywhere at once, keeping its size and
  going transparent - a ghost of the original ball. Smoke does the opposite: the thin outside
  mixes away first and the dense middle is the last of it. The visible radius now shrinks with
  age and the density is measured against *that*, so the cloud is eaten from its rim inwards
  and the last of it is a small dense knot where the heart of the puff was.
- **"in battle you are not allowed to make and direct changes - add UI with buttons like direct
  fire to warship / ... tradeships, retreat, board or fight to death"**. The battle was a
  spectacle: stop the clock, look at the wind, steer by hand, and that was all. There are two
  standing orders now, because they are the two decisions a commander of the day actually made:
  **Fire at** Nearest / Warships / Traders - the guns choose by the heaviest or the lightest
  ship in range (by her gun count, which is what tells a man-of-war from a merchantman at a
  cable), every time they are ready; and **The line**: Hold / Board / Retreat / No quarter -
  boarding lays her alongside the nearest enemy under everything she will carry, retreating
  turns away from the enemy's centre of mass and runs, "no quarter" holds station with the
  retreat locked out. They apply to the player's whole side; the enemy always engages the
  nearest, as before. Orders override the hand on the wheel and are cleared on entering a new
  action.
- **"add real cannonballs in game and sizes depending of cannons and add course of flying not
  just straight line but calculate wind imperfection in aiming and curvature and gravity also
  angle of firing based on waves and ship rotation and position. also add air resistance"**.
  Gravity was already there; everything else was not.
  **Calibre**: a shot now carries its radius, from the gun that threw it - 1.8 cm for a light
  piece to 3.1 for a lower-deck gun, in scene units - and it is drawn at that size (×3, because
  a 15 cm ball at a battle camera is a pixel and the point of drawing shot is that you can
  follow it).
  **Air resistance**: a sphere's drag deceleration is `3·ρ_air·Cd·v² / (8·ρ_iron·r)`, about
  `2.75e-5 · v²/r` - and the scene's scale cancels between the square of the speed and the
  radius, so that constant is the same in scene units as in SI. At a 32-pounder's 450 m/s it is
  ~77 m/s², which is round shot to the life. Being inversely proportional to the radius, it is
  also why light guns are short-ranged: measured in `physics_test`, **a 32-pounder's ball
  carries 30% further than a 6-pounder's** on the same lay.
  **The rolling deck**: a gun is bolted down, laid in the ship's frame and fired in the
  world's, and in a seaway those are not the same. The piece follows her roll (so a ship rolled
  towards the enemy throws her shot into the sea and one rolled away throws it over the
  mastheads - why gun captains fired *on the roll*) and her pitch, projected on the gun's
  bearing. This is the largest source of error in the model and it is a real one, not a random
  number: a steady ship shoots straight.
  **Curvature** is a constant sideways acceleration per ball, the curve a smooth bore puts on
  it from windage and spin - up to about a degree over a second's flight, sign and size the
  ball's own. And the shot carries the ship's own way with it off a moving platform.
- **"ships gliding as in video shown"**. On the strategic map a ship carried roll = pitch = 0
  and nothing but a 2.6-degree sinusoid in the vertex shader, so a squadron slid over the swell
  bolt upright - a decal dragged across the water, which is exactly what the video shows. Two
  fixes: a **wind heel**, the strength of the wind times the sine of the angle between it and
  her heading, to leeward, up to 8 degrees on the beam and further in a storm (nothing on a
  dead run or in irons) - that steady angle is most of what reads as "she is sailing" at a
  distance; and the working roll and pitch doubled, so a ship under way visibly works and a
  moored one still only stirs.
- **"as you have now real world data fine enough you can add real rivers and traversable and
  remove blockiness of rivvers add curvatures smooth"**. Traversable landed in 0.22.18. The
  blockiness had a specific cause: the waterwaymap layer is **825,000 lines of two points
  each** - every one a straight 2-3 km stick - and a navigable reach is carved 0.6 to 1.05
  units wide, which is *wider than the sticks it is carved along*, so the channel came out as a
  row of overlapping lozenges with a corner at every join. The segments are now chained back
  into rivers by an exact-endpoint walk (they were clipped from one source line, so they meet
  exactly) and each chain gets two Chaikin passes, which round every corner without moving the
  line off the survey. Measured: **287,511 segments of the reaches that are drawn wide became
  1,227 smoothed chains**; the 533,818 hairline streams are left straight, since a one-texel
  line has no corners to see and leaving them costs nothing. The width is per point now rather
  than per segment, so a river opens out downstream instead of stepping at every join.

### The relief

`GC_HEIGHT_SCALE` 7 -> 6 ("reduce mountain heights by 1"): 9000 m is 6 world units and the
vertical exaggeration 4.6x, against 5.4x. The 4x terrain build carries its own relief now, so
the exaggeration can come down without the ranges going flat (`shots/height6.png`).

### Physics and battle (docs/todo_list.md)

- **The butcher's bill goes home** (row 153, the biggest item on the list and open since the
  damage model was written). The damage was scenery: a ship could be dismasted, holed and sunk
  in front of the player and sail out of the scene without a scratch, because nothing read
  `nphys_damage_t`. `ship_t` now carries `d_rig`, `d_guns` and `d_flood` beside its hull
  points, and every tactical hull that stands for a real ship writes home. Three rules:
  damage is **taken, never given back** (the worse of what she had and what the battle did, so
  leaving and re-entering a scene cannot heal her); hull points **follow** the tactical hull
  damage down to a floor of a tenth rather than being set from it, because the discrete model
  is still the authority on an unwatched action; and **a ship that goes under in the scene is
  struck off the fleet**, which is the whole point. It runs on a five-second timer during the
  action as well as on the way out, so a battle left by Escape still counts. `SAVE_VERSION` 6.
- **Fire** (row 152). A heavy blow on the gun deck - where the powder, the slow match and the
  dry timber are - sometimes starts one, and a fire feeds on itself. It burns her rig first
  (canvas and cordage go like paper, and masts come down at the same thresholds a broadside
  brings them down at), then her timbers, which starts her leaking, so a fire ends where a
  broadside does. Her people are at the buckets and not the braces, so she loses way. And if
  it gets below it finds the **magazine**: she does not sink by degrees, she goes up all at
  once. Tuned against the clock: twenty seconds in she is at 60% of her speed with a mast gone
  and half her timbers started, and she is lost in a minute or two. `physics_test` covers it.
- **Gun damage with teeth** (row 149). The battery scalar had been kept and raised since
  0.22.17 and done nothing at all. A ship whose guns have been shot off their carriages fires
  up to four times slower and brings fewer pieces to bear; past nine tenths she has no battery
  left to fire with. And `fleet_power` reads `d_guns`, so a battery wrecked in a battle the
  player watched does not come back for the next one.
- **A sinking that reads**. A wreck used to simply stop. She settles now - down by the head,
  because the holes are forward of her centre of buoyancy - over about eight seconds, slow at
  first and then quickly, and then she is under. The heave and attitude channels into the
  shader have been in place since 0.22.12, so this was three lines of physics and no renderer
  work (`shots/battle_fire.png`).

### Measured

- MSVC build green; **`ctest` 7/7**, with two new cases in `physics_test`: `test_ball_drag`
  (a 32-pounder out-ranges a 6-pounder by 30%) and `test_fire` (she is lit, loses her rig and
  her way, and is lost).
- A 75-second staged battle: three ships caught fire, nine masts went by the board, and the
  screenshot catches one going down by the head with her bow buried.
- Shots in `shots/`: `height6.png` (the relief at 6), `battle_fire.png` (a dismasted ship
  sinking by the head).

### Not done this round

- **A verlet chain for the sheets and braces** (row 121b). The ropes are a per-class *static*
  mesh whose catenary is computed in the vertex shader, so a real verlet needs the rope
  geometry moved to a per-ship dynamic buffer with the endpoints enumerated per hull - a new
  subsystem, and a larger piece than anything else in this round. The simulation itself is
  cheap (30 ships x 40 ropes x 9 nodes is nothing); the plumbing is the work. Left open rather
  than faked.
- **Broadphase collision** (154) and **multithreaded physics jobs** (155): optimisations with
  no visible result at the hull counts the staged battle runs today.

## 0.22.18 — the 4x world, and rivers you can sail up

Session **elm**, working from `/mnt/c/projects/mare` (read only) into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_120500`. The new asset in
`assets/data-terrain-v2` first, then the open rows of `docs/todo_list.md`.

### The Mare Liberum 4x terrain build

`mare-liberum-world-terrain-4x.zip` is the same world as the 3 km overview at **four times the
samples per axis**: 2049 a chunk instead of 513, ~700 m a sample, resampled from GEBCO rather
than upscaled. 12.2 GB of it is what the game reads (`height.r32`, `coverage.r8`, `biome.r8`,
`biome_source.r8`, `splat.png`, `normal.png`, `tile.json`, `waterways.geojsonl`); the preview
PNGs and the palette texture are not read and were not unpacked.

Everything in the loader was already per-dataset except the four things that assumed 513:

- **The sample ceiling.** `GC_MAX_SAMPLES` is 2049; 513 stays as the overview's grid and the
  atlas' reference slot. A dataset with a finer grid used to be rejected by its own manifest.
- **The budget is a count of chunks, not megabytes.** A chunk is `N^2 * 17` bytes resident -
  4.5 MB at 513 samples, **68 MB at 2049** - so the flat 160 MB held two of them and thrashed
  the moment the camera straddled a border. The floor now rises to `GC_BUDGET_CHUNKS` of the
  finest dataset (408 MB for the 4x build); `MARE_GEOCHUNK_MB` still overrides.
- **Reads in flight scale with chunk size.** Four at once was right at 2.4 MB a chunk on disk;
  at 30 MB it stalls the IO lane for longer than the camera stays still.
- **The GPU chunk atlas picks its grid from the dataset.** A slot's cost is quadratic in its
  edge, so 4x4 slots of 2049 would be 800 MB. It now trades slots for texels and spends at
  most ~115 MB: 4x4 of 513 (50 MB, 52 degrees covered) for the overview, **3x3 of 1025 (113
  MB, 39 degrees)** for the 4x build. A finer dataset is point-sampled down by an integer
  stride - point-sampled and not box-filtered on purpose: the grids are `2^k + 1` and
  neighbouring chunks share their edge row and column, so taking samples `0, s, 2s ... C`
  lands on the shared edge exactly and the joins stay seamless, where averaging would mix a
  chunk's interior into its edge and open a seam at every border.
  `MARE_CHUNK_ATLAS_SAMPLES=2049` buys the full material detail at 2x2 slots and 200 MB.
- **The geography fold reads a finer build at a stride.** The fold writes a ~7 km base grid and
  takes nine taps across each cell; samples 700 m apart add nothing to that average. It had
  been reading 17 MB a chunk to throw fifteen sixteenths of it away: **2.6 s and about half a
  gigabyte of peak**, against the overview's 0.7 s. Reading every fourth row and column, by
  seeking over the rest, brings it to **1.0 s** and a fortieth of the memory, with the fold's
  own output identical to the sample (5,768,831 cells, 148,069 new land).
- `MARE_GEOCHUNK_DIR` takes a `;`-separated list, so a build outside the tree can be named
  *beside* the deliveries under `assets` rather than instead of them.

**What it looks like.** Compare `shots/terrain_3km.png` with `shots/terrain_4x.png` (the same
seed, camera and hour over the Dalmatian coast): the karst has rock faces, ridges and crags
where the 3 km data gave smooth green hills, and the islands have relief instead of being
plateaux. This closes todo row 93 - "the ranges at 12 read as terraces (the 2.8 km samples)" -
and the roadmap's standing "a genuinely finer DEM for the home seas", which has been the
highest-value terrain item since 0.20. It is still not street-level survey: GEBCO's own grid is
about 460 m at the equator, so 700 m output is the end of the real detail, not the start of it.

### Rivers: carved, navigable, and bridged (todo 118)

The package's river layer - 115 chunks of `waterways.geojsonl`, 1.27M segments - has been read
since 0.22.14, but only *painted*. Carving and navigation waited, correctly: the only rivers
before were a drainage network folded out of 7 km cells, which puts a watercourse wherever the
heightfield happens to dip, and cutting ship-scale channels along those opened navigable water
across half of Europe. These are surveyed watercourses, so they can be cut and sailed.

- **The class comes off the data.** `from_upstream_m` - the total length of river above a
  segment - is what decides it, and the thresholds were **read off the layer**, which is the
  half of row 118 that said "the half-width from the upstream length is a guess". Over the
  1.65M points in the game's box: `>= 15 Mm` is 74% of them, `>= 60 Mm` 15%, `>= 150 Mm` 3.9%;
  river by river, the Danube delta measures 338 Mm, the Rhine 110, the Elbe 94, the Po 59, the
  Ebro 50. So **150 Mm takes a merchantman** (the Danube and the lower Rhine) and **60 Mm a
  shallow draught** (the Elbe, the upper Rhine, the Po's last reach). 15 Mm would have reached
  Seville and Rouen and been not a river network but a canal system.
- **2,148 cells of the navigation grid are river** where there were none, by class, so a fleet
  can stand up the Danube as deep as her hulls allow.
- **Roads bridge them.** `river_mask` - the cells a road needs a bridge for - was built only by
  the *generated* network, so since the authored rivers replaced it a road had been laid over
  the Po as if it were dry ground. 31,895 cells carry a river for the roads to bridge now.
- **Carving defaults on for authored rivers and stays off for the generated network.**
  `MARE_RIVER_CARVE` decides it either way.
- Honest limit: `from_upstream_m` is length, not discharge, so it cannot tell a large river in
  a small basin from a middling reach of a huge one. The Seine (20 Mm) and the Guadalquivir
  (17) fall short of 60 and are not navigable; the Thames and the Tiber are not in the layer at
  all, falling under the export's own 10.9 Mm filter rather than ours. The layer carries no
  width or flow to correct this with - `width_m` is null throughout.

### Measured

- MSVC build green; **`ctest` 7/7**.
- The 3 km overview path is unchanged: same budget (160 MB), same atlas (4x4 of 513), same
  fold (5,767,931 cells, 148,995 new land), and with `MARE_RIVER_DATA=0` the generated network
  still comes up painted with 0 river cells open to ships.
- Shots in `shots/`: `terrain_3km.png` and `terrain_4x.png` (the same view, before and after),
  `rivers_po2.png` (the Po's lower reach as a navigable channel with its own beaches and surf).

### Where the data is

Unpacked to `mare_sessions/mare_elm_02-11-2026_120500/terrain_v2_extract/mare-world-terrain`,
because `assets/` is read-only to this session. To make it the game's default, move that folder
to `assets/data-terrain-v2/mare-world-terrain` - the loader probes that name first, ahead of
`data_new`. Until then, point `MARE_GEOCHUNK_DIR` at it.

## 0.22.17 — the fourteenth issues folder: the helm answers, the oars row, and the guns finally hit something

Session **elm**, working from `/mnt/c/projects/mare` (read only) into
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_002959`. From `assets/issues/issue14`, then the
open rows of `docs/todo_list.md`, then the physics.

**Note on what was in the tree.** The 0.22.16 round (issue13) was lost: the tree was committed at
`199ec3a` without it, and the changelog's own 0.22.16 entry went with it. That is exactly why
issue14 reports the oars, the lateen and the riggings as *still* wrong. The worthwhile parts of
that round are carried back here, marked below.

### issue14
- **Q and E in the battle** ("Q AND E IN BATTLE VIEW DONT WORK"). They had never been bound to
  anything - the feature existed only in a changelog entry. `Q` and `E` now put the player's line
  under **manual steering** and swing the ordered course to port and starboard; `H` hands her back
  to the line. The order goes through each ship's rigid body, so a fusta comes round smartly and a
  great carrack leans into it. Steering runs on the battle's own clock: a stopped battle does not
  steer.
- **The oars** ("wide part needs to be in water, pivot in the ship part, movement slowly 30 deg").
  The blade is the model's **-x** end - measured, its cross-section is twice the loom's - and a
  *positive* pitch about the oar's own z carries that end under, so the sign had been flipped the
  wrong way and the bank was rowing the air. Three things changed: the sign is right; the oar
  turns about its **rowlock on the rail** (the instance is placed at the rowlock less the rotated
  model-space offset of that point, the trick the lantina uses) instead of hanging centred
  outboard; and the sweep is **±15 degrees** at a stroke every three to five seconds.
- **Villages inland** ("other than cities add random small villages ... connected with roads").
  A settled region founds six (was three), a frontier region two, and they may stand 2 units apart
  (was 3). Each is a real inland settlement, so `world/roads.cc` routes it to the port it belongs
  to and `gfx/scatter.cc` gives it houses and a mill.
- **The front lateen** ("still front lanteen and sail isn't fixed"): the whole triangle - tack,
  foot and clew - rides a seventh of a mast higher, so it swings over the rail instead of through
  it, and the yards trim by the bisector rule, so the fore lateen sets to the wind rather than
  lying across the bow.
- **Rope elasticity** ("add more elasticity to rope for sails"): half as much sag again, two slow
  out-of-phase breathing terms, and the running rigging comes up taut under a drawing sail while
  the standing rigging keeps its sag.
- **Rope texture** ("add texture similar to riggings"): the ropes drawn inside the canvas mesh -
  the foot rope, the sheets, the clew-lines, the lateen's robands - were flat cross-quads and read
  as paper ribbon beside the geometry-stage cordage. They are six-sided, lathe-shaded prisms now.
- **The stays to nowhere** ("remove riggings on the stern that lead nowhere"): the stays were
  chosen by array *index* while the stations run bow to stern, so every mast stayed to the stem
  and the **foremast ran a stay the whole length of the ship to the taffrail**, through the main
  and mizzen tops. They are chosen by *station*: forward to the next mast ahead or the stem, aft
  to the next mast astern or, for the aftermost only, the taffrail.

### Carried back from the lost round
Model-space sampling of every surface detail (`v_mpos`) so the planks and the canvas weave stop
sliding over a moving hull; the bisector sail trim; long tapering swallow-tailed pennants; the
yaw rate *taken up* by displacement (1.7x on a fusta, 0.4x on a great carrack); issue9's last
line, "make fields more visible" (the worked land carries to 340 units instead of 220, the paint a
quarter stronger, the cropland floor 0.72 instead of 0.55); and the lee side of a sail, which was
a flat dark sheet and now glows as thin canvas does.

### Physics: the damage model (P1.3), and two bugs that had made every battle bloodless
- **No cannon shot had ever hit anything.** The guns fire in **world** coordinates; the tactical
  bodies live in the staged battle's **local** frame. The broadphase compared a gun at world
  (1000, -700) against a hull at local (-40, 20) and never matched, so every ball in every battle
  flew past and out to the 4 km cull. The same mismatch fed `nphys_impulse` a lever arm a
  kilometre long on every shot. `particles_set_origin` takes both into the body's own frame.
  Measured before: `battle damage: 0 hits` over a two-minute action. After: 440 hits, 31 masts
  down, 6 sunk.
- **The pieces were laid up, from above the target.** Elevation was +0.2 to +2 degrees from a
  muzzle that already stands above the enemy's hull box. Point-blank is fired level now (-1.3 to
  +0.1 degrees) and the hit box reaches the rail (0.16 of the length, was 0.11).
- **Zone damage** (`nphys_damage_t`: hull, rig, guns, flood, sunk, masts_down). Where the ball
  strikes decides what it breaks: between wind and water she starts to leak, on the gun deck she
  loses her battery and timber, aloft it is her rig. Each blow is scaled by the shot's speed and
  by her displacement, so the ball that starts a plank on a fusta is a dent in a great carrack.
- **Masts** go by the board at each threshold of rig damage, once, with an event the battle turns
  into splinters and a line in the log.
- **Flooding** grows with the holes below her line *and with itself*, so the end of a sinking is
  quick: a hull holed through floods in about twenty seconds. It drags her and costs her speed.
- **Sinking**: past full flooding she is a wreck - dead in the water, no helm, no guns, counted in
  `nphys_wrecks()`.
- A wounded rig costs up to 65% of her speed; water in her up to 55%.
- `physics_test` carries a new `test_damage`: a blow aloft wounds the rig, brings a mast down,
  slows her and leaves her dry; twelve balls between wind and water hole her through, she makes
  water and in the end she goes. **7/7 suites green.**

### Measured
- MSVC build green; `ctest` 7/7 including the new damage checks.
- `physics_test`: `rig 0.39, 1 mast(s) down, speed factor 0.75, dry` and `holed hull 1.00 flooded
  to 1.00 and sank; 1 wreck(s)`.
- A 110-second staged action: 440 hits, worst hull 1.00, rig 1.00, flood 1.00, 31 masts down,
  6 sunk of 30.
- `shots/damage_v.png` - a galley's full bank with the blades in the water and the looms over the
  rail; `shots/rig_v.png`, `shots/galley_lateen_v.png`.

## 0.22.15 — the twelfth issues folder: volumetric gunsmoke, sagging cordage, sternway and oars, a sun that moves, matte wood and canvas

From `assets/issues/issue12/issue.txt` (the trees, oars and rivers of it were done in 0.22.14) and the
open rows of `docs/todo_list.md`. No `issue13` folder exists yet.

### Smoke (issue12 "remove the particles, add 3D volumetric smoke with shaders"; "faster expansion, longer stay")
- **Volumetric gunsmoke.** The three smoke billboard layers are gone (`MARE_SMOKE_BILLBOARDS=1`
  brings them back). A broadside now spawns *density blobs* (`blob_t`, up to 128): three white
  cores that race out and swell, a grey plume, a thin haze - lives 12-30 s, cubic-eased growth so
  a puff is most of its size in the first seconds, advected at half the wind with a little lift.
  `fx/volsmoke.frag` is a fullscreen pass after the billboards: every blob the pixel's ray
  crosses is marched along its own chord (six jittered samples, clipped to the captured opaque
  depth) and the samples are accumulated order-free - the optical depths add, the colour is the
  extinction-weighted mean, which for one-coloured powder smoke needs no sorting. The density is
  the blob's falloff with its silhouette dented and eroded by a scrolling 3D noise; each sample
  is lit by the sun through two more taps of its blob towards it (self-shadowing), with the
  sky's ambient and a back-lit rim, fogged. Hulls and masts stand *in* the smoke now rather than
  behind a stack of discs. The flash, spray, rings and splinters stay billboards.
  `MARE_VOLSMOKE_DEBUG=1|4|5` draws the blobs' spheres, the marched alpha, or freezes the blobs'
  ages so a screenshot always holds smoke. (A first cut marched one union span over all the
  blobs with 18 steps: the samples fell in the water between them and nothing showed.)

### The rig (issue12 "rope behaviour flexible and physics based"; todo 115)
- **Ropes hang.** Every rope is eight segments whose vertices carry (t, slack, kind)
  (`mb_line_rope`); `rope.vert` hangs a catenary of the slack - small on a taut stay, large on a
  long level sheet - blown to leeward by the wind in the ship's frame, lagging the heel so the
  bight swings against the roll, with a slow breathing. The rig instance carries the wind for
  it. Not a verlet: a shaped parabola with the right forces behind it; a true simulation is the
  next step if the look is wanted closer.
- **Sails are never hidden by distance in the staged scenes** (issue12): the rig's pixel gate is
  bypassed when the ship is drawn at the staged scale.
- **Furled sails.** `shipyard_req_t.furl`: at 1 the canvas is not drawn, spars and cordage stay.

### Sailing (issue12 "sails only against the wind: move backwards; with oars, oar speed and roll the sails down"; "wind and sail direction for the speed")
- Every staged ship's own heading against the wind sets what her canvas gives (`wind_factor`, the
  point-of-sail curve), not only the line leader's. In the eye of the wind a sailing ship's canvas
  is aback and she makes **sternway** at up to a quarter of her speed (the physics drives a
  negative command now); an **oared** ship caught there takes in her sails over two seconds
  (`furl` -> 1) and pulls at her oar speed, setting them again once the wind draws aft.

### The sun (issue12 "animate the sun in delta time, not in ticks")
- The sun's position is read from the days already ticked *plus* the accumulated fraction waiting
  for the next tick, so it moves every frame.

### The sea (issue12 "in battle mode the wave length by wind strength, not zoom")
- The staged scenes use the finest tessellation cell for the Nyquist fade everywhere, so the set
  of wavelengths drawn is the wind's and does not change as the camera moves; their longest
  wavelength follows the wind strength harder (0.50 + 0.80 s, map 0.70 + 0.45 s); the wave set is
  rebuilt when a staged scene opens or closes.

### Materials (issue12 "wood, sails, rope: reflectiveness 0")
- `sun_shade_matte`: Lambert under the sun with the shadow, the hemispheric ambient, no specular
  lobe; `meshes.frag` uses it for wood, canvas and cordage. Painted trim, metal and stone keep
  their highlights.

### The hinterland (todo 117)
- The authored trees' LOD 5 (~200 tris) stands from 7 to 14 units from the ring centre; the cards
  begin past that.

### Process
- Mirror `C:\projects\mare_oak_01-11-2026_222329` (the `skills/` naming). The oak worktree was
  reset onto `main` (the other session had committed 0.22.14 from both worktrees as WIP). That
  commit also removed `docs/dev/summary_oak_*` except issue10 and `.github/workflows/build.yml`;
  the issue11 summary is re-added here, the workflow is left as the other session left it.
- The other session wrote a `gfx/volsmoke.cc/.h` and a composite shader into this mirror while it
  built; they were not part of this tree and were removed before the link.

## 0.22.14 — the eleventh issues folder: the lantina on the head, waves off the bow, dense white smoke, round shot, authored trees, the package's rivers

From `assets/issues/issue11` (nine screenshots and videos named for what they show; no text) and
`assets/issues/issue12` (`issue.txt`: the trees, the oars, the smoke), plus the round's own asks:
relief 5-8, no cannon parts, cannonballs, ropes at the foot of the sails, readable asset names,
a folder listing.

### The ships
- **The lantina is the sail's own spar** (issue11 "fix this lateen and sail", issue5/9/10 before
  it). The scanned `lateen_yard` never sat on the head whichever way it was scaled; it is not
  placed any more (`MARE_LATEEN_ASSET=1` brings it back). `rig_spar` builds an eight-sided tapered
  prism on the exact design line in every cut of the lateen mesh, the robands lash the head to it,
  and yard and canvas swing as one piece.
- **Ropes at the foot of the sails**: every square sail carries a bolt-rope along its foot, a
  sheet from each clew down and aft towards the deck and a clew-line up the leech, in the sail's
  own mesh so they brace with it.
- **The oars** (issue11 "need to be rotated", issue12 "fix oars positions"): squared to the hull
  trailing ten degrees aft, a short sweep (4-13 degrees, was 4-22) and a steeper blade (0.60 rad,
  was 0.46) so the loom crosses the rail and the blade is in the water, not lying along it.
- **The standing rigging stays aboard** (issue11 "remove external riggings"): the stays' bow and
  stern are the hull model's own ends (were 0.47 of the length, past every counter, so the
  backstays ran down into the sea), and the feet of the aft stations' shrouds and stays stand on
  the castle. `rig_build` takes the hull's bounds and the castle lift.
- **The cannon parts are off** ("remove cannons for now, I will update the assets"): the hulls
  carry their guns painted; `MARE_GUNS=1` stands the imported parts again. The seven `gun_*` assets
  stay in the store and the manifest.
- **Round shot** ("make cannonballs"): `nphys_shot_list` hands the renderer every ball in the air
  and the staged scene draws them as 32-face iron spheres of 0.13 local units (a touch over life
  size) with the ships.
- **The hull's overlay** (issue11 "weird texture over"): the triplanar plank modulation read as
  purple-brown blotches at the battle scale; finer (1.4x / 6.5x) and a third as strong.

### The sea
- **Waves off the bow** (issue11 "add ship waves and splashing", "add real waves forming from ships
  moving"): the bow wave is 0.030 L at speed (was 0.012), the pile-up along the forward hull
  0.022 L, the foam collar reads over a wider band; every stem under way throws spray that scales
  with her speed, not only in a fresh breeze.
- **Crests no longer uniform, the sea no longer flickers** (issue11 "crests are too uniform",
  "sea flickers too much", the menu video's "continuous flicker"): a slow large noise field
  (`capvar`) gates the whitecaps, the collapsing crests and the staged `peak` term, so only some
  crests break and in wandering patches; every threshold is wider (caps fade over frames instead
  of switching with the tessellation); the staged scenes' ripple layers drift at half the rate.
- **Smoke** (issue11 "more dense smoke, grey and white, remove ugly particles"): six white cores
  (shade 0.82, was 0.60) and six grey plumes (0.52) per gun, denser and longer-lived, greying as
  they thin; the orange ember dots and the spray spout under the guns are gone - the fire sheet on
  the flash carries the sparks. (issue12's "remove the particles, add 3D volumetric smoke with
  shaders" is the next step: todo 95.)

### The world
- **Relief 7** (`GC_HEIGHT_SCALE` 12 -> 7; "reduce to 5-8, see what fits best"): 5, 6.5 and 8
  compared over the Alps and the Dalmatian coast (`docs/screenshots/issue11_oak_alps_h*.png`,
  `dalmatia_h*.png`). 5 flattens the Dinarics to downs, 8 begins to show the 2.8 km sample grid
  as shelves on the ranges; 7 keeps the Alps readable without the terraces.
- **The package's rivers** (todo 91, TODO item 5): `tools/rivers_pack.py` packs each chunk's
  `waterways.geojsonl` into a `rivers.bin` (segments above 15,000 km of upstream length, the
  game's world box only: 63 chunks, 1.3M points from 500 MB of JSON); `world/rivers.cc` reads
  them in place of the generated drainage network (`MARE_RIVER_DATA=0` for the old one), each
  segment a polyline with its half-width from the upstream length. Painted on the ground through
  the same ring path; carving and navigation still wait (`MARE_RIVER_CARVE`). The hairline paint
  floor is raised (0.22 -> 0.38) so a mapped river reads at the ring's texel.
- **The authored trees** (issue12 "trees in assets/trees: rename, add, optimise, biomes"; todo
  96): the 27 low-poly Meshy trees are imported as props (`tree_*`, six LODs each, unit height)
  and the scatter stands them by biome - oak/beech/birch/maple and Scots pine/spruce/fir across
  temperate Europe, birch and spruce in the north, olives on the Mediterranean belt, acacias,
  baobabs, palms and kapoks in the tropics, maple/white pine/Douglas fir/sequoia in North America,
  quebracho and Paraná pine in the south - two or three species mixed per wood by a hash. The
  scatter takes one GPU copy of each species' LOD 4 (~700 tris) from the resident asset and
  instances it within `PROP_LOD1`; the cards stand beyond, the procedural trees until a species
  is resident, and the props are laid again once when a species arrives after the ring composed.
  Orchards use oak and maple, groves the olive.
- **Fleet icons fade at the hinterland zoom** (todo 94): under 7 units of camera distance in the
  3D view, gone at 4.

### The store and the docs
- Every Meshy / Designer / hash file name in `/assets` is readable (`assets/RENAMES.md`, written by
  the other worktree's session this day; audited here: none left outside the sidecars).
- `tools/tree_listing.py` writes `docs/FOLDER_STRUCTURE.md`: the repository and the asset store as
  one listing, directories with counts and sizes, same-shaped files as patterns.
- Build mirror `C:\projects\mare_oak_01-11-2026_211703` (the session's naming per `skills/`),
  assets by junction. MSVC build green, 7/7 headless suites.

## 0.22.13 — the tenth issues folder: the asset store sorted, the world package, cannons on the ships, sheets in the smoke, the rig at the mastheads

From `assets/issues/issue10` (`issue.txt`, the menu video "too much blur").

### The asset store (`/assets`, not in git)
- **Sorted and deduplicated** (`assets/README.md` is the map). One authored tree, `assets/generated/`
  (the name the other worktree gave the former `assets/elm`; the game probes `assets/generated`,
  then `assets/elm`, then `game/assets`). Gone: the `oak/`, `elm_lod/` trees (the same assets at an
  earlier import), `ships_clean/` and the sixteen `Meshy_AI_*` hull folders (three byte-identical
  copies of every hull GLB; `ships/<name>/<name>.glb` stays with a `source.txt`), `ships/rigging/`
  (four copies of the sail sheets), the root galley blends (in `ships/light_galley_gen_1`), the
  `(1)` duplicates of the sea sheets, `oak_terrain/` (an obsolete L2 tile cut), and a 2.8 GB
  regenerated normal map. `sea/sea__new` is `sea/sheets`; `FOAM/` + `foam/new/` are
  `ships/foam/{photos,masks}` (the six sheets restored from the Downloads folder after the
  case-insensitive rename ate them); root images and `example/` are `reference/`. 15 GB from 24.
- **The terrain package** `mare-world-terrain.zip` (the `new_terrain_data_v2` delivery) is unpacked
  and SHA-256 verified under `assets/data_new/mare-world-terrain/` - 378 chunks, biomes, splat,
  normals and, for the first time, **rivers** (`waterways.geojsonl`, 115 chunks). The reader finds
  it as it is (`mare-terrain-v2`); the zip is gone.
- **The repo tree.** The WIP merge had re-added the retired `world/geochunks.*` and
  `gfx/chunkatlas.*` beside their renamed successors, so the CMake build failed to link and the
  two headless suites that include `world.cc` could not resolve `geochunks_height_scale_rel`.
  Both pairs are removed; the suites get the ratio as a stub. **MSVC build green, 7/7 suites.**

### The world (issue10 "increase to 12 height of elevation")
- `GC_HEIGHT_SCALE` 5 -> 12 (`MARE_HEIGHT_SCALE` still overrides). Every threshold follows through
  `geochunks_height_scale_rel()` as before. At this scale the 2.8 km sample grid shows as terraces
  on the ranges: the finer DEM is the next data item (roadmap 1).

### The hinterland
- **The canopy is trees only.** The painted crown noise and its tilted normal are gone from
  `terrain.frag`; a wood is the instanced trees of `gfx/scatter.cc` on the forest colour, with a
  faint ground mottle between the trunks.
- **The fields are back, without props.** The cropland of the open lowland has a floor of 0.55
  under the settlement halo again (issue9 had confined it to the halo and the roads), and the
  standing wheat is off (`wheat = 0`); the parcels are paint.
- **Orchards and vineyards as props.** Orchards ring the villages twice as wide and stand in the
  open fields in patches; vines take more of the sunny slopes and no longer need a village beside
  them; the scatter stands them from land-use 70 / 80 (was 90 / 110), ahead of the woods.
- **Random villages in the hinterland.** Two ways: the land-use bake seeds a hamlet in every few
  2.2-unit cells of gentle, low, workable ground (a built disc of its own, houses from the scatter,
  roofs from the shader); and `sim/region.cc` founds three farm villages in a settled region (one
  in a frontier one) instead of one, so roads and mills come with them.

### The ships
- **Cannons.** The seven Meshy guns under `assets/ships/cannons` (large naval cannon, late-15th-c.
  cannon, heavy bombard, naval perrier, light breech-loader, heavy and light swivel) are imported
  as `gun_*` ship parts (150k-tri LOD 0, six LODs, unit length along the barrel) and stood at
  every port on both sides - the same stations the broadside's flashes come from - pointing
  outboard, rigid to the hull. The class picks its gun (`gun_role_for`: carracks the cannon, the
  northern merchantmen the perrier, galleys and small traders swivels on the rail); the class file
  can override it (`"gun"`, `"gun_m"`). `GUN_MIN_PX 120`.
- **The class file is live.** `ship_classes.json` had never been applied: the parse test was
  inverted (`gjson_parse` returns 0 on success), so every run since 0.22.6 logged "is not valid
  JSON" over a valid file and sailed the built-in table. Fixed; the per-class `mast_h`, `sails` and
  the rest now take effect.
- **The rig at the mastheads** (issue10 "lateen and masts and rigging to go to the top of the
  mast, not above"). The rope tops used the *main* mast's model height for every station, measured
  from the deck, while the masts are their own models stepped `MAST_STEP` below it and lifted on
  the castles - every shroud and stay ended a fifth of a hull height over its truck, a mizzen's
  two thirds of the pole over it. Each station is measured from its own asset, from the heel; the
  standing rigging is made fast at the hounds (0.965 of the pole); the cordage mesh waits until
  every mast's bounds are in; the flag staff follows. The square layers and the lateen are laid
  out in fractions of the mast *above the deck*, so the 0.96 topgallant and the lateen peak stand
  under the truck.
- **The lateen** (issue10 "the front lateen as wide as the sail and connected to it"). The design
  line is re-cut with the peak at the masthead (`LAT_PEAK_Y` 0.98, tack 0.40 forward and 0.16 up,
  clew and foot moved with it); the authored lantina is scaled by its true length to the line's
  length (not by its rise and a tenth more), centred on the line's middle and turned in its own
  plane to the design rake (`inst_t.fx2`), so both ends sit on the head's ends and every roband is
  a lashing. The same path serves the galleys' fore lateen.
- **The spritsail** was 0.31 of the hull wide and a twentieth deep - a white bar across the bow
  from abeam. Half the width, twice the depth.

### The effects (issue10 "there are smoke, fire and foam images - use them; make volumetric smoke")
- **Sprite atlases.** `tools/fx_sprites.py` (numpy + Pillow) cuts the Meshy sheets into RGBA atlases:
  smoke 2x2, fire 4x2, foam masks 2x2 (`<assets>/fx/*_atlas.png`). The sheets are flat grey with a
  faint checker, so the cover is the pixel's distance from that grey (chroma for the flames, glow
  for the dark sheets), the background unmixed from the colour, frames centred on their alpha.
- **The texture loader decodes RGBA** when the format asks for it (`RHI_FMT_SRGB8_A8`).
- **The puff shader** samples them: the smoke sheet's cover is the puff's shape (the 3D noise still
  erodes and churns it), its luminance the volume's own light and shade, and the gradient of its
  cover bends the sphere normal so the billows are lit as billows; the flash carries a fire frame
  over the procedural core; the ring and the spray are broken up by the foam masks. Units 8-10.
- **The blast ring** (issue10 "less white, waves bigger, maybe less distance"): runs out 1.6 hull
  lengths (was 2.3), lifts the water 0.085 (was 0.035), whitens at 0.14 with a 0.16 cap (was 0.30 /
  0.34) and only where the foam tile breaks.
- **The shockwave's radial blur** halved (`tonemap.frag`).

### The sea (issue10 "waves like wake.png as an actual 3D wave"; "foam should fade out, be less opaque, spread slowly")
- The wake's geometry was cut off at 1.55 hull lengths while its foam ran to 5: the arms now run
  to ~4.4 L (eased out before the early-out), their crests half as high again (0.052 L), the inboard
  trough deeper, the left trail's chop a little stronger.
- The wake map: `WAKE_TAU` 28 -> 20 s, `WAKE_DIFF` 0.55 -> 3.5 texels/s with a two-ring diffusion
  kernel (the cone opens now), the white (R) decays faster than the churn (G); in the fragment stage
  the trail's white follows the map value (`smoothstep(0.03, 0.90)`) instead of holding at full to
  0.55, with a 0.42 ceiling (was 0.62).
- The **foam tiles** are cut again from the restored sheets (`tools/foam_tiles.py`, new paths):
  `foam_wake/streak/crest/lane.png` in `game/assets/sea` - they had been missing from this tree,
  so the wake and the blast ring were falling back to constants.

### The menu (the video)
- The backdrop blur was the HDR frame at a quarter size through eight blur passes: a grey smear.
  Half size, one pair of passes, mixed at 0.7 and darkened 16% (was 22%).

### Process
- Mirror `C:\projects\mare_oak_20261101_173707` with a junction `assets -> C:\projects\mare\assets`;
  `tools/vid_frames.py` decodes the issue videos through Blender; `tools/fx_sprites.py` runs with the
  Windows Python (numpy). The asset store is shared with the other worktree's session, which renamed
  `elm` -> `generated` mid-round; both names are probed.

## 0.22.12 — the ninth issues folder: a gentler world, a working hinterland, the V of the wake, the rig heeling with the hull

From `assets/issues/issue9` (`issue.txt`, two screenshots, the rigging video).

### Recoil, as asked ("move the ship by 5 cm, not rotate her five times; about the x axis")
- The sideways set is the ball's real momentum now - 0.35 tonne-units per second a gun, which
  moves a 1000-tonne hull centimetres - applied at her **centre**, so the broadside no longer
  yaws her. What it does instead is **heel** her: each gun kicks her roll by a quarter of a
  degree (scaled by the gun's size), away from the side she fired, and a damped spring in the
  physics (`nphys_kick_roll`, period ~4 s) brings her back.
- **The body's attitude reaches the shader.** `inst_t` carries `roll_b`/`pitch_b` (the
  `i_anchor` attribute is a vec4 now), set on every part of a ship from `shipyard_req_t`, and
  the three vertex stages add them to the rigid rotation about the hull's pivot. So the
  five-point buoyancy roll and pitch of 0.22.11 and the recoil heel are *drawn*, hull, masts,
  canvas and cordage together; the canned motion is cut to a third and only adds the fidget.

### The world (issue9 "reduce elevation change by 70% ... nothing is fixed in terrain")
- The vertical scale is 16 -> 5 (`MARE_HEIGHT_SCALE` still overrides); the rings' ridge noise
  is cut to match. Every height threshold that was tuned against the old scale - the terrain
  shader's snow line, the shore fade and ridge gate in the stream, the lowland gates in the
  land-use bake, the scatter's tree line - is expressed through `geochunks_height_scale_rel()`
  (`u_hscale` in the shader), so the snow stays on the summits and the fields on the plains.
- **Why nothing was fixed before.** The base heightfield never followed the scale: the
  geography bake kept whichever was *higher*, the procedural relief or the data, so the data
  could never lower a hill the procedural world had invented, and every earlier cut only
  reached the rings, which then pulled half the country down by metres on top of a base that
  stayed where it was (the ring log: "142,960 texels lowered over 0.5"). Now the procedural
  land and the overview scale themselves by the same factor, and where the data answers for a
  land cell the base *is* the data (harbours keep theirs). The ring's correction is small
  again and the whole world, not just the near ground, is at the new scale.

### The hinterland
- **Fields only round the settlements and along the roads** between them: the worked land is a
  halo of ~0.75 units past each village's built disc, and a strip within a quarter unit of a
  road; the open country between is no longer cropland.
- **Woods fill the rest**: a clumped cover over every gentle lowland and hillside the data
  does not call crops, bare or snow, so the land between the villages reads as the forested
  Europe of 1620 (the "real forests" ask). Shrubland counts for more.
- **Villages read**: the built disc is a third wider and a village keeps three houses in four
  of its grid cells (was six in ten). The inland settlements, their roads to the port they
  belong to, their mills and pitheads were already there (0.22.3-0.22.6); they stand out now
  because the fields gather round them.
- **Fences off** ("it just looks weird, too much"); the parcel grid is kept for later.
- **Crops smaller and denser**: a wheat clump every 0.019 units at 0.015 (was 0.03 at 0.026).
- **Cows and sheep**: herds of cattle (a new prop) and five times the flocks on the pasture
  round the villages.

### The sea (issue9 "waves from ship should be directional", `wake.png`)
- **The V is the wake.** The diverging arms now root at the stem (the bow wave is their
  beginning), run out at 19.5 degrees as a steep crest twice its old height in a band half as
  wide, with a trough just inboard of each, and carry their wave train along their length; the
  bow's radial ring is smaller. In the fragment stage and the wake map the arms break white,
  denser and narrower, as the reference shows.
- **Foam on the staged crests** like the strategic map's: the `peak` term starts lower on the
  face and at a gentler slope, and the foam colour floor is raised under a dark sky.

### Smoke and clouds (issue9 "clouds more uneven, greyer, slower, longer")
- Gunsmoke: greyer again (core 0.60, plume 0.46, haze 0.42), opaque at birth, 1.6x the life,
  carried at half the wind and rising at two thirds the rate, and the billboards are stretched
  1.35:1 along their spin so no puff is a disc.
- The sky's cloud layer: drift at a third of the old rate, greyer, and its edges torn by a
  second finer noise before the threshold.

### The rig (the video)
- Masts thicker again (`MAST_SLIM` 0.52 -> 0.64).
- The lantina is a tenth longer than the design line, so the head of the canvas never
  overhangs its ends ("too small for the sail").
- **Sheets**: a rope from each clew of every square sail down and aft to the rail (the course)
  or to the yard below (a topsail), and from the lateen's clew to the quarter - the running
  rigging that ties the canvas to the hull ("add rope connecting sails to boat").

### Elsewhere
- **Silhouettes through fog and smoke**: the mesh fog stopped at an 85% mix, so fifteen percent
  of a dark hull or a black rope always survived the haze as an outline - and the cordage is
  drawn at a pixel minimum, so every distant rig stayed etched on the fog. The mix goes to
  100%. The smoke is opaque at birth and lasts longer, so the hulls behind a broadside are
  hidden rather than shown through it.
- **Zoom** on the map: `CAM_MIN_DIST` 1.6 -> 1.0 x scale.

## 0.22.11 — the eighth issues folder: recoil, shot and splashes, a diffusing wake, a blurred battle behind the menu, real forests and fenced fields

From `assets/issues/issue8` (six screenshots and a video, each named for what to change), the
recoil request, five todo rows (15, 17, 19, 21, 26), two physics items (buoyancy, ballistics)
and the hinterland asks (real forests, fenced fields, vines and wheat in 3D).

### Physics (sim/naval_physics.h, P1.3 items 7-9)
- **Recoil** (`nphys_impulse`). A gun throws its shot one way and the ship the other: each
  gun's impulse is applied at the gun's own position as it fires, so a rolling broadside sets
  the hull to leeward *and*, because the guns are off her centre of mass, turns her a little
  as the ripple walks along the side - Newton's third law and the lever arm. The ball's
  momentum (14.5 kg at ~480 m/s) is scaled up six times so the set of a 1000-tonne hull is a
  readable third of a unit a second rather than five centimetres.
- **Multi-point buoyancy** (`nphys_set_surface`, `nphys_attitude`): the caller's wave function
  sampled under the three keel discs and the two bilges every step gives each body a heave, a
  pitch and a roll, relaxed towards over a few tenths of a second (heavier hulls more slowly).
  The staged battle hands the sea's own `height_at` in; the renderer scales a hull's roll by
  what the water under her says. `physics_test`: a flat sea gives no attitude, a wave along the
  keel pitches and does not roll, one across the beam rolls.
- **Ballistics** (`nphys_fire`, `nphys_poll_events`): every shot is a swept sphere under
  gravity, tested each step against every other hull's three discs between the waterline and
  the rail; a hit or a splash comes back as an event and the shot is gone. A hit gives the
  hull the ball's momentum. `physics_test`: a shot at a hull abeam hits that hull and only it,
  a shot at open water splashes, nothing is left flying.
- The guns fire real shot now: `fire_gun` launches one at the target side with a little spread
  and a low elevation; the staged scene turns the events into a column of spray and a foam
  ring (splash) or splinters, dust and embers (hit). The battle HUD counts shot in the air and
  hits made.

### The water (issue8)
- **The wake spreads and fades** (`gfx/wake.cc`, "add fading out and cone spreading and
  disappearing"). The map is a ping-pong now: each frame the new map is the old one blurred by
  a fraction of a texel and multiplied by the decay, so a trail that was a line astern widens
  into a cone as it ages and thins until it is gone. The stamp is narrower and denser - the
  breakup takes the thin fringe away instead of spotting the whole band ("less foam, more
  dense and concentrated").
- **A stern wave** (`sea/vs_common.glsl`, "real generated waves from stern and bow"): the water
  closing behind the counter heaps into a transverse crest a tenth of a length astern, then a
  short train of transverse waves runs down the lane, a hull length apart, fading in three.
- **Bigger swell** in the staged scenes: 0.45 -> 0.60 of the map's.
- **The oil-slick look and the flicker** (the video). Three causes, three fixes: the foam
  tile's dark half was darkening the body (a third of that now); the near water's roughness
  floor was 0.26 - mirror glass - and is 0.36; the glitter was a pow-190 lobe on a normal with
  2.6x the ripple, a pixel-sized sparkle that flipped on and off every frame - it is half the
  ripple, a pow 40-110 lobe with the specular-AA roughness folded in. The screen-space
  reflection is damped and fades out within a few units of the eye, where its march shimmers.
- **Foam on the crests in a fog** ("missing crest and foam on top of waves"): the foam colour
  has a floor, so under a dark sky it stays lighter than the sea; the whitecaps come on from a
  smaller swell.

### The rig and the hulls (issue8)
- **Oars outboard**: with `heading - 90` the oar model's +x lay along the ship's own side, so
  the blade (-x) pointed inboard and the bank lay across the deck - the screenshot. Plus 90.
- **The Alps thicker and less pointy**: the ring ridge's crest was squared (a knife edge every
  ridge), now a 1.35 power with a shoulder, at two thirds the amplitude; the peak boost is
  0.22 (was 0.28) with a soft knee over 2600 m, so a 4000 m summit stands a little lower and
  its flanks catch up.
- **Inside a hull on the map** (the end of the video): the near-camera dissolve is back on the
  map as a tight band - a camera inside a ship still thins it, one looking at a ship never does.

### The menu (issue8 "add ship battle as start menu but blurred")
- A throwaway game is made when the menu comes up, a staged battle is entered behind it, the
  trailer camera moves over it on a 45 s loop, and the tonemap mixes in the frame blurred four
  times at quarter size (`hdr_blur_pass`, `u_blur`), a touch darker so the panel reads. The
  player's pick drops the preview (`detailview_abort`) and starts the real game.
  `MARE_NO_PREVIEW=1` for the old grey menu.

### The hinterland ("real forests", "fence the fields", vines and wheat in 3D)
- **Woods are a canopy**: every forest texel gets a tree and, where the cover is full, up to
  three more on their own jittered spots, so the crowns close and the ground disappears.
- **Parcels are fenced all the way round**: a panel on every cell of both long edges and both
  short ends, on the parcel grid, so a fence runs corner to corner - not a panel on a random
  third of one edge. Only the worked grain parcels (the shader's R >= 60); fencing the pasture
  fringe too laid 241k panels over one lowland ring. The fence itself is weathered pale and
  half its old height, a line on the land rather than a dark net.
- **Vineyards in rows**: bushes at a fixed spacing along lines that follow the contour, one
  row every 0.04 units up the slope, the rows quantised so neighbouring texels agree.
- **Fields full of standing grain**: a clump every 0.03 units on a jittered grid over the grain
  parcels (the shader's R 60-120), not a tuft on one texel in twenty.
- None of the props casts a shadow: the scatter is drawn in the main pass only, as before
  (that was already the case; nothing had to be removed). Instance budget 240k -> 600k.

### The sail state (todo 17)
- `ship_classes.json` takes `"sails": mixed | new | used` per class; the war carrack and the
  great carrack are state navy in new canvas, the fusta is in rags, the rest per ship.

### Weather in the battle (todo 19)
- In a fresh breeze the stems throw spray that the wind carries over the decks
  (`particles_spray`); the map's rain, haze and overcast already reach the staged scene.

## 0.22.10 — the seventh issues folder: rolling broadsides, real foam, sails that are tied to the ship, and a trailer

From `assets/issues/issue7` (`issue.txt`, one screenshot) and the request for a 45-second
battle film.

### The broadside
- **The guns fire in a ripple**, not as one wall (issue7 "not all at once but with a random
  timeout"). `particles_broadside` queues every gun with its own delay - a fifth of them at
  once, the rest strung out over up to ~1.7 s for a 36-gun side, squared so the tail is long -
  and `fire_gun` goes off when each comes due. The ring on the water is laid with the first.
- **More fire**: the muzzle flash is bigger and lasts longer, two tongues of flame race out
  ahead of it (one low along the water), and the burning wad thrown with the shot is doubled.
- **Greyer smoke** again: the core 0.80 -> 0.72, the plume 0.66 -> 0.56, the haze 0.60 -> 0.52.

### Foam
- **The real foam** (issue7: the FOAM folder was corrected and `foam/new` added).
  `tools/foam_tiles.py` is rewritten for what is there now: two foam *masks* rendered over a
  grey checkerboard, a tileable foam photograph, and a labelled sheet of panels. The masks are
  thresholded above the checker's two greys (found as the two histogram peaks) so the checker
  goes to zero and the foam stays; the photograph is gently high-passed; the sheet's streak
  panel is cut out. Four tiles: `foam_wake`, `foam_streak`, `foam_crest`, `foam_lane`.
- **Foam on the collapsing waves** (issue7): the whitecaps and the crest band are textured by
  the foam photograph at two scales, the coarser drifting with the swell, the crest band is
  wider and starts lower on the face, and both weigh more in the final white.

### The rig
- **Sails, yards and masts move as one body with the hull** (issue7 "the lantina and the
  sail should act with the ship as if tied with rope, which it is"). The roll and the pitch
  were applied to each part about its *own* origin, with a first-order vertical fix-up for the
  offset: a sail standing ten metres over the deck rolled about its own foot while the rail
  under it swung sideways, and the two slid against each other every swell. All three vertex
  stages (mesh, depth, cordage) now rotate the whole ship about one pivot - the hull's anchor
  at the waterline - so every part sweeps the same arc.
- **The stipple on the hull** (issue7 `wierd texture over ship rolling.png`) was the 0.22.9
  near-camera dissolve reaching a hull the camera was looking *at*, not inside of. The band
  is tighter (to 1.2 units over the near plane, was 3) and only exists in the staged scenes.
- **Sharper tessellation**: the sea's ceiling 16 -> 24 subdivisions per patch edge, the
  terrain's 32 -> 40 with finer cells.

### The trailer (`tools-bat\trailer.bat`, `tools/blender_encode_mp4.py`)
- **`MARE_RECORD=dir,fps,seconds[,warmup]`** (main.cc): the frame clock is locked to 1/fps
  however long a frame takes, so the film runs at true speed, and every frame after the
  warm-up is written as a BMP before the UI draws. **`MARE_BATTLE_SHIPS=N`** stages an N-ship
  exercise (the line's straight leg grows so the biggest hull keeps a gap of its own length).
  **`detailview_trailer`** is a keyed 45-second camera move - wide over the coast, down the
  line, close on a broadside, up over the fleet, away into the haze - on the recorder's clock.
- Blender's bundled ffmpeg encodes the sequence to H.264 (`blender_encode_mp4.py`), so no
  ffmpeg install is needed. The first film: 44 ships in dawn mist off Venice, 45 s at 60 fps.

## 0.22.9 — the sixth issues folder: a wake that stays behind, blast rings in the water, and the physics foundation

From `assets/issues/issue6` (`issues.txt`, one screenshot) and then dev.txt P1.3 items 1-6.

### The water a ship leaves (issue6 "trail of foam after ship that fades away")
- **The wake map** (`gfx/wake.cc`, `fx/wake_*`). The 0.22.6 Kelvin wake is analytic and bolted
  to the hull, so a ship that wears round drags her whole wake with her and leaves nothing
  where she actually was. There is now a 1024px map of the sea around the staged battle that
  every ship paints her churn into each frame - the cut-water, the shoulders, the boil under
  the counter, the two arm roots - and which fades with a fifty-second time constant. The
  sea's vertex stages read it as water already broken (a little lower, with its own short
  chop) and the fragment stage as foam, so the trail is the sea's own surface and not a decal.
  It curves where she turned, because it was written where she was.
- **The foam tiles** (`tools/foam_tiles.py`, issue6 "in assets/ships/FOAM are some textures").
  What is in that folder is two *sailcloth* sheets, not foam - the same woven and frayed
  hessian the sail tiles came from. Their colour is useless but their tears are exactly the
  structure broken water wants, so the tool throws the colour away, high-passes the luminance,
  **de-stripes** it (subtracting the per-column and per-row means removes the seams and the
  weave, which otherwise tiled across the sea as visible quilting), keeps only the brightest
  quarter - the torn edges - and blurs that into soft blobs. Out come `foam_wake.png` and
  `foam_streak.png`, which break up the wake and the blast rings.

### The broadside on the water (issue6 "shock around ship in waves form")
- **Blast rings are the sea's now** (`sea_set_blasts`, `sea/vs_common.glsl`, `sea.frag`). The
  0.22.8 flat white discs are gone. One ring per broadside runs out from the ship: the vertex
  stage lifts it into a ripple and the fragment stage whitens it, so the swell, the
  reflection and the refraction all still read through it - "still actual sea mesh". It is
  weighted hard towards the side she fired (8% of the strength to windward of the hull, 100%
  abeam of the guns), and a hole in the middle keeps it off the water directly under the
  cannon. Gone in a couple of seconds.
- **The air shock is much lighter**: one per six guns instead of one per three, at a third of
  the strength, so it reads as heat shimmer over the sea rather than a lens on the frame.
- **Greyer, denser, less tidy smoke**: powder smoke is grey, so the core comes down from 0.96
  to 0.80 and the plume from 0.80 to 0.66; four balls per layer instead of three, each thrown
  off the gun's own line; and `fx/puff.frag` warps the *silhouette* with a second coarse noise
  on its own drift, so no puff is a circle and no two are the same lump.

### The rig (issue6)
- **The spritsail** (`front lantent issue sail.png`). It was cut as a tall narrow panel whose
  head stood above the bowsprit and whose foot hung to the water - from the beam, the sliver
  of canvas trailing in the sea the screenshot circles. A spritsail is the opposite shape: it
  is now nearly twice as wide as it is deep, bent to a yard under the bowsprit, foot clear.
- **The near-camera blackout** ("when coming close to rigging ... the whole screen is
  invisible"). Steering the camera into a hull filled the frame with one near-plane-clipped
  triangle. Everything within a shade over the near plane is now dropped and the band above it
  dissolves on a Bayer grid of its own (`dither_clip_at`), in the mesh, cloth and cordage
  pipelines alike, so a part the camera is inside of thins out instead of swallowing the view.
- **A Battle panel** ("add turn off wind arrows button"): wind arrows, stop/run, the four
  speeds and Leave, as real buttons. The keys all still work.

### Physics foundation (dev.txt P1.3 items 1-6, `sim/naval_physics.h/.cc`)
A fixed-step rigid-body world for the tactical scale. The strategic simulation is untouched.
1. **Fixed timestep independent of render FPS**: 120 Hz, an accumulator, at most 8 steps a
   frame. `physics_test` drives the same ship for 8 seconds in 1/30 slices and in 1/240
   slices: 960 sub-steps either way and the same pose to 1e-3.
2. **Tactical naval fixed sub-step**: drive, drag, integrate, ship contacts, coast - in that
   order, every step.
3. **Stable rigid-body transforms**: each body keeps the previous step's pose and `nphys_pose`
   interpolates by the leftover accumulator, so the hulls are smooth at any frame rate. Drag
   is longitudinal *and* lateral, which is what makes a hull carve a turn instead of sliding
   through it; a ship with no way on does not answer her helm; and how hard she leans into a
   turn drives the renderer's roll.
4. **Simplified compound collision hulls**: three discs down the keel, radii from the beam.
5. **Ship-to-ship collision**: broadphase on the bounding circle, then disc pairs, with a
   positional correction split by inverse mass, a restitution impulse, Coulomb friction and
   the yaw the lever arm gives - so a ship struck on the bow swings.
6. **Ship-to-coast collision**: the staged shore profile as a signed distance; a hull disc in
   water shallower than her draft grounds, is pushed back to seaward, loses her way into the
   beach and is slewed by the ground holding that end of her.

The staged battle now runs on it: the formation code only asks for a course and a speed
(`nphys_drive`), and a ship may be shouldered off station or brought up all standing. The
battle HUD reads out the rate, the hull count and the contacts resolved.

### Elsewhere
- `game/tests/physics_test.cpp`, in `build.bat test` and ctest.
- Mirror `C:\projects\mare_oak_20261031_202444`.

## 0.22.8 — the fifth issues folder: layered gunsmoke and shockwaves, textured cordage, masts in proportion, lowland coasts, 800k hulls

From `assets/issues/issue5` (`issues.txt`, three screenshots, the Meshy "cannon fire shockwave"
reference frame) plus four rows of `docs/todo_list.md` (20, 22-25).

### The broadside (particles.cc, fx/puff.*, hdr/tonemap.frag)
- **Three layers of smoke per gun.** A dense white *core* - two cumulus balls that race out
  with the shot and keep rolling, lit hard with a bright crown - a grey *plume* of three puffs
  behind it, and a thin *haze* veil that outlives both (10-15 s) and drifts off downwind. Each
  layer has its own drag, buoyancy and erosion, so the cloud separates into a solid front, a
  trailing body and a smear the way the reference frame shows it.
- **The shock on the water**: under a low gun the blast flattens a ring of sea (a flat annulus
  of foam that spreads and breaks up) and throws a sheet of spray outboard, which falls back
  under gravity and is gone in a second.
- **The shock in the air**: one shockwave per three guns, from the muzzle. The tonemap bends
  the finished frame outward across a thin Gaussian ring that races out from the gun and
  blurs it along the radius - the compressed shell of air - for a third of a second
  (`hdr_set_shocks`, up to sixteen a frame; a frame without a battle draws none).
- Pool 4096, back-to-front sorted, soft against the hulls as before.

### Cordage with the Meshy rope tiles (issue5 "use them also for ropes")
- The geometry stage that widens each rope now writes a rope UV (across the rope, distance
  along it in rope diameters) and the ship's condition; `meshes.frag` lays the `rope_premium`
  / `rope_used` tile along it - the strands' lay, the tar and the fraying on the worn suit -
  normalised to the tile's mean so the rope keeps its colour, with a procedural twist where
  the texels have blurred away. Fades out past 70 units, where a rope is a line again.

### Masts in proportion (issue7.png, todo 20)
- A lesser mast was a *shorter copy* of the same model, so scaling it to 0.8 of the main's
  height also made it 0.8 as thick. The radial slim is now divided by the height ratio: every
  mast on the ship has the main's girth at the deck. Fore and mizzen heights raised
  (`ship_classes.json`: carrack 0.88/1.0/0.92, war carrack 0.90/1.0/0.95/0.86, great carrack
  0.91/1.0/0.97/0.87, hulk 0.88/1.0/0.84).
- **"Remove the lantina from the mast"**: checked in Blender - the mast library's lateen masts
  (`mast_mizzen` = split parts 1-6, `mast_lateen_tall` = parts 1-4) do not contain their yards;
  the lantina is the separate, swinging `lateen_yard` asset. Nothing to split.

### The giant spar (issue5.png)
- A yard, lantina or bowsprit is placed only once its asset is **loaded with real bounds**: a
  part still streaming has zero bounds, and a scale computed from them (`design length /
  extent`) is a spar the size of the fleet. A spar that still comes out longer than the hull
  and a half is suppressed and logged with the asset's name and extents. Not reproduced in
  pinned runs at 6-14 units; the log line is the trap for the next time.

### 800k hulls for the battle view (issue5 "800k models with high texture quality")
- `tools/meshy_import.py --lod0 800000 --tex 2048`: the eight hull classes re-imported with
  LOD 0 at 800,000 triangles (was 300-400k) and the base colour sampled from 2048 px sheets
  (was 1024). LOD 0 is only reached in the battle and city tiers, so the map never pays for
  it. In this renderer the texture is baked into vertex colours, so "texture quality" is
  vertex density: one colour sample every ~6 cm on a 47 m hull. Masts and yards keep 150k.
  The importer also dedupes a tree that holds both the raw Meshy folders and their organised
  copies.

### The strategic map (todo 22-25)
- **Lowland coasts from the data** (the Venice screenshot in issue4). The cliff class and the
  cliff profile were decided by `near_land`, the 7 km *base cell*, which carries the highest
  ground of the cell to the waterline - the Venetian lagoon and the Dutch coast got the
  Dinaric treatment. Where the data answers, the land behind the shore is the data's own
  height; the ridge noise in the coastal fringe is only added where that height is real.
- **Height near the coast**: inside the shore band the relief is the data's height (held just
  above the waterline so land stays land); the base cell's height returns only past the band.
- **Zoom in more** on the map (`CAM_MIN_DIST` 2.5 -> 1.6 x scale) and the look-at point rides
  the ground under it while the camera is close, so the view never passes through a hillside.
- **Dense pine forests**: 80% of forest texels get a tree (was 62%), pines 17% taller, a second
  pine per texel on full-cover conifer ground, conifers from 1.4 units of height or 46 N.

### Elsewhere
- `docs/todo_list.md` gains the issue5 rows (28-34) and a next-steps order.
- Mirror `C:\projects\mare_oak_20261031_180609` (the previous oak mirror removed).

## 0.22.7 — the fourth issues folder: the battle's own clock, the coast from the world, a particle system

From `assets/issues/issue4` (`issues.txt`, four screenshots). The whole list, with priorities,
is `docs/todo_list.md`; this round is its P0 rows: the staged battle.

### The battle clock, the wind
- **A separate battle time** (`staged_scene.cc dv.timescale`, `RF.anim_time`). The staged
  battle runs on its own clock now: Space stops and restarts it, 1-4 set quarter, half, real
  and double time, -/+ halve and double it; the strategic clock keeps whatever speed it had.
  Everything that moves in the scene follows it - the ships, the waves (`sea_update_waves`
  takes the scaled step), the cloth, the flags, the oars and the smoke - because the shaders'
  `u_time` and the shipyard's animation phases read the new scene clock `RF.anim_time`
  instead of the wall clock. The wall clock (`RF.time`) still drives the UI, the eviction
  timers and the cloud transition.
- **Wind arrows in battle mode** (`DetailView::overlays`): a lattice over the water, each
  arrow sliding downwind on the battle clock, none over the stage land; N toggles them.
- **Wind speed at sea battles.** The wind at the battle's position is read once on entry
  (`wind_at`), shown in the battle HUD as knots and a compass point ("wind NW 14 kn, moderate
  breeze") and used by the scene: each side's line sails at the pace the point-of-sail curve
  gives its leader's heading (`wind_factor`, blended 70%), unless the side is mostly under
  oars; the smoke drifts in it.
- **A battle HUD** (`hud_battle`): the clock, the wind, what coast the probe found, the keys.

### The coast, from the world
- **The stage coast is read from the map** (`probe_coast`). Rings out to ~56 km around the
  battle's position find the nearest land, its bearing and distance; a handful of samples
  behind the shore ask the terrain package (`geochunks_sample`) for the land cover and the
  relief - green, wooded, scrub, desert, marsh or snow; lowland, hills or mountains. If the
  chunks were not resident yet the probe retries for a few seconds and rebuilds the stage when
  the package answers. The coast then stands at its real bearing, 140-210 local units off,
  and the camera opens facing it.
- **A heightfield coast** replaces the three quads and nine faceted mounds: a beach profile
  under the water, a beach (sand, shingle on a mountain coast, mud on a marsh), a berm or dune
  line behind it, relief from a noise whose amplitude and slope follow the data (3-6 units on
  a lowland, 34 on hills, 75 with ridges and a snowline on mountains), coloured by the land
  cover with fields on the green, rock on the steep and snow on the high ground, and trees -
  pines north of 46 degrees and on the uplands, broadleaves elsewhere, palms on a southern
  desert shore - dense in the woods, in clumps on the green.
- **Foam and waves crushing on the coast in battle mode.** The staged sea's depth is no longer
  a constant: `sea_local_shore_a/b` carry the shoreline point, its inland normal, the wiggle
  and the seabed slope to the sea shaders (`local_depth_at` in `sea/vs_common.glsl` and
  `sea.frag`), so the swell shoals, the breaker line marches in, the surf sheet and the swash
  run up the sand with the existing code, and the water discards itself over the land. The
  city's harbour gets the same profile along its straight beach.
- **Open sea**: no land within the probe = no stage coast, and the air is thicker
  (`DetailView::weather`: fog density x2.3 from 60 units) so the water fades into the sky past
  the fleets - the "fog of war in the distance". A coast gets a light haze (x1.1 from 160).
- **The camera moves freely up and down.** The look-at point has a height now
  (`cam.ty`, R/F in the battle), so the eye can climb over the fleet and look down the lines
  or sit at deck height and look along the sea; middle-drag / PageUp / PageDown tilt as before.
  The eye is held a wave above the staged water (`detailview_eye_floor`).

### Particles
- **A particle system** (`gfx/particles.cc`, `fx/puff.vert`, `fx/puff.frag`): a pool of
  camera-facing billboards through the mesh instance path with their own translucent pipeline,
  sorted back to front, with wind drag, buoyancy that fades as the gas cools, gravity for the
  embers, size and alpha over life, and soft edges where a disc cuts a hull (the opaque depth
  the water already captures).
- **Gunsmoke that flows with the wind, muzzle flash, embers - no projectile.** Every gun on
  the engaged side (half the class's guns, 1-18) gives a flash, a dense core puff that races
  out with the shot, a looser plume behind it and a few embers. The puff size grows with the
  hull length and the count with the guns, so a sloop coughs and a ship of the line
  disappears behind her own broadside. The smoke is shaded as a volume the cheap way: a 3D
  value noise eroded with age, a sphere normal lit sun-side-bright and lee-side-ambient, a
  back-lit rim where the sun shines through the edge, the noise's pockets as self-shadow.
- The map's gunsmoke (`scene.cc`, the sphere puffs) is unchanged; the staged scene no longer
  uses `mesh_puff`.

### Elsewhere
- `docs/todo_list.md`: every line of issue4 and the P1.3 physics checklist, with priorities.
- The session mirror follows the `mare_<worktree>_<stamp>` rule; the older oak folders are
  gone.

## 0.22.6 — the third issues folder: a data-driven fleet, a wake, reflections, a worked hinterland

From `assets/issues/issue3`. The ocean checklist in `next feature.txt` was answered in 0.22.5;
this round is the rest of that folder.

### Ships
- **The fleet is data now** (`assets/ships/ship_classes.json`, `shipyard.cc classes_init`).
  The Gen I class table - stations, mast heights, sail codes, oar banks, every stat - is read
  from the asset tree at startup; the built-in table stays as the fallback, and the file
  overrides a class by name or appends a new one, so a tree without it still sails. Up to
  `SHIP_CLASS_MAX` classes; the log says how many were overridden and added.
- **Masts** thicker again (`MAST_SLIM` 0.425 -> 0.52) and the **aft masts taller** on the cogs
  and carracks: the coastal trader's mizzen 0.62 -> 0.76, the armed cog's 0.58 -> 0.74, the
  hulk's 0.62 -> 0.78, the carrack's 0.78 -> 0.90, the war carrack's two 0.82/0.68 ->
  0.94/0.82, the great carrack's 0.84/0.70 -> 0.96/0.84.
- **The lantina is bent to its sail** (issue3). The spar is 15% slimmer again, the canvas head
  sits right under it (0.014 -> 0.004 mast heights), and the sail is now *laced* to it:
  `LAT_ROBANDS` lashings along the head, a foot rope from tack to clew, a sheet aft and a tack
  hauled down forward. The whole line is hauled in - the tack from 0.44 to 0.30 mast heights
  forward, the peak further aft and up - so a mizzen's yard no longer reaches into the
  mainmast's rigging ("divide lanteen from front mast").
- **Oars out of the water**: the ports are just under the rail (0.72 -> 1.02 of the deck
  fraction), the blade angle steeper, the dip halved.
- **Two suits of sails and two conditions of cordage** (issue3). `tools/ship_textures.py` cuts
  the Meshy sheets in `assets/ships/sails` and `assets/ships/rigging` into
  `sail_premium_albedo/normal` and `sail_used_albedo/normal`; the panel's own (across, down)
  coordinates are the UV, so the weave, the bolt-rope seams and a used sail's frayed tears run
  along the cloths, with the tile's own normal map giving the canvas a surface. The cordage is
  laid twice per class - pale new hemp and dark tarred rope. Which a ship gets is a stable
  hash of her key, biased by nation: a pirate is in rags, a state navy mostly in new canvas.
- **Head to wind** (`fleet.cc`): a square rig cannot lie closer than about fifty degrees, so
  where the course demands it the ship **falls off** to the nearest bearing she can hold and is
  **set bodily to leeward** while she claws up - which from the deck is the ship going
  backwards. True sternway (a negative step) was written first and measured wrong: this
  pathfinder cannot tack, so a leg into the wind never finished and two years of trade fell
  from 19,700 to 6,800 in `sim_test`. Leeway gives the same picture and still completes a
  passage. Never within two units of a waypoint, and never onto a shore.

### Sea
- **A trailing wake, in 3D** (`sea/vs_common.glsl`): the Kelvin pattern as geometry - two
  diverging crests at 19.5 degrees whose phase runs out along each arm, and the dragged-down
  turbulent lane between them, decaying over about six hull lengths - with its own broken
  foam on the arms and churn in the lane (`sea.frag`). The 0.19 note stands: this is the real
  pattern, not the painted V scar that used to be drawn at every zoom.
- **Reflections** (issue3, dev.txt "Reflections"). A screen-space march against the scene copy
  the refraction already captures: sixteen geometric steps, hit by linear depth, faded at the
  screen edges and with a grazing ray, mixed into the sky term. Hulls and the near shore now
  appear in the water. `MARE_SEA_SSR=0`.
- **Foam on a collapsing wave**: where a fragment stands high in its own wave *and* the
  Gerstner displacement is folding it over, the water is spilling down the face - a band of
  white at the crest rather than over the whole swell.
- **Waves crushing on the coast**: the breaker's phase runs with the depth so the line of
  white marches shoreward, modulated along the shore so it breaks in stretches, and pulsed by
  the forty-second set clock - instead of the uniform white outline every island used to wear.

### Land
- **The spikes.** The ring was taking `max(fine data, coarse cell)`, and the base map is the
  7 km land-mask grid read bilinearly - a lattice of pyramids - so every pyramid that poked
  through the finer field survived as a cone. The ring draws the data's own height now, still
  held to the base's side of the waterline. On top of that the world is 20% lower
  (`GC_HEIGHT_SCALE` 20 -> 16, peak boost 0.35 -> 0.28, `MARE_HEIGHT_SCALE` to taste) and the
  ring's procedural ridge noise is lower again.
- **Forests in stands**, not spread through the fields: a low-frequency clump mask gathers the
  woodland, and cropland excludes it outright. Density on a knob (`MARE_FOREST_DENSITY`).
- **Fields bigger and varied**: the parcel size is drawn per district, so one valley is worked
  in long narrow strips and the next in broad blocks, with more contrast between neighbours.
- **More of the hinterland standing up** (`gfx/scatter.cc`): fence panels along the parcel
  edges, clumps of standing corn in the fields, a windmill beside every farm village, a
  pithead over every mine, and boulders on the bare steep ground (TODO 8c). 
- **LODs for the ambient props** (issue3): every tree is a mesh within 7 units of the look-at
  point and a crossed pair of cards beyond it, split when the ring recomposes so nothing flips
  while the view is still.

### Elsewhere
- `docs/ROADMAP_0.23.md`: what is done and the order for the rest.
- A zero-size render target (a minimised window) no longer ends the run; `u_vp` is declared in
  the sea fragment stage, which the SSR march needs.

## 0.22.5 — the second issues folder: shadows that hold still, a forest, villages, the battle sea

From `assets/issues/issue2` (`issues.txt`, `next feature.txt`, seven screenshots):

- **Terrain flickering close by.** The shadow cascades snapped their centre to texels in world
  x/y/z, which only aligns the grid for a sun along an axis; under the real tilted sun the
  texel grid slid a fraction every frame the camera moved and every shadow edge on the close
  ground crawled. The snap is now taken in the light's own plane (`lighting.cc cascade_matrix`).
- **Sea texture borders / blocks in the shallows.** Water-only terrain chunks were never drawn,
  so under a shelf the refraction saw the bottom in one 64-cell block and the clear colour in
  the next. Chunks within 90 units of the look-at point are drawn whatever they hold
  (`terrain.cc`), so the seabed is continuous under every shallow the camera can see.
- **Tessellation for the close terrain** already exists (8x8 patches per chunk, up to 32 per
  edge, `terrain.tesc/tese`); nothing new was added. The sea's cap goes 32 -> 16 per edge, the
  roadmap's tactical ceiling (`sea_tess_level_max`).
- **Oars** dip their blades: rigid instances may carry a pitch about their own z axis in `fx2`
  (`meshes.vert`, `depth.vert`), and the oar bank sets 17-25 degrees, steeper on the pull.
- **Sea foam whiter and less repetitive**: a third foam sample rotated 37 degrees at an
  incommensurate scale and a slow large-scale patch mask, a whiter foam colour; in the staged
  scenes white sits on every tessellated crest (`peak`), and their wave clock runs 30% slower.
  Waves break on the coast with their own clock (`brk`) and the surf band is wider and
  brighter at the battle zoom; the bow wave is half again as high.
- **Window title** names the worktree: `Mare Liberum / OAK`, from the run directory's
  `mare_<worktree>_<stamp>` name (`main.cc`).
- **Masts** +25% (`MAST_SLIM` 0.34 -> 0.425). **The carrack's aft masts**: the mizzen stations'
  heights go 0.70/0.52 -> 0.82/0.68 (0.84/0.70 on the great carrack, 0.68 -> 0.78 on the
  carrack), the sterncastle lift 0.10 -> 0.14 - and the real cause of the "missing" masts: the
  `mast_mizzen` asset's LOD 4 and 5 are a wrong node a third of the height, and the loader
  drew them. **A LOD whose extent is off by more than 1.5x is now replaced by the previous
  LOD** at its own switch distance (`asset.cc`); the warning stays in the log.
- **Close-by heights** of the map: the rings' procedural ridge amplitude is 40% lower
  (`stream.cc lvl_amp`).
- **A forest** (`gfx/scatter.cc`, a new `forest` byte per ring texel from the export's tree
  cover thinned by fields and villages): a tree on 62% of forest texels within 28 units -
  broadleaves 0.13 units, pines (three cones) above 1.6 units of height, north of 47 degrees
  and mixed in on the uplands - and the orchard trees are half again as big.
- **Villages**: the settlement discs are twice as wide, hamlets twice as likely, and the
  village mask now stands **house props** (walls and a tiled roof, on the shader's own roof
  grid so paint and geometry agree) out to 40 units.
- **3D waves from ships**: the bow wave and hull heap existed since 0.19 and are larger now;
  the trailing V wake stays off by design (0.19 notes: it read as a scar at every zoom).
- **The brown sliver in open water** (`wierd bug.png`, and in this session's own shots since
  0.22.3) was the seabed showing through a gap in the sea: the inner tessellation lattice's
  origin is snapped to its patch grid, so on the camera's side its edge fell up to one patch
  short of the disc the outer lattice cuts out for it. The hole is one inner patch smaller
  (`sea.cc sea_draw_patches`). Found by bisecting with `MARE_PASSES` and `MARE_SEA_INNER=0`.
  On the way, sea texels outside the shore band are clamped to stay 0.12 under the surface
  (`terrain_rings.cc`): the rings' ridge noise could lift a shallow shelf through the waterline.
- The sea's shore bands are thinner from the regional zoom (a 4 km foam rim is not a shore);
  a minimised window's 0x0 render target no longer ends the run (`rhi_gl.cc`).
- `next feature.txt` (the ocean checklist) is answered item by item in docs/TODO.md: the
  patch grid, both tessellation stages, screen-adaptive factors, Gerstner waves, normal maps,
  Fresnel, glitter, foam and the bow wave all exist; the factor table far 1-2 / medium 4 /
  near 8 / tactical 16 is what `u_tess` produces with the new cap.
- `docs/DESIGN_VISUAL_TARGET.md`: the `assets/example` frames read against the renderer -
  what exists, what is a knob, what is a session (finer DEM first, then terrain self-shadow,
  erosion on the rings, canopy displacement, sea SSR).
- `assets/dev_oak_<stamp>.txt` (also `docs/dev/`): dev.txt with every box checked against
  the code and a note on each partial item.

## 0.22.4 — the world package, coasts from the data, the issues folder, the Meshy sea

- **The world-game-package is the terrain** (`assets/data_chunks_terrain/world-game-package`,
  `mare-world-package-v1`): found beside the asset root as `data_chunks_terrain` (then
  `data_new_2`, `data_new`, or `MARE_GEOCHUNK_DIR`), its four LOD levels registered as four
  datasets, the finest answering first. Nothing else in the reader changed; the package's
  format was already read since 0.22.
- **The coastline follows the data** (`terrain_chunks.cc bake_rows`, on by default,
  `MARE_TERRAIN_ISLANDS=0` for the polygons alone). A mask-land cell the overview puts under
  water in all seventeen of its taps (nine inside, eight at the edges) becomes sea; a mask-sea
  cell with land under six of nine becomes land. Harbours are protected, the authored straits
  re-cut, the coast-distance field rebuilt. The Gulf of Trieste opens, Cres and the Kvarner
  islands appear, the Cyclades and the Antilles take their real outlines. The edge taps are
  what keep a peninsula one sample wide - Peljesac at 2.8 km samples - from being eaten.
- **Rounder coasts** (`terrain_rings.cc`). The rings' coast distance is now a 5x5 Gaussian
  blur of the field near the line (sigma ~1.3 cells), cross-faded back to the exact field two
  cells out: a blurred signed distance rounds convex and concave corners alike, so a bay's
  head and an island's sides are curves, where the old min-with-a-blur only cut the convex
  corners. And **the data's own waterline**: within a cell and a half of the raster line,
  where the 3 km sample is decisively on the other side of it, the distance is pushed up to
  two thirds of a cell towards the sample, so the drawn shore (and the sea pass, which reads
  the same rings) follows the data inside the 7 km cell.
- **The issues folder** (`assets/issues`, triage table in docs/TODO.md):
  - *the circle*: every close-range terrain fade (atlas texture, land use, canopy, cliff
    spray, micro-relief, sun occlusion) is gated by the camera's zoom (`u_camdist`) as well
    as the fragment's distance, so from a regional view it switches off everywhere at once
    instead of inside a disc around the camera;
  - *close-by camera shaking*: the eye floor followed the swell under the eye frame by frame
    (and the ring recompose over land); it is smoothed now - rises at once, settles over half
    a second - and over water stands on the swell's amplitude, not the crest (`main.cc`);
  - *masts too thick, the cog's a metre over the deck*: `MAST_SLIM` 0.44 -> 0.34, `MAST_STEP`
    0.14 -> 0.20; *aft sails through the sterncastle*: the aft stations of three- and
    four-masters, with their lantina and canvas, are lifted `CASTLE_LIFT` (0.10 hull heights);
  - *wind in the sails*: the canvas bellies fuller (`cloth.glsl`);
  - *the ship moves as the wind blows*: `wind.cc` has a point of sail - best with the wind on
    the quarter (1.20), a dead run 0.95, a beam reach 0.89, close-hauled falling to 0.18 in
    the eye, where she crawls until the wind veers. Not astern: a route must still complete;
  - *the sea in the battle view*: more of the sky and the ships' light in the surface
    (Fresnel cap +0.17 in staged scenes), and the refraction now absorbs by the **water
    thickness in front of the refracted pixel** (the captured depth linearised with
    `u_zrange`) instead of the column depth, so a hull a metre under the waterline shows
    through while the seabed - or a staged scene's ground forty units down - stays absorbed;
  - a **refraction feedback loop** found on the way: the water sampled the HDR target's *live*
    depth attachment while writing into it, so earlier patches of the same pass handed it
    their own depth and the staged battle's sea came out ruled into sky-coloured blocks once
    the column was transparent enough to show it. The depth is now snapshotted with the
    colour (`rhi_target_snapshot_depth`, `hdr_depth_captured`); the sky keeps the live one;
  - *whitecaps* were silently off since 0.22.3 (`u_capzoom` was never in the sea's uniform
    list); on again, they are masked by the foam texture rather than only tinted by it, since
    the Jacobian they come from is per tessellation cell and drew them as cell-sized blocks;
  - the sea's lapping, surf and cliff-burst bands narrow with the zoom (`u_shorescale`): at
    fixed world widths every close shore was a sheet of milk;
  - the lantina asset was checked (15k vertices, none more than 4% off a straight spar: it is
    cut right); the rest of the list - rigging, React UI, rivers, ship reflections, "waves
    larger than islands" - is answered or asked back in the TODO table.
- **The Meshy sea sheets** (`assets/sea/sea__new`) are the sea's tiles: `tools/sea_tiles.py`
  (pure Python) crops each sheet's frame, resizes to 1024 and cross-fades its edges seamless,
  renormalising the normal map; `sea_normal.png` (ripple normal), `sea_displacement.png`
  (tessellation chop), `sea_albedo.png` (foam, whose luminance drives the whitecaps - cubed
  in `sea.frag`, since the new tile is four times brighter than the old). The renames are
  listed in `assets/sea_src/README_renames.txt` beside the copies in the session mirror.
- `mare.exe --mask=` now shows the data-driven mask; the session's assets live in a per-session
  copy (`C:\projects\mare_<worktree>_<datetime>\assets`) so two sessions never share a file.

## 0.22.3 — the 0.22.2 regressions undone, a blue-grey sea, Peljesac, the hinterland painted

- **The world back at its height, and higher where it should be.** 0.22.2's 17 units per
  9000 m is 20 again (`terrain_chunks.cc GC_HEIGHT_SCALE`), and above 700 m the land gains up
  to 35% more (`GC_PEAK_BOOST`, full by 2600 m), so the Alps stand over the Po plain as a
  range with snow on it instead of a line of brown hills. Lowlands and every coast keep their
  calibration; nothing below 700 m moved.
- **Nothing carves the ground any more.** The 0.22.2 canyons are gone, and the 0.22.1 river
  channels - the 7 km ship-scale beds cut below sea level that opened gorges through every
  range the big rivers rise in - are off by default: rivers are painted on the surface at
  their true hairline width, no reach is navigable, no fleet enters one. `MARE_RIVER_CARVE=1`
  brings the channels back (`rivers_carve_enabled`). The authored river data to come replaces
  the generated network.
- **The sea is blue-grey, shallows included** (`sea/sea.frag`). The 0.22 warm-sea ramp ran
  every Mediterranean shallow through aqua and turquoise; the body is now one slate-to-deep
  blue-grey ramp, the shallows read as shallow because the bottom shows through them (weaker
  absorption, lower alpha, a darker seabed in `terrain.frag` so it is water and not milk), and
  the whitecaps and foam streaks are gated by the camera's zoom (`u_capzoom`): from the
  regional view they smeared into kilometre-long white diagonals across every bay.
- **The lantina is the authored spar again** (`shipyard.cc`): `assets/elm/lateen_yard` as it
  was taken from the import, scaled to the design line and slung at its middle on the swung
  heading, the canvas hanging under it; the procedural spar only stands in past
  `YARD_ASSET_MIN_PX`. Masts are slimmer (`MAST_SLIM` 0.58 -> 0.44).
- **Peljesac is a peninsula and Ston has the sea on both sides** (`data/geo_coast.h`). The old
  stretch was a self-crossing bow-tie that rasterized to a hole in the mainland. The coast now
  runs Split - Omis - Makarska - Gradac - Ploce - Komarna - Mali Ston, out along Peljesac's
  north shore to Loviste and back along the south shore to the Ston channel, then Slano and
  Ragusa. Peljesac is drawn two cells thick (the rings round a one-cell strip to islets) with
  the Mali Ston bay behind it cut by a strait that opens north-west into the Neretva channel,
  and the Ston channel cut from the south-west. The merged Hvar+Brac and Korcula+Mljet blobs,
  which sat where those channels run, are five islands now (Brac, Hvar, Korcula, Mljet,
  Lastovo), each shifted at most a cell so that one water row survives between neighbours.
- **The hinterland, painted** (`terrain_rings.cc`, `terrain/rings.glsl landuse_at`,
  `terrain.frag`; TODO 7a/7b/7c/8b). A fourth ring texture carries four masks baked on the
  ring's grid from the export's land cover and slope, the region's goods, the roads and the
  settlements: fields (cropland on gentle low ground, thickest round villages and along
  roads), vineyards (wine regions and the Mediterranean belt, sunny slopes, in patches),
  orchards round the villages and olive groves across the belt's slopes, and villages (every
  inland settlement's built disc plus roadside hamlets). The shader paints them as patterns in
  one orientation per 1.5-unit district: hedged strip parcels each with its own crop and
  furrows, vine rows along the contour between dry-stone walls, a jittered grid of lit crowns
  over grass or karst soil, roofs on a loose grid with yards between; each collapses to its
  average colour from the regional view. Woods are a canopy now - crowns as a bumpy noise with
  a tilted normal - instead of flat dark green.
- **Static props, no shadows** (`gfx/scatter.h/.cc`, `MARE_SCATTER=0`): low-poly orchard and
  olive trees one per 0.3 units of the district grid inside the groves, vine bushes along the
  contour in the vineyards, the odd flock of sheep on thin pasture - placed from the near ring's
  land-use texture when it recomposes, drawn in the main pass only, never in the shadow or the
  staged scenes. Trees are at the ship's scale (a fifth of a hull), since a tree the size of a
  texel is a pixel from any camera that shows a field.
- **Thin coasts in the fold** (`terrain_chunks.cc bake_rows`). A mask-land cell whose centre
  the overview puts in the sea - a peninsula or island a cell wide that slips between the
  cell centres, which is exactly Peljesac at 5-7 km against a 7 km cell - used to keep the
  procedural beach ramp; it now takes the highest land under any of its taps, out to the
  neighbouring cells' halves. Cells the data already puts on high ground are unchanged. Not
  enough on its own: the rings' shore profile still caps a two-cell strip at under half its
  relief, so Peljesac draws as a low sandy peninsula - see KNOWN_ISSUES.
- `mare.exe --mask=lon0,lat1[,wcells,hcells]` prints the land mask and the folded base height
  of a window as ASCII (headless): the way to check a coast edit without a screenshot.
- The build mirror convention is now `C:\projects\mare_<worktree>_<datetime>` per session
  (docs/TODO.md session notes).

## 0.22.2 — the tree tidied, a lower world, canyons, roads

- **Files named for what they are.** The framework's `v3.c/.h` moved from the repository root
  into `game/src/platform/` (its build script to `tools/v3_framework.sh`), `game/mare.cc`
  became `game/src/main.cc` and loses its `src/` include prefixes, and the terrain and scene
  modules whose names described their history rather than their job were renamed:
  `geochunks` -> `terrain_chunks`, `terrainmap` -> `terrain_overview`, `streamview` ->
  `terrain_rings`, `chunkatlas` -> `terrain_atlas`, `detailview` -> `staged_scene`,
  `terrain/detail.glsl` -> `terrain/rings.glsl`. Build scripts, CMake, tests and docs follow.
  C symbol prefixes are unchanged.
- **A lower world.** 9000 m now maps to 17 world units instead of 20; the sea calibration is
  untouched, so every shallow-water gate and shore threshold keeps its meaning.
- **Canyons.** Where a sizeable river runs through high ground it has cut a gorge: a flat
  floor with the stream on it, walls rising within a quarter width, deeper the higher the
  land stands (`terrain_rings.cc`). The walls are written into the shore mask's cliff
  channel, so they are painted as bare rock with scree at the foot - the sea cliffs' material.
  Brooks keep their shallow bed.
- **Roads** (`world/roads.h/.cc`, TODO item 6). Inland settlements are routed to the hub port
  of most influence in their region, and a region's hubs are chained, by an A* over the land
  mask whose step cost grows with the slope crossed, forbids water, and halves along cells
  already carrying a road - so farm tracks run together into one road to town. Class (track,
  road, highway) follows the region's `traded_today / throughput` and the network is rebuilt
  every 45 simulated days, so a road upgrades as the inland trade towards the port grows.
  Painted through the ring texture's A channel (previously an unread width) as packed earth;
  a dark line in the map modes. Where a route crosses a river a **bridge point** is recorded
  (`road_bridges`) for the prop pass. Seed 7, day 30: 46 roads, 57 bridges, 81 ms.
- The hinterland work is broken into small items in docs/TODO.md: scatter framework, trees,
  rocks, fields, vineyards and orchards, villages, bridges, road upgrade visuals, micro-detail.


## 0.22.1 — the rig seated and slimmed, Istria, round islands, rivers you can sail

- **Masts are poles again, and they stand on the deck.** The generated mast assemblies are
  chunky for their height and a uniform scale cannot thin one without shortening it, so
  `inst_t` gained a **radial slim** (`gpu.h`, the long-declared and never-used `xlen` field
  repurposed): it scales a model's x and z only, applied by `meshes.vert` and `depth.vert`
  alike so the shadow follows. Masts take 0.58 of it. They are also **stepped into** the hull
  (`MAST_STEP`) instead of stood on the `deck` fraction - that fraction is a per-class guess
  and where it guessed high a cog's mast floated a metre over its own planking. A mast is
  stepped on the keelson in any case, so burying the heel is both right and the safe direction
  to be wrong in. Only parts whose long axis is y may slim: a yard or a bowsprit lies in x or
  z, and thinning those would shorten them.
- **The lantina is drawn procedurally** and is one width from tack to peak. The authored
  `lateen_yard` is a scan whose two ends are visibly different spars, and its far LODs
  decimate the thin diagonal to a sliver; the procedural bar has neither problem, costs
  nothing extra and stands at every distance.
- **Canvas out of the hull.** The lateen's design line put its tack 0.56 mast-heights forward
  of the mast and 0.08 above the deck, which on a galiot threw the fore end past the stem and
  swept the sail *through* the bulwarks whenever it was swung. The line is shorter forward and
  carried higher (tack 0.44 / 0.15), the foot is sheeted well above the rail, and the sail is
  cut a little smaller - which is also how a settee yard is really slung.
- **Sails hold their wind.** The canvas keeps a round even in a light air (the fullness floor
  went 0.30 -> 0.45) and bellies deeper when it draws (0.42 -> 0.52), as the reference sheet
  draws it.
- **Istria exists.** The Europe polygon ran straight from Trieste to the Kvarner, which cut the
  whole peninsula off and left Pula and Rovinj under water. It now traces the west coast down
  past Poreč and Rovinj to Pula and back up the eastern shore to Rijeka. **Pelješac** is
  likewise drawn as a peninsula instead of a straight run of coast.
- **Round islands.** Every Mediterranean island was authored as an axis-aligned rectangle -
  which is exactly what read as a square island. All **44** of them (the Dalmatian chain, the
  Ionians, the Aegean, the Cyclades, Menorca, Ibiza, Malta, Madeira, the Azores, the Antilles)
  are now 12-point ellipses inscribed in the same bounds, radii widened 6% so the land area -
  and with it the navigation grid's majority-land rule and `raise_small_islands`' component
  size - is unchanged.
- **The black Adriatic, fixed.** The terrain sun occlusion added earlier in 0.22 marched forty
  world units - the better part of three hundred kilometres - and darkened by 92%, so with a
  low sun the Dinaric range put the entire northern Adriatic in a hard black shadow. It is now
  a short *local* contact shadow (eight taps, gentler falloff, 45% at most) with a light floor
  on water, which is all the exaggerated relief can honestly support. The **caustics** were
  clamped too: they multiplied the shallow seabed by up to 3.4 and turned every shelf into
  bands of white.
- **Rivers you can sail** (`world/rivers.*`, `gfx/terrain_rings.cc`). Each river *point* now
  carries its own navigable class from the catchment there - the Danube is a channel at
  Belgrade and a stream in the Black Forest - and a navigable reach is cut as a real channel
  whose **bed goes below sea level**, so the sea pass fills it and a hull floats in it. Width
  is at ship scale (`RIVER_W_DEEP` / `RIVER_W_NAV`): a 25 m cog is 1.1 world units here while
  a world unit is ~6.9 km of ground, so a river at its true width could never float one, and
  "sail up to Vienna" has to mean a channel the ship fits inside. The same river higher up
  stays painted on the ground - a bed forced down to sea level through a mountain would be a
  canyon - and streams keep their hairline.
- **UI toolkit question answered** in `docs/DESIGN_UI_TOOLKIT.md`: not React (a browser engine
  in the frame loop costs a full-screen copy or a 5.7 MB upload per repaint, ~200 MB of
  binaries and an IPC seam for every live value), but RmlUi - native HTML/CSS subset rendering
  through the existing RHI - if the player HUD is to be designed rather than laid out in code.
- Verified on screen: the staged battle close and wide, Istria, Pelješac, the Cyclades, the Po
  channel; headless suites green.


## 0.22.0 — the terrain-v2 chunks are the world, biome masks, shores the sea breaks on

- **One terrain source, the exporter's.** `assets/data_new/*` (format `mare-terrain-v2`,
  `MARE_INTEGRATION.md`) is now the only authored terrain the game reads, and it reads all of
  it: `height.r32` and `coverage.r8` as before, plus `biome.r8`, `biome_source.r8`, `splat.png`
  and `normal.png` per chunk, every map validated by length or dimensions before use.
  `world/geochunks` takes **several datasets at once** - the 3 km world overview and any finer
  regional build dropped beside it - sorted finest first, honouring `longitude_wrap`, with the
  class table from the manifest; every sampler answers from the finest resident dataset, so a
  30 m Dalmatian build sharpens that coast and changes nothing else. IO lane reads, DECODE lane
  unpacks the PNGs, MAIN publishes; 160 MB budget (`MARE_GEOCHUNK_MB`), `MARE_GEOCHUNK_DIR`
  points at a dataset or a folder of them, `MARE_GEOCHUNKS=0` turns it off.
- **Every earlier cache of the same data is gone**, on disk and in code: the v1
  `assets/data/export-global`, the u16 cuts (`assets/terrain/tiles`, `assets/elm/terrain/tiles`,
  `tiles-aimap`, `tiles-gebco40`, `gebco`, ~1.6 GB), the fine Mediterranean cut the height atlas
  read, the procedural `.mht` ring caches, `world/terraintiles`, `gfx/heightatlas`,
  `terrain/atlas.glsl` and `tools/terrain_{from_gebco,tiles,calibrate}.py`. The GEBCO GeoTIFF
  source cache (`assets/data/cache/gebco`, 7 GB) stays: the exporter needs it to cut a finer
  regional build. The geography bake reads the overview (3,915,031 cells in ~150-300 ms).
- **The chunks are drawn** through the existing path - height on the two clipmap rings, the
  tessellated 8x8 patch lattices per chunk amplifying them - and their material maps through a
  new **GPU chunk atlas** (`gfx/terrain_atlas.h/.cc`, `terrain/chunks.glsl`): 4x4 slots of 513
  texels in three RGBA8 atlases (class / splat / normal, 48 MB) plus a slot table, paged one
  chunk a frame, addressed with the spec's vertex-aligned texel rule, class nearest, the rest
  bilinear, no mipmaps. Metrics `terrain.chunks_*`, `terrain.atlas_slots`; `MARE_CHUNK_ATLAS=0`.
- **Biome material masks** (`terrain.frag`): the export's splat weights (grass / rock / sand /
  snow) and its land-cover classes - trees, shrubs, grass, crops, built-up, bare, snow, water,
  wetland, mangroves, moss - are the ground's masks wherever a chunk is resident, the
  procedural rules standing in elsewhere. Classes are read at the four surrounding samples and
  blended, then the edge is broken by noise, so two covers meet along a ragged line rather than
  a 3 km step: woods where the data has tree cover, strip fields and ploughland on cropland,
  roof-and-stone mottle on built-up ground, karst from the rock weight, ergs from high dry
  sand, mud flats and reed on wetland, dark mangrove, a still glossy surface on permanent
  water. The export's **normal** (east / up / south, true-metre slopes scaled by the world's
  15.5x vertical exaggeration) lights the ground wherever the rings are coarser than the data,
  i.e. outside the near ring.
- **Shores the sea breaks on.** Each ring now also carries a **shore mask** (RGBA8: cliff,
  beach, marsh, exposure), built in the same compose from the chunk data read one cell inland
  of the line - the export's slope and rock weight make a cliff, its sand weight and a gentle
  slope a beach, its wetland and mangrove classes a marsh - and the shore profile follows it:
  a strand 1.6x wider on sandy coasts and half as wide on steep ones, a rock face with deep
  water off it, flat mud running far out of a marsh. Convex corners of the land-mask raster are
  **rounded** (the minimum of the coast-distance field and a 3x3 blur of it, so the coast only
  ever recedes, by under half a cell, never fills a bay and never moves a harbour), which is
  what turns the two-cell Adriatic islands from boxes into rounded islets. The water meets each
  coast its own way: waves come in **sets** (a forty-second swell), the surf sheet runs further
  up the sand with each set while the terrain's wet band and foam line follow the swash, each
  set's wave bursts white at the foot of an exposed cliff and the rock throws spray up the face
  in streaks, and a marsh takes no surf at all.
- **The world package** (`assets/data_new_2/world-game-package`, format
  `mare-world-package-v1`, HOW_TO_USE_DATA.md) is read as well: its four LOD levels (513 / 257 /
  129 / 65 samples per chunk) register as four datasets of one bbox, so a coarse level answers
  while the fine one streams and the far ring can draw from it, its `coast.r8` classes (sand
  beach, rock cliff, rocky coast, coastal water) decide the shore mask where they speak, and the
  reader takes any sample count - the 257-sample regional Dalmatia export the guide describes
  loads the moment it is dropped beside the package. Both `data_new_2` and `data_new` are
  probed; every dataset found is registered, finest first.
- **Smooth ground.** Heights are reconstructed with a Catmull-Rom spline over the 4x4 samples
  around the point instead of bilinearly: the spline passes through the samples (peaks and the
  coastline stay where the data puts them) and is C1 between them, so the 3 km grid no longer
  shows as creases on every slope. The data's author asked for sharp edges to be avoided; the
  convex-corner rounding of the raster coast and the ragged class edges (above) are the rest of
  that answer.
- **Camera tilt.** The pitch is no longer fixed by the zoom: middle-drag up and down,
  PageUp / PageDown or the Options "camera tilt" slider add up to -60 degrees so the view can go
  down to the horizon and see the sun; Home levels it. The far plane follows the pitch (out to
  ~4400 units when level) so the world no longer ends in a hard line when looking out to sea;
  `MARE_CAM_TILT` pins it for screenshots.
- **Light over the sea at every distance.** The shadow cascades reach 1400 units (was 650), the
  sun's glitter and the caustics follow the camera distance instead of fading at a fixed 400
  units (the ripple normal maps keep their reach - at the regional zoom they alias), and
  **terrain sun occlusion** (`det_sun_occ`): ten taps marched towards the sun through the
  heightmap and the rings put valleys and the water at the foot of a range in the mountain's
  shadow, on the land and on the sea alike - the shadow atlas only ever held meshes. The sea's
  refraction gained **caustics** on the shallow bottom.
- **Map view modes** (HUD "View" button, `MARE_MAP_MODE`): terrain, depth (a hypsometric sea
  with a contour every quarter unit, read per fragment; height tints and contours on land),
  religion (the owner's faith - a historical table per nation - or the period's map where no
  one rules), political (nation colours with drawn borders), influence (how far each port's
  reach carries, as heat in the nation colour; the new `influence` byte beside `territory`) and
  production (each region's signature good by value-weighted potential, brighter with
  development, from a 256x1 region-info texture). Borders are read through a half-texel noise
  jitter so they wander like a drawn frontier instead of stepping along the half-degree grid;
  the sea dims to a flat grey-blue under the thematic modes.
- **Rivers** (`world/rivers.h/.cc`). The drainage of the base heightfield: a priority flood from
  the sea over every land cell (so depressions fill and every cell has a downstream neighbour),
  one unit of rain per cell accumulated down the flow, river wherever more than 45 cells
  (~2200 km2) drain, traced from each head to the sea or into the river it joins. The 7 km
  cell path is Chaikin-smoothed twice and given a small meander so it reads as a river, not a
  staircase, and the half-width (0.3 .. 1.7 km) follows the catchment. The clipmap rings carve
  the channel into the ground (a flat bed, banks sloping back over one and a half widths,
  9 .. 27 m deep before the exaggeration, fading into the shore profile at the mouth) and write
  a **river texture** (water, flow direction, width) the terrain shader paints: a dark
  green-blue surface reflecting the sky, ripples drawn along the flow and carried downstream by
  the clock, a glossy roughness; a blue line in the map modes. **Navigable reaches**: cells
  draining over 420 land cells are nav-grid water of class 2, over 1400 class 1, and a fleet
  enters as deep as its biggest hull allows - sloops and fluyts the upper reaches, merchantmen
  and frigates the estuaries, galleons and liners stay at sea (`nav_river_limit`, set around
  each fleet's path search and left at 0 for the port-distance rows, so the trade distances are
  unchanged). Deterministic: a pure function of the heightfield, no rng.
- Verified on screen: the Dalmatian coast at 60, 12 and 40 units, the Aegean, the western
  Mediterranean at 320 and 600 units, the horizon from over the Adriatic with a low sun, the
  depth / political / production modes, the Po valley and the Nile; headless suites green.

## 0.21.0 — Vulkan draws, DX12 wakes up, the discrete GPU, an Options panel, the last headers become classes

- **The game was running on the wrong GPU.** On a hybrid laptop the driver hands an OpenGL
  program the integrated chip unless the executable asks otherwise; the two exported symbols
  that ask for the discrete one (`NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`)
  were missing. Measured over the Dalmatian coast at 1600x900 with the sea clock frozen:
  **52.7 fps on the Intel iGPU, 202.7 fps on the RTX 5070 Ti - 3.85x** (mare.cc).
- **Vulkan draws** (`gfx/rhi_vk.cc`, T8). Rebuilt against the real SDK headers with
  `VK_NO_PROTOTYPES` and every entry point fetched from the loader at run time, so the game
  still links nothing and still starts without a driver. Two self tests run at bring-up and
  are reported in `rhi_info()`: the **transfer** path (host-coherent allocation mapped, copied
  on the graphics queue, fence-waited, read back byte for byte) and the **render** path (an
  offscreen image, a render pass, a graphics pipeline built from SPIR-V, a triangle drawn and
  the pixels checked for the interpolated gradient). Both pass on an RTX 5070 Ti. The SPIR-V
  is committed (`gfx/vk_selftest_spv.h`, generated by `tools/vk/spv2h.py`) so no build needs a
  shader compiler. Presenting still belongs to GL - two APIs cannot own one window surface -
  so drawing is delegated until the passes themselves are ported.
- **DX12 is a real bring-up** (`gfx/rhi_dx12.cc`): DXGI adapter enumeration preferring the
  high-performance GPU, `ID3D12Device` at the highest feature level it accepts, and a direct
  command queue. Reports "NVIDIA GeForce RTX 5070 Ti, 11856 MB dedicated, feature level 12.2".
  `d3d12.dll`/`dxgi.dll` are opened at run time; without the Windows SDK the backend compiles
  to a stub that says so. Both backends build everywhere: the headers are detected by
  `build.bat`, `build.sh` and CMake, and their absence is not an error.
- **An Options panel** (`ui/options.h/.cc`, new `mare::ui::Options`) with sliders for shadow
  distance, anisotropic filtering, model detail (LOD bias), wave height, exposure and bloom,
  checkboxes for shadows, tessellation and ambient occlusion, and a renderer choice. Settings
  persist to `mare_options.cfg` beside the executable and are read before the device is
  created, so the backend choice survives a restart; the environment variables the measurement
  scripts set still win where present. Verified by A/B: shadow radius 113 at 0.5x against 452
  at 2.0x, draw calls 93 with tessellation off against 111 with it on.
- **geochunks finished**: the one-shot geography fold now reads the export's float32 chunks
  directly instead of the u16 cut (`geochunks_bake`), writing 3,925,398 cells where the cut
  managed 2,205,872, and it folds **on the job pool** - 1019 ms down to **251 ms** for an
  identical result.
- **The last header-only modules became classes.** `data/tables.h` and `assets/gjson.h` held
  `static` arrays and a whole JSON parser *in the header*, so all forty translation units
  carried a private copy of each; both now have one definition in a `.cc` behind
  `mare::data::Tables` and `mare::assets::Json`, with the names every call site uses unchanged.
  `core/util.h`'s loose helpers moved into `mare::core::Math / Geo / Rng / Calendar / Colour`
  (`core/util.hh`), the old names forwarding one line each. `game_entities.cc` became
  `mare::game::EntityRegistry`. Two methods had to be renamed away from `make` and `next`: the
  v3 framework defines those as function-like macros, and a colliding method expands into
  nonsense in every unit that sees both.
- Backend selection moved out of file statics into `rhi::BackendSelector`; `build.bat` now
  compiles `src/data` too.

## 0.20.0 — the export read as chunks, a self-measuring height atlas, Vulkan's memory and command path

- **The GEBCO export is read as the chunks it is** (`world/terrain_chunks.h/.cc`, new
  `mare::game::world::GeoChunks`), which is what `assets/data/how_to.txt` asks for and the game
  was not doing. It streams `chunks/{y}/{x}/height.r32` - 513x513 little-endian float32 **metres** -
  with its `coverage.r8` mask, derives each chunk's bounds from the manifest and checks them
  against the shipped `tile.json`, and samples bilinearly. Previously the game only ever saw a
  u16 resample of this export (`tools/terrain_from_gebco.py`); now the source is read directly,
  the coverage mask is honoured as "unknown ground" rather than guessed at, and the exported
  edge agreement (verified at 0 m in the source, and re-verified here) is kept by construction.
  Decoding uses the identical land/sea calibration the u16 cut decoded to, so every shading
  threshold, wave gate and depth fade keeps its meaning. IO lane reads, MAIN lane publishes,
  96 MB budget, `MARE_GEOCHUNKS=0` / `MARE_GEOCHUNK_DIR` / `MARE_GEOCHUNK_MB`.
- **Three height sources, best first** (`gfx/terrain_rings.cc`): the fine regional set, then the
  GEBCO chunks, then the u16 cut. Measured on the Adriatic: the near clipmap ring takes all
  262144 of its texels from the fine set, and the far ring mixes 80250 from the chunks with
  the cut where neither reaches. Each compose logs its source split at debug level.
- **A height atlas that measures whether it is worth existing** (`gfx/heightatlas.h/.cc`,
  `terrain/atlas.glsl`): resident fine tiles held on the GPU as one atlas with a slot lookup,
  sampled by the terrain's vertex and tessellation stages for the detail the clipmap ring is
  too coarse to carry - reconstructing what the ring holds and subtracting it, so the layers
  sum to the data exactly once. It then **probes the data and switches itself off**, because
  the honest measurement is that there is nothing to add: the residual below the ring texel is
  0.0016 world units rms and 0.031 at worst against relief of 0.7-4.1. At 16 units per degree
  the ring texel is ~1.1 km and the fine set's samples are ~620 m, so the ring is already
  within 1.8x of the data. **The near ground looks smooth because of the data, not the
  renderer.** The machinery stays, self-measuring, and turns itself on the day a real
  high-resolution DEM is dropped in (`MARE_HEIGHT_ATLAS=1` forces it on to see the residual).
  Anything it does contribute is clamped so it can never cross the waterline the ports and the
  navigation grid were placed against (`ha_shape`).
- **Vulkan: the memory, command and queue layers, proved** (`gfx/rhi_vk.cc`, T8 step two). The
  backend now allocates buffers, picks a memory type, maps host-coherent memory, creates a
  command pool and buffer, records a transfer, submits it to the graphics queue, waits on a
  fence and reads the result back byte for byte. On an RTX 5070 Ti: 64 KB round-tripped with
  no mismatches, 0.11 ms submit to fence. Pipelines and render passes still wait on SPIR-V -
  there is no Vulkan SDK or glslang on this machine - and drawing stays on GL.
- **RHI**: sub-rectangle texture upload (`rhi_texture_update_rect`) through the device
  interface; backend selection moved out of file statics into `rhi::BackendSelector`.
- **No geometry shader for the terrain, deliberately.** At 16 world units per degree one unit
  is ~28 km, so scatter geometry - the usual reason to reach for the stage on terrain - would
  be kilometres wide and is meaningless at this scale. Tessellation remains the right
  amplification for the ground (as 0.18 already concluded), and the one place a geometry stage
  genuinely earns its keep is the rig's cordage, where it has been doing per-primitive
  screen-space expansion since 0.19. Vegetation belongs in the staged scenes, where a unit is
  about 2.8 m.

## 0.19.0 — the authored rig, hemp not poles, flags that fly, a shore that follows the Earth, a device behind the RHI

- **Authored yards on every station** (`gfx/shipyard.cc`): the lantina (`assets/elm/lateen_yard`,
  the Meshy part-segmented 45-degree spar slung at its middle) stands on every lateen station and
  the split square yards (`yard_cog_*`, `yard_carrack_*`, `yard_great_*`, generic `square_yard`)
  on every square layer, each instanced on the sail's own swung or braced heading so canvas and
  spar turn to the wind as one piece about the mast. The unit lateen canvas is cut to the asset's
  line (`LAT_TACK_*`/`LAT_PEAK_*`), so it hangs on the authored spar by construction. Below
  ~44 px of projected spar the sail's own procedural spar takes over (a decimated thin spar is a
  sliver), so the yard exists at every distance. The yard descriptors are generated
  (`tools/gen_asset_descriptors.py`: forward +z, fit *width* 1, pivot centre - the old
  `square_yard` descriptor fitted the wrong axis and came out 18x long), and the eight split
  yards were re-exported with full six-level LOD chains (`tmp/s19tools/yardlods.bat`).
- **Cordage is hemp, not poles**: ropes are line segments the new **geometry stage**
  (`meshes/rope.geom`, GL 3.2) widens to their *physical* size on screen - a 4 cm shroud - with a
  0.6 px floor, lit as a cylinder; they were fixed-width quads that came out 37 cm thick in the
  staged battle and read as black spars lying across the ship (the "lantinas in the wrong place"
  of the issue screenshot). Tarred-hemp brown instead of near-black; the shrouds anchor at the
  class's own beam so no scan's rail puts them in the sea. `MARE_GEOM=0` or a context without a
  geometry stage falls back to thinner cross quads.
- **Flags fly**: a verlet cloth per ship (9x5 particles, pinned along the hoist to the main
  masthead, in the ship's frame so the mesh shader's roll, pitch and swell carry it) driven by
  real gravity for the class's length and the apparent wind - true wind in the ship's frame
  less her way - with gusts; the mesh is rebuilt per frame through the new
  `rhi_geometry_update`. The nearest 48 rigged ships are simulated, LRU beyond.
- **Materials**: canvas with cloth seams, head and foot tablings, a weave micro-normal, a
  grimier foot, *transmission* (the sun behind a sail lights it from within, shadowed) and a
  fuzz sheen; water with crest scattering (the green-turquoise glow of light through a wave
  seen against the sun); ground with per-material roughness (matte grass, tighter rock, soft
  snow, wet dark-glossy rock in the splash zone) and a close-range micro-relief normal.
- **Coast polish** (`gfx/terrain_rings.cc`, `sea/vs_common.glsl`): inside a band around the base
  raster's waterline the clipmap rings may now move the shore - where the authored GEBCO map is
  decisive its side of the waterline wins (a sea cell never shallower than -0.30, a land cell
  never under 0.20, so the sea's shallow shading keeps its calibration), elsewhere the raster
  corners are box-blurred and a texel-aware fbm wiggles the contour - and in the coastal fringe
  the authored relief replaces the 0.17 cliff field on land; everything fades out around every
  harbour. The **sea reads the detail rings too**, so the water's edge is the detailed shore
  and not the 7 km cell steps. Presentation only: the simulation never reads the rings. Ring
  composes log their delta range at debug level.
- **`rhi::Device`** (`gfx/rhi_device.hh`, T4): the abstract render device; `GlDevice` implements
  it, the `rhi_*` C functions are shims onto the active one (`gfx/rhi.cc`), chosen by `--rhi=`
  / `MARE_RHI`. **Vulkan bring-up** (`gfx/rhi_vk.cc`, the first slice of T8): loads the loader at
  run time with no SDK, creates instance, physical device (discrete preferred), logical device
  and graphics queue, reports adapter, API version and device-local memory; drawing is delegated
  to GL until the SPIR-V/swapchain/pipeline work lands. `rhi_dx12.cc` is the API-neutrality
  stub. The RHI also gained `lines` geometry, an optional `gs` stage and dynamic vertex updates.
- Shipyard shadow submission split (`shipyard_submit_shadow`): canvas and spars cast, line
  ropes and flags do not; `mb_line` in the mesh builder.

## 0.18.0 — full C++20, the real Earth, a living rig, tessellated ground

- **Full C++ conversion**: all 40 game modules are C++20 classes in `mare::` namespaces
  (docs/CPP_CONVERSION.md); the v3 framework is the one remaining C translation unit behind
  `src/platform/v3cpp.hh`. The C ABIs survive as shims; sim_test's bit-deterministic run is
  identical through them. mare.c is retired for mare.cc.
- **Entity lifecycle + C++ core (earlier sessions)**: `mare::core` diagnostics and worker
  pool as classes; `mare::engine` Entity with on_load_async/on_load/on_update/on_render/
  on_unload hooks, weather as the first entity; every GLSL shader extracted to
  `game/assets/shaders/**` behind `engine::ShaderLibrary`.
- **The real Earth**: GEBCO bathymetry/elevation cut into the streamed tile set
  (tools/terrain_from_gebco.py; sea sqrt-compressed so shelves stay in the band the shading
  expects), plus a second *fine* regional set (`height_fine_*`, Mediterranean at ~520 m from
  mare-world-terrain) the loader prefers where resident. Overall height scale lowered
  (TT_HEIGHT_SCALE 28 -> 20) with relative detail kept.
- **Tessellated terrain** (GL 4.0, `MARE_TERRAIN_TESS=0` reverts): chunks draw as 8x8
  lattices of 4-point patches, subdivided by distance up to 32 per edge with watertight
  matched edge levels - near ground resolves the fine tiles and detail rings past the old
  grids' density. No geometry shaders by choice: tessellation supplies the amplification.
- **A living rig**: per-type authored mast library (cog/carrack/great-carrack/mizzen/lateen
  + bowsprit); square yards and the lantina are part of the swinging unit sail meshes -
  canvas hangs exactly on its spar, both trim to the apparent wind (lantina up to ~70
  degrees), bellies are fuller downwind, and canvas blown aback clings to its mast and yard.
  Every ship part is rigid to its hull (`i_anchor`: one swell, one roll, one pitch per ship).
- **Ship wood & sea**: grain-driven plank albedo and gloss, a wet waterline band; the sea
  body darkens exponentially past the shelf; shallow shelves damp the swell hard
  (squared depth fade) and the strategic view only keeps a small readable swell in its
  zoomed-in band - no more ten-metre map waves.
- **Builds**: every successful build is archived under `tmp/builds/<root>-<datetime>`.

## 0.17.0 — a world twice as wide again, 16 cells per degree, ridged relief, textured ground, warm shallows

- **WORLD_SCALE 4** (16 world units per degree): the map is twice as wide as 0.16, so Istria,
  Dalmatia and Ragusa are places rather than pixels. Every gameplay distance that had not yet
  been expressed in `WORLD_SCALE` now is (siege approach, pirate threat radius, expel offset,
  patrol radius, settlement harbour reach, battle gathering), so the rules are what they were.
- **CELLS_PER_DEG 16** (2816x2080 base grid): coasts and islands rasterize at half the old
  step. The navigation and territory grids keep their cell size (`NAV_DIV` 4, `TERR_DIV` 8) and
  the stream tiles their geography (`STREAM_L0_CELLS` 1024, an L3 chunk is still 8 degrees);
  port town placement, the occlusion march step and the AO bake follow the grid. The
  heightfield is computed on the job pool (131 ms for 5.9 M cells).
- **Relief** (`world.c`): a domain-warped *ridged* field - crests in lines, steep flanks - lifted
  by the broad mountain mask and, more gently, everywhere inland (`RELIEF_HILLS`), so no coast
  is billiard flat; cliff coasts where the cliff field is high rise straight out of the sea;
  below the waterline a shelf about a unit deep hangs for half a degree before the floor drops
  to the deep. The streamed chunks carry ridged, slope-aware detail on top (`stream.c`), the
  authored tiles add their relief only where it is *higher* than the procedural ground (the
  source's compressed land band used to flatten every coast into a plain), and the clipmap
  rings may only raise land and deepen sea.
- **Textured ground** (`gfx/terrain.c`): the 6x3 material atlas is finally sampled - grass,
  turf and meadow on the lowlands, Mediterranean scrub between 33 and 46 degrees, soil and
  gravel on the dry heights, **triplanar rock on slopes over ~0.2** (cliffs no longer smear),
  sand on the beach, dune and cracked clay in the deserts, moss in the tundra, snow above 6.5
  - two repeat scales blended by distance, faded out towards the strategic view. Wet dark rock
  at the waterline; a darker silt-and-weed sea floor from the shoreline down.
- **Warm seas** (`gfx/sea.c`): south of ~45 degrees the shelf runs turquoise and the deep a
  saturated blue. Water absorption is three times stronger and the refraction now fades into
  the *lit* water column rather than the unlit body colour - the sunlit seabed is twenty times
  brighter than an unlit blue and used to win every shallow pixel.
- **Regions**: `MAX_REGIONS` 250 (save version 5), coast segments of 3.2 degrees in the
  Mediterranean home seas (twice that elsewhere, so the cap is spent where the player looks)
  and 28 new name pins: Istria, Dalmatia, Ragusa, Albania, Epirus, Morea, Attica, Macedonia,
  Apulia, Calabria, Sicily, Sardinia, Corsica, Crete, Cyprus, Tuscany, Lazio, Balearics,
  Valencia, Murcia, Languedoc, Cyrenaica, Ionia, Caria, Friuli, Romagna, Abruzzo, Marche.
- **Fixed: the sea vanished after the 0.16 AO reorder** - `hdr_ao_pass` left its half-res
  framebuffer bound, so the refraction snapshot copied the AO buffer and the water was drawn
  *into* it. The pass now resumes the HDR target (`rhi_target_resume`). Also fixed three
  reversed-edge `smoothstep` calls (undefined in GLSL; the Intel driver returned ~1 and painted
  the whole world in the seabed colour).
- `MARE_SEA_DEBUG=1` paints the sea's depth / land / refraction readings; the geography bake
  logs land height statistics before and after the fold.

## 0.16.0 — the island seas, battle lines, buoyancy, and a sea without a grid

- **Every island of the Adriatic and the Greek seas** (`data/geo_coast.h`): the Dalmatian chain
  (Dugi Otok and the Kornati, Vis, Hvar with Brac, Korcula with Lastovo and Mljet), the Ionians
  (Kefalonia, Zakynthos, Kythira), the whole Aegean (Rhodes, Karpathos, Kos, Samos, Chios,
  Lesbos, Limnos, Thasos, Skyros, the Sporades, Skyros, and the Cyclades as four authored
  groups, Milos and Santorini), plus Menorca, Ibiza and Malta - 25 polygons authored as vector
  data, each at least two cells in both axes so its core navigation cell goes majority-land and
  routes bend around it, with a water row kept between neighbouring chains. Crete was widened
  so Candia's town stands on land. Authored *data*, not a heightmap guess: the supplied 32768
  map turned out to be a loose AI Earth whose Adriatic is half filled in - taking its coastline
  would have buried exactly these seas (`MARE_TERRAIN_ISLANDS=1` keeps that experiment).
- **Small islands lifted clear of the water** (`world.c raise_small_islands`): the heightfield
  ramps land up from the coast over ~5 cells, so one-or-two-cell islands topped out under the
  sea shader's land threshold and rendered as drowned platforms with a surf ring - the Azores
  have done this quietly all along. Land components of up to 80 cells now rise to at least
  0.55; mainland beaches keep their low fringe.
- **The streamed terrain is on by default** (`MARE_TERRAIN_TILES=0` reverts) and a one-shot
  **geography bake** (`terrainmap_bake_geography`, `terraintiles_bake`) folds the 16x24 height
  tiles into the base world at startup - 123 ms, one tile row resident at a time: authored
  relief where both agree on land, authored bathymetry where both agree on sea, never a new
  coastline (that is the islands' job), harbour basins pinned, the carved straits re-opened,
  coast distance rebuilt before navigation. The clipmap rings clamp the authored delta to the
  base's side of the waterline, so a loose map can no longer raise landforms through the sea
  near the camera. Tiles resolve to the per-worktree `assets/<tree>/terrain/tiles` first - two
  sessions cutting into the shared folder have already overwritten each other's index once -
  and `tools/terrain_tiles.py` now writes the true sample dims into the meta (the padded dims
  used to squeeze the east and south edges by 2%).
- **Battle lines** (`gfx/staged_scene.cc`): the staged battle fights in formation. Each side
  sails a stadium circuit in line ahead - every ship holds the station her slot gives her,
  turning where her next-ahead turned (turning in succession), speed trimmed against the
  station and capped by her class, the line at 0.8x its slowest sail. The two circuits mirror
  each other so the inner legs run opposed a cable apart: inside gun range, outside ramming
  distance - lines close, hold the range on the pass, trade broadsides and wear round the cap.
- **Buoyancy from displacement** (`gfx/shipyard.c`): a class's tonnage sets how much of the
  swell reaches her hull and how far she rolls (0.55 for a 1500 t great carrack to 1.15 for a
  fusta), and the heavy classes float lower on their lines. One number, derived from the stats
  table, carried by the existing instance fields - masts, rig and oars inherit it.
- **Oars**: the bank sits against the hull (the side offset was a third of an oar too far
  out), the stroke runs at a sweep's 20-26 per minute instead of a churn, and the blades dip
  on the pull and lift on the recovery. Galley stats eased: galiot 5.0 kn under sail / 6.5
  under oars, fusta 5.5 / 7.0.
- **The strategic sea** (`gfx/sea.c`): open water keeps a readable swell when zoomed out
  (amplitude floor 0.14-0.26 instead of 0.03-0.10) and a depth gate that rises with the camera
  (`u_shallow`, 1.1 to 3.6 wave-units) keeps the coasts and every island's shallows visibly
  calmer than the open sea.
- **The dark grid on the water is gone**: ambient occlusion is now computed from the *opaque*
  depth before the water draws (`mare.c`). Taken after, it read the tessellated sea's
  patch-border derivatives and ruled the ocean into a faint 64-unit grid at distance - isolated
  with MARE_SSAO/MARE_SEA_TESS A/Bs in the staged battle (edge-step energy 46.8k -> 4.2k with
  SSAO still on). The sea occludes nothing and no longer receives AO; the remaining long
  streaks on the water are the masts' and sails' real sun shadows.
- Generated assets consolidated per worktree: `assets/elm` in this tree and its mirror at
  `C:\projects\mare\assets\elm` carry every hull, mast part, oar and building at all six
  LODs plus descriptors, manifest, the sea textures and the terrain maps and tiles; the asset
  root resolves to `assets/elm` from the repo root, a build dir or a staged copy before
  falling back to `game/assets` (MARE_ASSETS still overrides). Builds in `tmp/elm-fin`, away
  from the other session's folders.

## 0.15.0 — sails that draw, rigs from the reference sheet, ships at their own speed

- **The masts are back.** The split mast parts (`*_masts_m0..m2`) had GLBs but no descriptors
  and were missing from the manifest, so every split class - coastal trader, hulk, carrack, war
  carrack, fusta - drew a bare hull. `tools/gen_asset_descriptors.py` regenerated them; the
  one-mast classes now use their narrowed, re-pivoted `_m0` part too.
- **Rigs laid out from `assets/fleet_sails.png`** (`gfx/shipyard.c`, `classes[]`): mast count,
  stations, heights and what each mast carries. Coastal trader and armed cog: main with course
  and topsail, lateen mizzen. Cog: one great course. Hulk: three square. Carrack: square fore
  and main, lateen mizzen, bowsprit and spritsail. War carrack and great carrack: four masts -
  fore, main with three sails, lateen mizzen and bonaventure - with the spritsail. Galiot and
  fusta: two lateens. Every yard is drawn, so the canvas hangs from a visible beam.
- **Cloth that answers the wind** (`GLSL_CLOTH` in `gfx/meshes.c`). Each canvas vertex carries
  a cloth code (`mb_cloth_code`): how free it is in 32 levels, where it sits between the
  leeches, and its panel's half-width; the instance carries the true wind at the ship
  (`inst_t.fx2/fx3`). A sail bellies along its own normal by the wind through it and the ship's
  way, stays bent to its yard, is held at the foot by its sheets, luffs - flutters hard - when
  the wind runs along it, and a square sail pressed back by a head wind stops at its mast. Flags
  stream in the apparent wind and droop in a calm. The shadow pass runs the same model.
- **Per-class stats** (`ship_stats_t`, `shipyard_class_stats()`): length, beam, draft,
  displacement, knots under sail and oars, rate of turn, guns, crew. The great carrack is its
  own class now (56 m on the war carrack's model, stretched), so the Ship of the Line has its
  own numbers. The staged battle moves every ship at 55-90 % of her class speed and turns her
  at her class rate: a carrack makes four knots, not the old twelve.
- **Ship waves, and foam only where the ship is going** (`gfx/sea.c`). The sea's vertex stages
  heap water at every nearby bow - a crest that rings outward ahead of the stem, decays within
  a hull length and is gone astern - and the foam follows the same mask: the stem's splash, a
  thin break along the forward half of the hull, streaks running out ahead, the crest of the
  bow wave breaking in the texture's bright streaks. Nothing trails. Ship speed reaches the
  water in hull lengths per second, so the map's fleets and the staged ships read alike.
- `MARE_DV_CAM="dist,yaw[,x,z[,pitch]]"` pins the staged camera for screenshots.
- Generated assets for this worktree live in `C:\projects\mare\assets\oak` (all LODs,
  descriptors, manifest; run with `MARE_ASSETS` pointing there). `docs/TODO.md` lists what is
  next: buoyancy from the stats, battle formations, the oars, four-part rig splits, strategic
  waves, the full-detail terrain.

## 0.14.0 — a world twice the size, and terrain streamed from a 32768-square Earth

- **The world is twice as wide.** `WORLD_SCALE` (2, raise to 3) multiplies `WORLD_K`, and the
  simulation's distances and the camera's ranges are expressed in terms of it, so a day's sail
  still covers the same degrees of ocean while the vertical exaggeration halves. A ship is no
  longer a 46 km monster and a mountain range no longer a ridge of sticks.
- **Masts fixed, properly.** Two real bugs: the split ordered parts **stern-first**, so after
  the hull's 180-degree turn the mizzen stood on the foremast station and every mast's rake
  pointed forward; and each part was centred on its **bounding box**, which the yards pull to
  one side, so every mast sat several per cent of its height off its station. They are now
  ordered bow-first in game space and stood on the foot of their own pole.
- **`tools/terrain_tiles.py`**: cuts a 32768x32768 source into streamable tiles. The height map
  is 2.1 GB decoded and its normal map 3.2 GB, so the PNG is read as a *stream* - one band of
  512 rows at a time, 33 MB - and only the playable window is cut, at half resolution: 384 + 384
  tiles, 480 MB, 45 samples per degree against the base map's 8.
- **`world/terraintiles.c`**: pages those tiles in around the camera and drops the coldest when
  over budget (192 MB, `MARE_TERRAIN_MB`). A tile is one read of a headerless fixed-size file,
  so a load sits on the IO lane and publishes on MAIN, the same discipline as the rest of the
  streamer. Feeds the clipmap detail rings, so nothing is loaded that the camera cannot see.
- **`tools/terrain_calibrate.py`** and a measured sea level. Guessing which grey means
  shoreline floods continents; sampling the cut tiles at known places (Alps, Amazon, Atlantic
  abyss, North Sea) puts it at 0.50.
- **Both authored-terrain paths are off by default** (`MARE_TERRAIN_MAP=1`,
  `MARE_TERRAIN_TILES=1`). The supplied maps are a shaded world overview and a bathymetry-
  weighted heightfield, not DEMs: they compress all land into a tenth of their range, so what
  they yield is *smoother* than the procedural world rather than sharper. The pipeline is
  complete and tested; it is waiting on a source map with real elevation. See DESIGN_TERRAIN.md.
- `stream_test` now derives its chunk-size expectation from `WORLD_K` instead of hard-coding 32.

## 0.13.0 — masts apart, six LODs, authored terrain

- **Every mast is its own object.** Meshy generates a rig as one mesh;
  `tools/blender_split_masts.py` cuts the three-mast assemblies into `<rig>_m0` (fore) ..
  `_m2` (mizzen). Connectivity is no use for the split — a scan separates into hundreds of
  fragments, not three masts — so the fragments are clustered along the ship's axis (1D
  k-means, seeded evenly). Each mast is narrowed in x and z (0.62; the generated spars were
  chunky for their height), stood on its own origin, and placed at the station its class's
  design sheet gives. Classes whose model was already one mast keep stepping it.
- **Six LOD levels** (`ASSET_MAX_LODS` 5 -> 6): ships import at 400k / 120k / 40k / 12k / 3k /
  800 triangles. **LOD 0 is gated to the battle and city tiers** — on the strategic map a great
  carrack can still project large enough to ask for the original scan, which is a lot of
  triangles for a ship the player is looking past, so the view tier and not the pixel size
  decides whether it may be reached for.
- **Water**: the strategic sea falls to 3-10 % of full wave height (was 7-20 %), and the
  Gerstner crests are rounded — `WAVE_PINCH` 0.55 holds the horizontal pinch well below the
  textbook trochoid, whose crests come to a point and read as rows of spikes at the amplitudes
  a running sea uses. `WAVE_STEEP` 0.030 -> 0.026.
- **Authored terrain** (`world/terrainmap.c`, [DESIGN_TERRAIN.md](DESIGN_TERRAIN.md)): the real
  Earth as three equirectangular maps — height/bathymetry, normals, and a 6x3 atlas of ground
  materials. Folded into the base heightfield **only where both agree about land and water**,
  so the authored map supplies relief and never geography: the ports, navigation grid and lanes
  were built against the procedural coastline. 68 % of cells on the supplied map. Every map is
  optional; a missing one leaves the procedural world untouched.
- Assets now build into `/mnt/c/projects/mare/assets/elm_lod` (worktree-prefixed, so two
  sessions cannot overwrite each other's exports); run with `MARE_ASSETS` pointing there.

## 0.12.0 — rigging, canvas, flags, and a 300k battle LOD

- **Rigging** (`gfx/shipyard.c`): shrouds, forestays, backstays and ratlines, built
  procedurally per ship class in the hull's own frame and instanced onto every ship of that
  class. Each rope is a cross of two double-sided quads in tarred-hemp near-black — a single
  quad vanishes edge-on, a tube costs twenty times as much for something a few pixels wide.
  Skipped below 90 px, where cordage is noise.
- **Sails and flags as cloth.** Square sails bent to a yard and bellied forward, triangular
  lateen sails for the Mediterranean classes, and a nation-coloured ribbon at the main
  masthead. They flutter: the mesh shader takes the instance's cloth strength from `i_fx.y`
  and, per vertex, how free that vertex is to move from the **metalness** channel — which
  cloth never has — 0 at the yard, 1 at the loose foot. No physics yet, and no textures:
  both are hooks for later.
- **`mb_quad_grad`**: a builder quad with per-vertex cloth weight. Without it a sail built from
  strips tears along every seam, since the two vertices meeting there carry different weights
  and flutter apart.
- **Oars row.** The bank swings together at a rate and reach that follow how hard the ship is
  being pulled; a ship lying still keeps her oars out and level. Oars are a quarter smaller
  again (0.34 → 0.26 world units).
- **300k battle LOD.** Imported ship LOD 0 goes from 150k to 300k triangles — the detail a hull
  shows when it fills the screen in battle view (`docs/screenshots/ships-lod0-battle.png`).
- **`asset_lod0_trim`** (roadmap follow-up): gives back the CPU copy of any detail level that
  has sat un-uploaded for 45 s. An imported LOD 0 is the original scan and most are never drawn
  in a session; a trimmed level is reloaded if something later picks it, so it costs a
  stream-in and never the level itself. `asset.lod_trimmed_mb`, and the LOD tab reports it.
- Dashboard: authored hulls, **rigged** ships, oars and fallbacks per frame.

## 0.11.1 — the fleet corrected, and the sea calmed down
Every hull and rig was turned the wrong way round, two classes were carrying one mast, the
oars rowed inboard, and every ship towed a wake across the ocean.

- **Every ship pointed the wrong way.** Rendering the plan view of all eight hulls and the side
  view of all eight rigs (`docs/screenshots/ships-hull-plan-sheet.png`,
  `ships-rig-sheet.png`) showed the fine bow and the bowsprit at -x on *all* of them, where the
  game puts the bow at +x. The descriptors now carry `forward: "-x"`; hull and rig turn together.
- **Galiots and armed cogs carry three masts.** `mediterranean_masts` and `armed_cog_masts`
  came out of the generator as a *single* mast, so those two classes were sailing with one.
  A class now says how many instances of its mast model to step along the hull and at what
  station and height, so they raise fore, main and mizzen as their design sheets show.
- **Rigs are cut down**: a rig may no longer overhang its own hull at all (was 15%), and all of
  them are trimmed to 85%, which brings the generated spars back from tree trunks.
- **Oars**: a third smaller (0.50 -> 0.34 world units) and turned the right way - they were
  rowing inboard, with the blades buried in the hull.
- **Ships no longer tow a wake.** The long V that widened astern read as a scar drawn on the
  sea, and at map zooms every ship dragged one across the ocean. What is left is splash: a bow
  wave, a broken band along the waterline, and a short stern boil, all at the hull.
- **Every ship in a fleet sails for itself.** A fleet is still one entity in the simulation, but
  its ships were welded in a rigid line and slid across the water as one object. Each ship now
  keeps a station and wanders around it on its own slow cycle, with its own heading deviation,
  so the formation breathes. The cycle is a hash of the fleet id and the ship's slot: stable,
  continuous, and no two ships move alike. Moored ships keep their berth.
- **The sea falls away with the zoom**: full height only inside 28 units, half by 90, a fifth by
  260 and near-flat beyond - a swell the size of a county at strategic zoom was nonsense.
  Staged battles and city scenes stretch the sea by `DV_SHIP_SCALE`, so their displacement is
  cut to 45% (`DV_SEA_AMP`); the cell density, and so the silhouette of the water, is untouched.
  Everything that must agree about the surface - the sea passes, the instanced meshes so ships
  ride what they float on, and the camera's wave-crest floor - now goes through `sea_amp_now()`.
- **Tessellation cell size is continuous.** It was taken from the patch's own tessellation
  level, which is quantized, so the Nyquist fade that drops waves a cell cannot carry stepped at
  every patch edge. It is now computed per vertex from the same formula the control stage uses.
  Cells are finer again (8 px). This did *not* remove the sea's grid artifact at regional zoom -
  see KNOWN_ISSUES.
- **Tools**: `meshy_import.py --organize DEST` copies each download to
  `DEST/<asset name>/<asset name>.glb` with a `source.txt`, which is what turns a tree of
  `Meshy_AI_model.glb` into something you can navigate. `mare --map-chunks=level[,max]` prints
  the whole chunk authoring work list, ordered by ports then land. `MARE_SEA_TESS_MAX` exposes
  the tessellation ceiling.

## 0.11.0 — the Generation I fleet, and generating the world a chunk at a time
Plan and measurements: [DESIGN_SHIPS.md](DESIGN_SHIPS.md),
[BLENDER_LOD.md](BLENDER_LOD.md) sections 4-5.

- **Ships are authored, not lofted boxes** (`gfx/shipyard.c`): a hull, a mast assembly and -
  for the oared Mediterranean interceptors - a bank of oars, assembled from models imported
  from Meshy and submitted through the ordinary authored-asset path, so they get screen-space
  LOD, cross-fades, residency, shadow casters and the wave displacement that makes a hull ride
  the swell. Eight Gen I classes (coastal trader, cog, fusta, galiot, armed cog, hulk, carrack,
  war carrack) sized from the design sheets at a single constant, **1 m = 0.045 world units**,
  chosen so a 21 m coastal trader occupies the ~0.95 units the procedural sloop did and every
  existing camera distance and LOD threshold stays valid.
- **A ship type is a role, not a hull**: the same Frigate is an armed cog in the Channel and an
  oared galiot in the Adriatic. Venice, the Ottomans and the pirates sail the fusta and galiot;
  the northern trading powers favour the hulk over the carrack. The procedural hulls remain the
  fallback, so a class that is still loading or was never imported never leaves a hole.
- **The staged battle and city views use the authored fleet too** (`gfx/staged_scene.cc`): the
  staged ships carry the scene's magnification through `shipyard_req_t.scale`, and the staged
  asset bracket now spans ships as well as buildings - authored hulls were being pushed but
  only submitted in city view, so a staged battle drew nothing at all.
- **Oars** are placed from the sheets (20-22 pairs, pivoted a third of their length inboard so
  the bank sits outboard of the rail) and only submitted once the hull is over 140 px, since a
  22-pair galiot adds 44 instances.
- **Meshy import pipeline** (`tools/meshy_import.py`): imports a tree of Meshy downloads,
  resolving `Meshy_AI_armed_cog_hull_faithful_keel_3d_..._d35aeea1` to the asset
  `armed_cog_hull`, decimating to five LODs per kind and writing a sidecar. The per-model work
  is `blender_export_glb.py`, which now also takes a `.glb` source (`--import`) and can
  straighten a model generated at an angle (`--rot`) - the galley oar comes out of Meshy dipped
  45 degrees. Both drivers translate WSL paths for a Windows Blender.
- **Generating the map a chunk at a time**: `mare --map-chunk=level,tx,ty[,res]` writes a
  top-down shaded-relief reference of one streamed tile (sea by depth, land by height, sun from
  the north-west) and prints its asset name, geographic extent, world rect, height range and
  land fraction - everything a generated chunk has to match. Feed the PNG to Meshy's
  image-to-3D, then `meshy_import.py --kind terrain --name tile_l3_12_7`. The descriptor
  generator recognises the `tile_l<level>_<x>_<y>` pattern and writes a terrain descriptor
  fitted to the tile's world size, keeping its own origin so sea level stays at y = 0.
- **LOD 0 is demand-loaded.** The asset commit now queues every LOD *but* the finest, coarsest
  first; LOD 0 reaches the GPU only when an instance actually picks it (`asset_upload_lod`). A
  fleet on the horizon costs no VRAM for a level it will never draw.
- **Imported LOD 0 is capped at 150k triangles** (six times LOD 1). The raw scans are 0.8-2.3 M
  triangles - 45-68 MB of vertex data per hull once baked, about a gigabyte across the fleet -
  for detail far below a pixel even when a ship fills the screen. `BUDGETS` in
  `tools/meshy_import.py` is the knob.
- Measured over Venice with two oar banks in frame: 11.3 ms (87 fps) authored against 9.1 ms
  (108 fps) procedural, +6 MB of asset VRAM. `MARE_SHIPS=0` for the A/B; the dashboard **LOD**
  tab reports authored hulls, oars and fallbacks per frame.
- `stream_test` covers the chunk reference (extent, land fraction, height range, PPM header,
  out-of-range refusal).

## 0.10.1 — streamed detail: lighting and a second clipmap ring

- **The streamed relief is lit.** The terrain fragment shader differentiates the composed
  detail delta in world space and adds that gradient to the base heightmap's before
  normalising, so the geometry the rings displace is shaded by its own normals. Until now it
  was lit by the base map and read as flat painted ground.
- **A second, coarser clipmap ring**: near 80 units (tactical L3 detail, 0.156 units/texel)
  and far 320 units (coastal L2 detail, 0.625), each sized by Nyquist against the level's
  detail frequency — a ring only carries detail it can sample twice per wavelength, so the far
  ring asks for L2 explicitly (`stream_delta_at_level`). The near ring cross-fades into the
  far one at its border rather than into nothing, so pulling the camera back costs resolution,
  never relief. At most one ring recomposes per frame, the near one first.
- `stream_test` covers the level-clamped lookup (never finer than asked, L1 and coarser carry
  no delta).

## 0.10.0 — world streaming
Plan and the terrain grid listing: [DESIGN_STREAMING.md](DESIGN_STREAMING.md).

- **World hierarchy, streamed from disk** (`world/stream.h/.c`): continent -> region ->
  coastal tile -> tactical chunk, a quadtree of 512 tiles over the real-world map (L0 3x3 64°
  tiles .. L3 22x17 8° chunks, the renderer's existing 64x64-cell terrain chunk). Each tile
  carries a 257x257 *delta* heightfield against the base map — detail that doubles per level,
  deterministic from the world seed, faded out below h=0.5 and over water so harbours, towns
  and coasts never move and the simulation stays untouched. Tiles load through the job system
  (disk reads/writes on the IO lane, generation on DECODE, the state flip on MAIN) and are
  cached in `tiles/` on disk, so the second visit is a read, not a regeneration. 227 mid-ocean
  tiles are water-only and never load.
- **Memory budget and unloading policy**: 64 MB of resident tiles (`MARE_STREAM_MB`);
  eviction is LRU by last-wanted, never a tile in the wanted set, never a parent whose
  children are resident (the coarser fallback that makes a stream-in hole impossible), a few
  tiles per update, payload only — the disk cache stays.
- **Terrain detail window** (`gfx/terrain_rings.cc`): the finest resident tiles compose into one
  512² R32F window around the camera (sub-millisecond, parallel on the WORKER lanes, rebuilt
  on residency change or when the camera leaves the inner half) that the terrain vertex
  shader adds on top of the base heightmap, faded at its border. The camera's ground floor
  includes the delta.
- **Progressive city loading: icon -> LOD4 -> ... -> LOD0.** Asset uploads queue coarsest
  first, and the scene draws the nearest *resident* LOD at or coarser than the pick instead
  of forcing a mid-frame upload — a city now fades in level by level through the existing
  cross-fade, and a stream-in can cost detail but never objects or a frame.
  `asset.lod_fallbacks` counts instances drawn coarser than picked.
- **Tick phases on the worker pool**: economy (production, consumption, prices, population),
  settlements (immigration, assimilation) and the fleet movement pre-pass (wind factor +
  effective speed) are split into parallel per-entity compute and an ordered commit for
  everything shared (treasury, chronicle, rng) — bit-identical results, sim_test's save/load
  determinism check covers it on the real pool. The dispatch is **adaptive**: measured on the
  Windows pool, waking the workers costs ~2 ms per parallel_for against ~0.2 ms of economy
  work, so a phase dispatches only past `PARSIM_MIN_BATCH` (2048) entities — today's world
  runs the same split sequentially and inherits the scaling for free. `MARE_PARSIM=0/1/2`.
  Measured: `sim_test 1` 17.7 s adaptive/sequential vs 54.6 s forced parallel.
- **Authored terrain tiles**: an asset named `tile_l<level>_<tx>_<ty>` (type `terrain`)
  replaces a streamed tile's visuals through the normal asset path; the streamer marks it
  authored. docs/BLENDER_LOD.md documents the whole exporter pipeline.
- **Streaming observability**: dashboard **Streaming** tab (residency vs budget, per-level
  grids, loads/evictions, last gen/IO cost, the list of wanted chunks still to model) and a
  world-space tile overlay (state-coloured frames that follow the ground, crosses on grids
  without a mesh; `MARE_STREAM_TILES=1`). Metrics: `stream.resident/wanted/loading/mb/
  evicted/disk_hits/generated/window_ms/unmodeled`, `asset.lod_fallbacks`.
- **Sea height follows the zoom**: wave displacement is full only near the water
  (`sea_zoom_amp`: 1.0 close in, 0.35 fully pulled out), ships ride the same scaled field,
  and the camera floor uses it; the tessellation ceiling drops (max level 48 -> 32, ~11 px
  cells) — zooming in buys wave height back, not subdivision.
- **Tests**: new headless `tests/stream_test.c` (grid shape, wanted set on the real pool,
  delta determinism, disk round-trip, wrong-seed rejection, coast fade, budget + LRU policy,
  water-only tiles); wired into build.sh, CMake and ctest.

## 0.9.0 — visual rendering
Plan and measurements: [DESIGN_VISUAL.md](DESIGN_VISUAL.md).

### Pass 10: fixes from playing it
- **Tessellation is a near-camera path** (`SEA_TESS_DIST` 90 units), not the default at every zoom.
- **Water is blue-grey, transparent and no longer plastic**: new body colours and absorption,
  per-channel Fresnel (dispersion) for the sky reflection, and a sparkle term from the
  sub-ripple normals in place of one smooth specular lobe.
- **Shadows from the whole cull radius**: cascades reach `cam.dist * 2.6 + 70` (max 650) and
  casters are collected from that radius rather than from the drawn set, so towns that are
  icons, behind the camera or hidden still cast. 30-69 towns and 23-34 fleets per frame that
  previously cast nothing. Nothing outside the radius is submitted.
- **Volumetric fog** with an analytic height integral and sun in-scattering, driven by the
  weather; **clouds visible from above** when zoomed out, cut against the depth buffer and faded
  past 1400 units; **bloom** so the sun disc, its glow and the glitter path read as a light.
- **Cloud noise is gradient noise**, which removes the soft rectangles the value-noise lattice
  painted across the ground through the cloud shadows. Whole cloud system measured at
  +0.1 ms GPU.
- **One simulation tick every 15 s** (`SIM_TICK_SECONDS`, or 6 simulated days, whichever comes
  first) instead of ~3 a second. The world keeps its speed (0.334 days/s measured against 0.333
  asked for) and the simulation is bit-for-bit unchanged, because a tick runs the accumulated
  time as a burst of `SIM_STEP` sub-steps; fleets glide across the interval. `MARE_TICK_S`.

- **Sea at water level**: an inner 32x32 patch lattice (14-unit radius, cells to 0.02 units)
  replaces the 0.25-unit facets up close; the eye is held above the wave crest (or the ground)
  by a CPU mirror of the wave sum (`sea_height_at`, `cam_min_eye_y`).
- **Water material**: refraction of the frame drawn so far (`hdr_capture_scene`) with
  per-channel absorption by depth, Fresnel-weighted sky reflection from the wave normal and a
  tamed sun glint; diffuse lighting of the body colour is gone, and with it the plastic look.
  The deep-water wedge (terrain-chunk coverage showing through 92 %-opaque water) is gone too.
  `MARE_SEA_REFRACT=0`, `MARE_SEA_INNER=0`.
- **Sky** (`gfx/sky.c`): gradient, sun disc and glow, fbm cloud layer drifting with the wind;
  `GLSL_CLOUDS` shadows the ground under the clouds in every lit shader. `MARE_CLOUDS=x`,
  `MARE_SKY=0`.
- **Ambient occlusion**: terrain horizon occlusion baked from the heightmap on the terrain's
  ambient (`sun_shade_ao`); half-resolution SSAO from the HDR depth applied in the tonemap.
  `MARE_SSAO=x`, `MARE_TERRAIN_AO=0`, dashboard knobs.

- **Weather** (`gfx/weather.c`): calm / haze / mist / overcast / rain derived from the wind at
  the look-at point, nearby storm events and the hour, smoothed over 3 s. Glassy sea and haze
  in calm air, dawn mist in the bays, a dimmed sun, grey sky, thick fog and wind-slanted rain
  under a storm. Fog gained density and start uniforms (`u_fog`). `MARE_WEATHER=0|clear|calm|
  mist|storm` pins a state; `weather.*` metrics; dashboard System line.

- **Waves anchored to the world** (fix): the wind-following wave set rotated with every pan and
  swept its crests sideways. Sets are now frozen once built and cross-faded by amplitude when
  the wind at the look-at point has moved enough; texture drift is accumulated. Pixel-identical
  frames after a pan and after a teleport (`MARE_PAN`, `MARE_SEA_FREEZE`).
- **Tessellated sea** (`rhi_tessellation_supported`, `tcs`/`tes` in `rhi_pipeline_desc_t`):
  a world-snapped 50x50 patch lattice 300 units around the look-at point with ~9 px cells,
  the authored displacement map (`game/assets/sea/sea_displacement.png`) as geometric chop,
  a circular hand-over to the world grid; warped grid fallback (`MARE_SEA_TESS=0`). Works
  around an Intel link crash on control shaders that read tess levels back.
- **Foam**: Jacobian whitecaps, shoaling and a surf sheet off the beach, bow/hull/wake foam
  around the 24 nearest ships (`sea_set_ships`), bounded union; `MARE_FOAM`.

- **Wind-driven sea** (`gfx/sea.c`, `glsl.h`): the authored four-wave set is replaced by five
  Gerstner waves derived every frame from the wind at the look-at point - a fan around the wind
  direction that narrows as it strengthens, wavelengths growing with it, height from glassy in
  the doldrums to heavy in a westerly, and per-wave randomness hashed from the game seed. The
  wind is smoothed over ~2 s and the phase is accumulated per wave, so a wavelength that changes
  with the wind never makes the crests jump. Ships ride the same set.
- **Waves no longer speed up and slow down with the camera**: the detail grid's cells grow from
  0.14 to 7.6 world units, and waves shorter than a few cells were aliasing into a pattern fixed
  to the camera-centred grid. `wave_sum` now fades out any wave the grid cannot sample at that
  point. `MARE_SEA_NYQUIST=0` renders the artefact for comparison.
- Surface detail and foam streaks drift downwind (the second layer 25 degrees off the first so
  they do not reinforce into rays), and everything the detail grid adds over the flat world grid
  fades out with the grid, so the two meet without a step.
- **Shadow casters at a coarse LOD** (`gfx/scene.c`): the cascades cast from a private copy of
  each asset's coarsest LOD (2k triangles rather than the 80k the main pass draws), with their
  own instance buffer that includes the instances the occlusion queries hid. `gpu.shadow_ms`
  5.15 vs 6.07 in the city tier and 2.29 vs 4.32 in the staged city; a low-sun town view differs
  in 0.11 % of pixels. The cross-frame static cache first planned for this step is not viable -
  the day/night cycle swings the sun ~5 degrees a second, so every cascade is dirty every frame.
- **Anisotropic filtering** (`rhi_gl.c`): the driver maximum (16x here) on every mipmapped
  texture, with a dashboard cycle and `MARE_ANISO=N`. The sea and the roofs are seen nearly
  edge-on, which is where trilinear filtering blurs.
- **Five LOD levels** (`ASSET_MAX_LODS` 4 -> 5) and the asset export pipeline:
  `tools/export_assets.py` walks the whole `.blend` source tree, resolves Meshy filenames to
  asset names, skips what is already exported and runs Blender headless per asset
  (`--check` prints the plan, `--validate` runs Blender but writes nothing, `--jobs N`
  parallel); `tools/blender_export_glb.py` exports LOD0..LOD4 (80k/24k/7k/2k/600 triangles);
  `tools/gen_asset_descriptors.py` writes 5-LOD descriptors and knows ship parts.
  `tools/ppm2png.py` converts the `MARE_SCREENSHOT` captures.

- **GPU occlusion culling for authored buildings** (`gfx/occquery.c`, `rhi_query_*`): every
  authored instance's bounding box is tested against the depth buffer after the opaque pass
  with a hardware occlusion query; instances whose last answer was "hidden" are left out of
  the main pass (the shadow pass still draws them), answers apply a frame later, and nothing is
  culled while the camera moves. Keys are the LOD selector's per-instance keys. City tier over
  Seville: 104 of 155 houses culled, 16.8 → 15.3 ms; the rest of the frame is the shadow pass
  (step 3). Toggles: dashboard "occlusion", `MARE_OCCQ=0|1|2` (2 = measure the queries' cost).
- **Terrain-horizon occlusion measured and shelved** (`gfx/occlusion.c`, `tools/occ_probe.c`):
  a ray-march over the heightmap hides 1 town in 37k camera placements and no terrain chunk -
  the relief is 7 units high and the camera never pitches below 12 degrees. The ray stays for
  towns and ships (it costs nothing: 218 heightmap reads for the whole probe), the chunk test
  was removed. `asset_test` covers the ray on synthetic terrain.
- Metrics `gfx.occluded_assets`, `gfx.occ_queries`, `gfx.cam_moving`, `gfx.occluded_ports`,
  `gfx.occluded_ships`; `gfx.occ_queries` profiler scope; the Performance tab's occlusion line.

## 0.8.3 — LOD director, residency, async save/load, Jobs and LOD dashboards (engine scale, steps 5-10)
Design: [DESIGN_LOD.md](DESIGN_LOD.md), [DESIGN_JOBS.md](DESIGN_JOBS.md) (with the before/after
table); plan in [ROADMAP](../../docs/ROADMAP.md). The engine-scale plan is complete.

- **View tiers and the LOD director** (`gfx/lod.c`): city / battle / regional / strategic /
  continental, derived from the camera, each with a triangle budget. The scene build reports
  what it submitted and the director scales the hand-over thresholds for the next frame so the
  smallest instances drop a level first and recover as soon as there is room
  (`lod.asset_tris`, `lod.budget_scale`).
- **Icons and clustering**: marker alpha coupled to the mesh fade, size by importance, and
  screen-space clustering of distant markers at continental zoom (`lod.icons_clustered`).
- **Residency** (`gfx/scene.c`): every authored asset is wanted up to the regional tier; past it,
  assets untouched for 12 s are evicted coldest-first once the 192 MB GPU budget is exceeded
  and reloaded through IO → DECODE → MAIN on return, the town drawing its procedural pieces
  meanwhile. Shared cloud cover (`gfx/clouds.c`) for zooms that outrun the streamer.
- **Simulation on the pool**: the 111 port-distance rows (~15 ms of Dijkstra each) are
  precomputed in parallel at game start and after a load - 301-307 ms on 8 chunks instead of
  ~1.7 s serial, and no Dijkstra inside a tick any more. The per-entity tick phases measured at
  1-2 us each and stay sequential (DESIGN_JOBS.md, "What is not worth parallelising").
- **Asynchronous save / load** (`sim/game.c`: `game_save_async`, `game_load_async`,
  `game_io_poll`): the frame copies the ~1.1 MB state (0.1-0.3 ms measured) and the IO lane
  writes `<name>.tmp`, renamed over the previous save only when complete; a load reads and
  validates on the IO lane and the poll swaps the state in and rebuilds the caches (~300 ms,
  the nav rows). One operation in flight: a save during a save is queued, a load during
  anything is refused. F5/F9 and the menu use it; **autosave** every 180 s of unpaused play
  to `mare_autosave.bin` (`AUTOSAVE_SECS`, `MARE_AUTOSAVE_S` overrides), loadable from the
  menu. `sim_test` checks the asynchronous files are byte-identical to the blocking save's,
  the queue/refuse rules, that no `.tmp` is left, and that a state applied by the poll
  continues bit-identically.
- **Dashboard Jobs tab**: per-lane busy % over a half-second window, queue depth now / peak
  with a history graph, jobs/s, executed and busy totals, inline runs, time blocked in
  `jobs_wait`, the asset pipeline's stages in flight (`asset_t.stage`,
  `asset_pipeline_counts`), streamed textures, save/load state and cost, autosave countdown,
  save buttons. **LOD tab**: tier and every tier's budget, triangles vs budget and the
  director scale, tracked / fading / clamped-jump counters, clustering and culling counts,
  the bias knob, GPU residency per asset and LOD with idle time, streamed textures. Seven
  tabs; `MARE_DASHTAB=jobs|lod|...` opens one unattended.
- **Metrics and evidence**: `jobs.busy_pct`, `jobs.wait_ms`, `jobs.io_queued`,
  `save.snapshot_ms`, `io.save_ms`, `io.load_ms`, `save.count`; a DEBUG `long frame` record
  for any frame over 25 ms with the previous frame's CPU/GPU split, upload jobs, assets
  loading and save/load state; a DEBUG record whenever the LOD clamp acts, with the camera
  distance; `tools/metrics_summary.py` (median / p99 / max per CSV column);
  `MARE_ZOOM=...,delay` holds the camera before a scripted zoom starts.
- **Measured** (Release, 24-core Windows box, Intel integrated graphics, 240 Hz, `--dev
  --skip=120`, nine authored assets resident, autosave forced every 4 s; details in
  DESIGN_JOBS.md): fully zoomed-out map 4.5 ms median / 6.1 ms worst after start-up (was
  4.0 / 90.4 with one asset), staged battle 4.4 / 5.9 (was 4.0 / 90.2); the worst frame while
  an autosave was being written was 4.0-5.1 ms in both; start-up parses eight houses in
  365-380 ms concurrently and the church in ~1 s, all off the render thread. The scripted
  6 → 900 → 6 zoom over Seville runs at 5.2 ms median; its only frames over 25 ms are at
  6-10 units with ~2.1 M authored triangles at LOD 0 on the iGPU, identically with autosave
  on and off (the step-5 GPU limit in KNOWN_ISSUES, not a stall: CPU 0.5 ms). `lod.pops`
  recorded clamp activations only when the church landed mid-run and the town rebuilt, and
  as the director relaxed across the city/battle boundary - walked one level per fade, no
  visible pop. The first frame still costs ~92 ms of GL warm-up.

Known: residency is per asset and the wanted set is the tier's, not velocity-predicted; a load
pauses ~300 ms for the nav rows; the city tier is GPU-bound on integrated graphics.

## 0.8.2 — background assets, screen-space LOD with cross-fades, authored sea (engine scale, steps 2-4)
Design: [DESIGN_LOD.md](DESIGN_LOD.md), [DESIGN_JOBS.md](DESIGN_JOBS.md); plan in
[ROADMAP](../../docs/ROADMAP.md).

- **Assets load on the job system** (`assets/asset.c`): file reads on the IO lane, glTF
  parsing + parallel texture decoding + mesh processing on the DECODE lane into a private copy,
  commit on the MAIN lane (so `asset_t.state` only changes on the render thread), then one
  budgeted GPU-upload job per LOD. New states `ASSET_LOADING`, `asset_request[_all]()`,
  `asset_generation()` (town caches rebuild when an asset lands), `asset_set_upload_hook()`.
  Start-up no longer waits: six Meshy buildings parsed concurrently in 322 ms each and were
  resident 0.1 s after the window opened (the church alone used to block for 795 ms).
  `core/diag.c` logging took a spinlock since workers log; `jobs_help()` added for callers that
  wait on a state rather than a counter.
- **Streamed textures** (`gfx/textures.c`): request once, poll; IO → DECODE → MAIN.
- **Authored sea** (`gfx/sea.c`, `game/assets/sea`): a seamless tangent-space wave normal map
  and a foam albedo cut from the reference sheet by `tools/sea_textures.py` (margin cross-fade
  in both axes, vectors renormalised, high-passed albedo so the photo's gradient does not band
  when tiled). The shader samples both at the swell and chop scales; bright streaks become
  whitecaps over the depth colour. The procedural wave map covers until the tiles are resident.
- **Screen-space LOD** (`gfx/lod.h/.c`): one metric for everything with levels of detail - the
  projected diameter in pixels - with thresholds in reference-1080p pixels. Authored switch
  distances convert at use; descriptors may also give `px` directly. Ships `[full, simplified,
  icon]` at 80 / 26 px of fleet footprint, towns `[town, icon]` at 35 px, authored assets
  their own LODs: the pixel equivalents of the old 75 / 230 / 260 unit rules, so a 12-ship
  fleet and a great city stay 3D further out than a sloop and a hamlet, and 4K keeps the same
  on-screen detail as 1080p.
- **No popping** (`inst_t.dither`, `GLSL_DITHER`): per-instance state with ±12 % hysteresis and
  a 0.3 s cross-fade; both levels draw with complementary ordered-dither masks in the mesh and
  shadow passes (exact pixel partition, no sorting), icons fade with the complementary weight,
  mid-fade reversals play back instead of snapping. `lod.pops` (jumps of 2+ levels) stays 0 on
  the scripted 6 → 900 → 6 unit zoom (`MARE_ZOOM`; median frame 4.9 ms, `frame.cpu` < 1 ms
  throughout, up to 83 instances mid-fade).
- **Building set pipeline**: `tools/blender_export_glb.py` now exports LOD0-3 per `.blend`
  (authored `*_LODn` nodes or decimation to 80k / 24k / 7k / 2k triangles, base colour only,
  1k JPEG), `tools/gen_asset_descriptors.py` writes descriptors (`px` thresholds 220 / 90 / 30,
  per-role heights) and the manifest. Towns place authored houses by tier (merchant houses on
  the square, huts by the wall), plus warehouse / lighthouse / town hall when those roles exist;
  the staged city view uses the same set at 1:1. Procedural boxes remain the fallback.
- Dashboard: asset states include "loading", the Assets tab shows lane queue depths and LOD
  fade/pop counters; `asset_lod_switch_dist()` for px-based LODs. Tests: `asset_test` now
  drives `lod.c` (metric, hysteresis, fade, reversal, pops, determinism) and the asset
  generation; all four suites pass.

Known: no triangle budget yet, so the city view with ~150 authored houses is GPU-bound on
integrated graphics (step 5); every LOD of every asset is resident (step 7).

## 0.8.1 — job system (engine scale, step 1 of 10)
First step of the engine-scale work planned in [ROADMAP](../../docs/ROADMAP.md): a thread pool
every long-running piece of work will go through, so that tick processing, asset loading and
(later) LOD streaming stop stalling the frame. Design: [DESIGN_JOBS.md](DESIGN_JOBS.md).

- **`core/jobs.h` / `core/jobs.cpp`** — one worker pool of `cores - 1` threads serving four
  lanes rather than a pool per kind of work: `WORKER` (frame-critical, drained first), `DECODE`
  (glTF parsing, texture decode, mesh processing), `IO` (two dedicated threads, because a
  blocking read should not cost a core) and `MAIN` (GPU uploads, run only by the render thread).
  Join counters, `jobs_parallel_for`, per-lane statistics, and a budgeted `jobs_main_drain`
  called once per frame (`jobs.upload` scope, `jobs.main_ran` / `jobs.queued` metrics).
- **Waiting helps:** `jobs_wait` and `jobs_parallel_for` run queued jobs on the calling thread,
  so a job may wait on jobs it spawned without deadlocking and the render thread never idles.
- **Determinism by construction:** `parallel_for` chunk boundaries depend only on `(count,
  grain)`, never on the worker count, so a reduction into per-chunk partials is bit-identical
  however the chunks were scheduled. This is what will let the simulation tick move onto the
  pool without breaking save/load determinism.
- **First C++ in the project**, and deliberately the only one: C11 has no portable threads that
  MSVC implements, so the pool is one C++17 translation unit behind a C ABI. `mare.c` stays the
  C unity build. Rules for any future C++ island are in ONBOARDING.
- **Synchronous mode** (`MARE_JOBS=0`, and the default before `jobs_init` / after
  `jobs_shutdown`) runs every submit inline, so headless runs and race hunts need no special
  casing. `MARE_JOBS=N` pins the worker count.
- `tests/jobs_test.cpp` (new, in CTest and both build scripts) covers the lanes, counters,
  chunk determinism across worker counts, nested waits, the MAIN-lane thread affinity and
  budget, back-pressure at 3x queue capacity, and shutdown. Build plumbing updated for the
  second translation unit (`build.sh`, `build.bat` — `cl` rejects `/std:c11` with `/std:c++17`,
  so the C++ unit is compiled separately — `CMakeLists.txt`, CI).

Nothing submits real work to the pool yet; steps 2 (asset loading) and 7-9 (LOD streaming,
tick, save/load) are its clients. Measured idle cost: `jobs.upload` averages 0.001 ms/frame.

## 0.8.0 — detail views: through the clouds to the battle and the city
Spectator close-ups for the two reference concepts (`assets/ship_battle*.png`,
`assets/city_view.png`), staged with the current low-poly models as 1:1 samples.

- **The transition:** entering a detail view rolls procedural fbm clouds over the finished
  frame (~0.8 s, the "loading" bake), swaps to the staged scene behind full cover, then dives
  the camera from 380 to ~50-130 units as the clouds thin (~1.1 s). Leaving plays the same in
  reverse and restores the exact map camera. The overlay draws after the tonemap.
- **Battle view (`B`):** a staged local scene (1 unit ≈ 4 m) - open sea in every direction and
  a hilly coastline a kilometre off. Ships reuse the world meshes at true relative scale
  (sloop ~24 m, ship of the line ~52 m, capped at 50). A real battle is reconstructed from the
  engaged fleets (sides, ship types, nation colours); on quiet seas a 30-ship exercise is
  staged instead. Every ship sails independently: own course, speed and wander, steering back
  into the melee, rolling and pitching in the swell, trading broadside gunsmoke with the
  nearest enemy. Ship shadows land on the water (0.7.9's fix at work). Spectating is free -
  the world keeps simulating at the chosen speed.
- **City view (`V`):** the selected/hovered town (or the nearest one) at 1:1, laid out in
  districts that follow the port's actual state: civic centre (tower + banner), the authored
  church, a harbour front whose pier count follows B_DOCKS and warehouse row follows
  B_WAREHOUSE, a commercial core when B_MARKET exists, a residential mass sized by population,
  the wall ring with corner towers per fort level, and farms outside the walls. Two moored
  ships give the harbour life. The clock crawls at 0.05 days/s while inside (same rule as the
  map's city view); leaving restores the chosen speed.
- **Camera:** the ordinary RTS camera drives the local space (pan / Q/E / wheel / middle-mouse
  all work), clamped to the stage; `cam_pitch_scale` eases the pitch towards the horizon for
  the cinematic framing. Esc or M leaves. `MARE_DETAIL=battle|city` enters straight from
  startup for visual tests.
- **Plumbing:** world passes (terrain, sea, ports, ships, overlays, icons, labels) are skipped
  while a detail scene owns the frame; the scene submits through the standard instanced mesh
  pipeline, so PBR, gunsmoke and the shadow cascades apply unchanged (the shadow pass hooks
  `scene_submit_shadow`). World picking and commands are disabled; speed keys still work.
- Screenshots: `docs/screenshots/detail-battle.png`, `detail-city.png`. All three CTest suites
  pass.

Known limits: the staged sea is a calm plane (no waves or wakes); the city has no street grid
yet, only districts; battles are not steerable by design. Next feature: optimisation and
multithreading (see ROADMAP).


## 0.7.9 — shadows actually cast, city view, battle detail, picking by size
- **Fix (issue-2): nothing ever cast a shadow onto the ground or the water.** The shadow
  camera was placed on the wrong side of the scene (`center - sun*back` instead of
  `center + sun*back`): it looked *towards* the sun, so depth sorted away from it, an occluder
  above a receiver stored a larger depth than the receiver, and the compare always answered
  "lit". Only side-face darkening survived, which is what made shadows look "sort of there".
  Boats, houses, walls and the church now cast onto terrain and water. Verified by projecting
  logged cascade matrices against a dumped atlas, then in game
  (`docs/screenshots/shadows-fixed.png`, `-lowsun.png`).
- **Shadow filtering is bilinear now:** the depth atlas is sampled as `sampler2DShadow` with
  LINEAR + compare mode, so every PCF tap is a hardware bilinear depth comparison (3x3 taps on
  top). The depth margin dropped from 260 to 24 world units: the old range mapped ~550 units
  into [0,1], which pushed the lookup bias past the occluder separation of anything shorter
  than a few units - the second reason boats never shadowed the sea. Bias no longer scales per
  cascade (the normal-offset already does).
- **Dev tools:** `MARE_SUN="x,y,z"` pins the sun (lighting/shadow tests);
  `MARE_SHADOW_DEBUG=1` renders the shadow factor as grayscale, `=2` renders diagnostics
  (out-of-bounds / factor / enabled as RGB); the cascade-0 matrix is logged once under it.
- **City view (3rd zoom):** the camera now goes down to 2.5 units (was 6) and the pitch eases
  towards street level below 16; Q/E and middle-mouse already rotate. With the camera over a
  town the clock crawls to 0.05 days/s (the world does not race on while you look at a city;
  the HUD shows `~city~`), and the moment a battle is being watched from the same height the
  throttle lifts - spectating never slows the simulation. Resuming zoom restores the chosen
  speed automatically (G.speed is never touched).
- **Battle view:** a fleet that fired in the last 0.3 days keeps its full mesh and its whole
  line of ships out to 230 units (instead of 4 simplified hulls), budgeted at ~50 promoted
  ships per frame so a huge melee cannot stall the renderer. `B` jumps the camera to the
  freshest battle.
- **Picking:** the hover/click radius for towns and fleets now grows with their projected
  size on screen (matching the selection circles) instead of a fixed 16-18 px dot at the
  centre - a zoomed-in town is clickable anywhere on its footprint.
- **Proposal:** `game/docs/DESIGN_MAP_DETAIL.md` - how to get a more detailed map without
  raising the global grid: shader detail octaves near the camera, a coastline SDF to kill the
  cell staircase, and (only if needed) a camera-centred clipmap. With estimates and rejected
  alternatives.


## 0.7.8 — town placement fix, clearer water, asset roles and footprints (asset steps 8-9)
- **Fix (issue-1): houses in the fort wall, roofs without houses.** Town pieces were placed by
  centre point only: houses could stand in the wall ring (their roof overhang reached the
  stones), and a house on a slope took its centre height, so the downhill side floated and the
  uphill side was buried with only the roof showing. `town_build` (scene.c) now keeps a
  footprint list — tower, church, every house — and rejects overlaps; houses stay a margin
  inside the wall ring, take the *lowest* ground under their footprint, and are skipped on
  steep ground (rise > 60% of their height) or on the beach (below the sand/grass blend).
  Walls do the same per segment and are left out on the beach. Big pieces are placed first
  (tower, then the church landward of it, with a wider search on hillside towns), then as many
  houses as fit. Layouts change as a result; the per-port cache still rebuilds only when size,
  fort, owner or population change.
- **Sea:** more transparent — shallow water now shows the seabed clearly (alpha 0.38 → 0.92
  over the first 1.6 units of depth, was 0.62 → 1.0 over 1.1); foam and the map view are
  unchanged.
- **Asset roles (step 9, generator wiring):** descriptors carry `"role"` (defaults to the
  name); `asset_get_role(type, role)` returns the first loadable asset of that type and role,
  so the town generator asks for `building`/`church` rather than a file name — drop in another
  church descriptor with the same role and towns use it, no code change.
- **Collision footprints (step 8):** `asset_footprint_radius` (sphere, or the AABB's half
  diagonal) and `asset_contains_xz` (point test in the rotated instance frame) on top of the
  derived collision shape; the town generator uses the radius to keep houses out of the church.
  Still no physics — these are placement helpers.
- **Church assets:** `game/assets/church/Church_LOD0..3.glb` are now the real per-LOD exports
  (one node each: 128k / 50k / 15k / 3k triangles), copied from `/assets`.
- Tests: role parsing and lookup, footprint radius, rotated AABB containment. All three CTest
  suites pass. Screenshots: `docs/screenshots/town-placement-issue.png` (before) and
  `town-placement-fixed.png` (after, Seville), `sea-transparency.png`, `asset-church-towns.png`.


## 0.7.7 — asset runtime: GLB/glTF import, descriptors, normalisation, cache, LODs
The first authored 3D asset (a Meshy church) renders inside a procedural town. Everything
needed to go from a `.blend` to an instanced, shadowed, LOD-switched mesh in the game now exists
as a standard pipeline under `game/src/assets/` (CPU-side, GL-free, covered by the headless
`asset_test`).

- **glTF 2.0 / GLB loader (`gltf.c`):** GLB container and plain `.gltf`, base64 `data:` URIs,
  external `.bin` buffers (confined to the asset directory), POSITION / NORMAL / TEXCOORD_0 /
  COLOR_0, u8/u16/u32 indices, node TRS or matrix hierarchies baked into flat world-space
  streams, pbrMetallicRoughness factors, base-colour textures handed over as encoded bytes.
  Every primitive remembers its glTF mesh and node name. Missing normals are generated flat.
  Rejected with a message naming the mesh: sparse accessors, required extensions (Draco),
  non-triangle modes, accessor overruns, out-of-range indices, non-finite positions, truncated
  GLB chunks, a 4M-vertex budget.
- **Asset descriptors (`<name>.asset.json`, format in `asset.h`):** type (prop, building, ship,
  ship_part, terrain), per-LOD file *or node name* with switch distance, scale (`fit` height /
  length / width / max to a world size, or a plain factor), pivot (keep / center /
  base_center + offset), forward axis, material rules, collision shape. Validated on parse:
  missing name or file, paths escaping the directory, non-increasing LOD distances, LOD 0 not
  at 0, fit without size, negative scale, unknown enums.
- **Registry and cache (`asset.c`):** `game/assets/manifest.json` lists descriptors;
  `asset_get(name)` loads on first use and caches; failures are remembered (no retry storm) and
  reported with the file and reason; `asset_unload_all` drops CPU data but keeps registrations.
  Files shared by several LODs are read once. The asset root is `MARE_ASSETS` or `game/assets`
  searched upwards from the working directory. Startup preloads every registered asset (the
  church: 4 LODs, 128k triangles, 4096² JPEG decoded, 9 MB, ~220 ms).
- **Scale normalisation:** derived from LOD 0 so every LOD lands at the same size; sanity limit
  of 1000 world units catches cm/m mistakes.
- **Pivot normalisation:** `base_center` puts the origin under the model's footprint, so
  `inst_t.y = world_height` sits it on the ground; `forward` rotates the model's front to the
  game's heading 0 (+x).
- **Material metadata:** glTF materials map onto the game's 12-float PBR vertex format
  (roughness, metalness, colour, instance-colour slot). Rules match material names
  case-insensitively (`"*"` for all) and override roughness / metalness / colour, mark a
  material as taking the per-instance colour (nation roofs, flags), or disable texture baking.
  glTF's literal defaults (metallic 1, roughness 1) are read as "unspecified" and become a matte
  dielectric. The base-colour texture is **baked into vertex colours** (nearest sample at each
  vertex UV × baseColorFactor × COLOR_0); the renderer stays untextured.
- **LODs:** per-LOD meshes with switch distances; `asset_pick_lod` selects per instance from
  the camera distance (the church shows 128k / 50k / 15k / 3k triangles). A LOD with more
  triangles than the previous one logs a warning.
- **Collision (stub):** `collision.type` aabb / sphere parsed; radius and half extents are
  derived from the normalised bounds when not given. Nothing uses them yet.
- **Renderer:** `mesh_upload_indexed` (u32 index buffers via the RHI); scene.c keeps one
  instance list per asset and LOD, uploads GPU meshes lazily and frees the CPU copy; authored
  assets go through the same instanced pipeline as procedural meshes, so they receive the sun,
  cast shadow-cascade shadows and are frustum-culled with their town. Established towns
  (size ≥ 2) place the church on the landward side of the square, facing the harbour.
- **Tooling:** `tools/blender_export_glb.py` exports a `.blend` headlessly to GLB (textures
  embedded, optional decimated copy). GLB files are not committed (`game/assets/**/*.glb`);
  `game/assets/README.md` explains the layout.
- **Tests:** `asset_test` (CTest) builds GLB/glTF files in memory and on disk and checks baked
  transforms, generated normals, texture baking (2×1 BMP via data URI), scale / pivot / forward
  maths, material rules, LOD picking (including the bias), registry, cache accounting and 20+
  rejection paths.
- **Cache & LOD runtime (steps 6-7):** the church ships as four GLB files
  (`game/assets/church/Church_LOD0..3.glb`, one LOD node each selected by name); the texture
  bake cache is keyed by (model, image) so multi-file assets cannot cross-talk, and is bounded.
  Per-asset GPU bytes are tracked next to the CPU bytes; `asset.gpu_mb` and `asset.instances`
  land in the metrics CSV. A new dashboard **Assets** tab lists every registered asset (state,
  GPU MB, load ms, per-LOD triangles / effective switch distance / live instance counts) and
  holds the global `asset_lod_bias` knob: authored switch distances are multiplied by it, so
  0.5 drops to coarse LODs at half distance (performance) and 2 holds detail twice as far
  (quality). `MARE_DASHTAB=assets` opens the dashboard on any tab for screenshot runs.
  `MARE_LOD_BIAS` pins the knob for screenshot runs.
- **Fix — pivot per LOD:** LOD objects are usually scattered around the source scene (the Meshy
  export places Church_LOD0..3 at different translations), so normalising every LOD with LOD 0's
  pivot put LOD 1-3 metres away from the instance position and sank them into the terrain. Each
  LOD now derives its own pivot from its own bounds (scale still comes from LOD 0, and a warning
  fires when a LOD's extent differs from LOD 0 by more than 1.5x - a wrong node pick). The
  headless test gives the LOD 1 node a translation to pin this down.
  Verified in game: camera at 9 / 55 / 110 world units picks LOD 0 / 1 / 2, and
  `MARE_LOD_BIAS=0.04` shows the 3k-triangle LOD 3 in place next to the 128k LOD 0
  (`docs/screenshots/asset-church-lod0.png` vs `-lod3.png`, same town, same camera).
- **Fix:** `lighting.c` used `near`/`far` as local names, which `windef.h` defines as empty
  macros; the game did not compile with the current Windows SDK until they were renamed.

Known limits: textures are baked per vertex, so surface detail is only as fine as the mesh
(the 128k-triangle church reads well, a 500-triangle crate would not); normals pass through
non-uniform node scales approximately; one base-colour texture per material; Draco / meshopt
compressed files must be re-exported uncompressed.


## 0.7.6 — profiling hooks, slower clock, ship motion, visual fixes
Completes the renderer modernisation (step 10) and slows the simulation down to a watchable pace.

- **Profiling (step 10):** per-pass GPU timer queries read back without stalling, RHI VRAM
  accounting (textures / render targets / buffers), and process RAM. All shown in the dashboard
  Perf tab and written to the metrics CSV (`gpu.*_ms`, `mem.vram_mb`, `mem.ram_mb`).
- **Simulation speed:** keys 1-4 are now 0.33, 0.5, 1.0 and 3.0 days per real second (were 1, 3,
  8, 20), with 0.33 the default. A voyage now reads as a voyage.
- **Ship movement:** fleets are drawn from a smoothed render position that chases the simulated
  one, so ships sail continuously instead of stepping once per 0.1-day tick (measured: 387
  distinct render positions across 47 simulation ticks). Rocking gained a pitch axis and a second
  frequency, and its amplitude now follows what the ship is doing - moored, under way, or in a
  storm. The shadow pass uses the same vertex motion, so shadows follow.
- **Visual fixes:**
  - Detail normal maps are mipmapped, and the sea derives extra roughness from per-pixel normal
    variance (`dFdx/dFdy`). Together these remove the field of white fireflies that covered the
    ocean at middle and long range; a real sun glitter path remains up close.
  - Cannon fire was a v3 debug-draw wire sphere that read as a gold scribble. It is now real
    gunsmoke: an instanced puff on a translucent pipeline drawn after the sea, swelling and
    drifting, excluded from shadow casting.
  - Billowed sails use a blended normal, so a sail reads as one curved surface instead of four
    hard light and dark wedges.
  - Shallow water was muddy where the seabed showed through; the shoal tint is brighter and the
    transparency ramp shorter, so shallows read as water over sand.
- `MARE_CAM="lon,lat,dist[,yaw]"` pins the camera for reproducible screenshots.


## 0.7.5 — normal maps, frustum culling, instancing (renderer steps 7-9)
- Procedural tileable normal maps baked at init (`detail.c`): scrolling two-scale ocean waves on
  the sea (replacing the hand-written sines) and triplanar plank/plaster grain on the meshes,
  which carry no UVs.
- The sea is transparent in the shallows (seabed visible, foam stays opaque, map view solid) and
  piers and flagpoles now cast shadows, so docks shade the water under them.
- Per-object frustum culling for ports and fleets with a shadow-caster margin; culled counts in
  the dashboard.
- Instance buffers upload once per frame and are reused by the shadow cascades and main pass;
  the RHI reports draws/instances/triangles/streamed bytes per frame (dashboard + metrics CSV).
- Depth-precision fixes against triangle flicker: projections remapped from D3D-style clip z to
  GL's full [-1,1] range (camera and cascades), and double-sided quads no longer emit an exactly
  coplanar mirrored twin (shading is two-sided since 0.7.4).


## 0.7.4 — linear/sRGB, PBR, directional lighting, shadows (renderer steps 3-6)
- **Linear/sRGB pipeline:** author colours are converted to linear on input, all lighting runs in
  linear space, and `hdr.c` encodes to sRGB once after a Reinhard luminance shoulder.
- **PBR materials:** Cook-Torrance GGX + Smith + Schlick with hemispheric ambient. Mesh vertices
  carry roughness/metalness set as builder state (`mb_material`, presets for wood, paint, cloth,
  stone, tile, metal); terrain roughness follows slope/snow and the sea is a smooth dielectric
  with a real sun glint and Fresnel sky reflection.
- **Directional lighting:** one sun shared by terrain, meshes and sea (`lighting.c`:
  `render_light_update` + `light_bind`), replacing the per-shader ad-hoc diffuse+specular.
- **Shadows:** three sun cascades in one depth atlas, fitted to each frustum slice's ground
  footprint, texel-snapped, with normal-offset bias and 3x3 PCF. Ships, towns, forts and piers
  cast; the terrain receives only (see KNOWN_ISSUES). `--no-shadows` and a dashboard toggle.
- Mesh shading is now two-sided: sails, flags and roof slopes are single quads with inconsistent
  winding and previously fell to the ambient floor under PBR.
- Scene geometry is built once per frame and submitted twice (`scene_build_*` / `scene_submit_*`),
  so the shadow cascades reuse the main pass's instance lists.
- RHI gains depth-only targets, sub-rect viewports and depth readback; dev tools:
  `MARE_SHADOWDUMP`, dashboard exposure/shadow controls.


## 0.7.3 — HDR rendering (renderer modernisation step 2)
The 3D scene renders into an RGBA16F offscreen target and is tonemapped to the backbuffer
(`gfx/hdr.c`); UI draws on top. RHI gains render targets (`rhi_target_*`), backbuffer binding
and pixel readback. The tonemap is identity below 1.0 with a hue-preserving rolloff above, so
visuals are unchanged until later steps emit real HDR values. `hdr_exposure` multiplier;
`MARE_SCREENSHOT` env var captures the backbuffer to a PPM for smoke tests.


## 0.7.2 — RHI: API-agnostic renderer abstraction (design: docs/DESIGN_RENDERER.md)
First step of the renderer modernisation. All game rendering now goes through an API-agnostic
RHI (`gfx/rhi.h`): pipelines with baked depth/blend state, geometry objects with streamed
instance buffers, format-enum textures and cached-location uniforms. `gfx/rhi_gl.c` is the only
game file calling OpenGL; the backend include in `mare.c` is the single switch point for future
Vulkan/DX12 backends. `gpu.c` keeps the mesh builder and `mesh_t`/`inst_t` API on top of the RHI.
Framework fix: MSVC-only `#pragma comment(lib, ...)` lines in `ext` guarded with `_MSC_VER`
(clang-based Linux builds failed on dependent-library specifiers). No visual change intended.


## 0.7.1 — recovered Windows baseline (2026-09-06)

- Reconstructed source history with the supplied v3 archive and development docs.
- Fixed GNU-only region report expression for MSVC and colony hinterland identity.
- Added regression coverage, root build/launch scripts and pending-feature roadmap.
- Restored safe GLFW key normalization. Windows Release and both CTest suites pass;
  actual 3D game launch verified. See ../../docs/RECOVERY.md for precise scope.

## 0.7 — hinterlands & trading hubs (design: docs/DESIGN_HINTERLAND.md)
Implemented: region generation from coast segments + land BFS (`world/regions.c`, 160 regions, named
pins), climate potentials + rare deposits (iron/coal/copper/silver/gold, noise + historical pins),
hub attachment (≤3 per region, shares follow prosperity/traffic), region tick (`sim/region.c`: rural
production into export pools, rural consumption fed through hubs with two-way throughput, development
tiers, prosperity, inland mines/farms founded at tier crossings), ports as hubs (region demand deepens
markets, staples arrive from the region instead of the old abstract share, inland sales tax,
seigniorage, iron boosts arms output), directed colonisation (attractiveness, AI picks the best coast,
three-hub limit unless at war), Regions map overlay (development shading + borders), inland settlement
icons, Hinterland section in the port panel, site panel attractiveness. Save format v4; new tests.
Pending from the design: player labour sliders per region, chartered settlements, interior regions
switching hubs, rivers as throughput pins, region panel, copper hull upgrade, rare-good shipyard discount.

## 0.6 — bug hunt
Found by log mining (2,733 `no route` records), sanitizer runs and the known-issues list:
- **Data:** Maracaibo's harbour snapped into Lake Maracaibo (unreachable; merchants spawned there were
  stuck and re-planned forever). Gulf of Venezuela added to the coastline, port moved to the gulf.
  Somalia pulled 0.8° off the map edge so the Red Sea/Gulf of Aden connect to the Indian Ocean:
  Jeddah and Mocha are now reachable. New test: every port reachable from Amsterdam.
- **Fleets:** a fleet whose destination became hostile/blockaded/absorbed now logs why and diverts
  to the nearest friendly port (was: silent idle at sea).
- **Settlements:** foreign immigrants now come from the nations whose ships actually call (docked
  fleets + lane partners) instead of a random nation; Fort levels now garrison a town
  (`FORT_GARRISON_CALM` divides culture unrest), giving the player a counter to post-conquest revolts.
- **Treasure fleet:** when playing Spain the flota is under the player's command (AI no longer
  re-orders it); silver still pays out on docking at a home port.
- **Rendering/UI:** greedy label collision avoidance by priority (selected/hovered > yours > size);
  town instances cached per port and rebuilt only when size/fort/owner/population change
  (`scene_reset_caches()` on new game/load); dashboard log autoscroll pauses while scrolled up.
- **Diagnostics:** metrics CSV waits 2 s before its header and re-emits a `#`-prefixed header when
  columns change.
- **Navigation:** founding a colony computes one distance row instead of invalidating all (0.5).

## 0.5 — colonisation expansion
- Free colony placement with climate/terrain resource potentials, labour allocation,
  population capacity & immigration, cultures, assimilation, civil rebellion, settlement merging,
  AI coast claims. Production/Population panels. Save format v3. `nav_ports_added()`.
## 0.4 — cross-platform hardening
- Windows audit (MAX_PATH collision, stack sizes, POSIX in tests, console colours, NOMINMAX),
  HiDPI mouse scaling, strict C11 compliance, CMake project, GitHub Actions matrix, build.bat rewrite.
## 0.3 — observability & performance
- `diag` (leveled file logging, metrics, profiler, crash handlers), dev dashboard, metrics CSV,
  render-pass toggles. Root-caused the 2 FPS cap (monitor 0 Hz), removed per-call `app_width()`
  (−16 ms/frame), cached market norms (−30% sim time), O(1) rejection of cross-basin paths.
## 0.2 — restructure
- Split into core/data/world/sim/gfx/ui modules; simulation made framework-independent and
  deterministic (rng/history/timers in `game_t`, synchronous distance rows); stable fleet ids;
  save header; structured chronicle with listener; fine-grid path smoothing; headless test suite.
## 0.1 — first playable
- World, economy, trade AI, pirates, warfare, sieges, events, history, rendering with LOD and
  map view, microui HUD, save/load.
