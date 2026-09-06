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

static int m_astar, m_nodes, m_fail, m_rows, p_astar, p_rows;
static void nav_metrics_init(void) { if (m_astar) return; m_astar = metric_id("nav.astar_calls"); m_nodes = metric_id("nav.astar_nodes"); m_fail = metric_id("nav.astar_unreachable"); m_rows = metric_id("nav.dijkstra_rows"); p_astar = prof_id("nav.astar"); p_rows = prof_id("nav.dijkstra"); }

unsigned char *navgrid;
unsigned char *navcoast;
static int *comp;   // water body id per nav cell

// search scratch (shared by A* and dijkstra; single threaded)
static float *g_cost; static int *parent; static unsigned *mark; static unsigned gen = 1;
static int *heap_idx; static float *heap_key; static int heap_n;
#define CLOSED (gen + 0x80000000u)
#define COAST_PENALTY 0.6f

static void heap_push(int idx, float key) {
    int i = heap_n++; heap_idx[i] = idx; heap_key[i] = key;
    while (i > 0) { int p = (i-1)/2; if (heap_key[p] <= heap_key[i]) break;
        int ti = heap_idx[p]; heap_idx[p] = heap_idx[i]; heap_idx[i] = ti; float tk = heap_key[p]; heap_key[p] = heap_key[i]; heap_key[i] = tk; i = p; }
}
static int heap_pop(void) {
    int r = heap_idx[0]; heap_n--; heap_idx[0] = heap_idx[heap_n]; heap_key[0] = heap_key[heap_n];
    int i = 0;
    for (;;) {
        int l = 2*i+1, rr = l+1, m = i;
        if (l < heap_n && heap_key[l] < heap_key[m]) m = l;
        if (rr < heap_n && heap_key[rr] < heap_key[m]) m = rr;
        if (m == i) break;
        int ti = heap_idx[m]; heap_idx[m] = heap_idx[i]; heap_idx[i] = ti; float tk = heap_key[m]; heap_key[m] = heap_key[i]; heap_key[i] = tk; i = m;
    }
    return r;
}

void nav_init(void) {
    double t0 = diag_now(); nav_metrics_init();
    navgrid  = calloc(NAV_W*NAV_H, 1);
    navcoast = calloc(NAV_W*NAV_H, 1);
    g_cost = calloc(NAV_W*NAV_H, sizeof(float)); parent = calloc(NAV_W*NAV_H, sizeof(int)); mark = calloc(NAV_W*NAV_H, sizeof(unsigned));
    heap_idx = calloc(NAV_W*NAV_H*2, sizeof(int)); heap_key = calloc(NAV_W*NAV_H*2, sizeof(float));
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
    comp = calloc(NAV_W*NAV_H, sizeof(int)); int *stack = heap_idx, ncomp = 0;
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
static void expand(int cur, int goal_x, int goal_y) {
    int cx = cur % NAV_W, cy = cur / NAV_W; float g = g_cost[cur];
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        int nx = cx+dx, ny = cy+dy; if (!nav_ok(nx,ny)) continue;
        if (dx && dy && (!nav_ok(cx+dx,cy) || !nav_ok(cx,cy+dy))) continue; // no corner cutting
        int n = ny*NAV_W+nx;
        if (mark[n] == CLOSED) continue;
        float ng = g + ((dx && dy) ? 1.41421356f : 1.f) + (navcoast[n] ? COAST_PENALTY : 0);
        if (mark[n] != gen || ng < g_cost[n]) {
            mark[n] = gen; g_cost[n] = ng; parent[n] = cur;
            float h = 0;
            if (goal_x >= 0) { int hx = abs(nx-goal_x), hy = abs(ny-goal_y); h = ((hx > hy) ? (hx - hy) + 1.41421356f*hy : (hy - hx) + 1.41421356f*hx) * 1.001f; }
            heap_push(n, ng + h);
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
    gen++; heap_n = 0;
    g_cost[start] = 0; parent[start] = -1; mark[start] = gen; heap_push(start, 0);
    int found = 0, iters = 0;
    while (heap_n) {
        int cur = heap_pop();
        if (cur == goal) { found = 1; break; }
        if (mark[cur] == CLOSED) continue;
        mark[cur] = CLOSED;
        if (++iters > 400000) break;
        expand(cur, x1, y1);
    }
    metric_add(m_nodes, iters);
    if (!found) { metric_add(m_fail, 1); prof_end(p_astar); LOG_DEBUG("nav", "unreachable %.1f,%.1f -> %.1f,%.1f (%d nodes)", x2lon(sx), z2lat(sz), x2lon(ex), z2lat(ez), iters); return -1; }
    float len = g_cost[goal] * CELL_UNITS;
    if (px && pz && npts) {
        static int raw[65536]; int nraw = 0;
        for (int c = goal; c >= 0 && nraw < 65536; c = parent[c]) raw[nraw++] = c;
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

static void dijkstra_row(int a) {
    const port_t *pa = &G.ports[a];
    double t0 = diag_now(); metric_add(m_rows, 1); prof_begin(p_rows);
    int x0, y0;
    if (!nav_nearest(pa->x, pa->z, &x0, &y0, 6)) { for (int b = 0; b < G.nports; ++b) dist_cache[a][b] = -2; row_ready[a] = 1; return; }
    int start = y0*NAV_W+x0;
    gen++; heap_n = 0;
    g_cost[start] = 0; mark[start] = gen; heap_push(start, 0);
    while (heap_n) {
        int cur = heap_pop();
        if (mark[cur] == CLOSED) continue;
        mark[cur] = CLOSED;
        expand(cur, -1, -1);
    }
    for (int b = 0; b < G.nports; ++b) {
        int bx, by;
        if (!nav_nearest(G.ports[b].x, G.ports[b].z, &bx, &by, 6)) { dist_cache[a][b] = -2; continue; }
        int c = by*NAV_W+bx;
        dist_cache[a][b] = (mark[c] == CLOSED) ? g_cost[c] * CELL_UNITS : -2;
    }
    row_ready[a] = 1;
    prof_end(p_rows);
    LOG_DEBUG("nav", "distance row for %s computed in %.1f ms", pa->name, (diag_now()-t0)*1000);
}

// rows are computed on first use (a few tens of ms each) so results never depend on
// cache state: the simulation stays deterministic across save/load
float nav_port_dist(int a, int b) {
    if (a == b) return 0;
    if (!row_ready[a]) dijkstra_row(a);   // (d(a,b) != d(b,a) slightly: coast penalties are paid on entry)
    return dist_cache[a][b];
}

void nav_ports_precompute(void) { for (int a = 0; a < G.nports; ++a) if (!row_ready[a]) dijkstra_row(a); }

// a settlement was appended at `idx`: compute its row and fill its column in the other rows
// (d(a,b) and d(b,a) differ only by coast penalties) instead of invalidating everything
void nav_ports_added(int idx) {
    if (idx < 0 || idx >= G.nports) return;
    row_ready[idx] = 0; dijkstra_row(idx);
    for (int a = 0; a < G.nports; ++a) if (a != idx && row_ready[a]) dist_cache[a][idx] = dist_cache[idx][a];
}
