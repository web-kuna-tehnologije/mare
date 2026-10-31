# Authored ships: the Generation I fleet

The ships were procedural boxes lofted in `gfx/meshes.c`. They are now assembled from authored
parts imported from Meshy AI — a hull, a mast assembly and, for the oared Mediterranean
interceptors, a bank of oars — and submitted through the ordinary authored-asset path, so they
inherit screen-space LOD selection, cross-fades, residency, the shadow caster pass and the wave
displacement that makes a hull ride the swell. The procedural hulls stay as the fallback: a
class that has not finished loading, or was never imported, draws the old mesh rather than
nothing.

Reference art (dimensions, mast layout, oar counts, materials) lives with the art sources in
`assets/ships/design`, one sheet per class.

## 1. The classes

Every sheet gives a length overall, a beam, a keel-to-deck height and a mast layout. Those
numbers are the single source of truth: `tools/gen_asset_descriptors.py` sizes the assets from
them and `gfx/shipyard.c` places the parts from them, both through the same constant.

**1 metre = 0.045 world units.** That factor is chosen so a 21 m coastal trader occupies the
~0.95 world units the procedural sloop did, which keeps every existing camera distance, LOD
threshold and formation spacing valid.

| class | role | length | hull asset | mast asset | rig (fleet_sails.png) | oars |
|-------|------|--------|-----------|------------|------------------------|------|
| coastal_trader | merchant | 21 m | `coastal_trader_hull` | `coastal_trader_masts` m1, m2 | main: course + topsail; lateen mizzen | — |
| cog | merchant | 25 m | `cog_hull` | `cog_masts` m0 | one great course | — |
| fusta | interceptor | 25 m | `fusta_hull` | `fusta_masts` m1, m2 | two lateens | 20 pairs |
| galiot | interceptor | 28 m | `galiot_hull` | `mediterranean_masts` m0 ×2 | two lateens | 22 pairs |
| armed_cog | warship | 32 m | `armed_cog_hull` | `armed_cog_masts` m0 ×2 | main: course + topsail; lateen mizzen | — |
| hulk | bulk merchant | 35 m | `hulk_hull` | `hulk_masts` m0..m2 | three square | — |
| carrack | merchant | 38 m | `carrack_hull` | `carrack_masts` m0..m2 | square fore and main, lateen mizzen, spritsail | — |
| war_carrack | warship | 47 m | `war_carrack_hull` | `war_carrack_masts` m0, m1, m2 ×2 | four masts: fore, main (three sails), lateen mizzen and bonaventure, spritsail | — |
| great_carrack | flagship | 56 m | `war_carrack_hull` ×1.19 | `war_carrack_masts` m0, m1, m2 ×2 | as the war carrack, taller | — |

The galiot has no mast model of its own and borrows the generic Mediterranean lateen rig, which
is what its sheet shows anyway. Oars come from `galley_oar`, exported from the light-galley
sources. The great carrack sails on the war carrack's model stretched to its own length until
its hull is generated; the four-mast rigs stand the mizzen part twice until the rig is split
into four (`docs/TODO.md`).

Every class also carries its **stats** (`ship_stats_t`, read with `shipyard_class_stats()`):
length, beam, draft, displacement, best speed under sail and under oars, rate of turn, guns and
crew. The staged battle sails by them (section 3b); the buoyancy pass to come will read
displacement and draft.

| class | tonnes | knots (oars) | turn °/s | guns | crew |
|-------|-------:|-------------:|---------:|-----:|-----:|
| coastal_trader | 60 | 5.0 | 6.0 | 4 | 15 |
| cog | 150 | 4.5 | 4.0 | 2 | 20 |
| fusta | 80 | 6.5 (7.5) | 6.0 | 4 | 90 |
| galiot | 120 | 6.0 (7.0) | 5.0 | 6 | 120 |
| armed_cog | 260 | 4.5 | 3.5 | 14 | 70 |
| hulk | 400 | 4.0 | 3.0 | 8 | 50 |
| carrack | 500 | 5.0 | 3.0 | 20 | 90 |
| war_carrack | 1000 | 4.5 | 2.2 | 40 | 250 |
| great_carrack | 1500 | 4.0 | 1.8 | 74 | 500 |

## 2. Which class a ship is

A game ship type is a *role*, not a hull: the same Frigate is an armed cog in the Channel and a
galiot in the Adriatic. `class_for()` maps type and nation:

| ship type | default | Mediterranean (Venice, Ottomans, pirates) | northern (England, Netherlands, Denmark) |
|-----------|---------|-------------------------------------------|-------------------------------------------|
| Sloop | coastal_trader | **fusta** (oared) | — |
| Fluyt | cog | **galiot** (oared) | — |
| Merchantman | carrack | — | **hulk** |
| Frigate | armed_cog | **galiot** (oared) | — |
| Galleon | war_carrack | — | — |
| Ship of the Line | great_carrack | — | — |

The great carrack's hull is not among the imported models yet: its class row points at the war
carrack's assets with `model_scale` 56/47. Drop a `great_carrack_hull` in, change the row's
`hull` and clear the scale, and nothing else moves.

## 3. Assembly

All three parts are instances of ordinary assets, positioned in world space (the instance
transform is position + heading + scale, so a part's offset along the hull is rotated by the
ship's heading on the CPU):

- **Hull** — fitted to its class length. Its descriptor pivot is `base_center`, i.e. the keel,
  so the instance is lowered by `draft × hull height` to put the waterline at y = 0, where the
  wave displacement in the mesh shader expects it.
- **Masts** — one part per station (`mast_part[k]` picks the fore, main or mizzen part of the
  split rig; a one-mast model's narrowed `_m0` stands at every station), fitted by height,
  pivoted at their foot, raised to `deck × hull height` above the keel and placed at
  `mast_at[k]` along the hull, scaled by `mast_h[k]`.
- **Oars** — one instance per oar, both sides, spread over `oar_span` of the hull length at
  `beam_frac` out from the centreline, turned 90° so the blade is outboard. They are only
  submitted when the ship is more than `OAR_MIN_PX` (260 px) across, since below that an oar is
  a pixel; a 22-pair galiot adds 44 instances of one mesh, which is one instanced draw.

`draft`, `deck`, `mast_at`, `mast_h`, `sail`, `oar_span` and `beam_frac` are per class in
`classes[]` — they are proportions of the model, so they are the knobs to turn when a rig sits
too high, a sail is the wrong kind or an oar bank too far aft.

## 3b. The staged scenes

`gfx/staged_scene.cc` stages its own battles and city squares at `DV_SHIP_SCALE` (about nine
times the map's scale), which is where LOD 0 earns its place: a hull fills the screen and shows
its planking, gunports and railings. The staged ships go through the same `shipyard_push`, with
`shipyard_req_t.scale` carrying the magnification, and the staged scene's asset bracket spans
both ships and buildings so authored hulls are submitted and cast shadows in battle view as
well as city view.

Every staged ship sails by her class's stats: `battle_add_ship` gives her 55–90 % of her best
speed (a carrack makes about four knots; `DV_KNOT` converts) and her class's rate of turn,
and `battle_update` lets her weave about her course no faster than that. Moored ships in the
city view stir at a quarter of it. `MARE_DV_CAM="dist,yaw[,x,z[,pitch]]"` pins the staged camera
(local offsets from the scene centre) for screenshots.

## 3c. Rigging, canvas and flags

The generated models are bare: hulls and spars, no cordage and no sails. Both are built
procedurally, one pair of meshes per class, in the ship's own frame normalised to its hull
length (+x bow, +y up from the waterline, +z starboard), so one instance - position, heading,
scale - dresses any ship of that class. They go through the instanced mesh pipeline, so they
roll, pitch and ride the swell with the hull they belong to, and they cast shadows with it.

- **Cordage** (`rig_rope`): shrouds three a side from each masthead to the channels, a forestay
  to the next mast's foot or the bow, a backstay aft, and ratlines on the main shrouds. Each
  rope is a cross of two double-sided quads - a single quad vanishes edge-on and a tube would
  cost twenty times as much for something a few pixels wide. Tarred hemp, near black.
- **Canvas**, by the class's `sail[k]`: `SAIL_SQUARE1` one great course (head at 0.80 of the
  mast, foot at 0.30), `SAIL_SQUARE2` course and topsail, `SAIL_SQUARE3` course, topsail and
  topgallant, `SAIL_LATEEN` a settee-cut lateen on a yard raked from the tack forward and low to
  the peak above the masthead. Every yard is drawn as a spar (`rig_bar`), so the canvas hangs
  from a visible beam whatever the generated mast's own yards do; the carracks get a bowsprit
  with a spritsail slung under it. Square sails hang `RIG_SAIL_OFF` forward of the mast, on the
  yard's fore side.
- **Flags**: a ribbon at the main masthead with `mat` 1, so it takes the nation's colour from
  the instance like every other flag in the game.

Both meshes are built the first time a class's hull *and* rig are resident, since their sizes
set the mast stations. Below `RIG_MIN_PX` (90 px) the whole rig is skipped - cordage below a
pixel is noise.

### Yards, cordage and flags (0.19)

**Yards are authored.** Every sail hangs from an imported spar: the lantina
(`assets/elm/lateen_yard`, a part-segmented 45-degree spar slung at its middle, low end forward
after the import's `-x` turn) on each lateen station, and the square yards split off the mast
library (`yard_cog_single/course/top`, `yard_carrack_course/top`, `yard_great_course/main/top`,
generic `square_yard`) on each square layer, chosen per mast type by `yard_sets[]`. Yard and
canvas are instanced on the same swung (lateen, up to ~70 degrees) or braced (square, up to
~30) heading about the mast axis, so they turn to the wind as one piece. The unit lateen canvas
is cut to the asset's line (`LAT_TACK_*` / `LAT_PEAK_*`, mast-height units), the square yard is
normalised to unit span across (descriptor: forward `+z`, fit `width` 1, pivot `center`) and
scaled to the sail's head; both are placed from the asset's own bounds at run time, so the
canvas hangs on its spar by construction. Past `YARD_ASSET_MIN_PX` (44 px of projected spar) a
decimated thin spar is a sliver, so the sail switches to the cut of its unit mesh that carries a
procedural spar (`lateen_cloth_mesh[1]`, `sq_cloth_mesh[kind][1]`).

**Cordage is physical.** A rope is a line segment (`mb_line`) with its half-width in the
roughness channel - `RIG_ROPE_W` 0.0009 of hull length, a 4 cm shroud on a carrack, stays x1.35,
ratlines x0.6 - drawn through the rope pipeline (`meshes/rope.vert` + `rope.geom`, GL 3.2
geometry stage): the stage projects that width to pixels, never under 0.6 px, and shades the
quad as a cylinder. Without a geometry stage (`MARE_GEOM=0`) the old cross quads stand in,
thinner than before. Line ropes are not submitted to the shadow pass (a triangle pipeline
cannot draw them, and they would not show in a shadow texel). Tarred-hemp brown, not black.

**Flags are simulated.** `flag_sim_t`: a 9x5 verlet grid per ship in the ship's frame (hull
fractions), pinned along the hoist to the main masthead. Gravity is 9.81 m/s² over the class's
length; the wind is the true wind turned into the ship's frame less her way (`FLAG_WIND_MS`,
`FLAG_WAY_MS`), with drag towards the air, gusts along the fly and four constraint iterations
(structural + shear). The mesh is rebuilt every frame (`rhi_geometry_update`) and drawn with the
rig's instance, so the mesh shader's roll, pitch and swell carry it with the mast. The nearest
48 rigged ships keep a flag; beyond that the least recently seen slot is reused.

### How the cloth moves

There is no integration; the shape is the steady state of a sheet pinned along one edge,
evaluated per vertex in the mesh shader (`GLSL_CLOTH`, meshes.c), so it costs nothing on the
CPU and keeps the one-instance-per-ship draw.

Every canvas vertex carries a **cloth code** in the metalness channel (`mb_cloth_code`,
gpu.h): 32 levels of how free it is - 0 where it is bent to its spar, 1 at the loose foot -
plus where it sits between the leeches, -1..1; its roughness channel carries the panel's
half-width; its normal is the direction the canvas bellies in (+x for a square sail, +z for a
lateen or a flag). The instance carries the true wind at the ship in `fx2`/`fx3` (world xz,
direction times strength; `wind_at`), turned into the ship's frame in the shader. The fragment
stage then forces the channels back to canvas (roughness 0.9, metalness 0), so their day job is
unaffected.

A **sail** is pressed along its normal by the wind through it; a square sail also draws from
the ship's own way (`i_misc.x`), so a ship under sail never shows slack canvas, and a lateen,
sheeted to one side, always holds a little shape. The belly is a sine from head to foot (zero
at the yard, fullest two thirds down, held at the foot by the sheets) times a cosine between
the leeches, scaled by the panel's half-width, so a topgallant bellies less than a course. When
the wind runs along the canvas instead of into it the sail **luffs**: the flutter grows from a
ripple to a shake. A square sail pressed back by a head wind goes **aback against its mast**:
the middle of the sail stops, only the leeches blow past - which is what the sail hitting its
beam looks like. The shading normal is tilted from the same profile, so the belly is lit as a
curve. A **flag** is swung from the staff into the apparent wind (the true wind less the ship's
way), droops in a light air and ripples along its length. The shadow pass runs the same model,
so a sail's shadow is the sail's shape.

### Oars

The bank swings together, as a rowed ship does, at a sweep's 20-26 strokes a minute, at a rate
and reach that follow how hard she is being pulled (`rock`); a ship lying still keeps her oars
out and level. The blades dip on the pull and lift on the recovery - height is the one other
axis the instance transform has, and it is exactly the part of the cycle that reads from the
quarterdeck. The bank sits just outboard of the rail (`OAR_INBOARD` 0.22 of the oar's length).

### Buoyancy

A class's displacement sets how she sits and rides (shipyard_push): `resp = 1.45 - 0.22
log10(tonnes)`, clamped 0.55..1.15, scales the wave field the mesh shader couples her to
(`inst_t.bob`), her roll and pitch (`sway`), and how low she floats on her lines - a fusta is
thrown about by every sea at 1.03, a laden great carrack shoulders through it at 0.75 and sits
visibly deeper. Masts, rig and oars copy the hull's instance fields, so the whole assembly
rides as one body.

### The battle line

The staged battle fights in formation (staged_scene.cc): each side sails a stadium circuit - two
straight legs joined by half-turn caps - in **line ahead**. Every ship chases a pursuit point
just ahead of the station her slot gives her, never turning faster than her class can
(`turn_dps`), trimming speed against the station (astern crowds on sail, ahead she luffs up),
the whole line at 0.8x its slowest sail. Because the stations round the caps one after another,
the line **turns in succession** where the leader turned. The two circuits mirror each other so
the inner legs run opposed `DV_LINE_SEP` (28 local units, roughly a cable) apart - inside the
55-unit gun range, outside ramming distance - and the lines close, hold the range on the pass,
trade broadsides and wear round the cap. The city view's moored ships keep their old gentle
wander.

## 3d. The water a ship pushes aside

The sea (`gfx/sea.c`) takes up to 24 nearby ships each frame - position, heading, speed in
*hull lengths per second*, hull length (`sea_set_ships`; scene.c gives the fleets near the
look-at point, staged_scene.cc its staged ships). In the vertex stages `ship_wave()` heaps water
at every bow: a crest at the stem that rings outward *ahead* of her and round the shoulders,
decaying within a hull length, plus the pile-up along the forward half of the hull, and
nothing astern. Its slope is folded into the surface tangents by finite differences, so the
heap is lit. The fragment stage foams on the same mask - the stem's splash, a thin break along
the forward half of the hull, streaks running out ahead, and the crest of the bow wave breaking
where the foam texture is bright - and nowhere behind the ship: at this scale a wake reads as a
scar on the sea, and what the eye wants is the water in front of a moving bow.

## 4. Level of detail

Imported models carry five LODs (`tools/meshy_import.py`). A ship hands over far sooner than a
building because LOD 0 is the original scan:

| LOD | triangles | drawn while the hull is |
|-----|-----------|--------------------------|
| 0 | ≤300 000 | over 400 px — it fills the screen in battle or city view |
| 1 | 24 000 | 130–400 px |
| 2 | 7 000 | 45–130 px |
| 3 | 2 000 | 15–45 px |
| 4 | 600 | under 15 px, down to the icon hand-over |

The raw Meshy scans are 0.8–2.3 M triangles for a hull, which is 45–70 MB of vertex data per
asset once the runtime has baked textures into vertex colours — well over a gigabyte across the
fleet, for detail far below a pixel even at LOD 0's range. The importer caps LOD 0 at 300 k,
which is the detail a hull actually shows when it fills the screen in battle view.
`BUDGETS["ship"][0]` in `tools/meshy_import.py` is the knob; 0 keeps the untouched scan.

Two things keep that affordable. LOD 0 is the one level the asset commit does **not** upload
eagerly: it reaches the GPU only when an instance actually picks it (`asset_upload_lod`), so a
fleet on the horizon costs no VRAM for a level it will never draw. And `asset_lod0_trim` gives
back the *CPU* copy of any level that has sat un-uploaded for 45 seconds — most LOD 0s are
never drawn in a session — reloading the asset if something later picks it. The dashboard's
LOD tab reports how much has been trimmed.

## 5. What it costs

`MARE_SHIPS=0` draws the procedural hulls instead, which is the A/B for "is the authored fleet
paying for itself". The dashboard's **LOD** tab reports authored ships, oar instances and
fallbacks per frame.

Measured over Venice at 9 units, with two oared galiots and their full oar banks in frame
(integrated GPU, 1600x900):

| | authored | procedural |
|---|---|---|
| frame | 11.3 ms (87 fps) | 9.1 ms (108 fps) |
| GPU, assets | 49.3 MB | 43.3 MB |
| instances | 5 921 | 5 723 |

So the fleet costs about 2 ms and 6 MB in the view that asks the most of it — an oar bank is
44 extra instances per ship. VRAM stays low because LOD 0 is never uploaded until something
picks it.

The cost that is *not* visible in that table: every imported LOD 0 is parsed and kept on the
CPU at load (about 9.5 MB per hull, ~160 MB across the fleet), whether or not it is ever
uploaded, because the asset runtime builds all LODs in one pass. Both legs of the A/B pay it,
since the startup preload requests every registered asset. Lowering `BUDGETS["ship"][0]` in
`tools/meshy_import.py` is the knob if that matters more than battle-view detail.

## 6. Importing more

See [BLENDER_LOD.md](BLENDER_LOD.md) section 5 for the Meshy workflow end to end. In short:

```
python3 tools/meshy_import.py --root <meshy download folder> --kind ship
python3 tools/gen_asset_descriptors.py game/assets
```

Then add the class to `classes[]` in `gfx/shipyard.c`, its length to `SHIP_HULLS_M` (hull) or
`SHIP_MASTS_M` (rig) in `tools/gen_asset_descriptors.py`, and map a ship type to it.
