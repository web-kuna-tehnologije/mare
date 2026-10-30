// mare::engine::render::HeightAtlas (see heightatlas.h): resident fine terrain tiles held on
// the GPU as one atlas, with a tile-grid lookup, so the terrain can sample the authored
// ground at its own resolution instead of through the clipmap ring's 512 texels.
#include "../platform/v3cpp.hh"
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "heightatlas.h"
#include "render.h"
#include "camera.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../world/terraintiles.h"
}

namespace mare::engine::render {

#define HA_SLOTS (HA_COLS * HA_ROWS)

typedef struct ha_slot_t { int tx, ty, used; unsigned last_want; } ha_slot_t;

class HeightAtlas {
public:
    void update();
    void bind(const rhi_pipeline_t *p, int unit_atlas, int unit_slots, float ring_texel);
    int  resident() { return nused; }
    int  available() { return inited && nused > 0; }

private:
    int        inited = 0, enabled = 1, nused = 0, probed = 0, worth = 0;
    float      probe_rms = 0.f;
    tt_setinfo_t set = {};
    rhi_texture_t tex_atlas = {}, tex_slots = {};
    ha_slot_t  slot[HA_SLOTS] = {};
    float     *slots_cpu = nullptr;      // tiles_x * tiles_y, slot + 1 (0 = not resident)
    int        slots_dirty = 0;
    float     *scratch = nullptr;        // one tile decoded to world units, for the upload
    float     *decode = nullptr;         // u16 -> world units, built once
    unsigned   want_no = 0;
    int        uploads = 0, evictions = 0;

    int  probe();
    int  init();
    int  slot_of(int tx, int ty) const;
    void release(int s);
    void upload(int s, int tx, int ty, const unsigned short *src);
};

HeightAtlas &height_atlas() { static HeightAtlas h; return h; }

// Is there anything to hold? Measured, not assumed.
//
// The atlas only pays if the authored tiles carry relief *below* the clipmap ring's texel -
// everything coarser the ring already draws. So before allocating anything, take the first
// resident fine tile and compare it against the reconstruction the ring would make of it (the
// same field sampled on the ring grid and bilinearly interpolated, which is exactly what the
// ring is) and look at what is left over.
//
// On the Mediterranean set shipped today that residual measures 0.0016 world units rms and
// 0.031 at worst, against local relief of 0.7 to 4.1 - two parts in a thousand, which is
// nothing anyone can see. The reason is simple once the numbers are lined up: at 16 world
// units per degree the near ring's texel is 0.157 units, about 1.1 km, and the fine set's
// samples are 1/180 of a degree, about 620 m. The ring is within a factor of 1.8 of the data
// - near enough to Nyquist that there is almost nothing left over. The near ground looking
// smooth at battle zoom is therefore a property of the *data* (that set is itself resampled
// from the same ~3 km GEBCO export; assets/data/how_to.txt says plainly the export is a
// global overview), not of the renderer, and no amount of GPU plumbing changes it.
//
// The machinery is kept because it is the right mechanism and it is now self-measuring: drop
// in a real high-resolution DEM and this probe will find the detail and switch the atlas on
// by itself. MARE_HEIGHT_ATLAS=1 forces it on regardless (to see the residual), =0 off.
#define HA_MIN_RMS 0.02f    // world units; ~9 m of sub-ring relief at the height scale in use

int HeightAtlas::probe() {
    if (probed) return worth;
    tt_setinfo_t s;
    if (!terraintiles_fine_info(&s) || !s.present || s.bytes_per_sample != 2) return 0;
    const unsigned short *src = nullptr; int ftx = 0, fty = 0;
    for (int ty = 0; ty < s.tiles_y && !src; ++ty) for (int tx = 0; tx < s.tiles_x && !src; ++tx)
        if ((src = (const unsigned short *)terraintiles_fine_tile(tx, ty))) { ftx = tx; fty = ty; }
    if (!src) return 0;                       // nothing resident yet; ask again next frame
    float ring = streamview_detail_texel(0);
    float wps = (s.lon1 - s.lon0) / (float)(s.width - 1) * WORLD_K;     // world units per sample
    int step = (int)(ring / maxf(wps, 1e-6f) + 0.5f); if (step < 2) step = 2; if (step > 64) step = 64;
    double s2 = 0; int n = 0; float mx = 0;
    for (int y = 0; y + step < TT_TILE; y += 3) for (int x = 0; x + step < TT_TILE; x += 3) {
        int x0 = (x / step) * step, y0 = (y / step) * step;
        if (x0 + step >= TT_TILE || y0 + step >= TT_TILE) continue;
        float fx = (x - x0) / (float)step, fy = (y - y0) / (float)step;
        float h00 = terraintiles_decode_u16(src[y0 * TT_TILE + x0]),        h10 = terraintiles_decode_u16(src[y0 * TT_TILE + x0 + step]);
        float h01 = terraintiles_decode_u16(src[(y0 + step) * TT_TILE + x0]), h11 = terraintiles_decode_u16(src[(y0 + step) * TT_TILE + x0 + step]);
        float low = lerpf(lerpf(h00, h10, fx), lerpf(h01, h11, fx), fy);
        float r = terraintiles_decode_u16(src[y * TT_TILE + x]) - low;
        s2 += (double)r * r; n++; mx = maxf(mx, fabsf(r));
    }
    probed = 1;
    probe_rms = n ? (float)sqrt(s2 / n) : 0.f;
    const char *force = getenv("MARE_HEIGHT_ATLAS");
    worth = probe_rms >= HA_MIN_RMS || (force && atoi(force) == 1);
    LOG_INFO("gpu", "height atlas probe on fine tile %d,%d: %.4f rms / %.3f peak world units below the ring texel (%.3f u, 1 sample in %d) - %s",
             ftx, fty, probe_rms, mx, ring, step,
             worth ? "worth holding on the GPU" : "nothing the clipmap ring is not already drawing, atlas stays off");
    return worth;
}

// The atlas exists only once there is a fine set worth holding. Everything is sized from it,
// so a tree with no regional cut - or one whose cut has no sub-ring detail - allocates nothing.
int HeightAtlas::init() {
    if (inited) return 1;
    { const char *e = getenv("MARE_HEIGHT_ATLAS"); if (e && !atoi(e)) { enabled = 0; LOG_INFO("gpu", "height atlas off (MARE_HEIGHT_ATLAS=0)"); } }
    if (!enabled) return 0;
    if (!probe()) return 0;
    if (!terraintiles_fine_info(&set) || !set.present) return 0;
    if (set.bytes_per_sample != 2) { LOG_WARN("gpu", "height atlas: the fine set is %d bytes per sample, expected 2", set.bytes_per_sample); enabled = 0; return 0; }
    tex_atlas = rhi_texture_make(HA_COLS * TT_TILE, HA_ROWS * TT_TILE, RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
    slots_cpu = (float *)calloc((size_t)set.tiles_x * set.tiles_y, sizeof(float));
    tex_slots = rhi_texture_make(set.tiles_x, set.tiles_y, RHI_FMT_R32F, RHI_FILTER_NEAREST, RHI_WRAP_CLAMP, slots_cpu);
    scratch = (float *)malloc((size_t)TT_TILE * TT_TILE * sizeof(float));
    // every raw sample value decoded once: the upload then costs a table read per sample
    // rather than a branch and a square root
    decode = (float *)malloc(65536 * sizeof(float));
    for (int i = 0; i < 65536; ++i) decode[i] = terraintiles_decode_u16((unsigned short)i);
    for (int i = 0; i < HA_SLOTS; ++i) slot[i].used = 0;
    inited = 1;
    LOG_INFO("gpu", "height atlas: %dx%d slots of %d (%.0f MB), fine set %dx%d tiles at lon %.0f..%.0f lat %.0f..%.0f",
             HA_COLS, HA_ROWS, TT_TILE, HA_COLS * HA_ROWS * (float)TT_TILE * TT_TILE * 4 / (1024 * 1024),
             set.tiles_x, set.tiles_y, set.lon0, set.lon1, set.lat0, set.lat1);
    return 1;
}

int HeightAtlas::slot_of(int tx, int ty) const {
    for (int i = 0; i < HA_SLOTS; ++i) if (slot[i].used && slot[i].tx == tx && slot[i].ty == ty) return i;
    return -1;
}

void HeightAtlas::release(int s) {
    if (!slot[s].used) return;
    slots_cpu[slot[s].ty * set.tiles_x + slot[s].tx] = 0.f;
    slot[s].used = 0; nused--; slots_dirty = 1; evictions++;
}

void HeightAtlas::upload(int s, int tx, int ty, const unsigned short *src) {
    for (int i = 0; i < TT_TILE * TT_TILE; ++i) scratch[i] = decode[src[i]];
    rhi_texture_update_rect(&tex_atlas, (s % HA_COLS) * TT_TILE, (s / HA_COLS) * TT_TILE, TT_TILE, TT_TILE, scratch);
    slot[s].used = 1; slot[s].tx = tx; slot[s].ty = ty; slot[s].last_want = want_no;
    slots_cpu[ty * set.tiles_x + tx] = (float)(s + 1);
    slots_dirty = 1; nused++; uploads++;
    LOG_DEBUG("gpu", "height atlas: fine tile %d,%d -> slot %d (%d of %d used)", tx, ty, s, nused, HA_SLOTS);
}

void HeightAtlas::update() {
    if (!init()) return;
    want_no++;
    // Which tiles matter: the ones resident on the CPU (terraintiles has already paged those
    // around the camera) ordered by how close they are to the look-at point, so when more are
    // resident than the atlas has slots the near ones win.
    float deg_x = (set.lon1 - set.lon0) / set.tiles_x, deg_y = (set.lat1 - set.lat0) / set.tiles_y;
    float clon = x2lon(cam.tx), clat = z2lat(cam.tz);
    struct { int tx, ty; float d; } want[64]; int nwant = 0;
    for (int ty = 0; ty < set.tiles_y; ++ty) for (int tx = 0; tx < set.tiles_x; ++tx) {
        if (!terraintiles_fine_tile(tx, ty)) continue;
        float lon = set.lon0 + (tx + 0.5f) * deg_x, lat = set.lat1 - (ty + 0.5f) * deg_y;
        float d = dist2d(lon, lat, clon, clat);
        if (nwant < 64) { want[nwant].tx = tx; want[nwant].ty = ty; want[nwant].d = d; nwant++; }
        else { int worst = 0; for (int i = 1; i < nwant; ++i) if (want[i].d > want[worst].d) worst = i;
               if (d < want[worst].d) { want[worst].tx = tx; want[worst].ty = ty; want[worst].d = d; } }
    }
    for (int i = 1; i < nwant; ++i) {   // insertion sort: nearest first
        auto v = want[i]; int j = i - 1;
        while (j >= 0 && want[j].d > v.d) { want[j + 1] = want[j]; j--; }
        want[j + 1] = v;
    }
    int keep = nwant < HA_SLOTS ? nwant : HA_SLOTS;
    for (int i = 0; i < keep; ++i) { int s = slot_of(want[i].tx, want[i].ty); if (s >= 0) slot[s].last_want = want_no; }
    // a slot whose tile has left the CPU set, or which no longer makes the near list, is free
    for (int i = 0; i < HA_SLOTS; ++i)
        if (slot[i].used && (slot[i].last_want != want_no || !terraintiles_fine_tile(slot[i].tx, slot[i].ty))) release(i);
    int budget = HA_UPLOADS_PER_FRAME;
    for (int i = 0; i < keep && budget > 0; ++i) {
        if (slot_of(want[i].tx, want[i].ty) >= 0) continue;
        int s = -1; for (int k = 0; k < HA_SLOTS; ++k) if (!slot[k].used) { s = k; break; }
        if (s < 0) break;
        const unsigned short *src = (const unsigned short *)terraintiles_fine_tile(want[i].tx, want[i].ty);
        if (!src) continue;
        upload(s, want[i].tx, want[i].ty, src);
        budget--;
    }
    if (slots_dirty) { rhi_texture_update(&tex_slots, slots_cpu); slots_dirty = 0; }
}

void HeightAtlas::bind(const rhi_pipeline_t *p, int unit_atlas, int unit_slots, float ring_texel) {
    int on = inited && nused > 0 && ring_texel > 0.f;
    (void)on;
    if (!inited) {   // no atlas at all: point the samplers at something valid and switch it off
        rhi_set_f2(rhi_uniform(p, "u_ha_on"), 0.f, 0.f);
        const rhi_texture_t *hm = terrain_heightmap_texture();
        rhi_set_i1(rhi_uniform(p, "u_hatlas"), unit_atlas); rhi_texture_bind(unit_atlas, hm);
        rhi_set_i1(rhi_uniform(p, "u_hslots"), unit_slots); rhi_texture_bind(unit_slots, hm);
        return;
    }
    rhi_set_i1(rhi_uniform(p, "u_hatlas"), unit_atlas); rhi_texture_bind(unit_atlas, &tex_atlas);
    rhi_set_i1(rhi_uniform(p, "u_hslots"), unit_slots); rhi_texture_bind(unit_slots, &tex_slots);
    rhi_set_f4(rhi_uniform(p, "u_ha_geo"), set.lon0, set.lat1, 1.f / (set.lon1 - set.lon0), 1.f / (set.lat1 - set.lat0));
    rhi_set_f4(rhi_uniform(p, "u_ha_grid"), (float)set.tiles_x, (float)set.tiles_y, (float)set.width, (float)set.height);
    rhi_set_f4(rhi_uniform(p, "u_ha_slot"), (float)HA_COLS, (float)HA_ROWS, (float)TT_TILE, 1.f / (HA_COLS * TT_TILE));
    rhi_set_f2(rhi_uniform(p, "u_ha_on"), on ? 1.f : 0.f, ring_texel);
    rhi_set_f1(rhi_uniform(p, "u_ha_k"), WORLD_K);
}

} // namespace mare::engine::render

// C ABI (heightatlas.h)
extern "C" {
void heightatlas_update(void) { mare::engine::render::height_atlas().update(); }
void heightatlas_bind(const rhi_pipeline_t *p, int unit_atlas, int unit_slots, float ring_texel) {
    mare::engine::render::height_atlas().bind(p, unit_atlas, unit_slots, ring_texel);
}
int  heightatlas_resident(void) { return mare::engine::render::height_atlas().resident(); }
int  heightatlas_available(void) { return mare::engine::render::height_atlas().available(); }
}
