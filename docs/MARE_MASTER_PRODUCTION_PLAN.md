# MARE — Master Production Plan

> Working production brief for development, content, performance, budget and Steam release.
> This document intentionally separates **current repository state** from **planned target scope**.

## 1. Current baseline

The repository currently describes **Mare Liberum v0.7.1** as a C game on the v3 engine, with real-time 3D naval strategy, dynamic economy/trade, naval combat, ports, colonies, migration, 3D terrain/ocean, LOD, strategic map, save/load, metrics and simulation tests.

Current documented setting is 1620. The expanded **MARE target below (1450–1820)** is a development direction and must not be confused with already implemented functionality.

## 2. Product vision

MARE target: a naval-first grand-strategy game spanning roughly **1450–1820**. The player operates at the level of naval/economic state power, with real-time simulation and pause.

Core pillars:

- global maritime trade and production
- ports, hinterlands and economic development
- naval construction and fleet management
- modular ship designer
- naval warfare, blockades, piracy and convoy protection
- migration, culture, religion and population effects
- diplomacy and political influence
- technology/knowledge progression
- colonies and overseas expansion
- data-driven NPC/state AI
- highly scalable 3D world from strategic zoom to close port/naval views

MARE should differentiate itself from EU4-style grand strategy through deeper physical maritime trade, ships, ports, ship design and 3D naval presentation rather than trying to copy every EU4 subsystem feature-for-feature.

## 3. Code and AI development strategy

Keep the existing **C + v3/OpenGL codebase** unless profiling demonstrates a concrete blocker. Do not rewrite to Vulkan simply because Vulkan is newer.

Use coding AI primarily for:

- implementation and refactoring
- tests and reproducible simulation scenarios
- profiling and optimization
- data/content tooling
- shaders and rendering systems
- build automation
- review of difficult architecture changes

Use the stronger/more expensive model selectively for architecture, large refactors and difficult bugs; use the cheaper capable model for routine implementation.

A focused AI coding weekend should target a **gameplay-ready vertical slice / alpha**, not claim EU4-equivalent completeness. A good milestone is being able to start a campaign and play for hours without developer-console intervention while trade, economy, fleets, AI decisions, combat, progression, UI and save/load function together.

## 4. NPC AI architecture

Runtime NPC/state AI should normally be deterministic game code rather than an online LLM dependency.

Suggested model:

`state -> goals -> utility scoring -> decision -> action -> consequences -> reassessment`

Inputs can include threat, treasury, fleet strength, trade importance, diplomatic relations, shortages, strategic objectives and technological state. Generative AI can help author personalities, events, strategies and decision data during development, but normal gameplay should not require API calls.

## 5. 3D asset target — 600 source assets

Target approximately **600 source assets**. LOD variants are generated from these and are not counted as separate source assets.

| Category | Target |
|---|---:|
| Ship Designer | 110 |
| Cities / architecture | 70 |
| Ports / shipyards | 45 |
| District modules | 70 |
| Cargo / props | 35 |
| World-map environment | 100 |
| Naval battle / destruction | 45 |
| Landmarks | 60 |
| Crew | 15 |
| Regional / coastal biomes | 50 |
| **Total** | **600** |

### Ship Designer

Baseline concept: **4 hull types x 6 generations = 24 base hulls**, combined with modular masts, sails, bows/sterns, guns, cargo, utility parts and cultural/decorative variants.

The objective is combinatorial variety: 600 source assets should produce the visual impression of far more content through modular assembly, materials, procedural placement, culture/region variants and LODs.

## 6. AI mesh + texture pipeline

Primary candidates:

- **Tripo AI** — bulk mesh + texture generation candidate
- **Meshy** — alternate generation, retexturing and topology workflow
- **Hyper3D/Rodin** — reserve for difficult or hero-quality assets if its output justifies the cost

Recommended production flow:

`reference/concept -> AI 3D generation -> topology check/remesh -> UV/PBR -> scale/pivot normalization -> collision -> LOD0-LOD4 -> engine import -> visual/performance QA`

Do not assume one generation per final asset. Budget retries. For 600 accepted assets, plan roughly **1,200–1,800 generation attempts** until real production statistics show otherwise.

Before committing heavily to a provider, generate the same representative test set in competing tools: ship hull, mast/rigging, Mediterranean building, port crane, fortress wall, cannon, cargo prop, cliff/rock, vegetation and landmark fragment. Compare topology, UVs, PBR quality, historical fidelity, symmetry, scale, export reliability and cleanup time.

## 7. Texture policy

Avoid giving every asset unique 4K PBR textures.

Suggested targets:

- hero ships / landmarks: 2K–4K
- normal buildings: 1K–2K
- props: 512–1K
- distant LOD assets: 256–512 or atlas/impostor solutions

Use texture atlases/arrays and material reuse where practical. Texture memory and state changes matter more than the raw count of source assets.

## 8. Terrain generation

Use a **hybrid real-geography + procedural-detail** pipeline rather than hand-modeling the world.

Recommended architecture:

`real DEM/geographic data -> terrain generator -> 16-bit tiled heightmaps + masks -> MARE importer -> quadtree/chunk terrain -> biome materials -> procedural vegetation/rocks -> handcrafted hero patches`

World Machine and Gaea are candidates. Validate free/evaluation workflows first; buy a commercial tier only when it is proven to fit the production pipeline.

### Terrain renderer

Initial implementation should favor a debuggable system:

- reusable grid meshes
- quadtree/chunk streaming
- approximately five terrain LOD levels
- frustum/cell culling
- height/slope/biome material blending
- triplanar mapping for steep surfaces
- local high-detail patches around important ports/cities

Possible conceptual LOD scale:

| LOD | View | Effective detail target |
|---|---|---|
| LOD0 | port / city close-up | ~1–5 m |
| LOD1 | city surroundings | ~10–25 m |
| LOD2 | region | ~50–100 m |
| LOD3 | country | ~250–500 m |
| LOD4 | continent / world | ~1–4 km |

Do not procedurally invent the macro geography of Europe. Preserve real coastline and elevation structure, then add erosion/microdetail and biome presentation procedurally.

## 9. Performance strategy

Development hardware discussed: **Intel Ultra 9, 64 GB RAM, RTX 5070 Ti 12 GB VRAM**. Treat this as a development/high-end machine, not the minimum customer target.

Core principle: 600 source assets are not a problem by themselves. Performance depends on visible instances, draw calls, texture residency, simulation load, particles and animation.

Targets/guidelines:

- 60 FPS target on a sensible mid-range gaming PC
- 12 GB VRAM can serve High/Ultra development testing; design lower presets for smaller VRAM budgets
- LOD0 only close to the camera
- merge/bake modular ships into cheaper distant representations
- district/city LODs can collapse many objects into merged meshes/impostors
- aggressive frustum/distance/occlusion or cell culling
- instanced rendering for vegetation, props and repeated architecture
- texture streaming/atlases/arrays
- separate simulation tick rate from render frame rate
- distant economic/AI systems should update at appropriate simulation intervals, not 60 Hz

Useful OpenGL techniques as the renderer matures:

- instanced rendering
- multi-draw indirect where useful
- SSBO-driven instance data
- LOD selection
- frustum/cell/occlusion culling
- batched materials / texture arrays

The critical validation scene should be an **Adriatic vertical slice**: Ragusa/Dubrovnik + Venice, dense ports, terrain/ocean, vegetation, approximately 20–30 ships and representative VFX. Profile CPU, GPU, draw calls, triangles, VRAM and simulation cost.

## 10. Ocean and visual effects

Ocean presentation is a major visual signature of MARE and deserves priority.

Potential ocean stack:

- Gerstner-style waves or equivalent scalable wave model
- Fresnel/reflection/refraction treatment
- foam
- ship wake
- weather-driven wave state
- scalable shadow/reflection quality presets

Build a small reusable VFX framework rather than purchasing hundreds of individual effects.

Core presets/systems:

- cannon smoke
- muzzle flash and transient light
- impact/debris/splinters
- fire and black smoke
- city/port fire emitters
- ship wake and hull foam
- rain/snow
- fog/height fog
- lightning/storm effects
- dust
- bloom/exposure/color grading

One parameterized particle/emitter system should create many variants rather than every effect being bespoke.

## 11. Audio and music

Audio layers:

1. **SFX** — cannon, impacts, hull/wood, sails, UI, construction, market/port activity, weather.
2. **Ambience** — ocean, wind, gulls, ropes, timber, crowds, harbor, storm, rain, regional environments.
3. **Music** — adaptive peace/trade/exploration/tension/war/naval-battle states with regional character.

Use layered and randomized SFX. For example, cannon fire should combine multiple components and small pitch/gain variations rather than replaying one identical sample.

### Music target

Target roughly **50–70 final tracks**, around **60** as a planning number. At ~2.5–3 minutes average this is roughly 2.5–3 hours of music.

Possible allocation:

| Music category | Tracks |
|---|---:|
| Main theme / menu | 2 |
| Mediterranean | 8 |
| Northern / Western Europe | 6 |
| Ottoman / Eastern Mediterranean | 6 |
| North Africa | 5 |
| India / Indian Ocean | 5 |
| East Asia | 5 |
| New World | 5 |
| Peace / trade / exploration | 6 |
| Tension / war | 5 |
| Naval battle | 5 |
| Victory / defeat / special | 2 |
| **Total** | **60** |

**Suno** is the current primary AI-music candidate. Only use tracks whose commercial/game-use rights are valid under the paid plan and terms in effect when they are generated/downloaded. Archive evidence of licenses/terms and generation dates with production records. Re-check terms before Steam release.

## 12. UI / art direction

Target a Renaissance / 1450s grand-strategy presentation:

- dark navy / near-black panels
- bronze/gold ornament
- serif typography
- realistic painted/3D environments
- clear strategic readability at every zoom level

2D production includes UI decoration, icons, heraldry, maps, resource/good icons and marketing art. Maintain a consistent style guide before bulk generation.

## 13. Budget framework

Treat all figures as **planning caps**, not guaranteed prices. AI subscriptions and vendor terms change and should be re-checked before purchase.

Current working total cash cap: **approximately EUR 1,800**, with a preferred actual spend closer to **EUR 1,200–1,500** if free tiers, efficient generation and reusable systems work well.

Indicative allocation:

| Area | Planning range |
|---|---:|
| AI coding / debugging / refactoring | EUR 250–350 |
| ~600 3D mesh + PBR source assets | EUR 250–350 |
| Terrain tool / pipeline | EUR 100–300 |
| UI / 2D / heraldry / icons | ~EUR 100 |
| VFX materials/tools | EUR 50–100 |
| Audio / music / SFX | EUR 100–200 |
| Steam Direct fee | ~USD 100 |
| Reserve / retries / licenses | remainder to cap |

Do **not** spend the full cap upfront. Suggested gating:

1. spend only enough to prove the Adriatic vertical slice;
2. profile and validate asset quality;
3. validate the gameplay loop;
4. only then buy bulk credits/licenses;
5. keep a meaningful reserve until the game is gameplay-ready.

## 14. Steam release strategy

Steam Direct requires a per-product fee; verify the current fee and financial terms before release.

Working pricing strategy:

- demo / Coming Soon page first
- Early Access candidate: around **EUR 29.99**
- later major-update pricing: around EUR 34.99
- 1.0 candidate: around **EUR 34.99–39.99**, provided quality/content justify it

Do not price the game cheaply merely because AI reduces production cash cost. Price should reflect market positioning, quality and content.

### Revenue planning

For conservative internal calculations, use the common **30% Steam platform share tier** for revenue below Steam's higher-revenue thresholds, but do not treat `price x 70%` as net profit. Actual developer receipts are affected by VAT/sales tax, regional pricing, refunds/chargebacks, withholding/tax treatment and business expenses.

Illustrative gross customer spend at EUR 29.99:

| Copies | Gross customer spend |
|---:|---:|
| 1,000 | EUR 29,990 |
| 2,000 | EUR 59,980 |
| 5,000 | EUR 149,950 |
| 10,000 | EUR 299,900 |
| 25,000 | EUR 749,750 |

These are **not guarantees and not net income**.

## 15. Sales expectations and launch gates

Never assume 2,000 copies are guaranteed. The commercial outcome depends heavily on product quality, store presentation, demo reception, reviews, wishlists, launch timing and marketing.

Use **wishlist and demo performance as evidence before committing to sales forecasts**.

Working internal milestone:

- first meaningful commercial target: **10,000 Steam wishlists**
- 2,000 lifetime copies: reasonable target only if pre-launch signals support it
- 5,000–10,000 lifetime copies: aspirational solid-indie business target, not a forecast
- 25,000+: very successful outcome
- 50,000+: hit territory for internal planning purposes

Do not launch simply because the code is technically finished. Prefer a strong demo, Coming Soon page, trailer/screenshots, wishlist accumulation, Steam festival opportunities where appropriate, and a stable launch build.

## 16. Vertical-slice definition

The first production proof should focus on the Adriatic rather than the whole planet.

Suggested slice:

- Ragusa/Dubrovnik and Venice
- surrounding Adriatic terrain/coastline
- functioning ocean/weather
- representative ports and city districts
- 20–30 ships
- trade routes and goods
- economy/treasury/production
- ship construction and basic designer
- NPC state/trade decisions
- naval engagement
- representative VFX/audio
- save/load
- playable UI
- stable multi-hour session
- repeatable benchmark scene

**Exit criterion:** the slice must be fun, visually coherent, stable and performant before scaling asset production and world content.

## 17. Recommended production order

1. Stabilize existing v0.7.x baseline and tests.
2. Lock MARE design scope and reconcile the current 1620 baseline with the proposed 1450–1820 campaign design.
3. Build/upgrade terrain + ocean pipeline.
4. Finish core gameplay loop and NPC utility AI.
5. Build asset importer, PBR conventions and automatic LOD pipeline.
6. Produce a 10-asset cross-provider AI 3D benchmark.
7. Build the Adriatic vertical slice.
8. Profile and optimize CPU/GPU/VRAM/simulation.
9. Lock visual style guide.
10. Scale toward 600 source assets in batches of 50–100.
11. Add regional content, landmarks and global geography.
12. Finish VFX, ambience and adaptive music.
13. Build polished onboarding/tutorial and accessibility/settings.
14. Create Steam Coming Soon page, trailer and demo.
15. Gather wishlists/demo data and adjust scope/pricing/release timing.
16. QA, balance, localization and release preparation.

## 18. Rules for future development

- Profile before rewriting systems.
- Prefer reusable systems over one-off content.
- Keep gameplay deterministic where practical.
- Every major feature gets tests and a playable validation case.
- Generate assets in batches; do not buy all credits first.
- Store source/provenance/license metadata for every externally generated or licensed asset.
- Validate commercial rights for AI outputs before shipping.
- Keep source assets separate from generated LOD/cache/build artifacts.
- Do not call planned functionality "implemented" until it exists in the repository and is tested.
- Update this document as prices, providers, scope and measured production rates change.

## 19. Definition of success

The immediate goal is not "clone EU4." It is to make MARE a compelling, replayable naval grand-strategy game whose strongest systems — maritime economy, ships, ports, trade and naval warfare — are deep enough to establish a distinct identity.

A successful production path is:

`existing prototype -> stable gameplay alpha -> Adriatic vertical slice -> scalable content pipeline -> demo -> wishlist validation -> Early Access/1.0`
