// mare::engine::render::ChunkAtlas (see terrain_atlas.h): the chunks' material maps on the GPU.
#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "render.h"
#include "camera.h"
#include "terrain_atlas.h"
#include "../core/diag.h"
#include "../core/util.h"
#include "../core/types.h"
#include "../world/terrain_chunks.h"
}

namespace mare::engine::render {

class ChunkAtlas {
public:
    void update();
    void bind(const rhi_pipeline_t *p, int unit_cls, int unit_splat, int unit_normal, int unit_slots);
    int  resident() { int n = 0; for (int i = 0; i < cols * rows; ++i) n += slot[i].used && slot[i].uploaded; return n; }
    int  available() { return on && resident() > 0; }

private:
    typedef struct ca_slot_t { int used, uploaded, cx, cy; unsigned last_want; } ca_slot_t;
    ca_slot_t slot[CA_SLOTS] = {};
    rhi_texture_t tex_cls = {}, tex_splat = {}, tex_normal = {}, tex_slots = {};
    unsigned char *table = nullptr; int table_w = 0, table_h = 0, table_dirty = 0;
    int on = 0, inited = 0, ds = -1;
    int cols = CA_COLS, rows = CA_ROWS;   // the grid this dataset's slot size affords
    int texels = GC_SAMPLES;              // texels per slot edge
    int stride = 1;                       // dataset samples per slot texel (an exact divisor)
    unsigned char *scratch = nullptr;     // one decimated slot, when stride > 1
    gc_dataset_t info = {};
    unsigned seen_gen = 0;
    static unsigned char *blank(size_t n) { unsigned char *b = (unsigned char *)calloc(n, 1); return b; }
    void init();
    const unsigned char *decimate(const unsigned char *src);
    int  slot_of(int cx, int cy) { for (int i = 0; i < cols * rows; ++i) if (slot[i].used && slot[i].cx == cx && slot[i].cy == cy) return i; return -1; }
    void release(int i) { if (slot[i].used) { table[(slot[i].cy * table_w + slot[i].cx) * 4] = 0; table_dirty = 1; } slot[i] = ca_slot_t{}; }
    void upload(int i);
};

ChunkAtlas &chunk_atlas() { static ChunkAtlas a; return a; }

void ChunkAtlas::init() {
    inited = 1;
    { const char *e = getenv("MARE_CHUNK_ATLAS"); if (e && !atoi(e)) { LOG_INFO("terrain", "chunk atlas off (MARE_CHUNK_ATLAS=0)"); return; } }
    // The world-covering dataset, finest first: the 4x build (2049) in front of the 3 km
    // overview (513) where both are present. Datasets whose grid is coarser than the
    // overview's - the package's LOD levels, the 257-sample regional sets - stay off the GPU,
    // as do the regional builds, which cover a corner of the map and would waste the table.
    int n = geochunks_dataset_count(); if (n <= 0) return;
    double best = -1;
    for (int i = 0; i < n; ++i) { gc_dataset_t d; if (!geochunks_dataset(i, &d)) continue;
        if (d.samples < GC_SAMPLES || (d.samples - 1) % GC_CELLS) continue;   // 513, 1025, 2049: a slot stride divides exactly
        double a = (double)(d.east - d.west) * (d.north - d.south);
        if (a > best + 1e-6 || (a > best - 1e-6 && d.samples > info.samples)) { best = a; ds = i; info = d; } }
    if (ds < 0) return;
    // Slot texels, then the grid that fits in the texel budget: finer slots buy detail with
    // coverage, and a chunk is 13 degrees, so even 2 x 2 holds the four under a view corner.
    texels = info.samples;
    { const char *e = getenv("MARE_CHUNK_ATLAS_SAMPLES");
      if (e && atoi(e) > 0) { int w = atoi(e); if (w >= 2 && (info.samples - 1) % (w - 1) == 0 && w <= info.samples) texels = w; } }
    if (texels > 1025 && !getenv("MARE_CHUNK_ATLAS_SAMPLES")) texels = 1025;   // 2049-texel slots cost 200 MB: opt in
    cols = CA_COLS; rows = CA_ROWS;
    // ... down to 2 x 2 and no further: four slots are the minimum that can hold the four
    // chunks meeting under a view corner without evicting one of them every frame
    while (cols > 2 && cols * texels > CA_SLOT_BUDGET_TEXELS) { cols--; rows--; }
    stride = (info.samples - 1) / (texels - 1);
    if (stride > 1) { scratch = (unsigned char *)malloc((size_t)texels * texels * 4); if (!scratch) { LOG_WARN("terrain", "chunk atlas: no scratch for decimation"); return; } }
    int edge = cols * texels;
    tex_cls    = rhi_texture_make(edge, rows * texels, RHI_FMT_RGBA8, RHI_FILTER_NEAREST, RHI_WRAP_CLAMP, 0);
    tex_splat  = rhi_texture_make(edge, rows * texels, RHI_FMT_RGBA8, RHI_FILTER_LINEAR,  RHI_WRAP_CLAMP, 0);
    tex_normal = rhi_texture_make(edge, rows * texels, RHI_FMT_RGBA8, RHI_FILTER_LINEAR,  RHI_WRAP_CLAMP, 0);
    table_w = info.chunks_x; table_h = info.chunks_y;
    table = blank((size_t)table_w * table_h * 4);
    tex_slots = rhi_texture_make(table_w, table_h, RHI_FMT_RGBA8, RHI_FILTER_NEAREST, RHI_WRAP_CLAMP, table);
    on = 1;
    LOG_INFO("terrain", "chunk atlas: %dx%d slots of %d texels from %d samples (stride %d; class, splat, normal: %d MB on the GPU) over the %dx%d-chunk world",
             cols, rows, texels, info.samples, stride, (int)(3ull * edge * rows * texels * 4 / (1024 * 1024)), table_w, table_h);
}

// One slot's worth of a finer chunk, point-sampled every `stride` samples. Point sampling,
// not a box filter, is deliberate: the grids are 2^k + 1 and neighbouring chunks share their
// edge row and column, so taking sample 0, s, 2s ... C lands on the shared edge exactly and
// the joins stay seamless. Averaging would mix a chunk's own interior into its edge and open
// a visible seam at every chunk border.
const unsigned char *ChunkAtlas::decimate(const unsigned char *src) {
    if (stride <= 1 || !src) return src;
    const int N = info.samples;
    for (int y = 0; y < texels; ++y) {
        const unsigned char *r = src + (size_t)(y * stride) * N * 4;
        unsigned char *o = scratch + (size_t)y * texels * 4;
        for (int x = 0; x < texels; ++x) memcpy(o + x * 4, r + (size_t)(x * stride) * 4, 4);
    }
    return scratch;
}

void ChunkAtlas::upload(int i) {
    const unsigned char *cls = 0, *splat = 0, *normal = 0;
    if (!geochunks_chunk_maps(ds, slot[i].cx, slot[i].cy, &cls, &splat, &normal)) return;
    int x = (i % cols) * texels, y = (i / cols) * texels;
    static unsigned char *zero; static int zero_n;
    if (!zero || zero_n < texels) { free(zero); zero = blank((size_t)texels * texels * 4); zero_n = texels; }
    // a chunk without one of the maps uploads zeros for it: class 0 (missing) and zero
    // weights are exactly what the shaders treat as "no data"
    rhi_texture_update_rect(&tex_cls,    x, y, texels, texels, cls    ? decimate(cls)    : zero);
    rhi_texture_update_rect(&tex_splat,  x, y, texels, texels, splat  ? decimate(splat)  : zero);
    rhi_texture_update_rect(&tex_normal, x, y, texels, texels, normal ? decimate(normal) : zero);
    slot[i].uploaded = 1;
    table[(slot[i].cy * table_w + slot[i].cx) * 4] = (unsigned char)(i + 1);
    table_dirty = 1;
}

void ChunkAtlas::update() {
    if (!inited) init();
    if (!on) return;
    unsigned now = geochunks_update_number();
    // drop slots whose chunk is gone or has not been wanted for a while
    for (int i = 0; i < cols * rows; ++i) {
        if (!slot[i].used) continue;
        unsigned w = geochunks_chunk_wanted(ds, slot[i].cx, slot[i].cy);
        slot[i].last_want = w;
        if (!geochunks_chunk_maps(ds, slot[i].cx, slot[i].cy, 0, 0, 0) || now - w > 600) release(i);
    }
    // give slots to the wanted resident chunks, nearest to the look-at point first
    float clon = x2lon(cam.tx), clat = z2lat(cam.tz);
    int uploads = 0;
    for (int pass = 0; pass < 2 && uploads < CA_UPLOADS_PER_FRAME; ++pass) {
        // pass 0: the chunk under the camera; pass 1: the rest of the wanted set
        for (int cy = 0; cy < info.chunks_y && uploads < CA_UPLOADS_PER_FRAME; ++cy)
        for (int cx = 0; cx < info.chunks_x && uploads < CA_UPLOADS_PER_FRAME; ++cx) {
            if (geochunks_chunk_wanted(ds, cx, cy) != now) continue;
            if (!geochunks_chunk_maps(ds, cx, cy, 0, 0, 0)) continue;
            float lon0 = info.west + cx * (info.east - info.west) / info.chunks_x, lon1 = lon0 + (info.east - info.west) / info.chunks_x;
            float lat1 = info.north - cy * (info.north - info.south) / info.chunks_y, lat0 = lat1 - (info.north - info.south) / info.chunks_y;
            int under = clon >= lon0 && clon <= lon1 && clat >= lat0 && clat <= lat1;
            if (pass == 0 && !under) continue;
            int s = slot_of(cx, cy);
            if (s >= 0) { if (!slot[s].uploaded) { upload(s); uploads++; } continue; }
            int free_i = -1; unsigned oldest = ~0u;
            for (int i = 0; i < cols * rows; ++i) {
                if (!slot[i].used) { free_i = i; break; }
                if (slot[i].last_want != now && slot[i].last_want < oldest) { oldest = slot[i].last_want; free_i = i; }
            }
            if (free_i < 0) continue;          // every slot holds a wanted chunk: nothing to steal
            release(free_i);
            slot[free_i].used = 1; slot[free_i].cx = cx; slot[free_i].cy = cy; slot[free_i].last_want = now;
            upload(free_i); uploads++;
        }
    }
    if (table_dirty) { rhi_texture_update(&tex_slots, table); table_dirty = 0; }
}

void ChunkAtlas::bind(const rhi_pipeline_t *p, int unit_cls, int unit_splat, int unit_normal, int unit_slots) {
    int ok = on && resident() > 0;
    const rhi_texture_t *fallback = terrain_heightmap_texture();
    rhi_set_i1(rhi_uniform(p, "u_ccls"),   unit_cls);    rhi_texture_bind(unit_cls,    ok ? &tex_cls    : fallback);
    rhi_set_i1(rhi_uniform(p, "u_csplat"), unit_splat);  rhi_texture_bind(unit_splat,  ok ? &tex_splat  : fallback);
    rhi_set_i1(rhi_uniform(p, "u_cnrm"),   unit_normal); rhi_texture_bind(unit_normal, ok ? &tex_normal : fallback);
    rhi_set_i1(rhi_uniform(p, "u_cslots"), unit_slots);  rhi_texture_bind(unit_slots,  ok ? &tex_slots  : fallback);
    if (!ok) { rhi_set_f2(rhi_uniform(p, "u_con"), 0.f, 1.f); return; }
    rhi_set_f4(rhi_uniform(p, "u_cgeo"), info.west, info.north, 1.f / (info.east - info.west), 1.f / (info.north - info.south));
    rhi_set_f4(rhi_uniform(p, "u_cgrid"), (float)info.chunks_x, (float)info.chunks_y, (float)(texels - 1), info.wrap ? 1.f : 0.f);
    rhi_set_f4(rhi_uniform(p, "u_cslot"), (float)cols, (float)rows, (float)texels, 1.f / (float)(cols * texels));
    rhi_set_f2(rhi_uniform(p, "u_con"), 1.f, geochunks_exaggeration());
}

} // namespace mare::engine::render

extern "C" {
void chunkatlas_update(void) { mare::engine::render::chunk_atlas().update(); }
void chunkatlas_bind(const rhi_pipeline_t *p, int a, int b, int c, int d) { mare::engine::render::chunk_atlas().bind(p, a, b, c, d); }
int  chunkatlas_resident(void) { return mare::engine::render::chunk_atlas().resident(); }
int  chunkatlas_available(void) { return mare::engine::render::chunk_atlas().available(); }
}
