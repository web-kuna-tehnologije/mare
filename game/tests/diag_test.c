// Tests for the diagnostics core: log ordering and filtering, file outputs, metrics, profiler.
//   cc game/tests/diag_test.c -o diag_test -lm -O2 && ./diag_test

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   // must precede the first system header (clock_gettime, isatty)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/diag.h"   // implementation in ../src/core/diag.cc, linked as C++

#ifdef _WIN32
#include <direct.h>
#define diag_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define diag_mkdir(p) mkdir(p, 0777)
#endif

// portable helpers (no shell): the log directory is reused, files are removed individually
static void remove_logs(const char *dir) {
    char p[512]; snprintf(p, sizeof p, "%s/mare_errors.log", dir); remove(p); snprintf(p, sizeof p, "%s/mare_metrics.csv", dir); remove(p);
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int count_lines(const char *path, const char *needle, unsigned long long *last_seq, int *ordered) {
    FILE *fp = fopen(path, "r"); if (!fp) return -1;
    char line[512]; int n = 0; unsigned long long prev = 0; *ordered = 1;
    while (fgets(line, sizeof line, fp)) {
        if (line[0] < '0' || line[0] > '9') continue;   // banner lines
        unsigned long long seq = strtoull(line, 0, 10);
        if (seq <= prev) *ordered = 0;
        prev = seq;
        if (!needle || strstr(line, needle)) n++;
    }
    fclose(fp); *last_seq = prev; return n;
}

int main(void) {
    const char *dir = "test_diag_logs";
    diag_config_t dc = { dir, LOG_FATAL, LOG_INFO, LOG_DEBUG, 1 };   // console silent, file >= INFO, ring >= DEBUG
    diag_mkdir(dir); remove_logs(dir);
    diag_init(&dc);
    char run_path[512]; snprintf(run_path, sizeof run_path, "%s", diag_run_log_path());
    unsigned long long first = diag_log_total();
    CHECK(first == 1, "init should log exactly one record, got %llu", first);

    // levels & filtering
    LOG_TRACE("test", "trace %d", 1); LOG_DEBUG("test", "debug %d", 2); LOG_INFO("test", "info %d", 3);
    LOG_WARN("test", "warn %d", 4); LOG_ERROR("test", "error %d", 5);
    CHECK(diag_log_total() == 6, "6 records expected, got %llu", diag_log_total());
    CHECK(diag_level_count(LOG_TRACE) == 1 && diag_level_count(LOG_ERROR) == 1 && diag_level_count(LOG_WARN) == 1, "level counters wrong");
    CHECK(diag_log_count() == 5, "ring keeps >= DEBUG: expected 5 records, got %d", diag_log_count());   // trace excluded
    const log_record_t *last = diag_log_get(diag_log_count()-1);
    CHECK(last && last->level == LOG_ERROR && !strcmp(last->text, "error 5") && !strcmp(last->module, "test") && strstr(last->where, "diag_test.c:"), "last ring record wrong: %s", last ? last->text : "(null)");
    for (int i = 1; i < diag_log_count(); ++i) CHECK(diag_log_get(i)->seq > diag_log_get(i-1)->seq, "ring not ordered at %d", i);
    CHECK(diag_level_parse("warn") == LOG_WARN && diag_level_parse("ERROR") == LOG_ERROR && diag_level_parse("bogus") == -1, "level parsing");

    // ring wraps without losing order
    for (int i = 0; i < LOG_RING + 10; ++i) LOG_INFO("wrap", "line %d", i);
    CHECK(diag_log_count() == LOG_RING, "ring should be full (%d), got %d", LOG_RING, diag_log_count());
    for (int i = 1; i < diag_log_count(); ++i) CHECK(diag_log_get(i)->seq == diag_log_get(i-1)->seq + 1, "ring gap at %d", i);
    CHECK(!strcmp(diag_log_get(diag_log_count()-1)->text, "line 521"), "newest ring entry should be the last logged, got '%s'", diag_log_get(diag_log_count()-1)->text);

    // metrics
    int c = metric_id("test.counter"), g = metric_id("test.gauge");
    CHECK(metric_id("test.counter") == c, "metric ids must be stable");
    metric_add(c, 3); metric_add(c, 4); metric_set(g, 10); metric_set(g, -2); metric_set(g, 7);
    CHECK(metric_value(c) == 7, "counter value %.0f", metric_value(c));
    CHECK(metric_get(g)->is_gauge && metric_get(g)->min == -2 && metric_get(g)->max == 10 && metric_value(g) == 7, "gauge stats");
    CHECK(!metric_get(c)->is_gauge, "counter flagged as gauge");

    // profiler: nested scopes, per-frame rolling stats
    int outer = prof_id("outer"), inner = prof_id("inner");
    for (int f = 0; f < 3; ++f) {
        prof_begin(outer); prof_begin(inner);
        volatile double x = 0; for (int i = 0; i < 200000; ++i) x += i * 0.5;   // ~ms of work
        prof_end(inner); prof_end(outer);
        prof_frame_end();
    }
    CHECK(prof_get(inner)->parent == outer, "inner scope should be nested under outer");
    CHECK(prof_get(outer)->avg_ms >= prof_get(inner)->avg_ms, "outer (%.3f ms) must include inner (%.3f ms)", prof_get(outer)->avg_ms, prof_get(inner)->avg_ms);
    CHECK(prof_get(inner)->avg_ms > 0 && prof_get(inner)->max_ms >= prof_get(inner)->avg_ms, "scope stats");
    CHECK(prof_frame_index() == 3, "frame index %d", prof_frame_index());
    CHECK(prof_get(inner)->frame_ms == 0 && prof_get(inner)->calls == 0, "per-frame accumulators must reset");

    // files
    unsigned long long total_before = diag_log_total();
    diag_shutdown();
    unsigned long long seq_run, seq_err; int ord_run, ord_err;
    int n_run = count_lines(run_path, 0, &seq_run, &ord_run);
    int n_err = count_lines("test_diag_logs/mare_errors.log", 0, &seq_err, &ord_err);
    CHECK(n_run > 0, "run log missing (%s)", run_path);
    CHECK(ord_run, "run log is not in sequence order");
    CHECK(ord_err, "error log is not in sequence order");
    CHECK(n_err == 2, "error log should hold the warning and the error only, has %d lines", n_err);
    int n_trace = count_lines(run_path, " TRACE ", &seq_run, &ord_run), n_debug = count_lines(run_path, " DEBUG ", &seq_run, &ord_run);
    CHECK(n_trace == 0 && n_debug == 0, "file level INFO must exclude trace/debug (found %d/%d)", n_trace, n_debug);
    CHECK(n_run == (int)(total_before + 1) - 2, "run log should contain every record >= INFO (%d vs %llu)", n_run, total_before + 1 - 2);   // +1 summary, -2 trace/debug
    int n_csv = count_lines("test_diag_logs/mare_metrics.csv", 0, &seq_run, &ord_run);
    CHECK(n_csv >= 0, "metrics csv missing");
    { FILE *fp = fopen("test_diag_logs/mare_errors.log", "r"); char line[512]; int banner = 0; while (fp && fgets(line, sizeof line, fp)) banner += strncmp(line, "==== run", 8) == 0; if (fp) fclose(fp); CHECK(banner == 2, "error log needs start/end banners, found %d", banner); }

    // a second run appends to the error log and opens a new run file
    diag_init(&dc); char run2[512]; snprintf(run2, sizeof run2, "%s", diag_run_log_path()); LOG_ERROR("test", "second run error"); diag_shutdown();
    CHECK(strcmp(run2, run_path) != 0, "second run must get its own log file");
    n_err = count_lines("test_diag_logs/mare_errors.log", 0, &seq_err, &ord_err);
    CHECK(n_err == 3, "error log should accumulate across runs (%d)", n_err);

    remove(run_path); remove(run2); remove_logs(dir);
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
