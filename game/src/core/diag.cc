// mare::core diagnostics implementation (see diag.hh for the class layout, diag.h for the
// C ABI the unported modules and the headless tests use). Behavior is the old diag.c's,
// bit-for-bit where files are concerned: record format, sequence numbers, banners and the
// level filtering are all pinned by tests/diag_test.c.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // localtime/getenv/fopen are fine here (standalone test builds)
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   // clock_gettime, fileno, isatty, gettimeofday in strict builds
#endif

#include "diag.hh"

#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
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
#else
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#endif

namespace mare::core {
namespace {

void make_dir(const char *p) {
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0777);
#endif
}

bool stderr_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

const char *level_names[LOG_LEVELS] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };

void wall_stamp(double wall, char *buf, int n) {   // "2026-09-01 11:21:35.123" (buf >= 32)
    time_t t = (time_t)wall; struct tm *tm = localtime(&t);
    int ms = (int)((wall - floor(wall)) * 1000);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, ms);
}

} // namespace

double monotonic_now() {
#ifdef _WIN32
    static LARGE_INTEGER freq; LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t); return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

double wall_now() {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft); unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime; return t / 1e7 - 11644473600.0;
#else
    struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6;
#endif
}

void write_record(FILE *fp, const log_record_t &r, bool color) {
    static const char *ansi[LOG_LEVELS] = { "\x1b[90m", "\x1b[36m", "\x1b[0m", "\x1b[33m", "\x1b[31m", "\x1b[41;97m" };
    char w[40]; wall_stamp(r.wall, w, sizeof w);
    fprintf(fp, "%s%08llu %s +%9.3fs day %8.2f %-5s %-10s %-28s %s%s\n", color ? ansi[r.level] : "", r.seq, w, r.run, r.day, level_names[r.level], r.module, r.where, r.text, color ? "\x1b[0m" : "");
}

// ---------------------------------------------------------------------------
// sinks

void ConsoleSink::configure(int min_level) {
    min_level_ = min_level;
    color_ = stderr_is_tty();
#ifdef _WIN32   // enable ANSI escape processing on the Windows console (Windows 10+); fall back to plain text
    if (color_) { HANDLE h = GetStdHandle(STD_ERROR_HANDLE); DWORD mode = 0; if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) { if (!SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/)) color_ = false; } }
#endif
}

void ConsoleSink::write(const log_record_t &r) { write_record(stderr, r, color_); }

void RunFileSink::open(const char *logdir, int min_level) {
    close();
    min_level_ = min_level;
    char stamp[32], path[512]; time_t t = time(0); struct tm *tm = localtime(&t);
    strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", tm);
    snprintf(path, sizeof path, "%s/mare_%s.log", logdir, stamp);
    for (int k = 1; k < 100; ++k) { FILE *probe = fopen(path, "r"); if (!probe) break; fclose(probe); snprintf(path, sizeof path, "%s/mare_%s_%d.log", logdir, stamp, k); }   // several runs in one second
    file_ = fopen(path, "w");
    snprintf(path_, sizeof path_, "%s", path);
}

void RunFileSink::close() { if (file_) { fclose(file_); file_ = nullptr; } }

void RunFileSink::write(const log_record_t &r) {
    if (!file_) return;
    write_record(file_, r, false);
    if (r.level >= LOG_WARN) fflush(file_);
}

ErrorFileSink::~ErrorFileSink() { if (file_) { fclose(file_); file_ = nullptr; } }

void ErrorFileSink::open(const char *logdir, const char *run_path) {
    if (file_) fclose(file_);
    char path[512]; snprintf(path, sizeof path, "%s/mare_errors.log", logdir);
    file_ = fopen(path, "a");
    if (file_) { char w[40]; wall_stamp(wall_now(), w, sizeof w); fprintf(file_, "==== run started %s (file %s) ====\n", w, run_path); fflush(file_); }
}

void ErrorFileSink::close(const int level_counts[LOG_LEVELS]) {
    if (!file_) return;
    fprintf(file_, "==== run ended: %d warnings, %d errors, %d fatal ====\n", level_counts[LOG_WARN], level_counts[LOG_ERROR], level_counts[LOG_FATAL]);
    fclose(file_); file_ = nullptr;
}

void ErrorFileSink::write(const log_record_t &r) {
    if (!file_) return;
    write_record(file_, r, false);
    fflush(file_);
}

void RingSink::write(const log_record_t &r) {
    ring_[head_] = r; head_ = (head_ + 1) % LOG_RING;
    if (count_ < LOG_RING) count_++;
}

const log_record_t *RingSink::get(int i) const {
    if (i < 0 || i >= count_) return nullptr;
    int start = (head_ - count_ + LOG_RING) % LOG_RING;
    return &ring_[(start + i) % LOG_RING];
}

// ---------------------------------------------------------------------------
// logger

void Logger::add_sink(LogSink *sink) {
    if (nsinks_ < (int)(sizeof sinks_ / sizeof sinks_[0])) sinks_[nsinks_++] = sink;
}

void Logger::log(int level, const char *module, const char *where, const char *fmt, va_list va) {
    if (level < 0) level = 0;
    if (level >= LOG_LEVELS) level = LOG_LEVELS-1;
    log_record_t r; memset(&r, 0, sizeof r);
    r.wall = wall_now(); r.run = started_ ? monotonic_now() - run_start_ : 0; r.day = sim_day_; r.level = level;
    snprintf(r.module, sizeof r.module, "%s", module ? module : "-");
    const char *slash = where; for (const char *p = where; *p; ++p) if (*p == '/' || *p == '\\') slash = p + 1;   // basename only
    snprintf(r.where, sizeof r.where, "%s", slash);
    vsnprintf(r.text, sizeof r.text, fmt, va);
    lock_.lock();
    r.seq = ++seq_;
    level_counts_[level]++;
    for (int i = 0; i < nsinks_; ++i) if (sinks_[i]->accepts(level)) sinks_[i]->write(r);
    lock_.unlock();
}

// ---------------------------------------------------------------------------
// metrics

int Metrics::id(const char *name) {
    for (int i = 0; i < nmetrics_; ++i) if (metrics_[i].name == name || !strcmp(metrics_[i].name, name)) return i;
    if (nmetrics_ >= METRIC_MAX) { diag_log(LOG_WARN, "diag", "diag.cc:0", "metric table full, dropping '%s'", name); return METRIC_MAX-1; }
    metric_t *m = &metrics_[nmetrics_]; memset(m, 0, sizeof *m); m->name = name; m->min = 1e300; m->max = -1e300;
    return nmetrics_++;
}
void Metrics::add(int id, double v) { if (id < 0 || id >= nmetrics_) return; metrics_[id].value += v; }
void Metrics::set(int id, double v) { if (id < 0 || id >= nmetrics_) return; metric_t *m = &metrics_[id]; m->is_gauge = 1; m->value = v; if (v < m->min) m->min = v; if (v > m->max) m->max = v; }
double Metrics::value(int id) const { return id >= 0 && id < nmetrics_ ? metrics_[id].value : 0; }
const metric_t *Metrics::get(int id) const { return id >= 0 && id < nmetrics_ ? &metrics_[id] : nullptr; }

void Metrics::sample_rates(double dt) {
    for (int i = 0; i < nmetrics_; ++i) { metric_t *m = &metrics_[i]; if (!m->is_gauge) { m->rate = (m->value - m->prev_sample) / dt; m->prev_sample = m->value; } m->samples++; }
}

void Metrics::write_summary(FILE *fp) const {
    for (int i = 0; i < nmetrics_; ++i) {
        if (metrics_[i].is_gauge) fprintf(fp, "metric %-28s gauge   %.3f (min %.3f max %.3f)\n", metrics_[i].name, metrics_[i].value, metrics_[i].min, metrics_[i].max);
        else fprintf(fp, "metric %-28s counter %.3f\n", metrics_[i].name, metrics_[i].value);
    }
}

// ---------------------------------------------------------------------------
// profiler

int Profiler::id(const char *name) {
    for (int i = 0; i < nscopes_; ++i) if (scopes_[i].name == name || !strcmp(scopes_[i].name, name)) return i;
    if (nscopes_ >= PROF_MAX) return PROF_MAX-1;
    prof_scope_t *s = &scopes_[nscopes_]; memset(s, 0, sizeof *s); s->name = name; s->parent = -1;
    return nscopes_++;
}

void Profiler::begin(int id) { if (id < 0 || id >= nscopes_) return; prof_scope_t *s = &scopes_[id]; s->t0 = monotonic_now(); if (s->parent < 0 && open_scope_ >= 0 && open_scope_ != id) s->parent = open_scope_; s->calls++; open_scope_ = id; }
void Profiler::end(int id) { if (id < 0 || id >= nscopes_) return; prof_scope_t *s = &scopes_[id]; s->frame_ms += (monotonic_now() - s->t0) * 1000.0; open_scope_ = s->parent; }

void Profiler::open_csv(const char *logdir) {
    close_csv();
    char path[512]; snprintf(path, sizeof path, "%s/mare_metrics.csv", logdir);
    csv_ = fopen(path, "w");
}
void Profiler::close_csv() { if (csv_) { fclose(csv_); csv_ = nullptr; } }

void Profiler::frame_end(float sim_day, double run_start) {
    double now = monotonic_now(); frame_ms_ = (now - frame_t0_) * 1000.0; frame_t0_ = now;
    frame_hist_[frame_index_ % PROF_WINDOW] = frame_ms_;
    for (int i = 0; i < nscopes_; ++i) { prof_scope_t *s = &scopes_[i];
        s->last_ms = s->frame_ms; s->history[frame_index_ % PROF_WINDOW] = s->frame_ms;
        int n = frame_index_ + 1 < PROF_WINDOW ? frame_index_ + 1 : PROF_WINDOW; double sum = 0, mx = 0;
        for (int k = 0; k < n; ++k) { sum += s->history[k]; if (s->history[k] > mx) mx = s->history[k]; }
        s->avg_ms = sum / n; s->max_ms = mx; s->frame_ms = 0; s->calls = 0; }
    // counter rates per second (sampled every half second)
    double dt = now - last_sample_;
    if (dt >= 0.5) {
        metrics_.sample_rates(dt);
        last_sample_ = now;
        if (csv_ && now - run_start > 2.0) {   // wait for registrations to settle; re-emit a header whenever columns appear
            if (csv_hdr_scopes_ != nscopes_ || csv_hdr_metrics_ != metrics_.count()) {
                fprintf(csv_, "%srun_s,sim_day,frame_ms", csv_hdr_scopes_ < 0 ? "" : "# columns changed\n");
                for (int i = 0; i < nscopes_; ++i) fprintf(csv_, ",%s_ms", scopes_[i].name);
                for (int i = 0; i < metrics_.count(); ++i) fprintf(csv_, ",%s", metrics_.get(i)->name);
                fprintf(csv_, "\n");
                csv_hdr_scopes_ = nscopes_; csv_hdr_metrics_ = metrics_.count();
            }
            fprintf(csv_, "%.3f,%.2f,%.3f", now - run_start, sim_day, frame_ms_);
            for (int i = 0; i < nscopes_; ++i) fprintf(csv_, ",%.3f", scopes_[i].last_ms);
            for (int i = 0; i < metrics_.count(); ++i) fprintf(csv_, ",%.3f", metrics_.get(i)->value);
            fprintf(csv_, "\n");
        }
    }
    frame_index_++;
}

const prof_scope_t *Profiler::get(int id) const { return id >= 0 && id < nscopes_ ? &scopes_[id] : nullptr; }

void Profiler::write_summary(FILE *fp) const {
    for (int i = 0; i < nscopes_; ++i) fprintf(fp, "scope  %-28s avg %.3f ms max %.3f ms total(unframed) %.1f ms calls %d\n", scopes_[i].name, scopes_[i].avg_ms, scopes_[i].max_ms, scopes_[i].frame_ms, scopes_[i].calls);
}

// ---------------------------------------------------------------------------
// signals: record crashes and interrupts in the error log before dying

namespace {

void on_crash(int sig) {
    signal(sig, SIG_DFL);
    const char *name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : sig == SIGABRT ? "SIGABRT" : "signal";
    Diagnostics::instance().write_crash_banner(name);
    raise(sig);
}

void on_interrupt(int sig) {
    diag_log(LOG_WARN, "diag", "diag.cc:0", "interrupted by signal %d", sig);
    Diagnostics::instance().shutdown();
    exit(130);
}

void install_signal_handlers() {
    signal(SIGSEGV, on_crash); signal(SIGFPE, on_crash); signal(SIGILL, on_crash); signal(SIGABRT, on_crash);
    signal(SIGINT, on_interrupt);
#ifdef SIGTERM
    signal(SIGTERM, on_interrupt);
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// facade

Diagnostics &Diagnostics::instance() {
    static Diagnostics d;
    return d;
}

Diagnostics::Diagnostics() : profiler_(metrics_) {
    cfg_.logdir = "logs"; cfg_.console_level = LOG_WARN; cfg_.file_level = LOG_DEBUG; cfg_.ring_level = LOG_TRACE; cfg_.metrics_csv = 0;
    // records logged before init() reach the console sink with the default level
    console_sink_.configure(cfg_.console_level);
    logger_.add_sink(&ring_);
    logger_.add_sink(&run_sink_);
    logger_.add_sink(&err_sink_);
    logger_.add_sink(&console_sink_);
}

void Diagnostics::init(const diag_config_t *c) {
    if (c) { if (c->logdir) cfg_.logdir = c->logdir; cfg_.console_level = c->console_level; cfg_.file_level = c->file_level; cfg_.ring_level = c->ring_level; cfg_.metrics_csv = c->metrics_csv; }
    const char *env = getenv("MARE_LOG_LEVEL"); int lv = diag_level_parse(env); if (lv >= 0) cfg_.console_level = lv;
    run_start_ = monotonic_now();
    logger_.begin_run(run_start_);
    profiler_.reset_clock(run_start_);
    initialized_ = true;
    console_sink_.configure(cfg_.console_level);
    ring_.configure(cfg_.ring_level);
    make_dir(cfg_.logdir);
    run_sink_.open(cfg_.logdir, cfg_.file_level);
    err_sink_.open(cfg_.logdir, run_sink_.path());
    if (cfg_.metrics_csv) profiler_.open_csv(cfg_.logdir);
    install_signal_handlers();
    if (!run_sink_.is_open()) LOG_ERROR("diag", "cannot open run log in %s/", cfg_.logdir);
    LOG_INFO("diag", "logging started: console>=%s file>=%s dir=%s", diag_level_name(cfg_.console_level), diag_level_name(cfg_.file_level), cfg_.logdir);
}

void Diagnostics::shutdown() {
    if (!initialized_) return;
    // the summary reports the state *before* its own record, like the old diag.c did
    unsigned long long total = logger_.total();
    const int *lc = logger_.level_counts();
    LOG_INFO("diag", "run summary: %llu records (trace %d debug %d info %d warn %d error %d fatal %d), %d frames, %.1fs", total,
             lc[0], lc[1], lc[2], lc[3], lc[4], lc[5], profiler_.frame_index(), monotonic_now() - run_start_);
    if (run_sink_.file()) {
        metrics_.write_summary(run_sink_.file());
        profiler_.write_summary(run_sink_.file());
    }
    err_sink_.close(logger_.level_counts());
    run_sink_.close();
    profiler_.close_csv();
    initialized_ = false;
}

void Diagnostics::log(int level, const char *module, const char *where, const char *fmt, va_list va) {
    logger_.log(level, module, where, fmt, va);
    if (level >= LOG_FATAL) { shutdown(); abort(); }
}

void Diagnostics::write_crash_banner(const char *signame) {
    // not async-signal-safe in theory, but the process is dying anyway and this is what we want in the file
    unsigned long long next = logger_.total() + 1;
    if (FILE *f = err_sink_.file()) { fprintf(f, "%08llu CRASH %s after %.3fs at sim day %.2f (see run log for the preceding records)\n", next, signame, initialized_ ? monotonic_now() - run_start_ : 0, logger_.sim_day()); fprintf(f, "==== run crashed ====\n"); fflush(f); }
    if (FILE *f = run_sink_.file()) { fprintf(f, "%08llu CRASH %s\n", next, signame); fflush(f); }
}

} // namespace mare::core

// ---------------------------------------------------------------------------
// C ABI (diag.h): thin shims over the Diagnostics singleton for unported C modules

using mare::core::Diagnostics;

extern "C" {

void diag_init(const diag_config_t *cfg) { Diagnostics::instance().init(cfg); }
void diag_shutdown(void) { Diagnostics::instance().shutdown(); }
void diag_set_sim_day(float day) { Diagnostics::instance().set_sim_day(day); }
double diag_now(void) { return mare::core::monotonic_now(); }
const char *diag_run_log_path(void) { return Diagnostics::instance().run_log_path(); }

const char *diag_level_name(int level) {
    static const char *names[LOG_LEVELS] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
    return level >= 0 && level < LOG_LEVELS ? names[level] : "?";
}

int diag_level_parse(const char *s) {
    if (!s) return -1;
    for (int i = 0; i < LOG_LEVELS; ++i) { const char *n = diag_level_name(i); int k = 0; while (n[k] && s[k] && (n[k] == s[k] || n[k] == s[k]-32)) k++; if (!n[k] && !s[k]) return i; }
    return -1;
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

void diag_logv(int level, const char *module, const char *where, const char *fmt, va_list va) {
    Diagnostics::instance().log(level, module, where, fmt, va);
}
void diag_log(int level, const char *module, const char *where, const char *fmt, ...) {
    va_list va; va_start(va, fmt); diag_logv(level, module, where, fmt, va); va_end(va);
}

int diag_log_count(void) { return Diagnostics::instance().ring().count(); }
const log_record_t *diag_log_get(int i) { return Diagnostics::instance().ring().get(i); }
unsigned long long diag_log_total(void) { return Diagnostics::instance().logger().total(); }
int diag_level_count(int level) { return Diagnostics::instance().logger().level_count(level); }

int    metric_id(const char *name) { return Diagnostics::instance().metrics().id(name); }
void   metric_add(int id, double v) { Diagnostics::instance().metrics().add(id, v); }
void   metric_set(int id, double v) { Diagnostics::instance().metrics().set(id, v); }
double metric_value(int id) { return Diagnostics::instance().metrics().value(id); }
int    metric_count(void) { return Diagnostics::instance().metrics().count(); }
const metric_t *metric_get(int id) { return Diagnostics::instance().metrics().get(id); }

int  prof_id(const char *name) { return Diagnostics::instance().profiler().id(name); }
void prof_begin(int id) { Diagnostics::instance().profiler().begin(id); }
void prof_end(int id) { Diagnostics::instance().profiler().end(id); }
void prof_frame_end(void) { Diagnostics::instance().frame_end(); }
int  prof_count(void) { return Diagnostics::instance().profiler().count(); }
const prof_scope_t *prof_get(int id) { return Diagnostics::instance().profiler().get(id); }
int  prof_frame_index(void) { return Diagnostics::instance().profiler().frame_index(); }
double prof_frame_ms(void) { return Diagnostics::instance().profiler().frame_ms(); }
const double *prof_frame_history(void) { return Diagnostics::instance().profiler().frame_history(); }

} // extern "C"
