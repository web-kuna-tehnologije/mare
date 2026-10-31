// Roads (see roads.h). Sections: the route search · the network · the segment grid.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

extern "C" {
#include "roads.h"
#include "rivers.h"
#include "world.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
}

float *road_px, *road_pz, *road_hw;
unsigned char *road_cls;
int    road_npts, road_n;
road_t *roads;
bridge_t *road_bridges; int road_nbridges;

namespace mare::game::world {

class Roads {
public:
    int  generate();
    void shutdown();
    double last_day = -1e9;
private:
    // A* over the land-mask grid. Cost per step: the distance, times one plus the slope
    // crossed (uphill counts more than down - a cart road switchbacks up a hill), a fifth of
    // that along cells that already carry a road. Water is impassable: this is a land route.
    std::vector<float> g_cost; std::vector<int> parent; std::vector<unsigned> mark; unsigned gen = 0;
    std::vector<unsigned char> roadcell;
    struct heap_t { std::vector<float> key; std::vector<int> idx;
        void push(float k, int i) { key.push_back(k); idx.push_back(i); int c = (int)key.size() - 1;
            while (c > 0) { int p = (c - 1) / 2; if (key[p] <= key[c]) break; std::swap(key[p], key[c]); std::swap(idx[p], idx[c]); c = p; } }
        int pop() { int r = idx[0]; key[0] = key.back(); idx[0] = idx.back(); key.pop_back(); idx.pop_back();
            int c = 0, n = (int)key.size();
            for (;;) { int l = 2 * c + 1, m = c; if (l < n && key[l] < key[m]) m = l; if (l + 1 < n && key[l + 1] < key[m]) m = l + 1; if (m == c) break; std::swap(key[m], key[c]); std::swap(idx[m], idx[c]); c = m; }
            return r; }
        void clear() { key.clear(); idx.clear(); } bool empty() const { return key.empty(); } } heap;
    int route(int ax, int ay, int bx, int by, std::vector<int> &out);
    std::vector<float> px, pz, hw; std::vector<unsigned char> cls; std::vector<road_t> list; std::vector<bridge_t> bridges;
};

Roads &roads_sys() { static Roads r; return r; }

void Roads::shutdown() {
    free(road_px); free(road_pz); free(road_hw); free(road_cls); free(roads); free(road_bridges);
    road_px = road_pz = road_hw = nullptr; road_cls = nullptr; roads = nullptr; road_bridges = nullptr;
    road_npts = road_n = road_nbridges = 0;
}

int Roads::route(int ax, int ay, int bx, int by, std::vector<int> &out) {
    const int W = LM_W, H = LM_H;
    out.clear();
    if (ax < 0 || ay < 0 || bx < 0 || by < 0 || ax >= W || bx >= W || ay >= H || by >= H) return 0;
    if (abs(ax - bx) + abs(ay - by) > 420) return 0;             // a hinterland is tens of cells, not a continent
    static const int ox[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, oy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int start = ay * W + ax, goal = by * W + bx;
    gen++; heap.clear();
    g_cost[start] = 0; parent[start] = -1; mark[start] = gen; heap.push(0, start);
    const unsigned CLOSED = 0x80000000u | gen;
    int iters = 0, found = 0;
    while (!heap.empty()) {
        int cur = heap.pop();
        if (cur == goal) { found = 1; break; }
        if (mark[cur] == CLOSED) continue;
        mark[cur] = CLOSED;
        if (++iters > 120000) break;
        int cx = cur % W, cy = cur / W; float hc = heightmap[cur], g = g_cost[cur];
        for (int k = 0; k < 8; ++k) {
            int nx = cx + ox[k], ny = cy + oy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int n = ny * W + nx;
            if (!landmask[n] || mark[n] == CLOSED) continue;
            float dh = heightmap[n] - hc, step = k < 4 ? 1.f : 1.41421356f;
            float slope = fabsf(dh) / step;                                   // world units per cell
            float cost = step * (1.f + 9.f * slope + (dh > 0 ? 6.f * slope : 0.f));
            if (roadcell[n]) cost *= 0.55f;
            float ng = g + cost;
            if (mark[n] != gen || ng < g_cost[n]) {
                mark[n] = gen; g_cost[n] = ng; parent[n] = cur;
                int hx = abs(nx - bx), hy = abs(ny - by);
                float h = (hx > hy ? (hx - hy) + 1.41421356f * hy : (hy - hx) + 1.41421356f * hx) * 1.001f;
                heap.push(ng + h, n);
            }
        }
    }
    if (!found) return 0;
    for (int c = goal; c >= 0; c = parent[c]) out.push_back(c);
    std::reverse(out.begin(), out.end());
    return 1;
}

int Roads::generate() {
    shutdown();
    if (!heightmap || !landmask || G.nports <= 0) return 0;
    double t0 = diag_now();
    const int W = LM_W, N = LM_W * LM_H;
    if ((int)g_cost.size() != N) { g_cost.assign(N, 0.f); parent.assign(N, -1); mark.assign(N, 0); }
    roadcell.assign(N, 0);
    px.clear(); pz.clear(); hw.clear(); cls.clear(); list.clear(); bridges.clear();
    const float cell = WORLD_K / CELLS_PER_DEG;
    auto cell_of = [&](float wx, float wz, int *cx, int *cy) { *cx = (int)((x2lon(wx) - LON0) * CELLS_PER_DEG); *cy = (int)((LAT1 - z2lat(wz)) * CELLS_PER_DEG); };
    // the port of most influence in a region: size, the fort counting for a little
    auto port_weight = [&](const port_t *p) { return p->size > 0 ? (float)p->size + 0.4f * p->building[B_FORT] : 0.f; };
    std::vector<int> path;
    int fails = 0;
    auto add_route = [&](float ax, float az, float bx, float bz, int rcls, int from_inland, int to_port) {
        int cax, cay, cbx, cby; cell_of(ax, az, &cax, &cay); cell_of(bx, bz, &cbx, &cby);
        // the ends may sit on a water cell (a harbour's quay): walk to the nearest land cell
        auto to_land = [&](int *x, int *y) { for (int r = 0; r < 4; ++r) for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
            int xx = *x + dx, yy = *y + dy; if (xx >= 0 && yy >= 0 && xx < W && yy < LM_H && landmask[yy * W + xx]) { *x = xx; *y = yy; return; } } };
        to_land(&cax, &cay); to_land(&cbx, &cby);
        if (!route(cax, cay, cbx, cby, path) || path.size() < 2) { fails++; return; }
        if ((int)px.size() > ROAD_MAX_POINTS - 4000) return;
        road_t r; r.first = (int)px.size(); r.cls = rcls; r.from_inland = from_inland; r.to_port = to_port;
        std::vector<float> cx, cz;
        for (int c : path) { cx.push_back(lon2x(cx2lon(c % W))); cz.push_back(lat2z(cy2lat(c / W))); roadcell[c] = 1; }
        // one round of corner cutting: a cart road bends, it does not step
        std::vector<float> nx2, nz2; nx2.push_back(cx[0]); nz2.push_back(cz[0]);
        for (size_t i = 0; i + 1 < cx.size(); ++i) {
            nx2.push_back(cx[i] * 0.75f + cx[i + 1] * 0.25f); nz2.push_back(cz[i] * 0.75f + cz[i + 1] * 0.25f);
            nx2.push_back(cx[i] * 0.25f + cx[i + 1] * 0.75f); nz2.push_back(cz[i] * 0.25f + cz[i + 1] * 0.75f); }
        nx2.push_back(cx.back()); nz2.push_back(cz.back());
        float w = rcls == 2 ? 0.030f : rcls == 1 ? 0.020f : 0.012f;      // half-width, world units (~80 .. 200 m)
        for (size_t i = 0; i < nx2.size(); ++i) { px.push_back(nx2[i]); pz.push_back(nz2[i]); hw.push_back(w); cls.push_back((unsigned char)rcls); }
        // bridges: a path cell that is river (rivers.h river_mask) becomes a crossing
        for (size_t i = 1; i + 1 < path.size(); ++i) if (river_mask && river_mask[path[i]] && !river_mask[path[i - 1]] && (int)bridges.size() < ROAD_MAX_BRIDGES) {
            bridge_t b; b.x = lon2x(cx2lon(path[i] % W)); b.z = lat2z(cy2lat(path[i] / W));
            float ddx = lon2x(cx2lon(path[i + 1] % W)) - lon2x(cx2lon(path[i - 1] % W)), ddz = lat2z(cy2lat(path[i + 1] / W)) - lat2z(cy2lat(path[i - 1] / W));
            float l = sqrtf(ddx * ddx + ddz * ddz); b.dx = l > 1e-6f ? ddx / l : 1.f; b.dz = l > 1e-6f ? ddz / l : 0.f; b.hw = cell * 0.5f;
            bridges.push_back(b);
        }
        r.count = (int)px.size() - r.first; list.push_back(r);
    };
    // 1. the hubs of each region, chained by influence (the biggest first), so the main road
    //    exists before the farm tracks look for something to join
    for (int ri = 0; ri < G.nregions; ++ri) {
        const region_t *R = &G.regions[ri]; if (!R->alive || R->nhub < 2) continue;
        std::vector<int> hubs(R->hub, R->hub + R->nhub);
        std::sort(hubs.begin(), hubs.end(), [&](int a, int b) { return port_weight(&G.ports[a]) > port_weight(&G.ports[b]); });
        float load = R->throughput > 0.f ? clampf(R->traded_today / R->throughput, 0.f, 1.f) : 0.f;
        int rcls = load > 0.6f ? 2 : load > 0.2f ? 1 : 0;
        for (size_t k = 0; k + 1 < hubs.size(); ++k) {
            const port_t *a = &G.ports[hubs[k]], *b = &G.ports[hubs[k + 1]];
            if (a->size <= 0 || b->size <= 0) continue;
            add_route(a->lx ? a->lx : a->x, a->lz ? a->lz : a->z, b->lx ? b->lx : b->x, b->lz ? b->lz : b->z, rcls, -1, hubs[k]);
        }
    }
    // 2. every inland settlement to the port of most influence in its region
    for (int i = 0; i < G.ninland; ++i) {
        const inland_t *s = &G.inland[i]; if (!s->alive || s->region < 0 || s->region >= G.nregions) continue;
        const region_t *R = &G.regions[s->region]; if (R->nhub <= 0) continue;
        int best = -1; float bw = -1.f;
        for (int k = 0; k < R->nhub; ++k) { const port_t *p = &G.ports[R->hub[k]]; float w = port_weight(p) - 0.002f * dist2d(p->x, p->z, s->x, s->z); if (p->size > 0 && w > bw) { bw = w; best = R->hub[k]; } }
        if (best < 0) continue;
        const port_t *p = &G.ports[best];
        float load = R->throughput > 0.f ? clampf(R->traded_today / R->throughput, 0.f, 1.f) : 0.f;
        int rcls = s->output > 2.5f && load > 0.4f ? 1 : 0;              // a busy mine's road is a road; a farm's is a track
        add_route(s->x, s->z, p->lx ? p->lx : p->x, p->lz ? p->lz : p->z, rcls, i, best);
    }
    road_npts = (int)px.size(); road_n = (int)list.size(); road_nbridges = (int)bridges.size();
    road_px = (float *)malloc((road_npts ? road_npts : 1) * sizeof(float)); road_pz = (float *)malloc((road_npts ? road_npts : 1) * sizeof(float));
    road_hw = (float *)malloc((road_npts ? road_npts : 1) * sizeof(float)); road_cls = (unsigned char *)malloc(road_npts ? road_npts : 1);
    memcpy(road_px, px.data(), road_npts * sizeof(float)); memcpy(road_pz, pz.data(), road_npts * sizeof(float));
    memcpy(road_hw, hw.data(), road_npts * sizeof(float)); memcpy(road_cls, cls.data(), road_npts);
    roads = (road_t *)malloc((road_n ? road_n : 1) * sizeof(road_t)); memcpy(roads, list.data(), road_n * sizeof(road_t));
    road_bridges = (bridge_t *)malloc((road_nbridges ? road_nbridges : 1) * sizeof(bridge_t)); memcpy(road_bridges, bridges.data(), road_nbridges * sizeof(bridge_t));
    last_day = G.day;
    int c1 = 0, c2 = 0; for (const road_t &r : list) { c1 += r.cls == 1; c2 += r.cls == 2; }
    LOG_INFO("roads", "%d roads (%d tracks, %d roads, %d highways) from %d settlements and %d regions, %d points, %d bridges, %d unroutable, %.0f ms",
             road_n, road_n - c1 - c2, c1, c2, G.ninland, G.nregions, road_npts, road_nbridges, fails, (diag_now() - t0) * 1000);
    return road_n;
}

} // namespace mare::game::world

extern "C" {
int  roads_generate(void) { return mare::game::world::roads_sys().generate(); }
void roads_shutdown(void) { mare::game::world::roads_sys().shutdown(); }
double roads_last_build_day(void) { return mare::game::world::roads_sys().last_day; }

void roads_grid_build(road_grid_t *g, float x0, float z0, float extent, float cell) {
    memset(g, 0, sizeof *g);
    if (!road_npts) return;
    g->x0 = x0; g->z0 = z0; g->cell = cell; g->nx = g->nz = (int)ceilf(extent / cell) + 1;
    int nb = g->nx * g->nz;
    g->first = (int *)calloc(nb, sizeof(int)); g->count = (int *)calloc(nb, sizeof(int));
    std::vector<int> segs;
    auto bins_of = [&](int i, int *bx0, int *bz0, int *bx1, int *bz1) {
        float pad = road_hw[i] * 3.f + 0.05f;
        *bx0 = (int)floorf((fminf(road_px[i], road_px[i + 1]) - pad - x0) / cell); *bx1 = (int)floorf((fmaxf(road_px[i], road_px[i + 1]) + pad - x0) / cell);
        *bz0 = (int)floorf((fminf(road_pz[i], road_pz[i + 1]) - pad - z0) / cell); *bz1 = (int)floorf((fmaxf(road_pz[i], road_pz[i + 1]) + pad - z0) / cell);
        if (*bx0 < 0) *bx0 = 0; if (*bz0 < 0) *bz0 = 0; if (*bx1 > g->nx - 1) *bx1 = g->nx - 1; if (*bz1 > g->nz - 1) *bz1 = g->nz - 1; };
    for (int r = 0; r < road_n; ++r) for (int i = roads[r].first; i + 1 < roads[r].first + roads[r].count; ++i) {
        float pad = road_hw[i] * 3.f + 0.05f;
        if (fmaxf(road_px[i], road_px[i + 1]) + pad < x0 || fminf(road_px[i], road_px[i + 1]) - pad > x0 + extent || fmaxf(road_pz[i], road_pz[i + 1]) + pad < z0 || fminf(road_pz[i], road_pz[i + 1]) - pad > z0 + extent) continue;
        segs.push_back(i);
    }
    for (int i : segs) { int a, b, c, d; bins_of(i, &a, &b, &c, &d); for (int z = b; z <= d; ++z) for (int x = a; x <= c; ++x) g->count[z * g->nx + x]++; }
    int total = 0; for (int b = 0; b < nb; ++b) { g->first[b] = total; total += g->count[b]; }
    g->idx = (int *)malloc((total ? total : 1) * sizeof(int)); g->nidx = total;
    std::vector<int> fill(nb, 0);
    for (int i : segs) { int a, b, c, d; bins_of(i, &a, &b, &c, &d); for (int z = b; z <= d; ++z) for (int x = a; x <= c; ++x) { int bi = z * g->nx + x; g->idx[g->first[bi] + fill[bi]++] = i; } }
}
void roads_grid_free(road_grid_t *g) { free(g->first); free(g->count); free(g->idx); memset(g, 0, sizeof *g); }
int roads_grid_query(const road_grid_t *g, float wx, float wz, float *dist, float *hw, int *cls) {
    if (!g->idx || !g->nidx) return 0;
    int bx = (int)floorf((wx - g->x0) / g->cell), bz = (int)floorf((wz - g->z0) / g->cell);
    if (bx < 0 || bz < 0 || bx >= g->nx || bz >= g->nz) return 0;
    int bi = bz * g->nx + bx; float best = 1e9f; int bseg = -1;
    for (int k = 0; k < g->count[bi]; ++k) {
        int i = g->idx[g->first[bi] + k];
        float ax = road_px[i], az = road_pz[i], ex = road_px[i + 1] - ax, ez = road_pz[i + 1] - az;
        float l2 = ex * ex + ez * ez, t = l2 > 1e-10f ? clampf(((wx - ax) * ex + (wz - az) * ez) / l2, 0.f, 1.f) : 0.f;
        float qx = ax + ex * t - wx, qz = az + ez * t - wz, d = sqrtf(qx * qx + qz * qz);
        if (d < best) { best = d; bseg = i; }
    }
    if (bseg < 0) return 0;
    *dist = best; *hw = road_hw[bseg]; *cls = road_cls[bseg];
    return 1;
}
}
