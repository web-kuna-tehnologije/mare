// Developer dashboard (F3): frame-time graph, profiling scopes, metrics, filtered log, system info.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../sim/game.h"
#include "../gfx/camera.h"
#include "../gfx/render.h"
#include "../gfx/scene.h"

static int tab = 0;                      // 0 perf, 1 metrics, 2 log, 3 system
static int log_min_level = LOG_DEBUG, log_autoscroll = 1, log_module_filter = 0;
static const char *module_filters[] = { "all", "sim", "nav", "ai", "fleet", "combat", "gpu", "game", "chronicle", "world", "diag" };
static float perf_scale_ms = 33.3f;

static mu_Color level_color(int level) {
    switch (level) { case LOG_TRACE: return mu_color(130,130,140,255); case LOG_DEBUG: return mu_color(150,190,230,255); case LOG_INFO: return mu_color(220,220,220,255);
                     case LOG_WARN: return mu_color(255,200,90,255); case LOG_ERROR: return mu_color(255,110,90,255); default: return mu_color(255,60,60,255); }
}

static void graph(const double *hist, int n, int cursor, float scale_ms, mu_Color col, const char *caption) {
    mu_Rect r = mu_layout_next(ui_ctx);
    mu_draw_rect(ui_ctx, r, mu_color(25,25,32,255));
    int y16 = r.y + r.h - (int)(r.h * 16.7f / scale_ms), y33 = r.y + r.h - (int)(r.h * 33.3f / scale_ms);
    if (y16 > r.y) mu_draw_rect(ui_ctx, mu_rect(r.x, y16, r.w, 1), mu_color(60,120,60,255));
    if (y33 > r.y) mu_draw_rect(ui_ctx, mu_rect(r.x, y33, r.w, 1), mu_color(120,90,50,255));
    int count = cursor < n ? cursor : n;
    for (int i = 0; i < count; ++i) {
        int idx = (cursor - count + i) % n; if (idx < 0) idx += n;
        float v = (float)hist[idx] / scale_ms; if (v > 1) v = 1;
        int x = r.x + i * r.w / n, w = maxi(1, r.w / n), h = (int)(v * r.h);
        mu_draw_rect(ui_ctx, mu_rect(x, r.y + r.h - h, w, h), v >= 0.999f ? mu_color(255,80,60,255) : col);
    }
    mu_draw_text(ui_ctx, NULL, caption, -1, mu_vec2(r.x + 4, r.y + 2), mu_color(200,200,210,255));
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
    ROW(8, 75, 60, 60, 60, 75, 60, 60, -1, 18);
    mu_checkbox(ui_ctx, "terrain", &U.pass_terrain); mu_checkbox(ui_ctx, "sea", &U.pass_sea); mu_checkbox(ui_ctx, "ports", &U.pass_ports); mu_checkbox(ui_ctx, "ships", &U.pass_ships);
    mu_checkbox(ui_ctx, "overlays", &U.pass_overlays); mu_checkbox(ui_ctx, "icons", &U.pass_icons); mu_checkbox(ui_ctx, "labels", &U.pass_labels); mu_checkbox(ui_ctx, "panels", &U.pass_panels);
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("render: %d chunks, %d terrain tris | sim speed %.2f days/s | %d steps/frame budgeted", terrain_chunks_drawn, terrain_tris_drawn, G.speed, 40));

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
      mu_label(ui_ctx, va("draws %d | instances %d | tris %dk | culled %d ports %d fleets (GPU times: Perf tab)",
                          st.draws, st.instances, st.tris / 1000, scene_culled_ports, scene_culled_ships)); }
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
    mu_Container *c = mu_get_container(ui_ctx, "Dev Dashboard"); if (c->rect.w == 0) c->rect = mu_rect(20, 50, 640, 460);
    if (!mu_begin_window_ex(ui_ctx, "Dev Dashboard", c->rect, MU_OPT_NOCLOSE)) return;
    ROW(4, 120, 120, 120, -1, 22);
    static const char *tabs[4] = { "Performance", "Metrics", "Log", "System" };
    for (int i = 0; i < 4; ++i) { mu_push_id(ui_ctx, &i, sizeof i); if (ui_btn(tab == i ? va("[%s]", tabs[i]) : tabs[i])) tab = i; mu_pop_id(ui_ctx); }
    int errs = diag_level_count(LOG_ERROR) + diag_level_count(LOG_FATAL), warns = diag_level_count(LOG_WARN);
    if (errs || warns) { ROW(1, -1, 16); ui_label_col(va("%d errors, %d warnings this run (see logs/mare_errors.log)", errs, warns), errs ? mu_color(255,110,90,255) : mu_color(255,200,90,255)); }
    switch (tab) { case 0: tab_perf(); break; case 1: tab_metrics(); break; case 2: tab_log(); break; default: tab_system(); }
    mu_end_window(ui_ctx);
}
