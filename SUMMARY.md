# Mare Liberum - session summary, **elm**, 2026-11-02 (0.22.17)

Worked from `/mnt/c/projects/mare` (read only) into this folder,
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_002959`, per `skills/session_skill.md`.
Nothing was written into the project tree or into `docs/dev`.

## What I found first
The **0.22.16 round was lost**. The tree had been committed at `199ec3a` without it and the
changelog's own 0.22.16 entry went with it. That is precisely why issue14 opens by saying the
oars, the lateen and the riggings are *still* wrong, and why "Q and E in battle view don't work" -
the helm existed only as a changelog sentence, never as code. The worthwhile parts of that round
are carried back here alongside the new work.

## issue14, item by item
| the ask | what was wrong | what it is now |
|---|---|---|
| "Q AND E IN BATTLE VIEW DONT WORK" | never bound to anything | Q/E steer the player's line by hand, H returns it to the line; each ship answers at her own rate, on the battle's clock |
| "oars ... wide part in water, pivot in the ship part, slowly 30 deg" | the pitch sign was flipped (the bank rowed the air), the oar hung centred outboard, the sweep was 2-7 deg | blade down (measured: the -x end is twice the loom's section), the oar turns about its **rowlock on the rail**, ±15 deg at a stroke every 3-5 s |
| "random small villages ... connected with roads" | three per settled region, 3 units apart | six per settled region, two on a frontier, 2 units apart - each a real settlement, so roads and a mill follow |
| "still front lanteen and sail isn't fixed" | the triangle cut through the deck; the yards barely trimmed | the whole sail rides a seventh of a mast higher; the yards trim by the bisector rule |
| "more elasticity to rope for sails" | one stiff curve | half as much sag again, two breathing terms, and running rigging that comes taut under a drawing sail |
| "add texture similar to riggings" | the sails' own ropes were flat cross-quads | six-sided lathe-shaded prisms, like the cordage |
| "remove riggings on the stern that lead nowhere" | stays chosen by array index: every mast stayed to the stem and the **foremast ran one the length of the ship to the taffrail** | chosen by station - forward to the next mast ahead or the stem, aft to the next astern or the taffrail |

## The physics (asked for last, and where the real finds were)
Two bugs had made every battle bloodless, and neither was visible without measuring:

1. **No cannon shot had ever hit anything.** The guns fire in world coordinates; the tactical
   bodies live in the battle's local frame. The broadphase compared a gun at world (1000, -700)
   with a hull at local (-40, 20). Every ball flew past and out to the 4 km cull. The same
   mismatch handed `nphys_impulse` a lever arm a kilometre long on every shot.
2. **The pieces were laid up, from above the target.** +0.2 to +2 degrees of elevation from a
   muzzle already above the enemy's hull box.

Measured before: `battle damage: 0 hits` over a two-minute action. After: **440 hits, 31 masts
down, 6 of 30 sunk.**

On that foundation, the **damage model** (P1.3): `nphys_damage_t` with hull, rig, guns, flood,
sunk and masts_down. Where the ball strikes decides what it breaks - below the waterline she
leaks, on the gun deck she loses her battery, aloft her rig - scaled by the shot's speed and her
displacement. Masts go by the board at each threshold. Flooding grows with the holes and with
itself, so the end is quick. A wreck is dead in the water: no helm, no guns. A wounded rig costs
up to 65% of her speed.

## Measured
- MSVC build green; **`ctest` 7/7**, including a new `test_damage` in `physics_test`.
- `physics_test`: `rig 0.39, 1 mast(s) down, speed factor 0.75, dry` and `holed hull 1.00
  flooded to 1.00 and sank; 1 wreck(s)`.
- Shots in `shots/`: `damage_v.png` (a galley's full bank, blades in the water, looms over the
  rail), `rig_v.png`, `galley_lateen_v.png`, `oars_c.png`.
- My test caught my own harness error on the way: `nphys_advance` runs at most
  `NPHYS_MAX_STEPS` per call, so a 2-second call advances 0.067 s. Sim time has to be fed in
  frame-sized slices, as the game feeds it.

## Percentage done
Counted over `docs/todo_list.md` as it stands after this round:
**153 done, 8 partial, 30 open of 191 tracked items - about 82%.**
The renderer and the world are close to finished for this phase; what remains is weighted
towards simulation (damage consequences, the campaign loop) rather than looks.

## What I propose next, in order
1. **The butcher's bill goes home** (todo 153). The damage exists and nothing reads it. Write the
   tactical result back into the strategic ship - hull, crew, guns, sails, sunk - so a battle
   costs a fleet something that outlives the scene. This is the single highest-value item left:
   without it the whole damage model is scenery.
2. **Fire** (todo 152). A hit on a ship already burning or carrying powder starts a fire that
   spreads, smokes through the volumetric system that is already there, and ends in flooding or
   an explosion.
3. **Gun damage with teeth** (todo 149): the battery scalar exists; let it slow the broadside.
4. **A sinking that reads**: a wreck should settle by the head and go down over several seconds
   rather than simply stopping - the heave channel into the shader is already in place.
5. Then the standing renderer items: a fruit-tree set (117b), river carving and navigation on the
   authored data (118), a verlet chain for sheets and braces (121b).

## A standing risk worth naming
Two sessions share one tree and rounds have now been lost twice to commits taken mid-round. Work
delivered into `mare_sessions/` is safe from that, but it also does not reach the game until
someone merges it. The 0.22.16 loss cost a full round of work that issue14 then had to re-report.
