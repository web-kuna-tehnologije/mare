// microui helpers. v3 wraps every frame in a "debug" window; our windows are nested root
// containers inside it, and a frameless full-screen overlay (zindex -1) carries labels.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ui.h"
#include "../core/util.h"
#include "../data/tables.h"

ui_state_t U = { .sel_port = -1, .hover_port = -1, .show_fleets = 1, .show_log = 1, .show_lanes = 1, .in_menu = 1,
                 .pass_terrain = 1, .pass_sea = 1, .pass_ports = 1, .pass_ships = 1, .pass_overlays = 1, .pass_icons = 1, .pass_labels = 1, .pass_panels = 1 };
mu_Container *ui_overlay;

// ids depend on the id stack; the framework's debug window was created at the root
mu_Container *ui_root_container(const char *name) { int bak = ui_ctx->id_stack.idx; ui_ctx->id_stack.idx = 0; mu_Container *c = mu_exists(ui_ctx, name); ui_ctx->id_stack.idx = bak; return c; }
int ui_mouse_over_panel(void) { return ui_ctx->hover_root && ui_ctx->hover_root != ui_overlay && ui_ctx->hover_root != ui_root_container("debug"); }

void ui_overlay_begin(void) {
    mu_Container *c = mu_get_container(ui_ctx, "##overlay");
    c->rect = mu_rect(0, 0, screen_w, screen_h);
    mu_begin_window_ex(ui_ctx, "##overlay", c->rect, MU_OPT_NOFRAME|MU_OPT_NOTITLE|MU_OPT_NORESIZE|MU_OPT_NOSCROLL|MU_OPT_NOINTERACT|MU_OPT_NOCLOSE);
    ui_overlay = mu_get_current_container(ui_ctx);
}
void ui_overlay_end(void) { mu_Container *c = mu_get_current_container(ui_ctx); mu_end_window(ui_ctx); c->zindex = -1; }   // never brought to front
int  ui_text_w(const char *s) { return ui_ctx->text_width(NULL, s, -1); }
void ui_overlay_text(float sx, float sy, const char *s, mu_Color col, int centered, int shadow) {
    int w = ui_text_w(s), h = ui_ctx->text_height(NULL), x = (int)(centered ? sx - w/2 : sx), y = (int)(sy - h/2);
    if (shadow) mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(x+1, y+1), mu_color(0,0,0,160));
    mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(x, y), col);
}
void ui_overlay_rect(float x, float y, float w, float h, mu_Color col) { mu_draw_rect(ui_ctx, mu_rect((int)x, (int)y, (int)w, (int)h), col); }

void ui_row(int n, const int *widths, int h) { mu_layout_row(ui_ctx, n, widths, h); }
int  ui_btn(const char *label) { return mu_button_ex(ui_ctx, label, 0, MU_OPT_ALIGNCENTER); }
void ui_text_full(const char *s) { int w[1] = { -1 }; mu_layout_row(ui_ctx, 1, w, 0); mu_text(ui_ctx, s); }
void ui_label_col(const char *s, mu_Color c) { mu_Rect r = mu_layout_next(ui_ctx); mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(r.x, r.y + (r.h - ui_ctx->text_height(NULL))/2), c); }
mu_Color ui_mcol(unsigned c, int a) { return mu_color(c & 255, (c>>8) & 255, (c>>16) & 255, a); }

void toast(const char *fmt, ...) { va_list va; va_start(va, fmt); vsnprintf(U.toast, sizeof U.toast, fmt, va); va_end(va); U.toast_timer = 4; }

const char *ui_money(double v) { return va("%s%.0f", v < 0 ? "-" : "", fabs(v)); }
const char *ui_date(float day) {
    static const char *mn[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int y, m, d; day2date(day, &y, &m, &d); return va("%d %s %d", d, mn[m-1], y);
}
const char *ui_order(const fleet_t *f) {
    const char *tp = f->target_port >= 0 ? G.ports[f->target_port].name : "?";
    switch (f->order) {
        case ORDER_IDLE:     return f->at_port >= 0 ? va("Docked at %s", G.ports[f->at_port].name) : "Idle at sea";
        case ORDER_MOVE:     return "Sailing";
        case ORDER_DOCK:     return f->at_port >= 0 ? va("Docked at %s", G.ports[f->at_port].name) : va("Sailing to %s", tp);
        case ORDER_TRADE:    return va("Trading -> %s", tp);
        case ORDER_ATTACK:   { const fleet_t *t = fleet_by_id(f->target_fleet_id); return va("Attacking %s", t ? t->name : "?"); }
        case ORDER_PATROL:   return "Patrolling";
        case ORDER_SIEGE:    return va("Besieging %s", tp);
        case ORDER_BLOCKADE: return va("Blockading %s", tp);
        case ORDER_COLONIZE: return va("Settlers bound for %s", tp);
        case ORDER_FLEE:     return "Fleeing!";
        case ORDER_RAID:     return "Hunting";
    }
    return "?";
}
