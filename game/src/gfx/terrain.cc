// mare::engine::render::TerrainRenderer (see render.h): terrain. One heightmap texture sampled
// in the vertex shader, 5 LOD grids with skirts, frustum-culled 64x64-cell chunks, biome
// colouring, parchment map style with territories.

#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "render.h"
#include "../core/diag.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/regions.h"
#include "../assets/asset.h"
#include "../engine/shaders.h"
}

render_frame_t RF;
int screen_w = 1, screen_h = 1, window_w = 1, window_h = 1;
int terrain_chunks_drawn, terrain_tris_drawn, terrain_show_regions;
int terrain_ao_on = 1;    // baked horizon occlusion on the ambient term (MARE_TERRAIN_AO=0)

#define CHUNK 64
#define CHUNKS_X ((LM_W + CHUNK-1)/CHUNK)
#define CHUNKS_Y ((LM_H + CHUNK-1)/CHUNK)
#define LODS 5

namespace mare::engine::render {

class TerrainRenderer {
public:
    void init();
    float max_height() { return terrain_hmax; }       // terrain_max_height
    const rhi_texture_t *heightmap_texture() { return &tex_height; }
    void territory_upload();
    void draw(float planes[6][4]);

private:
    typedef struct chunk_t { float x0, z0, x1, z1, minh, maxh; int water_only; } chunk_t;

    float terrain_hmax = 1e9f;        // highest heightmap value, for the occlusion ray early-out
    chunk_t chunks[CHUNKS_X*CHUNKS_Y];
    rhi_pipeline_t terrain_prg;
    rhi_geometry_t terrain_geo[LODS];
    rhi_texture_t tex_height, tex_territory, tex_ao;
    int tex_atlas = -1;               // textures.c handle of the ground material atlas (NULL until streamed in)
    static constexpr float lod_dist[LODS-1] = { 45, 110, 240, 520 };

    // Streamed detail rings (streamview.c): two clipmap windows of delta height around the camera
    // - near (tactical detail) and far (coastal detail, 4x wider). Each fades out at its own
    // border; the near one cross-fades into the far rather than into nothing, so pulling the
    // camera back loses resolution, never relief. Shared by the vertex stage (height) and the
    // fragment stage (normals), which is why it lives in a string both of them include.

    void build_lod(int lod);
    static int aabb_visible(float planes[6][4], float x0, float y0, float z0, float x1, float y1, float z1);
};

TerrainRenderer &terrain_renderer() { static TerrainRenderer t; return t; }

void TerrainRenderer::build_lod(int lod) {
    int n = CHUNK >> lod, vn = n + 1, nverts = vn*vn + 4*vn, k = 0;
    float *v = (float *)malloc(nverts * 3 * sizeof(float));
    for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[k++] = x/(float)n; v[k++] = y/(float)n; v[k++] = 0; }
    int skirt0 = vn*vn;
    for (int i = 0; i < vn; ++i) { v[k++] = i/(float)n; v[k++] = 0; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = i/(float)n; v[k++] = 1; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = 0; v[k++] = i/(float)n; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = 1; v[k++] = i/(float)n; v[k++] = 1; }
    int nidx = n*n*6 + 4*n*6, m = 0;
    unsigned *idx = (unsigned *)malloc(nidx * sizeof(unsigned));
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { unsigned a = y*vn+x, b = a+1, c = a+vn, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
    for (int i = 0; i < n; ++i) {   // skirts (no culling, winding irrelevant)
        unsigned a = i, b = i+1, sa = skirt0 + i, sb = sa + 1;                   idx[m++]=a; idx[m++]=b; idx[m++]=sa; idx[m++]=b; idx[m++]=sb; idx[m++]=sa;
        a = n*vn + i; b = a+1; sa = skirt0 + vn + i; sb = sa+1;                   idx[m++]=a; idx[m++]=sa; idx[m++]=b; idx[m++]=b; idx[m++]=sa; idx[m++]=sb;
        a = i*vn; b = a+vn; sa = skirt0 + 2*vn + i; sb = sa+1;                    idx[m++]=a; idx[m++]=sa; idx[m++]=b; idx[m++]=b; idx[m++]=sa; idx[m++]=sb;
        a = i*vn + n; b = a+vn; sa = skirt0 + 3*vn + i; sb = sa+1;                idx[m++]=a; idx[m++]=b; idx[m++]=sa; idx[m++]=b; idx[m++]=sb; idx[m++]=sa;
    }
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 3, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 3*sizeof(float), 0 } };
    terrain_geo[lod] = rhi_geometry_make(&layout, v, nverts, idx, nidx);
    free(v); free(idx);
}

void TerrainRenderer::init() {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 3, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 3*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_vp", "u_origin", "u_size", "u_flatten", "u_hm", "u_geo", "u_k", "u_terr", "u_texel", "u_texel_world", "u_campos", "u_mapness", "u_sky", "u_nation_colors", "u_daylight", "u_regions", "u_terr_texel", "u_ao", "u_ao_on", "u_det", "u_det_far", "u_det_rect", "u_det_far_rect", "u_det_on", "u_det_texel", "u_atlas", "u_has_atlas", GLSL_LIGHT_UNIFORMS, 0 };
    const rhi_pipeline_desc_t terrain_desc{ .vs = shader_source("terrain/terrain.vert"), .fs = shader_source("terrain/terrain.frag"), .layout = &layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1 };   // (C compound literal -> named: C++ cannot take &T{...})
    terrain_prg = rhi_pipeline_make(&terrain_desc);
    for (int i = 0; i < LODS; ++i) build_lod(i);
    tex_height = rhi_texture_make(LM_W, LM_H, RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, heightmap);
    {   // Horizon occlusion (DESIGN_VISUAL.md step 9): at half the heightmap's resolution, the
        // mean over eight directions of how high the ground rises against the sky within 32
        // cells. A valley floor sees less sky than a ridge, so its ambient is darker. Baked once.
        double t0 = diag_now();
        int aw = LM_W / 2, ah = LM_H / 2; float *ao = (float *)malloc((size_t)aw * ah * sizeof(float));
        static const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1} };
        static const int steps[6] = { 1, 2, 4, 8, 16, 32 };
        float cell = 1.f / CELLS_PER_DEG * WORLD_K;
        for (int y = 0; y < ah; ++y) for (int x = 0; x < aw; ++x) {
            int hx = x * 2, hy = y * 2; float h0 = heightmap[hy * LM_W + hx], occ = 0;
            for (int d = 0; d < 8; ++d) { float best = 0;
                for (int s = 0; s < 6; ++s) { int sx = hx + dirs[d][0] * steps[s], sy = hy + dirs[d][1] * steps[s];
                    if (sx < 0 || sy < 0 || sx >= LM_W || sy >= LM_H) break;
                    float dh = heightmap[sy * LM_W + sx] - h0; if (dh <= 0) continue;
                    float dist = steps[s] * cell * (dirs[d][0] && dirs[d][1] ? 1.41421f : 1.f);
                    float sn = dh / sqrtf(dh * dh + dist * dist); if (sn > best) best = sn; }
                occ += best; }
            ao[y * aw + x] = clampf(1.f - 1.15f * occ / 8.f, 0.3f, 1.f);
        }
        tex_ao = rhi_texture_make(aw, ah, RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, ao);
        free(ao);
        LOG_INFO("gpu", "terrain horizon occlusion baked (%dx%d) in %.0f ms", aw, ah, (diag_now() - t0) * 1000);
        const char *e = getenv("MARE_TERRAIN_AO"); if (e) terrain_ao_on = atoi(e) != 0;
    }
    tex_territory = rhi_texture_make(TERR_W, TERR_H, RHI_FMT_RGB8, RHI_FILTER_NEAREST, RHI_WRAP_CLAMP, 0);
    {   // the ground material atlas, streamed like the sea textures: this worktree's terrain folder first
        char path[600]; FILE *pf;
        snprintf(path, sizeof path, "%s/terrain/terrain_textures.png", asset_root());
        if ((pf = fopen(path, "rb"))) fclose(pf); else snprintf(path, sizeof path, "%s/../terrain/terrain_textures.png", asset_root());
        tex_atlas = texture_request(path, RHI_FMT_SRGB8, RHI_FILTER_MIPMAP, RHI_WRAP_CLAMP);
    }
    for (int cy = 0; cy < CHUNKS_Y; ++cy) for (int cx = 0; cx < CHUNKS_X; ++cx) {
        chunk_t *c = &chunks[cy*CHUNKS_X+cx]; float mn = 1e9f, mx = -1e9f;
        for (int y = cy*CHUNK; y <= (cy+1)*CHUNK && y < LM_H; ++y) for (int x = cx*CHUNK; x <= (cx+1)*CHUNK && x < LM_W; ++x) { float h = heightmap[y*LM_W+x]; mn = minf(mn, h); mx = maxf(mx, h); }
        if (cx == 0 && cy == 0) terrain_hmax = mx; else terrain_hmax = maxf(terrain_hmax, mx);
        c->minh = mn; c->maxh = mx; c->water_only = mx < -0.05f;
        c->x0 = lon2x(LON0 + cx*CHUNK/(float)CELLS_PER_DEG); c->x1 = c->x0 + CHUNK/(float)CELLS_PER_DEG*WORLD_K;
        c->z0 = lat2z(LAT1 - cy*CHUNK/(float)CELLS_PER_DEG); c->z1 = c->z0 + CHUNK/(float)CELLS_PER_DEG*WORLD_K;
    }
}

void TerrainRenderer::territory_upload() {
    static unsigned char *rgb; if (!rgb) rgb = (unsigned char *)malloc(TERR_W*TERR_H*3);   // R nation+1, G development*80, B region id+1
    for (int i = 0; i < TERR_W*TERR_H; ++i) { rgb[i*3] = territory[i]; rgb[i*3+1] = devmap ? devmap[i] : 0; rgb[i*3+2] = regionmap ? regionmap[i] : 0; }
    rhi_texture_update(&tex_territory, rgb);
    territory_dirty = 0;
}

int TerrainRenderer::aabb_visible(float planes[6][4], float x0, float y0, float z0, float x1, float y1, float z1) {
    for (int i = 0; i < 6; ++i) { const float *p = planes[i];
        float px = p[0] > 0 ? x1 : x0, py = p[1] > 0 ? y1 : y0, pz = p[2] > 0 ? z1 : z0;
        if (p[0]*px + p[1]*py + p[2]*pz + p[3] < 0) return 0; }
    return 1;
}

void TerrainRenderer::draw(float planes[6][4]) {
    rhi_pipeline_bind(&terrain_prg);
    rhi_set_mat4(UNI(terrain_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_i1(UNI(terrain_prg,"u_hm"), 0); rhi_set_i1(UNI(terrain_prg,"u_terr"), 1);
    rhi_texture_bind(0, &tex_height);
    rhi_set_i1(UNI(terrain_prg,"u_ao"), 5); rhi_texture_bind(5, &tex_ao); rhi_set_f1(UNI(terrain_prg,"u_ao_on"), terrain_ao_on ? 1.f : 0.f);
    {   // streamed detail rings (streamview.c): the resident tiles' delta around the camera,
        // near (tactical) on unit 6 and far (coastal) on unit 7
        const rhi_texture_t *dn = streamview_detail_tex(0), *df = streamview_detail_tex(1);
        float rn[4] = { 0, 0, 1, 1 }, rf[4] = { 0, 0, 1, 1 };
        if (dn) streamview_detail_rect(0, rn);
        if (df) streamview_detail_rect(1, rf);
        rhi_set_i1(UNI(terrain_prg,"u_det"), 6);     rhi_texture_bind(6, dn ? dn : &tex_height);
        rhi_set_i1(UNI(terrain_prg,"u_det_far"), 7); rhi_texture_bind(7, df ? df : &tex_height);
        rhi_set_f4(UNI(terrain_prg,"u_det_rect"), rn[0], rn[1], rn[2], rn[3]);
        rhi_set_f4(UNI(terrain_prg,"u_det_far_rect"), rf[0], rf[1], rf[2], rf[3]);
        rhi_set_f2(UNI(terrain_prg,"u_det_on"), dn ? 1.f : 0.f, df ? 1.f : 0.f);
        rhi_set_f2(UNI(terrain_prg,"u_det_texel"), streamview_detail_texel(0), streamview_detail_texel(1));
    }
    rhi_texture_bind(1, &tex_territory);
    { const rhi_texture_t *atlas = texture_get(tex_atlas);
      rhi_set_i1(UNI(terrain_prg,"u_atlas"), 8); rhi_texture_bind(8, atlas ? atlas : &tex_territory);
      rhi_set_f1(UNI(terrain_prg,"u_has_atlas"), atlas ? 1.f : 0.f); }
    rhi_set_f4(UNI(terrain_prg,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(UNI(terrain_prg,"u_k"), WORLD_K);
    rhi_set_f2(UNI(terrain_prg,"u_texel"), 1.f/LM_W, 1.f/LM_H);
    rhi_set_f1(UNI(terrain_prg,"u_texel_world"), WORLD_K / CELLS_PER_DEG);
    rhi_set_f3(UNI(terrain_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(terrain_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    light_bind(&terrain_prg);
    rhi_set_f1(UNI(terrain_prg,"u_mapness"), RF.mapness);
    rhi_set_f1(UNI(terrain_prg,"u_flatten"), RF.flatten);
    rhi_set_f1(UNI(terrain_prg,"u_daylight"), RF.daylight);
    rhi_set_f1(UNI(terrain_prg,"u_regions"), terrain_show_regions ? 1.f : 0.f);
    rhi_set_f2(UNI(terrain_prg,"u_terr_texel"), 1.f/TERR_W, 1.f/TERR_H);
    float cols[11*3];
    for (int i = 0; i < 11; ++i) { unsigned c = i == 0 ? 0xFF888888 : nations_info[i-1].color; cols[i*3] = col_r(c); cols[i*3+1] = col_g(c); cols[i*3+2] = col_b(c); }
    rhi_set_f3v(UNI(terrain_prg,"u_nation_colors"), cols, 11);
    rhi_set_f1(UNI(terrain_prg,"u_size"), CHUNK/(float)CELLS_PER_DEG*WORLD_K);
    int loc_origin = UNI(terrain_prg,"u_origin");
    terrain_chunks_drawn = terrain_tris_drawn = 0;
    for (int i = 0; i < CHUNKS_X*CHUNKS_Y; ++i) {
        const chunk_t *c = &chunks[i];
        if (c->water_only || !aabb_visible(planes, c->x0, minf(c->minh,-3.5f), c->z0, c->x1, maxf(c->maxh + 0.6f, 0.5f), c->z1)) continue;   // +0.6: the streamed detail delta can raise a peak
        float dx = maxf(maxf(c->x0 - RF.campos.x, 0), RF.campos.x - c->x1), dz = maxf(maxf(c->z0 - RF.campos.z, 0), RF.campos.z - c->z1);
        float d = sqrtf(dx*dx + dz*dz + RF.campos.y*RF.campos.y*0.5f);
        int lod = 0; while (lod < LODS-1 && d > lod_dist[lod]) lod++;
        if (RF.mapness > 0.99f && lod < 2) lod = 2;
        rhi_set_f2(loc_origin, c->x0, c->z0);
        rhi_draw(&terrain_geo[lod], 0);                 // rhi_draw caches the geometry binding across chunks
        terrain_chunks_drawn++; terrain_tris_drawn += terrain_geo[lod].nindices/3;
    }
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
float terrain_max_height(void) { return mare::engine::render::terrain_renderer().max_height(); }
void terrain_init(void) { mare::engine::render::terrain_renderer().init(); }
const rhi_texture_t *terrain_heightmap_texture(void) { return mare::engine::render::terrain_renderer().heightmap_texture(); }
void render_territory_upload(void) { mare::engine::render::terrain_renderer().territory_upload(); }
void terrain_draw(float planes[6][4]) { mare::engine::render::terrain_renderer().draw(planes); }
}
