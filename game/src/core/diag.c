// Diagnostics (see diag.h). Sections: state · level names / clocks · signal handlers · init and
// shutdown (run file with collision-safe name, cross-run error file with banners, optional CSV) ·
// logging (ring, files, console) · metrics · profiler (per-frame accumulation, rolling window).
// Platform code is confined to the #ifdef _WIN32 blocks at the top and in diag_now/wall_now.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // localtime/getenv/fopen are fine here (standalone test builds)
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   // clock_gettime, fileno, isatty, gettimeofday in strict -std=c11 builds
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <signal.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi")   // GetProcessMemoryInfo
#endif
#include <direct.h>
#include <io.h>
#define diag_mkdir(p) _mkdir(p)
#define diag_isatty() _isatty(_fileno(stderr))
#else
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#define diag_mkdir(p) mkdir(p, 0777)
#define diag_isatty() isatty(fileno(stderr))
#endif
#include "diag.h"

// ---------------------------------------------------------------------------
// state

static diag_config_t cfg = { "logs", LOG_WARN, LOG_DEBUG, LOG_TRACE, 0 };
static FILE *f_run, *f_err, *f_csv;
static double t_start;
static float sim_day;
static unsigned long long seq;
static int level_counts[LOG_LEVELS];
static log_record_t ring[LOG_RING]; static int ring_n, ring_head;
static metric_t metrics[METRIC_MAX]; static int nmetrics;
static prof_scope_t scopes[PROF_MAX]; static int nscopes, open_scope = -1;
static int frame_index; static double frame_t0, frame_ms, frame_hist[PROF_WINDOW];
static int initialized, console_color;
static char run_log_path[512];

static const char *level_names[LOG_LEVELS] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
const char *diag_level_name(int level) { return level >= 0 && level < LOG_LEVELS ? level_names[level] : "?"; }
int diag_level_parse(const char *s) {
    if (!s) return -1;
    for (int i = 0; i < LOG_LEVELS; ++i) { const char *n = level_names[i]; int k = 0; while (n[k] && s[k] && (n[k] == s[k] || n[k] == s[k]-32)) k++; if (!n[k] && !s[k]) return i; }
    return -1;
}

double diag_now(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq; LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}
// Resident set size of this process, in bytes (0 if unavailable). Platform code stays here with
// the rest of it; the dashboard and the metrics CSV report it alongside the GPU figures.
size_t diag_process_ram(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return (size_t)pmc.WorkingSetSize;
    return 0;
#elif defined(__APPLE__)
    struct mach_task_basic_info info; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) == KERN_SUCCESS) return (size_t)info.resident_size;
    return 0;
#else
    FILE *f = fopen("/proc/self/statm", "r");          // pages: size resident shared ...
    if (!f) return 0;
    unsigned long total = 0, resident = 0;
    int got = fscanf(f, "%lu %lu", &total, &resident);
    fclose(f);
    return got == 2 ? (size_t)resident * (size_t)sysconf(_SC_PAGESIZE) : 0;
#endif
}

static double wall_now(void) {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft); unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime; return t / 1e7 - 11644473600.0;
#else
    struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6;
#endif
}
static void wall_stamp(double wall, char *buf, int n) {   // "2026-09-01 11:21:35.123" (buf >= 32)
    time_t t = (time_t)wall; struct tm *tm = localtime(&t);
    int ms = (int)((wall - floor(wall)) * 1000);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
}

// ---------------------------------------------------------------------------
// signals: record crashes and interrupts in the error log before dying

static void on_crash(int sig) {
    signal(sig, SIG_DFL);
    const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : sig == SIGABRT ? "SIGABRT" : "signal";
    // not async-signal-safe in theory, but the process is dying anyway and this is what we want in the file
    if (f_err) { fprintf(f_err, "%08llu CRASH %s after %.3fs at sim day %.2f (see run log for the preceding records)\n", seq+1, name, initialized ? diag_now()-t_start : 0, sim_day); fprintf(f_err, "==== run crashed ====\n"); fflush(f_err); }
    if (f_run) { fprintf(f_run, "%08llu CRASH %s\n", seq+1, name); fflush(f_run); }
    raise(sig);
}
const char *diag_run_log_path(void) { return run_log_path; }
static void on_interrupt(int sig) { (void)sig; LOG_WARN("diag", "interrupted by signal %d", sig); diag_shutdown(); exit(130); }
static void install_signal_handlers(void) {
    signal(SIGSEGV, on_crash); signal(SIGFPE, on_crash); signal(SIGILL, on_crash); signal(SIGABRT, on_crash);
    signal(SIGINT, on_interrupt);
#ifdef SIGTERM
    signal(SIGTERM, on_interrupt);
#endif
}

// ---------------------------------------------------------------------------
// init / shutdown

void diag_set_sim_day(float day) { sim_day = day; }

void diag_init(const diag_config_t *c) {
    if (c) { if (c->logdir) cfg.logdir = c->logdir; cfg.console_level = c->console_level; cfg.file_level = c->file_level; cfg.ring_level = c->ring_level; cfg.metrics_csv = c->metrics_csv; }
    const char *env = getenv("MARE_LOG_LEVEL"); int lv = diag_level_parse(env); if (lv >= 0) cfg.console_level = lv;
    t_start = diag_now(); frame_t0 = t_start; seq = 0; initialized = 1;
    console_color = diag_isatty();
#ifdef _WIN32   // enable ANSI escape processing on the Windows console (Windows 10+); fall back to plain text
    if (console_color) { HANDLE h = GetStdHandle(STD_ERROR_HANDLE); DWORD mode = 0; if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) { if (!SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/)) console_color = 0; } }
#endif
    diag_mkdir(cfg.logdir);
    char stamp[32], path[512]; time_t t = time(0); struct tm *tm = localtime(&t);
    strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", tm);
    snprintf(path, sizeof path, "%s/mare_%s.log", cfg.logdir, stamp);
    for (int k = 1; k < 100; ++k) { FILE *probe = fopen(path, "r"); if (!probe) break; fclose(probe); snprintf(path, sizeof path, "%s/mare_%s_%d.log", cfg.logdir, stamp, k); }   // several runs in one second
    f_run = fopen(path, "w");
    snprintf(run_log_path, sizeof run_log_path, "%s", path); const char *run_path = run_log_path;
    snprintf(path, sizeof path, "%s/mare_errors.log", cfg.logdir);   f_err = fopen(path, "a");
    if (cfg.metrics_csv) { snprintf(path, sizeof path, "%s/mare_metrics.csv", cfg.logdir); f_csv = fopen(path, "w"); }
    if (f_err) { char w[40]; wall_stamp(wall_now(), w, sizeof w); fprintf(f_err, "==== run started %s (file %s) ====\n", w, run_path); fflush(f_err); }
    install_signal_handlers();
    if (!f_run) LOG_ERROR("diag", "cannot open run log in %s/", cfg.logdir);
    LOG_INFO("diag", "logging started: console>=%s file>=%s dir=%s", diag_level_name(cfg.console_level), diag_level_name(cfg.file_level), cfg.logdir);
}

void diag_shutdown(void) {
    if (!initialized) return;
    LOG_INFO("diag", "run summary: %llu records (trace %d debug %d info %d warn %d error %d fatal %d), %d frames, %.1fs", seq,
             level_counts[0], level_counts[1], level_counts[2], level_counts[3], level_counts[4], level_counts[5], frame_index, diag_now() - t_start);
    if (f_run) { for (int i = 0; i < nmetrics; ++i) { if (metrics[i].is_gauge) fprintf(f_run, "metric %-28s gauge   %.3f (min %.3f max %.3f)\n", metrics[i].name, metrics[i].value, metrics[i].min, metrics[i].max); else fprintf(f_run, "metric %-28s counter %.3f\n", metrics[i].name, metrics[i].value); }
                 for (int i = 0; i < nscopes; ++i) fprintf(f_run, "scope  %-28s avg %.3f ms max %.3f ms total(unframed) %.1f ms calls %d\n", scopes[i].name, scopes[i].avg_ms, scopes[i].max_ms, scopes[i].frame_ms, scopes[i].calls); }
    if (f_err) { fprintf(f_err, "==== run ended: %d warnings, %d errors, %d fatal ====\n", level_counts[LOG_WARN], level_counts[LOG_ERROR], level_counts[LOG_FATAL]); fclose(f_err); f_err = 0; }
    if (f_run) { fclose(f_run); f_run = 0; }
    if (f_csv) { fclose(f_csv); f_csv = 0; }
    initialized = 0;
}

// ---------------------------------------------------------------------------
// logging

static void write_record(FILE *fp, const log_record_t *r, int color) {
    static const char *ansi[LOG_LEVELS] = { "\x1b[90m", "\x1b[36m", "\x1b[0m", "\x1b[33m", "\x1b[31m", "\x1b[41;97m" };
    char w[40]; wall_stamp(r->wall, w, sizeof w);
    fprintf(fp, "%s%08llu %s +%9.3fs day %8.2f %-5s %-10s %-28s %s%s\n", color ? ansi[r->level] : "", r->seq, w, r->run, r->day, level_names[r->level], r->module, r->where, r->text, color ? "\x1b[0m" : "");
}

void diag_logv(int level, const char *module, const char *where, const char *fmt, va_list va) {
    if (level < 0) level = 0;
    if (level >= LOG_LEVELS) level = LOG_LEVELS-1;
    log_record_t r; memset(&r, 0, sizeof r);
    r.seq = ++seq; r.wall = wall_now(); r.run = initialized ? diag_now() - t_start : 0; r.day = sim_day; r.level = level;
    snprintf(r.module, sizeof r.module, "%s", module ? module : "-");
    const char *slash = where; for (const char *p = where; *p; ++p) if (*p == '/' || *p == '\\') slash = p + 1;   // basename only
    snprintf(r.where, sizeof r.where, "%s", slash);
    vsnprintf(r.text, sizeof r.text, fmt, va);
    level_counts[level]++;
    if (level >= cfg.ring_level) { ring[ring_head] = r; ring_head = (ring_head + 1) % LOG_RING; if (ring_n < LOG_RING) ring_n++; }
    if (f_run && level >= cfg.file_level) { write_record(f_run, &r, 0); if (level >= LOG_WARN) fflush(f_run); }
    if (f_err && level >= LOG_WARN) { write_record(f_err, &r, 0); fflush(f_err); }
    if (level >= cfg.console_level) write_record(stderr, &r, console_color);
    if (level == LOG_FATAL) { diag_shutdown(); abort(); }
}
void diag_log(int level, const char *module, const char *where, const char *fmt, ...) { va_list va; va_start(va, fmt); diag_logv(level, module, where, fmt, va); va_end(va); }

int diag_log_count(void) { return ring_n; }
const log_record_t *diag_log_get(int i) { if (i < 0 || i >= ring_n) return 0; int start = (ring_head - ring_n + LOG_RING) % LOG_RING; return &ring[(start + i) % LOG_RING]; }
unsigned long long diag_log_total(void) { return seq; }
int diag_level_count(int level) { return level >= 0 && level < LOG_LEVELS ? level_counts[level] : 0; }

// ---------------------------------------------------------------------------
// metrics

int metric_id(const char *name) {
    for (int i = 0; i < nmetrics; ++i) if (metrics[i].name == name || !strcmp(metrics[i].name, name)) return i;
    if (nmetrics >= METRIC_MAX) { LOG_WARN("diag", "metric table full, dropping '%s'", name); return METRIC_MAX-1; }
    metric_t *m = &metrics[nmetrics]; memset(m, 0, sizeof *m); m->name = name; m->min = 1e300; m->max = -1e300;
    return nmetrics++;
}
void metric_add(int id, double v) { if (id < 0 || id >= nmetrics) return; metrics[id].value += v; }
void metric_set(int id, double v) { if (id < 0 || id >= nmetrics) return; metric_t *m = &metrics[id]; m->is_gauge = 1; m->value = v; if (v < m->min) m->min = v; if (v > m->max) m->max = v; }
double metric_value(int id) { return id >= 0 && id < nmetrics ? metrics[id].value : 0; }
int metric_count(void) { return nmetrics; }
const metric_t *metric_get(int id) { return id >= 0 && id < nmetrics ? &metrics[id] : 0; }

// ---------------------------------------------------------------------------
// profiling

int prof_id(const char *name) {
    for (int i = 0; i < nscopes; ++i) if (scopes[i].name == name || !strcmp(scopes[i].name, name)) return i;
    if (nscopes >= PROF_MAX) return PROF_MAX-1;
    prof_scope_t *s = &scopes[nscopes]; memset(s, 0, sizeof *s); s->name = name; s->parent = -1;
    return nscopes++;
}
void prof_begin(int id) { if (id < 0 || id >= nscopes) return; prof_scope_t *s = &scopes[id]; s->t0 = diag_now(); if (s->parent < 0 && open_scope >= 0 && open_scope != id) s->parent = open_scope; s->calls++; open_scope = id; }
void prof_end(int id) { if (id < 0 || id >= nscopes) return; prof_scope_t *s = &scopes[id]; s->frame_ms += (diag_now() - s->t0) * 1000.0; open_scope = s->parent; }

void prof_frame_end(void) {
    double now = diag_now(); frame_ms = (now - frame_t0) * 1000.0; frame_t0 = now;
    frame_hist[frame_index % PROF_WINDOW] = frame_ms;
    for (int i = 0; i < nscopes; ++i) { prof_scope_t *s = &scopes[i];
        s->last_ms = s->frame_ms; s->history[frame_index % PROF_WINDOW] = s->frame_ms;
        int n = frame_index + 1 < PROF_WINDOW ? frame_index + 1 : PROF_WINDOW; double sum = 0, mx = 0;
        for (int k = 0; k < n; ++k) { sum += s->history[k]; if (s->history[k] > mx) mx = s->history[k]; }
        s->avg_ms = sum / n; s->max_ms = mx; s->frame_ms = 0; s->calls = 0; }
    // counter rates per second (sampled every frame)
    static double last_sample = 0; double dt = now - last_sample;
    if (dt >= 0.5) {
        for (int i = 0; i < nmetrics; ++i) { metric_t *m = &metrics[i]; if (!m->is_gauge) { m->rate = (m->value - m->prev_sample) / dt; m->prev_sample = m->value; } m->samples++; }
        last_sample = now;
        if (f_csv && now - t_start > 2.0) {   // wait for registrations to settle; re-emit a header whenever columns appear
            static int hdr_scopes = -1, hdr_metrics = -1;
            if (hdr_scopes != nscopes || hdr_metrics != nmetrics) {
                fprintf(f_csv, "%srun_s,sim_day,frame_ms", hdr_scopes < 0 ? "" : "# columns changed\n");
                for (int i = 0; i < nscopes; ++i) fprintf(f_csv, ",%s_ms", scopes[i].name); for (int i = 0; i < nmetrics; ++i) fprintf(f_csv, ",%s", metrics[i].name); fprintf(f_csv, "\n");
                hdr_scopes = nscopes; hdr_metrics = nmetrics;
            }
            fprintf(f_csv, "%.3f,%.2f,%.3f", now - t_start, sim_day, frame_ms); for (int i = 0; i < nscopes; ++i) fprintf(f_csv, ",%.3f", scopes[i].last_ms); for (int i = 0; i < nmetrics; ++i) fprintf(f_csv, ",%.3f", metrics[i].value); fprintf(f_csv, "\n");
        }
    }
    frame_index++;
}
int prof_count(void) { return nscopes; }
const prof_scope_t *prof_get(int id) { return id >= 0 && id < nscopes ? &scopes[id] : 0; }
int prof_frame_index(void) { return frame_index; }
double prof_frame_ms(void) { return frame_ms; }
const double *prof_frame_history(void) { return frame_hist; }
