// Markets and trade.
//
// Every settled port is a market: it produces goods (from its resource potential and labour
// allocation, see settlement.h), consumes them (population x per-capita coefficient, scaled by
// prosperity, region and price elasticity) and prices them from the ratio of a "comfortable"
// stock level (the norm) to the actual stock. Fleets buy and sell against that curve, paying a
// tariff to the port's owner. The daily tick also moves prosperity, unrest, population growth,
// fort regeneration, blockade expiry and taxes.
//
// All functions operate on the global game state `G` (core/types.h).

#ifndef MARE_ECONOMY_H
#define MARE_ECONOMY_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- relations & queries --------------------------------------------------------------------
int  is_player(int nation);                             // is this the human player's nation?
int  at_war(int a, int b);                              // 1 if hostile (pirates are hostile to everyone)
int  port_colonial(const port_t *p);                    // colonial regions use the "imports manufactures" demand profile
int  port_random(int min_size, int region);             // random settled port (size >= min_size, region or -1). -1 if none
int  port_random_of(int nation, int min_size);          // random port owned by `nation`. -1 if none
// nearest port to a world position. nation_or_minus1 filters by owner; settled_only skips sites;
// friendly_to (nation id or -1) skips ports at war with / blockaded against that nation
int  port_nearest(float x, float z, int nation_or_minus1, int settled_only, int friendly_to);
int  port_by_name(const char *name);                    // index or -1

// ---- market model ---------------------------------------------------------------------------
float port_consumption(const port_t *p, int g);         // units consumed per day at current prosperity/price
float port_production(const port_t *p, int g);          // units produced per day (includes hinterland staples, buildings, elasticity)
float port_norm(const port_t *p, int g);                // comfortable stock level = consumption*45d + production*30d (warehouses widen it)
float price_for_stock(const port_t *p, int g, float stock);   // price at an arbitrary stock level (for what-if planning)
void  port_update_prices(port_t *p);                    // recompute price[] from stock[]; also refreshes the per-tick norm cache
void  economy_refresh_caches(void);                     // rebuild derived caches without touching state (after loading a game)
float trade_cost(const port_t *p, int g, float qty);    // cost of buying qty units (price at mid-transaction)
float trade_revenue(const port_t *p, int g, float qty); // revenue of selling qty units
float tariff_rate(const port_t *p, int nation);         // 2% home, 3% free city, 4% foreign, 0 at pirate havens
int   can_trade(const port_t *p, int nation);           // settled, not at war with the owner, not blockaded by a third party

// ---- fleets as economic agents -----------------------------------------------------------------
float   fleet_capacity(const fleet_t *f);               // total cargo capacity (tons)
float   fleet_cargo(const fleet_t *f);                  // tons carried
int     fleet_guns(const fleet_t *f);                   // guns of ships still afloat
int     fleet_crew(const fleet_t *f);
float   fleet_speed(const fleet_t *f);                  // slowest ship; damaged ships are slower
float   fleet_value(const fleet_t *f);                  // sum of ship prices
float   fleet_upkeep_day(const fleet_t *f);             // ducats per day (crew wages)
float   fleet_hull_fraction(const fleet_t *f);          // 0..1 average hull state
double *wallet_of(fleet_t *f);                          // the player's fleets spend the treasury, AI fleets their own purse
float   trade_buy(fleet_t *f, port_t *p, int g, float qty);   // buys up to qty (limited by room, stock, money). returns units bought
float   trade_sell(fleet_t *f, port_t *p, int g, float qty);  // sells up to qty. returns net revenue after tariff
void    fleet_sell_all(fleet_t *f, port_t *p);
void    fleet_repair(fleet_t *f, float hull_per_ship);  // heal each ship by up to hull_per_ship
void    fleet_jettison(fleet_t *f);                     // drop cargo above capacity (after losing ships)

// ---- ownership -----------------------------------------------------------------------------------
void port_set_owner(port_t *p, int nation);             // changes the flag, marks the territory map dirty, recounts ports
void nations_recount_ports(void);

// ---- simulation ----------------------------------------------------------------------------------
void economy_tick(float dt);                            // daily tick of all markets + weekly price history sample
int  parsim_go(int batch);                              // should a tick phase of `batch` entities dispatch to the pool?
                                                        // (adaptive: the wake costs more than a small batch; MARE_PARSIM=0 never, 2 always)

#ifdef __cplusplus
}
#endif

#endif
