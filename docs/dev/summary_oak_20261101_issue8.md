# Mare Liberum - summary, worktree oak, 2026-11-01 (0.22.11: issue8, recoil, five todo rows, two physics items, the hinterland)

## issue8 (assets/issues/issue8), file by file

- **Oars on the wrong side.** The bank's heading had the wrong sign, so each oar's blade
  pointed inboard and the whole bank lay across the deck. Flipped.
- **Missing crest and foam on top of the waves.** Under a dark sky the foam colour went
  darker than the water and vanished. A floor keeps it lighter whatever the sky does, and the
  whitecaps come on from a smaller swell.
- **A little bigger wave amplitude, less foam, more dense and concentrated.** Staged swell
  0.45 -> 0.60 of the map's; the wake stamp is narrower and its breakup takes the fringe away
  instead of spotting the band; the trail is thresholded so only solid churn whitens.
- **Add fading out and cone spreading and disappearing; real generated waves from stern and
  bow.** The wake map is a ping-pong now: each frame it is blurred by a fraction of a texel
  and decayed, so a trail widens into a cone as it ages and thins to nothing. A transverse
  stern wave and a short train down the lane join the bow wave.
- **The Alps look too pointy.** The ring ridge's crest power 2 -> 1.35 at two thirds the
  amplitude; the peak boost 0.28 -> 0.22 with a soft knee over 2600 m. Broad massifs now.
- **Water looks like an oil spill; flickering (video).** Three causes: the foam tile's dark
  half darkening the body (a third of that now), a roughness floor of mirror glass (0.26 ->
  0.36 close in), and a pow-190 glitter on a jittery normal that flipped per pixel per frame
  (half the ripple, a pow 40-110 lobe tied to the specular AA). SSR damped and off within a
  few units of the eye. The video's last seconds were the camera *inside* a hull on the map:
  the near-camera dissolve is back on the map as a tight band.
- **Add the ship battle as the start menu, blurred.** A throwaway game is made when the menu
  is up, a staged battle is entered behind it, the trailer camera loops over it, and the
  tonemap mixes in the frame blurred four times at quarter size. `MARE_NO_PREVIEW=1` for the
  plain menu.

## Recoil (the request) and the two physics items
- `nphys_impulse`: each gun's impulse at the gun's own position as it fires - the hull is set
  to leeward and, the guns being off her centre, turned a little as the ripple walks along the
  side. The ball's momentum is scaled x6 so it reads.
- `nphys_set_surface` / `nphys_attitude`: five-point buoyancy per hull (three keel discs, two
  bilges) against the sea's own wave sum; the renderer scales the hull's roll by it.
- `nphys_fire` / `nphys_poll_events`: real round shot as swept spheres under gravity against
  the compound hulls; hits and splashes are events the staged scene turns into spray, foam
  rings, splinters and dust. The battle HUD counts shot in the air and hits.

## Five todo rows
15 (water impacts: shot splashes + the stern wave), 17 (`"sails"` per class in
`ship_classes.json`), 19 (bow spray in a fresh breeze), 21 (the dark lateens were the fusta's
used suit, now declared; the foam discs are the wake), 26 (vines, wheat, fences, woods as 3D
props within the near ring).

## The hinterland
- Woods are a canopy: every forest texel a tree, up to three more on full cover.
- Parcels fenced on all four edges, on the parcel grid; the fence is weathered pale and half
  the height so it reads as a line, not a net.
- Vines in contour rows at a fixed spacing; grain parcels full of standing wheat.
- No prop casts a shadow - the scatter was already main-pass only.

## Measured
- MSVC build green. Headless suites: diag, jobs, sim, asset, entity, physics - see the commit
  for the final run. `physics_test` now also proves buoyancy (pitch on a fore-aft wave, roll
  on a beam wave, nothing on a flat sea), ballistics (one hit on the target hull, one splash
  in open water, nothing left flying) and recoil (velocity to leeward and yaw from an
  off-centre impulse). A first run showed four failures: one real (the splash test ran 5 s on
  a 6 s flight) and three from the ship-ship test breaking on the *global* failure counter;
  a standalone driver showed the hulls meeting at 15.7 units with a contact. Both fixed.
- Screenshots in `docs/screenshots/issue8_oak_*.png`: the battle with recoil, shot and the
  diffusing wake; the widened Alps; the fenced farmland; the blurred menu.

## Process
- **Raided again, moved again.** At 14:10 another session created `mare_oak_20261101_141058`
  and *moved* my `assets\elm` and `assets\data_chunks_terrain` out of my folder into it (my
  `assets\` was left empty; the next three screenshot runs logged "no asset manifest", "no
  terrain dataset", and drew grey stand-in hulls on a bare world). Per the rule, nothing of
  theirs was touched: I made a new folder (the path is in the worktree's `.mirror`) and
  *copied* the two asset trees back in from theirs - a read, not a change. The earlier
  battle, Alps and forest screenshots, and the test runs, predate the raid and stand.
- The issue video would not decode in the Playwright ffmpeg (an odd MP4 flavour); Blender's
  did (`tools-bat\vid_frames.py`).

## Next, in order (also in docs/todo_list.md "Order of work")
1. The damage model: zones with hit points, masts that fall, canvas that tears, the result
   written back to the strategic ship.
2. Flooding, fire and sinking, with their VFX through the particle system.
3. The body's attitude into the mesh shader (a vec4 anchor), replacing the canned roll.
4. True sternway from a sail force by point of sail.
5. Broadphase and physics jobs when the battle outgrows 60 hulls.
