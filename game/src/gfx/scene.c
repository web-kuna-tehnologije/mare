// Scene assembly (see scene.h): LOD selection for ships and ports, procedural towns, icons,
// debug-draw overlays (paths, routes, lanes, storms, wind).
#include <math.h>
#include "scene.h"
#include "render.h"
#include "camera.h"
#include "../core/util.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/wind.h"
#include "../sim/economy.h"
#include "../sim/fleet.h"
#include "../sim/events.h"
#include "../sim/settlement.h"
#include "../world/regions.h"

#define LOD0_DIST         75.f
#define LOD1_DIST        230.f
#define PORT_DETAIL_DIST 260.f

scene_hints_t SH;

static float cam_distance(float x, float z) { float d = dist2d(x, z, cam.pos.x, cam.pos.z); return sqrtf(d*d + cam.pos.y*cam.pos.y); }

void scene_fleet_pos(const fleet_t *f, float *x, float *z, float *heading) {
    *x = f->x; *z = f->z; *heading = f->heading;
    if (f->at_port < 0) return;
    int k = 0; for (int i = 0; i < fleet_index(f); ++i) if (G.fleets[i].alive && G.fleets[i].at_port == f->at_port) k++;
    const port_t *p = &G.ports[f->at_port];
    float ang = atan2f(p->z - p->lz, p->x - p->lx);     // land -> sea
    float r = 1.2f + 0.9f * (k / 3), a = ang + ((k % 3) - 1) * 0.55f;
    *x = p->x + cosf(a) * r; *z = p->z + sinf(a) * r; *heading = -ang + 1.5708f;   // moored broadside to the shore
}

// ---------------------------------------------------------------------------
// ships

static instlist_t ship_inst[SHIP_COUNT][2];

void scene_build_ships(void) {
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) instlist_clear(&ship_inst[t][l]);
    for (int i = 0; i < G.nfleets; ++i) {
        const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        float x, z, heading; scene_fleet_pos(f, &x, &z, &heading);
        float d = cam_distance(x, z);
        if (d > LOD1_DIST || cam.mapness > 0.6f) continue;   // icons handle it
        unsigned col = f->role == ROLE_PIRATE ? rgb(20,20,20) : nations_info[f->nation].color;
        int sel = f->id == SH.sel_fleet_id, hov = f->id == SH.hover_fleet_id;
        int lod = d < LOD0_DIST ? 0 : 1, nshow = lod == 1 ? mini(f->nships, 4) : f->nships;
        float hx = cosf(heading), hz = -sinf(heading);
        for (int s = 0; s < nshow; ++s) {
            float sc = ship_scale(f->ships[s].type);
            float along = -(s * 1.05f) * 1.35f, side = ((s % 2) ? 0.45f : -0.45f) * (s > 0) * 1.35f;
            inst_t *in = instlist_push(&ship_inst[f->ships[s].type][lod]);
            *in = (inst_t){ x + hx*along - hz*side, 0, z + hz*along + hx*side, heading, sc, col_r(col), col_g(col), col_b(col), f->at_port >= 0 ? 0.3f : 1.f, sel ? 1.f : hov ? 0.5f : 0.f, 1, 0 };
        }
        if (sel || hov) dd_circle(vec3(x, 0.1f, z), vec3(0,1,0), (sel ? 1.6f : 1.4f) * 1.35f + f->nships * 0.2f, .color = rgb_linear(sel ? rgb(255, 240, 120) : rgb(220, 220, 220)));
        if (G.day - f->last_fire < 0.12f && f->at_port < 0) dd_sphere(vec3(x + hz*0.6f, 0.4f, z - hx*0.6f), .r = 0.35f, .color = rgb_linear(rgb(255, 200, 80)));
    }
}

// submit the instances built this frame with whatever pipeline is bound (main or shadow pass)
int scene_submit_ships(void) {
    int n = 0;
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) { mesh_draw(&ship_mesh[t][l], ship_inst[t][l].v, ship_inst[t][l].n); n += ship_inst[t][l].n; }
    return n;
}

// ---------------------------------------------------------------------------
// ports (towns are regenerated each frame from a per-port seed: cheap and always in sync)

static instlist_t houses, roofs, towers, walls, piers, flagpoles;

// per-port cache of the town instances: rebuilt only when what it depends on changes
enum { TM_HOUSE, TM_ROOF, TM_TOWER, TM_WALL, TM_PIER, TM_FLAG };
typedef struct town_item_t { unsigned char mesh; inst_t in; } town_item_t;
typedef struct town_cache_t { unsigned sig; int n; town_item_t *items; } town_cache_t;
static town_cache_t town_cache[MAX_PORTS];
static town_cache_t *tc_cur;
static void tc_push(int mesh, inst_t in) { town_cache_t *c = tc_cur; if (c->n % 64 == 0) c->items = realloc(c->items, (c->n + 64) * sizeof(town_item_t)); c->items[c->n].mesh = (unsigned char)mesh; c->items[c->n].in = in; c->n++; }
static unsigned town_signature(const port_t *p) {
    unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
    return 0x9E3779B1u * (unsigned)(p->size + 1) ^ 0x85EBCA6Bu * (unsigned)(p->building[B_FORT] + 1) ^ 0xC2B2AE35u * (unsigned)(mini((int)(p->pop / 12), 63) + 1) ^ col ^ (unsigned)(p->merged_into >= 0) << 31;
}

static void town_build(const port_t *p, int index) {
    unsigned seed = 1000 + index * 7919;
    float ang = atan2f(p->z - p->lz, p->x - p->lx);
    unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
    tc_push(TM_PIER, (inst_t){ p->x - cosf(ang)*0.9f, 0, p->z - sinf(ang)*0.9f, -ang, 0.9f, 1,1,1, 0,0,0, 2.0f });
    float fy = world_height(p->lx, p->lz);
    if (p->size <= 0) { tc_push(TM_FLAG, (inst_t){ p->lx, fy, p->lz, 0, 1.2f, 0.8f,0.8f,0.8f, 0,0,0,0 }); return; }
    tc_push(TM_FLAG, (inst_t){ p->lx + 0.3f, fy, p->lz + 0.3f, 0, 1.0f + 0.2f*p->size, col_r(col), col_g(col), col_b(col), 0,0,0,0 });
    int nh = mini(4 + 7 * p->size + (int)(p->pop / 12), 40);
    float rad = 0.55f + 0.3f * p->size;
    int med = p->region == REG_MED || p->region == REG_ATLANTIC, colonial = port_colonial(p);
    float roof[3]; if (med) { roof[0]=0.68f; roof[1]=0.32f; roof[2]=0.20f; } else if (colonial) { roof[0]=0.55f; roof[1]=0.42f; roof[2]=0.22f; } else { roof[0]=0.30f; roof[1]=0.32f; roof[2]=0.36f; }
    for (int k = 0; k < nh; ++k) {
        float a = rnd_f(&seed) * 6.2832f, r = sqrtf(rnd_f(&seed)) * rad, hx = p->lx + cosf(a) * r, hz = p->lz + sinf(a) * r;
        float s = 0.16f + rnd_f(&seed) * 0.12f, rot = rnd_f(&seed) * 3.14f, wc = 0.75f + rnd_f(&seed) * 0.2f;
        if (!world_is_land(hx, hz)) continue;
        float hy = world_height(hx, hz); if (hy < 0.05f) continue;
        float wr = med ? wc : colonial ? wc*0.9f : wc*0.7f, wg = med ? wc*0.92f : colonial ? wc*0.8f : wc*0.55f, wb = med ? wc*0.75f : colonial ? wc*0.6f : wc*0.45f;
        tc_push(TM_HOUSE, (inst_t){ hx, hy - 0.05f, hz, rot, s, wr, wg, wb, 0,0,0,0 });
        tc_push(TM_ROOF,  (inst_t){ hx, hy - 0.05f + s, hz, rot, s, roof[0], roof[1], roof[2], 0,0,0,0 });
    }
    if (p->size >= 2) tc_push(TM_TOWER, (inst_t){ p->lx, fy - 0.05f, p->lz, 0, 0.35f + 0.12f*p->size, 0.8f,0.78f,0.72f, 0,0,0,0 });
    if (p->building[B_FORT] > 0) {
        int nseg = 8 + 2 * p->building[B_FORT]; float wr = rad + 0.35f, wh = 0.22f + 0.05f * p->building[B_FORT];
        for (int k = 0; k < nseg; ++k) {
            float a0 = k * 6.2832f / nseg, a1 = (k+1) * 6.2832f / nseg;
            float x0 = p->lx + cosf(a0)*wr, z0 = p->lz + sinf(a0)*wr, x1 = p->lx + cosf(a1)*wr, z1 = p->lz + sinf(a1)*wr, mx = (x0+x1)*0.5f, mz = (z0+z1)*0.5f;
            if (!world_is_land(mx, mz)) continue;
            tc_push(TM_WALL, (inst_t){ mx, world_height(mx, mz) - 0.1f, mz, -atan2f(z1-z0, x1-x0), wh, 1,1,1, 0,0,0, dist2d(x0,z0,x1,z1) / wh });
        }
    }
}

void scene_reset_caches(void) { for (int i = 0; i < MAX_PORTS; ++i) town_cache[i].sig = 0, town_cache[i].n = 0; }   // world/heights may have changed (new game, load)

static void town_instances(const port_t *p, int index) {
    town_cache_t *c = &town_cache[index]; unsigned sig = town_signature(p);
    if (c->sig != sig || !c->items) { c->n = 0; c->sig = sig; tc_cur = c; town_build(p, index); }
    static instlist_t *lists[6] = { &houses, &roofs, &towers, &walls, &piers, &flagpoles };
    for (int i = 0; i < c->n; ++i) *instlist_push(lists[c->items[i].mesh]) = c->items[i].in;
}

void scene_build_ports(void) {
    instlist_clear(&houses); instlist_clear(&roofs); instlist_clear(&towers); instlist_clear(&walls); instlist_clear(&piers); instlist_clear(&flagpoles);
    for (int i = 0; i < G.nports; ++i) {
        const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        if (cam_distance(p->x, p->z) > PORT_DETAIL_DIST || cam.mapness > 0.5f) continue;
        town_instances(p, i);
        int sel = i == SH.sel_port, hov = i == SH.hover_port;
        if (sel || hov) dd_circle(vec3(p->x, 0.1f, p->z), vec3(0,1,0), 2.2f + 0.4f*p->size, .color = rgb_linear(sel ? rgb(255,240,120) : rgb(220,220,220)));
        if (p->blockaded_by) dd_circle(vec3(p->x, 0.15f, p->z), vec3(0,1,0), 3.5f, .color = rgb_linear(rgb(255,60,40)), .dashed = 1);
    }
}

int scene_submit_ports(void) {
    mesh_draw(&mesh_pier, piers.v, piers.n); mesh_draw(&mesh_house, houses.v, houses.n); mesh_draw(&mesh_roof, roofs.v, roofs.n);
    mesh_draw(&mesh_tower, towers.v, towers.n); mesh_draw(&mesh_wall, walls.v, walls.n); mesh_draw(&mesh_flagpole, flagpoles.v, flagpoles.n);
    return piers.n + houses.n + roofs.n + towers.n + walls.n + flagpoles.n;
}

// shadow casters: towns and ships (piers and flagpoles are too thin to matter)
int scene_submit_shadow(void) {
    mesh_draw(&mesh_house, houses.v, houses.n); mesh_draw(&mesh_roof, roofs.v, roofs.n);
    mesh_draw(&mesh_tower, towers.v, towers.n); mesh_draw(&mesh_wall, walls.v, walls.n);
    int n = houses.n + roofs.n + towers.n + walls.n;
    return n + scene_submit_ships();
}

// ---------------------------------------------------------------------------
// icons

static void icon(float x, float z, float heading, float size, int shape, float alpha, float outline, unsigned col) {
    icon_t *ic = icon_push(); *ic = (icon_t){ x, 0.3f, z, heading, size, (float)shape, alpha, outline, col_r(col), col_g(col), col_b(col), 0 };
}

void scene_push_icons(void) {
    for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        if (cam_distance(p->x, p->z) <= PORT_DETAIL_DIST && cam.mapness <= 0.5f) continue;
        unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
        float sz = p->size <= 0 ? 5.f : 6.f + 2.5f * p->size;
        icon(p->x, p->z, 0, sz, p->size <= 0 ? ICON_DIAMOND : ICON_DISC, p->size <= 0 ? 0.8f : 1.f, 1.f, col);
        if (p->blockaded_by) icon(p->x, p->z, 0, sz + 8, ICON_RING, 0.9f, 0, rgb(255,50,25));
        if (p->haven && p->size > 0) icon(p->x, p->z, 0.785f, sz * 0.6f, ICON_CROSS, 1, 0, rgb(12,12,12));
        if (i == SH.sel_port || i == SH.hover_port) icon(p->x, p->z, 0, sz + 10, ICON_RING, 0.9f, 0, rgb(255,240,128));
    }
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        float x, z, heading; scene_fleet_pos(f, &x, &z, &heading);
        if (cam_distance(x, z) <= LOD1_DIST && cam.mapness <= 0.6f) continue;
        int sel = f->id == SH.sel_fleet_id, hov = f->id == SH.hover_fleet_id;
        unsigned col = f->role == ROLE_PIRATE ? rgb(25,25,25) : nations_info[f->nation].color;
        icon(x, z, heading, sel ? 13.f : is_player(f->nation) ? 11.f : 9.f, ICON_ARROW, 1, 1, col);
        if (sel || hov) icon(x, z, 0, 20, ICON_RING, 0.9f, 0, rgb(255,240,128));
    }
    if (SH.show_regions || cam.mapness > 0.4f) for (int i = 0; i < G.ninland; ++i) { const inland_t *s = &G.inland[i]; if (!s->alive) continue;   // mines and farms of the hinterlands
        icon(s->x, s->z, 0, 6 + 2 * minf(s->pop, 4), ICON_SQUARE, 0.95f, 1, goods[s->good].color); }
    if (cam.mapness > 0.4f) for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type == EV_STORM) icon(e->x, e->z, RF.time*0.8f, 22, ICON_STORM, 0.9f, 0, rgb(64,64,90)); }
}

// ---------------------------------------------------------------------------
// debug-draw overlays

void scene_draw_overlays(void) {
    const fleet_t *f = fleet_by_id(SH.sel_fleet_id);
    if (f && f->at_port < 0 && f->pathi < f->npath) {
        float px = f->x, pz = f->z;
        for (int i = f->pathi; i < f->npath; ++i) { dd_line(vec3(px, 0.15f, pz), vec3(f->path_x[i], 0.15f, f->path_z[i]), .color = rgb_linear(rgb(255, 240, 140)), .top = 1); px = f->path_x[i]; pz = f->path_z[i]; }
        if (f->target_port >= 0) dd_circle(vec3(G.ports[f->target_port].x, 0.15f, G.ports[f->target_port].z), vec3(0,1,0), 2.5f, .color = rgb_linear(rgb(255,240,140)), .top = 1);
    }
    if (f && f->nroute >= 2) for (int i = 0; i < f->nroute; ++i) { const port_t *a = &G.ports[f->route[i]], *b = &G.ports[f->route[(i+1)%f->nroute]];
        if (SH.route_edit && i + 1 == f->nroute) break;
        dd_line(vec3(a->x, 0.2f, a->z), vec3(b->x, 0.2f, b->z), .color = rgb_linear(SH.route_edit ? rgb(120,255,160) : rgb(120, 220, 255)), .dashed = !SH.route_edit, .top = 1); }
    if (SH.show_lanes && cam.mapness > 0.2f)
        for (int i = 0; i < G.nlanes; ++i) { const lane_t *l = &G.lanes[i]; if (l->traffic < 0.8f) continue;
            float t = clampf(l->traffic / 12.f, 0.15f, 1.f); unsigned c = rgb_linear(rgb((unsigned)(90 + 120*t), (unsigned)(60 + 60*t), (unsigned)(30 + 20*t)));
            for (int k = 0; k + 1 < l->npts; ++k) dd_line(vec3(l->x[k], 0.25f, l->z[k]), vec3(l->x[k+1], 0.25f, l->z[k+1]), .color = c, .top = 1); }
    for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type != EV_STORM) continue;
        float t = RF.time;
        for (int k = 0; k < 3; ++k) dd_circle(vec3(e->x, 0.4f + k*0.3f, e->z), vec3(0,1,0), e->radius * (0.55f + 0.22f*k) + sinf(t*2 + k)*0.5f, .color = rgb_linear(rgb(70, 70, 90)), .top = cam.mapness > 0.5f);
        if (cam.mapness < 0.4f) for (int k = 0; k < 24; ++k) { float a = k * 0.2618f + t*0.7f, r = e->radius * (0.3f + 0.6f*((k*7)%5)/5.f), x = e->x + cosf(a)*r, z = e->z + sinf(a)*r;
            dd_line(vec3(x, 3.5f, z), vec3(x - 0.4f, 0.2f, z + 0.3f), .color = rgb_linear(rgb(120,130,160))); }
    }
    if (SH.show_wind && cam.mapness < 0.9f) {
        float step = maxf(3, cam.dist * 0.18f + 2), x0 = floorf((cam.tx - step*7) / step) * step, z0 = floorf((cam.tz - step*5) / step) * step;
        for (int j = 0; j < 11; ++j) for (int i = 0; i < 15; ++i) {
            float x = x0 + i*step, z = z0 + j*step; if (world_is_land(x, z)) continue;
            float dx, dz, s; wind_at(x, z, G.day, &dx, &dz, &s);
            float l = step * 0.3f * s, ph = fmodf(RF.time*0.5f + i*0.37f + j*0.61f, 1.f);
            vec3 a = vec3(x + dx*l*(ph-0.5f), 0.6f, z + dz*l*(ph-0.5f)), b = vec3(a.x + dx*l*0.5f, 0.6f, a.z + dz*l*0.5f);
            dd_arrow(a, b, .color = rgb_linear(rgb(200, 215, 235))); }
    }
}
