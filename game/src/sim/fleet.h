// Fleets: lifecycle, orders, movement and trade lanes.
//
// A fleet is a group of up to MAX_SHIPS ships sharing position, cargo and an order. Fleets live
// in the fixed array G.fleets; slots are reused, so cross-references (attack target, camera
// follow, UI selection) must use the stable `id`, never the index. Movement follows an A* path
// (world/nav.h) at a speed modified by wind and storms. Orders are resolved on arrival by
// fleet_arrived() (internal) and by combat/siege ticks (combat.h).

#ifndef MARE_FLEET_H
#define MARE_FLEET_H

#include "../core/types.h"

// ---- lifecycle -----------------------------------------------------------------------------------
fleet_t *fleet_new(int nation, int role, float x, float z, const char *name);   // name NULL = auto. NULL if the table is full (logged)
void     fleet_add_ship(fleet_t *f, int type);          // SHIP_* at full hull; ignored when the fleet is full
void     fleet_destroy(fleet_t *f);                     // frees the slot and clears references held by other fleets
fleet_t *fleet_by_id(int id);                           // NULL if dead/unknown (ids start at 1)
int      fleet_index(const fleet_t *f);                 // slot index (transient: do not store)

// ---- orders. each computes a path first; if the destination is unreachable the fleet stays IDLE
int  fleet_set_path(fleet_t *f, float tx, float tz);    // 1 if a route exists (and is stored in path_x/z)
void fleet_undock(fleet_t *f);                          // leave the harbour (position stays at the port)
void fleet_expel(fleet_t *f);                           // kicked out of a port that became hostile: moved just offshore, idle
void fleet_order_move(fleet_t *f, float x, float z);
void fleet_order_dock(fleet_t *f, int port);            // sail to and enter a port (trade happens while docked)
void fleet_order_attack(fleet_t *f, const fleet_t *target);
void fleet_order_siege(fleet_t *f, int port, int blockade_only);   // siege bombards the fort; blockade only stops trade
void fleet_order_patrol(fleet_t *f, float x, float z);  // bounce around a point; warships engage targets of opportunity
void fleet_order_colonize(fleet_t *f, int port);        // sail to a site and found a colony on arrival (ai_colony_found)
void fleet_order_stop(fleet_t *f);                      // idle, clears route and automation

// ---- per-fleet tick: morale recovery, upkeep, AI thinking (ai.h), movement, siege -----------------
void fleet_tick(fleet_t *f, float dt);

// ---- trade lanes: the emergent route network drawn in map view --------------------------------------
void lane_register(int a, int b, const fleet_t *f);     // a fleet departs a -> b along its current path
void lanes_decay(float dt);                             // traffic fades ~1.5% per day

#endif
