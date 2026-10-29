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

## 4. Terrain tiles (world streaming, DESIGN_STREAMING.md)

A tactical chunk (8x8 deg) or coastal tile (16x16 deg) of the streamed world can be replaced
by an authored mesh. The dashboard **Streaming** tab and the tile overlay (toggle there, or
`MARE_STREAM_TILES=1`) list the wanted grids that have no mesh yet, by name:

1. Pick a tile from the list, e.g. `tile_l3_12_7` (the label tells you the geographic area;
   the tile's world bounds are `8 deg = 32 world units` per side for L3, 64 for L2).
2. Model it in Blender with the tile's centre at the origin; 1 Blender unit = 1 world unit
   is simplest (the descriptor can rescale with `"scale": {"factor": ...}` if not).
3. Export: `blender -b tile.blend --python tools/blender_export_glb.py -- game/assets/tile_l3_12_7 tile_l3_12_7`
4. `python3 tools/gen_asset_descriptors.py game/assets` — then edit the generated descriptor:
   set `"type": "terrain"` and remove the `fit` scaling if you modelled at world scale.
5. Run the game: the streamer marks the tile authored, the overlay's cross disappears, and
   the mesh draws at the tile's centre through the normal asset path (LODs, budget, eviction).

## Checking the result

- `MARE_DASHTAB=lod` / `assets` / `streaming` with `--dev` opens the dashboard on the right tab.
- `asset.lod_fallbacks` must fall to 0 moments after the camera settles: while it is non-zero
  the scene is showing a coarser LOD than it picked (streaming in progress).
- `MARE_ZOOM="60,900,10"` flies the camera through every LOD; `lod.pops` must stay 0.
