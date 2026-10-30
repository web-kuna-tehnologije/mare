// mare::ui::UiKit (see ui.h): microui helpers. v3 wraps every frame in a "debug" window; our
// windows are nested root containers inside it, and a frameless full-screen overlay (zindex -1)
// carries labels. The ui-state struct U and the overlay container stay plain C globals (ui.h
// declares them), defined here.

#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

extern "C" {
#include "ui.h"
#include "../core/util.h"
#include "../data/tables.h"
#include "../sim/fleet.h"     // fleet_by_id (ui_order)
#include "../gfx/render.h"    // screen_w / screen_h (the overlay)
}

ui_state_t U = { .sel_port = -1, .hover_port = -1, .show_fleets = 1, .show_log = 1, .show_lanes = 1, .in_menu = 1,
                 .pass_terrain = 1, .pass_sea = 1, .pass_ports = 1, .pass_ships = 1, .pass_overlays = 1, .pass_icons = 1, .pass_labels = 1, .pass_panels = 1 };
mu_Container *ui_overlay;

namespace mare::ui {

class UiKit {
public:
    mu_Container *root_container(const char *name);
    int  mouse_over_panel(void);
    void overlay_begin(void);
    void overlay_end(void);
    void overlay_text(float sx, float sy, const char *s, mu_Color col, int centered, int shadow);
    void overlay_rect(float x, float y, float w, float h, mu_Color col);
    int  text_w(const char *s);
    void row(int n, const int *widths, int h);
    int  btn(const char *label);
    void text_full(const char *s);
    void label_col(const char *s, mu_Color c);
    mu_Color mcol(unsigned c, int a);
    void toast(const char *fmt, ...);
    void vtoast(const char *fmt, va_list va);
    const char *money(double v);
    const char *date(float day);
    const char *order(const fleet_t *f);
};

UiKit &uikit() { static UiKit k; return k; }

// ids depend on the id stack; the framework's debug window was created at the root
mu_Container *UiKit::root_container(const char *name) { int bak = ui_ctx->id_stack.idx; ui_ctx->id_stack.idx = 0; mu_Container *c = mu_exists(ui_ctx, name); ui_ctx->id_stack.idx = bak; return c; }
int UiKit::mouse_over_panel(void) { return ui_ctx->hover_root && ui_ctx->hover_root != ui_overlay && ui_ctx->hover_root != root_container("debug"); }

void UiKit::overlay_begin(void) {
    mu_Container *c = mu_get_container(ui_ctx, "##overlay");
    c->rect = mu_rect(0, 0, screen_w, screen_h);
    mu_begin_window_ex(ui_ctx, "##overlay", c->rect, MU_OPT_NOFRAME|MU_OPT_NOTITLE|MU_OPT_NORESIZE|MU_OPT_NOSCROLL|MU_OPT_NOINTERACT|MU_OPT_NOCLOSE);
    ui_overlay = mu_get_current_container(ui_ctx);
}
void UiKit::overlay_end(void) { mu_Container *c = mu_get_current_container(ui_ctx); mu_end_window(ui_ctx); c->zindex = -1; }   // never brought to front
int  UiKit::text_w(const char *s) { return ui_ctx->text_width(NULL, s, -1); }
void UiKit::overlay_text(float sx, float sy, const char *s, mu_Color col, int centered, int shadow) {
    int w = text_w(s), h = ui_ctx->text_height(NULL), x = (int)(centered ? sx - w/2 : sx), y = (int)(sy - h/2);
    if (shadow) mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(x+1, y+1), mu_color(0,0,0,160));
    mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(x, y), col);
}
void UiKit::overlay_rect(float x, float y, float w, float h, mu_Color col) { mu_draw_rect(ui_ctx, mu_rect((int)x, (int)y, (int)w, (int)h), col); }

void UiKit::row(int n, const int *widths, int h) { mu_layout_row(ui_ctx, n, widths, h); }
int  UiKit::btn(const char *label) { return mu_button_ex(ui_ctx, label, 0, MU_OPT_ALIGNCENTER); }
void UiKit::text_full(const char *s) { int w[1] = { -1 }; mu_layout_row(ui_ctx, 1, w, 0); mu_text(ui_ctx, s); }
void UiKit::label_col(const char *s, mu_Color c) { mu_Rect r = mu_layout_next(ui_ctx); mu_draw_text(ui_ctx, NULL, s, -1, mu_vec2(r.x, r.y + (r.h - ui_ctx->text_height(NULL))/2), c); }
mu_Color UiKit::mcol(unsigned c, int a) { return mu_color(c & 255, (c>>8) & 255, (c>>16) & 255, a); }

void UiKit::toast(const char *fmt, ...) { va_list va; va_start(va, fmt); vtoast(fmt, va); va_end(va); }
void UiKit::vtoast(const char *fmt, va_list va) { vsnprintf(U.toast, sizeof U.toast, fmt, va); U.toast_timer = 4; }

const char *UiKit::money(double v) { return va("%s%.0f", v < 0 ? "-" : "", fabs(v)); }
const char *UiKit::date(float day) {
    static const char *mn[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int y, m, d; day2date(day, &y, &m, &d); return va("%d %s %d", d, mn[m-1], y);
}
const char *UiKit::order(const fleet_t *f) {
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

} // namespace mare::ui

// C ABI (ui.h) for the unported callers
extern "C" {
mu_Container *ui_root_container(const char *name) { return mare::ui::uikit().root_container(name); }
int  ui_mouse_over_panel(void) { return mare::ui::uikit().mouse_over_panel(); }
void ui_overlay_begin(void) { mare::ui::uikit().overlay_begin(); }
void ui_overlay_end(void) { mare::ui::uikit().overlay_end(); }
void ui_overlay_text(float sx, float sy, const char *s, mu_Color col, int centered, int shadow) { mare::ui::uikit().overlay_text(sx, sy, s, col, centered, shadow); }
void ui_overlay_rect(float x, float y, float w, float h, mu_Color col) { mare::ui::uikit().overlay_rect(x, y, w, h, col); }
int  ui_text_w(const char *s) { return mare::ui::uikit().text_w(s); }
void ui_row(int n, const int *widths, int h) { mare::ui::uikit().row(n, widths, h); }
int  ui_btn(const char *label) { return mare::ui::uikit().btn(label); }
void ui_text_full(const char *s) { mare::ui::uikit().text_full(s); }
void ui_label_col(const char *s, mu_Color c) { mare::ui::uikit().label_col(s, c); }
mu_Color ui_mcol(unsigned c, int a) { return mare::ui::uikit().mcol(c, a); }
void toast(const char *fmt, ...) { va_list va; va_start(va, fmt); mare::ui::uikit().vtoast(fmt, va); va_end(va); }
const char *ui_money(double v) { return mare::ui::uikit().money(v); }
const char *ui_date(float day) { return mare::ui::uikit().date(day); }
const char *ui_order(const fleet_t *f) { return mare::ui::uikit().order(f); }
}
