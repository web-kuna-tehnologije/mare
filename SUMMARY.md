# Mare Liberum - session summary, **elm**, 2026-11-02 (0.22.18)

Worked from `/mnt/c/projects/mare` (read only) into this folder and
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_120500`, per `skills/session_skill.md`.
Nothing was written into the project tree or into `docs/dev`. `assets/issues/` holds nothing
newer than issue14, which 0.22.17 closed, so this round went straight to the new asset and
then to the open rows of `docs/todo_list.md`.

**The whole list, as checkboxes, is `docs/TODO_STATUS.md`** - that is the file to open to see
where things stand.

## The new asset: the Mare Liberum 4x world

`assets/data-terrain-v2/mare-liberum-world-terrain-4x.zip` is the same Earth as the 3 km
overview at **four times the samples per axis** - 2049 a chunk, ~700 m a sample, resampled from
GEBCO rather than upscaled - plus a river layer of 1.27M surveyed segments. 12.2 GB of it is
what the game reads; the preview PNGs and the palette texture are not read and were not
unpacked.

The loader was already written per-dataset, which is why this came down to four assumptions
rather than a rewrite:

| what assumed 513 | what it is now |
|---|---|
| the sample ceiling | `GC_MAX_SAMPLES` 2049; a finer manifest is no longer rejected |
| a flat 160 MB resident budget | a count of **chunks** - a 4x chunk is 68 MB resident where the overview's is 4.5, so 160 MB held two and thrashed at every border |
| four reads in flight | scaled by chunk size: right at 2.4 MB on disk, wrong at 30 |
| 4x4 GPU atlas slots of 513 | the grid comes from the dataset - 3x3 of 1025 (113 MB) at 4x, 4x4 of 513 (50 MB) at 3 km - with finer data point-sampled down **on the shared edges**, so the chunk joins stay seamless |

One thing found on the way, and worth the measurement: **the geography fold was reading 17 MB a
chunk to throw fifteen sixteenths of it away.** It writes a ~7 km base grid and takes nine taps
across each cell, so 700 m samples add nothing to that average - but it cost 2.6 s and about
half a gigabyte of peak, against the overview's 0.7 s. Reading every fourth row and column, by
seeking over the rest, brings it to 1.0 s with the fold's output identical to the sample.

**What it looks like**: `shots/terrain_3km.png` against `shots/terrain_4x.png`, the same seed,
camera and hour over the Dalmatian coast. The karst has rock faces, ridges and crags where the
3 km data gave smooth green hills; the islands have relief instead of reading as plateaux. That
closes todo row 93 ("the ranges read as terraces") and the roadmap's **"a genuinely finer DEM
for the home seas"**, which had been the highest-value terrain item since 0.20 - though by a
4x resample of the whole Earth rather than the EU-DEM/EMODnet cut that entry expected. It is
not street-level: GEBCO's own grid is ~460 m, so 700 m is the end of the source's detail.

## Then the todo list: rivers (row 118)

The package's rivers have been read since 0.22.14 but only **painted**. Carving and navigation
waited, and correctly: the only rivers before were a drainage network folded out of 7 km cells,
which puts a watercourse wherever the heightfield dips, and cutting ship-scale channels along
those opened navigable water across half of Europe. These are surveyed lines, so they can be
cut and sailed.

The part of row 118 that mattered was *"the half-width from the upstream length is a guess"*. It
is not a guess now - the thresholds were **read off the layer**. Over the 1.65M river points in
the game's box, `>= 15 Mm` of upstream length is 74% of them, `>= 60 Mm` is 15%, `>= 150 Mm` is
3.9%; river by river the Danube delta measures 338 Mm, the Rhine 110, the Elbe 94, the Po 59,
the Ebro 50. So **150 Mm takes a merchantman and 60 Mm a shallow draught**. My first pass used
15 Mm, on the strength of the per-river numbers alone, and opened three quarters of every
watercourse in Europe to shipping - not a river network, a canal system. The distribution is
what caught it.

- **2,148 cells of the navigation grid are river** where there were none, by class.
- **Roads bridge them.** `river_mask` was built only by the *generated* network, so since the
  authored rivers replaced it a road had been laid over the Po as if it were dry ground. 31,895
  cells carry a river for the roads to bridge now. (Found, not asked for.)
- Carving is on for authored rivers and off for the generated network; `MARE_RIVER_CARVE`
  overrides either way.
- Honest limit: `from_upstream_m` is length, not discharge, so it cannot tell a large river in
  a small basin from a middling reach of a huge one. The Seine and the Guadalquivir fall short
  of 60 Mm; the Thames and the Tiber are not in the layer at all, under the export's own
  filter. `width_m` is null throughout, so there is nothing to correct it with (todo 159).

## Measured

- MSVC build green; **`ctest` 7/7**.
- The 3 km path is untouched, which is the thing worth checking: same budget (160 MB), same
  atlas (4x4 of 513), same fold (5,767,931 cells, 148,995 new land), and with
  `MARE_RIVER_DATA=0` the generated network still comes up painted, 0 river cells open to ships.
- The 4x path: 27x14 chunks of 2049 samples at 699 m; budget raised to 408 MB; atlas 3x3 of
  1025 texels; fold 5,768,831 cells / 148,069 new land in 976 ms.
- Shots: `terrain_3km.png`, `terrain_4x.png`, `rivers_po2.png` (the Po's lower reach as a
  navigable channel with its own beaches and surf).

## Percentage done

Over `docs/TODO_STATUS.md` as it now stands: **162 done, 9 partial, 29 open and 2 dropped of
202 tracked - about 82%.** The number has not moved much, because this round added rows as well
as closing them; what has moved is the *balance*. The renderer and the world are finished for
this phase. What is left is simulation and authored art.

## What I propose next, in order

1. **The butcher's bill goes home** (row 153). Unchanged from last round, and now the only
   thing of its size left: the damage model is complete and **nothing reads it**. Write the
   tactical result back into the strategic ship - hull, crew, guns, sails, sunk - so a battle
   costs a fleet something that outlives the scene.
2. **Fire** (row 152): a hit on a ship already burning or carrying powder starts a fire that
   spreads, smokes through the volumetric system already in place, and ends in flooding or an
   explosion.
3. **Bridge props** (TODO 10b, row 161) - newly unblocked. The roads now know where they cross
   a real river; a stone arch and a timber beam are an afternoon with a visible return.
4. **Gun damage with teeth** (row 149): the battery scalar exists; let it slow the broadside.
5. **A fruit-tree set** (row 117b) and the authored village props (7c): the hinterland reads
   well now, and these are what is thin in it.

## Two things the next session should know

- **Where the data is.** `assets/` is read-only to a session, so the 4x build is unpacked to
  `mare_sessions/mare_elm_02-11-2026_120500/terrain_v2_extract/mare-world-terrain` (12 GB) with
  its `rivers.bin` already packed beside each chunk. To make it the game's default, move that
  folder to **`assets/data-terrain-v2/mare-world-terrain`** - the loader probes that name first,
  ahead of `data_new`. Until then set `MARE_GEOCHUNK_DIR` to it. If both are present the 4x
  build answers and the overview stands behind it, which is the intended arrangement.
- **The standing risk, again.** Two sessions share one tree and rounds have been lost twice to
  commits taken mid-round. A `mare_oak` session started at 12:06 today, while this one was
  running.
