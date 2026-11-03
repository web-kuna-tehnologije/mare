// mare::ui::Options - the player-facing graphics settings, their panel and their file.
//
// Everything here was previously an environment variable read once at start-up (MARE_SHADOWS,
// MARE_ANISO, MARE_LOD_BIAS, MARE_TERRAIN_TESS, MARE_RHI...), which is fine for A/B runs and
// useless to a player. The same knobs are now a settings object with a panel of sliders, and
// they persist to `mare_options.cfg` beside the executable, so a choice survives the session.
//
// Two kinds of setting live here. Most apply the moment the slider moves - shadow distance,
// filtering, LOD bias, wave height, exposure, the post passes. The render backend cannot:
// the device is chosen before the window's context exists, so the panel records the choice,
// writes the file and says it takes effect next launch (mare.cc reads it during start-up,
// before rhi_init). The environment variables still win where they are set, so every existing
// measurement script keeps working unchanged.

#ifndef MARE_OPTIONS_H
#define MARE_OPTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

// Graphics detail, as one choice (0.22.19, issue15 "add in menu starting options with
// posibility of changing rendering software and level of graphics details low -> high").
// Every knob below is still individually adjustable; a preset is simply a named set of them,
// so picking "Low" is the same as moving nine sliders and the panel shows where they landed.
// QUALITY_CUSTOM is what a preset becomes the moment the player moves one of those sliders.
enum { QUALITY_LOW = 0, QUALITY_MEDIUM = 1, QUALITY_HIGH = 2, QUALITY_ULTRA = 3, QUALITY_CUSTOM = 4 };
extern const char *const quality_names[5];
// write the preset's values into OPT (and apply() pushes them on as usual)
void options_set_quality(int q);
// which preset OPT currently matches, or QUALITY_CUSTOM
int  options_quality(void);

typedef struct options_t {
    // renderer (applied at the next launch)
    char  backend[16];        // "gl" | "vulkan" | "dx12"
    // shadows
    int   shadows;            // cascaded sun shadows on
    float shadow_distance;    // multiplier on the fitted cascade range, 0.4 .. 2.5
    // texture filtering
    float anisotropy;         // 1 .. driver maximum
    // terrain & detail
    int   tessellation;       // GL 4.0 tessellated terrain patches
    float lod_bias;           // authored-asset LOD switch distance, 0.4 .. 3
    // water
    float wave_height;        // 0 .. 2
    // camera
    float cam_tilt;           // degrees added to the zoom's pitch, -60 .. +30 (0 = the classic RTS pitch)
    // post
    int   ssao;
    float bloom;              // 0 .. 1.5
    float exposure;           // 0.4 .. 2.5
    // frame limit (0.22.21, issue17 "limit fps to 60 (add limiter to options)"): frames per
    // second the main loop will not exceed, 0 = uncapped. Saves power and keeps a laptop
    // quiet; the sim is frame-rate independent so nothing else changes.
    int   fps_cap;
    int   loaded;             // a settings file was read at start-up
} options_t;

extern options_t OPT;

// Read `mare_options.cfg` if it is there. Call before rhi_init: the backend field is what
// mare.cc passes to rhi_select, unless MARE_RHI overrides it.
void options_load(void);
void options_save(void);
// Push every runtime setting into the systems that own it (shadows, filtering, LOD, waves,
// post). Safe to call every frame; it only writes what changed.
void options_apply(void);
void options_panel(void);     // the Options window (hud.c draws it with the other panels)
extern int options_show;      // panel toggle, in the top bar next to Help

#ifdef __cplusplus
}
#endif

#endif
