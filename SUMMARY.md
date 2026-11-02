# Mare Liberum - session summary, **elm**, 2026-11-02 (0.22.20)

Worked from `/mnt/c/projects/mare` (read only) into this folder and
`/mnt/c/projects/mare_sessions/mare_elm_02-11-2026_160813`, per `skills/session_skill.md`.
Nothing written into `docs/dev`. **issue16** (15:43 today) resolved first - issue15 was closed
in 0.22.19 - then the carried rows. `docs/TODO_STATUS.md` is the whole list as checkboxes.

## issue16, item by item

| the ask | what it is now |
|---|---|
| "make mor realistic fire" (screenshot: two yellow discs) | the 0.22.19 fire borrowed the muzzle-flash sprite - a hot *disc*. Fire is tongues: a new flame particle draws the fire sheet 2:1 tall, never spinning, each tongue born at the deck, climbing, dead in under a second; white root to red tip, self-lit; embers downwind. The flicker is many short tongues, not one long sprite |
| "oars ... wide part needs to be in water and thin part in ship" (the **third** report) | the asset was finally **measured instead of assumed**: the oar GLB is modelled on a **-45° diagonal**, blade at **+x** (cross-section 0.227 vs the loom's 0.090). Every prior fix believed the blade was -x, so the mount put the real blade *inboard* - wide part in the hull, thin loom in the air, exactly what every screenshot showed. Side sign flipped, pivot moved to the loom's true end. *Honest caveat:* the close-up verification shot kept missing the moving ship, so this rests on the measurement, not a picture |
| "retract sails if upwind" | a **Take in sails** button: the player's side furls; oared ships pull at their oars, sailing ships heave to |
| "sunk or get strayed on beach" | **stranding**: a quarter of her draft out of her depth she grinds on the bottom (hull damage per second), and after four seconds she is stuck fast - no helm, no way, "(aground)" on her muster card. Sinking has existed since 0.22.17 |
| "damage on collision" | the contact's closing speed above a walking pace costs timber between wind and water, split by inverse mass - the boat takes the worst of touching a carrack |
| "mesh damage removal ... actually removing parts of mesh" | a mast that goes by the board **is not drawn** - with its yards, canvas and lantina, stations falling bow-aft to the physics' count. The ships are assembled from parts per frame, so removal is removal. The standing rigging still shows its runs: one per-class mesh, same blocker as the rope (121b) |
| "ship icons and list on top enemy on bottom player, stats (hull, water, crew, sails, ammo)" | the **muster list**: enemy top, player bottom, per ship four bars - hull, sails, guns, water - a flame when burning, "+" lost, "(aground)" stranded. **Crew and ammo are not shown because they are not simulated**; a bar that never moves is a lie (todo 178 makes them real first) |
| "villages to small ... same size as cities" | built disc to a small town's footprint (0.34 + 0.11·√pop), houses a third bigger, drawn to 70 units out (the issue's screenshot is from ~55, where villages used to show bare paint) |
| issue1.png "city ... looks just bad; you have assets for buildings" | a size-3 city stands **~90 houses (was ~40) on ring streets** - doors to the lane, lanes between the rings - inside a wall with a **tower at every other joint**. A better procedural town; authored landmarks for Venice/Dubrovnik/Constantinople stay on the roadmap |
| "connect cities with roads ... depending where trade is flowing" | **not done** - roads already run settlement→hub port; weighting by trade flow needs the economy's flows plumbed into the road builder. Left open (181) rather than faked with cosmetic lines |
| "add rope for then next round if unable this one" | taken at its word: carried (121b). Its blocker - a per-ship rope/rig buffer - now also unblocks cutting a felled mast's rigging, which makes it the clear next renderer task |

## Found on the way

**The mirror build was linking stale objects.** `rsync -a` preserves source mtimes, so an edit
whose mtime landed older than the existing `.obj` was never recompiled - the linker failed (or
worse, succeeded) against last round's code. The sync step now touches sources after copying.
Last round's lesson was "grep the linker too"; this is its twin: make sure the linker is even
looking at your code.

## Measured

- MSVC build green; **`ctest` 7/7**.
- A 55-second staged battle logged six fires; `shots/fire_new.png` shows a flame column with
  smoke where 0.22.19 showed two discs. `shots/venice_city2.png`: the towered wall and the
  ring-street houses.
- The oar GLB numbers the fix rests on: long axis −45.0°, blade cross-section 0.227 at +x
  against 0.090 at −x.

## Percentage done

Over `docs/TODO_STATUS.md`: **184 done, 9 partial, 28 open, 2 dropped of 223 tracked — about
83%.** The battle is now a commanded, visible, costly thing; what is left clusters around one
renderer buffer (121b) and the campaign loop (yard, crew, trade-flow roads).

## What I propose next, in order

1. **The per-ship rope/rig buffer** (121b) — one piece of work, two asks unblocked: the verlet
   sheets and braces, and a felled mast's rigging going with its mast. Promised "next round" in
   the issue's own words.
2. **Crew and ammunition** (178): the muster list has two honest gaps where they belong, and
   the damage model has nowhere to send casualties.
3. **A yard that repairs** (170): damage persists and nothing takes it off, so a campaign only
   grinds down.
4. **Roads by trade flow** (181), **bridge props** (161), **a fruit-tree set** (117b).

## For the next session

- The 4x terrain build still lives at
  `mare_sessions/mare_elm_02-11-2026_120500/terrain_v2_extract/mare-world-terrain`; move it to
  `assets/data-terrain-v2/mare-world-terrain` or point `MARE_GEOCHUNK_DIR` at it.
- After any `rsync` into a mirror that has a `build/`, **touch the sources** (or the build will
  link whatever was there before).
- `SAVE_VERSION` is 6 since 0.22.19; old saves are rejected cleanly.
