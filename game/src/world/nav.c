// Navigation (see nav.h). Sections: binary heap · grid + water-body labelling · nearest/snap
// (body-aware variant for AI targets) · fine-grid line of sight · A* with shared expand() ·
// string pulling · port distance rows (Dijkstra, computed on first use, added incrementally).
// Instrumented: nav.astar_* and nav.dijkstra_rows metrics, nav.astar/nav.dijkstra scopes.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nav.h"
#include "world.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"

#define NAV_PRE_CHUNKS 8    // parallel chunks for nav_ports_precompute (each owns ~10 MB of scratch)

static int m_astar, m_nodes, m_fail, m_rows, p_astar, p_rows;
static void nav_metrics_init(void) { if (m_astar) return; m_astar = metric_id("nav.astar_calls"); m_nodes = metric_id("nav.astar_nodes"); m_fail = metric_id("nav.astar_unreachable"); m_rows = metric_id("nav.dijkstra_rows"); p_astar = prof_id("nav.astar"); p_rows = prof_id("nav.dijkstra"); }

unsigned char *navgrid;
unsigned char *navcoast;
static int *comp;   // water body id per nav cell

// Search scratch. A* and the lazily computed distance rows run on the main thread and share
// `S`; the precompute (nav_ports_precompute) gives every chunk of rows its own set, so several
// Dijkstras can run at once without a lock. The grid itself (navgrid/navcoast/comp) is built
// once and only read, and each row writes its own slice of dist_cache, so the rows are
// independent - and because a row is a pure function of the grid and the port positions, the
// result does not depend on how many threads computed it.
typedef struct nav_scratch_t {
    float *g_cost; int *parent; unsigned *mark; unsigned gen;
    int *heap_idx; float *heap_key; int heap_n;
} nav_scratch_t;
static nav_scratch_t S;
#define CLOSED(s) ((s)->gen + 0x80000000u)
#define COAST_PENALTY 0.6f

static int nav_scratch_alloc(nav_scratch_t *s) {
    s->g_cost = calloc(NAV_W*NAV_H, sizeof(float)); s->parent = calloc(NAV_W*NAV_H, sizeof(int)); s->mark = calloc(NAV_W*NAV_H, sizeof(unsigned));
    s->heap_idx = calloc(NAV_W*NAV_H*2, sizeof(int)); s->heap_key = calloc(NAV_W*NAV_H*2, sizeof(float));
    s->gen = 1; s->heap_n = 0;
    return s->g_cost && s->parent && s->mark && s->heap_idx && s->heap_key;
}
static void nav_scratch_free(nav_scratch_t *s) { free(s->g_cost); free(s->parent); free(s->mark); free(s->heap_idx); free(s->heap_key); memset(s, 0, sizeof *s); }

static void heap_push(nav_scratch_t *s, int idx, float key) {
    int i = s->heap_n++; s->heap_idx[i] = idx; s->heap_key[i] = key;
    while (i > 0) { int p = (i-1)/2; if (s->heap_key[p] <= s->heap_key[i]) break;
        int ti = s->heap_idx[p]; s->heap_idx[p] = s->heap_idx[i]; s->heap_idx[i] = ti; float tk = s->heap_key[p]; s->heap_key[p] = s->heap_key[i]; s->heap_key[i] = tk; i = p; }
}
static int heap_pop(nav_scratch_t *s) {
    int r = s->heap_idx[0]; s->heap_n--; s->heap_idx[0] = s->heap_idx[s->heap_n]; s->heap_key[0] = s->heap_key[s->heap_n];
    int i = 0;
    for (;;) {
        int l = 2*i+1, rr = l+1, m = i;
        if (l < s->heap_n && s->heap_key[l] < s->heap_key[m]) m = l;
        if (rr < s->heap_n && s->heap_key[rr] < s->heap_key[m]) m = rr;
        if (m == i) break;
        int ti = s->heap_idx[m]; s->heap_idx[m] = s->heap_idx[i]; s->heap_idx[i] = ti; float tk = s->heap_key[m]; s->heap_key[m] = s->heap_key[i]; s->heap_key[i] = tk; i = m;
    }
    return r;
}

void nav_init(void) {
    double t0 = diag_now(); nav_metrics_init();
    navgrid  = calloc(NAV_W*NAV_H, 1);
    navcoast = calloc(NAV_W*NAV_H, 1);
    nav_scratch_alloc(&S);
    // a nav cell is water if at least half of its sub-cells are water (keeps carved straits open)
    for (int ny = 0; ny < NAV_H; ++ny) for (int nx = 0; nx < NAV_W; ++nx) {
        int water = 0;
        for (int sy = 0; sy < NAV_DIV; ++sy) for (int sx = 0; sx < NAV_DIV; ++sx) water += !landmask[(ny*NAV_DIV+sy)*LM_W + nx*NAV_DIV+sx];
        navgrid[ny*NAV_W+nx] = water * 2 >= NAV_DIV*NAV_DIV;
    }
    for (int ny = 0; ny < NAV_H; ++ny) for (int nx = 0; nx < NAV_W; ++nx) {
        int c = 0;
        if (navgrid[ny*NAV_W+nx])
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                int xx = nx+dx, yy = ny+dy;
                if (xx < 0 || yy < 0 || xx >= NAV_W || yy >= NAV_H || !navgrid[yy*NAV_W+xx]) c = 1;
            }
        navcoast[ny*NAV_W+nx] = c;
    }
    // connected components: requests between different water bodies (lakes, the Black Sea,
    // the Red Sea basin...) are rejected in O(1) instead of exhausting the grid
    comp = calloc(NAV_W*NAV_H, sizeof(int)); int *stack = S.heap_idx, ncomp = 0;
    for (int i = 0; i < NAV_W*NAV_H; ++i) {
        if (!navgrid[i] || comp[i]) continue;
        int id = ++ncomp, sp = 0; stack[sp++] = i; comp[i] = id;
        while (sp) { int c = stack[--sp], cx = c % NAV_W, cy = c / NAV_W;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { int nx = cx+dx, ny = cy+dy; if (!nav_ok(nx,ny)) continue; int n = ny*NAV_W+nx; if (!comp[n]) { comp[n] = id; stack[sp++] = n; } } }
    }
    int water = 0; for (int i = 0; i < NAV_W*NAV_H; ++i) water += navgrid[i];
    LOG_INFO("nav", "navigation grid %dx%d built in %.1f ms: %d water cells in %d separate bodies", NAV_W, NAV_H, (diag_now()-t0)*1000, water, ncomp);
}

static void nav_cell_water_point(int nx, int ny, int *fx, int *fy);
static float fine_wx(int fx);
static float fine_wz(int fy);
static int nav_nearest_body(float wx, float wz, int *out_x, int *out_y, int maxr, int body) {   // body 0 = any
    int cx = nav_x(wx), cy = nav_y(wz);
    for (int r = 0; r <= maxr; ++r) {
        int best = -1; float bd = 1e9f;
        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
            if (abs(dx) != r && abs(dy) != r) continue;
            int x = cx+dx, y = cy+dy;
            if (nav_ok(x,y) && (!body || comp[y*NAV_W+x] == body)) { float d = (float)(dx*dx+dy*dy); if (d < bd) { bd = d; best = y*NAV_W+x; } }
        }
        if (best >= 0) { *out_x = best % NAV_W; *out_y = best / NAV_W; return 1; }
    }
    return 0;
}
int nav_nearest(float wx, float wz, int *out_x, int *out_y, int maxr) { return nav_nearest_body(wx, wz, out_x, out_y, maxr, 0); }
int nav_snap_reachable(float *wx, float *wz, int maxr, float from_x, float from_z) {
    int fx, fy; if (!nav_nearest(from_x, from_z, &fx, &fy, 6)) return 0;
    int nx, ny; if (!nav_nearest_body(*wx, *wz, &nx, &ny, maxr, comp[fy*NAV_W+fx])) return 0;
    int px, py; nav_cell_water_point(nx, ny, &px, &py);
    *wx = fine_wx(px); *wz = fine_wz(py); return 1;
}
int nav_snap(float *wx, float *wz, int maxr) {
    int nx, ny; if (!nav_nearest(*wx, *wz, &nx, &ny, maxr)) return 0;
    int fx, fy; nav_cell_water_point(nx, ny, &fx, &fy);
    *wx = fine_wx(fx); *wz = fine_wz(fy); return 1;
}

// line of sight over water, tested on the fine land mask so smoothed segments never cut a coast
static int fine_ok(int x, int y) { return x >= 0 && y >= 0 && x < LM_W && y < LM_H && !landmask[y*LM_W+x]; }
static int fine_los(int x0, int y0, int x1, int y1) {
    int dx = abs(x1-x0), dy = -abs(y1-y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx+dy;
    for (;;) {
        if (!fine_ok(x0,y0)) return 0;
        if (x0 == x1 && y0 == y1) return 1;
        int e2 = 2*err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
// centre of a nav cell, moved onto its nearest water sub-cell (fine grid coordinates)
static void nav_cell_water_point(int nx, int ny, int *fx, int *fy) {
    int cx = nx*NAV_DIV + NAV_DIV/2, cy = ny*NAV_DIV + NAV_DIV/2, best = -1, bd = 1<<30;
    for (int y = ny*NAV_DIV; y < (ny+1)*NAV_DIV; ++y) for (int x = nx*NAV_DIV; x < (nx+1)*NAV_DIV; ++x)
        if (fine_ok(x,y)) { int d = (x-cx)*(x-cx) + (y-cy)*(y-cy); if (d < bd) { bd = d; best = y*LM_W+x; } }
    if (best < 0) { *fx = cx; *fy = cy; } else { *fx = best % LM_W; *fy = best / LM_W; }
}
static float fine_wx(int fx) { return (LON0 + (fx + 0.5f) / CELLS_PER_DEG) * WORLD_K; }
static float fine_wz(int fy) { return -(LAT1 - (fy + 0.5f) / CELLS_PER_DEG) * WORLD_K; }

// expands `cur`; shared by A* (with heuristic to goal) and dijkstra (goal < 0)
static void expand(nav_scratch_t *s, int cur, int goal_x, int goal_y) {
    int cx = cur % NAV_W, cy = cur / NAV_W; float g = s->g_cost[cur];
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        int nx = cx+dx, ny = cy+dy; if (!nav_ok(nx,ny)) continue;
        if (dx && dy && (!nav_ok(cx+dx,cy) || !nav_ok(cx,cy+dy))) continue; // no corner cutting
        int n = ny*NAV_W+nx;
        if (s->mark[n] == CLOSED(s)) continue;
        float ng = g + ((dx && dy) ? 1.41421356f : 1.f) + (navcoast[n] ? COAST_PENALTY : 0);
        if (s->mark[n] != s->gen || ng < s->g_cost[n]) {
            s->mark[n] = s->gen; s->g_cost[n] = ng; s->parent[n] = cur;
            float h = 0;
            if (goal_x >= 0) { int hx = abs(nx-goal_x), hy = abs(ny-goal_y); h = ((hx > hy) ? (hx - hy) + 1.41421356f*hy : (hy - hx) + 1.41421356f*hx) * 1.001f; }
            heap_push(s, n, ng + h);
        }
    }
}

static const float CELL_UNITS = (float)NAV_DIV / CELLS_PER_DEG * WORLD_K;

float nav_path(float sx, float sz, float ex, float ez, float *px, float *pz, int *npts, int maxpts) {
    int x0, y0, x1, y1;
    if (npts) *npts = 0;
    metric_add(m_astar, 1); prof_begin(p_astar);
    if (!nav_nearest(sx, sz, &x0, &y0, 6) || !nav_nearest(ex, ez, &x1, &y1, 6)) { metric_add(m_fail, 1); prof_end(p_astar); LOG_DEBUG("nav", "no water near %.1f,%.1f -> %.1f,%.1f", x2lon(sx), z2lat(sz), x2lon(ex), z2lat(ez)); return -1; }
    int start = y0*NAV_W+x0, goal = y1*NAV_W+x1;
    if (comp[start] != comp[goal]) { metric_add(m_fail, 1); prof_end(p_astar); LOG_TRACE("nav", "different water bodies %.1f,%.1f -> %.1f,%.1f", x2lon(sx), z2lat(sz), x2lon(ex), z2lat(ez)); return -1; }
    S.gen++; S.heap_n = 0;
    S.g_cost[start] = 0; S.parent[start] = -1; S.mark[start] = S.gen; heap_push(&S, start, 0);
    int found = 0, iters = 0;
    while (S.heap_n) {
        int cur = heap_pop(&S);
        if (cur == goal) { found = 1; break; }
        if (S.mark[cur] == CLOSED(&S)) continue;
        S.mark[cur] = CLOSED(&S);
        if (++iters > 400000) break;
        expand(&S, cur, x1, y1);
    }
    metric_add(m_nodes, iters);
    if (!found) { metric_add(m_fail, 1); prof_end(p_astar); LOG_DEBUG("nav", "unreachable %.1f,%.1f -> %.1f,%.1f (%d nodes)", x2lon(sx), z2lat(sz), x2lon(ex), z2lat(ez), iters); return -1; }
    float len = S.g_cost[goal] * CELL_UNITS;
    if (px && pz && npts) {
        static int raw[65536]; int nraw = 0;
        for (int c = goal; c >= 0 && nraw < 65536; c = S.parent[c]) raw[nraw++] = c;
        for (int i = 0; i < nraw/2; ++i) { int t = raw[i]; raw[i] = raw[nraw-1-i]; raw[nraw-1-i] = t; }
        // greedy string pulling: each waypoint is a water point of its nav cell, segments are
        // checked on the fine land mask
        int n = 0, i = 0, ix, iy; nav_cell_water_point(raw[0]%NAV_W, raw[0]/NAV_W, &ix, &iy);
        while (i < nraw && n < maxpts) {
            int j = mini(nraw - 1, i + 120), jx, jy;
            for (;;) { nav_cell_water_point(raw[j]%NAV_W, raw[j]/NAV_W, &jx, &jy); if (j <= i+1 || fine_los(ix, iy, jx, jy)) break; j--; }
            px[n] = fine_wx(jx); pz[n] = fine_wz(jy); n++;
            if (j >= nraw-1) break;
            i = j; ix = jx; iy = jy;
        }
        if (n > 0) { px[n-1] = ex; pz[n-1] = ez; }
        *npts = n;
    }
    prof_end(p_astar);
    return len;
}

// ---------------------------------------------------------------------------
// port distance cache

static float dist_cache[MAX_PORTS][MAX_PORTS];
static unsigned char row_ready[MAX_PORTS];

void nav_ports_reset(void) { memset(row_ready, 0, sizeof row_ready); }

// `quiet` runs without the profiler scope or the log line: those are main-thread only, and the
// precompute accounts for the whole batch in one place instead.
static void dijkstra_row_into(nav_scratch_t *s, int a, int quiet) {
    const port_t *pa = &G.ports[a];
    double t0 = diag_now();
    if (!quiet) { metric_add(m_rows, 1); prof_begin(p_rows); }
    int x0, y0;
    if (!nav_nearest(pa->x, pa->z, &x0, &y0, 6)) { for (int b = 0; b < G.nports; ++b) dist_cache[a][b] = -2; row_ready[a] = 1; if (!quiet) prof_end(p_rows); return; }
    int start = y0*NAV_W+x0;
    s->gen++; s->heap_n = 0;
    s->g_cost[start] = 0; s->mark[start] = s->gen; heap_push(s, start, 0);
    while (s->heap_n) {
        int cur = heap_pop(s);
        if (s->mark[cur] == CLOSED(s)) continue;
        s->mark[cur] = CLOSED(s);
        expand(s, cur, -1, -1);
    }
    for (int b = 0; b < G.nports; ++b) {
        int bx, by;
        if (!nav_nearest(G.ports[b].x, G.ports[b].z, &bx, &by, 6)) { dist_cache[a][b] = -2; continue; }
        int c = by*NAV_W+bx;
        dist_cache[a][b] = (s->mark[c] == CLOSED(s)) ? s->g_cost[c] * CELL_UNITS : -2;
    }
    row_ready[a] = 1;
    if (!quiet) { prof_end(p_rows); LOG_DEBUG("nav", "distance row for %s computed in %.1f ms", pa->name, (diag_now()-t0)*1000); }
}
static void dijkstra_row(int a) { dijkstra_row_into(&S, a, 0); }

// rows are computed on first use (a few tens of ms each) so results never depend on
// cache state: the simulation stays deterministic across save/load
float nav_port_dist(int a, int b) {
    if (a == b) return 0;
    if (!row_ready[a]) dijkstra_row(a);   // (d(a,b) != d(b,a) slightly: coast penalties are paid on entry)
    return dist_cache[a][b];
}

// One Dijkstra over the whole water grid per port, ~15 ms each. Computed lazily these used to
// land inside simulation ticks as single-frame hitches scattered over the first minutes of play;
// computed here, in parallel, they cost a fraction of a second of loading and none afterwards.
// The rows are identical either way (a row is a pure function of the static grid and the port
// positions), so the simulation stays bit-for-bit what it was.
typedef struct nav_pre_t { int done; } nav_pre_t;
static void nav_pre_range(void *arg, int begin, int end, int slot) {
    (void)arg; (void)slot;
    nav_scratch_t s; memset(&s, 0, sizeof s);
    if (!nav_scratch_alloc(&s)) { nav_scratch_free(&s); for (int a = begin; a < end; ++a) if (!row_ready[a]) dijkstra_row_into(&S, a, 1); return; }
    for (int a = begin; a < end; ++a) if (!row_ready[a]) dijkstra_row_into(&s, a, 1);
    nav_scratch_free(&s);
}

void nav_ports_precompute(void) {
    int n = G.nports, todo = 0;
    for (int a = 0; a < n; ++a) todo += !row_ready[a];
    if (!todo) return;
    double t0 = diag_now();
    prof_begin(p_rows);
    // Each chunk allocates its own ~10 MB of search scratch, so the chunk count is also the
    // concurrency cap: 8 of them is a few hundred milliseconds of work and 80 MB at peak.
    int grain = (n + NAV_PRE_CHUNKS - 1) / NAV_PRE_CHUNKS;
    jobs_parallel_for(JOB_LANE_WORKER, nav_pre_range, NULL, n, grain < 1 ? 1 : grain);
    prof_end(p_rows);
    metric_add(m_rows, todo);
    LOG_INFO("nav", "precomputed %d port distance rows in %.0f ms on %d chunks (%d workers)",
             todo, (diag_now()-t0)*1000, NAV_PRE_CHUNKS, jobs_worker_count());
}

// a settlement was appended at `idx`: compute its row and fill its column in the other rows
// (d(a,b) and d(b,a) differ only by coast penalties) instead of invalidating everything
void nav_ports_added(int idx) {
    if (idx < 0 || idx >= G.nports) return;
    row_ready[idx] = 0; dijkstra_row(idx);
    for (int a = 0; a < G.nports; ++a) if (a != idx && row_ready[a]) dist_cache[a][idx] = dist_cache[idx][a];
}
