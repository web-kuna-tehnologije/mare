// Game session: new game, fixed-step tick, save/load. Pure simulation: no rendering, no v3.

#ifndef MARE_GAME_H
#define MARE_GAME_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void game_new(int player_nation, unsigned seed);   // rebuild G from the data tables (requires world_init + nav_init first)
void game_tick(float dt_days);                     // one fixed step (SIM_STEP): economy, settlements, events, fleets, combat, nation AI
void game_advance(float days);                     // run `days` worth of fixed steps (headless runs, --skip)
int  game_save(const char *path);                  // whole state struct with magic/version/size header. 1 on success (blocking: tests, tools)
int  game_load(const char *path);                  // 1 on success; G is untouched on failure (bad magic/version/size). Blocking.
int  game_count_fleets(void);                      // alive fleets

// Asynchronous save / load (DESIGN_JOBS.md step 9), what the game itself uses. game_save_async
// copies the state into a private buffer on the calling thread (a ~1 MB memcpy, a fraction of
// a millisecond) and hands the file write to the IO lane, so the frame never waits for the
// disk; game_load_async reads and validates the file on the IO lane and game_io_poll swaps the
// state in on the main thread. One operation is in flight at a time: a save requested while a
// save is writing is queued behind it (and snapshots the state when it starts), a load while
// anything is in flight is refused. Files are written to <path>.tmp and renamed over the old
// save only once complete, so a crash mid-write never costs the previous save.
typedef enum game_io_kind_t { GAME_IO_IDLE = 0, GAME_IO_SAVE, GAME_IO_LOAD } game_io_kind_t;
typedef struct game_io_result_t {
    game_io_kind_t kind; int ok;
    char   path[256];
    double ms;              // request to completion, wall clock (includes queueing behind other IO)
    double snapshot_ms;     // save: the main-thread copy; load: applying the state and rebuilding caches
} game_io_result_t;
int  game_save_async(const char *path);            // 1 = started or queued, 0 = refused (a load is in flight)
int  game_load_async(const char *path);            // 1 = started, 0 = refused (something is in flight)
int  game_io_poll(game_io_result_t *out);          // main thread, once per frame: 1 when an operation finished this call (a finished load is applied before returning)
game_io_kind_t game_io_state(void);                // what is in flight right now
void game_io_flush(void);                          // block until nothing is in flight, polling as the main thread would (tests, shutdown)

#ifdef __cplusplus
}
#endif

#endif
