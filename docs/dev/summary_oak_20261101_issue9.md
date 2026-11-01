# Mare Liberum - summary, worktree oak, 2026-11-01 (0.22.12: issue9)

`assets/issues/issue9`: `issue.txt`, `wake.png`, the crest-foam screenshot, the rigging video.

## The recoil, as asked
"Move the ship by five centimetres, not rotate her five times; the rotation is about the x
axis, a couple of percent of a degree by the number of shots and the guns' strength." The
sideways set is now the ball's real momentum (centimetres on a 1000-tonne hull) applied at the
centre, so there is no yaw; each gun kicks a quarter-degree of **heel** away from the firing
side onto a damped roll spring (`nphys_kick_roll`). To make that visible the body's roll and
pitch now go to the shader on every part of the ship (`inst_t.roll_b/pitch_b`; `i_anchor` is a
vec4) - which also means the 0.22.11 buoyancy is drawn, not just hinted at. The canned motion
is cut to a third.

## The world
- Relief cut by 70% (height scale 16 -> 5, ring noise to match). Every threshold tuned against
  the old scale - snow line, shore fade, lowland gates, tree line - is now relative to it
  through `geochunks_height_scale_rel()` / `u_hscale`, so the snow stays on the summits.
- Fields only round settlements and along the roads between them; wild clumped woodland over
  the open country between; villages a third wider and fuller; fences off; wheat smaller and
  denser; cattle herds and five times the flocks. The far tree card has a flat crown quad so
  a wood reads as crowns from above.
- The inland settlements, their roads to their port, mills and pitheads already existed
  (0.22.3-0.22.6); they read now because the fields gather round them.

## The sea
- The wake is the V: arms rooted at the stem, twice the height in half the width, inboard
  troughs, breaking white and denser in the fragment stage and the wake map (wake.png). The
  bow's radial ring is smaller.
- Foam on the staged crests like the map's; the foam floor raised under a dark sky.

## Smoke, clouds, fog
- Gunsmoke greyer (0.60 / 0.46 / 0.42), opaque at birth, 1.6x the life, half the wind, oval
  billboards. Sky clouds a third the drift, greyer, torn edges.
- The mesh fog mix was capped at 85%, so every rope's outline survived the haze; it goes to
  100%. Hulls behind a broadside are hidden by the smoke rather than shown through it.

## The rig
- Masts 0.64 (was 0.52); the lantina a tenth longer than the design line; sheets from every
  clew to the rail or the yard below ("rope connecting sails to boat").
- Map zoom to 1.0 x scale.

## Measured
- MSVC build green; all six headless suites pass (physics 1-9 unchanged in result).
- Screenshots `docs/screenshots/issue9_oak_*.png`: the battle with grey opaque smoke and V
  wakes, the Alps at the new scale, farmland round a town with no fences, the woodland.

## Process
- Mirror `C:\projects\mare_oak_20261101_141803`, untouched by others this round. The issue
  video decoded with Blender (`tools-bat\vid_frames.py`), not the Playwright ffmpeg.

## Next, in order
The damage model; flooding, fire and sinking; heave into the shader; true sternway; the
settlement generator proper (street layouts, authored houses, landmarks).
