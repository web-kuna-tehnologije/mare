# The look we are aiming for, and how far the engine can take it (0.22.5)

The three Meshy reference frames in `assets/example` (and the Montague Dawson painting beside
them) are the target the user set for the regional and tactical zooms. This note says what each
frame shows, which of it the renderer already does, what is a knob away, and what needs new
work - so the next sessions spend their time on the gap and not on guessing.

## What the frames show

**Frame 1 - a strait seen from very high** (`3c86f7d6`): a deep indigo channel with the shelf
in cyan, every hillside drawn as a dense ridge-and-gully relief, dark woods on the shaded
slopes, pale terraces and villages on the sunny ones, a town on a round harbour, foam only as
a hairline along the rock. The relief is the *illumination* of a fine DEM, not colour: the
hills read because every gully is a shadow.

**Frame 2 - an atoll from orbit** (`bc487ec8`): the turquoise-to-cobalt bathymetry ramp with
sand showing through the lagoon, white reef foam, the open sea almost black-blue with visible
swell texture. Land is a thin green-and-ochre rim with roofs as a speckle.

**Frame 3 - a volcanic coast at the regional zoom** (`f76f4e25`): vertical rock faces into the
sea, a continuous forest canopy with individual crowns, terraces and tracks, a town filling a
bay with a quay, surf as a thin bright line at the foot of the cliffs.

## What the engine has today (0.22.5)

| Element | State | Where |
|---|---|---|
| Deep-to-shallow sea ramp | yes - blue-grey by request, the cobalt/turquoise of frame 2 is one palette edit | `sea/sea.frag` `deep`/`shallow`, the old warm ramp is in git (0.22) |
| Seabed through the water | yes, by measured thickness | `sea.frag` refraction |
| Sun glitter, Fresnel, foam, whitecaps, bow waves | yes | `sea.frag`, `sea/vs_common.glsl` |
| Surf line on the coast | yes, zoom-scaled band + breaker clock (0.22.5) | `sea.frag` `band`, `brk` |
| Relief that reads as ridges and gullies | **partly** - the base is the 2.8 km overview with procedural ridge noise on the rings; a gully is 3-5 km wide here against 100-300 m in the frames | `terrain_rings.cc`, `stream.cc lvl_amp` |
| Forest canopy with crowns | yes (paint) + instanced trees/pines within 28 units | `terrain.frag` canopy, `gfx/scatter.cc` |
| Fields, terraces, vineyards, villages | yes as painted masks + house props | land-use ring texture |
| Towns on the harbour | yes (procedural + Meshy houses) | `scene.cc`, `staged_scene.cc` |
| Cliffs into the sea | yes where the data says rock | shore mask R |
| Tracks between villages | yes (roads, painted) | `world/roads.cc` |

## The gap, and what closes it

1. **Resolution.** Everything in the frames that we lack is sub-kilometre: gully spacing,
   terrace lines, the crown texture of a forest, a 200 m quay. The overview is 2.8 km per
   sample and the land mask 7 km. The engine already reads finer regional sets (the reader
   accepts any `mare-terrain-v2` set; a 0.7 km Dalmatia exists in the exporter's folder and a
   30 m SRTM/Copernicus cut for the Adriatic would be the real step). **This is the single
   biggest lever and needs no renderer change.** Cost: 30 m over Dalmatia (6.5 x 5.2 deg) is
   ~24k x 19k samples = 1.8 GB float32 - cut it as 0.1-0.3 km (120-360 MB) first.
2. **Lighting the relief.** The frames' hills are drawn by shadow. We have sun shading, a
   baked horizon AO, a short-range sun occlusion and cascaded shadow maps that do not cover
   the terrain at the regional zoom. A **terrain self-shadow term from the heightfield** (a
   horizon map baked per ring, 8 directions, or a ray-march in the fragment shader against the
   ring texture) at the regional zoom would give frame 1's look at any data resolution. One
   session.
3. **Micro-relief where the data stops.** Below the sample spacing we add ridge noise. Frame 3
   wants *erosion* (gullies draining downhill, not isotropic bumps). A thermal/hydraulic
   erosion pass over the near ring (a few iterations on the GPU or the worker pool when a
   ring recomposes) gives directional gullies cheaply. One session; TODO 9a.
4. **Canopy as geometry.** The painted canopy plus 0.13-unit trees reads at the battle zoom,
   not from frame 3's altitude. Frame 3 needs ~100 m crowns as a continuous bumpy surface: a
   **parallax or displaced canopy layer** (the tessellated terrain already exists - displace
   the tree-cover texels upward by a crown height and shade them with the canopy noise) would
   do it without instances. Half a session.
5. **Sea colour.** Frame 2's ramp is a palette: `deep`/`shallow`/absorption in `sea.frag`. The
   user asked for blue-grey in 0.22.3; if the atoll look is wanted for the tropics, gate the
   warm ramp by latitude again (the 0.22 code did exactly that) - fifteen minutes.
6. **Towns.** Frame 1/3 towns are hundreds of roofs on a street grid filling the bay. Ours are
   a few dozen houses. The settlement generator (P2.1 in dev.txt) is the real answer; the
   painted village mask (roofs on a grid) already gives the colour from altitude.
7. **The Dawson painting** is the *battle* target: low sun, warm canvas, dark glossy water
   with long swell, spray at the bow. We have most of it; what is missing is a **planar or
   screen-space reflection of the hulls** in the water (dev.txt "Reflections", KNOWN_ISSUES)
   and bloom on the sail highlights. One session for SSR on the sea.

## Can we get there?

Yes, at the regional and tactical zooms, with the five items above in this order: finer DEM
(data, no code) -> terrain self-shadow -> erosion on the rings -> canopy displacement -> sea
SSR. The strategic zoom will never match frame 2's crispness without a 30 m source, and the
"orbital" look of frame 2 is a photograph's dynamic range: expect 80% of it. Nothing here
requires leaving GL 4 or the v3 framework.
