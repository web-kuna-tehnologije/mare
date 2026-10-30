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

The maps and the tile set resolve **per worktree first**: `<asset root>/terrain[/tiles]`
(which `assets/elm` carries) before the shared `assets/terrain` — two sessions cutting tiles
into the shared folder have already overwritten each other's index once. The tile meta now
records the true sample dimensions; the padded-to-512 dims it used to record squeezed the east
and south edges by the pad (~2 %).

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

### 2b. The geography bake and the island data (0.16)

The streamed tiles (section 5) are folded into the base world once at startup by
`terrainmap_bake_geography()` → `terraintiles_bake()`: one tile row resident at a time, authored
relief where both agree on land, authored bathymetry where both agree on sea, harbour basins
pinned, the carved straits re-forced open, the coast-distance field rebuilt before `nav_init`.
123 ms against the 16x24 set. `MARE_TERRAIN_GEO=0` reverts to the purely procedural heights.

The *coastline* still never comes from the image, and 0.16 is why that rule survived contact
with the user's wish for "every island of the Adriatic": the supplied 32768 map turned out to
be a loose AI Earth — recognisable continents, regionally wrong coasts, its Adriatic half
filled with land — so taking its mask would have paved over exactly the island seas that
matter (the experiment is kept behind `MARE_TERRAIN_ISLANDS=1`, with port protection and a
vote filter). The islands are instead **authored vector data** in `data/geo_coast.h`: the
Dalmatian chain, the Ionians, the Aegean and the Cyclades, Menorca, Ibiza, Malta — each
polygon at least two cells in both axes so its navigation cell goes majority-land and the A*
routes around it (`sim_test` fails on fleets sailing over land below that size), with a water
row kept between neighbouring chains so the channels stay open. `raise_small_islands()` in
world.c then lifts any land component of ≤80 cells to at least 0.55 world units: the coast
ramp alone tops a two-cell island out *under* the sea shader's land threshold, which is why
the Azores spent several versions as drowned platforms with a surf ring.

The clipmap rings clamp the authored height delta to the base's side of the waterline
(`streamview.c`), so the loose map refines relief and bathymetry near the camera but can never
raise a landform through the open sea or open a hole in a coast the ports were placed against.

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

## 3b. The relief model and the textured ground (0.17)

`compute_heightmap` (world.c, on the job pool) is no longer a coast ramp plus fbm. Land rises
over a coastal plain, then a **domain-warped ridged field** - crests in lines, steep flanks -
is lifted by the broad mountain mask (`RELIEF_MOUNTAIN`) and, more gently, everywhere inland
(`RELIEF_HILLS`); where the cliff field is high the coast skips the beach and rises straight
out of the sea; below the waterline a shelf about a unit deep (`SHELF_CELLS`, `SHELF_DEPTH`)
hangs for half a degree before the floor drops to the deep. Small islands are lifted clear of
the water afterwards. The streamed chunks (`stream.c tile_generate`) carry the same ridge shape
one and two octaves finer, growing with the ground's height; the authored tiles add relief
only where they are *higher* than the procedural ground (their land band is compressed and
smooth - taken as-is it flattened every coast into a plain) and the clipmap rings may only
raise land and deepen sea.

The terrain shader samples the 6x3 material atlas (`atlas_cell`: per-cell wrap by hand, mip
derivatives from the unwrapped coordinate so the seam does not show): grass/turf/meadow on the
lowlands, Mediterranean scrub between 33 and 46 degrees, soil and gravel on the dry heights,
**triplanar rock wherever the slope passes ~0.2**, sand on the beach, dune and cracked clay in
the deserts, moss in the tundra, snow above 6.5; two repeat scales blended by distance, faded
out towards the strategic view so the map keeps its clean biome colours. The sea floor darkens
from the shoreline down, and warm seas (south of ~45 degrees) run turquoise over the shelf.

The reference for all of this is the aerial art under `assets/example/`: rugged ridged
mountains with vegetation and exposed rock on the crests, cliff coasts, turquoise shelves
dropping to deep blue. What the relief model cannot supply on its own is the *real* geography
at that fidelity - see "what would make it real" below.

### 3c. The shoreline band (0.19)

The base heightfield is the coastline polygons rasterized at 16 cells per degree, so at battle
zoom its waterline reads as 7 km steps. The clipmap rings (`gfx/streamview.cc sv_fill_rows`)
now treat a band around that waterline - `|base| < 0.62` on land (full under 0.22), `< 0.78`
on the sea side (full under 0.32): the first land cell and the first two sea cells -
differently from the rest of the ring: where an authored tile is resident and *decisive* (over
0.05 or under -0.05) its side of the waterline wins - a base sea cell GEBCO calls land rises
to the authored height (a mountain foot continues where the raster cut it), a base land cell
GEBCO calls sea goes under - with a base sea cell never shallower than -0.30 (the sea's shallow
shading was tuned for that floor; GEBCO's real 2-30 m shallows would turn every bay white) and
a base land cell never under 0.20 (a beach above the sea's land threshold). Elsewhere the
raster corners are rounded with a half-cell box blur, and a fbm whose shortest wavelength is
eight ring texels wiggles the contour so it reads as a shore rather than a ruler. Inside the
coastal *fringe* (1.5-5 cells of coast distance, interpolated so it has no cell steps) the
authored relief replaces the procedural one on base land instead of only raising it, so the
0.17 cliff field no longer walls a shore GEBCO says is gentle. `--log-level=debug` logs every
ring compose with the delta's range and how many texels the shore work lowered. The band fades to nothing within `SV_PORT_FADE`
(3.2 units) of every harbour, so no port town ends up in the sea. The sea's vertex stages and
the surf band sample the same rings (`sea/vs_common.glsl height_at`, `sea.frag depth_at`), so
the water stops where the detailed ground meets it. The simulation never reads the rings: this
is presentation, the navigation grid and the ports are untouched. Small islands lifted by
`raise_small_islands` sit at 0.55 and above and keep their authored shape.

## 3d. Reading the export as chunks, and what resolution is actually available (0.20)

`assets/data/how_to.txt` describes the delivered data precisely: 27 x 14 chunks under
`chunks/{y}/{x}`, each `height.r32` 513 x 513 little-endian float32 **metres** (negative is
sea floor), each `coverage.r8` a 513 x 513 validity mask that is explicitly *not* a land/sea
mask, each `tile.json` the chunk's WGS84 bounds, and - twice over - the instruction to use the
float32 rather than the 16-bit preview. It also says plainly that the export is ~3 km per
sample, "za globalni pregled, ne za detaljan teren".

Until 0.20 the game read none of that directly: `tools/terrain_from_gebco.py` cut the export
down to a u16 tile set and the renderer read the cut. `world/geochunks.h` now reads the
chunks as published - exact metres, coverage honoured, bounds taken from the manifest and
checked against a shipped `tile.json`, neighbouring chunks sharing their edge samples so the
joins stay the 0 m the export verified. The clipmap rings prefer, in order: the fine regional
set, these chunks, then the u16 cut; all three decode through the same land/sea calibration,
so which one answers changes fidelity and nothing else.

**What that buys, honestly.** The u16 cut was faithful - its quantisation step is about
0.0006 world units - so the visible difference is nil. What changes is that the game reads
the delivered format, handles missing samples as unknown rather than as data, and no longer
depends on a resampling step having been run correctly.

**And what no pipeline change can buy.** The near ground is smooth at battle zoom because the
data is smooth, not because the renderer throws detail away. At `WORLD_K` 16 the near clipmap
ring's texel is 0.157 world units, about 1.1 km; the finest authored set samples every 620 m.
The ring is therefore within a factor of 1.8 of the data - essentially Nyquist-matched - and
the detail left underneath it measures 0.0016 world units rms, 0.031 at worst, against local
relief of 0.7 to 4.1. `gfx/heightatlas.cc` measures exactly this at start-up and disables
itself on the strength of it. Getting a visibly sharper coast needs a genuinely finer DEM
(EU-DEM, EMODnet bathymetry or similar for the Adriatic); the moment one is cut into the fine
set, the probe will find the detail and the atlas will switch itself on.

## 4. Scale

The world is 16 world units per degree (`WORLD_K` = 4 x `WORLD_SCALE`, 0.17), which makes the
playable extent 2816 x 2080 units for ground that rises ~10-15 units - a vertical exaggeration
of two to three, which keeps a range readable from the regional view without turning it into a
ridge of sticks. The base grid is 16 cells per degree (2816 x 2080); navigation (704 x 520)
and territory (352 x 260) keep their old cell sizes.

### What would make it real

A real DEM and a real coastline. The 32768-square map in `assets/terrain` is a loose AI Earth,
so it supplies relief only. To replace it: a 16-bit greyscale equirectangular height/bathymetry
export (GEBCO 2024 or ETOPO 2022, 15 arc-seconds, lon -180..180 / lat 90..-90 or the playable
window) dropped into `assets/<tree>/terrain/world-heightmap.png`, cut with
`tools/terrain_tiles.py`, calibrated with `tools/terrain_calibrate.py` (`TT_SEA`,
`TT_HEIGHT_SCALE`, `TT_DEPTH_SCALE` in terraintiles.c); and a land-mask raster (Natural Earth
10 m) from which `data/geo_coast.h` can be regenerated at the full grid resolution.

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
