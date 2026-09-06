// Chronicle (see log.h): appends to G.log, mirrors to the technical log, notifies the listener.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "diag.h"

static log_listener_fn log_listener;
static int log_quiet;

void log_set_listener(log_listener_fn fn) { log_listener = fn; }
void log_set_quiet(int quiet) { log_quiet = quiet; }

int log_player_involved(int a, int b) { return a == G.player || b == G.player; }

static void log_push(int type, int nation, int important, float x, float z, int has_pos, const char *fmt, va_list va) {
    if (log_quiet) return;
    if (G.nlog == MAX_LOG) { memmove(G.log, G.log+1, sizeof(logline_t)*(MAX_LOG-1)); G.nlog--; }
    logline_t *l = &G.log[G.nlog++];
    l->day = G.day; l->type = type; l->nation = nation; l->x = x; l->z = z; l->has_pos = has_pos;
    l->important = important || nation == G.player || type == EV_WAR || type == EV_PEACE || type == EV_TREASURE;
    vsnprintf(l->text, sizeof l->text, fmt, va);
    diag_log(LOG_INFO, "chronicle", "log.c:0", "%s%s", l->important ? "! " : "", l->text);
    if (log_listener) log_listener(l);
}

void log_event(int type, int nation, int important, const char *fmt, ...) {
    va_list va; va_start(va, fmt); log_push(type, nation, important, 0, 0, 0, fmt, va); va_end(va);
}
void log_event_at(int type, int nation, int important, float x, float z, const char *fmt, ...) {
    va_list va; va_start(va, fmt); log_push(type, nation, important, x, z, 1, fmt, va); va_end(va);
}
