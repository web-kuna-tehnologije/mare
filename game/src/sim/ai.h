// AI.
//
// * Trade planner (ai_plan_trade): from the current port, scores every reachable market by the
//   profit per day of the best greedy cargo (several lot sizes per good, tariffs, saturation via
//   the price curve) plus 60% of the best return cargo, preferring own/free ports and avoiding
//   pirate-infested destinations. Used by AI merchants and by the player's automated fleets.
// * Per-fleet behaviour (ai_think): merchants trade, warships patrol/hunt/besiege, pirates raid
//   busy lanes and fence prizes at havens, colonists found colonies, treasure fleets sail home.
// * Nation level (ai_nations_tick): buy merchants/warships when rich, invest in port buildings,
//   settle historical sites on schedule, claim new coasts near colonies, spawn pirates at havens,
//   launch the yearly Spanish treasure fleet.

#ifndef MARE_AI_H
#define MARE_AI_H

#include "../core/types.h"

typedef struct trade_plan_t {
    int   port;                 // destination (-1 = nothing worth doing)
    float profit, score;        // expected profit of the outbound leg; score = profit per day incl. return estimate
    float buy[MAX_GOODS];       // what to buy here before leaving
} trade_plan_t;

// best destination from port `a`; if nallowed > 0 only `allowed[]` ports are considered (routes)
trade_plan_t ai_plan_trade(fleet_t *f, int a, const int *allowed, int nallowed, float max_dist);
void         ai_execute_plan(fleet_t *f, int a, const trade_plan_t *plan);   // buy, order the voyage, register the lane

void ai_think(fleet_t *f);                            // per-fleet decision; called by fleet_tick when think_timer expires
void ai_colony_found(fleet_t *f, int port);           // a colonist fleet reached a site: found the colony (player pays COLONY_COST)
void ai_treasure_arrived(fleet_t *f, port_t *p);      // treasure fleet reached its home port: silver into the treasury
void ai_nations_tick(float dt);                       // nation-level decisions (every 12-25 simulated days per nation)

// spawns (used by setup and the nation AI)
fleet_t *spawn_merchant(int nation, int port);        // 1-2 trading ships with working capital; autotrade on
fleet_t *spawn_warfleet(int nation, int port, int strength);   // `strength` warships (frigates/galleons/liners)
fleet_t *spawn_pirates(int haven, int nships);        // sloops/frigates based at a haven
void     spawn_treasure_fleet(void);                  // Spanish flota from Veracruz/Cartagena/Portobelo to Seville

#endif
