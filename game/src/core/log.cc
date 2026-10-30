// mare::game::Chronicle (see log.h): appends to G.log, mirrors to the technical log,
// notifies the listener.
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
#include "log.h"
#include "diag.h"
}

namespace mare::game {

class Chronicle {
public:
    void set_listener(log_listener_fn fn) { log_listener = fn; }
    void set_quiet(int quiet) { log_quiet = quiet; }
    int  player_involved(int a, int b) { return a == G.player || b == G.player; }
    void push(int type, int nation, int important, float x, float z, int has_pos, const char *fmt, va_list va);

private:
    log_listener_fn log_listener = nullptr;
    int log_quiet = 0;
};

Chronicle &chronicle() { static Chronicle c; return c; }

void Chronicle::push(int type, int nation, int important, float x, float z, int has_pos, const char *fmt, va_list va) {
    if (log_quiet) return;
    if (G.nlog == MAX_LOG) { memmove(G.log, G.log+1, sizeof(logline_t)*(MAX_LOG-1)); G.nlog--; }
    logline_t *l = &G.log[G.nlog++];
    l->day = G.day; l->type = type; l->nation = nation; l->x = x; l->z = z; l->has_pos = has_pos;
    l->important = important || nation == G.player || type == EV_WAR || type == EV_PEACE || type == EV_TREASURE;
    vsnprintf(l->text, sizeof l->text, fmt, va);
    diag_log(LOG_INFO, "chronicle", "log.c:0", "%s%s", l->important ? "! " : "", l->text);
    if (log_listener) log_listener(l);
}

} // namespace mare::game

// C ABI (log.h) for the unported callers
using mare::game::chronicle;
extern "C" {
void log_set_listener(log_listener_fn fn) { chronicle().set_listener(fn); }
void log_set_quiet(int quiet) { chronicle().set_quiet(quiet); }
int  log_player_involved(int a, int b) { return chronicle().player_involved(a, b); }
void log_event(int type, int nation, int important, const char *fmt, ...) {
    va_list va; va_start(va, fmt); chronicle().push(type, nation, important, 0, 0, 0, fmt, va); va_end(va);
}
void log_event_at(int type, int nation, int important, float x, float z, const char *fmt, ...) {
    va_list va; va_start(va, fmt); chronicle().push(type, nation, important, x, z, 1, fmt, va); va_end(va);
}
}
