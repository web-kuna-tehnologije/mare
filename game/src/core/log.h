// Chronicle: the in-game event log shown to the player (storms, battles, wars, colonies...).
// Lines live in G.log (saved with the game). UI code subscribes with log_set_listener() to show
// toasts; the simulation never depends on any UI. Every line is mirrored to the technical log
// (core/diag.h) under the "chronicle" module.

#ifndef MARE_LOG_H
#define MARE_LOG_H

#include "types.h"

typedef void (*log_listener_fn)(const logline_t *line);

void log_set_listener(log_listener_fn fn);      // called for every new line (NULL to unsubscribe)
void log_set_quiet(int quiet);                  // suppress lines (used during world setup)

// type: EV_*; nation: the nation the line concerns (-1 none); important: force a notification.
// Lines about the player's nation, wars, peace and treasure fleets are always important.
void log_event(int type, int nation, int important, const char *fmt, ...);
void log_event_at(int type, int nation, int important, float x, float z, const char *fmt, ...);   // with a map position (clickable)

int log_player_involved(int nation_a, int nation_b); // is either nation the player's?

#endif
