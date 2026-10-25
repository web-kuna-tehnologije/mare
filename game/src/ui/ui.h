// UI layer: state shared by panels, input and labels, plus a small microui kit.
//
// All v3/microui/GLFW dependent code lives under src/ui and src/gfx. Panels are microui root
// windows opened inside the frame; a frameless full-screen overlay (zindex -1) carries world
// labels and toasts behind them. Selection always uses fleet ids, never slot indices.

#ifndef MARE_UI_H
#define MARE_UI_H

#include "../core/types.h"

typedef struct ui_state_t {
    int sel_fleet_id, hover_fleet_id;   // 0 = none
    int sel_port, hover_port;           // -1 = none
    int route_mode;                     // clicking ports appends them to the selected fleet's route
    int show_fleets, show_log, show_lanes, show_wind, show_regions, show_help, show_stats, show_debug;   // panel toggles (show_debug = dev dashboard)
    int in_menu;                        // main menu (nation choice) is up
    char toast[160]; float toast_timer; // transient notification at the top of the screen
    float saved_dist;                   // zoom level to return to after map view (M key)
    int city_view;                      // camera is in the city view over a town: the clock crawls (mare.c)
    int pass_terrain, pass_sea, pass_ports, pass_ships, pass_overlays, pass_icons, pass_labels, pass_panels; // dev: render pass toggles (1 = on)
} ui_state_t;
extern ui_state_t U;

// keys. v3's KEY_* enum names are shadowed on Linux by <linux/input.h> macros that GLFW's
// joystick code drags in, so special keys are addressed by GLFW code through v3's raw state.
#define KDOWN(glfw_key) (button_(glfw_key) == key_down)   // pressed this frame
#define KHELD(glfw_key) (button_(glfw_key) != key_idle)   // down (pressed, held or released this frame)

// ---- microui kit (uikit.c) ------------------------------------------------------------------------
extern mu_Container *ui_overlay;
mu_Container *ui_root_container(const char *name);   // container created at the root of the id stack (v3's "debug" window)
int  ui_mouse_over_panel(void);                      // is the mouse over one of our panels (not the overlay)?
void ui_overlay_begin(void);                         // full-screen label layer behind the panels; call before drawing labels
void ui_overlay_end(void);
void ui_overlay_text(float sx, float sy, const char *s, mu_Color col, int centered, int shadow);   // framebuffer pixels
void ui_overlay_rect(float x, float y, float w, float h, mu_Color col);
int  ui_text_w(const char *s);                       // text width in pixels (UI font)
void ui_row(int n, const int *widths, int h);        // microui layout row (negative width = from the right edge)
#define ROW(n, ...) ui_row(n, (int[]){ __VA_ARGS__ }, ((int[]){ __VA_ARGS__ })[n])   // n widths then the row height
int  ui_btn(const char *label);                      // centred button, 1 when clicked
void ui_text_full(const char *s);                    // wrapped paragraph across the full width
void ui_label_col(const char *s, mu_Color c);        // coloured label in the next layout cell
mu_Color ui_mcol(unsigned c, int a);                 // 0xAABBGGRR -> mu_Color with alpha a
void toast(const char *fmt, ...);                    // 4-second notification

// strings
const char *ui_money(double v);
const char *ui_date(float day);                      // "12 May 1620"
const char *ui_order(const fleet_t *f);              // human readable current order

// ---- panels (hud.c) -------------------------------------------------------------------------------
void hud_panels(void);                               // top bar, selection, fleets, chronicle, stats, help
int  hud_menu(void);                                 // main menu; returns chosen nation, -1 for load, 0 nothing
void hud_labels(void);                               // world labels, toast, ticker (inside the overlay)
void hud_on_log(const logline_t *l);                 // chronicle listener: toasts for important lines
void dashboard_panel(void);                          // dev dashboard (dashboard.c), shown when U.show_debug

// ---- input & commands (input.c) -------------------------------------------------------------------
void input_frame(float dt, int ui_blocked);          // picking, clicks (select / commands), hotkeys, camera control
fleet_t *ui_player_fleet(void);                      // selected fleet if it belongs to the player, else NULL
void ui_select_fleet(const fleet_t *f);              // NULL clears
void ui_select_port(int i);

#endif
