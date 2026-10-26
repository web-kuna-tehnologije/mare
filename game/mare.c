// MARE LIBERUM - 17th century naval trade & war RTS on the v3 framework.
// Unity build: this file pulls in every module except the job system, which is the one C++
// translation unit and is compiled and linked alongside it (game/docs/DESIGN_JOBS.md).
// See README.md for the layout and game/build.sh / build.bat for the canonical builds.
//
//   linux:   cc -c game/mare.c -I. -O2 && c++ -std=c++17 -c game/src/core/jobs.cpp -O2 &&
//            c++ mare.o jobs.o -o mare -lm -ldl -lpthread -lasound
//   windows: cl /std:c11 /std:c++17 game\mare.c game\src\core\jobs.cpp -I. /O2 /Fe:mare.exe
//   osx:     cc -ObjC -c game/mare.c -I. -O2 && c++ -std=c++17 -c game/src/core/jobs.cpp -O2 &&
//            c++ mare.o jobs.o -o mare -framework cocoa -framework iokit -framework audiotoolbox

#define V3_IMPLEMENTATION
#include "v3.h"

#include "src/core/jobs.h"   // C ABI over src/core/jobs.cpp (linked separately)

// simulation (framework independent)
#include "src/core/diag.c"
#include "src/core/log.c"
#include "src/world/world.c"
#include "src/world/nav.c"
#include "src/world/wind.c"
#include "src/world/regions.c"
#include "src/sim/economy.c"
#include "src/sim/events.c"
#include "src/sim/fleet.c"
#include "src/sim/ai.c"
#include "src/sim/combat.c"
#include "src/sim/settlement.c"
#include "src/sim/region.c"
#include "src/sim/game.c"
// asset runtime (CPU side, no GL: also exercised by the headless asset_test)
#include "src/assets/gltf.c"
#include "src/assets/asset.c"
// presentation (v3: GL, microui, input). rhi_gl.c is the active RHI backend (rhi.h): all
// game rendering goes through it; swap this include to target another graphics API.
#include "src/gfx/rhi_gl.c"
#include "src/gfx/gpu.c"
#include "src/gfx/detail.c"
#include "src/gfx/terrain.c"
#include "src/gfx/sea.c"
#include "src/gfx/meshes.c"
#include "src/gfx/icons.c"
#include "src/gfx/hdr.c"
#include "src/gfx/lighting.c"
#include "src/gfx/camera.c"
#include "src/gfx/lod.c"
#include "src/gfx/textures.c"
#include "src/gfx/scene.c"
#include "src/gfx/detailview.c"
#include "src/ui/uikit.c"
#include "src/ui/input.c"
#include "src/ui/hud.c"
#include "src/ui/dashboard.c"

// How long the render thread may spend per frame on work the pool handed back to it (GPU
// uploads). At 60 Hz the frame has ~16 ms: 2 ms of uploads is invisible, and anything that does
// not fit simply lands in the next frame (see jobs_main_drain).
#define JOBS_UPLOAD_BUDGET_MS 2.0

static void print_report(int days) {
    printf("simulated %d days: %d log lines, %d lanes, %d fleets, %d trades, %d battles, %d captures\n", days, G.nlog, G.nlanes, game_count_fleets(), G.trades, G.battles, G.captures);
    for (int i = 0; i < G.nlog; ++i) printf("  %s: %s\n", ui_date(G.log[i].day), G.log[i].text);
    for (int r = 0; r < G.nregions; ++r) {
        const region_t *R = &G.regions[r];
        int inland_count = 0;
        for (int i = 0; i < G.ninland; ++i)
            inland_count += G.inland[i].alive && G.inland[i].region == r;
        if (R->nhub && (R->pop > 40 || r % 9 == 0))
            printf("  region %-22s dev %.2f pop %6.0fk prosp %.2f sat %.2f link %.0f hubs %d inland %d\n", R->name, R->development, R->pop, R->prosperity, R->satisfaction, R->throughput, R->nhub, inland_count);
    }
    for (int n = 0; n < MAX_NATIONS; ++n) printf("  %-22s treasury %9.0f ports %2d fleets %2d sunk %3d lost %3d  tax %.0f tariffs %.0f\n", nations_info[n].name, G.nations[n].treasury, G.nations[n].ports, G.nations[n].fleets, G.nations[n].ships_sunk, G.nations[n].ships_lost, G.nations[n].income_tax, G.nations[n].income_trade);
    fflush(stdout);
}

// MARE_SCREENSHOT=path.ppm: capture the backbuffer (tonemapped scene, pre-UI) once, shortly
// before MARE_AUTOQUIT_S expires. Smoke tests read it back to verify the render output.
static void screenshot_ppm(const char *path) {
    int w = screen_w, h = screen_h;
    unsigned char *px = malloc((size_t)w * h * 4);
    rhi_read_pixels(0, 0, w, h, px);
    FILE *f = fopen(path, "wb");
    if (!f) { LOG_ERROR("main", "screenshot: cannot write %s", path); free(px); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) for (int x = 0; x < w; ++x) fwrite(px + (y*(size_t)w + x)*4, 1, 3, f);   // GL rows are bottom-up
    fclose(f); free(px);
    LOG_INFO("main", "screenshot saved: %s (%dx%d)", path, w, h);
}

static void debug_keys(void) {
    if (!U.show_debug) return;
    if (KDOWN(GLFW_KEY_F6)) { event_t *e = event_add(EV_STORM, 10); e->x = cam.tx; e->z = cam.tz; e->radius = 12; e->vx = 0.5f; e->strength = 1; snprintf(e->text, sizeof e->text, "Debug storm"); toast("debug: storm spawned"); }
    if (KDOWN(GLFW_KEY_F7)) { float x = cam.tx, z = cam.tz; if (nav_snap(&x, &z, 20)) { fleet_t *f = fleet_new(NATION_PIRATES, ROLE_PIRATE, x, z, 0); if (f) { fleet_add_ship(f, SHIP_FRIGATE); fleet_add_ship(f, SHIP_SLOOP); f->home_port = port_nearest(x, z, NATION_PIRATES, 1, -1); toast("debug: pirates spawned"); } } }
    if (KDOWN(GLFW_KEY_F8)) { G.nations[G.player].treasury += 100000; toast("debug: +100000 ducats"); }
}

int main(int argc, char **argv) {
    unsigned seed = (unsigned)time(0); int autostart = 0, headless = 0, skip = 0, dev = 0;
    diag_config_t dc = { "logs", LOG_WARN, LOG_DEBUG, LOG_TRACE, 0 };
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--seed=", 7)) seed = (unsigned)atoi(argv[i]+7);
        else if (!strncmp(argv[i], "--nation=", 9)) autostart = atoi(argv[i]+9);
        else if (!strncmp(argv[i], "--simulate=", 11)) headless = atoi(argv[i]+11);
        else if (!strncmp(argv[i], "--skip=", 7)) skip = atoi(argv[i]+7);
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--no-shadows")) shadow_enabled = 0;
        else if (!strncmp(argv[i], "--log-level=", 12)) { int l = diag_level_parse(argv[i]+12); if (l >= 0) dc.console_level = l; }
        else if (!strncmp(argv[i], "--log-dir=", 10)) dc.logdir = argv[i]+10;
        else if (strncmp(argv[i], "--fps", 5)) fprintf(stderr, "unknown option %s\n", argv[i]);
    }
    if (dev) { dc.metrics_csv = 1; dc.file_level = LOG_TRACE; }
    diag_init(&dc); atexit(diag_shutdown);
    // The pool starts before anything that could submit to it. Registered after diag_shutdown so
    // it runs first at exit, while logging still works. MARE_JOBS=0 forces synchronous mode.
    { int nw = jobs_init(-1); atexit(jobs_shutdown);
      LOG_INFO("jobs", "%s: %d worker threads, %d slots", nw ? "job system" : "job system (synchronous)", nw, jobs_slot_count()); }
    LOG_INFO("main", "mare liberum starting: seed %u nation %d dev %d headless %d", seed, autostart, dev, headless);
    if (headless) {   // balance run without a window
        world_init(seed); nav_init(); game_new(autostart ? autostart : NATION_NETHERLANDS, seed);
        double t0 = diag_now(); game_advance((float)headless);
        LOG_INFO("main", "simulated %d days in %.2f s (%.1f ms per day)", headless, diag_now()-t0, (diag_now()-t0)*1000/headless);
        print_report(headless);
        return 0;
    }

    app_create(.title = "Mare Liberum", .w = 1600, .h = 900, .scale = 0.92f);
    { app_state_t st = app_state(); screen_w = maxi(1, st.w); screen_h = maxi(1, st.h); }
    // v3 caps the frame rate to the monitor refresh rate + 2. Virtual displays (VMs, Xvfb, some
    // remote desktops) report 0 Hz, which would lock the game to 2 FPS: fall back to uncapped.
    { float hz = monitor_hz(0); if (hz < 10 && !flag("--fps")) { vsync(0); LOG_WARN("main", "monitor reports %.0f Hz: frame limiter disabled (use --fps=N to cap)", hz); } else LOG_INFO("main", "monitor %.0f Hz", hz); }
    LOG_INFO("gpu", "%s", rhi_info());
    world_init(seed); nav_init();
    if (getenv("MARE_WAVES")) { sea_wave_amp = clampf((float)atof(getenv("MARE_WAVES")), 0.f, 4.f);
        LOG_INFO("gpu", "wave height pinned to %.2f (MARE_WAVES)", sea_wave_amp); }
    if (getenv("MARE_LOD_BIAS")) { asset_lod_bias = clampf((float)atof(getenv("MARE_LOD_BIAS")), 0.01f, 16.f);   // pin the LOD knob for screenshot runs
        LOG_INFO("asset", "LOD bias pinned to %.2f (MARE_LOD_BIAS)", asset_lod_bias); }
    { char manifest[512]; snprintf(manifest, sizeof manifest, "%s/manifest.json", asset_root());   // authored assets (GLB): register, then load in the background
      if (asset_registry_load(manifest)) LOG_INFO("asset", "%d assets requested: parsing and decoding on the job system, GPU upload inside the frame budget", asset_request_all()); }
    { double t0 = diag_now(); render_init(); LOG_INFO("gpu", "renderer initialised in %.1f ms", (diag_now()-t0)*1000); rhi_check_errors("render_init"); }
    asset_set_upload_hook(scene_asset_upload);   // finished assets reach the GPU through the MAIN lane, one LOD per budgeted job
    U.show_debug = dev;
    if (getenv("MARE_PASSES")) { const char *p = getenv("MARE_PASSES");   // experiment: enable only the listed passes
        U.pass_terrain = !!strstr(p, "terrain"); U.pass_sea = !!strstr(p, "sea"); U.pass_ports = !!strstr(p, "ports"); U.pass_ships = !!strstr(p, "ships");
        U.pass_overlays = !!strstr(p, "overlays"); U.pass_icons = !!strstr(p, "icons"); U.pass_labels = !!strstr(p, "labels"); U.pass_panels = !!strstr(p, "panels");
        LOG_INFO("main", "render passes restricted to: %s", p); }
    if (getenv("MARE_AUTOQUIT_S")) LOG_INFO("main", "will quit after %s seconds", getenv("MARE_AUTOQUIT_S"));
    log_set_listener(hud_on_log);
    U.in_menu = !autostart;
    if (autostart) { game_new(autostart, seed); game_advance((float)skip); int c = G.nations[G.player].capital; cam_reset(G.ports[c].x, G.ports[c].z, 60);
        const char *dvw = getenv("MARE_DETAIL");   // "battle" | "city": straight into a staged detail view (visual tests)
        if (dvw && !strcmp(dvw, "battle")) detailview_enter_battle(0);
        else if (dvw && !strcmp(dvw, "city")) detailview_enter_city(c);
    }

    int p_frame = prof_id("frame.cpu"), p_input = prof_id("input"), p_sim = prof_id("sim"), p_gfx = prof_id("gfx"), p_terrain = prof_id("gfx.terrain"), p_ports = prof_id("gfx.ports"),
        p_ships = prof_id("gfx.ships"), p_shadow = prof_id("gfx.shadow"), p_sea = prof_id("gfx.sea"), p_over = prof_id("gfx.overlays"), p_icons = prof_id("gfx.icons"), p_labels = prof_id("ui.labels"), p_panels = prof_id("ui.panels");
    int m_steps = metric_id("frame.sim_steps"), m_dt = metric_id("frame.dt_ms"), m_fps = metric_id("frame.fps");
    int m_draws = metric_id("gfx.draw_calls"), m_inst = metric_id("gfx.instances"), m_upload = metric_id("gfx.upload_kb");
    int g_terrain = rhi_gpu_scope("gpu.terrain"), g_shadow = rhi_gpu_scope("gpu.shadow"), g_meshes = rhi_gpu_scope("gpu.meshes"),
        g_sea = rhi_gpu_scope("gpu.sea"), g_resolve = rhi_gpu_scope("gpu.resolve");
    int m_gpu_terrain = metric_id("gpu.terrain_ms"), m_gpu_shadow = metric_id("gpu.shadow_ms"), m_gpu_meshes = metric_id("gpu.meshes_ms"),
        m_gpu_sea = metric_id("gpu.sea_ms"), m_gpu_resolve = metric_id("gpu.resolve_ms"), m_gpu_total = metric_id("gpu.total_ms");
    int m_vram = metric_id("mem.vram_mb"), m_ram = metric_id("mem.ram_mb");
    int m_asset_gpu = metric_id("asset.gpu_mb"), m_asset_inst = metric_id("asset.instances");
    int p_upload = prof_id("jobs.upload"), m_jobs_main = metric_id("jobs.main_ran"), m_jobs_queued = metric_id("jobs.queued");
    int m_lod_pops = metric_id("lod.pops"), m_lod_fading = metric_id("lod.fading"), m_asset_loading = metric_id("asset.loading");
    int m_lod_tris = metric_id("lod.asset_tris"), m_lod_budget = metric_id("lod.budget_scale"), m_icons_clustered = metric_id("lod.icons_clustered");
    double last = time_ss(); float sim_acc = 0;
    double t_run0 = diag_now(); const char *autoquit = getenv("MARE_AUTOQUIT_S");
    while (app_swap(.color = 0xFF000000)) {
        if (autoquit && diag_now() - t_run0 > atof(autoquit)) break;
        prof_begin(p_frame);
        { app_state_t st = app_state(); screen_w = maxi(1, st.w); screen_h = maxi(1, st.h); glfwGetWindowSize(st.handle, &window_w, &window_h); window_w = maxi(1, window_w); window_h = maxi(1, window_h); }   // once per frame
        double now = time_ss(); float dt = minf((float)(now - last), 0.1f); last = now;
        RF.time += dt; metric_set(m_dt, dt * 1000); metric_set(m_fps, fps());

        mu_Container *dbg = ui_root_container("debug");   // the framework's own window: keep it out of the way
        if (dbg) dbg->rect = mu_rect(-5000, -5000, 10, 10);

        ui_overlay_begin();
        int ui_blocked = ui_mouse_over_panel();
        // MARE_CAM="lon,lat,dist[,yaw_deg]" pins the camera: screenshots of a given seed then
        // frame the same thing every run, which is what makes visual regressions comparable.
        // It overrides the camera *after* the normal update, so the simulation still runs.
        static const char *cam_lock; static int cam_lock_read;
        if (!cam_lock_read) { cam_lock = getenv("MARE_CAM"); cam_lock_read = 1; }
        if (U.in_menu) {
            cam.want_dist = 800; cam.want_tx = lon2x(-30); cam.want_tz = lat2z(25); cam.want_yaw += dt * 0.02f;
            cam_update(dt);
        } else {
            prof_begin(p_input); input_frame(dt, ui_blocked); debug_keys(); cam_update(dt); prof_end(p_input);
            prof_begin(p_sim);
            // City view (3rd zoom): with the camera down on a town square the clock crawls to
            // 0.05 days/s so the place can be looked at without the world racing on. A battle
            // being watched from the same height must NOT slow down - spectating is free - so
            // any fresh broadside near the camera target overrides the throttle.
            U.city_view = 0;
            float eff_speed = G.speed;
            if (detailview_mode() == DV_CITY) { U.city_view = 1; eff_speed = minf(G.speed, 0.05f); }
            else if (detailview_mode() == DV_OFF && cam.dist < CAM_CITY_DIST) {
                int over_town = 0, battle_near = 0;
                for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i];
                    if (!port_is_gone(p) && p->size >= 1 && dist2d(p->x, p->z, cam.tx, cam.tz) < 8.f) { over_town = 1; break; } }
                for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i];
                    if (f->alive && f->at_port < 0 && f->last_fire > G.day - 0.3f && dist2d(f->x, f->z, cam.tx, cam.tz) < 30.f) { battle_near = 1; break; } }
                if (over_town && !battle_near) { U.city_view = 1; eff_speed = minf(G.speed, 0.05f); }
            }
            int steps = 0;
            if (eff_speed > 0) { sim_acc += dt * eff_speed; while (sim_acc >= SIM_STEP && steps < 40) { game_tick(SIM_STEP); sim_acc -= SIM_STEP; steps++; }
                if (steps >= 40 && sim_acc >= SIM_STEP) { LOG_WARN("main", "simulation behind real time: dropping %.1f days (frame %.0f ms at speed x%.0f)", sim_acc, dt*1000, G.speed); sim_acc = 0; } }
            metric_set(m_steps, steps);
            prof_end(p_sim);
            if (U.toast_timer > 0) U.toast_timer -= dt;
        }
        // MARE_ZOOM="d0,d1,seconds[,lon,lat]" flies the camera distance from d0 to d1 over the given
        // time (and back, looping): the scripted full-range zoom the LOD work is measured with
        // (lod.pops must stay 0, frame.dt_ms must stay flat while assets stream in and out).
        static const char *zoom_env; static int zoom_read;
        if (!zoom_read) { zoom_env = getenv("MARE_ZOOM"); zoom_read = 1; if (zoom_env) LOG_INFO("main", "scripted zoom %s (MARE_ZOOM)", zoom_env); }
        if (zoom_env && !U.in_menu && detailview_mode() == DV_OFF) {
            float d0 = 60, d1 = 900, secs = 10, lo = 1e9f, la = 1e9f; sscanf(zoom_env, "%f,%f,%f,%f,%f", &d0, &d1, &secs, &lo, &la);
            float ph = fmodf((float)(diag_now() - t_run0) / maxf(secs, 0.1f), 2.f); if (ph > 1) ph = 2 - ph;   // there and back
            cam.want_dist = cam.dist = lerpf(d0, d1, ph);
            if (lo < 1e8f) { cam.want_tx = cam.tx = lon2x(lo); cam.want_tz = cam.tz = lat2z(la); }
            cam_update(dt);
        }
        if (cam_lock && detailview_mode() == DV_OFF) {   // the detail view drives the camera itself
            float lo = 0, la = 0, di = 60, ya = 0; sscanf(cam_lock, "%f,%f,%f,%f", &lo, &la, &di, &ya);
            cam.want_tx = cam.tx = lon2x(lo); cam.want_tz = cam.tz = lat2z(la);
            cam.want_dist = cam.dist = di; cam.want_yaw = cam.yaw = ya * 0.0174533f;
            cam_update(dt);
        }
        // Work that finished on the pool and needs the GL context - asset uploads, LOD streaming
        // (DESIGN_JOBS.md) - is run here, rate limited so a burst of ready meshes cannot turn
        // into a 30 ms frame. Whatever does not fit waits for the next frame.
        prof_begin(p_upload);
        { int ran = jobs_main_drain(JOBS_UPLOAD_BUDGET_MS);
          job_stats_t js; jobs_stats(&js);
          metric_set(m_jobs_main, ran);
          metric_set(m_jobs_queued, js.lane[JOB_LANE_WORKER].queued + js.lane[JOB_LANE_DECODE].queued
                                  + js.lane[JOB_LANE_IO].queued + js.lane[JOB_LANE_MAIN].queued); }
        prof_end(p_upload);
        if (territory_dirty == 2) render_territory_upload();

        // ---- 3d
        RF.vp = cam.vp; RF.campos = cam.pos; RF.mapness = cam.mapness; RF.flatten = cam.mapness;
        RF.sea_center = vec3(cam.tx, 0, cam.tz);          // the sea's wave grid follows the look-at point
        render_sun_update(G.day);                         // sun direction, daylight and sky colour (day/night cycle)
        RF.sky = lerp3(RF.sky, vec3(0.36f, 0.30f, 0.24f), cam.mapness);   // the map view fades to parchment
        render_light_update();
        rhi_frame_stats_reset();
        prof_begin(p_gfx);
        // scene geometry is built once, then submitted to the shadow cascades and the main passes
        lod_frame_begin((float)screen_h, 45.f * 0.0174533f, dt);   // screen-space LOD metric for this frame (camera.c: 45 degree vertical FOV)
        lod_set_tier(cam.dist, cam.mapness);                       // which kind of scene this is, and its triangle budget
        if (!U.in_menu) {
            SH = (scene_hints_t){ U.sel_fleet_id, U.hover_fleet_id, U.sel_port, U.hover_port, U.route_mode, U.show_lanes, U.show_wind, U.show_regions }; terrain_show_regions = U.show_regions;
            detailview_frame(dt);                 // transition clock + staged-scene animation (before the shadow pass)
            int dv_on = detailview_mode() != DV_OFF;
            prof_begin(p_ports); if (U.pass_ports && !dv_on) scene_build_ports(); prof_end(p_ports);
            prof_begin(p_ships); if (U.pass_ships && !dv_on) scene_build_ships(); prof_end(p_ships);
            prof_begin(p_shadow); rhi_gpu_begin(g_shadow); shadow_render(); rhi_gpu_end(); prof_end(p_shadow);
        }
        // the HDR target is cleared in linear space: the sky colour is authored in sRGB
        { vec3 skyl = srgb_to_linear3(RF.sky); hdr_begin(screen_w, screen_h, skyl.x, skyl.y, skyl.z); }
        float planes[6][4]; cam_frustum(planes);
        int dv_on = !U.in_menu && detailview_mode() != DV_OFF;
        prof_begin(p_terrain); rhi_gpu_begin(g_terrain); if (U.pass_terrain && !dv_on) terrain_draw(planes); rhi_gpu_end(); prof_end(p_terrain);
        icons_begin();
        if (!U.in_menu) {
            meshes_bind();
            rhi_gpu_begin(g_meshes);
            if (dv_on) { prof_begin(p_ports); detailview_submit(); prof_end(p_ports); }
            else {
                prof_begin(p_ports); if (U.pass_ports) scene_submit_ports(); prof_end(p_ports);
                prof_begin(p_ships); if (U.pass_ships) scene_submit_ships(); prof_end(p_ships);
            }
            rhi_gpu_end();
        }
        prof_begin(p_sea); rhi_gpu_begin(g_sea); if (U.pass_sea && !U.in_menu && dv_on) detailview_sea(); else if (U.pass_sea) sea_draw(); rhi_gpu_end(); prof_end(p_sea);
        if (!U.in_menu) { meshes_bind_smoke(); if (dv_on) detailview_submit_smoke(); else if (U.pass_ships) scene_submit_smoke(); }   // translucent, after the water
        if (!U.in_menu && !dv_on) {
            prof_begin(p_over); if (U.pass_overlays) { scene_draw_overlays(); dd_flush(cam.proj, cam.view); } prof_end(p_over);
            prof_begin(p_icons); if (U.pass_icons) { scene_push_icons(); icons_draw(); } prof_end(p_icons);
        }
        rhi_gpu_begin(g_resolve); hdr_resolve(screen_w, screen_h); rhi_gpu_end();   // tonemap to the backbuffer; labels/panels draw on top
        if (!U.in_menu) detailview_clouds();                                        // the transition rolls over the finished frame
        { static int shot_done; const char *shot = getenv("MARE_SCREENSHOT");
          if (shot && !shot_done && autoquit && diag_now() - t_run0 > atof(autoquit) - 0.5) { screenshot_ppm(shot);
            const char *sd = getenv("MARE_SHADOWDUMP"); if (sd) shadow_dump(sd);
            shot_done = 1; } }
        if (!U.in_menu && detailview_mode() == DV_OFF) { prof_begin(p_labels); if (U.pass_labels) hud_labels(); prof_end(p_labels); }
        prof_end(p_gfx);
        { rhi_stats_t st = rhi_frame_stats(); metric_set(m_draws, st.draws); metric_set(m_inst, st.instances); metric_set(m_upload, st.upload_bytes / 1024.0); }
        rhi_gpu_collect();
        { float gt = rhi_gpu_ms(g_terrain), gs = rhi_gpu_ms(g_shadow), gm = rhi_gpu_ms(g_meshes), gw = rhi_gpu_ms(g_sea), gr = rhi_gpu_ms(g_resolve);
          metric_set(m_gpu_terrain, gt); metric_set(m_gpu_shadow, gs); metric_set(m_gpu_meshes, gm);
          metric_set(m_gpu_sea, gw); metric_set(m_gpu_resolve, gr); metric_set(m_gpu_total, gt+gs+gm+gw+gr);
          rhi_mem_t mem = rhi_memory();
          metric_set(m_vram, (mem.textures + mem.targets + mem.buffers) / (1024.0*1024.0));
          metric_set(m_ram, diag_process_ram() / (1024.0*1024.0));
          metric_set(m_asset_gpu, asset_gpu_bytes() / (1024.0*1024.0)); metric_set(m_asset_inst, scene_asset_instances);
          metric_set(m_lod_pops, lod_pops); metric_set(m_lod_fading, lod_fading); metric_set(m_asset_loading, asset_loading_count());
          metric_set(m_lod_tris, (double)lod_last_tris); metric_set(m_lod_budget, lod_budget_scale); metric_set(m_icons_clustered, scene_icons_clustered); }
        ui_overlay_end();
        if (U.show_debug) rhi_check_errors("frame");

        // ---- panels
        prof_begin(p_panels);
        if (U.in_menu) {
            int pick = hud_menu();
            if (pick > 0) { game_new(pick, seed); scene_reset_caches(); int c = G.nations[G.player].capital; cam_reset(G.ports[c].x, G.ports[c].z, 60); U.in_menu = 0; U.show_help = 1; }
            else if (pick < 0) { if (game_load("mare_save.bin")) { scene_reset_caches(); int c = G.nations[G.player].capital; cam_reset(G.ports[c].x, G.ports[c].z, 60); U.in_menu = 0; } else toast("No compatible saved game found"); }
        } else { if (U.pass_panels) hud_panels(); dashboard_panel(); }
        prof_end(p_panels);
        prof_end(p_frame);
        prof_frame_end();
    }
    LOG_INFO("main", "window closed, shutting down");
    return 0;
}
