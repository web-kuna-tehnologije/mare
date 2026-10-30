// mare::core diagnostics - the C++ implementation behind the C ABI in diag.h.
//
// Single-responsibility split of the old diag.c:
//   Logger      assigns sequence numbers, counts levels, fans records out to sinks
//   LogSink     one output destination with its own minimum level (open/closed for new
//               destinations: console, run file, cross-run error file, in-memory ring)
//   Metrics     counters and gauges, registered lazily by name
//   Profiler    CPU scopes with per-frame accumulation and rolling stats; metrics CSV
//   Diagnostics facade owning all of the above; its singleton serves the extern "C" shims
//               in diag.cc so unported C modules keep calling diag_log()/metric_add()/...
//
// The record structs (log_record_t, metric_t, prof_scope_t) stay the C types from diag.h:
// they are returned by pointer across the C ABI and must not change layout.

#ifndef MARE_CORE_DIAG_HH
#define MARE_CORE_DIAG_HH

#include "diag.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace mare::core {

// Logging is called from worker-pool threads; the record path takes this spinlock (the
// critical section is a few hundred nanoseconds, a mutex would be overkill).
class SpinLock {
public:
    void lock()   { while (flag_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// --------------------------------------------------------------------------
// sinks

class LogSink {
public:
    explicit LogSink(int min_level) : min_level_(min_level) {}
    virtual ~LogSink() = default;
    bool accepts(int level) const { return level >= min_level_; }
    virtual void write(const log_record_t &r) = 0;
protected:
    int min_level_;
};

// Formats a record the way every text destination prints it (files have no color).
void write_record(FILE *fp, const log_record_t &r, bool color);

class ConsoleSink final : public LogSink {           // stderr, ANSI color when it is a tty
public:
    ConsoleSink() : LogSink(LOG_WARN) {}
    void configure(int min_level);                   // also probes tty/VT support
    void write(const log_record_t &r) override;
private:
    bool color_ = false;
};

class RunFileSink final : public LogSink {           // one file per run, collision-safe name
public:
    RunFileSink() : LogSink(LOG_DEBUG) {}
    ~RunFileSink() override { close(); }
    void open(const char *logdir, int min_level);
    void close();
    void write(const log_record_t &r) override;      // flushes at WARN and above
    bool is_open() const { return file_ != nullptr; }
    FILE *file() { return file_; }                   // crash handler and shutdown summary
    const char *path() const { return path_; }
private:
    FILE *file_ = nullptr;
    char  path_[512] = {0};
};

class ErrorFileSink final : public LogSink {         // appended across runs, with banners
public:
    ErrorFileSink() : LogSink(LOG_WARN) {}
    ~ErrorFileSink() override;
    void open(const char *logdir, const char *run_path);
    void close(const int level_counts[LOG_LEVELS]);  // writes the end banner
    void write(const log_record_t &r) override;      // always flushes
    FILE *file() { return file_; }                   // crash handler
private:
    FILE *file_ = nullptr;
};

class RingSink final : public LogSink {              // newest-last window for the dashboard
public:
    RingSink() : LogSink(LOG_TRACE) {}
    void configure(int min_level) { min_level_ = min_level; }
    void write(const log_record_t &r) override;
    int count() const { return count_; }
    const log_record_t *get(int i) const;            // 0 = oldest, NULL out of range
private:
    log_record_t ring_[LOG_RING];
    int head_ = 0, count_ = 0;
};

// --------------------------------------------------------------------------

class Logger {
public:
    // Fan a record out to every sink that accepts its level. FATAL is handled by the caller
    // (Diagnostics), which flushes and aborts after the record is safely written.
    void log(int level, const char *module, const char *where, const char *fmt, va_list va);
    void add_sink(LogSink *sink);                    // not owned; at most 4 (console/run/err/ring)
    void remove_sinks() { nsinks_ = 0; }
    unsigned long long total() const { return seq_; }
    int level_count(int level) const { return level >= 0 && level < LOG_LEVELS ? level_counts_[level] : 0; }
    const int *level_counts() const { return level_counts_; }
    // a new run restarts sequence numbers (level counts accumulate, as before)
    void begin_run(double t0) { run_start_ = t0; started_ = true; seq_ = 0; }
    void set_sim_day(float day) { sim_day_ = day; }
    float sim_day() const { return sim_day_; }
private:
    SpinLock lock_;
    LogSink *sinks_[4] = {nullptr, nullptr, nullptr, nullptr};
    int nsinks_ = 0;
    unsigned long long seq_ = 0;
    int level_counts_[LOG_LEVELS] = {0};
    double run_start_ = 0; bool started_ = false;
    float sim_day_ = 0;
};

class Metrics {
public:
    int id(const char *name);                        // stable id, creates on first use
    void add(int id, double v);                      // counter
    void set(int id, double v);                      // gauge
    double value(int id) const;
    int count() const { return nmetrics_; }
    const metric_t *get(int id) const;
    void sample_rates(double dt);                    // counter rates over the sample interval
    void write_summary(FILE *fp) const;
private:
    metric_t metrics_[METRIC_MAX] = {};
    int nmetrics_ = 0;
};

class Profiler {
public:
    explicit Profiler(Metrics &metrics) : metrics_(metrics) {}
    int id(const char *name);
    void begin(int id);                              // scopes nest; first enclosing scope becomes parent
    void end(int id);
    void frame_end(float sim_day, double run_start); // rolls stats, samples metrics, writes CSV
    int count() const { return nscopes_; }
    const prof_scope_t *get(int id) const;
    int frame_index() const { return frame_index_; }
    double frame_ms() const { return frame_ms_; }
    const double *frame_history() const { return frame_hist_; }
    void open_csv(const char *logdir);
    void close_csv();
    void reset_clock(double now) { frame_t0_ = now; }
    void write_summary(FILE *fp) const;
private:
    Metrics &metrics_;
    prof_scope_t scopes_[PROF_MAX] = {};
    int nscopes_ = 0, open_scope_ = -1;
    int frame_index_ = 0;
    double frame_t0_ = 0, frame_ms_ = 0, frame_hist_[PROF_WINDOW] = {0};
    double last_sample_ = 0;
    FILE *csv_ = nullptr;
    int csv_hdr_scopes_ = -1, csv_hdr_metrics_ = -1;
};

// --------------------------------------------------------------------------

class Diagnostics {
public:
    static Diagnostics &instance();

    void init(const diag_config_t *cfg);             // cfg may be NULL for defaults
    void shutdown();                                 // summary, banners, close files
    bool initialized() const { return initialized_; }

    void log(int level, const char *module, const char *where, const char *fmt, va_list va);

    Logger   &logger()   { return logger_; }
    Metrics  &metrics()  { return metrics_; }
    Profiler &profiler() { return profiler_; }
    RingSink &ring()     { return ring_; }

    void set_sim_day(float day) { logger_.set_sim_day(day); }
    void frame_end() { profiler_.frame_end(logger_.sim_day(), run_start_); }
    const char *run_log_path() const { return run_sink_.path(); }
    double run_start() const { return run_start_; }

    // crash/interrupt handlers write straight to the raw files before the process dies
    void write_crash_banner(const char *signame);

private:
    Diagnostics();
    diag_config_t cfg_;
    Logger        logger_;
    Metrics       metrics_;
    Profiler      profiler_;
    ConsoleSink   console_sink_;
    RunFileSink   run_sink_;
    ErrorFileSink err_sink_;
    RingSink      ring_;
    double run_start_ = 0;
    bool initialized_ = false;
};

double monotonic_now();                              // seconds, monotonic (diag_now)
double wall_now();                                   // seconds since epoch

} // namespace mare::core

#endif
