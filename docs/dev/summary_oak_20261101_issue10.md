# Mare Liberum - summary, worktree oak, 2026-11-01 (0.22.13: issue10)

`assets/issues/issue10`: `issue.txt` and the menu video ("too much blur"). Decoded with Blender
(`tools/vid_frames.py`): the title menu over a backdrop blurred to a grey smear.

## The store first
The asset store (`/mnt/c/projects/mare/assets`, not in git, 24 GB) held the same hulls three times,
two older imports of the authored tree beside the live one, and a 2.8 GB normal map nobody read.
Sorted to 15 GB, documented in `assets/README.md`: `generated/` (the game's tree), `data_new/` (the
terrain package), `ships/` (scans, cannons, effect sheets, sails), `sea/`, `terrain/`, `buildings/`,
`reference/`, `issues/`. Two things to know:
- The other worktree's session was reorganising the same folder at the same time (it renamed
  `elm` to `generated` under me). The game probes both names.
- `FOAM/` and `foam/` were one folder on the case-insensitive disk; my merge removed it. The six
  sheets were still in the Downloads folder and are back as `ships/foam/{photos,masks}`.

## issue10, item by item
- **Elevation 12**: `GC_HEIGHT_SCALE` 5 -> 12. The ranges show the 2.8 km grid as terraces now.
- **Cannons and particles**: seven guns imported (`gun_*`), stood at the ports of every class;
  the smoke, fire and foam sheets cut into RGBA atlases and sampled by the puff shader - cover,
  luminance and a cover-gradient normal, so a puff is lit as billows. RGBA decode in the loader.
- **New terrain data**: unpacked, verified, read (`terrain-v2 dataset: 27x14 chunks ...` in the
  log). Rivers are in it (`waterways.geojsonl`) - not wired yet, top of the list.
- **Shock**: the sea ring runs 1.6 L, lifts 0.085, whitens at half; the tonemap's radial blur halved.
- **Canopy to trees only**: the painted crowns are gone; woods are instanced trees.
- **Fields back without props**: cropland floor 0.55 over the lowland, wheat off.
- **Orchards and vineyards as props**: wider rings and patches, lower scatter thresholds.
- **Lateen, masts, rigging**: rope tops per station from the heel (they ended a fifth of a hull
  height over every truck); the lateen's peak at the masthead; the lantina scaled by its true
  length to the sail's head and turned to the design rake; square layers under the truck.
- **Waves like wake.png in 3D**: the arms' geometry runs to 4.4 L (was cut at 1.55), half as high
  again; the wake map spreads (3.5 texels/s) and fades (20 s) with the white going first.
- **Random villages**: hashed hamlets on the workable lowland and three farm villages per
  settled region.
- **The blur**: half-size, one pass pair, mixed at 0.7.

## Found on the way
- `ship_classes.json` had never been applied: `!gjson_parse(...)` on a function that returns 0 on
  success. Fixed - the class data is live for the first time.
- HEAD did not link: the WIP merge had re-added `world/geochunks.*` and `gfx/chunkatlas.*` next to
  their renamed successors. Removed; the two headless suites that include `world.cc` get the height
  ratio as a stub. 7/7 suites pass.
- The foam tiles were missing from this tree (the sea fell back to constants); cut again.

## Measured
- MSVC build green; `ctest` 7/7 (physics, diag, stream, asset, jobs, sim, entity).
- `docs/screenshots/issue10_oak_*.png`: the menu over a soft backdrop, the battle with textured
  smoke, a hull with the rig at the mastheads, the fusta's lateens, the hinterland as trees over
  fields, the Alps at 12, the Po plain.

## Open after this round (docs/todo_list.md rows 77-92)
The rivers from the package; the terraced look of the ranges at 12 (a finer DEM); the fleet icon
drawn at the hinterland zoom; guns barely read at the ports from outside the hull - a muzzle
through each painted port wants the hull's real port positions; the volumetric smoke is still
billboards, a ray-marched slab is the next step; vineyards/orchards want authored trees.
