// Region simulation (see region.h). Sections: setup · inland settlements · daily tick
// (production, consumption, hub links, development, prosperity, hub shares).
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "region.h"
#include "economy.h"
#include "settlement.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/regions.h"

const char *va_name(int good, int kind, int n);
static int is_rare(int g) { return g >= G_IRON; }
static int is_staple(int g) { return g == G_GRAIN || g == G_FISH || g == G_TIMBER || g == G_SALT || g == G_WINE; }

// a land spot inside region r, at least 1 TERR cell from the coast, near the region's centroid
static int inland_spot(int r, float *x, float *z) {
    const region_t *R = &G.regions[r]; float best = -1;
    for (int i = 0; i < TERR_W*TERR_H; ++i) { if ((int)regionmap[i] - 1 != r) continue;
        int cx = i % TERR_W, cy = i / TERR_W; int inland = 1;
        for (int dy = -1; dy <= 1 && inland; ++dy) for (int dx = -1; dx <= 1; ++dx) { int xx = cx+dx, yy = cy+dy; if (xx < 0 || yy < 0 || xx >= TERR_W || yy >= TERR_H || !regionmap[yy*TERR_W+xx]) { inland = 0; break; } }
        if (!inland) continue;
        float wx = lon2x(cx2lon(cx*TERR_DIV+TERR_DIV/2)), wz = lat2z(cy2lat(cy*TERR_DIV+TERR_DIV/2));
        float score = 10 - dist2d(wx, wz, R->cx, R->cz) * 0.1f + world_vnoise(wx*0.3f, wz*0.3f, 901) * 3;
        for (int k = 0; k < G.ninland; ++k) if (G.inland[k].alive && dist2d(G.inland[k].x, G.inland[k].z, wx, wz) < 3) score -= 20;
        if (score > best) { best = score; *x = wx; *z = wz; } }
    return best > -10;
}

int region_found_inland(int r, int good, int kind, int nation, const char *name) {
    if (G.ninland >= MAX_INLAND || r < 0) return -1;
    float x = 0, z = 0; if (!inland_spot(r, &x, &z)) return -1;
    inland_t *s = &G.inland[G.ninland]; memset(s, 0, sizeof *s);
    s->alive = 1; s->region = r; s->good = good; s->kind = kind; s->x = x; s->z = z; s->pop = 1.0f;
    snprintf(s->name, sizeof s->name, "%s", name ? name : va_name(good, kind, G.ninland));
    (void)nation;
    return G.ninland++;
}
// small deterministic naming helper (kept out of the header)
const char *va_name(int good, int kind, int n) { static char b[NAME_LEN]; snprintf(b, sizeof b, "%s %s %d", goods[good].name, kind == INLAND_MINE ? "Mine" : kind == INLAND_FURPOST ? "Post" : "Farms", n % 100); return b; }

void regions_setup(void) {
    G.ninland = 0;
    for (int r = 0; r < G.nregions; ++r) { region_t *R = &G.regions[r]; if (!R->alive) continue;
        float hubpop = 0; int colonial = 1; for (int k = 0; k < R->nhub; ++k) { hubpop += G.ports[R->hub[k]].pop; if (!port_colonial(&G.ports[R->hub[k]])) colonial = 0; }
        R->pop = R->nhub ? hubpop * (colonial ? 0.6f : 1.2f) : 0;
        R->development = !R->nhub ? 0.2f : colonial ? (hubpop > 20 ? 1.2f : 0.6f) : 2.2f;
        R->throughput = REGION_THROUGHPUT_BASE + REGION_THROUGHPUT_DEV * R->development;
        R->prosperity = 1; for (int g = 0; g < MAX_GOODS; ++g) { R->export_pool[g] = 0; R->import_need[g] = 0; }
        // initial inland settlements: rare deposits (pins/noise) where the region is at least frontier, plus one farm in settled regions
        for (int g = G_IRON; g <= G_GOLD; ++g) if (R->potential[g] >= 0.8f && R->development >= 1) { int i = region_found_inland(r, g, INLAND_MINE, -1, 0); if (i >= 0) G.inland[i].pop = 1 + R->potential[g]; }
        if (R->development >= 2) { int bg = -1; float bp = 0; for (int g = 0; g < G_IRON; ++g) if (is_staple(g) && g != G_FISH && R->potential[g] > bp) { bp = R->potential[g]; bg = g; } if (bg >= 0) region_found_inland(r, bg, INLAND_FARM, -1, 0); }
    }
    regions_refresh_devmap();
    LOG_INFO("regions", "%d regions populated, %d inland settlements", G.nregions, G.ninland);
}

float region_attractiveness(int r) {
    if (r < 0 || r >= G.nregions) return 0;
    const region_t *R = &G.regions[r]; float pot = 0;
    for (int g = 0; g < MAX_GOODS; ++g) pot += R->potential[g] * (is_rare(g) ? 1.5f : 1.f);
    return pot * (1 + R->development) * (R->nhub >= MAX_REGION_PORTS ? 0.3f : 1.f);
}

int region_consumption_bonus(const port_t *p, int g, float *extra_pop) {
    *extra_pop = 0; if (p->hinterland < 0) return 0;
    const region_t *R = &G.regions[p->hinterland]; *extra_pop = R->pop * p->hub_share * 0.3f; (void)g; return 1;
}

static float region_consumption(const region_t *R, int g, int colonial) {
    float c = goods[g].consume * R->pop * REGION_CONSUME_MUL;
    if (is_staple(g) || g == G_COAL) return 0;                  // the countryside feeds and heats itself
    if (colonial && (g == G_CLOTH || g == G_ARMS)) c *= 3;
    if (g == G_SUGAR || g == G_TOBACCO || g == G_SPICES || is_rare(g)) c *= colonial ? 0.1f : 0.6f;
    return c;
}

void regions_tick(float dt) {
    static int p_s = -1, m_tr; if (p_s < 0) { p_s = prof_id("sim.regions"); m_tr = metric_id("region.inland_trade"); }
    prof_begin(p_s);
    int day_changed = (int)G.day != (int)(G.day - dt);
    for (int r = 0; r < G.nregions; ++r) {
        region_t *R = &G.regions[r]; if (!R->alive || R->nhub == 0) continue;
        int colonial = 1; for (int k = 0; k < R->nhub; ++k) if (!port_colonial(&G.ports[R->hub[k]])) colonial = 0;
        float devmul = 0.5f + 0.5f * R->development / 3.f;
        // 1. rural production of staples and the settled-area crops
        for (int g = 0; g < MAX_GOODS; ++g) if (R->potential[g] > 0 && !is_rare(g)) R->export_pool[g] += REGION_RURAL_RATE * R->pop * R->potential[g] * devmul * dt;
        // 2. inland settlements
        for (int i = 0; i < G.ninland; ++i) { inland_t *s = &G.inland[i]; if (!s->alive || s->region != r) continue;
            float rate = s->kind == INLAND_MINE ? MINE_RATE : FARM_RATE;
            s->output = rate * maxf(R->potential[s->good], 0.3f) * (0.5f + 0.5f * s->pop) * clampf(R->satisfaction + 0.5f, 0.5f, 1.f);
            R->export_pool[s->good] += s->output * dt;
            float cap = 3 + 6 * R->development; if (s->pop < cap) s->pop += 0.0003f * (R->prosperity) * dt; }
        // 3. hub links
        R->throughput = REGION_THROUGHPUT_BASE + REGION_THROUGHPUT_DEV * R->development;
        float moved = 0, met = 0, needed = 0;
        for (int k = 0; k < R->nhub; ++k) { port_t *P = &G.ports[R->hub[k]]; float cap_out = R->throughput * P->hub_share * dt, cap_in = cap_out;   // carts travel both ways
            for (int g = 0; g < MAX_GOODS; ++g) { float q = minf(R->export_pool[g] * P->hub_share, cap_out); if (q <= 0) continue;
                R->export_pool[g] -= q; P->stock[g] += q; cap_out -= q; moved += q; if (day_changed) P->region_in[g] = 0; P->region_in[g] += q / dt / 2; }
            for (int g = 0; g < MAX_GOODS; ++g) { float need = region_consumption(R, g, colonial) * P->hub_share * dt; if (need <= 0) continue; needed += need;
                float q = minf(minf(need, cap_in), P->stock[g] * 0.2f); if (q <= 0) continue;
                P->stock[g] -= q; cap_in -= q; moved += q; met += q;
                double tax = q * P->price[g] * REGION_INLAND_TAX; if (P->nation != NATION_NONE && P->nation != NATION_PIRATES) { G.nations[P->nation].treasury += tax; if (is_player(P->nation)) G.nations[P->nation].income_acc += tax; } } }
        for (int g = 0; g < MAX_GOODS; ++g) { float norm = 30 * REGION_RURAL_RATE * R->pop * R->potential[g]; if (R->export_pool[g] > norm + 50) R->export_pool[g] -= (R->export_pool[g] - norm - 50) * 0.05f * dt; }
        R->satisfaction = needed > 0 ? met / needed : 1;
        for (int g = 0; g < MAX_GOODS; ++g) R->import_need[g] = region_consumption(R, g, colonial) * (1 - R->satisfaction);
        R->traded_today += moved; metric_add(m_tr, moved);
        // 4. development & prosperity (daily)
        if (day_changed) {
            float traded = R->traded_today / (R->pop * 0.1f + 10); R->traded_today = 0;
            int markets = 0; for (int k = 0; k < R->nhub; ++k) markets += G.ports[R->hub[k]].building[B_MARKET] > 0;
            float target = clampf(0.9f * log2f(1 + traded * 8) + 0.5f * R->prosperity + 0.4f * markets, 0, 3);
            float before = R->development; R->development += (target - R->development) * REGION_DEV_RATE * 1.0f;
            if ((int)before != (int)R->development && R->development > before) { log_event_at(EV_INFO, G.ports[R->hub[0]].nation, is_player(G.ports[R->hub[0]].nation), R->cx, R->cz, "The %s%s has become %s", R->name, strstr(R->name, "Hinterland") ? "" : " hinterland", dev_tier_name(R->development));
                for (int g = G_IRON; g <= G_GOLD; ++g) if (R->potential[g] >= 0.6f) { int have = 0; for (int i = 0; i < G.ninland; ++i) have += G.inland[i].alive && G.inland[i].region == r && G.inland[i].good == g; if (!have && region_found_inland(r, g, INLAND_MINE, -1, 0) >= 0) log_event_at(EV_STRIKE, G.ports[R->hub[0]].nation, 0, R->cx, R->cz, "A %s mine opens in the %s", goods[g].name, R->name); } }
            float ptarget = 0.4f + 0.9f * R->satisfaction + 0.3f * R->development / 3 + 0.2f * minf(traded, 1);
            R->prosperity = clampf(R->prosperity + (ptarget - R->prosperity) * 0.02f, 0.25f, 2.2f);
            R->pop *= 1 + 0.00008f * (R->prosperity - 0.8f);
            // hub shares follow prosperity and traffic
            float tot = 0, w[MAX_REGION_PORTS]; for (int k = 0; k < R->nhub; ++k) { const port_t *P = &G.ports[R->hub[k]]; w[k] = P->prosperity * (1 + P->traffic) * (1 + 0.5f * P->building[B_MARKET]); tot += w[k]; }
            for (int k = 0; k < R->nhub; ++k) G.ports[R->hub[k]].hub_share = tot > 0 ? lerpf(G.ports[R->hub[k]].hub_share, w[k] / tot, 0.05f) : 1.f / R->nhub;
        }
    }
    if (day_changed && ((int)G.day % 30) == 0) regions_refresh_devmap();
    prof_end(p_s);
}
