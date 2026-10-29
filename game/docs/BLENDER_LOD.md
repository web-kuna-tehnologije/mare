# The Blender LOD pipeline: how to get a model into the game

Three tools turn a `.blend` into the five-LOD GLB set the asset runtime streams
(`game/src/assets`, progressive loading icon -> LOD4 -> ... -> LOD0). Nothing here needs the
game rebuilt: assets are data.

## 1. One model: tools/blender_export_glb.py

```
blender -b <source.blend> --python tools/blender_export_glb.py -- <out_dir> <Name> [tris0,..] [tex_px]
blender -b <source.blend> --python tools/blender_export_glb.py -- --validate
```

- Writes `<out_dir>/<Name>_LOD0.glb .. <Name>_LOD4.glb`, each holding one node named
  `<Name>_LODn` — the descriptor keys on that node name, so keep it.
- If the .blend already contains objects named `*_LOD0..n` (hand-authored LODs), they are
  exported object by object. A single-mesh source is **decimated automatically** to the
  triangle budgets `80000, 24000, 7000, 2000, 600` (override with the `tris0,..` argument,
  e.g. `120000,30000,9000,2500,800` for a hero building).
- Materials: only Base Color survives (the game bakes it into vertex colours); textures are
  embedded as JPEG and downscaled to `tex_px` (default 1024). Modifiers are applied, +Y up.
- `--validate` loads the .blend, prints what would be written, writes nothing: run it first.

## 2. A whole source tree: tools/export_assets.py

```
python3 tools/export_assets.py --check            # no Blender, no writes: list the plan
python3 tools/export_assets.py --validate         # Blender loads each .blend, writes nothing
python3 tools/export_assets.py                    # the real export (~30-60 s per asset)
python3 tools/export_assets.py --only church,lighthouse --jobs 4
```

Sources live outside the repository (default `../assets` next to the checkout). Meshy export
names like `Meshy_AI_Small_Wooden_House_1024...blend` become `small_wooden_house`
automatically. Already-exported assets are skipped unless `--force`.

## 3. Descriptors and the manifest: tools/gen_asset_descriptors.py

```
python3 tools/gen_asset_descriptors.py game/assets
```

Writes `<name>.asset.json` for every exported set and regenerates `game/assets/manifest.json`.
Existing descriptors are **left alone**, so hand tuning survives a re-run. The generated
descriptor sets:

- `type`/`role` from the name (roles are what the town generator asks for: "church",
  "warehouse", "poor_house"...; see the HEIGHTS table in the script),
- world size (`fit: height`) from the same table — a procedural house is ~0.3 units tall,
  the church 0.55,
- LOD switch points in projected 1080p pixels: LOD0 above 220 px, then 90 / 30 / 12 px.

Start the game; the registry picks the manifest up, towns rebuild with the new asset the
moment it lands (asset_generation), and the dashboard **Assets** / **LOD** tabs show its
residency per LOD.

## 4. Map chunks: generating the world a tile at a time

A tactical chunk (8 degrees, 32 world units) or coastal tile (16 degrees, 64 units) of the
streamed world can be replaced by an authored mesh - modelled by hand or generated with Meshy.
The game tells you which tiles are missing and exactly what one has to look like.

**Pick a tile.** The dashboard **Streaming** tab lists the wanted chunks that have no mesh yet
by name and region, and the tile overlay (toggle there, or `MARE_STREAM_TILES=1`) draws a red
cross on each of them in the world.

**Get its brief.** Headless, no window:

```
./mare --map-chunk=3,12,7 --seed=7               # level, tx, ty [, resolution]
python3 tools/ppm2png.py map_chunk_l3_12_7.ppm   # the reference image, for Meshy
```

which prints the asset name, the geographic extent, the world rect, the height range and the
land fraction, and writes a top-down shaded-relief reference of that tile (sea by depth, land
by height, sun from the north-west). Pass the world's seed, or the reference will not match the
terrain the chunk has to merge into.

**Generate or model it.** Feed the reference PNG to Meshy's image-to-3D, or model it in
Blender. Either way the mesh must be:

- **32 world units across** for an L3 chunk (64 for L2) - the importer fits it to that, so the
  proportions matter more than the absolute size;
- **sea level at y = 0** - the chunk keeps its own origin (`"pivot": "keep"`), and the streamer
  places it at the tile centre at y = 0;
- **+x east, +z south**, matching the reference image's orientation.

**Import it** (a Meshy download, or any GLB in a folder):

```
python3 tools/meshy_import.py --kind terrain --root <download folder> --name tile_l3_12_7
python3 tools/gen_asset_descriptors.py game/assets
```

`--name` forces the asset name, which is how a folder called
`Meshy_AI_rocky_coast_..._a1b2c3` becomes `tile_l3_12_7`. The generator recognises the
`tile_l<level>_<x>_<y>` pattern and writes a `terrain` descriptor fitted to the tile's world
size with the LOD thresholds a large object needs.

**Check it.** Run the game: the streamer marks the tile authored, the overlay's red cross turns
into a bright green frame, and the mesh draws at the tile's centre through the normal asset
path - LODs, residency, shadows and all. If the shoreline sits above or below the water, nudge
`pivot_offset[1]` in the descriptor; nothing else needs rebuilding.

## 5. Ships and other Meshy models

`tools/meshy_import.py` imports a whole tree of Meshy downloads. Meshy writes one folder per
model holding a single `Meshy_AI_model.glb`, named after the prompt; the importer strips
Meshy's decorations and the prompt-engineering words ("faithful", "bare", "keel", "counter",
"lateen", bare numbers), so

```
Meshy_AI_armed_cog_hull_faithful_keel_3d_20261029_image-to-3d-texture_d35aeea1/
```

becomes the asset `armed_cog_hull`.

```
python3 tools/meshy_import.py --check --root ../assets/ships     # the plan: names, sizes
python3 tools/meshy_import.py --root ../assets/ships --jobs 3    # the real import
python3 tools/gen_asset_descriptors.py game/assets
```

`--kind` (`ship`, `ship_part`, `building`, `terrain`, `prop`) picks the LOD budgets and texture
size. LOD 0 keeps the model at its import budget - 150k triangles for a ship, effectively the
original scan minus the noise, drawn only when the object fills the screen. See
[DESIGN_SHIPS.md](DESIGN_SHIPS.md) for what happens to a ship after import: how it is sized
from the design sheets, which game ship type uses it, and how hull, masts and oars are
assembled.

## Checking the result

- `MARE_DASHTAB=lod` / `assets` / `streaming` with `--dev` opens the dashboard on the right tab.
- `asset.lod_fallbacks` must fall to 0 moments after the camera settles: while it is non-zero
  the scene is showing a coarser LOD than it picked (streaming in progress).
- `MARE_ZOOM="60,900,10"` flies the camera through every LOD; `lod.pops` must stay 0.
