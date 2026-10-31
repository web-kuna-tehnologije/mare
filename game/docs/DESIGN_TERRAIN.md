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
(`terrain_rings.cc`), so the loose map refines relief and bathymetry near the camera but can never
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
zoom its waterline reads as 7 km steps. The clipmap rings (`gfx/terrain_rings.cc sv_fill_rows`)
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
down to a u16 tile set and the renderer read the cut. `world/terrain_chunks.h` now reads the
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

## 3e. The terrain-v2 chunks are the terrain (0.22)

Everything in 3d still holds about *what* a chunk is; what changed is that the exporter's
output under `assets/data_new` (format `mare-terrain-v2`, `MARE_INTEGRATION.md` beside it) is
now the **only** authored terrain, and all of it is read:

| map | read as | used for |
|-----|---------|----------|
| `height.r32` + `coverage.r8` | float32 metres, validity | the rings' height (bilinear, a hole hands over to the next dataset or the base), the geography bake |
| `biome.r8` + `biome_source.r8` | class id, provenance | the material masks (nearest, blended over the four surrounding samples) |
| `splat.png` | RGBA8 linear | grass / rock / sand / snow weights (bilinear, renormalised) |
| `normal.png` | RGB8 linear, east / up / south | terrain-scale lighting outside the near ring, the shore mask's slope |

`world/geochunks` holds **several datasets at once**, finest first: the overview and whatever
regional builds the exporter cut (`mare-terrain all --bbox ... --resolution-m 30`). A sampler
asks the finest resident dataset first; independent builds are not stitched (their grids need
not align, as the spec warns) - the rings cross-fade by distance anyway. `longitude_wrap` is
honoured for the overview; a regional dataset rejects points outside its bbox.

**On the GPU** (`gfx/chunkatlas`, `terrain/chunks.glsl`): resident chunks of the overview take
slots in three RGBA8 atlases (class: id / coverage / provenance; splat; normal) with a slot
table, one chunk uploaded per frame. Addressing is the spec's vertex-aligned rule, `(sample +
0.5) / 513` inside the slot, so the shared edge sample reads the same from either chunk and a
bilinear tap never reaches a neighbour's slot. No mipmaps (independent per-chunk mips seam).
The regional datasets feed the rings' heights but not yet the atlas - docs/TODO.md.

**The masks** (`terrain.frag`): every procedural rule keeps standing where no chunk answers;
where one does, the export's weights and classes are the masks - rock weight adds karst on
the flat, sand weight a beach near the water and an erg high and dry, the tree class puts the
woods, cropland becomes strip fields, built-up a roof-and-stone mottle, wetland reed and mud,
mangrove dark green, permanent water a still glossy surface, moss the tundra. The class is
read at the four samples around the fragment (`ch_classes`) and each class's weight is their
bilinear blend sharpened around a noise offset, so classes meet along ragged lines and not the
3 km grid. The export's normal has true-metre slopes; the drawn world is exaggerated 15.5x
(`geochunks_exaggeration`: 20 units per 9000 m against 16 units per 111 km), so its horizontal
components are scaled by that before normalising.

**The shore** (`gfx/terrain_rings.cc`): the 0.22 shore profile is driven by the data. For every
ring texel within reach of the coastline the chunk is sampled *one cell inland* (the shoreline
sample itself is as likely to be the ocean class): a steep export normal or a rock weight makes
a cliff, a sand weight and a gentle slope a beach whose strand is up to 1.6x wider, the wetland
and mangrove classes a marsh (flat on both sides, mud far out, no surf). The 0.17 cliff noise
keeps a voice only where the data is undecided. What the texel became is written to a second
ring texture, the **shore mask** (RGBA8: cliff, beach, marsh, exposure - exposure is the
harbour fade, so no port ever gets surf), which `terrain.frag` and `sea.frag` read through
`shore_at` with the detail rings' rects. Convex corners of the land-mask raster are rounded by
taking the minimum of the Euclidean coast-distance field and a 3x3 blur of it: the coast only
ever recedes (under half a cell), bays stay, harbours are protected, and the simulation never
sees it - this is what turns a two-cell island from a box into an islet.

**What the data cannot give yet.** The overview is heuristic land cover (`biome_source` 2
everywhere: no WorldCover was supplied to the exporter), so the classes follow elevation,
latitude and slope rather than observed cover; a build with `--landcover` tiles fixes that
without a code change. And it is still ~3 km per sample: a regional 30 m build is what
sharpens a coast.

## 3f. Rivers (0.22)

`world/rivers.h/.cc`. The base heightfield (after the geography bake) is flooded from the sea
in order of height (a priority flood: the cell a land cell is first reached from is its
downstream neighbour, and a depression fills to its spill point, so every cell drains). One
unit of rain per cell accumulated down those directions is the drainage area; a cell draining
more than `RIVER_MIN_CELLS` (45 land cells, ~2200 km2) is river; rivers are traced from the
heads (river cells with no river flowing in) down to the sea or into the river they join, the
biggest catchments first. The 7 km cell path is Chaikin-smoothed once and given a meander from
the world's fbm so it reads as a river rather than a staircase; the half-width grows with the
catchment (0.3 .. 1.7 km). Deterministic, no rng, ~450 ms at start-up for 11,500 rivers.

The rings consume them through a segment grid built per compose (`rivers_grid_build`, 1-unit
bins over the ring): every texel within 2.5 half-widths of a centreline is **carved** - a flat
bed and banks sloping back, 9 .. 27 m deep before the vertical exaggeration, fading into the
shore profile within a cell of the coast - and written to a **river texture** (water, flow
direction, width). A river narrower than the ring texel is drawn at 0.6 texels so it does not
fall between the samples. `terrain.frag` paints the water where that texture says so: a dark
green-blue surface reflecting the sky, ripples drawn along the flow and advected by the clock,
a glossy roughness; the map modes draw it as a blue line.

**Navigation.** Cells draining over `RIVER_NAV_CELLS` (420) are nav-grid water of class 2,
over `RIVER_DEEP_CELLS` (1400) class 1 (`navriver`, folded in by `nav_init`); A* enters a
river cell only while `nav_river_limit` allows its class, which `fleet.cc` sets from the
fleet's largest hull around each path search (sloops and fluyts 2, merchantmen and frigates 1,
galleons and liners 0) and the port-distance rows leave at 0, so trade distances are unchanged.

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
