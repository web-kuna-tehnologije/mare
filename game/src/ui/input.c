// Mouse picking, player commands, hotkeys and camera control.

#include <math.h>
#include "ui.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/nav.h"
#include "../sim/economy.h"
#include "../sim/fleet.h"
#include "../sim/game.h"
#include "../sim/settlement.h"
#include "../gfx/camera.h"
#include "../gfx/render.h"
#include "../gfx/scene.h"

// mouse position in framebuffer pixels (GLFW reports window coordinates; they differ on HiDPI displays)
static float mouse_px(void) { return mouse('x') * (float)screen_w / (float)window_w; }
static float mouse_py(void) { return mouse('y') * (float)screen_h / (float)window_h; }

fleet_t *ui_player_fleet(void) { fleet_t *f = fleet_by_id(U.sel_fleet_id); return f && is_player(f->nation) ? f : 0; }
void ui_select_fleet(const fleet_t *f) { U.sel_fleet_id = f ? f->id : 0; U.sel_port = -1; U.route_mode = 0; }
void ui_select_port(int i) { U.sel_port = i; U.sel_fleet_id = 0; U.route_mode = 0; }

// ---------------------------------------------------------------------------
// picking: nearest fleet/port marker in screen space; port name labels are clickable too

static void pick(float mx, float my) {
    U.hover_fleet_id = 0; U.hover_port = -1;
    float best = 16*16; const fleet_t *bf = 0; float bfx = 0, bfy = 0;
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        float x, z, h, sx, sy; scene_fleet_pos(f, &x, &z, &h);
        if (!cam_project(x, 0.2f, z, &sx, &sy)) continue;
        float d = sqr(sx-mx) + sqr(sy-my); if (is_player(f->nation)) d *= 0.6f;
        if (d < best) { best = d; bf = f; bfx = sx; bfy = sy; } }
    float bestp = 18*18; int bp = -1, label_hit = 0; float bpx = 0, bpy = 0;
    for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        float sx, sy; if (!cam_project(p->x, 0.2f, p->z, &sx, &sy)) continue;
        float d = sqr(sx-mx) + sqr(sy-my);
        float ly = sy - 12 - (cam.mapness > 0.5f ? 2 + 2*p->size : 8) - 2, lw = ui_text_w(p->name) * 0.5f + 4;
        if (fabsf(mx - sx) < lw && fabsf(my - ly) < 9) { d = 0; label_hit = 1; }
        if (d < bestp) { bestp = d; bp = i; bpx = sx; bpy = sy; } }
    if (label_hit) { U.hover_port = bp; return; }
    if (bf && bp >= 0) { // both: closest wins, a fleet docked at that port wins
        if (bf->at_port == bp || sqr(bfx-mx)+sqr(bfy-my) <= sqr(bpx-mx)+sqr(bpy-my)) bp = -1; else bf = 0;
    }
    U.hover_fleet_id = bf ? bf->id : 0; U.hover_port = bp;
}

// ---------------------------------------------------------------------------
// commands

static void right_click(float mx, float my) {
    fleet_t *f = ui_player_fleet();
    if (U.route_mode && f) {
        if (f->nroute >= 2) { f->autotrade = 1; if (f->at_port >= 0) f->think_timer = 0.1f; toast("Trade route set (%d ports). The fleet will trade on its own.", f->nroute); }
        else { toast("A route needs at least 2 ports"); f->nroute = 0; }
        U.route_mode = 0; return;
    }
    if (!f) { if (U.hover_port >= 0) ui_select_port(U.hover_port); return; }
    const fleet_t *t = fleet_by_id(U.hover_fleet_id);
    if (t && t != f) {
        if (!at_war(f->nation, t->nation)) toast("%s is not hostile (%s)", t->name, nations_info[t->nation].name);
        else if (fleet_guns(f) <= 0) toast("This fleet has no guns");
        else { fleet_order_attack(f, t); toast("%s: attack %s!", f->name, t->name); }
        return;
    }
    if (U.hover_port >= 0) {
        port_t *p = &G.ports[U.hover_port];
        int shift = KHELD(GLFW_KEY_LEFT_SHIFT) || KHELD(GLFW_KEY_RIGHT_SHIFT), ctrl = KHELD(GLFW_KEY_LEFT_CONTROL) || KHELD(GLFW_KEY_RIGHT_CONTROL);
        if (port_is_site(p)) {
            if (G.nations[f->nation].treasury >= COLONY_COST) { fleet_order_colonize(f, U.hover_port); toast("Settlers sail to found %s (%.0f ducats on arrival)", p->name, COLONY_COST); }
            else toast("Founding a colony requires %.0f ducats", COLONY_COST);
        } else if (at_war(f->nation, p->nation) || (shift && p->nation != f->nation && (p->nation == NATION_NONE || p->nation == NATION_PIRATES))) {
            if (fleet_guns(f) >= 10) { fleet_order_siege(f, U.hover_port, 0); toast("%s lays siege to %s", f->name, p->name); } else toast("Not enough guns to besiege %s", p->name);
        } else if (shift && p->nation != f->nation) toast("You must declare war on %s first (Stats panel)", nations_info[p->nation].name);
        else if (ctrl && p->nation != f->nation) {
            if (fleet_guns(f) >= 10) { fleet_order_siege(f, U.hover_port, 1); toast("%s will blockade %s", f->name, p->name); } else toast("Not enough guns to blockade");
        } else if (p->blockaded_by && p->blockaded_by != f->nation) toast("%s is blockaded by %s", p->name, nations_info[p->blockaded_by].name);
        else { fleet_order_dock(f, U.hover_port); if (f->order == ORDER_DOCK) toast("%s sails for %s", f->name, p->name); else toast("No sea route to %s", p->name); }
        return;
    }
    float wx, wz;
    if (!cam_pick_ground(mx, my, 0, &wx, &wz)) return;
    if (world_is_land(wx, wz)) {   // free colonisation: claim this coast
        char reason[96];
        if (!site_check(wx, wz, f->nation, reason, sizeof reason)) { toast("%s", reason); return; }
        if (G.nations[f->nation].treasury < COLONY_COST) { toast("Founding a colony requires %.0f ducats", COLONY_COST); return; }
        int s = site_create(wx, wz, f->nation); if (s < 0) { toast("Cannot found a colony here"); return; }
        fleet_order_colonize(f, s);
        if (f->order == ORDER_COLONIZE) { char res[96] = ""; int n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (G.ports[s].potential[g] > 0) n += snprintf(res+n, sizeof res-n, "%s%s", n ? ", " : "", goods[g].name); toast("Settlers sail to found %s (%s)", G.ports[s].name, res); }
        else toast("No sea route to that coast");
        return;
    }
    if ((KHELD(GLFW_KEY_LEFT_SHIFT) || KHELD(GLFW_KEY_RIGHT_SHIFT)) && fleet_guns(f) > 0) { fleet_order_patrol(f, wx, wz); toast("%s patrols the area", f->name); }
    else { fleet_order_move(f, wx, wz); if (f->order != ORDER_MOVE) toast("No sea route there"); }
}

static void left_click(void) {
    fleet_t *f = ui_player_fleet();
    if (U.route_mode && f && U.hover_port >= 0) {
        if (f->nroute < MAX_ROUTE) { f->route[f->nroute++] = U.hover_port; toast("Route: %d/%d ports. Right-click to finish.", f->nroute, MAX_ROUTE); }
        return;
    }
    if (U.hover_fleet_id) ui_select_fleet(fleet_by_id(U.hover_fleet_id));
    else if (U.hover_port >= 0) ui_select_port(U.hover_port);
    else { U.sel_fleet_id = 0; U.sel_port = -1; U.route_mode = 0; }
}

// ---------------------------------------------------------------------------
// camera control

static void camera_input(float dt, int ui_blocked) {
    float mx = mouse_px(), my = mouse_py();
    static float last_mx, last_my; float dmx = mx - last_mx, dmy = my - last_my; last_mx = mx; last_my = my;
    float pan = cam.want_dist * 0.9f * dt;
    float kx = (float)((key('d') || KHELD(GLFW_KEY_RIGHT)) - (key('a') || KHELD(GLFW_KEY_LEFT))), kz = (float)((key('s') || KHELD(GLFW_KEY_DOWN)) - (key('w') || KHELD(GLFW_KEY_UP)));
    if (kx || kz) {   // camera right = (-gz, gx), forward = (gx, gz)
        float gx = sinf(cam.yaw), gz = -cosf(cam.yaw);
        cam.want_tx += (kx * -gz + kz * -gx) * pan; cam.want_tz += (kx * gx + kz * -gz) * pan; cam.follow_fleet_id = 0;
    }
    if (key('q')) cam.want_yaw -= 1.6f * dt;
    if (key('e')) cam.want_yaw += 1.6f * dt;
    int wheel = mouse('w');   // v3 reports wheel-up as negative
    if (wheel && !ui_blocked) {
        float bx, bz; int hit = cam_pick_ground(mx, my, 0, &bx, &bz);
        float f = powf(0.86f, clampf(-wheel / 100.f, -3, 3));
        cam.want_dist = clampf(cam.want_dist * f, CAM_MIN_DIST, CAM_MAX_DIST);
        if (hit && f < 1) { cam.want_tx += (bx - cam.want_tx) * (1 - f) * 0.9f; cam.want_tz += (bz - cam.want_tz) * (1 - f) * 0.9f; cam.follow_fleet_id = 0; }
    }
    static int rotating = 0;
    if (mouse_down('m') && !ui_blocked) rotating = 1;
    if (!mouse_held('m')) rotating = 0;
    if (rotating) { cam.want_yaw += dmx * 0.006f; cam.want_dist *= powf(1.004f, dmy); }
    if (cam.follow_fleet_id) { const fleet_t *f = fleet_by_id(cam.follow_fleet_id); if (f) { float x, z, h; scene_fleet_pos(f, &x, &z, &h); cam.want_tx = x; cam.want_tz = z; } else cam.follow_fleet_id = 0; }
}

// ---------------------------------------------------------------------------

void input_frame(float dt, int ui_blocked) {
    float mx = mouse_px(), my = mouse_py();
    pick(mx, my);
    if (ui_blocked) { U.hover_fleet_id = 0; U.hover_port = -1; }

    // right button: click = command, drag = pan the map under the cursor
    static int rdown = 0, rpan = 0; static float rx0, ry0, rlx, rly;
    int rpressed = mouse_held('r') || mouse_down('r');
    if (mouse_down('r') && !ui_blocked) { rdown = 1; rpan = 0; rx0 = rlx = mx; ry0 = rly = my; }
    if (rdown && rpressed) {
        if (!rpan && (fabsf(mx - rx0) > 6 || fabsf(my - ry0) > 6)) rpan = 1;
        if (rpan) { float ax, az, bx, bz;
            if (cam_pick_ground(rlx, rly, 0, &ax, &az) && cam_pick_ground(mx, my, 0, &bx, &bz)) { cam.want_tx += ax - bx; cam.want_tz += az - bz; cam.tx = cam.want_tx; cam.tz = cam.want_tz; cam.follow_fleet_id = 0; }
            rlx = mx; rly = my; }
    }
    if (rdown && !rpressed) { if (!rpan) right_click(mx, my); rdown = rpan = 0; }
    static float lx0, ly0; static int ldown = 0;
    if (mouse_down('l') && !ui_blocked) { ldown = 1; lx0 = mx; ly0 = my; }
    if (ldown && !(mouse_held('l') || mouse_down('l'))) { if (!ui_blocked && fabsf(mx - lx0) < 6 && fabsf(my - ly0) < 6) left_click(); ldown = 0; }

    // hotkeys
    { static const float speeds[4] = SIM_SPEEDS;
      if (key_down(' ')) G.speed = G.speed == 0 ? SIM_SPEED_DEFAULT : 0;
      for (int k = 0; k < 4; ++k) if (key_down('1' + k)) G.speed = speeds[k]; }
    if (KDOWN(GLFW_KEY_ESCAPE)) { if (U.route_mode) U.route_mode = 0; else if (U.show_help) U.show_help = 0; else { U.sel_fleet_id = 0; U.sel_port = -1; } }
    if (KDOWN(GLFW_KEY_F1)) U.show_help ^= 1;
    if (KDOWN(GLFW_KEY_F3)) U.show_debug ^= 1;
    if (KDOWN(GLFW_KEY_F5)) { int ok = game_save("mare_save.bin"); toast(ok ? "Game saved" : "Save failed"); printf("save: %s\n", ok ? "ok" : "failed"); }
    if (KDOWN(GLFW_KEY_F9)) { if (game_load("mare_save.bin")) { ui_select_fleet(0); scene_reset_caches(); toast("Game loaded"); } else toast("No compatible saved game"); }
    if (key_down('h')) { int c = G.nations[G.player].capital; if (c >= 0) cam_goto(G.ports[c].x, G.ports[c].z); }
    if (key_down('f') && U.sel_fleet_id) cam.follow_fleet_id = cam.follow_fleet_id == U.sel_fleet_id ? 0 : U.sel_fleet_id;
    if (key_down('m')) { if (cam.want_dist < CAM_MAP_FULL) { U.saved_dist = cam.want_dist; cam.want_dist = 700; } else cam.want_dist = U.saved_dist > 0 ? U.saved_dist : 60; }
    if (KDOWN(GLFW_KEY_TAB)) {   // cycle the player's fleets
        int start = -1; for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && G.fleets[i].id == U.sel_fleet_id) start = i;
        for (int k = 1; k <= G.nfleets; ++k) { int i = (start + k) % maxi(G.nfleets, 1); fleet_t *f = &G.fleets[i];
            if (f->alive && is_player(f->nation)) { ui_select_fleet(f); float x, z, h; scene_fleet_pos(f, &x, &z, &h); cam_goto(x, z); break; } }
    }
    if (KDOWN(GLFW_KEY_DELETE)) { fleet_t *f = ui_player_fleet(); if (f) { if (f->at_port >= 0) { G.nations[G.player].treasury += fleet_value(f) * 0.5; fleet_destroy(f); ui_select_fleet(0); toast("Fleet sold for scrap"); } else toast("Fleets can only be scrapped in port"); } }
    camera_input(dt, ui_blocked);
}
