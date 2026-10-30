# Changelog

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
- **Battle lines** (`gfx/detailview.c`): the staged battle fights in formation. Each side
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
- **The staged battle and city views use the authored fleet too** (`gfx/detailview.c`): the
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
- **Terrain detail window** (`gfx/streamview.c`): the finest resident tiles compose into one
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
