# MARE --- 3D Art, Asset & Production Summary

## 1. Osnovna filozofija

MARE bi trebao koristiti **modularni asset sustav**: relativno mali broj kvalitetnih source asseta kombinira se u velik broj brodova, gradova, luka i scena.

Cilj nije modelirati svaki brod ili grad zasebno, nego koristiti modularne hullove i brodske komponente, regionalne arhitektonske setove, modularne distrikte, proceduralno postavljanje propsa i vegetacije, LOD sustav, terrain/ocean shadere, VFX i atmosferu te landmarke za prepoznatljivost regija.

## 2. Ship Designer

- **6 Tierova / generacija**
- **4 Hull Typea:** Interceptor, Trade Ship, Warship, Scout
- **24 osnovna hulla** (6 × 4)
- cilj za prvi ozbiljni modularni sustav: **~90–120 source asseta**, praktična meta **~100**

Modularni dijelovi uključuju hull construction varijante, gun deckove i cannon layoute, jarbole, rigging, jedra, bow/stern varijante, cargo/provision/ammunition module, marine quarters, surgeon/workshop, navigation room, officer quarters, dekoracije, figureheads, zastave te kulturne/regionalne varijante.

Ship Designer koristi **displacement/weight budget**. Svaka komponenta utječe na firepower, speed, maneuverability, range, cargo, crew requirement, stability, construction cost i maintenance, tako da nema jednog univerzalno najboljeg dizajna.

## 3. Gradovi

Gradovi se ne modeliraju pojedinačno. Koriste se regionalni modularni arhitektonski setovi. Početni setovi: Mediterranean, Iberian, Northern European, Ottoman / Eastern Mediterranean i Colonial / Atlantic.

Procjena: **~5 setova × ~10 core asseta = ~50 city asseta**.

Primjeri: obične i bogate kuće, trgovi, administrativne i crkvene zgrade, warehouses, kule, gradske zidine, gates i market buildings.

## 4. District System

Razvijeni grad može imati **6–10 district slotova**. Glavne kategorije: Harbor, Shipyard, Merchant Quarter, Residential / Old Town, Government, Military / Arsenal, Religious Quarter, Knowledge / Academy i Fortifications.

Klik na district može pomaknuti kameru prema fizičkoj lokaciji distrikta i otvoriti odgovarajući management UI.

| District | Asseti |
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

Ukupno: **~70–75 modularnih district asseta**.

## 5. Luke

Port progression treba uglavnom **dodavati module** umjesto zamjene cijelog modela:

- Tier I: mali drveni mol + warehouse
- Tier II: dodatni pier + crane + harbor office
- Tier III: customs house + merchant warehouses
- Tier IV: dry dock + arsenal
- Tier V: veliki naval dockyard
- Tier VI: veliki late-game naval/industrial kompleks

Procjena: **~30 port-specific asseta**.

## 6. Dynamic Economic Visuals

Gradovi i luke trebaju vizualno pokazivati ekonomiju: wine trade → bačve, timber → hrpe drveta, grain → vreće i skladišta, ship construction → hull u dry docku, veliki trade volume → više merchant shipova, rat → warships/vojnici/topovi/supplies, pad grada → manje aktivnosti i prazniji dockovi.

Procjena: **~30 reusable cargo/prop asseta**.

## 7. 3D World Map

Sama karta je prije svega terrain mesh + terrain materials + procedural vegetation + ocean shader + atmospheric effects + modularni environment asseti.

| Kategorija | Procjena |
|---|---:|
| Trees / forests | 15–20 |
| Bush / scrub | 8–10 |
| Rocks / cliffs | 10–15 |
| Mountains | 15–20 |
| Coastal cliffs | 8–12 |
| River details | 5–10 |
| Farms / fields | 10–15 |
| Bridges | 6–10 |
| Ruins / monuments | 10–15 |
| Small village modules | 10–15 |
| General map props | 15–20 |

Ukupno **~120–160 world environment asseta**, praktična meta **~140**.

Terrain koristi otprilike **15–25 material setova** (grassland, Mediterranean dry, farmland, rock, mountain, snow, sand, mud, forest floor...), blendanih prema visini, nagibu, klimi, biomeu i regiji.

## 8. Map Scale & Visual Style

Vizual treba imati **painted / miniature / cartographic** karakter. Ključ je **cartographic exaggeration**: planine mogu biti 1.5–2× izraženije, gradovi veći od realne geografske skale, luke jasno čitljive, landmarki vidljivi.

Tri razine prikaza:

- **World View:** simplified terrain, exaggerated mountains, forests, city miniatures, borders, province overlays, trade routes, fleet models, atmospheric haze.
- **Regional View:** roads, farms, villages, individual forest clusters, ports, fortifications, ships.
- **City / Port View:** detailed buildings, district props, people, carts, cargo, dock workers, ships, smoke, animals, flags.

Idealno: **World → Region → City/Port** bez loading screena.

## 9. Ocean System

Ocean treba tretirati kao ključni sustav, prvenstveno kroz **ocean shader + VFX**, ne kroz velik broj modela.

Podrška: wind direction, wave direction, wind strength, wave height, depth-based color, coastal foam, weather response, ship wake, cannon impacts, storms, fog i dawn/sunset/night lighting.

Isti shader može pokriti Mediterranean calm, Atlantic swell, Caribbean, North Sea, storm, dawn, sunset i night kroz parametre.

## 10. Naval Battles

Brodovi iz Ship Designera ponovno se koriste u battle scenama.

**Water VFX: ~10–15 efekata** (bow wave, stern wake, foam, cannon splash, large splash, debris splash...).

**Combat VFX: ~15–20 efekata** (muzzle flash, cannon smoke, impact smoke, wood impact, sail hit, fire, powder explosion, burning deck, smoke columns, ship explosion...).

## 11. Destruction System

Koristi se reusable modularni damage kit, ne posebna uništena verzija svakog hulla: broken mast pieces, broken yards, torn sails, hull-hole decals, burn decals, wood debris, floating debris, rope pieces, destroyed cannons i fire/smoke VFX.

Procjena: **~40 physical destruction asseta**.

## 12. Crew

Dovoljno je **~8–12 base character modela** za sailor, marine, officer, captain i gunner role, uz materijalne/uniform/regionalne varijante.

Otprilike **30–40 osnovnih animacija**: idle, run, carry ammunition, reload cannon, fire cannon, pull rope, climb, fall, injured, melee, abandon ship itd. Crew se gasi na većim udaljenostima.

## 13. Coastal Battle Environments

Početni biome setovi: Mediterranean, Atlantic Europe, North Sea / Baltic, Caribbean, West Africa i Indian Ocean / Asia.

Naval battle može koristiti lokalni skyline/obalu (npr. Ragusa cliffs/walls/forts, Venice lagoon/skyline, Caribbean islands, North Sea dunes).

Procjena: **~80–100 reusable coastal environment asseta**.

## 14. Weather

Weather je uglavnom shader/VFX sustav: **Clear → Cloudy → Rain → Heavy Rain → Storm → Fog**.

Uključuje cloud layers, rain particles, lightning, fog, wind, ocean response i visibility changes. Može utjecati na accuracy, visibility, sailing speed, maneuverability, sail usage i risk.

## 15. Landmark System

Važne regije trebaju prepoznatljive strategy-map landmarke, npr. Ragusa walls/Lovrijenac, Venice San Marco/campanile, Constantinople Hagia Sophia/walls, Rome monumental skyline, Paris Notre-Dame, London Tower, Lisbon Belém area.

Procjena: **~50–80**, praktična meta **~60 landmark asseta**.

## 16. LOD Standard

LOD varijante se **ne računaju kao novi source asseti**.

| LOD | Upotreba | Geometry |
|---|---|---:|
| LOD0 | Close view | 100% |
| LOD1 | Close regional | ~50% |
| LOD2 | Normal strategy | ~20% |
| LOD3 | Distant strategy | ~5–8% |
| LOD4 | World view | impostor / ultra-low |

### City LOD
- LOD0: buildings, people, props, flags, smoke, animations
- LOD1: bez ljudi i sitnih propsa
- LOD2: optimized district cluster meshes
- LOD3: compact city mesh + port + landmarks
- LOD4: silhouette/impostor + strategic marker

### Ship LOD
- LOD0: hull, cannons, masts, sails, decoration, crew
- LOD1: bez crew i sitnih detalja
- LOD2: simplified rigging + baked/merged details
- LOD3: simple hull + mast/sail silhouette
- LOD4: impostor/silhouette

Custom ship class nakon spremanja može dobiti **merged/baked distant representation** radi boljeg renderinga velikih flota.

## 17. Texture LOD

- LOD0: 2K / 4K
- LOD1: 2K
- LOD2: 1K
- LOD3: 512 px
- LOD4: 128 / 256 px ili impostor

Normal, roughness i detail mape reduciraju se ili uklanjaju na velikim udaljenostima.

## 18. Ukupni okvirni Asset Budget

| Sustav | Source asseti |
|---|---:|
| Ship Designer | ~100 |
| Cities | ~50 |
| Ports | ~30 |
| Districts | ~70 |
| Cargo / props | ~30 |
| World environment | ~140 |
| Naval battle environments | ~90 |
| Destruction | ~40 |
| Crew | ~10–15 |
| Landmarks | ~60 |
| **TOTAL** | **~620** |

To je široki production scope, ne nešto što treba napraviti odmah.

## 19. Credit Budget

Za grubo planiranje, uz pretpostavku **~30 AI generation credits po source assetu**:

**620 × 30 ≈ 18,600 kredita**.

LOD1–LOD4 se generiraju/optimiziraju iz postojećeg source asseta i ne bi trebali množiti AI generation budget.

## 20. Preporučeni Vertical Slice

Prvo napraviti **Adriatic Vertical Slice**:

- Ragusa
- Venice
- nekoliko manjih luka
- dio Jadrana
- nekoliko custom shipova
- Ship Designer
- jedna kompletna naval battle scena
- jedan kompletan port
- 6–8 distrikta
- trade traffic
- weather
- ocean
- damage/destruction
- osnovni crew
- World → Region → City camera transition

Za vertical slice okvirno **~150–200 kvalitetnih source asseta**.

Tek kada su potvrđeni art style, modularni sustav, kamera, LOD tranzicije, performance i gameplay readability, širiti biblioteku prema punom svijetu.

## 21. Production Pipeline

**Concept / Reference → AI ili artist 3D source → Mesh cleanup → UV / materials → LOD0 game-ready → LOD1 → LOD2 → LOD3 → LOD4 / impostor → collision → engine import → prefab → procedural placement → lighting / VFX → performance test**

## 22. MARE Asset Spec

Prije masovne proizvodnje definirati zajednički **MARE Asset Spec** s pravilima za real-world scale, unit convention, naming, folder structure, pivot/origin, modular sockets, ship attachment points, building snapping, material channels, texture resolutions, polygon budgets, LOD0–LOD4, collision, animation hooks, shader requirements, engine import, prefab i performance targets.

## 23. Trenutna ukupna produkcijska slika

MARE može postići bogat vizualni identitet bez tisuća potpuno unikatnih modela. Široki scope je oko **~620 source asseta**, a modularnost + procedural placement + materials + shaders + VFX + LOD + instancing + landmarks mogu iz toga proizvesti mnogo veći broj vizualno različitih brodova, gradova, luka, distrikta i battle scena.

Za prvu fazu prioritet treba biti:

**150–200 asseta → Adriatic Vertical Slice → potvrda kvalitete i performancea → skaliranje produkcije.**
