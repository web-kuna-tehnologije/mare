// Core types and the single POD simulation state `game_t`.
//
// Everything the simulation needs to resume after a save/load lives inside game_t
// (including the rng state, the scripted-history cursor and timers). Render and UI
// state live elsewhere and are rebuilt from it.
//
// Coordinate conventions
//   geographic: longitude/latitude in degrees
//   world:      x = lon * WORLD_K, z = -lat * WORLD_K, y = height (sea level = 0).
//               North is -z, east is +x. 1 world unit = 0.25 degree.
//   time:       G.day = fractional days since 1 Jan 1620.

#ifndef MARE_TYPES_H
#define MARE_TYPES_H

#include <stdint.h>

// --------------------------------------------------------------------------
// world extents. widen these when adding continents (LM_W/LM_H are asserted at startup)

#define LON0   (-130.f)
#define LON1   (  46.f)
#define LAT0   ( -58.f)
#define LAT1   (  72.f)
#define WORLD_K        4.f      // world units per degree
#define CELLS_PER_DEG  8        // land mask / heightmap resolution
#define LM_W  1408              // = (LON1-LON0)*CELLS_PER_DEG
#define LM_H  1040              // = (LAT1-LAT0)*CELLS_PER_DEG
#define NAV_DIV   2             // navigation grid = land mask / NAV_DIV
#define NAV_W  (LM_W/NAV_DIV)
#define NAV_H  (LM_H/NAV_DIV)
#define TERR_DIV  4             // territory map = land mask / TERR_DIV
#define TERR_W (LM_W/TERR_DIV)
#define TERR_H (LM_H/TERR_DIV)

// --------------------------------------------------------------------------
// limits

#define MAX_PORTS      192   // 110 historical + player/AI founded colonies
#define MAX_FLEETS     640
#define MAX_SHIPS       12      // per fleet
#define MAX_NATIONS     10
#define MAX_GOODS       17      // 12 trade goods + 5 regional rare goods (iron, coal, copper, silver, gold)
#define MAX_REGIONS    160      // land regions (hinterlands)
#define MAX_INLAND     320      // inland settlements (mines, farms, fur posts)
#define MAX_REGION_PORTS 3
#define MAX_BUILDINGS    6
#define MAX_WAYPOINTS        96      // waypoints per fleet path
#define MAX_ROUTE        6      // ports per trade route
#define MAX_EVENTS      64      // active events
#define MAX_LOG        160      // chronicle lines kept
#define MAX_LANES      256      // cached trade lanes
#define LANE_PTS        64
#define NAME_LEN        24

// --------------------------------------------------------------------------
// enums

enum { NATION_NONE=0, NATION_SPAIN, NATION_ENGLAND, NATION_FRANCE, NATION_NETHERLANDS,
       NATION_PORTUGAL, NATION_VENICE, NATION_OTTOMAN, NATION_DENMARK, NATION_PIRATES };

enum { G_GRAIN=0, G_FISH, G_TIMBER, G_SALT, G_WINE, G_CLOTH, G_ARMS, G_SUGAR, G_TOBACCO,
       G_SPICES, G_BULLION, G_FURS, G_IRON, G_COAL, G_COPPER, G_SILVER, G_GOLD };

enum { DEV_WILDERNESS=0, DEV_FRONTIER, DEV_SETTLED, DEV_DEVELOPED };
enum { INLAND_FARM=0, INLAND_MINE, INLAND_FURPOST };

enum { B_WAREHOUSE=0, B_DOCKS, B_FORT, B_MARKET, B_PLANTATION, B_MINE };

enum { REG_NORTH=0, REG_ATLANTIC, REG_MED, REG_CARIB, REG_NAMER, REG_SAMER, REG_AFRICA,
       REG_ARCTIC, REG_PACIFIC, REG_INDIAN, REG_COUNT };

enum { SHIP_SLOOP=0, SHIP_FLUYT, SHIP_MERCHANT, SHIP_FRIGATE, SHIP_GALLEON, SHIP_LINER, SHIP_COUNT };

enum { ROLE_TRADER=0, ROLE_WARSHIP, ROLE_PIRATE, ROLE_TREASURE, ROLE_COLONIST };

enum { ORDER_IDLE=0, ORDER_MOVE, ORDER_DOCK, ORDER_TRADE, ORDER_ATTACK, ORDER_PATROL,
       ORDER_SIEGE, ORDER_BLOCKADE, ORDER_COLONIZE, ORDER_FLEE, ORDER_RAID };

enum { EV_NONE=0, EV_STORM, EV_PLAGUE, EV_HARVEST, EV_FAMINE, EV_STRIKE, EV_WAR, EV_PEACE,
       EV_FASHION, EV_EMBARGO, EV_TREASURE, EV_COLONISTS, EV_PIRACY, EV_REBELLION,
       EV_CAPTURE, EV_COLONY, EV_FIRE, EV_BATTLE, EV_INFO, EV_COUNT };

// --------------------------------------------------------------------------
// static data tables (defined in data/tables.h)

typedef struct good_info_t {
    const char *name;
    float base_price;
    float consume;          // units per 1000 pop per day, baseline
    int luxury;             // 0 staple, 1 comfort, 2 luxury
    unsigned color;         // 0xAABBGGRR
} good_info_t;

typedef struct ship_info_t {
    const char *name;
    float cargo, hull, speed; int guns, crew; float cost;
} ship_info_t;

typedef struct building_info_t {
    const char *name, *desc;
    float cost;
} building_info_t;

typedef struct nation_info_t {
    const char *name, *adj;
    unsigned color;
    int ai_merchants, ai_warships;  // target fleet counts
    float tolerance;                // 0..1: how well foreign cultures are accepted under this flag
    const char *colony_prefix;      // naming of founded colonies
} nation_info_t;

// --------------------------------------------------------------------------
// dynamic state

typedef struct ship_t {
    int type;
    float hull;
} ship_t;

typedef struct fleet_t {
    int alive;
    int id;                     // unique, never reused. references between fleets use ids
    char name[NAME_LEN];
    int nation, role;
    float x, z;                 // world position
    float heading;              // radians, 0 = east, pi/2 = north
    int nships;
    ship_t ships[MAX_SHIPS];
    float cargo[MAX_GOODS];
    double money;               // own wallet (AI fleets); player fleets use the treasury
    float morale;
    int order;
    int target_port;            // DOCK/TRADE/SIEGE/BLOCKADE/COLONIZE
    int target_fleet_id;        // ATTACK (0 = none)
    float dest_x, dest_z;       // MOVE/PATROL/RAID
    int at_port;                // -1 or docked port
    float dock_timer;           // days left loading/unloading
    float path_x[MAX_WAYPOINTS], path_z[MAX_WAYPOINTS];
    int npath, pathi;
    int route[MAX_ROUTE], nroute, autotrade;
    float think_timer;
    int home_port;
    float repath_timer;
    int flee_from_id;
    float last_fire;            // sim day of last broadside (for vfx and battle logging)
} fleet_t;

typedef struct port_t {
    char name[NAME_LEN];
    float lon, lat;
    float x, z;                 // harbour (water) position
    float lx, lz;               // town (land) position
    int nation;
    int size;                   // 0 = unsettled site, 1..3
    int region;
    int preferred_nation;       // historical settler of a site
    int settle_year;
    float pop;                  // thousands
    float prosperity;           // 0.25 .. 2.2
    float unrest;               // 0 .. 1
    float stock[MAX_GOODS];
    float price[MAX_GOODS];
    float produce[MAX_GOODS];   // baseline units/day from tags
    float prod_mul[MAX_GOODS];  // event multipliers
    float cons_mul[MAX_GOODS];
    float event_timer[MAX_GOODS];
    int   building[MAX_BUILDINGS];
    float fort_hp, fort_max;
    int   blockaded_by;         // nation id or 0
    float blockade_timer;
    float traffic;              // recent visits (decays)
    double tariffs_collected;
    int   haven;                // pirate haven
    float capture_cooldown;     // days during which the AI will not besiege it again
    // settlements (colonisation expansion)
    int   founded;              // 1 = created during play (free placement), eligible for merging
    int   merged_into;          // -1, or index of the settlement that absorbed this one (entry is gone)
    float founded_day;
    float potential[MAX_GOODS]; // resource potential of the site per good (0 = none). produce[] is derived
    float labor[MAX_GOODS];     // labour allocation shares over goods with potential (sum 1)
    float culture[MAX_NATIONS]; // population (thousands) by culture/nation of origin
    float pop_cap;              // thousands the site can support
    float immigration;          // last daily immigration (thousands/day), for the UI
    int   hinterland;           // land region this port is a hub of (-1: island/no hinterland). `region` above is the sea area
    float hub_share;            // share of the region's trade flowing through this port (0..1)
    float region_in[MAX_GOODS]; // goods received from the region yesterday (units/day), for the UI
} port_t;

// a land region (hinterland) behind a stretch of coast. Connected to the world only through its hub ports
typedef struct region_t {
    char  name[NAME_LEN];
    int   alive;                 // has land cells
    float pop;                   // thousands (rural + inland towns), separate from port populations
    float prosperity;            // 0.25..2.2
    float development;           // continuous 0..3, tier = (int)
    float potential[MAX_GOODS];  // resource potential incl. rare goods
    float export_pool[MAX_GOODS];// produced, waiting for a hub
    float import_need[MAX_GOODS];// unmet daily demand (for UI and prosperity)
    float throughput;            // units/day the links to the hubs can move
    int   hub[MAX_REGION_PORTS]; int nhub;
    int   ncells;                // TERR cells
    float cx, cz;                // centroid (world)
    float traded_today;          // units moved through the links (development driver)
    float satisfaction;          // share of needs met
} region_t;

typedef struct inland_t {
    char  name[NAME_LEN];
    int   alive, region, kind, good;   // kind INLAND_*, good produced
    float x, z;                  // inland position (world)
    float pop, output;           // thousands; units/day into the region's export pool
} inland_t;

typedef struct nation_t {
    double treasury;
    int relation[MAX_NATIONS];          // 0 peace, 1 war
    float war_timer[MAX_NATIONS];       // days since the war started
    int captures_this_war[MAX_NATIONS];
    int capital;
    float ai_timer;
    double income_acc, expense_acc;     // accumulate over the current day
    double stat_income, stat_expense;   // last full day
    double income_trade, income_tax;    // lifetime
    int fleets, ports, ships_lost, ships_sunk;
} nation_t;

typedef struct event_t {
    int type;
    float day_start, day_end;
    int port, nation, good;
    float x, z, radius, vx, vz, strength;
    char text[96];
} event_t;

typedef struct logline_t {
    float day;
    int type;
    int important;              // concerns the player: show as a toast
    int nation;                 // main nation involved (-1 none)
    float x, z; int has_pos;
    char text[112];
} logline_t;

typedef struct lane_t {
    int a, b;                   // ports (a < b)
    float traffic;
    int npts; float x[LANE_PTS], z[LANE_PTS];
} lane_t;

#define SAVE_MAGIC   0x4D415245u  // 'MARE'
#define SAVE_VERSION 4

typedef struct game_t {
    unsigned magic, version, size;
    float day;                  // simulation clock
    float speed;                // days per real second, 0 = paused
    int player;                 // nation id
    unsigned seed;
    unsigned rng;               // xorshift state (part of the state: saves are deterministic)

    int nports;
    port_t ports[MAX_PORTS];
    int nfleets;                // high-water mark of used slots
    fleet_t fleets[MAX_FLEETS];
    nation_t nations[MAX_NATIONS];
    int next_fleet_id;

    int nevents;
    event_t events[MAX_EVENTS];
    int nlog;
    logline_t log[MAX_LOG];
    int nlanes;
    lane_t lanes[MAX_LANES];

    int nregions; region_t regions[MAX_REGIONS];
    int ninland;  inland_t inland[MAX_INLAND];
    int history_cursor;         // next scripted historical event
    float treasure_timer;       // days since the last treasure fleet
    float price_history[MAX_GOODS][64]; int history_i; float history_timer;
    int battles, captures, trades;
} game_t;

extern game_t G;

#endif
