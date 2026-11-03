# Mare Liberum - session summary, **elm**, 2026-11-03 (0.22.25)

Worked per `skills/session_skill.md` (mare liberum: elm) into
`/mnt/c/projects/mare_sessions/mare_elm_03-11-2026_020336`. Nothing written into `./docs/dev`
or into the read-only assets.

**This session found the oar bug properly and fixed it. The rest of issue20 and of your
message is not done — it is written up as the next session's list, in order, below and in
`docs/TODO_STATUS.md`.**

## The oars — the real cause, after four rounds of my getting it wrong

One line of the asset log explains everything:

    loaded 'galley_oar' (ship_part): 5 LODs, LOD0 59924 tris, size 0.26x0.02x0.01, scale 0.1006

**The loader re-fits every part.** The raw GLB spans 2.583 units tip-to-tip on a −45° diagonal
— I measured that in 0.22.20 and built every later "fix" on it — but what is actually drawn is
a thin stick **0.26 units long on its own x**. So 0.22.21's `oscale = 0.38 * len / 2.583` drew
each sweep at **0.43 world units on an eleven-unit hull**: a sliver. That is why the bank
disappeared after I declared it fixed.

Worse: the numeric probe I added in that same round measured the tips at `0.913 × scale` — the
*raw* model's half-extent — so it reported a handsome 4.20-unit oar **that was never on
screen**. Every "verified" measurement since has been self-consistent and fictional. That is
how this survived four rounds of you reporting it and me confirming it was fine.

Restored to the mount the oars had **before I ever called them fixed** — the asset's own
loaded extent at the hull's import scale, the pre-claim rowlock placement and heading sign —
and rebuilt the probe on the loaded bounds so it measures what is drawn:

    oar check: hull 11.34, oar 2.34 (21% of hull), rail 1.25;
               -x end lateral 2.72 height -0.39;   +x end lateral 0.77 height 0.78 (deck 0.49)

A sweep a fifth of the hull's length, blade outboard of the rail and **in the water**, loom
inboard **over the deck**.

Also added `MARE_DV_MINDIST`, which relaxes the staged camera's six-unit floor — the other
half of why four rounds of screenshots settled nothing: they were all taken from too far away
to judge.

## Measured

- MSVC build green; **`ctest` 7/7**.
- The oar probe, on the loaded asset (above).

## Not done — the next session's list, in order

issue20's other contents and the rest of your message. I would rather hand this over honestly
than claim partial credit:

1. **The 22 asset LOD-extent warnings** in issue20 (`mast_carrack`, `mast_mizzen`, `bowsprit`,
   the yards: *"LOD n extent differs from LOD 0 — wrong node?"*). This is **the same class of
   fault the oars just turned out to be** — a part not being what the code believes it is — so
   it goes first. [206]
2. **Rivers**: missing again, want making better, want **3D/depth channels even where not
   traversable**, and more traversable reaches. [209]
3. **The map**: the Po valley and the lowlands read as sand and should not. [210]
4. **Smoke**: much more of it, untextured particle smoke, massive particle counts, better
   cannon smoke, all denser and slower to clear. [208]
5. **Ships fire only with a line of fire.** [214]
6. **Per-ship sail quality** shown by texture in battle. [213]
7. **Hinterland villages** as ordinary small settlements, **not selectable**. [211]
8. **Bridges too large** — reduce. [212]
9. **Lateen behaviour with the wind**, especially angled sails running downwind. [207]
10. **Build warnings**: C4005 `_CRT_SECURE_NO_WARNINGS`, cimgui C5287. [205]

Then the standing items: per-rope verlet geometry, deltas and lakes, authored village props,
and the river-packing command that needs write access to `assets/`.

## Percentage done

Over `docs/TODO_STATUS.md`: **213 done, 9 partly, 27 open, 4 dropped of 253 tracked — about
84%.** The open count rose because issue20 and your message added twelve rows.

## What went wrong with my process, and what I changed

Three sessions in a row I "verified" the oars with an instrument built on the same wrong
assumption as the code it was checking. A probe that shares a premise with the thing it
measures cannot catch that premise being false. The probe now reads the asset's loaded bounds
— the same numbers the renderer uses — and `MARE_DV_MINDIST` makes a real close-up possible,
so the next disagreement can be settled by looking.

## For the next session

- Session folders go under **`/mnt/c/projects/mare_sessions`** (a `~/.cache` path is WSL-local
  and MSVC cannot reach it).
- After any `rsync` into a mirror with a `build/`, **touch the sources**.
- `MARE_OAR_MIN_PX=0` forces the oars on at any range; `MARE_DV_MINDIST=1.5` lets the staged
  camera get close; `MARE_BATTLE_FREEZE=1` holds a battle still.
- Still needing write access to `assets/`:
  `python3 tools/rivers_pack.py assets/data-terrain-v2/mare-liberum-world-terrain-4x/mare-world-terrain 10.9e6`
