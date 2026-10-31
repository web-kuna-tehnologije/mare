# Should the UI be React (or something like it)?

Asked in the 0.22 round: *"Check if react would be good for UI or something similar with easy
styling apply."* The short answer is **no for React, yes for the problem behind the question**
- the UI is hard to style, and that is worth fixing with a native retained-mode toolkit rather
than a browser.

## What the UI is now

`ui/uikit.cc`, `ui/hud.cc`, `ui/dashboard.cc` and `ui/options.cc` drive **microui**, an
immediate-mode toolkit vendored inside the v3 framework. Every panel is re-emitted from C++
each frame (`mu_begin_window`, `ROW(...)`, `mu_slider_ex`), the widget set is fixed, and
"styling" is a handful of colour entries plus manual `mu_layout_set_next` rectangles. Adding a
designed panel means writing layout arithmetic by hand, which is exactly the friction behind
the question.

## Why not React

React is a DOM library. Running it means shipping a browser engine - CEF, WebView2 or
Ultralight - and that brings:

- **The compositing problem.** The game owns an OpenGL context on the window surface. A
  browser view must either take the window (and the game renders into a texture it composites,
  costing a full-screen copy per frame) or render offscreen and be uploaded as a texture every
  frame it changes. At 1600x900 that is a 5.7 MB upload per UI repaint, on the render thread.
- **The process and dependency weight.** CEF is ~200 MB of binaries and a multi-process
  architecture with its own update and crash story. The whole game today is a 3.4 MB
  executable built by one `cl` invocation with no package manager. This would be, by a wide
  margin, the largest dependency in the project.
- **The IPC seam.** Every value the HUD shows - treasury, fleet list, hover target, 17 goods
  per port - would have to be serialised to JavaScript each tick, and every click routed back.
  The dev dashboard alone reads dozens of live metrics per frame.
- **Latency and input.** A browser overlay wants the mouse. World picking, fleet orders and
  the right-drag pan all share that mouse with the panels; the current code answers this with
  one `ui_blocked` flag (`input.cc`). Through a browser it becomes a hit-testing round trip.

The one thing React would genuinely buy - CSS and a component model - does not need a browser.

## What is actually worth doing

**RmlUi** is the closest fit: a native C++ library that lays out a subset of HTML and CSS,
renders through *your* backend (it emits vertex/index buffers and texture binds, which maps
straight onto `rhi_geometry_make` / `rhi_draw`), has no browser, no extra process, and is a
few hundred KB. Panels become `.rml` + `.rcss` files that can be restyled without recompiling,
which is the "easy styling" the question is after. Data binding exists for the live values.

Rough shape of the work: an RmlUi render interface over the RHI (~300 lines), a system
interface for clock and clipboard (~50), input forwarding from `ui/input.cc` reusing the
existing `ui_blocked` path, then port panels one at a time - microui and RmlUi can draw in the
same frame, so the HUD could move first and the dev dashboard could stay on microui forever.

**Dear ImGui** is the other candidate: much better widgets than microui and excellent for the
dev dashboard, but it is immediate-mode and themed in C++, so it does not solve styling either.
It would be a straight upgrade for `dashboard.cc` and a sideways move for the player HUD.

**Recommendation.** Keep microui for the dev dashboard. If the player-facing UI is to be
designed rather than laid out in code, adopt RmlUi for the HUD, port panels incrementally, and
do not put a browser in the frame loop. Not scheduled yet - it is a day or two of plumbing
before the first panel looks different, and the 0.22 list has terrain work ahead of it.
