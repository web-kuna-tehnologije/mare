# Mare Liberum - session summary, **elm**, 2026-11-03 (0.22.24)

Worked per `skills/session_skill.md` (mare liberum: elm) into
`/mnt/c/projects/mare_sessions/mare_elm_03-11-2026_010247`. Nothing written into `./docs/dev`
or into the read-only assets. `docs/TODO_STATUS.md` holds the DONE list and the LEFT list.

## The oars, put back

You were right: they were fine, and my 0.22.23 rowing animation is what broke them. The fault
was not the drive-and-recovery cycle in itself - it was **what the cycle touched**. It
feathered the blade by moving `fx2`, and `fx2` is not only the blade's pitch: the rowlock
mount solves the oar's *placement* from it. So as the stroke ran, the whole bank shifted and
lifted off the geometry that had been measured and verified in 0.22.21.

Reverted to the 0.22.21 sine exactly, and checked against the numbers rather than by eye - the
one-shot oar probe prints the same figures it printed then:

| | 0.22.21 (good) | now |
|---|---|---|
| blade tip, outboard of the 1.25 rail | 4.19 | **4.20** |
| blade tip height (water at 0) | −0.33 | **−0.33** |
| loom tip, lateral | +0.19 | **+0.19** |
| loom tip height (deck at 0.49) | 0.79 | **0.79** |

A rowing cycle can come back, but it has to drive the *swing* only and leave the mount's pitch
alone. That constraint is now written on the code so the next attempt does not repeat it.

## Then the todo list

- **Bridges where roads cross rivers** (161). The crossings have been recorded since 0.22.2,
  and filled for the *authored* rivers since 0.22.20 - which is what finally made them worth
  drawing. A stone arch stands at each now: two abutments, an eight-voussoir segmental arch
  springing from them, a deck and a low parapet either side, laid along the road, scaled to the
  span and set at the higher bank so it bridges the cut rather than floating over it.
  **16 standing in one ring, of 218 known crossings.**
- **Roads graded by where trade flows** (181). The builder already tested
  `traded_today / throughput` — and `sim/region.cc` **zeroes `traded_today` every day**, so
  whenever roads were rebuilt it read nothing and *every road in the world came out a track*
  ("195 roads (195 tracks, 0 roads, 0 highways)"). The persistent measure is
  `region.development`, which region.cc drives from the trade that actually moved. A link is
  now graded by the **smaller of the two towns it joins** - a road is only as busy as its lesser
  end - plus the development behind them. **183 tracks, 4 roads, 8 highways** on a fresh map.
- **A fruit-tree set** (117b). Up close an orchard was planted with the forest **oak and maple
  scaled down**, which is what the row complained of. Three pruned varieties now - an apple,
  low and wide on a short bole; a cherry, upright with a narrower head; a plum, bushy and
  nearly to the ground - each with a second off-centre lobe so the head reads as boughs rather
  than a ball, one chosen per grove cell. The olive groves keep their authored olive.

## Measured

- MSVC build green; **`ctest` 7/7**, run twice.
- The oar probe: identical to 0.22.21, digit for digit (table above).
- 16 bridges in a ring; roads 183 / 4 / 8 (was 195 / 0 / 0).
- 400 simulated days: trade and war healthy.
- Shots: `oar_fixed_crop.png`, `bridge.png`, `orchard.png`.

## Percentage done

Over `docs/TODO_STATUS.md`: **211 done, 9 partly, 17 open, 4 dropped of 241 tracked — about
88%.**

## What I propose next, in order

1. **Per-rope verlet geometry** — the rig off its single per-class mesh onto per-ship dynamic
   geometry. It finishes 121b (today one chain serves a whole ship) and buys a felled mast
   taking its own shrouds and stays with it.
2. **Deltas and lakes** from the authored river layer.
3. **The hinterland's authored props** (7c): the village houses, the mill, the well as assets.
4. **182b** — the one river-packing command in your terrain extraction.

## For the next session

- **The session folder must be under `/mnt/c/projects/mare_sessions`.** A path under `~/.cache`
  is WSL-local and `cmd.exe` cannot reach it ("UNC paths are not supported"), so MSVC cannot
  build there. I lost time to this and left two stray mirrors behind before spotting it (both
  removed).
- Still needing write access to `assets/`:
  `python3 tools/rivers_pack.py assets/data-terrain-v2/mare-liberum-world-terrain-4x/mare-world-terrain 10.9e6`
- After any `rsync` into a mirror with a `build/`, **touch the sources**. If the linker says it
  cannot open an exe, a previous run is still alive: `taskkill /F /IM mare.exe`.
- `MARE_OAR_MIN_PX=0` forces the oars on at any range; `MARE_BATTLE_FREEZE=1` holds a staged
  battle still; the oar probe logs its numbers at `--log-level=info`.
