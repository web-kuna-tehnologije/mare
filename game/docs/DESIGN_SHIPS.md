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

| class | role | length | hull asset | mast asset | oars |
|-------|------|--------|-----------|------------|------|
| coastal_trader | merchant | 21 m | `coastal_trader_hull` | `coastal_trader_masts` | — |
| cog | merchant | 25 m | `cog_hull` | `cog_masts` (single mast) | — |
| fusta | interceptor | 25 m | `fusta_hull` | `fusta_masts` (lateen) | 20 pairs |
| galiot | interceptor | 28 m | `galiot_hull` | `mediterranean_masts` (lateen) | 22 pairs |
| armed_cog | warship | 32 m | `armed_cog_hull` | `armed_cog_masts` | — |
| hulk | bulk merchant | 35 m | `hulk_hull` | `hulk_masts` | — |
| carrack | merchant | 38 m | `carrack_hull` | `carrack_masts` | — |
| war_carrack | warship | 47 m | `war_carrack_hull` | `war_carrack_masts` | — |

The galiot has no mast model of its own and borrows the generic Mediterranean lateen rig, which
is what its sheet shows anyway. Oars come from `galley_oar`, exported from the light-galley
sources.

## 2. Which class a ship is

A game ship type is a *role*, not a hull: the same Frigate is an armed cog in the Channel and a
galiot in the Adriatic. `class_for()` maps type and nation:

| ship type | default | Mediterranean (Venice, Ottomans, pirates) | northern (England, Netherlands, Denmark) |
|-----------|---------|-------------------------------------------|-------------------------------------------|
| Sloop | coastal_trader | **fusta** (oared) | — |
| Fluyt | cog | **galiot** (oared) | — |
| Merchantman | carrack | — | **hulk** |
| Frigate | armed_cog | **galiot** (oared) | — |
| Galleon | war_carrack ×0.92 | — | — |
| Ship of the Line | war_carrack ×1.18 | — | — |

The Ship of the Line wants the Great Carrack of the design sheets (50–60 m), which is not among
the imported models: it stands in as a stretched war carrack until that one is generated. Drop
a `great_carrack_hull` / `great_carrack_masts` pair in, add the row to `classes[]` and the
mapping, and the stretch goes away.

## 3. Assembly

All three parts are instances of ordinary assets, positioned in world space (the instance
transform is position + heading + scale, so a part's offset along the hull is rotated by the
ship's heading on the CPU):

- **Hull** — fitted to its class length. Its descriptor pivot is `base_center`, i.e. the keel,
  so the instance is lowered by `draft × hull height` to put the waterline at y = 0, where the
  wave displacement in the mesh shader expects it.
- **Masts** — fitted by height, pivoted at their base, raised to `deck × hull height` above the
  keel and offset along the hull by `mast_dx` where a class needs it.
- **Oars** — one instance per oar, both sides, spread over `oar_span` of the hull length at
  `beam_frac` out from the centreline, turned 90° so the blade is outboard. They are only
  submitted when the ship is more than `OAR_MIN_PX` (260 px) across, since below that an oar is
  a pixel; a 22-pair galiot adds 44 instances of one mesh, which is one instanced draw.

`draft`, `deck`, `mast_dx`, `oar_span` and `beam_frac` are per class in `classes[]` — they are
proportions of the model, so they are the knobs to turn when a rig sits too high or an oar bank
too far aft.

## 3b. The staged scenes

`gfx/detailview.c` stages its own battles and city squares at `DV_SHIP_SCALE` (about nine
times the map's scale), which is where LOD 0 earns its place: a hull fills the screen and shows
its planking, gunports and railings. The staged ships go through the same `shipyard_push`, with
`shipyard_req_t.scale` carrying the magnification, and the staged scene's asset bracket now
spans both ships and buildings so authored hulls are submitted and cast shadows in battle view
as well as city view.

## 4. Level of detail

Imported models carry five LODs (`tools/meshy_import.py`). A ship hands over far sooner than a
building because LOD 0 is the original scan:

| LOD | triangles | drawn while the hull is |
|-----|-----------|--------------------------|
| 0 | ≤150 000 | over 400 px — it fills the screen in battle or city view |
| 1 | 24 000 | 130–400 px |
| 2 | 7 000 | 45–130 px |
| 3 | 2 000 | 15–45 px |
| 4 | 600 | under 15 px, down to the icon hand-over |

The raw Meshy scans are 0.8–1.8 M triangles for a hull, which is 45–68 MB of vertex data per
asset once the runtime has baked textures into vertex colours — about a gigabyte across the
fleet, for detail far below a pixel even at LOD 0's range. The importer caps LOD 0 at 150 k
(six times LOD 1) and the whole fleet costs ~100 MB. Raise `BUDGETS["ship"][0]` in
`tools/meshy_import.py` to 0 for the untouched scan.

LOD 0 is also the one level the asset commit does **not** upload eagerly: it reaches the GPU
only when an instance actually picks it (`asset_upload_lod`), so a fleet on the horizon costs
no VRAM for a level it will never draw.

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
