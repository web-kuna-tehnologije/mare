// MARE LIBERUM - 17th century naval trade & war RTS on the v3 framework.
// The C++ driver (docs/REWRITE_PLAN.md): every module is its own C++20 translation unit under
// src/, the framework is the one C unit in src/platform/v3_impl.c, and this file owns main()
// and the frame loop. See game/build.sh / build.bat for the canonical builds.

#include "src/platform/v3cpp.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

extern "C" {
#include "src/core/jobs.h"
#include "src/core/diag.h"
#include "src/core/types.h"
#include "src/core/util.h"
#include "src/core/balance.h"
#include "src/core/log.h"
#include "src/engine/entities.h"
#include "src/data/tables.h"
#include "src/data/geo_ports.h"
#include "src/sim/settlement.h"
#include "src/world/world.h"
#include "src/world/terrainmap.h"
#include "src/world/geochunks.h"
#include "src/world/rivers.h"
#include "src/world/stream.h"
#include "src/world/nav.h"
#include "src/sim/game.h"
#include "src/sim/fleet.h"
#include "src/sim/events.h"
#include "src/sim/economy.h"
#include "src/assets/asset.h"
#include "src/gfx/rhi.h"
#include "src/gfx/render.h"
#include "src/gfx/camera.h"
#include "src/gfx/scene.h"
#include "src/gfx/shipyard.h"
#include "src/gfx/lod.h"
#include "src/gfx/occlusion.h"
#include "src/gfx/occquery.h"
#include "src/gfx/detailview.h"
#include "src/gfx/weather.h"
#include "src/ui/ui.h"
#include "src/ui/options.h"
}

// Ask a hybrid laptop for the discrete GPU.
//
// On a machine with an Intel iGPU and an NVIDIA or AMD dGPU, the driver hands an OpenGL
// program the integrated chip unless the executable says otherwise. Both vendors read these
// exported symbols out of the .exe at process start; this is the documented way to opt in and
// there is no runtime cost. Measured over the Dalmatian coast at 1600x900, 20 s with the sea
// clock frozen: 52.7 fps on the Intel iGPU, 202.7 fps once the driver handed the context to
// the RTX 5070 Ti - 3.85x, for two exported symbols. The game had been drawing a tessellated
// sea and terrain on the integrated chip while the discrete GPU sat idle. Neither driver
// reads an opt-out from the process, so forcing the iGPU back is the per-application setting
// in the vendor control panel.
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// How long the render thread may spend per frame on work the pool handed back to it (GPU
// uploads). At 60 Hz the frame has ~16 ms: 2 ms of uploads is invisible, and anything that does
// not fit simply lands in the next frame (see jobs_main_drain).
#define JOBS_UPLOAD_BUDGET_MS 2.0

// How long an un-uploaded detail level may hold its CPU copy before it is given back. Long
// enough that entering a battle does not pay for a reload, short enough that the Gen I fleet's
// LOD 0 scans do not sit in RAM for a whole session.
#define ASSET_LOD0_IDLE_S 45.0

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
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
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

// --map-chunk=level,tx,ty[,res]: write the shaded-relief reference a map chunk is modelled
// from (docs/BLENDER_LOD.md section 4) and print everything the model has to match - extent,
// world size, height range. Headless: no window, no GL.
static int map_chunk_dump(const char *spec, unsigned seed) {
    int level = 3, tx = 0, ty = 0, res = 768;
    if (sscanf(spec, "%d,%d,%d,%d", &level, &tx, &ty, &res) < 3) {
        fprintf(stderr, "--map-chunk=level,tx,ty[,res]\n");
        return 2;
    }
    world_init(seed);
    stream_init(seed, "tiles");
    char path[128]; snprintf(path, sizeof path, "map_chunk_l%d_%d_%d.ppm", level, tx, ty);
    stream_chunk_ref_t r;
    if (!stream_dump_chunk_reference(level, tx, ty, res, path, &r)) return 1;
    const stream_tile_t *t = stream_tile(level, tx, ty);
    char label[64]; stream_tile_label(t, label, sizeof label);
    printf("map chunk %s\n", label);
    printf("  asset name   tile_l%d_%d_%d          (game/assets/tile_l%d_%d_%d/)\n", level, tx, ty, level, tx, ty);
    printf("  reference    %s  (%dx%d; tools/ppm2png.py turns it into a PNG for Meshy)\n", path, res, res);
    printf("  geographic   lon %.2f .. %.2f, lat %.2f .. %.2f (%.0f degrees square)\n", r.lon0, r.lon1, r.lat0, r.lat1, r.size_deg);
    printf("  world rect   x %.1f .. %.1f, z %.1f .. %.1f (%.0f units square)\n", r.x0, r.x1, r.z0, r.z1, r.size_units);
    printf("  height       %.2f .. %.2f world units, %.0f%% land, %s\n", r.min_h, r.max_h, r.land_frac * 100,
           t->water_only ? "ALL WATER - not worth modelling" : "has land");
    printf("  model it     %.0f units across, sea level at y = 0, +x east and +z south\n", r.size_units);
    printf("  import       python3 tools/meshy_import.py --kind terrain --root <download dir> --name tile_l%d_%d_%d\n", level, tx, ty);
    stream_shutdown();
    return 0;
}

// --map-chunks=level[,max]: the work list for authoring the world in chunks. Every tile of
// that level with land in it, ordered by how much the player will look at it (ports first,
// then how much land it holds), with the asset name and the command for its reference image.
static int map_chunks_list(const char *spec, unsigned seed) {
    int level = STREAM_L_TACTICAL, maxn = 0;
    sscanf(spec, "%d,%d", &level, &maxn);
    world_init(seed);
    stream_init(seed, "tiles");
    int gw = stream_grid_w(level), gh = stream_grid_h(level);
    if (!gw) { fprintf(stderr, "level must be 0..%d\n", STREAM_LEVELS - 1); return 2; }
    typedef struct row_t { int tx, ty, ports, big; float land; char names[72]; } row_t;
    row_t *rows = (row_t *)calloc((size_t)gw * gh, sizeof *rows); int n = 0;
    float deg = (STREAM_L0_CELLS >> level) / (float)CELLS_PER_DEG;
    for (int ty = 0; ty < gh; ++ty) for (int tx = 0; tx < gw; ++tx) {
        const stream_tile_t *t = stream_tile(level, tx, ty);
        if (!t || t->water_only) continue;
        float lon0 = LON0 + tx * deg, lat1 = LAT1 - ty * deg;
        int land = 0, cells = 0;
        for (int j = 0; j < 24; ++j) for (int i = 0; i < 24; ++i) {
            float lo = lon0 + (i + 0.5f) * deg / 24, la = lat1 - (j + 0.5f) * deg / 24;
            cells++; land += world_is_land(lon2x(lo), lat2z(la)) != 0;
        }
        if (!land) continue;
        row_t *r = &rows[n++];
        r->tx = tx; r->ty = ty; r->land = land / (float)cells;
        for (int p = 0; p < GEO_NPORTS; ++p) {
            const geo_port_t *gp = &geo_ports[p];
            if (gp->size <= 0) continue;
            if (gp->lon < lon0 || gp->lon >= lon0 + deg || gp->lat > lat1 || gp->lat <= lat1 - deg) continue;
            r->ports++; if (gp->size >= 2) r->big++;
            int len = (int)strlen(r->names);
            if (len < (int)sizeof r->names - 18)
                snprintf(r->names + len, sizeof r->names - len, "%s%.14s", len ? ", " : "", gp->name);
        }
    }
    for (int i = 1; i < n; ++i) {   // insertion sort: ports, then big ports, then land
        row_t v = rows[i]; int j = i - 1;
        while (j >= 0 && (rows[j].ports < v.ports || (rows[j].ports == v.ports && rows[j].big < v.big)
                       || (rows[j].ports == v.ports && rows[j].big == v.big && rows[j].land < v.land)))
            { rows[j+1] = rows[j]; j--; }
        rows[j+1] = v;
    }
    printf("\n%d of %d L%d chunks have land (%.0f degrees / %.0f world units square).\n",
           n, gw * gh, level, deg, deg * WORLD_K);
    printf("Ordered by what the player looks at: ports first, then how much land.\n\n");
    printf("  %-4s %-18s %-26s %5s %5s  %s\n", "#", "asset name", "region", "ports", "land", "ports in it");
    printf("  ---- ------------------ -------------------------- ----- -----  -----------\n");
    int show = maxn > 0 && maxn < n ? maxn : n;
    for (int i = 0; i < show; ++i) {
        const row_t *r = &rows[i];
        const stream_tile_t *t = stream_tile(level, r->tx, r->ty);
        char label[64]; stream_tile_label(t, label, sizeof label);
        const char *reg = strchr(label, '('); char regbuf[32] = "";
        if (reg) { snprintf(regbuf, sizeof regbuf, "%.*s", (int)strlen(reg + 1) - 1, reg + 1); }
        char asset[24]; snprintf(asset, sizeof asset, "tile_l%d_%d_%d", level, r->tx, r->ty);
        printf("  %-4d %-18s %-26.26s %5d %4.0f%%  %s\n",
               i + 1, asset, regbuf, r->ports, r->land * 100, r->names);
    }
    if (show < n) printf("  ... and %d more with land but no ports\n", n - show);
    printf("\nFor any of them:  ./mare --map-chunk=%d,<tx>,<ty> --seed=%u\n", level, seed);
    printf("then model it 32 units across with sea level at y = 0 (game/docs/BLENDER_LOD.md section 4).\n");
    free(rows);
    stream_shutdown();
    return 0;
}

int main(int argc, char **argv) {
    unsigned seed = (unsigned)time(0); int autostart = 0, headless = 0, skip = 0, dev = 0;
    const char *map_chunk = 0, *map_chunks = 0;
    diag_config_t dc = { "logs", LOG_WARN, LOG_DEBUG, LOG_TRACE, 0 };
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--seed=", 7)) seed = (unsigned)atoi(argv[i]+7);
        else if (!strncmp(argv[i], "--nation=", 9)) autostart = atoi(argv[i]+9);
        else if (!strncmp(argv[i], "--simulate=", 11)) headless = atoi(argv[i]+11);
        else if (!strncmp(argv[i], "--map-chunk=", 12)) map_chunk = argv[i]+12;
        else if (!strncmp(argv[i], "--map-chunks=", 13)) map_chunks = argv[i]+13;
        else if (!strcmp(argv[i], "--map-chunks")) map_chunks = "3";
        else if (!strncmp(argv[i], "--skip=", 7)) skip = atoi(argv[i]+7);
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--no-shadows")) shadow_enabled = 0;
        else if (!strncmp(argv[i], "--rhi=", 6)) rhi_select(argv[i]+6);   // gl (default) | vulkan | dx12 (docs/REWRITE_PLAN.md T4/T8)
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
    // Graphics settings before anything creates a device: the backend the player picked is
    // in the file, and MARE_RHI (set by the measurement scripts) still overrides it.
    options_load();
    if (!getenv("MARE_RHI") && OPT.backend[0]) rhi_select(OPT.backend);
    if (map_chunk) return map_chunk_dump(map_chunk, seed);    // reference image for a map chunk, then out
    if (map_chunks) return map_chunks_list(map_chunks, seed); // the whole authoring work list, then out
    if (headless) {   // balance run without a window
        world_init(seed); nav_init(); game_new(autostart ? autostart : NATION_NETHERLANDS, seed);
        double t0 = diag_now(); game_advance((float)headless);
        LOG_INFO("main", "simulated %d days in %.2f s (%.1f ms per day)", headless, diag_now()-t0, (diag_now()-t0)*1000/headless);
        print_report(headless);
        return 0;
    }

    app_create_flags_t acf = {};   // the C macro's designators are out of declaration order for C++20
    acf.title = "Mare Liberum"; acf.w = 1600; acf.h = 900; acf.scale = 0.92f;
    acf.resizable = 1; acf.decorated = 1; acf.visible = 1; acf.debug = !V3_RETAIL; acf.silent = V3_RETAIL;
    (app_create)(acf);
    { app_state_t st = app_state(); screen_w = maxi(1, st.w); screen_h = maxi(1, st.h); }
    // v3 caps the frame rate to the monitor refresh rate + 2. Virtual displays (VMs, Xvfb, some
    // remote desktops) report 0 Hz, which would lock the game to 2 FPS: fall back to uncapped.
    { float hz = monitor_hz(0); if (hz < 10 && !flag("--fps")) { vsync(0); LOG_WARN("main", "monitor reports %.0f Hz: frame limiter disabled (use --fps=N to cap)", hz); } else LOG_INFO("main", "monitor %.0f Hz", hz); }
    LOG_INFO("gpu", "%s", rhi_info());
    world_init(seed);
    terrainmap_load(NULL);          // authored height / normals / ground atlas, if the art is there
    terrainmap_apply_to_world();   // folded in before nav, so routes see the same ground
    // The terrain-v2 export read as the chunks it is (assets/data_new/MARE_INTEGRATION.md):
    // float32 metres, coverage, biome classes, material weights and normals, from every
    // dataset under data_new (the 3 km overview and any finer regional build). Two consumers:
    // the clipmap rings and the chunk atlas page it in around the camera (gfx/streamview.cc,
    // gfx/chunkatlas.cc), and the one-shot geography bake below folds its relief into the base
    // world before the navigation grid, the regions and the ports are built against it.
    // MARE_GEOCHUNKS=0 / MARE_TERRAIN_GEO=0 bring the procedural world back.
    geochunks_init(NULL);
    terrainmap_bake_geography(NULL);
    rivers_generate();             // the drainage of the folded heightfield: channels for the rings, navigable reaches for nav (world/rivers.h)
    nav_init();
    if (getenv("MARE_WAVES")) { sea_wave_amp = clampf((float)atof(getenv("MARE_WAVES")), 0.f, 4.f);
        LOG_INFO("gpu", "wave height pinned to %.2f (MARE_WAVES)", sea_wave_amp); }
    if (getenv("MARE_LOD_BIAS")) { asset_lod_bias = clampf((float)atof(getenv("MARE_LOD_BIAS")), 0.01f, 16.f);   // pin the LOD knob for screenshot runs
        LOG_INFO("asset", "LOD bias pinned to %.2f (MARE_LOD_BIAS)", asset_lod_bias); }
    { char manifest[512]; snprintf(manifest, sizeof manifest, "%s/manifest.json", asset_root());   // authored assets (GLB): register, then load in the background
      if (asset_registry_load(manifest)) LOG_INFO("asset", "%d assets requested: parsing and decoding on the job system, GPU upload inside the frame budget", asset_request_all()); }
    { double t0 = diag_now(); render_init(); LOG_INFO("gpu", "renderer initialised in %.1f ms", (diag_now()-t0)*1000); rhi_check_errors("render_init"); }
    // C++ entities (engine/entities.h): async halves load on the pool, GL halves right here
    { double t0 = diag_now(); entities_startup(); entities_load();
      LOG_INFO("main", "%d entities loaded in %.1f ms", entities_count(), (diag_now()-t0)*1000); rhi_check_errors("entities_load"); }
    asset_set_upload_hook(scene_asset_upload);   // finished assets reach the GPU through the MAIN lane, one LOD per budgeted job
    if (getenv("MARE_SHIPS")) { shipyard_enabled = atoi(getenv("MARE_SHIPS")) != 0; LOG_INFO("asset", "authored ships %s (MARE_SHIPS)", shipyard_enabled ? "on" : "off"); }
    if (shipyard_enabled) shipyard_request_all();   // the Gen I hulls, rigs and oars, ahead of the first battle
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
    int m_lod_fallbacks = metric_id("asset.lod_fallbacks"), m_lod_trim = metric_id("asset.lod_trimmed_mb");
    int m_lod_tris = metric_id("lod.asset_tris"), m_lod_budget = metric_id("lod.budget_scale"), m_icons_clustered = metric_id("lod.icons_clustered");
    int m_jobs_busy = metric_id("jobs.busy_pct"), m_jobs_wait = metric_id("jobs.wait_ms"), m_jobs_io_q = metric_id("jobs.io_queued");
    int m_occ_ports = metric_id("gfx.occluded_ports"), m_occ_ships = metric_id("gfx.occluded_ships"), m_occ_samples = metric_id("gfx.occ_samples");
    int m_occq_hidden = metric_id("gfx.occluded_assets"), m_occq_queries = metric_id("gfx.occ_queries"), m_occq_moving = metric_id("gfx.cam_moving");
    if (getenv("MARE_OCCLUSION")) { occlusion_enabled = atoi(getenv("MARE_OCCLUSION")) != 0; LOG_INFO("gpu", "occlusion culling %s (MARE_OCCLUSION)", occlusion_enabled ? "on" : "off"); }
    if (getenv("MARE_SKY")) { sky_enabled = atoi(getenv("MARE_SKY")) != 0; LOG_INFO("gpu", "sky pass %s (MARE_SKY)", sky_enabled ? "on" : "off"); }
    if (getenv("MARE_SHIP_WAVES")) { sea_ship_waves = atoi(getenv("MARE_SHIP_WAVES")) != 0; LOG_INFO("gpu", "ship bow waves %s (MARE_SHIP_WAVES)", sea_ship_waves ? "on" : "off"); }
    if (getenv("MARE_SEA_REFRACT")) { sea_refract = atoi(getenv("MARE_SEA_REFRACT")) != 0; LOG_INFO("gpu", "sea refraction %s (MARE_SEA_REFRACT)", sea_refract ? "on" : "off"); }
    if (getenv("MARE_SEA_TESS_MAX")) { sea_tess_level_max = atoi(getenv("MARE_SEA_TESS_MAX")); LOG_INFO("gpu", "sea tessellation ceiling %d (MARE_SEA_TESS_MAX)", sea_tess_level_max); }
    if (getenv("MARE_SEA_INNER")) { sea_inner_on = atoi(getenv("MARE_SEA_INNER")) != 0; LOG_INFO("gpu", "sea inner lattice %s (MARE_SEA_INNER)", sea_inner_on ? "on" : "off"); }
    if (getenv("MARE_FOAM")) { sea_foam = clampf((float)atof(getenv("MARE_FOAM")), 0.f, 3.f); LOG_INFO("gpu", "sea foam x%.2f (MARE_FOAM)", sea_foam); }
    if (getenv("MARE_SEA_NYQUIST")) { sea_nyquist = atoi(getenv("MARE_SEA_NYQUIST")) != 0; LOG_INFO("gpu", "sea grid-aware wave fade %s (MARE_SEA_NYQUIST)", sea_nyquist ? "on" : "off"); }
    if (getenv("MARE_SHADOW_LOD")) { shadow_caster_lod = atoi(getenv("MARE_SHADOW_LOD")) != 0; LOG_INFO("gpu", "shadow casters use the coarse LOD: %s (MARE_SHADOW_LOD)", shadow_caster_lod ? "yes" : "no"); }
    int m_shadow_tris = metric_id("gfx.shadow_tris"), m_shadow_ports = metric_id("gfx.shadow_ports"), m_shadow_fleets = metric_id("gfx.shadow_fleets"), m_shadow_radius = metric_id("gfx.shadow_radius");
    int m_wx_rain = metric_id("weather.rain"), m_wx_haze = metric_id("weather.haze"), m_wx_calm = metric_id("weather.calm");
    int m_eff_speed = metric_id("sim.speed_eff"), m_cam_dist = metric_id("sim.cam_dist"), m_city_view = metric_id("sim.city_view");
    // Autosave clock (balance.h AUTOSAVE_SECS, MARE_AUTOSAVE_S overrides; 0 disables). Counts
    // unpaused play only, so a game left paused overnight is not rewritten every three minutes.
    float autosave_secs = AUTOSAVE_SECS; if (getenv("MARE_AUTOSAVE_S")) { autosave_secs = (float)atof(getenv("MARE_AUTOSAVE_S")); LOG_INFO("main", "autosave every %.0f s (MARE_AUTOSAVE_S)", autosave_secs); }
    U.autosave_in = autosave_secs;
    float tick_secs = SIM_TICK_SECONDS;
    if (getenv("MARE_TICK_S")) { tick_secs = clampf((float)atof(getenv("MARE_TICK_S")), 0.05f, 120.f); LOG_INFO("main", "simulation tick every %.2f s (MARE_TICK_S)", tick_secs); }
    else LOG_INFO("main", "simulation ticks every %.0f s (or %.0f simulated days, whichever comes first)", tick_secs, SIM_TICK_MAX_DAYS);
    double last = time_ss(); float sim_acc = 0, tick_acc = 0;
    double t_run0 = diag_now(); const char *autoquit = getenv("MARE_AUTOQUIT_S");
    app_swap_flags_t asf = {}; asf.hz = 1; asf.depth = 1; asf.stencil = 0; asf.color = 0xFF000000;
    while ((app_swap)(asf)) {
        if (autoquit && diag_now() - t_run0 > atof(autoquit)) break;
        prof_begin(p_frame);
        { app_state_t st = app_state(); screen_w = maxi(1, st.w); screen_h = maxi(1, st.h); glfwGetWindowSize((GLFWwindow *)st.handle, &window_w, &window_h); window_w = maxi(1, window_w); window_h = maxi(1, window_h); }   // once per frame
        double now = time_ss(); float dt = minf((float)(now - last), 0.1f); last = now;
        RF.time += dt; metric_set(m_dt, dt * 1000); metric_set(m_fps, fps());
        // Evidence for the stall hunt (DESIGN_JOBS.md step 10): any frame over 25 ms after the
        // first second is logged with where the previous frame's time went, so a hitch in a run
        // log can be attributed (CPU scope, GPU, uploads, a save in flight) instead of guessed.
        if (dt > 0.025f && diag_now() - t_run0 > 1.0 && prof_frame_index() > 10)
            LOG_DEBUG("main", "long frame %.1f ms: previous frame cpu %.1f ms, gpu %.1f ms, %.0f main-lane jobs, %d assets loading, save/load %s, cam dist %.0f",
                      dt * 1000, prof_get(p_frame)->last_ms, metric_value(m_gpu_total), metric_value(m_jobs_main), asset_loading_count(),
                      game_io_state() == GAME_IO_IDLE ? "idle" : "in flight", cam.dist);

        mu_Container *dbg = ui_root_container("debug");   // the framework's own window: keep it out of the way
        if (dbg) dbg->rect = mu_rect(-5000, -5000, 10, 10);

        ui_overlay_begin();
        int ui_blocked = ui_mouse_over_panel();
        // MARE_CAM="lon,lat,dist[,yaw_deg]" pins the camera: screenshots of a given seed then
        // frame the same thing every run, which is what makes visual regressions comparable.
        // It overrides the camera *after* the normal update, so the simulation still runs.
        static const char *cam_lock; static int cam_lock_read;
        if (!cam_lock_read) { cam_lock = getenv("MARE_CAM"); cam_lock_read = 1;
                              if (getenv("MARE_CAM_TILT")) cam_pitch_user = (float)atof(getenv("MARE_CAM_TILT"));     // pinned tilt for horizon screenshots
                              if (getenv("MARE_MAP_MODE")) U.map_mode = (int)clampf((float)atoi(getenv("MARE_MAP_MODE")), 0.f, (float)(MAP_MODE_COUNT - 1)); }   // pinned map mode
        if (U.in_menu) {
            cam.want_dist = 800; cam.want_tx = lon2x(-30); cam.want_tz = lat2z(25); cam.want_yaw += dt * 0.02f;
            cam_update(dt);
        } else {
            // the eye stays above whatever is under it: the wave crest over water, the ground
            // (base map + streamed detail) over land
            { float floor_y = world_is_land(cam.pos.x, cam.pos.z) ? world_height(cam.pos.x, cam.pos.z) + stream_delta_at(cam.pos.x, cam.pos.z) + 0.6f : sea_height_at(cam.pos.x, cam.pos.z) + 0.45f;
              cam_min_eye_y = detailview_mode() == DV_OFF ? floor_y : -1e9f; }
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
                int over_town = 0, battle_near = 0; float bw = maxf(0.3f, scene_smoke_window());
                for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i];
                    if (!port_is_gone(p) && p->size >= 1 && dist2d(p->x, p->z, cam.tx, cam.tz) < 8.f) { over_town = 1; break; } }
                for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i];
                    if (f->alive && f->at_port < 0 && f->last_fire > G.day - bw && dist2d(f->x, f->z, cam.tx, cam.tz) < 30.f) { battle_near = 1; break; } }
                if (over_town && !battle_near) { U.city_view = 1; eff_speed = minf(G.speed, 0.05f); }
            }
            // One tick every SIM_TICK_SECONDS (or SIM_TICK_MAX_DAYS, whichever comes first),
            // advancing all the time that has accumulated as a burst of SIM_STEP sub-steps: the
            // world runs at its usual speed and the simulation is bit-for-bit what it was, there
            // are simply far fewer tick events. scene.c glides the fleets across the interval.
            int steps = 0;
            if (eff_speed > 0) {
                sim_acc += dt * eff_speed; tick_acc += dt;
                // the first tick after a load fires as soon as there is a step to run, so the
                // world is not frozen for the first SIM_TICK_SECONDS of a session
                static int first_tick = 1;
                if (tick_acc >= tick_secs || sim_acc >= SIM_TICK_MAX_DAYS || (first_tick && sim_acc >= SIM_STEP)) {
                    first_tick = 0;
                    float days0 = sim_acc;
                    int cap = (int)(SIM_TICK_MAX_DAYS / SIM_STEP) + 2;
                    while (sim_acc >= SIM_STEP && steps < cap) { game_tick(SIM_STEP); sim_acc -= SIM_STEP; steps++; }
                    if (steps >= cap && sim_acc >= SIM_STEP) { LOG_WARN("main", "simulation behind real time: dropping %.1f days (speed x%.1f)", sim_acc, G.speed); sim_acc = 0; }
                    scene_set_tick(tick_acc, days0 - sim_acc, tick_secs);
                    tick_acc = 0;
                }
            } else { tick_acc = 0; scene_set_tick(tick_secs, 0.f, tick_secs); }
            scene_set_tick(tick_secs, scene_smoke_window(), maxf(tick_secs - tick_acc, 0.0001f));
            metric_set(m_steps, steps);
            metric_set(m_eff_speed, eff_speed); metric_set(m_cam_dist, cam.dist); metric_set(m_city_view, U.city_view);
            prof_end(p_sim);
            if (U.toast_timer > 0) U.toast_timer -= dt;
            if (autosave_secs > 0 && eff_speed > 0) {
                U.autosave_in -= dt;
                if (U.autosave_in <= 0) { U.autosave_in = autosave_secs; game_save_async("mare_autosave.bin"); }   // refused only during a load; the next tick retries
            }
        }
        // MARE_ZOOM="d0,d1,seconds[,lon,lat[,delay]]" flies the camera distance from d0 to d1 over
        // the given time (and back, looping): the scripted full-range zoom the LOD work is measured
        // with (lod.pops must stay 0, frame.dt_ms must stay flat while assets stream in and out).
        // `delay` holds the camera at d0 for that many seconds first, so a measurement can start
        // once the start-up asset loads have landed and the towns around d0 are built.
        static const char *zoom_env; static int zoom_read;
        if (!zoom_read) { zoom_env = getenv("MARE_ZOOM"); zoom_read = 1; if (zoom_env) LOG_INFO("main", "scripted zoom %s (MARE_ZOOM)", zoom_env); }
        if (zoom_env && !U.in_menu && detailview_mode() == DV_OFF) {
            float d0 = 60, d1 = 900, secs = 10, lo = 1e9f, la = 1e9f, delay = 0; sscanf(zoom_env, "%f,%f,%f,%f,%f,%f", &d0, &d1, &secs, &lo, &la, &delay);
            float ph = fmodf(maxf(0.f, (float)(diag_now() - t_run0) - delay) / maxf(secs, 0.1f), 2.f); if (ph > 1) ph = 2 - ph;   // there and back
            cam.want_dist = cam.dist = lerpf(d0, d1, ph);
            if (lo < 1e8f) { cam.want_tx = cam.tx = lon2x(lo); cam.want_tz = cam.tz = lat2z(la); }
            cam_update(dt);
        }
        // MARE_PAN="lonA,latA,lonB,latB,seconds[,dist[,yaw]]" slides the look-at point from A to B over
        // the given time and holds it there: the test for the sea being anchored to the world and
        // not to the camera (DESIGN_VISUAL.md step 4). With MARE_SEA_FREEZE=1 the frame after a
        // 3 s pan must equal the frame after a 0.01 s teleport, pixel for pixel.
        static const char *pan_env; static int pan_read;
        if (!pan_read) { pan_env = getenv("MARE_PAN"); pan_read = 1; if (pan_env) LOG_INFO("main", "scripted pan %s (MARE_PAN)", pan_env); }
        if (pan_env && !U.in_menu && detailview_mode() == DV_OFF) {
            float la0 = 0, lo0 = 0, lo1 = 0, la1 = 0, secs = 3, di = 22, ya = 30; sscanf(pan_env, "%f,%f,%f,%f,%f,%f,%f", &lo0, &la0, &lo1, &la1, &secs, &di, &ya);
            float ph = clampf((float)(diag_now() - t_run0 - 1.0) / maxf(secs, 0.01f), 0.f, 1.f);   // the first second settles
            cam.want_tx = cam.tx = lon2x(lerpf(lo0, lo1, ph)); cam.want_tz = cam.tz = lat2z(lerpf(la0, la1, ph));
            cam.want_dist = cam.dist = di; cam.want_yaw = cam.yaw = ya * 0.0174533f;
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
        // Give back the CPU copies of detail levels nothing has asked to draw. An imported
        // LOD 0 is the original scan and most are never picked in a session (DESIGN_SHIPS.md).
        { static double next_trim; double tnow = diag_now();
          if (tnow > next_trim) { next_trim = tnow + 5.0; asset_lod0_trim(ASSET_LOD0_IDLE_S); } }
        prof_begin(p_upload);
        { int ran = jobs_main_drain(JOBS_UPLOAD_BUDGET_MS);
          job_stats_t js; jobs_stats(&js);
          metric_set(m_jobs_main, ran);
          metric_set(m_jobs_queued, js.lane[JOB_LANE_WORKER].queued + js.lane[JOB_LANE_DECODE].queued
                                  + js.lane[JOB_LANE_IO].queued + js.lane[JOB_LANE_MAIN].queued);
          metric_set(m_jobs_io_q, js.lane[JOB_LANE_IO].queued);
          metric_set(m_jobs_wait, js.wait_ms);
          // pool utilisation over the last half second: busy time on the pool lanes / (window x workers)
          static double busy_prev, busy_t0; double busy_now = js.lane[JOB_LANE_WORKER].busy_ms + js.lane[JOB_LANE_DECODE].busy_ms;
          if (now - busy_t0 >= 0.5) { if (busy_t0 > 0 && js.workers > 0) metric_set(m_jobs_busy, 100.0 * (busy_now - busy_prev) / ((now - busy_t0) * 1000.0 * js.workers)); busy_prev = busy_now; busy_t0 = now; } }
        prof_end(p_upload);
        // A save or load that finished on the IO lane: the state swap (load) happens inside the
        // poll, here on the main thread between the simulation step and the scene build.
        { game_io_result_t r;
          static float io_worst_dt;                       // the evidence that a save does not hitch: the worst frame while the IO lane worked
          if (game_io_state() != GAME_IO_IDLE) io_worst_dt = maxf(io_worst_dt, dt);
          if (game_io_poll(&r)) {
              int autosave = strstr(r.path, "autosave") != NULL;
              LOG_INFO("main", "%s %s %s: worst frame while it was in flight %.1f ms (snapshot/apply %.2f ms, %.1f ms end to end)",
                       r.kind == GAME_IO_SAVE ? (autosave ? "autosave" : "save") : "load", r.path, r.ok ? "done" : "FAILED", io_worst_dt * 1000, r.snapshot_ms, r.ms);
              io_worst_dt = 0;
              if (r.kind == GAME_IO_SAVE) { if (r.ok) toast(autosave ? "Autosaved (%s)" : "Game saved (%s)", ui_date(G.day)); else toast("Save failed: see the log"); }
              else if (r.ok) { ui_select_fleet(0); scene_reset_caches();
                               if (U.in_menu) { int c = G.nations[G.player].capital; cam_reset(G.ports[c].x, G.ports[c].z, 60); U.in_menu = 0; }
                               toast("Game loaded (%s)", ui_date(G.day)); }
              else toast("No compatible saved game found");
          } }
        options_apply();                      // the sliders reach the systems that own them
        if (territory_dirty == 2) render_territory_upload();

        // ---- 3d
        RF.vp = cam.vp; RF.campos = cam.pos; RF.mapness = cam.mapness; RF.flatten = cam.mapness;
        RF.sea_center = vec3(cam.tx, 0, cam.tz);          // the sea's wave grid follows the look-at point
        render_sun_update(G.day);                         // sun direction, daylight and sky colour (day/night cycle)
        { static int freeze = -1; if (freeze < 0) { const char *e = getenv("MARE_SEA_FREEZE"); freeze = e ? atoi(e) != 0 : 0; if (freeze) LOG_INFO("gpu", "sea clocks frozen (MARE_SEA_FREEZE)"); }
          sea_update_waves(G.day, freeze ? 0.f : dt); }   // the wave set follows the wind at the look-at point
        entities_update(ENT_PHASE_FRAME, dt);             // weather (src/game/weather.cc): calm / haze / mist / storm from the wind, the storms and the hour
        sky_update(dt);                                   // cloud cover from the weather, drift with the wind (sky.c)
        RF.sky = lerp3(RF.sky, vec3(0.36f, 0.30f, 0.24f), cam.mapness);   // the map view fades to parchment
        render_light_update();
        rhi_frame_stats_reset();
        prof_begin(p_gfx);
        // scene geometry is built once, then submitted to the shadow cascades and the main passes
        lod_frame_begin((float)screen_h, 45.f * 0.0174533f, dt);   // screen-space LOD metric for this frame (camera.c: 45 degree vertical FOV)
        lod_set_tier(cam.dist, cam.mapness);                       // which kind of scene this is, and its triangle budget
        if (!U.in_menu) streamview_update();                       // world streaming: wanted set, tile loads, the terrain detail window
        occ_frame_begin(&RF.campos.x, terrain_max_height());        // terrain occlusion rays start from this eye (occlusion.h)
        occq_frame_begin();                                         // GPU occlusion answers from the last frames (occquery.h)
        if (!U.in_menu) {
            SH = scene_hints_t{ U.sel_fleet_id, U.hover_fleet_id, U.sel_port, U.hover_port, U.route_mode, U.show_lanes, U.show_wind, U.show_regions }; terrain_show_regions = U.show_regions; terrain_map_mode = U.map_mode;
            detailview_frame(dt);                 // transition clock + staged-scene animation (before the shadow pass)
            int dv_on = detailview_mode() != DV_OFF;
            // One authored-asset bracket around both builds: towns and ships share the instance
            // lists (shipyard.h puts hulls, masts and oars through the same path), so the
            // single upload has to happen once both have pushed.
            if (!dv_on) scene_assets_begin();
            prof_begin(p_ports); if (U.pass_ports && !dv_on) scene_build_ports(); prof_end(p_ports);
            prof_begin(p_ships); if (U.pass_ships && !dv_on) scene_build_ships(); prof_end(p_ships);
            if (!dv_on) scene_assets_end();
            prof_begin(p_shadow); rhi_gpu_begin(g_shadow); shadow_render(); rhi_gpu_end(); prof_end(p_shadow);
        }
        // the HDR target is cleared in linear space: the sky colour is authored in sRGB
        { vec3 skyl = srgb_to_linear3(RF.sky); hdr_begin(screen_w, screen_h, skyl.x, skyl.y, skyl.z); }
        sky_draw();                                       // the background: gradient, sun disc, clouds (sky.c)
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
            occq_flush();                       // bounding-box queries against the depth the opaque pass just wrote
        }
        // Ambient occlusion from the *opaque* depth, before the water draws: the sea occludes
        // nothing and should not be darkened by AO - computed after the water it read the
        // tessellated surface's patch-border derivatives and ruled the ocean into a faint grid
        // at distance (the staged battle showed it clearly; MARE_SSAO=0 confirmed the source).
        hdr_ao_pass();
        if (U.pass_sea && sea_refract) hdr_capture_scene();   // the frame so far: what the water refracts
        prof_begin(p_sea); rhi_gpu_begin(g_sea); if (U.pass_sea && !U.in_menu && dv_on) detailview_sea(); else if (U.pass_sea) sea_draw(); rhi_gpu_end(); prof_end(p_sea);
        if (!U.in_menu) sky_draw_overhead();   // the cloud layer from above, over the map (after the water, before the translucent passes)
        if (!U.in_menu) { meshes_bind_smoke(); if (dv_on) detailview_submit_smoke(); else if (U.pass_ships) scene_submit_smoke(); }   // translucent, after the water
        if (!U.in_menu && !dv_on) {
            prof_begin(p_over); if (U.pass_overlays) { scene_draw_overlays(); streamview_draw_debug(); dd_flush(cam.proj, cam.view); } prof_end(p_over);
            prof_begin(p_icons); if (U.pass_icons) { scene_push_icons(); icons_draw(); } prof_end(p_icons);
        }
        rhi_gpu_begin(g_resolve); hdr_bloom_pass(); hdr_resolve(screen_w, screen_h); rhi_gpu_end();   // tonemap to the backbuffer (AO was taken before the water); labels/panels draw on top
        if (!U.in_menu) detailview_clouds();                                        // the transition rolls over the finished frame
        if (!U.in_menu) entities_render(ENT_PHASE_OVERLAY, RF.mapness);             // rain over the finished frame (weather entity)
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
          if (lod_pops) LOG_DEBUG("lod", "%d multi-level jumps clamped this frame (cam dist %.1f, %d fading, %d assets loading): a camera teleport or a town rebuilt around a newly landed asset, walked one level per fade - not a visible pop", lod_pops, cam.dist, lod_fading, asset_loading_count());
          metric_set(m_lod_tris, (double)lod_last_tris); metric_set(m_lod_budget, lod_budget_scale); metric_set(m_icons_clustered, scene_icons_clustered);
          metric_set(m_lod_fallbacks, scene_asset_lod_fallbacks);
          metric_set(m_lod_trim, asset_lod0_trimmed_bytes() / (1024.0 * 1024.0));
          metric_set(m_occ_ports, scene_occluded_ports); metric_set(m_occ_ships, scene_occluded_ships); metric_set(m_occ_samples, occ_samples);
          metric_set(m_occq_hidden, scene_occluded_assets); metric_set(m_occq_queries, occq_queried); metric_set(m_occq_moving, occq_moving);
          metric_set(m_shadow_tris, (double)scene_shadow_tris);
          metric_set(m_shadow_ports, scene_shadow_ports); metric_set(m_shadow_fleets, scene_shadow_fleets); metric_set(m_shadow_radius, shadow_radius);
          metric_set(m_wx_rain, WX.rain); metric_set(m_wx_haze, WX.haze); metric_set(m_wx_calm, WX.calm); }
        ui_overlay_end();
        if (U.show_debug) rhi_check_errors("frame");

        // ---- panels
        prof_begin(p_panels);
        if (U.in_menu) {
            int pick = hud_menu();
            if (pick > 0) { game_new(pick, seed); scene_reset_caches(); int c = G.nations[G.player].capital; cam_reset(G.ports[c].x, G.ports[c].z, 60); U.in_menu = 0; U.show_help = 1; }
            else if (pick < 0) game_load_async(pick == -2 ? "mare_autosave.bin" : "mare_save.bin");   // applied by the poll above; the menu shows "Loading..." meanwhile
        } else { if (U.pass_panels) hud_panels(); dashboard_panel(); }
        prof_end(p_panels);
        prof_end(p_frame);
        prof_frame_end();
    }
    LOG_INFO("main", "window closed, shutting down");
    entities_unload();
    return 0;
}
