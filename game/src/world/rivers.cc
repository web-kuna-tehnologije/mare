// Rivers (see rivers.h). Sections: the flood and the flow · accumulation and tracing · the
// polylines · navigation marks · the segment grid for the rings.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

extern "C" {
#include "rivers.h"
#include "nav.h"
#include "world.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
}

float *river_px, *river_pz, *river_hw, *river_dx, *river_dz;
unsigned char *river_cls;
unsigned char *river_mask;
int    river_npts, river_n;
river_t *rivers;

namespace mare::game::world {

class Rivers {
public:
    void generate();
    void shutdown();
private:
    // a binary heap of (height, cell) for the priority flood
    struct heap_t { std::vector<float> key; std::vector<int> idx;
        void push(float k, int i) { key.push_back(k); idx.push_back(i); int c = (int)key.size() - 1;
            while (c > 0) { int p = (c - 1) / 2; if (key[p] <= key[c]) break; std::swap(key[p], key[c]); std::swap(idx[p], idx[c]); c = p; } }
        int pop(float *k) { int r = idx[0]; *k = key[0]; key[0] = key.back(); idx[0] = idx.back(); key.pop_back(); idx.pop_back();
            int c = 0, n = (int)key.size();
            for (;;) { int l = 2 * c + 1, m = c; if (l < n && key[l] < key[m]) m = l; if (l + 1 < n && key[l + 1] < key[m]) m = l + 1; if (m == c) break; std::swap(key[m], key[c]); std::swap(idx[m], idx[c]); c = m; }
            return r; }
        bool empty() const { return key.empty(); } };
    std::vector<float> px, pz, hw, dx, dz; std::vector<unsigned char> cls; std::vector<river_t> list;
};

Rivers &rivers_sys() { static Rivers r; return r; }

void Rivers::shutdown() {
    free(river_px); free(river_pz); free(river_hw); free(river_dx); free(river_dz); free(river_cls); free(rivers); free(river_mask); river_mask = nullptr;
    river_px = river_pz = river_hw = river_dx = river_dz = nullptr; river_cls = nullptr; rivers = nullptr; river_npts = river_n = 0;
}

void Rivers::generate() {
    shutdown();
    if (!heightmap || !landmask) return;
    double t0 = diag_now();
    const int W = LM_W, H = LM_H, N = W * H;
    static const int ox[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, oy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    // --- the flood: every land cell is reached from the water in order of (filled) height, so
    // the cell it was reached from is its downstream neighbour and no cell is a pit
    std::vector<int> down(N, -1); std::vector<unsigned char> seen(N, 0); std::vector<float> filled(N, 0.f);
    std::vector<int> order; order.reserve(N / 2);
    heap_t heap;
    for (int i = 0; i < N; ++i) if (landmask[i]) {
        int x = i % W, y = i / W, edge = 0;
        for (int k = 0; k < 8 && !edge; ++k) { int nx = x + ox[k], ny = y + oy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || !landmask[ny * W + nx]) edge = 1; }
        if (edge) { filled[i] = heightmap[i]; heap.push(heightmap[i], i); seen[i] = 1; }
    }
    while (!heap.empty()) {
        float hc; int c = heap.pop(&hc);
        order.push_back(c);
        int x = c % W, y = c / W;
        for (int k = 0; k < 8; ++k) { int nx = x + ox[k], ny = y + oy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int n = ny * W + nx; if (!landmask[n] || seen[n]) continue;
            seen[n] = 1; down[n] = c;
            filled[n] = maxf(heightmap[n], hc + 1e-4f);     // a depression fills to its spill point
            heap.push(filled[n], n); }
    }
    // --- accumulation: upstream cells were reached later, so walking the order backwards adds
    // every cell's drainage into its downstream neighbour before that one is read
    std::vector<int> acc(N, 0);
    for (int i = 0; i < N; ++i) if (landmask[i]) acc[i] = 1;
    for (int i = (int)order.size() - 1; i >= 0; --i) { int c = order[i]; if (down[c] >= 0) acc[down[c]] += acc[c]; }
    // --- heads: river cells with no river flowing in
    std::vector<unsigned char> isriver(N, 0); int ncells = 0;
    for (int i = 0; i < N; ++i) if (landmask[i] && acc[i] >= RIVER_MIN_CELLS) { isriver[i] = 1; ncells++; }
    river_mask = (unsigned char *)calloc(N, 1);
    for (int i = 0; i < N; ++i) if (isriver[i] && acc[i] >= RIVER_MIN_CELLS * 3) river_mask[i] = 1;
    std::vector<unsigned char> hasin(N, 0);
    for (int i = 0; i < N; ++i) if (isriver[i] && down[i] >= 0) hasin[down[i]] = 1;
    std::vector<int> owner(N, -1);
    px.clear(); pz.clear(); hw.clear(); dx.clear(); dz.clear(); cls.clear(); list.clear();
    const float cell = WORLD_K / CELLS_PER_DEG;
    std::vector<int> heads; for (int i = 0; i < N; ++i) if (isriver[i] && !hasin[i]) heads.push_back(i);
    // the biggest catchments first, so a tributary joins a river that already exists
    std::sort(heads.begin(), heads.end(), [&](int a, int b) { int ta = a, tb = b; while (down[ta] >= 0) ta = down[ta]; while (down[tb] >= 0) tb = down[tb]; return acc[ta] > acc[tb]; });
    std::vector<int> trace;
    for (int hcell : heads) {
        if ((int)px.size() > RIVER_MAX_POINTS - 8000) break;
        trace.clear();
        int c = hcell, joined = -1;
        while (c >= 0) {
            trace.push_back(c);
            if (owner[c] >= 0) { joined = owner[c]; break; }          // into an existing river
            owner[c] = (int)list.size();
            int d = down[c];
            if (d < 0) {   // the mouth: one more point in the water so the channel reaches the sea
                int x = c % W, y = c / W, best = -1;
                for (int k = 0; k < 8; ++k) { int nx = x + ox[k], ny = y + oy[k]; if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue; if (!landmask[ny * W + nx]) { best = ny * W + nx; break; } }
                if (best >= 0) trace.push_back(best);
                break;
            }
            c = d;
        }
        if (trace.size() < 3) continue;
        river_t r; r.first = (int)px.size(); r.cells = acc[trace[trace.size() - (joined >= 0 ? 2 : 1)]]; r.navigable = 0;
        // cell centres, then one round of Chaikin corner cutting and a small meander, so the
        // 7 km stair becomes a curve; the half-width grows with the catchment along the way
        std::vector<float> cx, cz, ca;
        for (int t : trace) { cx.push_back(lon2x(cx2lon(t % W))); cz.push_back(lat2z(cy2lat(t / W))); ca.push_back((float)acc[t]); }
        for (int it = 0; it < 1; ++it) {
            std::vector<float> nx2, nz2, na2; nx2.push_back(cx[0]); nz2.push_back(cz[0]); na2.push_back(ca[0]);
            for (size_t i = 0; i + 1 < cx.size(); ++i) {
                nx2.push_back(cx[i] * 0.75f + cx[i + 1] * 0.25f); nz2.push_back(cz[i] * 0.75f + cz[i + 1] * 0.25f); na2.push_back(ca[i] * 0.75f + ca[i + 1] * 0.25f);
                nx2.push_back(cx[i] * 0.25f + cx[i + 1] * 0.75f); nz2.push_back(cz[i] * 0.25f + cz[i + 1] * 0.75f); na2.push_back(ca[i] * 0.25f + ca[i + 1] * 0.75f); }
            nx2.push_back(cx.back()); nz2.push_back(cz.back()); na2.push_back(ca.back());
            cx.swap(nx2); cz.swap(nz2); ca.swap(na2);
        }
        for (size_t i = 0; i < cx.size(); ++i) {
            float tx = (i + 1 < cx.size() ? cx[i + 1] : cx[i]) - (i > 0 ? cx[i - 1] : cx[i]);
            float tz = (i + 1 < cx.size() ? cz[i + 1] : cz[i]) - (i > 0 ? cz[i - 1] : cz[i]);
            float tl = sqrtf(tx * tx + tz * tz); if (tl < 1e-5f) { tx = 1; tz = 0; tl = 1; }
            tx /= tl; tz /= tl;
            float m = (world_fbm(cx[i] * 0.9f + 17.f, cz[i] * 0.9f + 5.f, 2, 0x51Eu) - 0.5f) * 0.35f * cell;   // meander: up to a sixth of a cell either side
            if (i == 0 || i + 1 == cx.size()) m = 0.f;
            // The class is per *point*, from the catchment there, not per river: the Danube is a
            // channel at Belgrade and a stream in the Black Forest, and the width has to follow
            // it down. The widths cross over smoothly so there is no step where a class changes.
            float a = ca[i];
            unsigned char pc = a >= (float)RIVER_DEEP_CELLS ? 1 : a >= (float)RIVER_NAV_CELLS ? 2 : 0;
            float hair = minf((0.010f + 0.011f * sqrtf(a / (float)RIVER_MIN_CELLS)) * 0.5f, 0.07f);
            float w;
            if (!rivers_carve_enabled()) w = hair;   // painted only: the true width, never the ship-scale channel
            else if (a >= (float)RIVER_NAV_CELLS) {
                float t = clampf((a - RIVER_NAV_CELLS) / (float)(RIVER_DEEP_CELLS - RIVER_NAV_CELLS), 0.f, 1.f);
                w = lerpf(RIVER_W_NAV, RIVER_W_DEEP, t);
            } else {
                float t = clampf((a - RIVER_NAV_CELLS * 0.45f) / (RIVER_NAV_CELLS * 0.55f), 0.f, 1.f);
                w = lerpf(hair, RIVER_W_NAV, t);
            }
            px.push_back(cx[i] - tz * m); pz.push_back(cz[i] + tx * m); hw.push_back(w); dx.push_back(tx); dz.push_back(tz); cls.push_back(pc);
        }
        r.count = (int)px.size() - r.first;
        if (r.cells >= RIVER_DEEP_CELLS) r.navigable = 1; else if (r.cells >= RIVER_NAV_CELLS) r.navigable = 2;
        list.push_back(r);
    }
    // --- the arrays the renderer reads
    river_npts = (int)px.size(); river_n = (int)list.size();
    river_px = (float *)malloc(river_npts * sizeof(float)); river_pz = (float *)malloc(river_npts * sizeof(float));
    river_hw = (float *)malloc(river_npts * sizeof(float)); river_dx = (float *)malloc(river_npts * sizeof(float)); river_dz = (float *)malloc(river_npts * sizeof(float));
    river_cls = (unsigned char *)malloc(river_npts ? river_npts : 1);
    memcpy(river_px, px.data(), river_npts * sizeof(float)); memcpy(river_pz, pz.data(), river_npts * sizeof(float));
    memcpy(river_hw, hw.data(), river_npts * sizeof(float)); memcpy(river_dx, dx.data(), river_npts * sizeof(float)); memcpy(river_dz, dz.data(), river_npts * sizeof(float));
    memcpy(river_cls, cls.data(), river_npts);
    rivers = (river_t *)malloc(river_n * sizeof(river_t)); memcpy(rivers, list.data(), river_n * sizeof(river_t));
    // --- navigation: the lower reaches of the great rivers, where the catchment is large
    // enough, become water cells of the nav grid (nav_init folds navriver in), by class
    if (!navriver) navriver = (unsigned char *)calloc(NAV_W * NAV_H, 1);
    int nnav = 0, ndeep = 0;
    for (int i = 0; i < N; ++i) if (isriver[i] && rivers_carve_enabled()) {   // no channel cut, no river open to ships
        int cls = acc[i] >= RIVER_DEEP_CELLS ? 1 : acc[i] >= RIVER_NAV_CELLS ? 2 : 0;
        if (!cls) continue;
        int nx = (i % W) / NAV_DIV, ny = (i / W) / NAV_DIV, n = ny * NAV_W + nx;
        if (!navriver[n] || cls < navriver[n]) { navriver[n] = (unsigned char)cls; }
        nnav++; ndeep += cls == 1;
    }
    int great = 0; for (const river_t &r : list) great += r.navigable == 1;
    {   // a sanity sample the log can be checked against: the Po basin
        int inbox = 0; float hwmax = 0; for (int i = 0; i < river_npts; ++i) { float lo = x2lon(river_px[i]), la = z2lat(river_pz[i]); if (lo > 9.f && lo < 13.f && la > 44.f && la < 46.f) { inbox++; hwmax = maxf(hwmax, river_hw[i]); } }
        LOG_INFO("rivers", "sample: %d river points in the Po basin (9..13E 44..46N), widest half-width %.3f units", inbox, hwmax);
    }
    LOG_INFO("rivers", "%d rivers from %d river cells (%d points), %d navigable to a merchantman, %d river cells open to ships (%d deep), %d heads, %.0f ms",
             river_n, ncells, river_npts, great, nnav, ndeep, (int)heads.size(), (diag_now() - t0) * 1000);
}

} // namespace mare::game::world

// ---------------------------------------------------------------------------
// the segment grid (C, so the ring composer's workers can call it without the class)

extern "C" {
int  rivers_carve_enabled(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("MARE_RIVER_CARVE"); v = e && atoi(e) != 0 ? 1 : 0;
                 LOG_INFO("rivers", "river channels %s (MARE_RIVER_CARVE)", v ? "carved into the ground" : "painted on the surface, nothing carved"); }
    return v;
}
void rivers_generate(void) { mare::game::world::rivers_sys().generate(); }
void rivers_shutdown(void) { mare::game::world::rivers_sys().shutdown(); }

void rivers_grid_build(river_grid_t *g, float x0, float z0, float extent, float cell) {
    memset(g, 0, sizeof *g);
    if (!river_npts) return;
    g->x0 = x0; g->z0 = z0; g->cell = cell;
    g->nx = g->nz = (int)ceilf(extent / cell) + 1;
    int nb = g->nx * g->nz;
    g->first = (int *)calloc(nb, sizeof(int)); g->count = (int *)calloc(nb, sizeof(int));
    // two passes: count the segments per bin (a segment goes into every bin its bbox touches,
    // widened by its half-width and the bank), then fill
    std::vector<int> segs;
    for (int r = 0; r < river_n; ++r) for (int i = rivers[r].first; i + 1 < rivers[r].first + rivers[r].count; ++i) {
        float ax = river_px[i], az = river_pz[i], bx = river_px[i + 1], bz = river_pz[i + 1];
        float pad = river_hw[i] * 3.f + 0.05f;
        if (fmaxf(ax, bx) + pad < x0 || fminf(ax, bx) - pad > x0 + extent || fmaxf(az, bz) + pad < z0 || fminf(az, bz) - pad > z0 + extent) continue;
        segs.push_back(i);
    }
    auto bins_of = [&](int i, int *bx0, int *bz0, int *bx1, int *bz1) {
        float pad = river_hw[i] * 3.f + 0.05f;
        *bx0 = (int)floorf((fminf(river_px[i], river_px[i + 1]) - pad - x0) / cell); *bx1 = (int)floorf((fmaxf(river_px[i], river_px[i + 1]) + pad - x0) / cell);
        *bz0 = (int)floorf((fminf(river_pz[i], river_pz[i + 1]) - pad - z0) / cell); *bz1 = (int)floorf((fmaxf(river_pz[i], river_pz[i + 1]) + pad - z0) / cell);
        if (*bx0 < 0) *bx0 = 0; if (*bz0 < 0) *bz0 = 0; if (*bx1 > g->nx - 1) *bx1 = g->nx - 1; if (*bz1 > g->nz - 1) *bz1 = g->nz - 1; };
    for (int i : segs) { int a, b, c, d; bins_of(i, &a, &b, &c, &d); for (int z = b; z <= d; ++z) for (int x = a; x <= c; ++x) g->count[z * g->nx + x]++; }
    int total = 0; for (int b = 0; b < nb; ++b) { g->first[b] = total; total += g->count[b]; }
    g->idx = (int *)malloc((total ? total : 1) * sizeof(int)); g->nidx = total;
    std::vector<int> fill(nb, 0);
    for (int i : segs) { int a, b, c, d; bins_of(i, &a, &b, &c, &d); for (int z = b; z <= d; ++z) for (int x = a; x <= c; ++x) { int bi = z * g->nx + x; g->idx[g->first[bi] + fill[bi]++] = i; } }
}
void rivers_grid_free(river_grid_t *g) { free(g->first); free(g->count); free(g->idx); memset(g, 0, sizeof *g); }

int rivers_grid_query(const river_grid_t *g, float wx, float wz, float *dist, float *hw, float *dx, float *dz, int *cls) {
    if (!g->idx || !g->nidx) return 0;
    int bx = (int)floorf((wx - g->x0) / g->cell), bz = (int)floorf((wz - g->z0) / g->cell);
    if (bx < 0 || bz < 0 || bx >= g->nx || bz >= g->nz) return 0;
    int bi = bz * g->nx + bx; float best = 1e9f; int bseg = -1; float bt = 0;
    for (int k = 0; k < g->count[bi]; ++k) {
        int i = g->idx[g->first[bi] + k];
        float ax = river_px[i], az = river_pz[i], ex = river_px[i + 1] - ax, ez = river_pz[i + 1] - az;
        float l2 = ex * ex + ez * ez, t = l2 > 1e-10f ? clampf(((wx - ax) * ex + (wz - az) * ez) / l2, 0.f, 1.f) : 0.f;
        float qx = ax + ex * t - wx, qz = az + ez * t - wz, d = sqrtf(qx * qx + qz * qz);
        if (d < best) { best = d; bseg = i; bt = t; }
    }
    if (bseg < 0) return 0;
    *dist = best; *hw = lerpf(river_hw[bseg], river_hw[bseg + 1], bt);
    if (cls) { int a = river_cls[bseg], b = river_cls[bseg + 1];
               *cls = a && b ? (a < b ? a : b) : (a ? a : b); }   // the deeper class wins across a transition
    float fx = lerpf(river_dx[bseg], river_dx[bseg + 1], bt), fz = lerpf(river_dz[bseg], river_dz[bseg + 1], bt), fl = sqrtf(fx * fx + fz * fz);
    if (fl > 1e-6f) { fx /= fl; fz /= fl; } else { fx = 1; fz = 0; }
    *dx = fx; *dz = fz;
    return 1;
}
}
