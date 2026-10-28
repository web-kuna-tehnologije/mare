// Developer dashboard (F3): frame-time graph, profiling scopes, metrics, job lanes, LOD tiers and
// residency, assets, filtered log, system info.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "ui.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../sim/game.h"
#include "../gfx/camera.h"
#include "../gfx/render.h"
#include "../gfx/scene.h"
#include "../assets/asset.h"
#include "../core/jobs.h"
#include "../gfx/lod.h"
#include "../gfx/occlusion.h"
#include "../gfx/occquery.h"

enum { TAB_PERF, TAB_METRICS, TAB_JOBS, TAB_LOD, TAB_ASSETS, TAB_LOG, TAB_SYSTEM, TAB_COUNT };
static int tab = TAB_PERF;
static int log_min_level = LOG_DEBUG, log_autoscroll = 1, log_module_filter = 0;
static const char *module_filters[] = { "all", "sim", "nav", "ai", "fleet", "combat", "gpu", "game", "chronicle", "world", "diag" };
static float perf_scale_ms = 33.3f;

static mu_Color level_color(int level) {
    switch (level) { case LOG_TRACE: return mu_color(130,130,140,255); case LOG_DEBUG: return mu_color(150,190,230,255); case LOG_INFO: return mu_color(220,220,220,255);
                     case LOG_WARN: return mu_color(255,200,90,255); case LOG_ERROR: return mu_color(255,110,90,255); default: return mu_color(255,60,60,255); }
}

static void graph_ex(const double *hist, int n, int cursor, float scale_ms, mu_Color col, const char *caption, int ms_guides) {
    mu_Rect r = mu_layout_next(ui_ctx);
    mu_draw_rect(ui_ctx, r, mu_color(25,25,32,255));
    int y16 = r.y + r.h - (int)(r.h * 16.7f / scale_ms), y33 = r.y + r.h - (int)(r.h * 33.3f / scale_ms);
    if (ms_guides && y16 > r.y) mu_draw_rect(ui_ctx, mu_rect(r.x, y16, r.w, 1), mu_color(60,120,60,255));
    if (ms_guides && y33 > r.y) mu_draw_rect(ui_ctx, mu_rect(r.x, y33, r.w, 1), mu_color(120,90,50,255));
    int count = cursor < n ? cursor : n;
    for (int i = 0; i < count; ++i) {
        int idx = (cursor - count + i) % n; if (idx < 0) idx += n;
        float v = (float)hist[idx] / scale_ms; if (v > 1) v = 1;
        int x = r.x + i * r.w / n, w = maxi(1, r.w / n), h = (int)(v * r.h);
        mu_draw_rect(ui_ctx, mu_rect(x, r.y + r.h - h, w, h), v >= 0.999f ? mu_color(255,80,60,255) : col);
    }
    mu_draw_text(ui_ctx, NULL, caption, -1, mu_vec2(r.x + 4, r.y + 2), mu_color(200,200,210,255));
}
static void graph(const double *hist, int n, int cursor, float scale_ms, mu_Color col, const char *caption) { graph_ex(hist, n, cursor, scale_ms, col, caption, 1); }
static void hbar(float share, mu_Color col) {   // a filled bar in the next layout cell
    mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255));
    mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+2, (int)(r.w * clampf(share, 0, 1)), r.h-4), col);
}

static void tab_perf(void) {
    ROW(3, 120, 120, -1, 18);
    mu_label(ui_ctx, va("frame %.2f ms", prof_frame_ms())); mu_label(ui_ctx, va("%.1f fps", fps()));
    if (ui_btn(va("scale %.0f ms", perf_scale_ms))) perf_scale_ms = perf_scale_ms >= 100 ? 16.7f : perf_scale_ms * 2;
    ROW(1, -1, 70);
    graph(prof_frame_history(), PROF_WINDOW, prof_frame_index(), perf_scale_ms, mu_color(90,170,230,255), "frame time (green line 16.7 ms, orange 33 ms)");
    ROW(5, 170, 60, 60, 60, -1, 16);
    mu_label(ui_ctx, "scope"); mu_label(ui_ctx, "last"); mu_label(ui_ctx, "avg"); mu_label(ui_ctx, "max"); mu_label(ui_ctx, "share of frame");
    double frame = maxf(0.001f, (float)prof_frame_ms());
    // tree order: roots, then their children (two levels are enough for our scopes)
    for (int root = 0; root < prof_count(); ++root) {
        if (prof_get(root)->parent >= 0) continue;
        for (int depth = 0; depth < 2; ++depth) for (int i = 0; i < prof_count(); ++i) {
            const prof_scope_t *s = prof_get(i);
            if (depth == 0 ? i != root : s->parent != root) continue;
            mu_push_id(ui_ctx, &i, sizeof i);
            ROW(5, 170, 60, 60, 60, -1, 16);
            ui_label_col(va("%s%s", depth ? "    " : "", s->name), depth ? mu_color(190,200,220,255) : mu_color(230,230,230,255));
            mu_label(ui_ctx, va("%.2f", s->last_ms)); mu_label(ui_ctx, va("%.2f", s->avg_ms)); mu_label(ui_ctx, va("%.2f", s->max_ms));
            mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255));
            float share = clampf((float)(s->avg_ms / frame), 0, 1); mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+2, (int)(r.w*share), r.h-4), depth ? mu_color(120,150,200,255) : mu_color(90,190,120,255));
            mu_pop_id(ui_ctx);
        }
    }
    { const prof_scope_t *cpu = prof_get(prof_id("frame.cpu")); ROW(5, 170, 60, 60, 60, -1, 16);
      ui_label_col("swap / gpu wait", mu_color(230,200,160,255)); mu_label(ui_ctx, va("%.2f", maxf(0, (float)(prof_frame_ms() - cpu->last_ms)))); mu_label(ui_ctx, "-"); mu_label(ui_ctx, "-"); mu_label(ui_ctx, "(frame total minus CPU scope)"); }
    ROW(1, -1, 18); mu_label(ui_ctx, "render passes (untick to measure their cost):");
    ROW(9, 75, 60, 60, 60, 75, 60, 60, 70, -1, 18);
    mu_checkbox(ui_ctx, "terrain", &U.pass_terrain); mu_checkbox(ui_ctx, "sea", &U.pass_sea); mu_checkbox(ui_ctx, "ports", &U.pass_ports); mu_checkbox(ui_ctx, "ships", &U.pass_ships);
    mu_checkbox(ui_ctx, "overlays", &U.pass_overlays); mu_checkbox(ui_ctx, "icons", &U.pass_icons); mu_checkbox(ui_ctx, "labels", &U.pass_labels); mu_checkbox(ui_ctx, "panels", &U.pass_panels);
    mu_checkbox(ui_ctx, "occlusion", &occq_enabled);
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("render: %d chunks, %d terrain tris | sim speed %.2f days/s | %d steps/frame budgeted", terrain_chunks_drawn, terrain_tris_drawn, G.speed, 40));
    ROW(1, -1, 18);
    ui_label_col(va("occlusion: GPU queries %s | %d buildings hidden, %d queried, %d answers pending, %d tracked, %d timed out%s | terrain rays hid %d ports %d fleets (%d reads)",
                    occq_active() ? "active" : occq_enabled ? "idle (far tier or map)" : "off", scene_occluded_assets, occq_queried, occq_pending, occq_tracked, occq_timeouts,
                    occq_moving ? " | camera moving: nothing culled" : "", scene_occluded_ports, scene_occluded_ships, occ_samples),
                 scene_occluded_assets ? mu_color(160,220,160,255) : mu_color(170,180,200,255));

    // --- GPU timings and memory (DESIGN_RENDERER.md step 10)
    ROW(1, -1, 18);
    ui_label_col(rhi_gpu_timers_supported() ? "GPU passes (timer queries, read back 4 frames late):"
                                            : "GPU passes: timer queries unavailable on this driver",
                 mu_color(230,200,160,255));
    if (rhi_gpu_timers_supported()) {
        float total = 0;
        for (int i = 0; i < rhi_gpu_scope_count(); ++i) total += rhi_gpu_ms(i);
        float scale = maxf(0.05f, total);
        for (int i = 0; i < rhi_gpu_scope_count(); ++i) {
            mu_push_id(ui_ctx, &i, sizeof i);
            ROW(3, 170, 60, -1, 16);
            ui_label_col(rhi_gpu_scope_name(i), mu_color(190,200,220,255));
            mu_label(ui_ctx, va("%.2f", rhi_gpu_ms(i)));
            mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255));
            mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+2, (int)(r.w * clampf(rhi_gpu_ms(i)/scale, 0, 1)), r.h-4), mu_color(200,140,90,255));
            mu_pop_id(ui_ctx);
        }
        ROW(3, 170, 60, -1, 16);
        ui_label_col("gpu total", mu_color(230,230,230,255)); mu_label(ui_ctx, va("%.2f", total));
        mu_label(ui_ctx, va("vs %.2f ms CPU frame", prof_frame_ms()));
    }
    { rhi_mem_t mem = rhi_memory(); rhi_stats_t st = rhi_frame_stats();
      double mb = 1024.0*1024.0;
      ROW(1, -1, 18);
      mu_label(ui_ctx, va("VRAM %.1f MB = textures %.1f (%d) + targets %.1f + buffers %.1f (%d)",
                          (mem.textures+mem.targets+mem.buffers)/mb, mem.textures/mb, mem.ntextures,
                          mem.targets/mb, mem.buffers/mb, mem.nbuffers));
      ROW(1, -1, 18);
      mu_label(ui_ctx, va("process RAM %.1f MB | game state %.1f MB | streamed %zu KB/frame in %d draws",
                          diag_process_ram()/mb, sizeof(G)/mb, st.upload_bytes/1024, st.draws)); }
}

static void tab_metrics(void) {
    ROW(4, 200, 110, 90, -1, 16);
    mu_label(ui_ctx, "metric"); mu_label(ui_ctx, "value"); mu_label(ui_ctx, "rate /s"); mu_label(ui_ctx, "min .. max");
    for (int i = 0; i < metric_count(); ++i) { const metric_t *m = metric_get(i); mu_push_id(ui_ctx, &i, sizeof i);
        ROW(4, 200, 110, 90, -1, 16);
        ui_label_col(m->name, m->is_gauge ? mu_color(200,220,255,255) : mu_color(255,230,180,255));
        mu_label(ui_ctx, fabs(m->value) >= 1e6 ? va("%.3g", m->value) : va("%.2f", m->value));
        mu_label(ui_ctx, m->is_gauge ? "-" : va("%.2f", m->rate));
        mu_label(ui_ctx, m->is_gauge && m->min <= m->max ? va("%.1f .. %.1f", m->min, m->max) : "-");
        mu_pop_id(ui_ctx); }
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("log records: %llu total | warn %d error %d fatal %d", diag_log_total(), diag_level_count(LOG_WARN), diag_level_count(LOG_ERROR), diag_level_count(LOG_FATAL)));
}

// task 6/7 of the asset runtime: the cache and the LOD selection, made visible
static void tab_assets(void) {
    double mb = 1024.0 * 1024.0;
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("%d registered | CPU %.1f MB (freed after upload) | GPU %.1f MB | %d instances this frame",
                        asset_count(), asset_cpu_bytes() / mb, asset_gpu_bytes() / mb, scene_asset_instances));
    ROW(3, 150, 150, -1, 22);
    if (ui_btn("LOD bias -")) asset_lod_bias = maxf(0.25f, asset_lod_bias * 0.8f);
    if (ui_btn("LOD bias +")) asset_lod_bias = minf(4.0f, asset_lod_bias * 1.25f);
    mu_label(ui_ctx, va("bias %.2f (authored switch distances x bias; <1 coarser sooner, >1 detail further)", asset_lod_bias));
    ROW(1, -1, 16);
    ui_label_col(va("%d loading | %d evicted this session | lanes and the pipeline's stages: Jobs tab; tiers, budgets and residency: LOD tab", asset_loading_count(), scene_assets_evicted), mu_color(170,180,200,255));
    ROW(6, 110, 70, 60, 70, 70, -1, 16);
    mu_label(ui_ctx, "asset"); mu_label(ui_ctx, "type"); mu_label(ui_ctx, "state"); mu_label(ui_ctx, "GPU MB"); mu_label(ui_ctx, "load ms"); mu_label(ui_ctx, "LODs: tris @ dist (instances now)");
    for (int i = 0; i < asset_count(); ++i) {
        const asset_t *a = asset_at(i);
        mu_push_id(ui_ctx, &i, sizeof i);
        ROW(6, 110, 70, 60, 70, 70, -1, 16);
        ui_label_col(a->desc.name, mu_color(220,220,230,255));
        mu_label(ui_ctx, asset_type_name(a->desc.type));
        ui_label_col(a->state == ASSET_LOADED ? "loaded" : a->state == ASSET_FAILED ? "FAILED" : a->state == ASSET_LOADING ? "loading" : "idle",
                     a->state == ASSET_FAILED ? mu_color(255,110,90,255) : a->state == ASSET_LOADING ? mu_color(240,210,120,255) : mu_color(160,220,160,255));
        mu_label(ui_ctx, va("%.1f", a->gpu_bytes / mb));
        mu_label(ui_ctx, va("%.0f", a->load_ms));
        if (a->state == ASSET_LOADED) {
            char line[256]; int o = 0;
            for (int l = 0; l < a->nlods && o < (int)sizeof line - 32; ++l)
                o += snprintf(line + o, sizeof line - o, "%s%dk@%.0f%s(%d)", l ? "  " : "", a->lod[l].nidx / 3000, a->lod[l].px > 0 ? a->lod[l].px / asset_lod_bias : asset_lod_switch_dist(a, l), a->lod[l].px > 0 ? "px" : "", scene_asset_lod_count(i, l));
            mu_label(ui_ctx, line);
        } else mu_label(ui_ctx, a->state == ASSET_FAILED ? a->error : "-");
        mu_pop_id(ui_ctx);
    }
    ROW(1, -1, 16);
    ui_label_col(va("size: %s | bounds are normalised (scale fit + pivot) at load; LOD picked per instance by camera distance",
                    asset_count() ? va("LOD0 %.2fx%.2fx%.2f", asset_at(0)->bmax[0]-asset_at(0)->bmin[0], asset_at(0)->bmax[1]-asset_at(0)->bmin[1], asset_at(0)->bmax[2]-asset_at(0)->bmin[2]) : "-"),
                 mu_color(170,180,200,255));
}

// --- Jobs tab (DESIGN_JOBS.md step 10): lane occupancy, queue depths, the asset pipeline's stages
// in flight, and the save/load state. Sampled every frame the dashboard is open; busy % and
// jobs/s are windowed over half a second so they read steadily.
#define JOBS_HIST 240
static double jobs_q_hist[JOBS_HIST]; static int jobs_q_cursor, jobs_q_peak[JOB_LANE_COUNT], jobs_q_peak_total;
static job_stats_t jobs_now, jobs_prev; static double jobs_prev_t, jobs_busy_pct[JOB_LANE_COUNT], jobs_per_s[JOB_LANE_COUNT];
static int jobs_lane_threads(const job_stats_t *js, int l) { return l == JOB_LANE_IO ? maxi(1, js->io_threads) : l == JOB_LANE_MAIN ? 1 : maxi(1, js->workers); }
static void jobs_sample(void) {
    jobs_stats(&jobs_now); double now = diag_now(); int total = 0;
    for (int l = 0; l < JOB_LANE_COUNT; ++l) { total += jobs_now.lane[l].queued; if (jobs_now.lane[l].queued > jobs_q_peak[l]) jobs_q_peak[l] = jobs_now.lane[l].queued; }
    if (total > jobs_q_peak_total) jobs_q_peak_total = total;
    jobs_q_hist[jobs_q_cursor++ % JOBS_HIST] = total;
    if (now - jobs_prev_t >= 0.5) {
        double dt = now - jobs_prev_t;
        if (jobs_prev_t > 0) for (int l = 0; l < JOB_LANE_COUNT; ++l) {
            jobs_busy_pct[l] = 100.0 * (jobs_now.lane[l].busy_ms - jobs_prev.lane[l].busy_ms) / (dt * 1000.0 * jobs_lane_threads(&jobs_now, l));
            jobs_per_s[l] = (double)(jobs_now.lane[l].executed - jobs_prev.lane[l].executed) / dt;
        }
        jobs_prev = jobs_now; jobs_prev_t = now;
    }
}
static void tab_jobs(void) {
    const job_stats_t *js = &jobs_now;
    ROW(1, -1, 18);
    mu_label(ui_ctx, js->workers ? va("pool: %d workers + %d IO threads + the render thread | %d slots | %lld jobs ran inline (full lane) | blocked in jobs_wait with nothing to help: %.1f ms total",
                                      js->workers, js->io_threads, js->slots, js->inline_runs, js->wait_ms)
                                 : va("synchronous mode (MARE_JOBS=0): every submit runs on the calling thread | %lld jobs", js->inline_runs));
    ROW(1, -1, 56);
    { float scale = (float)maxi(8, jobs_q_peak_total);
      graph_ex(jobs_q_hist, JOBS_HIST, jobs_q_cursor, scale, mu_color(120,170,230,255), va("queued jobs, all lanes (scale %d, peak %d)", (int)scale, jobs_q_peak_total), 0); }
    ROW(9, 70, 50, 40, 70, 55, 45, 60, 70, -1, 16);
    mu_label(ui_ctx, "lane"); mu_label(ui_ctx, "threads"); mu_label(ui_ctx, "busy"); mu_label(ui_ctx, "(0.5 s window)"); mu_label(ui_ctx, "queued"); mu_label(ui_ctx, "peak"); mu_label(ui_ctx, "jobs/s"); mu_label(ui_ctx, "executed"); mu_label(ui_ctx, "busy ms total");
    for (int l = 0; l < JOB_LANE_COUNT; ++l) {
        mu_push_id(ui_ctx, &l, sizeof l);
        ROW(9, 70, 50, 40, 70, 55, 45, 60, 70, -1, 16);
        ui_label_col(jobs_lane_name((job_lane_t)l), mu_color(220,220,230,255));
        mu_label(ui_ctx, l == JOB_LANE_WORKER || l == JOB_LANE_DECODE ? va("%d*", jobs_lane_threads(js, l)) : va("%d", jobs_lane_threads(js, l)));
        mu_label(ui_ctx, va("%.0f%%", jobs_busy_pct[l]));
        hbar((float)(jobs_busy_pct[l] / 100.0), jobs_busy_pct[l] > 90 ? mu_color(240,160,90,255) : mu_color(90,190,120,255));
        ui_label_col(va("%d", js->lane[l].queued), js->lane[l].queued ? mu_color(240,210,120,255) : mu_color(200,200,210,255));
        mu_label(ui_ctx, va("%d", jobs_q_peak[l])); mu_label(ui_ctx, va("%.0f", jobs_per_s[l]));
        mu_label(ui_ctx, va("%lld", js->lane[l].executed)); mu_label(ui_ctx, va("%.0f", js->lane[l].busy_ms));
        mu_pop_id(ui_ctx);
    }
    ROW(1, -1, 16);
    ui_label_col("* WORKER and DECODE share the pool: WORKER is drained first, DECODE never queues ahead of frame-critical work. MAIN runs only inside the frame's 2 ms drain.", mu_color(150,160,180,255));
    { int io, dec, com, up; asset_pipeline_counts(&io, &dec, &com, &up);
      int tl = 0, tr = 0, tf = 0; for (int i = 0; i < texture_count(); ++i) { int s = texture_state(i); tl += s == 1; tr += s == 2; tf += s == 3; }
      ROW(1, -1, 16);
      ui_label_col(va("asset pipeline: %d reading (IO) | %d parsing + decoding (DECODE) | %d awaiting commit (MAIN) | %d uploading LODs inside the budget | streamed textures: %d loading, %d resident, %d failed",
                      io, dec, com, up, tl, tr, tf), io + dec + com + up + tl ? mu_color(240,210,120,255) : mu_color(170,180,200,255)); }
    { game_io_kind_t st = game_io_state();
      ROW(1, -1, 16);
      ui_label_col(va("save/load: %s | last save %.1f ms on the IO lane, snapshot %.2f ms on the main thread | last load %.0f ms | %.0f saves this run | autosave in %.0f s",
                      st == GAME_IO_SAVE ? "SAVING" : st == GAME_IO_LOAD ? "LOADING" : "idle",
                      metric_value(metric_id("io.save_ms")), metric_value(metric_id("save.snapshot_ms")), metric_value(metric_id("io.load_ms")), metric_value(metric_id("save.count")), U.autosave_in),
                   st != GAME_IO_IDLE ? mu_color(240,210,120,255) : mu_color(170,180,200,255)); }
    ROW(3, 150, 150, -1, 22);
    if (ui_btn("Save now (F5)")) toast(game_save_async("mare_save.bin") ? "Saving..." : "Cannot save while loading");
    if (ui_btn("Autosave now")) toast(game_save_async("mare_autosave.bin") ? "Autosaving..." : "Cannot save while loading");
    mu_label(ui_ctx, "metrics: jobs.busy_pct, jobs.queued, jobs.io_queued, jobs.wait_ms, io.save_ms, save.snapshot_ms");
}

// --- LOD tab (DESIGN_LOD.md step 10): the tier and its budget, the director's response, the
// per-instance state counters, and what is resident on the GPU.
static void tab_lod(void) {
    int t = lod_tier();
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("tier %s | camera dist %.1f, mapness %.2f | screen %dx%d (thresholds are reference-1080p pixels)", lod_tier_name(t), cam.dist, cam.mapness, screen_w, screen_h));
    ROW(6, 90, 100, 100, 100, 100, -1, 16);
    mu_label(ui_ctx, "tier"); for (int i = 0; i < TIER_COUNT; ++i) ui_label_col(lod_tier_name(i), i == t ? mu_color(240,210,120,255) : mu_color(190,200,220,255));
    ROW(6, 90, 100, 100, 100, 100, -1, 16);
    mu_label(ui_ctx, "budget Mtris"); for (int i = 0; i < TIER_COUNT; ++i) ui_label_col(lod_tier_budget(i) ? va("%.1f", lod_tier_budget(i) / 1e6) : "icons only", i == t ? mu_color(240,210,120,255) : mu_color(190,200,220,255));
    { long long budget = lod_tier_budget(t);
      ROW(3, 200, 60, -1, 16);
      ui_label_col(va("asset triangles %.2fM of %.2fM", lod_last_tris / 1e6, budget / 1e6), mu_color(220,220,230,255));
      mu_label(ui_ctx, budget ? va("%.0f%%", 100.0 * lod_last_tris / budget) : "-");
      hbar(budget ? (float)((double)lod_last_tris / budget) : 0.f, lod_last_tris > budget ? mu_color(240,160,90,255) : mu_color(90,190,120,255));
      ROW(1, -1, 16);
      ui_label_col(va("director threshold scale x%.2f %s | hysteresis +-%.0f%% | fade %.2f s", lod_budget_scale, lod_budget_scale > 1.01f ? "(over budget: the smallest instances hand over a level sooner)" : "(within budget)", LOD_HYST * 100, LOD_FADE_S),
                   lod_budget_scale > 1.01f ? mu_color(240,210,120,255) : mu_color(170,180,200,255)); }
    ROW(1, -1, 16);
    ui_label_col(va("instances: %d tracked, %d fading, pops %d (must stay 0) | %d markers clustered | culled %d ports %d fleets | %d authored instances drawn",
                    lod_tracked, lod_fading, lod_pops, scene_icons_clustered, scene_culled_ports, scene_culled_ships, scene_asset_instances),
                 lod_pops ? mu_color(255,110,90,255) : mu_color(170,180,200,255));
    ROW(3, 120, 120, -1, 22);
    if (ui_btn("LOD bias -")) asset_lod_bias = maxf(0.25f, asset_lod_bias * 0.8f);
    if (ui_btn("LOD bias +")) asset_lod_bias = minf(4.0f, asset_lod_bias * 1.25f);
    mu_label(ui_ctx, va("bias %.2f (<1 coarser sooner, >1 detail further; MARE_LOD_BIAS pins it)", asset_lod_bias));
    { double mb = 1024.0 * 1024.0; size_t budget = scene_asset_gpu_budget();
      ROW(3, 200, 60, -1, 16);
      ui_label_col(va("residency: GPU %.1f MB of %.0f MB", asset_gpu_bytes() / mb, budget / mb), mu_color(220,220,230,255));
      mu_label(ui_ctx, va("%.0f%%", 100.0 * asset_gpu_bytes() / budget));
      hbar((float)((double)asset_gpu_bytes() / budget), asset_gpu_bytes() > budget ? mu_color(240,160,90,255) : mu_color(90,190,120,255));
      ROW(1, -1, 16);
      ui_label_col(va("%d assets evicted this session (coldest first, only past the regional tier, never a stall) | %d loading now | CPU copies %.1f MB", scene_assets_evicted, asset_loading_count(), asset_cpu_bytes() / mb), mu_color(170,180,200,255));
      ROW(6, 120, 60, 60, 90, 60, -1, 16);
      mu_label(ui_ctx, "asset"); mu_label(ui_ctx, "state"); mu_label(ui_ctx, "GPU MB"); mu_label(ui_ctx, "LODs on GPU"); mu_label(ui_ctx, "idle s"); mu_label(ui_ctx, "instances now per LOD");
      for (int i = 0; i < asset_count(); ++i) {
          const asset_t *a = asset_at(i); mu_push_id(ui_ctx, &i, sizeof i);
          ROW(6, 120, 60, 60, 90, 60, -1, 16);
          ui_label_col(a->desc.name, mu_color(220,220,230,255));
          ui_label_col(a->state == ASSET_LOADED ? "resident" : a->state == ASSET_LOADING ? (a->stage == 1 ? "IO" : a->stage == 2 ? "DECODE" : "MAIN") : a->state == ASSET_FAILED ? "FAILED" : "evicted",
                       a->state == ASSET_LOADED ? mu_color(160,220,160,255) : a->state == ASSET_FAILED ? mu_color(255,110,90,255) : mu_color(240,210,120,255));
          mu_label(ui_ctx, va("%.1f", a->gpu_bytes / mb));
          { char res[16] = ""; int o = 0; for (int l = 0; l < a->nlods && o < 12; ++l) o += snprintf(res + o, sizeof res - o, "%s%c", l ? " " : "", a->lod[l].gpu ? '0' + l : '-'); mu_label(ui_ctx, a->nlods ? res : "-"); }
          { float idle = scene_asset_idle_s(i); mu_label(ui_ctx, idle < 0 ? "never" : va("%.0f", idle)); }
          { char line[64]; int o = 0; for (int l = 0; l < a->nlods && o < 56; ++l) o += snprintf(line + o, sizeof line - o, "%s%d", l ? " / " : "", scene_asset_lod_count(i, l)); mu_label(ui_ctx, a->nlods ? line : "-"); }
          mu_pop_id(ui_ctx);
      }
      if (texture_count()) {
          ROW(3, 320, 70, -1, 16); mu_label(ui_ctx, "streamed texture"); mu_label(ui_ctx, "state"); mu_label(ui_ctx, "size");
          for (int i = 0; i < texture_count(); ++i) { int w, h; texture_size(i, &w, &h); mu_push_id(ui_ctx, &i, sizeof i);
              ROW(3, 320, 70, -1, 16); const char *p = texture_path(i); const char *base = strrchr(p, '/'); base = base ? base + 1 : p;
              ui_label_col(base, mu_color(220,220,230,255));
              ui_label_col(texture_state(i) == 2 ? "resident" : texture_state(i) == 1 ? "loading" : "FAILED", texture_state(i) == 2 ? mu_color(160,220,160,255) : texture_state(i) == 1 ? mu_color(240,210,120,255) : mu_color(255,110,90,255));
              mu_label(ui_ctx, w ? va("%dx%d", w, h) : "-"); mu_pop_id(ui_ctx); }
      } }
}

static void tab_log(void) {
    ROW(5, 100, 110, 130, 110, -1, 20);
    if (ui_btn(va(">= %s", diag_level_name(log_min_level)))) log_min_level = (log_min_level + 1) % LOG_LEVELS;
    if (ui_btn(va("module: %s", module_filters[log_module_filter]))) log_module_filter = (log_module_filter + 1) % (int)(sizeof module_filters / sizeof module_filters[0]);
    mu_checkbox(ui_ctx, "autoscroll", &log_autoscroll);
    mu_label(ui_ctx, va("%d in ring", diag_log_count()));
    mu_label(ui_ctx, "files: logs/mare_<date>.log, logs/mare_errors.log");
    ROW(1, -1, -1);   // fill the rest of the window
    mu_begin_panel(ui_ctx, "logpanel");
    int shown = 0;
    for (int i = 0; i < diag_log_count(); ++i) { const log_record_t *r = diag_log_get(i);
        if (r->level < log_min_level) continue;
        if (log_module_filter && strcmp(r->module, module_filters[log_module_filter])) continue;
        mu_push_id(ui_ctx, &r->seq, sizeof r->seq);
        ROW(4, 60, 75, 70, -1, 15);
        ui_label_col(va("%llu", r->seq), mu_color(120,120,130,255));
        ui_label_col(va("%8.2f", r->day), mu_color(160,160,170,255));
        ui_label_col(diag_level_name(r->level), level_color(r->level));
        ui_label_col(va("[%s] %s", r->module, r->text), level_color(r->level));
        mu_pop_id(ui_ctx); shown++; }
    { mu_Container *c = mu_get_current_container(ui_ctx);   // autoscroll only while the view is at the bottom; scrolling up pauses it
      static int last_content = 0; int at_bottom = c->scroll.y >= last_content - c->body.h - 4;
      if (log_autoscroll && at_bottom) c->scroll.y = c->content_size.y;
      last_content = c->content_size.y; }
    mu_end_panel(ui_ctx);
    (void)shown;
}

static void tab_system(void) {
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("renderer: %s", rhi_info()));
    mu_label(ui_ctx, va("window %dx%d | camera dist %.0f pitch %.0f mapness %.2f", screen_w, screen_h, cam.dist, cam.pitch*57.3f, cam.mapness));
    mu_label(ui_ctx, va("world %dx%d cells, nav %dx%d, %d ports, fleets %d (slots %d/%d)", LM_W, LM_H, NAV_W, NAV_H, G.nports, game_count_fleets(), G.nfleets, MAX_FLEETS));
    mu_label(ui_ctx, va("state struct %u bytes | sim day %.2f | seed %u | rng %08x", (unsigned)sizeof G, G.day, G.seed, G.rng));
    mu_label(ui_ctx, va("build %s %s | save v%u", __DATE__, __TIME__, (unsigned)SAVE_VERSION));
    mu_label(ui_ctx, va("HDR RGBA16F, linear/sRGB, PBR | exposure %.2f | shadows %s (%d casters)",
                        hdr_exposure, shadow_enabled ? "on" : "off", shadow_casters_drawn));
    { rhi_stats_t st = rhi_frame_stats();
      mu_label(ui_ctx, va("draws %d | instances %d | tris %dk | frustum-culled %d ports %d fleets | occluded %d buildings (GPU times: Perf tab)",
                          st.draws, st.instances, st.tris / 1000, scene_culled_ports, scene_culled_ships, scene_occluded_assets)); }
    ROW(3, 150, 150, -1, 22);
    if (ui_btn(shadow_enabled ? "Shadows: on" : "Shadows: off")) shadow_enabled = !shadow_enabled;
    if (ui_btn("Exposure -")) hdr_exposure = maxf(0.25f, hdr_exposure - 0.1f);
    if (ui_btn("Exposure +")) hdr_exposure = minf(4.0f, hdr_exposure + 0.1f);
    ROW(3, 150, 150, -1, 22);
    if (ui_btn("Log test warning")) LOG_WARN("dashboard", "test warning from the dashboard");
    if (ui_btn("Log test error")) LOG_ERROR("dashboard", "test error from the dashboard");
    if (ui_btn("GPU error check")) { int n = rhi_check_errors("dashboard"); toast("%d GPU errors", n); }
}

void dashboard_panel(void) {
    if (!U.show_debug) return;
    // MARE_DASHTAB=performance|metrics|jobs|lod|assets|log|system opens the dashboard on that
    // tab (unattended runs can then show any tab without input)
    static const char *names[TAB_COUNT] = { "performance", "metrics", "jobs", "lod", "assets", "log", "system" };
    static const char *tabs[TAB_COUNT]  = { "Performance", "Metrics", "Jobs", "LOD", "Assets", "Log", "System" };
    static int tab_env_read;
    if (!tab_env_read) { tab_env_read = 1; const char *t = getenv("MARE_DASHTAB");
        if (t) for (int i = 0; i < TAB_COUNT; ++i) if (!strcmp(t, names[i])) tab = i; }
    jobs_sample();   // keep the queue history and the windowed lane rates continuous whichever tab is up
    mu_Container *c = mu_get_container(ui_ctx, "Dev Dashboard"); if (c->rect.w == 0) c->rect = mu_rect(20, 50, 700, 480);
    if (!mu_begin_window_ex(ui_ctx, "Dev Dashboard", c->rect, MU_OPT_NOCLOSE)) return;
    ROW(7, 100, 70, 55, 50, 65, 50, -1, 22);
    for (int i = 0; i < TAB_COUNT; ++i) { mu_push_id(ui_ctx, &i, sizeof i); if (ui_btn(tab == i ? va("[%s]", tabs[i]) : tabs[i])) tab = i; mu_pop_id(ui_ctx); }
    int errs = diag_level_count(LOG_ERROR) + diag_level_count(LOG_FATAL), warns = diag_level_count(LOG_WARN);
    if (errs || warns) { ROW(1, -1, 16); ui_label_col(va("%d errors, %d warnings this run (see logs/mare_errors.log)", errs, warns), errs ? mu_color(255,110,90,255) : mu_color(255,200,90,255)); }
    switch (tab) { case TAB_PERF: tab_perf(); break; case TAB_METRICS: tab_metrics(); break; case TAB_JOBS: tab_jobs(); break; case TAB_LOD: tab_lod(); break;
                   case TAB_ASSETS: tab_assets(); break; case TAB_LOG: tab_log(); break; default: tab_system(); }
    mu_end_window(ui_ctx);
}
