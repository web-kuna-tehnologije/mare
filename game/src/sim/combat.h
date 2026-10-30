// Naval combat, sieges and blockades.
//
// combat_tick pairs every hostile couple of fleets within SIGHT_RANGE: merchants flee stronger
// armed fleets toward a friendly port; within ENGAGE_RANGE aggressive fleets (warships, pirates
// or explicit attackers) exchange broadsides (DAMAGE_PER_GUN per gun per day, scaled by morale
// and luck). Sunk ships spill cargo; captured ships join the victor. siege_tick handles a fleet
// holding station at an enemy port: trade is blocked, the fort is bombarded and, once it falls,
// stormed by a fleet with enough crew (port changes owner).

#ifndef MARE_COMBAT_H
#define MARE_COMBAT_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// apply `dmg` hull points to a random ship of `victim`. attacker may be NULL (forts, storms).
// handles sinking/capture, loot, bounties, morale, logging and fleet destruction
void fleet_take_damage(fleet_t *victim, fleet_t *attacker, float dmg);
void combat_tick(float dt);            // engagements between all hostile fleets in range
void siege_tick(fleet_t *f, float dt); // per fleet with ORDER_SIEGE/ORDER_BLOCKADE at its target port

#ifdef __cplusplus
}
#endif

#endif
