// Diagnostics: leveled logging (console + files + in-memory ring), metrics (counters,
// gauges, rates) and CPU profiling scopes with rolling statistics.
//
// Framework independent (used by the headless tests too). Thread-unsafe by design:
// the game is single threaded.
//
// Files (created under <logdir>/, default "logs"):
//   mare_<YYYYMMDD_HHMMSS>.log   every record of the run at or above the file level
//   mare_errors.log              WARN and above, appended across runs
//   mare_metrics.csv             one row per sampled frame (dev mode) for offline analysis
// Every record carries a monotonic sequence number, wall-clock time, run time, simulation
// day, level, module and source location, so files interleave unambiguously.

#ifndef MARE_DIAG_H
#define MARE_DIAG_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LOG_TRACE=0, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL, LOG_LEVELS };

typedef struct log_record_t {
    unsigned long long seq;
    double wall;           // seconds since epoch
    double run;            // seconds since diag_init
    float  day;            // simulation day
    int    level;
    char   module[16];
    char   where[48];      // file:line
    char   text[160];
} log_record_t;

typedef struct diag_config_t {
    const char *logdir;    // default "logs"
    int console_level;     // minimum level echoed to stderr (default LOG_WARN)
    int file_level;        // minimum level written to the run file (default LOG_DEBUG)
    int ring_level;        // minimum level kept in memory for the dashboard (default LOG_TRACE)
    int metrics_csv;       // write mare_metrics.csv (default 0, dev mode sets 1)
} diag_config_t;

void diag_init(const diag_config_t *cfg);        // cfg may be NULL for defaults
void diag_shutdown(void);                        // flushes, writes a summary, closes files
void diag_set_sim_day(float day);                // stamped on every record
double diag_now(void);                           // monotonic seconds
size_t diag_process_ram(void);                   // process resident set size in bytes (0 if unavailable)
const char *diag_run_log_path(void);             // path of this run's log file

// --------------------------------------------------------------------------
// logging

// emit a record (prefer the LOG_* macros, which fill `where` with file:line). FATAL flushes and aborts
void diag_logv(int level, const char *module, const char *where, const char *fmt, va_list va);
void diag_log(int level, const char *module, const char *where, const char *fmt, ...);
const char *diag_level_name(int level);         // "WARN"
int  diag_level_parse(const char *name);          // "warn" -> LOG_WARN, -1 if unknown

#define DIAG_WHERE_(f, l) f ":" #l
#define DIAG_WHERE(f, l)  DIAG_WHERE_(f, l)
#define LOGF(level, module, ...) diag_log(level, module, DIAG_WHERE(__FILE__, __LINE__), __VA_ARGS__)
#define LOG_TRACE(module, ...) LOGF(LOG_TRACE, module, __VA_ARGS__)
#define LOG_DEBUG(module, ...) LOGF(LOG_DEBUG, module, __VA_ARGS__)
#define LOG_INFO(module, ...)  LOGF(LOG_INFO,  module, __VA_ARGS__)
#define LOG_WARN(module, ...)  LOGF(LOG_WARN,  module, __VA_ARGS__)
#define LOG_ERROR(module, ...) LOGF(LOG_ERROR, module, __VA_ARGS__)
#define LOG_FATAL(module, ...) LOGF(LOG_FATAL, module, __VA_ARGS__)
#define DIAG_CHECK(cond, module, ...) do { if (!(cond)) LOG_ERROR(module, "check failed: " #cond " | " __VA_ARGS__); } while (0)

// in-memory ring (newest last)
#define LOG_RING 512
int  diag_log_count(void);                       // records currently in the ring
const log_record_t *diag_log_get(int i);         // 0 = oldest
unsigned long long diag_log_total(void);         // records ever logged
int  diag_level_count(int level);                // records ever logged at this level

// --------------------------------------------------------------------------
// metrics: counters (monotonic, with rate), gauges (last value). Registered lazily by name.

#define METRIC_MAX 128
typedef struct metric_t { const char *name; int is_gauge; double value, prev_sample, rate, min, max; int samples; } metric_t;

int    metric_id(const char *name);              // stable id, creates the metric on first use
void   metric_add(int id, double v);             // counter
void   metric_set(int id, double v);             // gauge
double metric_value(int id);
int    metric_count(void);                       // registered metrics (ids are 0..count-1)
const metric_t *metric_get(int id);
// metric_id() is a short linear search (<= METRIC_MAX strcmp); cache the id in a static at hot call sites

// --------------------------------------------------------------------------
// profiling scopes: accumulate CPU time per named scope per frame; rolling stats over frames.

#define PROF_MAX    48
#define PROF_WINDOW 240
typedef struct prof_scope_t {
    const char *name; int parent;
    double frame_ms;                 // accumulated this frame
    double last_ms, avg_ms, max_ms;  // rolling stats (window)
    double history[PROF_WINDOW];
    int calls;                       // calls this frame
    double t0;                       // open timestamp
} prof_scope_t;

int  prof_id(const char *name);                  // stable id, creates the scope on first use (linear search: cache it)
void prof_begin(int id);                         // scopes nest: the first enclosing scope becomes the parent
void prof_end(int id);
#define PROF_SCOPE_BEGIN(name) prof_begin(prof_id(name))
#define PROF_SCOPE_END(name)   prof_end(prof_id(name))
void prof_frame_end(void);              // call once per frame: rolls stats, samples metrics, writes csv
int  prof_count(void);
const prof_scope_t *prof_get(int id);           // rolling stats are valid after prof_frame_end()
int  prof_frame_index(void);            // frames since init
double prof_frame_ms(void);             // last whole frame, ms
const double *prof_frame_history(void); // PROF_WINDOW entries, ring indexed by frame index

#ifdef __cplusplus
}
#endif

#endif
