// Game session: new game, fixed-step tick, save/load. Pure simulation: no rendering, no v3.

#ifndef MARE_GAME_H
#define MARE_GAME_H

#include "../core/types.h"

void game_new(int player_nation, unsigned seed);   // rebuild G from the data tables (requires world_init + nav_init first)
void game_tick(float dt_days);                     // one fixed step (SIM_STEP): economy, settlements, events, fleets, combat, nation AI
void game_advance(float days);                     // run `days` worth of fixed steps (headless runs, --skip)
int  game_save(const char *path);                  // whole state struct with magic/version/size header. 1 on success
int  game_load(const char *path);                  // 1 on success; G is untouched on failure (bad magic/version/size)
int  game_count_fleets(void);                      // alive fleets

#endif
