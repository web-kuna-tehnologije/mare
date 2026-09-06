// Markets and trade (see economy.h for the model overview).
// Sections: queries · market model (consumption with region/luxury/elasticity modifiers, production
// with hinterland staples and buildings, norm, price curve, tariffs) · fleets as agents (buy/sell
// against the curve, wallets, tariffs to the owner) · ownership · daily port tick · price history.
// Performance note: the norm of each (port, good) is cached per tick because the AI planner asks
// for prices tens of thousands of times per plan.
#include <string.h>
#include <math.h>
#include "economy.h"
#include "settlement.h"
#include "region.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../data/tables.h"
#include "../world/world.h"

// ---------------------------------------------------------------------------
// queries

int is_player(int nation) { return nation == G.player; }
int at_war(int a, int b) { if (a == b) return 0; if (a == NATION_PIRATES || b == NATION_PIRATES) return 1; return G.nations[a].relation[b]; }
int port_colonial(const port_t *p) {
    return p->region == REG_CARIB || p->region == REG_NAMER || p->region == REG_SAMER || p->region == REG_AFRICA || p->region == REG_PACIFIC || p->region == REG_ARCTIC;
}
int port_random(int min_size, int region) {
    int cand[MAX_PORTS], n = 0;
    for (int i = 0; i < G.nports; ++i) if (G.ports[i].size >= min_size && (region < 0 || G.ports[i].region == region)) cand[n++] = i;
    return n ? cand[rnd_int(&G.rng, n)] : -1;
}
int port_random_of(int nation, int min_size) {
    int cand[MAX_PORTS], n = 0;
    for (int i = 0; i < G.nports; ++i) if (G.ports[i].size >= min_size && G.ports[i].nation == nation) cand[n++] = i;
    return n ? cand[rnd_int(&G.rng, n)] : -1;
}
int port_nearest(float x, float z, int nation, int settled_only, int friendly_to) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < G.nports; ++i) {
        const port_t *p = &G.ports[i];
        if (settled_only && p->size <= 0) continue;
        if (port_is_gone(p)) continue;
        if (nation >= 0 && p->nation != nation) continue;
        if (friendly_to >= 0 && (at_war(friendly_to, p->nation) || (p->blockaded_by && p->blockaded_by != friendly_to))) continue;
        float d = dist2d(x, z, p->x, p->z); if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int port_by_name(const char *name) { for (int i = 0; i < G.nports; ++i) if (!strcmp(G.ports[i].name, name)) return i; return -1; }

// ---------------------------------------------------------------------------
// market model

float port_consumption(const port_t *p, int g) {
    if (p->size <= 0) return 0;
    const good_info_t *gi = &goods[g];
    float extra = 0; region_consumption_bonus(p, g, &extra);   // the hinterland shops through its hub
    float c = gi->consume * (p->pop + extra), pr = p->prosperity;
    if (gi->luxury == 1) c *= 0.6f + 0.5f*pr;
    if (gi->luxury == 2) c *= 0.4f + 0.6f*pr;
    if (port_colonial(p)) {                       // colonies import manufactures and staples they lack
        if (g == G_CLOTH || g == G_ARMS) c *= 6.f;
        if (g == G_WINE || g == G_SALT) c *= 3.5f;
        if (g == G_GRAIN) c *= 1.5f;
        if (g == G_SUGAR || g == G_TOBACCO || g == G_SPICES || g == G_BULLION) c *= 0.25f;
        if (g == G_FURS) c *= 0.1f;
    } else {                                      // europe craves colonial goods
        if (g == G_SUGAR || g == G_TOBACCO || g == G_SPICES || g == G_FURS) c *= 1.6f;
        if (g == G_BULLION) c *= 1.4f;
        if (g == G_FURS && p->lat < 42) c *= 0.5f;
        if (g == G_TIMBER && (p->region == REG_ATLANTIC || p->region == REG_MED)) c *= 1.6f;
    }
    if (g == G_ARMS) c += p->building[B_FORT] * 0.05f + (p->blockaded_by ? 0.1f : 0);
    if (g >= G_IRON) { if (port_colonial(p)) c *= 0.35f; if (g == G_COAL && fabsf(p->lat) < 45) c *= 0.3f; if ((g == G_IRON || g == G_COPPER) && p->building[B_DOCKS]) c *= 1 + 0.5f * p->building[B_DOCKS]; }
    c *= p->cons_mul[g];
    if (p->price[g] > 0) c *= clampf(powf(gi->base_price / p->price[g], DEMAND_ELASTICITY), 0.35f, 1.6f);
    return c;
}

float port_production(const port_t *p, int g) {
    if (p->size <= 0) return 0;
    float r = p->produce[g] * p->prod_mul[g];
    if (g == G_GRAIN || g == G_FISH || g == G_TIMBER || g == G_SALT) {   // staples: the region's export pool supplies hubs (region.c);
        float base = goods[g].consume * p->pop * (port_colonial(p) && g == G_GRAIN ? 1.5f : 1.f);                  // ports without a hinterland keep a small local share
        float share = p->hinterland < 0 ? (g == G_FISH ? 0.75f : 0.4f) : (g == G_FISH ? 0.5f : 0.2f);
        r += base * share * p->prod_mul[g];
    }
    if (g == G_ARMS && p->stock[G_IRON] > 0) r *= 1 + 0.5f * clampf(p->stock[G_IRON] / 100.f, 0, 1);   // iron feeds the armouries
    if ((g == G_SUGAR || g == G_TOBACCO) && p->building[B_PLANTATION]) r *= 1.f + 0.5f*p->building[B_PLANTATION];
    if (g == G_BULLION && p->building[B_MINE]) r *= 1.f + 0.6f*p->building[B_MINE];
    if (g == G_FISH && p->building[B_DOCKS]) r *= 1.f + 0.25f*p->building[B_DOCKS];
    if (p->blockaded_by && g == G_FISH) r *= 0.3f;
    if (p->price[g] > 0 && p->produce[g] > 0) r *= clampf(powf(p->price[g] / goods[g].base_price, SUPPLY_ELASTICITY), 0.45f, 1.3f);
    return r;
}

float port_norm(const port_t *p, int g) {
    float n = port_consumption(p, g) * NORM_CONS_DAYS + port_production(p, g) * NORM_PROD_DAYS;
    n *= 1.f + 0.5f * p->building[B_WAREHOUSE];
    return maxf(n, NORM_FLOOR);
}
// the norm of every (port, good) only changes with the daily tick, yet the AI planner asks
// for prices tens of thousands of times per plan: cache it per tick (refreshed in port_update_prices)
static float norm_cache[MAX_PORTS][MAX_GOODS];
static inline float port_norm_cached(const port_t *p, int g) { int i = (int)(p - G.ports); float n = norm_cache[i][g]; return n > 0 ? n : port_norm(p, g); }

float price_for_stock(const port_t *p, int g, float stock) {
    float ratio = port_norm_cached(p, g) / (maxf(stock, 0) + 1.f);
    float pr = goods[g].base_price * powf(ratio, PRICE_EXPONENT);
    pr = clampf(pr, goods[g].base_price * PRICE_MIN_MUL, goods[g].base_price * PRICE_MAX_MUL);
    if (p->nation == NATION_PIRATES) pr *= PIRATE_FENCE_MUL;
    return pr;
}
void port_update_prices(port_t *p) {
    int i = (int)(p - G.ports);
    for (int g = 0; g < MAX_GOODS; ++g) norm_cache[i][g] = port_norm(p, g);
    for (int g = 0; g < MAX_GOODS; ++g) p->price[g] = price_for_stock(p, g, p->stock[g]);
}
void economy_refresh_caches(void) { for (int i = 0; i < G.nports; ++i) for (int g = 0; g < MAX_GOODS; ++g) norm_cache[i][g] = port_norm(&G.ports[i], g); }   // state untouched

float trade_cost(const port_t *p, int g, float qty)    { return qty * price_for_stock(p, g, p->stock[g] - qty*0.5f); }
float trade_revenue(const port_t *p, int g, float qty) { return qty * price_for_stock(p, g, p->stock[g] + qty*0.5f); }

float tariff_rate(const port_t *p, int nation) {
    if (p->nation == nation) return TARIFF_HOME;
    if (p->nation == NATION_NONE) return TARIFF_FREE;
    if (p->nation == NATION_PIRATES) return 0.f;
    return TARIFF_FOREIGN;
}
int can_trade(const port_t *p, int nation) {
    if (p->size <= 0) return 0;
    if (at_war(nation, p->nation)) return nation == NATION_PIRATES && p->nation == NATION_PIRATES;
    if (p->blockaded_by && p->blockaded_by != nation) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// fleets as economic agents

float fleet_capacity(const fleet_t *f) { float c = 0; for (int i = 0; i < f->nships; ++i) c += ship_types[f->ships[i].type].cargo; return c; }
float fleet_cargo(const fleet_t *f)    { float c = 0; for (int g = 0; g < MAX_GOODS; ++g) c += f->cargo[g]; return c; }
int   fleet_guns(const fleet_t *f)     { int c = 0; for (int i = 0; i < f->nships; ++i) if (f->ships[i].hull > 0) c += ship_types[f->ships[i].type].guns; return c; }
int   fleet_crew(const fleet_t *f)     { int c = 0; for (int i = 0; i < f->nships; ++i) c += ship_types[f->ships[i].type].crew; return c; }
float fleet_value(const fleet_t *f)    { float v = 0; for (int i = 0; i < f->nships; ++i) v += ship_types[f->ships[i].type].cost; return v; }
float fleet_upkeep_day(const fleet_t *f) { return fleet_crew(f) * UPKEEP_PER_CREW; }
float fleet_speed(const fleet_t *f) {
    float s = 1e9f;
    for (int i = 0; i < f->nships; ++i) { const ship_info_t *t = &ship_types[f->ships[i].type]; float hs = t->speed * (0.6f + 0.4f * f->ships[i].hull / t->hull); if (hs < s) s = hs; }
    return s == 1e9f ? 1.f : s;
}
float fleet_hull_fraction(const fleet_t *f) {
    float h = 0, m = 0; for (int i = 0; i < f->nships; ++i) { h += f->ships[i].hull; m += ship_types[f->ships[i].type].hull; }
    return m > 0 ? h / m : 0;
}
void fleet_repair(fleet_t *f, float hull_per_ship) {
    for (int i = 0; i < f->nships; ++i) f->ships[i].hull = minf(ship_types[f->ships[i].type].hull, f->ships[i].hull + hull_per_ship);
}
void fleet_jettison(fleet_t *f) {
    float cap = fleet_capacity(f), have = fleet_cargo(f);
    if (have <= cap) return;
    float k = cap / have; for (int g = 0; g < MAX_GOODS; ++g) f->cargo[g] *= k;
}
double *wallet_of(fleet_t *f) { return is_player(f->nation) ? &G.nations[f->nation].treasury : &f->money; }

static void pay_tariff(port_t *p, double tariff) {
    p->tariffs_collected += tariff;
    if (p->nation != NATION_NONE && p->nation != NATION_PIRATES) {
        G.nations[p->nation].treasury += tariff; G.nations[p->nation].income_trade += tariff;
        if (is_player(p->nation)) G.nations[p->nation].income_acc += tariff;
    }
}

float trade_buy(fleet_t *f, port_t *p, int g, float qty) {
    if (!can_trade(p, f->nation)) return 0;
    float room = fleet_capacity(f) - fleet_cargo(f);
    qty = minf(qty, minf(room, p->stock[g]));
    if (qty <= 0.01f) return 0;
    double *wallet = wallet_of(f);
    float rate = tariff_rate(p, f->nation);
    float cost = trade_cost(p, g, qty), tariff = cost * rate;
    if (cost + tariff > *wallet) {
        qty *= (float)(*wallet / (cost + tariff)) * 0.98f; if (qty <= 0.01f) return 0;
        cost = trade_cost(p, g, qty); tariff = cost * rate;
    }
    *wallet -= cost + tariff;
    pay_tariff(p, tariff);
    p->stock[g] -= qty; f->cargo[g] += qty; G.trades++;
    if (is_player(f->nation)) G.nations[f->nation].expense_acc += cost + tariff;
    return qty;
}
float trade_sell(fleet_t *f, port_t *p, int g, float qty) {
    if (!can_trade(p, f->nation)) return 0;
    qty = minf(qty, f->cargo[g]);
    if (qty <= 0.01f) return 0;
    float rev = trade_revenue(p, g, qty), tariff = rev * tariff_rate(p, f->nation);
    if (g == G_SILVER || g == G_GOLD) tariff += rev * SEIGNIORAGE;   // the mint takes its cut
    *wallet_of(f) += rev - tariff;
    pay_tariff(p, tariff);
    p->stock[g] += qty; f->cargo[g] -= qty; G.trades++;
    if (is_player(f->nation)) G.nations[f->nation].income_acc += rev - tariff;
    return rev - tariff;
}
void fleet_sell_all(fleet_t *f, port_t *p) { for (int g = 0; g < MAX_GOODS; ++g) if (f->cargo[g] > 0.01f) trade_sell(f, p, g, f->cargo[g]); }

// ---------------------------------------------------------------------------
// ownership

void nations_recount_ports(void) {
    for (int n = 0; n < MAX_NATIONS; ++n) G.nations[n].ports = 0;
    for (int i = 0; i < G.nports; ++i) if (G.ports[i].size > 0) G.nations[G.ports[i].nation].ports++;
}
void port_set_owner(port_t *p, int nation) {
    p->nation = nation;
    territory_dirty = 1;
    nations_recount_ports();
}

// ---------------------------------------------------------------------------
// daily port simulation

static void port_tick(port_t *p, float dt) {
    if (p->size <= 0) return;
    float satisfied = 0, weight = 0;
    for (int g = 0; g < MAX_GOODS; ++g) {
        p->stock[g] += port_production(p, g) * dt;
        float cons = port_consumption(p, g) * dt;
        float w = goods[g].luxury == 0 ? 1.0f : goods[g].luxury == 1 ? 0.5f : 0.3f;
        if (cons > 0) { float take = minf(cons, p->stock[g]); p->stock[g] -= take; satisfied += w * (take / cons); weight += w; }
        float norm = port_norm(p, g);
        if (p->stock[g] > 2.0f * norm) p->stock[g] -= (p->stock[g] - 2.0f*norm) * SURPLUS_DECAY * dt;
        if (p->event_timer[g] > 0) { p->event_timer[g] -= dt; if (p->event_timer[g] <= 0) { p->prod_mul[g] = 1; p->cons_mul[g] = 1; } }
        if (p->stock[g] < 0) p->stock[g] = 0;
    }
    float sat = weight > 0 ? satisfied / weight : 1;
    float out = 0; for (int g = 0; g < MAX_GOODS; ++g) out += port_production(p, g);   // output per head feeds prosperity
    float target = 0.35f + sat * 1.1f + minf(p->traffic, 6) * 0.06f + p->building[B_MARKET] * 0.08f - (p->blockaded_by ? 0.4f : 0) + PROSPERITY_OUTPUT * clampf(out / maxf(p->pop, 1), 0, 1.5f);
    p->prosperity = clampf(p->prosperity + (target - p->prosperity) * PROSPERITY_RATE * dt, 0.25f, 2.2f);
    p->unrest = clampf(p->unrest + ((sat < 0.5f ? 0.004f : -0.003f) + (p->blockaded_by ? 0.003f : 0)) * dt, 0, 1);
    float growth = (POP_GROWTH * (sat - 0.45f) + 0.00003f * (p->prosperity - 1) + (port_colonial(p) ? POP_GROWTH_COLONY : 0)) * dt;
    if (p->pop_cap > 0 && p->pop > p->pop_cap && growth > 0) growth = 0;   // natural growth stops at the site's capacity
    p->pop = maxf(0.5f, p->pop * (1 + growth));
    if (p->culture[p->nation] < p->pop * 0.01f) p->culture[p->nation] += p->pop * growth; else p->culture[p->nation] *= 1 + growth;
    int want = p->pop > SIZE_CITY_POP ? 3 : p->pop > SIZE_TOWN_POP ? 2 : 1;
    if (want != p->size) {
        if (want > p->size) log_event_at(EV_INFO, p->nation, 0, p->x, p->z, "%s has grown into a %s", p->name, want == 3 ? "great city" : "town");
        p->size = want; territory_dirty = 1;
    }
    p->fort_max = FORT_BASE_HP + FORT_LEVEL_HP * p->building[B_FORT] + FORT_SIZE_HP * p->size;
    if (!p->blockaded_by) p->fort_hp = minf(p->fort_max, p->fort_hp + p->fort_max * 0.01f * dt);
    if (p->capture_cooldown > 0) p->capture_cooldown -= dt;
    if (p->blockade_timer > 0) { p->blockade_timer -= dt; if (p->blockade_timer <= 0) p->blockaded_by = 0; }
    p->traffic *= powf(0.97f, dt);
    if (p->nation != NATION_NONE && p->nation != NATION_PIRATES) {
        double tax = p->pop * p->prosperity * TAX_RATE * (1 + 0.25f*p->building[B_MARKET]) * dt;
        G.nations[p->nation].treasury += tax; G.nations[p->nation].income_tax += tax;
        if (is_player(p->nation)) G.nations[p->nation].income_acc += tax;
    }
    port_update_prices(p);
}

static void history_tick(float dt) {
    G.history_timer += dt;
    if (G.history_timer < 7) return;
    G.history_timer = 0;
    for (int g = 0; g < MAX_GOODS; ++g) {
        float s = 0; int n = 0;
        for (int i = 0; i < G.nports; ++i) if (G.ports[i].size > 0) { s += G.ports[i].price[g]; n++; }
        G.price_history[g][G.history_i % 64] = n ? s / n : goods[g].base_price;
    }
    G.history_i++;
}

void economy_tick(float dt) {
    for (int i = 0; i < G.nports; ++i) port_tick(&G.ports[i], dt);
    history_tick(dt);
}
