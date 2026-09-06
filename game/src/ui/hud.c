// Panels: top bar, selection (fleet / port with market, shipyard, buildings), fleets,
// chronicle, stats & diplomacy, help, main menu; world labels; toast notifications.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../data/geo_ports.h"
#include "../world/world.h"
#include "../sim/economy.h"
#include "../sim/fleet.h"
#include "../sim/events.h"
#include "../sim/game.h"
#include "../sim/settlement.h"
#include "../sim/region.h"
#include "../world/regions.h"
#include "../gfx/camera.h"
#include "../gfx/scene.h"

void hud_on_log(const logline_t *l) { if (l->important) toast("%s", l->text); }

static mu_Color log_color(int type) {
    switch (type) {
        case EV_WAR: case EV_BATTLE: case EV_CAPTURE: return mu_color(255,160,140,255);
        case EV_STORM: case EV_PLAGUE: case EV_FAMINE: case EV_FIRE: case EV_REBELLION: return mu_color(255,210,130,255);
        case EV_TREASURE: case EV_STRIKE: return mu_color(255,230,120,255);
        case EV_COLONY: case EV_PEACE: case EV_HARVEST: return mu_color(160,230,160,255);
        default: return mu_color(220,220,220,255);
    }
}

// ---------------------------------------------------------------------------

static void panel_topbar(void) {
    int W = screen_w;
    mu_Container *c = mu_get_container(ui_ctx, "##topbar"); c->rect = mu_rect(0, 0, W, 34);
    if (!mu_begin_window_ex(ui_ctx, "##topbar", c->rect, MU_OPT_NOTITLE|MU_OPT_NORESIZE|MU_OPT_NOSCROLL|MU_OPT_NOCLOSE)) return;
    const nation_t *me = &G.nations[G.player];
    ROW(6, 130, 34, 34, 34, 34, 34, 22);
    ui_label_col(ui_date(G.day), mu_color(240,230,200,255));
    if (ui_btn(G.speed == 0 ? ">" : "||")) G.speed = G.speed == 0 ? 1 : 0;
    static const float speeds[4] = { 1, 3, 8, 20 };
    for (int i = 0; i < 4; ++i) { mu_push_id(ui_ctx, &i, sizeof i); if (ui_btn(G.speed == speeds[i] ? va("[%d]", i+1) : va("%d", i+1))) G.speed = speeds[i]; mu_pop_id(ui_ctx); }
    mu_layout_set_next(ui_ctx, mu_rect(350, 4, 230, 22), 0); ui_label_col(va("Treasury: %s ducats", ui_money(me->treasury)), mu_color(255,220,120,255));
    double net = me->stat_income - me->stat_expense;
    mu_layout_set_next(ui_ctx, mu_rect(580, 4, 190, 22), 0); ui_label_col(va("Yesterday: %s%.0f", net >= 0 ? "+" : "", net), net >= 0 ? mu_color(150,240,150,255) : mu_color(240,140,140,255));
    mu_layout_set_next(ui_ctx, mu_rect(770, 4, 170, 22), 0); ui_label_col(va("%s | Ports: %d", nations_info[G.player].name, me->ports), ui_mcol(nations_info[G.player].color, 255));
    struct { const char *name; int *flag; } toggles[] = { {"Fleets", &U.show_fleets}, {"Log", &U.show_log}, {"Lanes", &U.show_lanes}, {"Regions", &U.show_regions}, {"Wind", &U.show_wind}, {"Stats", &U.show_stats}, {"Help", &U.show_help} };
    int x = W - 7*64 - 8;
    for (int i = 0; i < 7; ++i) { mu_layout_set_next(ui_ctx, mu_rect(x + i*64, 4, 60, 24), 0); if (ui_btn(*toggles[i].flag ? va("[%s]", toggles[i].name) : toggles[i].name)) *toggles[i].flag ^= 1; }
    int errs = diag_level_count(LOG_ERROR) + diag_level_count(LOG_FATAL);
    if (errs) { mu_layout_set_next(ui_ctx, mu_rect(x - 70, 4, 66, 24), 0); mu_push_id(ui_ctx, "errs", 4); if (mu_button_ex(ui_ctx, va("%d err", errs), 0, MU_OPT_ALIGNCENTER)) { U.show_debug = 1; } mu_pop_id(ui_ctx); }
    mu_end_window(ui_ctx);
}

// market table; `f` may be NULL (read only)
static void market_rows(fleet_t *f, port_t *p) {
    int docked = f && f->at_port == (int)(p - G.ports) && is_player(f->nation);
    if (docked) { ROW(6, 78, 50, 58, 40, 44, -1, 18); mu_label(ui_ctx, "Good"); mu_label(ui_ctx, "Stock"); mu_label(ui_ctx, "Price"); mu_label(ui_ctx, "Hold"); mu_label(ui_ctx, "Buy"); mu_label(ui_ctx, "Sell"); }
    else { ROW(3, 90, 70, -1, 18); mu_label(ui_ctx, "Good"); mu_label(ui_ctx, "Stock"); mu_label(ui_ctx, "Price (base)"); }
    int step = (KHELD(GLFW_KEY_LEFT_SHIFT) || KHELD(GLFW_KEY_RIGHT_SHIFT)) ? 100 : 10;
    for (int g = 0; g < MAX_GOODS; ++g) {
        mu_push_id(ui_ctx, &g, sizeof g);
        float ratio = p->price[g] / goods[g].base_price;
        mu_Color pc = ratio > 1.6f ? mu_color(255,150,120,255) : ratio < 0.7f ? mu_color(140,230,140,255) : mu_color(230,230,230,255);
        if (docked) {
            ROW(6, 78, 50, 58, 40, 44, -1, 20);
            ui_label_col(goods[g].name, ui_mcol(goods[g].color, 255)); ui_label_col(va("%.0f", p->stock[g]), mu_color(220,220,220,255));
            ui_label_col(va("%.1f", p->price[g]), pc); ui_label_col(va("%.0f", f->cargo[g]), mu_color(220,220,220,255));
            if (ui_btn(va("+%d", step)) && trade_buy(f, p, g, step) <= 0) toast("Cannot buy: no room, stock or funds");
            if (ui_btn(va("-%d", step)) && trade_sell(f, p, g, step) <= 0) toast("Nothing to sell");
        } else {
            ROW(3, 90, 70, -1, 18);
            ui_label_col(goods[g].name, ui_mcol(goods[g].color, 255)); ui_label_col(va("%.0f", p->stock[g]), mu_color(220,220,220,255));
            ui_label_col(va("%.1f  (%.0f)", p->price[g], goods[g].base_price), pc);
        }
        mu_pop_id(ui_ctx);
    }
    if (docked) {
        ROW(3, 130, 130, -1, 22);
        if (ui_btn("Sell all cargo")) fleet_sell_all(f, p);
        float cost = 0; for (int i = 0; i < f->nships; ++i) cost += (ship_types[f->ships[i].type].hull - f->ships[i].hull) * REPAIR_COST_PER_HP;
        if (ui_btn(va("Repair (%.0f)", cost))) { if (cost <= 0) toast("Nothing to repair"); else if (G.nations[f->nation].treasury >= cost) { G.nations[f->nation].treasury -= cost; fleet_repair(f, 1e9f); toast("Repaired for %.0f", cost); } else toast("Repairs cost %.0f", cost); }
        mu_label(ui_ctx, "Shift = x100");
    }
}

static void fleet_panel(fleet_t *f) {
    int mine = is_player(f->nation), idx = fleet_index(f);
    ROW(2, -90, -1, 20);
    ui_label_col(f->name, ui_mcol(nations_info[f->nation].color, 255));
    float fx, fz, fh; scene_fleet_pos(f, &fx, &fz, &fh);
    if (ui_btn("Center")) cam_goto(fx, fz);
    ROW(1, -1, 18);
    mu_label(ui_ctx, va("%s  |  %s", nations_info[f->nation].name, ui_order(f)));
    mu_label(ui_ctx, va("Ships %d  Guns %d  Crew %d  Morale %.0f%%  Speed %.1f kn", f->nships, fleet_guns(f), fleet_crew(f), f->morale*100, fleet_speed(f)*6.5f));
    mu_label(ui_ctx, va("Cargo %.0f / %.0f tons   Upkeep %.0f/day", fleet_cargo(f), fleet_capacity(f), fleet_upkeep_day(f)));
    for (int i = 0; i < f->nships; ++i) { ROW(3, 140, 140, -1, 16); mu_push_id(ui_ctx, &i, sizeof i);
        mu_label(ui_ctx, ship_types[f->ships[i].type].name);
        mu_Rect r = mu_layout_next(ui_ctx); float hp = clampf(f->ships[i].hull / ship_types[f->ships[i].type].hull, 0, 1);
        mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+4, r.w, r.h-8), mu_color(60,30,30,255)); mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+4, (int)(r.w*hp), r.h-8), hp > 0.5f ? mu_color(80,200,80,255) : mu_color(220,90,60,255));
        mu_label(ui_ctx, va("%.0f/%.0f", f->ships[i].hull, ship_types[f->ships[i].type].hull)); mu_pop_id(ui_ctx); }
    { char buf[256] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (f->cargo[g] >= 1) n += snprintf(buf+n, sizeof buf - n, "%s %.0f  ", goods[g].name, f->cargo[g]); ROW(1, -1, 18); mu_label(ui_ctx, n ? buf : "Holds empty"); }
    if (!mine) return;
    ROW(4, 92, 92, 110, -1, 22);
    if (ui_btn("Stop")) fleet_order_stop(f);
    if (ui_btn(U.route_mode ? "Click ports..." : "Set route") && !U.route_mode) { U.route_mode = 1; f->nroute = 0; f->autotrade = 0; toast("Left-click ports in order, right-click to finish"); }
    if (ui_btn(f->autotrade ? "Auto-trade: ON" : "Auto-trade: OFF")) { f->autotrade ^= 1; if (f->autotrade) { f->nroute = 0; if (f->at_port >= 0) f->think_timer = 0.1f; toast("Free trading: the fleet seeks profit on its own"); } }
    if (ui_btn(cam.follow_fleet_id == f->id ? "Following" : "Follow")) cam.follow_fleet_id = cam.follow_fleet_id == f->id ? 0 : f->id;
    if (f->nroute >= 2) { char buf[256] = ""; int n = 0; for (int i = 0; i < f->nroute; ++i) n += snprintf(buf+n, sizeof buf - n, "%s%s", i ? " > " : "Route: ", G.ports[f->route[i]].name); ui_text_full(buf); }
    if (f->at_port >= 0) {
        port_t *pt = &G.ports[f->at_port];
        int others = 0; for (int i = 0; i < G.nfleets; ++i) if (i != idx && G.fleets[i].alive && is_player(G.fleets[i].nation) && G.fleets[i].at_port == f->at_port) others++;
        ROW(3, 130, 130, -1, 20);
        if (ui_btn(others ? va("Merge %d fleet(s) here", others) : "Merge (none here)") && others) {
            for (int i = 0; i < G.nfleets && f->nships < MAX_SHIPS; ++i) { fleet_t *o = &G.fleets[i]; if (i == idx || !o->alive || !is_player(o->nation) || o->at_port != f->at_port) continue;
                while (o->nships && f->nships < MAX_SHIPS) f->ships[f->nships++] = o->ships[--o->nships];
                for (int g = 0; g < MAX_GOODS; ++g) { f->cargo[g] += o->cargo[g]; o->cargo[g] = 0; }
                if (o->nships == 0) fleet_destroy(o); else fleet_jettison(o); }
            if (fleet_guns(f) > 0 && f->role == ROLE_TRADER && fleet_capacity(f) < 300) f->role = ROLE_WARSHIP;
            toast("Fleets merged");
        }
        if (ui_btn("Split off last ship") && f->nships > 1) {
            fleet_t *n = fleet_new(f->nation, f->role, f->x, f->z, va("Fleet %d", G.next_fleet_id+1));
            if (n) { n->at_port = f->at_port; n->home_port = f->at_port; n->order = ORDER_DOCK; n->ships[0] = f->ships[--f->nships]; n->nships = 1;
                float cap = fleet_capacity(f); for (int g = 0; g < MAX_GOODS; ++g) { float keep = minf(f->cargo[g], cap); n->cargo[g] = f->cargo[g] - keep; f->cargo[g] = keep; cap -= keep; }
                ui_select_fleet(n); toast("Ship detached as a new fleet"); }
        }
        if (ui_btn("Rename")) snprintf(f->name, sizeof f->name, "%s %d", f->role == ROLE_WARSHIP ? "Squadron" : "Convoy", f->id);
        ROW(2, -80, -1, 20); ui_label_col(va("Market of %s (%s)", pt->name, nations_info[pt->nation].name), mu_color(255,220,140,255)); if (ui_btn("Port info")) ui_select_port(f->at_port);
        if (can_trade(pt, f->nation)) { mu_label(ui_ctx, va("Tariff here: %.0f%%", tariff_rate(pt, f->nation)*100)); market_rows(f, pt); }
        else mu_label(ui_ctx, "No trade possible here (war or blockade)");
    }
    ui_text_full("Right-click: sail / dock / attack / besiege / settle a site / found a colony on any free coast. Shift+right-click: patrol at sea, or besiege an independent/pirate port. Ctrl+right-click port: blockade.");
}

static void port_panel(port_t *p, int pi) {
    ROW(2, -90, -1, 20);
    ui_label_col(va("%s%s", p->name, p->size <= 0 ? " (unsettled site)" : p->nation == G.player ? " (yours)" : ""), ui_mcol(p->size > 0 ? nations_info[p->nation].color : 0xFFAAAAAA, 255));
    if (ui_btn("Center")) cam_goto(p->x, p->z);
    ROW(1, -1, 18);
    if (p->size <= 0) {
        char res[160] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (p->potential[g] > 0) n += snprintf(res+n, sizeof res-n, "%s%s %.1f", n ? ", " : "", goods[g].name, p->potential[g]);
        if (p->hinterland >= 0) { const region_t *R = &G.regions[p->hinterland]; ui_text_full(va("Hinterland: %s (%s, %d hub port%s). Attractiveness %.1f.", R->name, dev_tier_name(R->development), R->nhub, R->nhub == 1 ? "" : "s", region_attractiveness(p->hinterland))); }
        if (p->founded) ui_text_full(va("A claimed coast in the %s. Resources: %s. Send a fleet here to found the colony (%.0f ducats on arrival).", region_names[p->region], n ? res : "none", COLONY_COST));
        else ui_text_full(va("An unsettled site in the %s. Send a fleet here (right-click with a fleet selected) to found a colony for %.0f ducats. Historically settled by %s in %d. Resources: %s", region_names[p->region], COLONY_COST, p->preferred_nation ? nations_info[p->preferred_nation].name : "nobody", p->settle_year, n ? res : "none"));
        return;
    }
    mu_label(ui_ctx, va("%s | %s", nations_info[p->nation].name, region_names[p->region]));
    mu_label(ui_ctx, va("Population %.1fk | prosperity %.0f%% | unrest %.0f%%", p->pop, p->prosperity*100, p->unrest*100));
    mu_label(ui_ctx, va("Fort %.0f/%.0f%s%s | tariffs collected %.0f", p->fort_hp, p->fort_max, p->blockaded_by ? " | BLOCKADED by " : "", p->blockaded_by ? nations_info[p->blockaded_by].name : "", p->tariffs_collected));
    { char buf[200] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (p->produce[g] > 0) n += snprintf(buf+n, sizeof buf-n, "%s%s", n ? ", " : "Produces: ", goods[g].name); mu_label(ui_ctx, n ? buf : "Produces nothing"); }
    { char buf[200] = ""; int n = 0; for (int b = 0; b < MAX_BUILDINGS; ++b) if (p->building[b]) n += snprintf(buf+n, sizeof buf-n, "%s%s %d", n ? ", " : "", buildings[b].name, p->building[b]); mu_label(ui_ctx, n ? buf : "No buildings"); }
    int mine = p->nation == G.player;
    if (mine) {
        if (mu_header_ex(ui_ctx, "Build", 0)) {
            for (int b = 0; b < MAX_BUILDINGS; ++b) { mu_push_id(ui_ctx, &b, sizeof b); ROW(3, 110, 60, -1, 20);
                float cost = buildings[b].cost * (1 + p->building[b]);
                mu_label(ui_ctx, va("%s %d", buildings[b].name, p->building[b]));
                if (p->building[b] >= 3) mu_label(ui_ctx, "max");
                else if (ui_btn(va("%.0f", cost))) { if (G.nations[G.player].treasury >= cost) { G.nations[G.player].treasury -= cost; p->building[b]++; territory_dirty = 1; toast("%s upgraded to level %d", buildings[b].name, p->building[b]); } else toast("Not enough ducats"); }
                mu_label(ui_ctx, buildings[b].desc); mu_pop_id(ui_ctx); }
        }
        fleet_t *docked = 0; for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && is_player(G.fleets[i].nation) && G.fleets[i].at_port == pi) { docked = &G.fleets[i]; break; }
        if (p->building[B_DOCKS] > 0) {
            if (mu_header_ex(ui_ctx, "Shipyard", MU_OPT_EXPANDED)) {
                mu_label(ui_ctx, docked ? va("New ships join %s", docked->name) : "New ships form a new fleet here");
                for (int t = 0; t < SHIP_COUNT; ++t) { mu_push_id(ui_ctx, &t, sizeof t); ROW(3, 120, 70, -1, 20);
                    const ship_info_t *s = &ship_types[t];
                    mu_label(ui_ctx, s->name);
                    if (ui_btn(va("%.0f", s->cost))) {
                        if (G.nations[G.player].treasury < s->cost) toast("Not enough ducats");
                        else {
                            fleet_t *tf = docked && docked->nships < MAX_SHIPS ? docked : fleet_new(G.player, t >= SHIP_FRIGATE && t != SHIP_GALLEON ? ROLE_WARSHIP : ROLE_TRADER, p->x, p->z, va("Fleet %d", G.next_fleet_id+1));
                            if (!tf) toast("Too many fleets at sea");
                            else { if (tf != docked) { tf->at_port = pi; tf->home_port = pi; tf->order = ORDER_DOCK; } G.nations[G.player].treasury -= s->cost; fleet_add_ship(tf, t); toast("%s launched at %s", s->name, p->name); }
                        } }
                    mu_label(ui_ctx, va("cargo %.0f guns %d spd %.1f", s->cargo, s->guns, s->speed)); mu_pop_id(ui_ctx); }
            }
        } else mu_label(ui_ctx, "Build Docks to open a shipyard.");
    }
    if (p->hinterland >= 0 && mu_header_ex(ui_ctx, "Hinterland", MU_OPT_EXPANDED)) {
        const region_t *R = &G.regions[p->hinterland];
        ROW(1, -1, 16);
        mu_label(ui_ctx, va("%s: %s (%.1f/3), rural pop %.0fk, prosperity %.0f%%", R->name, dev_tier_name(R->development), R->development, R->pop, R->prosperity*100));
        { char hubs[160] = ""; int n = 0; for (int k = 0; k < R->nhub; ++k) n += snprintf(hubs+n, sizeof hubs-n, "%s%s %.0f%%", n ? ", " : "", G.ports[R->hub[k]].name, G.ports[R->hub[k]].hub_share*100); mu_label(ui_ctx, va("Hubs: %s | link %.0f units/day", hubs, R->throughput)); }
        { char res[220] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (R->potential[g] > 0) n += snprintf(res+n, sizeof res-n, "%s%s %.1f", n ? ", " : "Resources: ", goods[g].name, R->potential[g]); ui_text_full(n ? res : "No resources"); }
        { char in[220] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (p->region_in[g] > 0.05f) n += snprintf(in+n, sizeof in-n, "%s%s %.1f", n ? ", " : "Arriving from the land: ", goods[g].name, p->region_in[g]); if (n) ui_text_full(in); }
        { char need[220] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (R->import_need[g] > 0.05f) n += snprintf(need+n, sizeof need-n, "%s%s %.1f/day", n ? ", " : "The countryside lacks: ", goods[g].name, R->import_need[g]); if (n) ui_text_full(need); }
        for (int i = 0; i < G.ninland; ++i) { const inland_t *s = &G.inland[i]; if (!s->alive || s->region != p->hinterland) continue; mu_push_id(ui_ctx, &i, sizeof i); ROW(3, 150, 70, -1, 16);
            ui_label_col(s->name, ui_mcol(goods[s->good].color, 255)); mu_label(ui_ctx, va("pop %.1fk", s->pop)); mu_label(ui_ctx, va("%.1f %s/day", s->output, goods[s->good].name)); mu_pop_id(ui_ctx); }
        if (mine) mu_label(ui_ctx, "Trade through this port develops the region: Markets and Docks raise the link capacity.");
    }
    if (mu_header_ex(ui_ctx, "Production", mine ? MU_OPT_EXPANDED : 0)) {
        ROW(1, -1, 16); mu_label(ui_ctx, mine ? "Labour shares (Shift = 25% steps). Output = workforce x potential x share." : "Labour shares");
        for (int g = 0; g < MAX_GOODS; ++g) { if (p->potential[g] <= 0 && p->produce[g] <= 0) continue; mu_push_id(ui_ctx, &g, sizeof g);
            ROW(5, 80, 60, -1, 36, 36, 18);
            ui_label_col(goods[g].name, ui_mcol(goods[g].color, 255));
            mu_label(ui_ctx, va("pot %.1f", p->potential[g]));
            mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255)); mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+3, (int)(r.w * clampf(p->labor[g],0,1)), r.h-6), mu_color(120,170,110,255));
            mu_draw_text(ui_ctx, NULL, va("%.0f%%  %.1f/day", p->labor[g]*100, port_production(p, g)), -1, mu_vec2(r.x+4, r.y+1), mu_color(240,240,240,255));
            float step = (KHELD(GLFW_KEY_LEFT_SHIFT) || KHELD(GLFW_KEY_RIGHT_SHIFT)) ? 0.25f : 0.1f;
            if (mine) { if (ui_btn("-")) settlement_set_labor(p, g, p->labor[g] - step); if (ui_btn("+")) settlement_set_labor(p, g, p->labor[g] + step); } else { mu_label(ui_ctx, ""); mu_label(ui_ctx, ""); }
            mu_pop_id(ui_ctx); }
    }
    if (mu_header_ex(ui_ctx, "Population & cultures", 0)) {
        ROW(1, -1, 16);
        mu_label(ui_ctx, va("%.1fk of %.0fk capacity | immigration %.2fk/day | tolerance of %s rule %.0f%%", p->pop, p->pop_cap, p->immigration, nations_info[p->nation].adj, nations_info[p->nation].tolerance*100));
        int dom = settlement_dominant_culture(p);
        if (dom != p->nation) { mu_label(ui_ctx, va("WARNING: %s settlers are the majority (%.0f%% foreign) - risk of secession. Forts garrison the town (unrest /%.1f now).", nations_info[dom].adj, settlement_foreign_share(p)*100, 1 + FORT_GARRISON_CALM * p->building[B_FORT])); }
        for (int n = 0; n < MAX_NATIONS; ++n) { if (p->culture[n] < 0.05f) continue; mu_push_id(ui_ctx, &n, sizeof n); ROW(2, 140, -1, 16);
            ui_label_col(nations_info[n].adj, ui_mcol(nations_info[n].color, 255));
            mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255)); mu_draw_rect(ui_ctx, mu_rect(r.x, r.y+3, (int)(r.w * clampf(p->culture[n]/maxf(p->pop,0.1f),0,1)), r.h-6), ui_mcol(nations_info[n].color, 255));
            mu_draw_text(ui_ctx, NULL, va("%.1fk (%.0f%%)", p->culture[n], 100*p->culture[n]/maxf(p->pop,0.1f)), -1, mu_vec2(r.x+4, r.y+1), mu_color(240,240,240,255));
            mu_pop_id(ui_ctx); }
        if (p->founded) mu_label(ui_ctx, va("Founded colony: merges with neighbours within %.1f degrees; the dominant culture takes control", MERGE_DIST / WORLD_K));
    }
    if (mu_header_ex(ui_ctx, "Market", mine ? 0 : MU_OPT_EXPANDED)) market_rows(0, p);
}

static void panel_selection(void) {
    fleet_t *f = fleet_by_id(U.sel_fleet_id);
    if (!f && U.sel_port < 0) { U.sel_fleet_id = 0; return; }
    int H = screen_h, ph = f ? 420 : 470;
    mu_Container *c = mu_get_container(ui_ctx, "Selection"); c->rect = mu_rect(0, H - ph, 400, ph);
    if (!mu_begin_window_ex(ui_ctx, "Selection", c->rect, MU_OPT_NORESIZE|MU_OPT_NOCLOSE)) return;
    if (f) fleet_panel(f); else port_panel(&G.ports[U.sel_port], U.sel_port);
    mu_end_window(ui_ctx);
}

static void panel_fleets(void) {
    if (!U.show_fleets) return;
    mu_Container *c = mu_get_container(ui_ctx, "Fleets"); if (c->rect.w == 0) c->rect = mu_rect(screen_w - 270, 40, 270, 260);
    if (!mu_begin_window_ex(ui_ctx, "Fleets", c->rect, MU_OPT_NOCLOSE)) return;
    int n = 0;
    for (int i = 0; i < G.nfleets; ++i) { fleet_t *f = &G.fleets[i]; if (!f->alive || !is_player(f->nation)) continue; n++;
        mu_push_id(ui_ctx, &f->id, sizeof f->id); ROW(2, -60, -1, 18);
        if (mu_button_ex(ui_ctx, va("%s%s (%d) %s", f->id == U.sel_fleet_id ? "> " : "", f->name, f->nships, f->role == ROLE_WARSHIP ? "[W]" : f->autotrade ? "[auto]" : ""), 0, 0)) ui_select_fleet(f);
        if (ui_btn("Go")) { ui_select_fleet(f); float x, z, h; scene_fleet_pos(f, &x, &z, &h); cam_goto(x, z); }
        mu_pop_id(ui_ctx); ROW(1, -1, 14); mu_label(ui_ctx, va("   %s", ui_order(f))); }
    if (!n) ui_text_full("No fleets. Select one of your ports with Docks and buy ships in the Shipyard.");
    mu_end_window(ui_ctx);
}

static void panel_log(void) {
    if (!U.show_log) return;
    mu_Container *c = mu_get_container(ui_ctx, "Chronicle"); if (c->rect.w == 0) c->rect = mu_rect(screen_w - 460, screen_h - 230, 460, 230);
    if (!mu_begin_window_ex(ui_ctx, "Chronicle", c->rect, MU_OPT_NOCLOSE)) return;
    for (int i = G.nlog - 1; i >= 0; --i) { const logline_t *l = &G.log[i]; mu_push_id(ui_ctx, &i, sizeof i);
        ROW(2, 90, -1, 0);
        if (l->has_pos) { if (mu_button_ex(ui_ctx, ui_date(l->day), 0, 0)) cam_goto(l->x, l->z); } else mu_label(ui_ctx, ui_date(l->day));
        mu_Color bak = ui_ctx->style->colors[MU_COLOR_TEXT]; ui_ctx->style->colors[MU_COLOR_TEXT] = log_color(l->type); mu_text(ui_ctx, l->text); ui_ctx->style->colors[MU_COLOR_TEXT] = bak;
        mu_pop_id(ui_ctx); }
    mu_end_window(ui_ctx);
}

static void panel_stats(void) {
    if (!U.show_stats) return;
    mu_Container *c = mu_get_container(ui_ctx, "Empire & Markets"); if (c->rect.w == 0) c->rect = mu_rect(screen_w/2 - 260, 60, 520, 440);
    if (!mu_begin_window_ex(ui_ctx, "Empire & Markets", c->rect, 0)) return;
    mu_label(ui_ctx, "Nations");
    for (int n = 0; n < MAX_NATIONS; ++n) { const nation_t *nt = &G.nations[n]; ROW(4, 150, 90, 60, -1, 16);
        ui_label_col(nations_info[n].name, ui_mcol(nations_info[n].color, 255)); mu_label(ui_ctx, va("%.0f", nt->treasury)); mu_label(ui_ctx, va("%d ports", nt->ports));
        char wars[128] = ""; int k = 0; if (n != NATION_PIRATES) for (int m = 1; m < NATION_PIRATES; ++m) if (nt->relation[m]) k += snprintf(wars+k, sizeof wars-k, "%s%s", k ? ", " : "war: ", nations_info[m].adj);
        mu_label(ui_ctx, k ? wars : n == NATION_PIRATES ? "enemy of all" : "at peace"); }
    mu_label(ui_ctx, "Diplomacy");
    for (int n = NATION_SPAIN; n < NATION_PIRATES; ++n) { if (n == G.player) continue; mu_push_id(ui_ctx, &n, sizeof n); ROW(3, 150, 100, -1, 18);
        ui_label_col(nations_info[n].name, ui_mcol(nations_info[n].color, 255));
        if (G.nations[G.player].relation[n]) { if (ui_btn("Sue for peace")) { if (ai_accepts_peace(n, G.player)) set_war(G.player, n, 0, 0); else toast("%s refuses: the war is too young", nations_info[n].name); } mu_label(ui_ctx, va("at war for %.0f days", G.nations[G.player].war_timer[n])); }
        else { if (ui_btn("Declare war")) set_war(G.player, n, 1, "the Admiralty's ambitions"); mu_label(ui_ctx, "peace"); }
        mu_pop_id(ui_ctx); }
    mu_label(ui_ctx, "Average prices (world) and trend");
    for (int g = 0; g < MAX_GOODS; ++g) { ROW(3, 90, 70, -1, 16);
        ui_label_col(goods[g].name, ui_mcol(goods[g].color, 255));
        int hi = G.history_i; float now = hi ? G.price_history[g][(hi-1)%64] : goods[g].base_price, old = hi > 7 ? G.price_history[g][(hi-8)%64] : now;
        mu_label(ui_ctx, va("%.1f", now));
        mu_Rect r = mu_layout_next(ui_ctx); mu_draw_rect(ui_ctx, r, mu_color(40,40,50,255));
        int pts = mini(hi, 64); float mx = 1; for (int i = 0; i < pts; ++i) mx = maxf(mx, G.price_history[g][i]);
        mu_Color bar = now > old*1.05f ? mu_color(230,120,100,255) : now < old*0.95f ? mu_color(120,220,120,255) : mu_color(180,180,200,255);
        for (int i = 0; i < pts; ++i) { float v = G.price_history[g][(hi - pts + i) % 64] / mx; int x = r.x + i * r.w / 64; mu_draw_rect(ui_ctx, mu_rect(x, r.y + r.h - (int)(v*r.h), maxi(1, r.w/64), (int)(v*r.h)), bar); } }
    double worth = G.nations[G.player].treasury; int ships = 0;
    for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && is_player(G.fleets[i].nation)) { worth += fleet_value(&G.fleets[i]); ships += G.fleets[i].nships; for (int g = 0; g < MAX_GOODS; ++g) worth += G.fleets[i].cargo[g] * goods[g].base_price; }
    for (int i = 0; i < G.nports; ++i) if (G.ports[i].size > 0 && G.ports[i].nation == G.player) worth += 20000 * G.ports[i].size + G.ports[i].pop * 500;
    mu_label(ui_ctx, va("Your standing: %d ships, %d ports, net worth %.0f ducats", ships, G.nations[G.player].ports, worth));
    mu_label(ui_ctx, va("World: %d battles, %d captures, %d trades, %d fleets at sea, %d active events", G.battles, G.captures, G.trades, game_count_fleets(), G.nevents));
    mu_end_window(ui_ctx);
}

static void panel_help(void) {
    if (!U.show_help) return;
    mu_Container *c = mu_get_container(ui_ctx, "Help"); if (c->rect.w == 0) c->rect = mu_rect(screen_w/2 - 250, 80, 500, 400);
    if (!mu_begin_window_ex(ui_ctx, "Help", c->rect, 0)) return;
    ui_text_full(
        "MARE LIBERUM - 17th century naval trade & war\n\n"
        "Camera: WASD/arrows or right-drag to pan, wheel to zoom (zoom out into the map view), Q/E or middle-drag to rotate, F follow fleet, H home, M toggle map.\n"
        "Select: left-click a ship or port name. Right-click with a fleet selected: sail to sea, dock at a port, attack a hostile fleet, besiege an enemy port, found a colony at an unsettled site. Shift+right-click: patrol.\n"
        "Trade: dock at a port and use the market in the Selection panel. Prices emerge from supply and demand: colonies pay for cloth, arms and wine; Europe pays for sugar, tobacco, spices, furs and silver. 'Set route' makes a fleet shuttle between ports on its own; 'Auto-trade' lets it roam for profit.\n"
        "Empire: build Docks to launch ships; Forts, Warehouses, Markets, Plantations and Mines improve your ports. Capture enemy ports by siege when at war, or settle unclaimed sites. The Chronicle reports storms, plagues, wars, treasure fleets and pirates.\n"
        "Keys: Space pause, 1-4 speed, Tab cycle fleets, F5 save, F9 load, Esc deselect.");
    if (ui_btn("Close")) U.show_help = 0;
    mu_end_window(ui_ctx);
}

int hud_menu(void) {
    int W = screen_w, H = screen_h, chosen = 0;
    mu_Container *c = mu_get_container(ui_ctx, "Mare Liberum"); c->rect = mu_rect(W/2 - 230, H/2 - 200, 460, 400);
    if (!mu_begin_window_ex(ui_ctx, "Mare Liberum", c->rect, MU_OPT_NORESIZE|MU_OPT_NOCLOSE)) return 0;
    ui_text_full("Anno Domini 1620. Choose the flag under which you will sail. Each nation begins with its historical ports, a small fleet and 60,000 ducats.");
    static const char *blurb[MAX_NATIONS] = { 0, "silver of the Indies", "rising sea power", "furs and Mediterranean trade", "the merchant republic (recommended)", "Brazil, Africa and spices", "the Levant trade", "master of the eastern seas", "Baltic and Norwegian trade", 0 };
    for (int n = NATION_SPAIN; n < NATION_PIRATES; ++n) { ROW(2, 170, -1, 24); mu_push_id(ui_ctx, &n, sizeof n);
        if (ui_btn(nations_info[n].name)) chosen = n;
        int ports = 0; for (int i = 0; i < GEO_NPORTS; ++i) ports += geo_ports[i].nation == n && geo_ports[i].size > 0;
        mu_label(ui_ctx, va("%d ports, %s", ports, blurb[n])); mu_pop_id(ui_ctx); }
    ROW(1, -1, 24); if (ui_btn("Load saved game")) chosen = -1;
    mu_end_window(ui_ctx);
    return chosen;
}

void hud_panels(void) {
    panel_topbar(); panel_selection(); panel_fleets(); panel_log(); panel_stats(); panel_help();
}

// ---------------------------------------------------------------------------
// labels (drawn in the overlay)

// labels are placed greedily by priority; a label that would overlap an already placed one is skipped
typedef struct label_rect_t { float x0, y0, x1, y1; } label_rect_t;
static label_rect_t placed[MAX_PORTS + 64]; static int nplaced;
static int label_fits(float cx, float cy, float w, float h) {
    label_rect_t r = { cx - w/2 - 2, cy - h/2 - 1, cx + w/2 + 2, cy + h/2 + 1 };
    for (int i = 0; i < nplaced; ++i) if (r.x0 < placed[i].x1 && r.x1 > placed[i].x0 && r.y0 < placed[i].y1 && r.y1 > placed[i].y0) return 0;
    if (nplaced < (int)(sizeof placed / sizeof placed[0])) placed[nplaced++] = r;
    return 1;
}

void hud_labels(void) {
    int h = ui_ctx->text_height(NULL), W = screen_w, H = screen_h;
    nplaced = 0;
    // ports sorted by priority: selected/hovered, then the player's, then by size
    static int order[MAX_PORTS]; int n = 0;
    for (int i = 0; i < G.nports; ++i) if (!port_is_gone(&G.ports[i])) order[n++] = i;
    #define PRIO(i) ((i == U.sel_port || i == U.hover_port) * 100 + (G.ports[i].size > 0 && G.ports[i].nation == G.player) * 10 + G.ports[i].size)
    for (int a = 1; a < n; ++a) { int v = order[a], b = a - 1; while (b >= 0 && PRIO(order[b]) < PRIO(v)) { order[b+1] = order[b]; b--; } order[b+1] = v; }
    #undef PRIO
    for (int k = 0; k < n; ++k) { int i = order[k]; const port_t *p = &G.ports[i];
        float sx, sy; if (!cam_project(p->x, 0.5f, p->z, &sx, &sy) || sx < -50 || sy < -50 || sx > W+50 || sy > H+50) continue;
        int important = i == U.sel_port || i == U.hover_port, mine = p->size > 0 && p->nation == G.player;
        if (p->size > 0 && cam.dist > 380 && p->size < 2 && !important && !mine) continue;
        if (cam.dist > 550 && p->size < 3 && !important && !(mine && p->size >= 2)) continue;
        if (cam.dist > 800 && p->size < 3 && !important) continue;
        if (p->size <= 0 && cam.dist > 400 && !important) continue;
        const char *txt = p->size <= 0 ? va("%s (site)", p->name) : p->name;
        float ly = sy - 12 - (cam.mapness > 0.5f ? 2 + 2*p->size : 8);
        if (!important && !label_fits(sx, ly, (float)ui_text_w(txt), (float)h)) continue;   // collides with a more important label
        mu_Color col = p->size <= 0 ? mu_color(200,200,200,170) : p->nation == NATION_PIRATES ? mu_color(230,230,230,255) : ui_mcol(nations_info[p->nation].color, 255);
        ui_overlay_text(sx, ly, txt, col, 1, 1);
        if (i == U.hover_port && p->size > 0) ui_overlay_text(sx, sy - 12 - h - 8, va("%s, pop %.0fk", nations_info[p->nation].name, p->pop), mu_color(220,220,220,220), 1, 1);
    }
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        int mine = is_player(f->nation), focus = f->id == U.sel_fleet_id || f->id == U.hover_fleet_id;
        if (!focus && (!mine || f->at_port >= 0)) continue;
        float x, z, hd, sx, sy; scene_fleet_pos(f, &x, &z, &hd);
        if (!cam_project(x, 0.8f, z, &sx, &sy)) continue;
        ui_overlay_text(sx, sy + 12, va("%s (%d)", f->name, f->nships), ui_mcol(nations_info[f->nation].color, 255), 1, 1);
        if (focus) ui_overlay_text(sx, sy + 12 + h, va("%s - %s", nations_info[f->nation].name, ui_order(f)), mu_color(230,230,230,230), 1, 1);
    }
    if (U.toast_timer > 0) { int tw = ui_text_w(U.toast); ui_overlay_rect(W/2 - tw/2 - 10, 44, tw + 20, h + 8, mu_color(20,20,30,200)); ui_overlay_text(W/2, 44 + h/2 + 4, U.toast, mu_color(255,240,200,255), 1, 0); }
    if (G.nlog > 0 && G.day - G.log[G.nlog-1].day < 6) { const logline_t *l = &G.log[G.nlog-1]; ui_overlay_text(W/2, H - 14, va("%s: %s", ui_date(l->day), l->text), mu_color(255,235,190,230), 1, 1); }
    if (U.route_mode) ui_overlay_text(W/2, 80, "ROUTE MODE: left-click ports in order, right-click to finish", mu_color(140,255,170,255), 1, 1);
    if (G.speed == 0 && !U.in_menu) ui_overlay_text(W/2, 100, "- PAUSED -", mu_color(255,255,255,200), 1, 1);
}
