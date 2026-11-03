// mare::ui::Options implementation (see options.h): the settings object, its file and its
// panel of sliders.
#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "options.h"
#include "ui.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../gfx/render.h"
#include "../gfx/rhi.h"
#include "../gfx/camera.h"
#include "../assets/asset.h"
#include "../gfx/scene.h"
}

#include <initializer_list>

// The ROW macro in ui.h builds its width list with a C compound literal, which C++ cannot
// take; dashboard.cc solves it the same way.
static inline void ui_row_widths(int n, std::initializer_list<int> w) { ui_row(n, w.begin(), w.begin()[n]); }
#undef ROW
#define ROW(n, ...) ui_row_widths(n, { __VA_ARGS__ })

options_t OPT;
int options_show;

namespace mare::ui {

#define OPT_FILE "mare_options.cfg"

class Options {
public:
    void load();
    void save();
    void apply();
    void panel();

private:
    int   applied_ = 0;
    float last_aniso_ = -1.f;
    float last_tilt_  = -1e9f;
    // what the file was written with, so save() only runs when something actually moved
    options_t saved_ = {};

    static void defaults(options_t *o) {
        memset(o, 0, sizeof *o);
        snprintf(o->backend, sizeof o->backend, "gl");
        o->shadows = 1; o->shadow_distance = 1.f; o->anisotropy = 0.f;   // 0 = driver maximum
        o->tessellation = 1; o->lod_bias = 1.f; o->wave_height = 1.f;
        o->ssao = 1; o->bloom = 0.55f; o->exposure = 1.f;
        o->cam_tilt = 0.f;
        o->fps_cap = 60;        // 0.22.21 (issue17): capped out of the box; 0 in the panel uncaps
    }
    int dirty() const { return memcmp(&saved_, &OPT, sizeof OPT) != 0; }
};

Options &options() { static Options o; return o; }

// ---------------------------------------------------------------------------
// the file: one `key value` per line, unknown keys ignored so an older build can read a
// newer file and vice versa

void Options::load() {
    defaults(&OPT);
    FILE *f = fopen(OPT_FILE, "r");
    if (const char *e = getenv("MARE_OPTIONS")) options_show = atoi(e) != 0;   // open the panel for a capture
    if (!f) { saved_ = OPT; LOG_INFO("ui", "no %s: graphics settings at their defaults", OPT_FILE); return; }
    char key[64]; char val[64];
    while (fscanf(f, "%63s %63s", key, val) == 2) {
        if      (!strcmp(key, "backend"))         snprintf(OPT.backend, sizeof OPT.backend, "%s", val);
        else if (!strcmp(key, "shadows"))         OPT.shadows = atoi(val);
        else if (!strcmp(key, "shadow_distance")) OPT.shadow_distance = (float)atof(val);
        else if (!strcmp(key, "anisotropy"))      OPT.anisotropy = (float)atof(val);
        else if (!strcmp(key, "fps_cap"))         OPT.fps_cap = atoi(val);
        else if (!strcmp(key, "tessellation"))    OPT.tessellation = atoi(val);
        else if (!strcmp(key, "lod_bias"))        OPT.lod_bias = (float)atof(val);
        else if (!strcmp(key, "wave_height"))     OPT.wave_height = (float)atof(val);
        else if (!strcmp(key, "cam_tilt"))        OPT.cam_tilt = (float)atof(val);
        else if (!strcmp(key, "ssao"))            OPT.ssao = atoi(val);
        else if (!strcmp(key, "bloom"))           OPT.bloom = (float)atof(val);
        else if (!strcmp(key, "exposure"))        OPT.exposure = (float)atof(val);
    }
    fclose(f);
    OPT.loaded = 1; saved_ = OPT;
    if (const char *e = getenv("MARE_OPTIONS")) options_show = atoi(e) != 0;
    LOG_INFO("ui", "graphics settings read from %s: backend %s, shadows %d at %.2fx, aniso %.0f, tess %d, lod %.2f, waves %.2f",
             OPT_FILE, OPT.backend, OPT.shadows, OPT.shadow_distance, OPT.anisotropy, OPT.tessellation, OPT.lod_bias, OPT.wave_height);
}

void Options::save() {
    FILE *f = fopen(OPT_FILE, "w");
    if (!f) { LOG_WARN("ui", "cannot write %s", OPT_FILE); return; }
    fprintf(f, "# MARE LIBERUM graphics settings (the Options panel writes this)\n");
    fprintf(f, "backend %s\n", OPT.backend);
    fprintf(f, "shadows %d\n", OPT.shadows);
    fprintf(f, "shadow_distance %.3f\n", OPT.shadow_distance);
    fprintf(f, "anisotropy %.1f\n", OPT.anisotropy);
    fprintf(f, "fps_cap %d\n", OPT.fps_cap);
    fprintf(f, "tessellation %d\n", OPT.tessellation);
    fprintf(f, "lod_bias %.3f\n", OPT.lod_bias);
    fprintf(f, "wave_height %.3f\n", OPT.wave_height);
    fprintf(f, "cam_tilt %.1f\n", OPT.cam_tilt);
    fprintf(f, "ssao %d\n", OPT.ssao);
    fprintf(f, "bloom %.3f\n", OPT.bloom);
    fprintf(f, "exposure %.3f\n", OPT.exposure);
    fclose(f);
    saved_ = OPT;
}

// ---------------------------------------------------------------------------
// pushing the settings into the systems that own them

void Options::apply() {
    // Environment variables still win: the measurement scripts set them and must keep
    // behaving the same, so a pinned value is read once and the slider follows it.
    if (!applied_) {
        applied_ = 1;
        if (getenv("MARE_LOD_BIAS")) OPT.lod_bias = asset_lod_bias;
        if (getenv("MARE_WAVES"))    OPT.wave_height = sea_wave_amp;
        if (getenv("MARE_ANISO"))    OPT.anisotropy = rhi_anisotropy();
        if (getenv("MARE_CAM_TILT")) OPT.cam_tilt = cam_pitch_user;      // pinned by mare.cc for horizon screenshots
        if (OPT.anisotropy <= 0.f)   OPT.anisotropy = rhi_anisotropy_max();   // default: the driver maximum
        saved_ = OPT;
    }
    shadow_enabled        = OPT.shadows;
    shadow_distance_scale = OPT.shadow_distance;
    terrain_tess_enabled  = OPT.tessellation;
    asset_lod_bias        = OPT.lod_bias;
    sea_wave_amp          = OPT.wave_height;
    // the tilt is live in both directions: the slider sets it, the middle-drag / PageUp keys
    // move it and the slider follows
    if (OPT.cam_tilt != last_tilt_) { cam_pitch_user = OPT.cam_tilt; last_tilt_ = OPT.cam_tilt; }
    else if (cam_pitch_user != OPT.cam_tilt) { OPT.cam_tilt = cam_pitch_user; last_tilt_ = cam_pitch_user; }
    hdr_ssao_enabled      = OPT.ssao;
    hdr_bloom             = OPT.bloom;
    hdr_bloom_enabled     = OPT.bloom > 0.01f;
    hdr_exposure          = OPT.exposure;
    // anisotropy re-touches every mipmapped texture, so only when it really moved
    if (OPT.anisotropy != last_aniso_) { rhi_set_anisotropy(OPT.anisotropy); last_aniso_ = OPT.anisotropy; }
}


// ---------------------------------------------------------------------------
// the panel

void Options::panel() {
    if (!options_show) return;
    mu_Container *c = mu_get_container(ui_ctx, "Options");
    if (c->rect.w == 0) c->rect = mu_rect(screen_w / 2 - 210, 70, 420, 470);
    if (!mu_begin_window_ex(ui_ctx, "Options", c->rect, 0)) return;
    { static int said; if (!said) { said = 1;
        LOG_INFO("ui", "options panel open: backend %s (running %s), shadows %d at %.2fx, aniso %.0f of %.0f, tess %d, lod %.2f, waves %.2f, ssao %d, bloom %.2f, exposure %.2f",
                 OPT.backend, rhi_backend(), OPT.shadows, OPT.shadow_distance, OPT.anisotropy, rhi_anisotropy_max(),
                 OPT.tessellation, OPT.lod_bias, OPT.wave_height, OPT.ssao, OPT.bloom, OPT.exposure); } }

    mu_label(ui_ctx, "Renderer (takes effect next launch)");
    { ROW(3, 120, 120, -1, 24);
      static const char *names[3] = { "gl", "vulkan", "dx12" };
      static const char *shown[3] = { "OpenGL", "Vulkan", "DirectX 12" };
      for (int i = 0; i < 3; ++i) {
          mu_push_id(ui_ctx, &names[i], sizeof names[i]);
          char lbl[32]; snprintf(lbl, sizeof lbl, "%s%s", !strcmp(OPT.backend, names[i]) ? "* " : "", shown[i]);
          if (ui_btn(lbl)) { snprintf(OPT.backend, sizeof OPT.backend, "%s", names[i]); }
          mu_pop_id(ui_ctx);
      } }
    { ROW(1, -1, 20); char s[128];
      snprintf(s, sizeof s, "running on %s - %s", rhi_backend(),
               strcmp(OPT.backend, rhi_backend()) ? "restart to switch" : "active");
      mu_label(ui_ctx, s); }

    mu_label(ui_ctx, "Graphics detail");
    { ROW(4, 95, 95, 95, -1, 24);
      int cur = options_quality();
      for (int q = 0; q <= QUALITY_ULTRA; ++q) {
          mu_push_id(ui_ctx, &quality_names[q], sizeof quality_names[q]);
          char lbl[32]; snprintf(lbl, sizeof lbl, "%s%s", cur == q ? "* " : "", quality_names[q]);
          if (ui_btn(lbl)) options_set_quality(q);
          mu_pop_id(ui_ctx);
      } }
    if (options_quality() == QUALITY_CUSTOM) { ROW(1, -1, 20); mu_label(ui_ctx, "custom - the sliders below have been moved off a preset"); }

    mu_label(ui_ctx, "Frame limit");
    { ROW(4, 95, 95, 95, -1, 24);
      static const int caps[4] = { 0, 30, 60, 120 };
      for (int i = 0; i < 4; ++i) { mu_push_id(ui_ctx, &caps[i], sizeof caps[i]);
          char lbl[24]; snprintf(lbl, sizeof lbl, "%s%s", OPT.fps_cap == caps[i] ? "* " : "", caps[i] ? va("%d fps", caps[i]) : "off");
          if (ui_btn(lbl)) OPT.fps_cap = caps[i];
          mu_pop_id(ui_ctx); } }

    mu_label(ui_ctx, "Shadows");
    { ROW(2, 120, -1, 24);
      mu_checkbox(ui_ctx, "enabled", &OPT.shadows);
      mu_slider_ex(ui_ctx, &OPT.shadow_distance, 0.4f, 2.5f, 0.05f, "distance %.2fx", MU_OPT_ALIGNCENTER); }

    mu_label(ui_ctx, "Texture filtering");
    { ROW(1, -1, 24);
      float mx = rhi_anisotropy_max();
      if (mx < 2.f) mu_label(ui_ctx, "anisotropic filtering unsupported by this driver");
      else mu_slider_ex(ui_ctx, &OPT.anisotropy, 1.f, mx, 1.f, "anisotropic %.0fx", MU_OPT_ALIGNCENTER); }

    mu_label(ui_ctx, "Detail");
    { ROW(2, 120, -1, 24);
      mu_checkbox(ui_ctx, "tessellation", &OPT.tessellation);
      mu_slider_ex(ui_ctx, &OPT.lod_bias, 0.4f, 3.0f, 0.05f, "model detail %.2fx", MU_OPT_ALIGNCENTER); }

    mu_label(ui_ctx, "Water and light");
    { ROW(1, -1, 24); mu_slider_ex(ui_ctx, &OPT.wave_height, 0.f, 2.f, 0.05f, "wave height %.2f", MU_OPT_ALIGNCENTER); }
    { ROW(1, -1, 24); mu_slider_ex(ui_ctx, &OPT.cam_tilt, -60.f, 30.f, 1.f, "camera tilt %+.0f deg (PageUp/PageDown, Home)", MU_OPT_ALIGNCENTER); }
    { ROW(1, -1, 24); mu_slider_ex(ui_ctx, &OPT.exposure, 0.4f, 2.5f, 0.05f, "exposure %.2f", MU_OPT_ALIGNCENTER); }
    { ROW(2, 120, -1, 24);
      mu_checkbox(ui_ctx, "ambient occlusion", &OPT.ssao);
      mu_slider_ex(ui_ctx, &OPT.bloom, 0.f, 1.5f, 0.05f, "bloom %.2f", MU_OPT_ALIGNCENTER); }

    { ROW(1, -1, 20); char s[160];
      snprintf(s, sizeof s, "%.0f fps   %d shadow casters   %d asset instances", fps(), shadow_casters_drawn, scene_asset_instances);
      mu_label(ui_ctx, s); }

    { ROW(3, 120, 120, -1, 26);
      if (ui_btn("Defaults")) { char keep[16]; snprintf(keep, sizeof keep, "%s", OPT.backend);
                                defaults(&OPT); snprintf(OPT.backend, sizeof OPT.backend, "%s", keep);
                                OPT.anisotropy = rhi_anisotropy_max(); last_aniso_ = -1.f; }
      if (ui_btn(dirty() ? "Save *" : "Save")) save();
      if (ui_btn("Close")) options_show = 0; }
    mu_end_window(ui_ctx);
}

} // namespace mare::ui

// C ABI (options.h)
extern "C" {

// ---------------------------------------------------------------------------
// graphics detail presets (0.22.19, issue15)

const char *const quality_names[5] = { "Low", "Medium", "High", "Ultra", "Custom" };

// Four named sets of the same knobs the panel already has. Low turns off everything that
// costs a pass - shadows, tessellation, ambient occlusion, bloom - and halves the model
// detail distance; Ultra pushes every one of them past the default. The backend, the camera
// tilt and the exposure are not touched: the first is a different kind of choice and the
// other two are taste, not cost.
typedef struct quality_set_t { int shadows; float shadow_distance; float anisotropy; int tess;
                               float lod_bias; float wave_height; int ssao; float bloom; } quality_set_t;
static const quality_set_t QSET[4] = {
    /* Low    */ { 0, 0.6f,  1.f, 0, 0.55f, 0.6f, 0, 0.00f },
    /* Medium */ { 1, 0.9f,  4.f, 0, 0.85f, 1.0f, 0, 0.35f },
    /* High   */ { 1, 1.2f,  8.f, 1, 1.20f, 1.0f, 1, 0.60f },
    /* Ultra  */ { 1, 1.8f, 16.f, 1, 2.00f, 1.2f, 1, 0.85f },
};

void options_set_quality(int q) {
    if (q < 0 || q > QUALITY_ULTRA) return;
    const quality_set_t &k = QSET[q];
    OPT.shadows = k.shadows; OPT.shadow_distance = k.shadow_distance;
    OPT.anisotropy = minf(k.anisotropy, maxf(rhi_anisotropy_max(), 1.f));
    OPT.tessellation = k.tess; OPT.lod_bias = k.lod_bias; OPT.wave_height = k.wave_height;
    OPT.ssao = k.ssao; OPT.bloom = k.bloom;
    LOG_INFO("ui", "graphics detail: %s (shadows %d at %.2fx, aniso %.0f, tess %d, lod %.2f, waves %.2f, ssao %d, bloom %.2f)",
             quality_names[q], OPT.shadows, OPT.shadow_distance, OPT.anisotropy, OPT.tessellation, OPT.lod_bias, OPT.wave_height, OPT.ssao, OPT.bloom);
}

int options_quality(void) {
    for (int q = 0; q <= QUALITY_ULTRA; ++q) {
        const quality_set_t &k = QSET[q];
        float want_aniso = minf(k.anisotropy, maxf(rhi_anisotropy_max(), 1.f));
        if (OPT.shadows == k.shadows && fabsf(OPT.shadow_distance - k.shadow_distance) < 1e-3f
            && fabsf(OPT.anisotropy - want_aniso) < 0.5f && OPT.tessellation == k.tess
            && fabsf(OPT.lod_bias - k.lod_bias) < 1e-3f && fabsf(OPT.wave_height - k.wave_height) < 1e-3f
            && OPT.ssao == k.ssao && fabsf(OPT.bloom - k.bloom) < 1e-3f) return q;
    }
    return QUALITY_CUSTOM;
}


void options_load(void) { mare::ui::options().load(); }
void options_save(void) { mare::ui::options().save(); }
void options_apply(void) { mare::ui::options().apply(); }
void options_panel(void) { mare::ui::options().panel(); }
}
