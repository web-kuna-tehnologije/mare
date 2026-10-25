**Naval Designer, Cities, Ports, Districts & LOD Asset Plan**

*Production summary / first-pass scope*

# 1. Core production philosophy

MARE should use a modular asset system: a relatively small library of high-quality source assets is recombined into many ships, cities and ports. The goal is to avoid creating every ship class or every city as a unique model. Customization, tier progression and procedural placement provide variety.

# 2. Ship Designer

The ship system uses two independent dimensions: Tier = technological generation, while Hull Type = the ship's intended role. The current plan has 6 generations and 4 hull roles, producing 24 base hulls.

| Dimension | Count | Meaning |
|---|---:|---|
| Tiers / generations | 6 | Progression from early campaign hulls to late 18th-century designs |
| Hull types | 4 | Interceptor, Trade Ship, Warship, Scout |
| Base hulls | 24 | 6 tiers × 4 hull types |

## Ship customization modules

- Hull construction: light / standard / reinforced / heavy.
- Gun decks and cannon layouts.
- Masts, rigging and sail configurations.
- Bow and stern variants.
- Cargo, provisions and ammunition spaces.
- Special modules such as marine quarters, surgeon, workshop, navigation room and officer spaces.
- Decorative/cultural sets, figureheads, flags and regional visual details.

Target for a strong first-pass naval designer: approximately 90-120 modular 3D source assets. A practical working target is ~100 assets, including the 24 base hulls.

## Designer gameplay principle

Every customization should involve trade-offs. Displacement acts as the main design budget: cannons, reinforcement, cargo, provisions, sails and special modules consume capacity and affect speed, range, crew, stability, construction cost and maintenance. This prevents a single universally optimal design.

# 3. Cities, Ports & Districts

Cities should also be modular. Regional architecture sets provide the visual language, while gameplay districts and port modules are assembled into individual settlements. The same modules can therefore support many locations without requiring a unique asset library for every city.

## Initial architecture sets

- Mediterranean - e.g. Ragusa/Venice/Genoa visual family
- Iberian
- Northern European
- Ottoman / Eastern Mediterranean
- Colonial / Atlantic

Working estimate: ~5 architectural sets × ~10 core assets = ~50 city assets.

## District structure

A developed city can expose roughly 6-10 district slots. Example districts include Harbor, Shipyard, Merchant Quarter, Residential/Old Town, Government, Arsenal/Military, Religious Quarter, Academy/Knowledge district and Fortifications. Selecting a district can move the camera to its physical location and open the corresponding management UI.

| District family | Approx. modular assets |
|---|---:|
| Harbor | ~10 |
| Shipyard | ~8 |
| Merchant Quarter | ~8 |
| Residential | ~8 |
| Government | ~6 |
| Military / Arsenal | ~8 |
| Religious | ~8 |
| Knowledge / Education | ~8 |
| Fortifications | ~10 |

District subtotal: approximately 74 modular assets.

## Port progression

Port tiers should primarily add modules rather than replace the entire harbor model. A simple progression:

- Tier I - small wooden pier + warehouse
- Tier II - additional pier + crane + harbor office
- Tier III - customs house + merchant warehouses
- Tier IV - dry dock + arsenal
- Tier V - large naval dockyard
- Tier VI - major late-game naval/industrial complex

## Dynamic economic visuals

Ports should visually reflect the economy: barrels for wine, timber stacks, grain sacks, ship hulls under construction, merchant traffic, warships, military supplies, population activity and reduced activity in declining ports. These are mostly reusable props and state-driven placement rather than unique city assets.

# 4. LOD strategy

LOD variants are not treated as separately generated source assets. One quality source model is cleaned and optimized into multiple LODs. This is essential because the strategic map may show many cities, ports and ships simultaneously.

| LOD | Use | Geometry target |
|---|---|---:|
| LOD0 | Close city/port/ship view | 100% |
| LOD1 | Close regional view | ~50% |
| LOD2 | Normal strategy camera | ~20% |
| LOD3 | Distant strategy camera | ~5-8% |
| LOD4 | Continental/world view | Ultra-low mesh or impostor/billboard |

## LOD behavior for cities

- LOD0: individual buildings, props, people, flags, smoke and animations.
- LOD1: remove people and small props; preserve major buildings, fortifications and docks.
- LOD2: replace many objects with optimized district cluster meshes.
- LOD3: reduce the settlement to a compact city mesh, port and a few landmarks.
- LOD4: silhouette/impostor plus strategic UI marker.

## LOD behavior for custom ships

- LOD0: full custom design - hull, guns, masts, sails, decorations and crew.
- LOD1: remove crew, small ropes and fine decoration.
- LOD2: simplify rigging and bake/merge gun details.
- LOD3: simple hull + mast/sail silhouette.
- LOD4: impostor/silhouette for very distant fleets.

When a player saves a custom ship class, the engine can create merged/baked distant representations so large fleets do not render dozens of modular components per ship. Instancing should be used wherever possible.

## Texture LOD guideline

Suggested progression: LOD0 2K/4K → LOD1 2K → LOD2 1K → LOD3 512 px → LOD4 128/256 px or impostor.

# 5. Asset & credit budget

| Category | Approx. source assets | Notes |
|---|---:|---|
| Ships / Ship Designer | ~100 | 24 base hulls + modular components |
| City architecture | ~50 | Initial regional architecture sets |
| Ports | ~30 | Harbor-specific structures and progression modules |
| Districts | ~70 | Reusable gameplay district modules |
| Props / cargo | ~30 | Economic and ambient visual storytelling |
| TOTAL | ~280 | First broad production target |

Using the current rough planning assumption of ~30 generation credits per finished source asset, ~280 assets correspond to approximately 8,400 credits. LOD meshes should be produced from the source models and therefore should not multiply this AI-generation credit estimate.

# 6. Recommended MVP / vertical slice

Do not generate the full library immediately. First build Ragusa as a vertical slice and validate the entire pipeline: one complete city, functional port, 6-8 visible districts, representative props, several ships and a working custom Ship Designer. Once the art scale, camera distances, modular snapping, LODs and performance are proven, expand into the other regional sets.

1. Phase 1: 24 base ship hulls + ~40 essential ship modules (~64 naval assets).
2. Phase 2: Ragusa city/port vertical slice with core district families.
3. Phase 3: lock the MARE Asset Spec - scale, pivots, sockets, materials, polygon budgets, textures, collision and LOD rules.
4. Phase 4: expand regional architecture and port sets.
5. Phase 5: add decorative/cultural variation and additional props only after gameplay readability is proven.

# 7. MARE Asset Spec - proposed standard

Every production asset should eventually follow one shared specification: real-world scale, naming rules, pivot/origin convention, modular sockets, material channels, texture resolution, polygon budget by category, LOD0-LOD4 targets, collision mesh rules, animation hooks where required, and engine import/prefab settings. This keeps AI-generated and hand-edited assets interchangeable and prevents technical debt as the library grows.

*MARE - working asset production summary*
