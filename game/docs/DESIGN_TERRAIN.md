# Authored terrain: the real Earth as chunks of quads

The heightfield in `world/world.c` was always a placeholder — a distance-to-coast ramp plus
fbm. This replaces its *detail* with authored data while leaving the coastline the simulation
placed its ports against exactly where it was.

## 1. The three maps

Equirectangular over the playable extent (lon -130..46, lat -58..72), under
`assets/terrain/`:

| file | what | read as |
|------|------|---------|
| `depth_map_terrain.png` | height and bathymetry | 1 channel, grey |
| `normal_map_terrain.png` | matching surface normals | 3 channels, tangent-space RGB |
| `terrain_textures.png` | 6x3 atlas of ground materials | 3 channels, RGB |

**None of them is required.** A missing map leaves the procedural world exactly as it was, so
the game runs with the art half-finished and improves as each file lands — which is the state
it is in now, and the state it should stay usable in.

Calibration lives in `world/terrainmap.c`: `TM_SEA_LEVEL` is the grey that means the
shoreline (0.52 for the supplied bathymetry), `TM_HEIGHT_SCALE` the world height of pure white
and `TM_DEPTH_SCALE` the depth of pure black. Those three numbers are what you turn if the new
coastline sits above or below the old one.

## 2. How it joins the world

`terrainmap_apply_to_world()` folds the authored height into the base heightfield **only where
the two agree about land and water**. The ports, the navigation grid and the trade lanes were
all built against the procedural coastline; letting an image move a shoreline would strand a
harbour inland or put a town in the sea. So the authored map supplies relief, never geography.
On the supplied depth map that is 68 % of cells — the interiors — and the disputed 32 % is
almost entirely the coastal fringe.

Everything downstream then sees one consistent world: `world_height` is the folded field, nav
rebuilds from it, and the renderer's chunks sample the same texture they always did.

## 3. Chunks of quads

The renderer already draws the world as 64x64-cell chunks at five grid resolutions, picked by
distance (`gfx/terrain.c`), and the streamer already decides which chunks are resident at what
level (`world/stream.h`, DESIGN_STREAMING.md). The authored maps slot into that unchanged:

- a chunk's vertices read the folded heightfield, as before;
- its fragments can take their normal from `normal_map_terrain.png` instead of a finite
  difference of the heightfield, which is what puts real erosion detail on a slope that the
  8-cells-per-degree base map cannot carry;
- its ground material comes from `terrainmap_ground(height, slope, latitude)` — beach, cliff,
  bare high ground, tundra, the desert belt, grass — which indexes the atlas.

`terrainmap_ground` is a pure function of its arguments so the same rule can run on the CPU
(placement, the minimap) and in the shader, and the two cannot disagree.

## 4. Scale

The world is 4 world units per degree (`WORLD_K`), which makes the playable extent 704 x 520
units for ground that rises ~9 units — vertically exaggerated by roughly an order of magnitude,
which is why a mountain range reads as a ridge of sticks rather than terrain.

Doubling `WORLD_K` to 8 halves that exaggeration and is the intended next step. It is **not**
done here, because it moves every world coordinate: port positions, the navigation grid, lane
geometry, camera distances, the LOD pixel thresholds, ship scale (`SHIP_M_PER_UNIT`), the sea's
patch lattice and the streaming tile grid all derive from it. That is a change worth making on
its own, with the save format bumped and `sim_test` re-baselined, rather than folded into an
art import.

## 5. Using it

```
assets/terrain/depth_map_terrain.png     # drop in, restart, that is all
assets/terrain/normal_map_terrain.png
assets/terrain/terrain_textures.png
```

The log says what was found and how much of the world it changed:

```
terrain  authored terrain in use: depth 1376x768, normals yes, atlas yes (6x3 cells), 48 ms
terrain  authored heights folded into 992816 of 1464320 cells (68%) in 10 ms
```

A larger source map needs no code change — the sampler is resolution independent. Once the
images outgrow memory (they are 7.5 MB together today), the per-chunk slice streaming belongs
behind the same `terrainmap_height` / `terrainmap_normal` calls, so nothing in the renderer has
to learn about it.
