# Design: hinterlands, trading hubs and regional resources

Status: **design, not yet implemented**. Target release 0.7. This document specifies the model,
data structures, algorithms, integration points, UI, AI, balance, migration and tests, in the
order they should be built. Read `ONBOARDING.md` first for the current architecture.

## 1. Goals

1. **Inland economy.** The land behind the coast is no longer an abstract "hinterland share" in
   `port_production()`. It is made of *land regions* (provinces) with their own population,
   prosperity, resources and inland settlements.
2. **Ports as hubs.** Everything a region produces for export flows to the port(s) it is connected
   to; everything the region imports arrives through them. Ports with rich, developed hinterlands
   become trading hubs with deep markets; ports without one stay fishing villages.
3. **Development loop.** Regions develop when their ports trade (goods arrive, money circulates);
   developed regions give their ports more goods, more population and more prosperity. Neglected
   regions stagnate and their settlements shrink.
4. **Directed colonisation.** Sites are no longer uniformly attractive: the prosperity/potential of
   the region behind a coast decides whether a colony thrives, and AI nations (and the UI hints for
   the player) prefer coasts whose region promises the most.
5. **Rare regional goods.** Iron, coal, copper, gold and silver (and later dyes, hemp...) exist
   only where the terrain/climate puts them, creating unique hubs worth fighting for.

Non-goals for 0.7: land units, roads as player-built objects, inland settlements as independent
actors with fleets, multi-port caravans between regions (regions are connected only through their
ports, which keeps the model simple and the sea the centre of the game).

## 2. Model overview

```
                 sea trade (fleets)                     region tick (daily)
   port A  <====================>  port B            ┌──────────────────────────┐
     ▲ hub link (throughput-limited)                 │ region R                 │
     │                                               │  population, prosperity  │
     └──────────────────────────── ─ ─ ─ ─ ─ ─ ─ ─ ─▶│  resources & development │
   inland settlements s1, s2 ... (in R)              │  inland settlements      │
   produce -> region export pool -> port stock       │  import needs <- port    │
```

* The world is partitioned into **land regions**: contiguous land cells grouped around coast
  segments (one region per ~4–6° of coast, plus interior regions that attach to the nearest coastal
  one). A region has 1–3 **hub ports**; each port belongs to exactly one region.
* A region has a **population** (thousands, separate from its ports), **prosperity**,
  **development** (0–3 tiers: wilderness, frontier, settled, developed), **resources** (potential
  per good incl. the new rare goods) and a list of **inland settlements** (small towns at inland
  resource spots: mines, farms, fur posts).
* Every day the region produces into an **export pool**, consumes from an **import pool**, and the
  hub link moves goods between the pools and the port markets, limited by the link's **throughput**
  (roads/rivers, grows with development). Ports are therefore markets *for the region*: their
  consumption norm grows with the region's population and their stock receives the region's exports.

## 3. Data model

```c
// core/types.h
#define MAX_REGIONS        96      // land regions (Europe+Americas+Africa at ~5 deg of coast each)
#define MAX_INLAND         256     // inland settlements
#define MAX_REGION_PORTS   3
#define MAX_GOODS          17      // + G_IRON, G_COAL, G_COPPER, G_GOLD, G_SILVER  (G_BULLION stays the coin-metal trade good)

enum { DEV_WILDERNESS=0, DEV_FRONTIER, DEV_SETTLED, DEV_DEVELOPED };

typedef struct region_t {
    char  name[NAME_LEN];          // "Andalusia", "Tidewater", "Minas Gerais"... from data/geo_regions.h
    int   alive;                   // has land
    int   culture_nation;          // dominant nation of the region's people (may differ from port owners)
    float pop;                     // thousands (rural + inland towns), separate from port pops
    float prosperity;              // 0.25..2.2 like ports
    float development;             // continuous 0..3; tier = (int)development
    float potential[MAX_GOODS];    // resource potential incl. rare goods (sum of cells, normalised)
    float labor[MAX_GOODS];        // region-level allocation (player sets it for regions whose hub ports it owns)
    float export_pool[MAX_GOODS];  // produced, waiting for a hub
    float import_need[MAX_GOODS];  // unmet daily demand (for prices/prosperity and the UI)
    int   hub[MAX_REGION_PORTS]; int nhub;     // port indices; hub[0] is the principal hub
    float throughput;              // units/day the links can move (roads, rivers, porters); grows with development
    int   ninland; int inland[8];  // inland settlement indices
    float cx, cz;                  // centroid for UI
    unsigned char *cells_hint;     // NOT saved: runtime lookup only (region id per TERR cell lives in world)
} region_t;

typedef struct inland_t {
    char  name[NAME_LEN];
    int   region;
    float x, z;                    // inland position (on land, >= 2 cells from the coast)
    int   kind;                    // INLAND_FARM, INLAND_MINE, INLAND_FURPOST, INLAND_TOWN
    int   good;                    // primary good (mines: G_IRON/G_COAL/G_COPPER/G_GOLD/G_SILVER; farms: grain/wine/sugar...)
    float pop, pop_cap;            // thousands
    float output;                  // units/day into the region export pool
    int   nation;                  // settler culture (set when founded), follows region culture
    int   alive;
} inland_t;

// game_t additions (SAVE_VERSION 4)
int       nregions; region_t regions[MAX_REGIONS];
int       ninland;  inland_t inland[MAX_INLAND];
// port_t additions
int   region;                      // owning land region (-1 for islands without one -> their own micro region)
float hub_share;                   // share of the region's export pool this port receives (0..1, by prosperity+traffic)
```

World side (`src/world`): `unsigned char *regionmap` (TERR_W×TERR_H, region id+1) computed at
`world_init` from coast segments (see §4.1). Not saved: deterministic from the seed and data.

## 4. Algorithms

### 4.1 Region generation (world_init, once)
1. Walk the coastline of the land mask; every TERR cell adjacent to water gets a **coast segment
   id** by flood-filling along the coast until the segment spans ~5° or hits a data-defined
   boundary (a list of lon/lat cuts in `data/geo_regions.h` with names: "Biscay", "Brittany",
   "Flanders", "New England", "Chesapeake", "Carolinas", "Florida", "Yucatán", "Spanish Main",
   "Brazilian Coast", "Gold Coast", "Andalusia", "Levant", ...). Named segments take their name;
   unnamed ones generate "<Nearest port> Hinterland".
2. Every land cell is assigned to the nearest coast segment **by land distance** (multi-source
   BFS over land cells, cost 1 per cell, so mountain ranges don't matter yet; a later refinement
   can weight cells by height). This gives contiguous regions that reach inland.
3. Regions larger than 60 TERR cells or reaching >8° inland are split at the BFS distance 4°:
   the far part becomes an **interior region** attached to the same hubs but with throughput 0.4×
   (longer roads). This models e.g. the Andes vs the Peruvian coast.
4. Ports are attached to the region of their town cell. Islands smaller than 4 cells get a
   micro-region of their own (Madeira, Barbados) — they still work, just with little land.

### 4.2 Regional resources (world_init)
For each region, sum `site_potential()` over its cells (the existing climate model) and add the
rare goods from a new deposit model:

| Good | Where (deterministic fbm per seed, thresholds tuned so ~8–12% of regions have each) |
|---|---|
| `G_IRON` | temperate/cold uplands with forest (charcoal): height 2–6, lat 40–65 |
| `G_COAL` | lowland/upland temperate basins, strong low-frequency noise band (England, Low Countries, Appalachia) |
| `G_COPPER` | mid-height hills any latitude, medium-frequency noise |
| `G_SILVER` | mountains (height > 5), tropics and subtropics favoured (Andes, Mexico) |
| `G_GOLD` | mountains + tropical rivers (Minas Gerais, Gold Coast), rarest |

Historical anchoring: `data/geo_regions.h` may pin known deposits (`{ "Potosí", -65.7, -19.6, G_SILVER, 2.0 }`,
Zacatecas silver, Minas Gerais gold, Falun copper, Swedish/Basque iron, Newcastle coal) so the
17th-century map feels right while the noise fills the rest. Pins add to the region's potential and
spawn an initial inland mine.

Potentials are normalised so a region's total lies in 2–10; rare goods are 0–2.

### 4.3 Inland settlements
* Initial: every pinned deposit and every region with `potential[g] > 0.8` for a farm good gets 1–2
  inland settlements at the best cell (highest local potential, ≥ 2 cells from the coast, on land),
  population 1–4k for historical regions with developed ports, 0 (unsettled) elsewhere.
* New inland settlements appear when a region's development crosses a tier (frontier → settled →
  developed) and an unexploited `potential[g] > 0.6` exists: the settlement is founded by the
  region's culture. The player can also found one explicitly from a hub port they own (button
  "Charter a mine/plantation at <spot>": cost 8000, requires development ≥ frontier).
* `output = kind_rate * potential[good] * f(pop)` into the region's export pool (mines: 4/day at
  pop 1k, +1.5 per extra k, capped by pop_cap = 3 + 6*development).
* Inland settlements need imports (arms, cloth, salt, wine/grain for mines); unmet needs reduce
  output (×0.5 at full shortage) and pop growth.

### 4.4 Region daily tick (`regions_tick`, after `economy_tick`)
```
for each region R with alive hubs:
  // 1. production
  for g: export_pool[g] += pop_rate(R) * potential[g] * labor[g] * dev_mul(R)      (rural output)
  for s in inland(R): export_pool[s.good] += s.output
  // 2. region consumption: like a port with pop = R.pop, colonial/european profile of its hubs
  for g: need = region_consumption(R, g); take from import pool (fed by hubs, see 3); import_need[g] = need - taken
  // 3. hub links (throughput T shared across hubs by hub_share)
  for each hub port P (share k):
      cap = T * k
      export side: move min(export_pool[g]*k, cap_left) into P.stock[g]  (goods arrive in the port market)
      import side: move from P.stock[g] into the region import pool up to the region's unmet need,
                   paying the port's price * (1 - 0.1) to the port owner as inland sales tax  -> treasury
  // 4. development
  traded = goods moved both ways today / (R.pop + 10)
  dev_target = 0.6*log2(1 + traded*4) + 0.4*prosperity + 0.3*(#hubs owned by a nation with Market) ...
  development += (dev_target - development) * 0.002 * dt   (years to develop; decays when neglected)
  throughput = 6 + 14*development + 4*(hub Docks levels)
  // 5. prosperity & population (same shape as ports): met needs, exports sold, development
  // 6. culture drifts toward the hub owners' nations weighted by hub_share; big mismatch -> unrest
  //    in the hubs (feeds the existing settlement unrest: a restless hinterland is a restless port)
```
Complexity: regions ≤ 96 × goods 17 per tick — negligible next to fleets.

### 4.5 Ports as hubs
* `port_consumption()` uses `pop + region.pop * hub_share * 0.5` (the region shops through the
  port) → deeper markets in real hubs. The old "hinterland staples" share in `port_production()`
  is **removed**: staples now arrive from the region's export pool. (Ports on a micro-region keep a
  small fixed staple share so fishing villages don't starve.)
* `hub_share` is recomputed daily: `share ∝ prosperity * (1 + traffic) * (1 + 0.5*Market level)`,
  so the busiest port of a region captures most of its trade — a region's hub can shift over time.
* A port whose region reaches DEV_DEVELOPED gets `pop_cap += 40`, prosperity +0.15 and a
  "Staple port" badge; its norm uses region consumption → prices more stable (big market).

### 4.6 Directed colonisation
* A site's attractiveness `A = Σ potential(region) * (1 + development) * access` where `access` =
  1 for a coast cell with ≥ 3 land cells behind it within 2° (room to grow), 0.3 for a sliver.
* `site_check` now also refuses coasts whose region already has 3 hubs (no room for another
  hub) unless the existing hubs are at war with the founder (a contested coast is allowed: the
  new colony competes for the region's trade and culture).
* AI `nation_claim_coast` samples 10 coasts and picks the highest `A` instead of the first valid
  one; `nation_colonize` for historical sites is unchanged.
* UI: hovering a coast with a fleet selected shows a tooltip "Region: Carolinas — frontier,
  resources: tobacco 1.2, timber 0.9, iron 0.5 — attractiveness 7.4"; map view colours regions by
  development (hatching) when the new "Regions" toggle is on.

### 4.7 Rare goods in the economy
| Good | base price | consumers | notes |
|---|---|---|---|
| Iron | 18 | shipyards (ship cost −10% at ports with iron stock > 100), forts (+repair), arms production (arms output ×1.5 with iron) | staple-industrial |
| Coal | 10 | European cities (heating, luxury 0 in cold lat), arms/iron smelting multiplier | bulky, low value: short-haul good |
| Copper | 40 | shipyards (hull sheathing: +10% ship speed upgrade purchasable), arms, Europe luxury | |
| Silver | 120 | European demand like bullion; mints: the **owner of a hub receiving silver gets 2% of sales as seigniorage** | treasure fleets load silver from export pools |
| Gold | 250 | same, rarer, Europe only | |

`G_BULLION` is kept as the generic coin metal for compatibility (saves, tables); new deposits
produce silver/gold specifically, and the Spanish treasure fleet draws from Veracruz/Cartagena's
silver pools first.

## 5. UI

* **Region panel** (click a region name in a port panel or the map): name, development tier bar,
  population, prosperity, hubs with their shares, resources with labour sliders (owner of the
  principal hub controls labour), inland settlements list with output and needs, import shortages
  ("lacks: arms 12/day, cloth 30/day"), culture bars, a "Charter settlement" button.
* **Port panel additions**: "Hub of <region> (share 62%) — region exports arriving: sugar 18/day…",
  link throughput used/available.
* **Map view**: "Regions" toggle shades land by development; hubs drawn with a larger disc; inland
  settlements as small squares with their good's colour; links as faint dashed lines to the hub.
* **Chronicle**: "The Carolinas reach the frontier tier", "A silver mine opens at …", "Roads of
  Andalusia improved (throughput 28/day)".

## 6. AI

* Merchant planner: region export pools appear as expected port stock (`stock + pool*share`), so
  merchants anticipate arrivals at hubs.
* Nation AI invests in **Market/Docks at principal hubs** first (throughput), charters mines when
  `potential[rare] > 1` and development ≥ frontier, sends colonists to the coast with the highest
  attractiveness in reach.
* Warship AI values ports by `hub_share * region.development` when choosing siege targets — rich
  hubs are worth taking (and the player should fortify them).

## 7. Balance targets (checked with `--simulate`)
* A developed European region supplies ≈ 85% of its hubs' staples (today's hinterland share);
  undeveloped colonial regions ≈ 40% → early colonies depend on imports (historical).
* Development from wilderness to settled takes 8–15 years of steady trade; neglect halves it in ~10.
* Rare goods: world output iron ≈ 60/day, coal 80, copper 25, silver 35, gold 8; Europe's demand
  slightly above supply.
* Hub share: the principal hub of a two-port region sits at 60–75%.
* Player income from inland sales tax + seigniorage should be comparable to tariffs (~30% of
  passive income) so owning hubs matters.

## 8. Implementation plan

| Phase | Scope | Files | Tests |
|---|---|---|---|
| 1 | region map generation, names, resources incl. rare deposits, `regionmap`, debug overlay | `world/regions.[ch]`, `data/geo_regions.h`, `types.h`, `scene.c` | every land cell has a region; every port has a region; deposits count in range; deterministic per seed |
| 2 | region tick: production, consumption, hub links, development, prosperity; remove the staple share from ports | `sim/region.[ch]`, `economy.c` | staple supply ratios (§7); no NaN; determinism after load |
| 3 | inland settlements (initial, tier-triggered, chartered), needs and output | `sim/region.c`, `hud.c` | mine appears when tier reached; output drops under shortage |
| 4 | rare goods in tables, consumers and effects (shipyard discount, arms multiplier, seigniorage) | `data/tables.h`, `economy.c`, `hud.c` | prices in band; seigniorage paid |
| 5 | directed colonisation (attractiveness, AI choice, tooltip), hub_share dynamics | `settlement.c`, `ai.c`, `input.c` | AI picks the best of sampled coasts; hub share shifts with traffic |
| 6 | UI: region panel, map shading, chronicle lines; docs & balance pass | `hud.c`, `scene.c`, docs | screenshot checks; balance targets |

Save format: bump to **v4** in phase 1 (new arrays in `game_t`). Keep `G_BULLION`.

## 9. Risks and mitigations
* **Complexity creep**: regions are connected to the world only through hub links — no inland
  pathfinding, no land units. Keep it that way in 0.7.
* **Double counting** staples (old hinterland share + new region supply): remove the old term in
  phase 2 and re-tune `TAG_PRODUCTION`; the headless balance run is the gate.
* **Determinism**: region generation must use only the seed and data; the region tick uses `G.rng`
  exclusively; `regionmap` is rebuilt on load from the seed.
* **Performance**: region BFS at init ≈ 50 ms; daily tick negligible. Region tooltips must use the
  cached `regionmap`, never recompute.
* **Player agency**: make sure the player can *do* something about a poor region: build Market/
  Docks (throughput), bring imports (shortages shown), charter settlements, fortify hubs.

## 10. Open questions
* Should interior regions be able to switch hubs (e.g. Andes → Buenos Aires vs Callao)? Proposal:
  interior regions attach to the coastal region with the highest development, re-evaluated yearly.
* Do inland cultures cause rebellions on their own, or only through their hubs? Proposal: only
  through hubs in 0.7.
* Rivers as high-throughput links (Rhine, St Lawrence, Amazon): cheap to add as data pins that
  multiply throughput ×2 for the regions they touch. Candidate for 0.7 if time permits.
