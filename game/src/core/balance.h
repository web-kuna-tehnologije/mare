// All gameplay tunables in one place. Run `mare --simulate=730` after changing them.

#ifndef MARE_BALANCE_H
#define MARE_BALANCE_H

// movement
#define BASE_SPEED        (9.0f * WORLD_SCALE)    // world units per day at ship speed 1.0 (~2.25 deg/day)
#define ENGAGE_RANGE      (3.0f * WORLD_SCALE)    // broadside range (world units)
#define SIGHT_RANGE      (14.0f * WORLD_SCALE)    // detection range
#define WIND_MIN_FACTOR   0.45f
#define WIND_MAX_FACTOR   1.35f
#define STORM_SPEED_MUL   0.55f
#define STORM_DAMAGE_DAY  2.5f    // hull per day per unit of storm strength
#define FLEE_SPEED_MUL    1.12f

// economy
#define PRICE_EXPONENT    0.55f   // price = base * (norm/stock)^exp
#define PRICE_MIN_MUL     0.25f
#define PRICE_MAX_MUL     4.0f
#define NORM_CONS_DAYS   45.f     // comfortable stock = consumption*days + production*days
#define NORM_PROD_DAYS   30.f
#define NORM_FLOOR        6.f
#define SURPLUS_DECAY     0.05f   // per day, of stock above 2x norm (hinterland absorbs)
#define DEMAND_ELASTICITY 0.6f    // consumption scales with (base/price)^e
#define SUPPLY_ELASTICITY 0.35f
#define TAG_PRODUCTION    5.0f    // units/day per tag letter per size point
#define COLONIAL_TAG_MUL  1.6f    // sugar/tobacco/bullion/furs in the colonies
#define TARIFF_HOME       0.02f
#define TARIFF_FREE       0.03f
#define TARIFF_FOREIGN    0.04f
#define TAX_RATE          0.25f   // ducats per 1000 pop per prosperity per day
#define PIRATE_FENCE_MUL  0.75f   // price factor at pirate havens
#define UPKEEP_PER_CREW   0.02f   // ducats per crew per day
#define COLONY_COST    6000.f
#define REPAIR_COST_PER_HP 2.f

// settlements (colonisation expansion)
#define MERGE_DIST          (6.0f * WORLD_SCALE)    // world units (~1.5 deg): settlements closer than this merge
#define MIN_SITE_DIST       (3.0f * WORLD_SCALE)    // minimum distance from an existing settlement for a new site
#define SITE_WATER_REACH    6       // nav cells searched for a harbour
#define MERGE_MIN_AGE      60.f     // days a founded colony must exist before it can be merged
#define MERGE_UNREST_SHOCK  0.3f    // unrest added when the merged settlement changes owner
#define IMMIGRATION_RATE    0.0003f // share of free capacity arriving per day at prosperity 1
#define IMMIGRATION_FOREIGN 0.3f    // share of immigrants coming from trading partners' cultures
#define ASSIMILATION_RATE   0.0004f // share of minority culture adopting the owner's per day (at tolerance 1)
#define UNREST_CULTURE      0.004f  // unrest per day at 100% foreign culture (scaled by 1-tolerance)
#define UNREST_WAR_MUL      2.0f    // ... doubled when that culture's nation is at war with the owner
#define REBELLION_FOREIGN   0.5f    // foreign culture share needed for a secession rebellion
#define FORT_GARRISON_CALM  0.6f    // each Fort level divides culture unrest by (1 + this): the player's answer to restless conquests
#define POPCAP_BASE        10.f     // thousands a bare colony site supports
#define POPCAP_POTENTIAL    4.f     // per unit of total resource potential
#define POPCAP_BUILDING    15.f     // per Market/Warehouse level
#define POPCAP_EUROPE     400.f     // old-world cities
#define PROSPERITY_OUTPUT   0.25f   // prosperity bonus at 1 unit of output per 1000 pop per day

// hinterlands (0.7)
#define REGION_SEGMENT_DEG   5.5f   // coast length per region
#define REGION_RURAL_RATE    0.10f  // units/day per 1000 rural pop per unit of potential (staples)
#define REGION_CONSUME_MUL   0.15f   // rural per-capita consumption vs towns (self-sufficient)
#define REGION_THROUGHPUT_BASE 20.f  // link capacity units/day at wilderness
#define REGION_THROUGHPUT_DEV 30.f  // ... per development point
#define REGION_DEV_RATE      0.002f // development approaches its target at this rate per day (years to develop)
#define REGION_INLAND_TAX    0.01f  // share of inland sales value paid to the hub owner
#define SEIGNIORAGE          0.02f  // share of silver/gold sales paid to the hub owner
#define MINE_RATE            4.0f   // units/day per inland mine at pop 1k and potential 1
#define FARM_RATE            6.0f

// prosperity / population
#define PROSPERITY_RATE   0.01f
#define POP_GROWTH        0.00012f
#define POP_GROWTH_COLONY 0.00006f
#define SIZE_TOWN_POP    12.f
#define SIZE_CITY_POP    60.f

// combat
#define DAMAGE_PER_GUN    1.8f    // hull points per gun per day of engagement
#define CAPTURE_CHANCE    0.4f
#define MORALE_LOSS_SHIP  0.25f
#define MORALE_RECOVERY   0.08f   // per day
#define MORALE_FLEE       0.35f
#define PIRATE_BOUNTY   800.f
#define FORT_BASE_HP    300.f
#define FORT_LEVEL_HP   400.f
#define FORT_SIZE_HP    100.f
#define FORT_GUN_DAMAGE   1.2f
#define SIEGE_RATE        1.0f    // fort hp per gun per day
#define STORM_CREW_MIN    60      // crew needed to storm a port once the fort falls
#define CAPTURE_COOLDOWN 150.f

// AI
#define AI_MERCHANT_RANGE      (520.f * WORLD_SCALE)
#define AI_FREECITY_RANGE      (200.f * WORLD_SCALE)
#define AI_MERCHANT_CAPITAL  6000.f
#define AI_MERCHANT_CAPITAL_SHIP 4000.f
#define AI_BUY_MERCHANT_TREASURY 30000.
#define AI_BUY_WARSHIP_TREASURY  90000.
#define AI_INVEST_TREASURY      120000.
#define AI_SIEGE_WAR_AGE        45.f    // days of war before sieges start
#define AI_SIEGE_MAX_CAPTURES    2
#define AI_SIEGE_CHANCE          0.12f
#define AI_SIEGE_TREASURY    50000.
#define PIRATE_MAX_SHIPS         4      // pirates return to haven to sell prizes beyond this

// events (probabilities per day)
#define P_STORM      0.035f
#define P_PLAGUE     0.004f
#define P_HARVEST    0.012f
#define P_STRIKE     0.0025f
#define P_FASHION    0.006f
#define P_COLONISTS  0.004f
#define P_FIRE       0.003f
#define P_REBELLION  0.0025f
#define P_RANDOM_WAR 0.0006f
#define P_PEACE      0.0025f    // per day once a war is older than PEACE_MIN_DAYS
#define PEACE_MIN_DAYS 365.f

// simulation
#define SIM_STEP 0.1f            // days per fixed step
// How often the simulation runs, in real seconds. The world keeps its speed: a tick advances
// however many days have accumulated since the last one, as a burst of SIM_STEP sub-steps, so
// balance, events and save/load determinism are exactly what they were - only the number of
// tick *events* changes. Fleets are interpolated across the whole interval (gfx/scene.c), so
// nothing on screen slows down or stutters. Capped in days as well, so speed 4 does not jump a
// month and a half at once. MARE_TICK_S overrides.
#define SIM_TICK_SECONDS  15.f
#define SIM_TICK_MAX_DAYS  6.f
// Simulation speeds in days per real second, selected with keys 1-4 and the HUD buttons.
// Deliberately slow: at one day per three seconds a voyage reads as a voyage, and fleets are
// watchable rather than teleporting between ticks. Speed 4 stays a fast-forward for long waits.
#define SIM_SPEEDS { 0.3333f, 0.5f, 1.0f, 3.0f }
#define SIM_SPEED_DEFAULT 0.3333f
// Autosave (mare.c): every so many seconds of unpaused play the state is written to
// mare_autosave.bin on the IO lane; the frame that triggers it only pays the ~1 MB snapshot.
// MARE_AUTOSAVE_S overrides it for a run (0 disables).
#define AUTOSAVE_SECS 180.f
#define GUNSMOKE_DAYS 0.12f      // how long a broadside's smoke hangs around (also the "recently fired" window)

// Day and night (gfx/lighting.c). The cycle is driven by the simulated day, so it pauses with
// the game; 24 days per cycle is ~80 s of real time at the default speed, slow enough to read
// as moving light rather than a strobe. Night never goes black - this is a map that has to stay
// playable - and the sun never drops below MIN_ELEV so the shadow cascades stay well formed.
#define DAYNIGHT_PERIOD_DAYS 24.f
#define DAYNIGHT_FLOOR     0.50f   // darkest night, as a fraction of full daylight
#define DAYNIGHT_START     0.40f   // time of day at day 0 (mid-morning: a new game opens in daylight)
#define DAYNIGHT_MIN_ELEV  0.12f   // sine of the lowest sun elevation used for shadows (~7 degrees)
#define DAYNIGHT_NOON      0.62f   // time of day used when the cycle is off (the old fixed afternoon)

#endif
