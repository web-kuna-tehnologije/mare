// Events and diplomacy.
//
// Random events (storms that move and damage fleets, plague, bumper/failed harvests, silver
// strikes, fashion crazes, settler arrivals, fires, revolts, random wars) perturb supply,
// demand and politics; probabilities per day live in core/balance.h. Scripted history fires the
// wars and peaces of the century on their historical dates (the player's nation included).

#ifndef MARE_EVENTS_H
#define MARE_EVENTS_H

#include "../core/types.h"

event_t       *event_add(int type, float duration_days);   // appends an active event (oldest dropped when full)
const event_t *storm_at(float x, float z);                // storm covering this position, or NULL
void           event_spawn_storm(int region_hint);        // -1 = pick by season (hurricanes Jul-Oct, winter gales)

// diplomacy: symmetric war/peace with chronicle entry; declaring war expels fleets from now-hostile ports
void set_war(int a, int b, int war, const char *reason);  // reason may be NULL
int  ai_accepts_peace(int ai_nation, int with);           // exhausted, broke, or losing ports

void events_tick(float dt);                               // scripted history, event expiry/movement, random spawns, war timers

#endif
