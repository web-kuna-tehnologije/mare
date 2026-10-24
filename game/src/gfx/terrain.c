// Terrain: one heightmap texture sampled in the vertex shader, 5 LOD grids with skirts,
// frustum-culled 64x64-cell chunks, biome colouring, parchment map style with territories.

#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/regions.h"

render_frame_t RF;
int screen_w = 1, screen_h = 1, window_w = 1, window_h = 1;
int terrain_chunks_drawn, terrain_tris_drawn, terrain_show_regions;

#define CHUNK 64
#define CHUNKS_X ((LM_W + CHUNK-1)/CHUNK)
#define CHUNKS_Y ((LM_H + CHUNK-1)/CHUNK)
#define LODS 5

typedef struct chunk_t { float x0, z0, x1, z1, minh, maxh; int water_only; } chunk_t;
static chunk_t chunks[CHUNKS_X*CHUNKS_Y];
static rhi_pipeline_t terrain_prg;
static rhi_geometry_t terrain_geo[LODS];
static rhi_texture_t tex_height, tex_territory;
static const float lod_dist[LODS-1] = { 45, 110, 240, 520 };

static const char *terrain_vs =
    "in vec3 a_pos;\n" // u, v, skirt
    "uniform mat4 u_vp; uniform vec2 u_origin; uniform float u_size; uniform float u_flatten;\n"
    "uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;\n"
    "out vec3 v_wpos; out vec2 v_uv; out float v_h;\n"
    "void main(){\n"
    "  vec2 xz = u_origin + a_pos.xy * u_size;\n"
    "  float lon = xz.x / u_k, lat = -xz.y / u_k;\n"
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n"
    "  float h = texture(u_hm, uv).r;\n"
    "  float hf = mix(h, (h > 0.0 ? 0.12 : -0.5), u_flatten);\n"
    "  v_wpos = vec3(xz.x, hf - a_pos.z * 3.0, xz.y); v_uv = uv; v_h = h;\n"
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n"
    "}\n";

static const char *terrain_fs =
    "in vec3 v_wpos; in vec2 v_uv; in float v_h;\n"
    "uniform sampler2D u_hm; uniform sampler2D u_terr; uniform vec2 u_texel; uniform float u_texel_world;\n"
    "uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_k;\n"
    "uniform vec3 u_nation_colors[11]; uniform float u_daylight; uniform float u_regions; uniform vec2 u_terr_texel;\n"
    "out vec4 fragColor;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }\n"
    "float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_FOG
    "void main(){\n"
    "  float hl = texture(u_hm, v_uv - vec2(u_texel.x,0)).r, hr = texture(u_hm, v_uv + vec2(u_texel.x,0)).r;\n"
    "  float hu = texture(u_hm, v_uv - vec2(0,u_texel.y)).r, hd = texture(u_hm, v_uv + vec2(0,u_texel.y)).r;\n"
    "  vec3 n = normalize(vec3(-(hr-hl)/(2.0*u_texel_world), 1.0, -(hd-hu)/(2.0*u_texel_world)));\n"
    "  float h = v_h; float lat = -v_wpos.z / u_k; float lon = v_wpos.x / u_k;\n"
    "  float slope = 1.0 - n.y;\n"
    "  float nz = noise(v_wpos.xz*0.9) * 0.6 + noise(v_wpos.xz*3.7)*0.4;\n"
    "  vec3 sand  = vec3(0.78,0.72,0.52);\n"
    "  vec3 grass = mix(vec3(0.33,0.50,0.22), vec3(0.22,0.38,0.16), nz);\n"
    "  vec3 dry   = mix(vec3(0.55,0.50,0.32), vec3(0.45,0.42,0.28), nz);\n"
    "  vec3 rock  = vec3(0.46,0.43,0.40);\n"
    "  vec3 snow  = vec3(0.93,0.94,0.96);\n"
    "  vec3 desert= mix(vec3(0.86,0.74,0.48), vec3(0.80,0.66,0.42), nz);\n"
    "  vec3 tundra= mix(vec3(0.55,0.58,0.48), vec3(0.70,0.72,0.68), nz);\n"
    "  vec3 c = mix(sand, grass, smoothstep(0.15, 0.6, h));\n"
    "  c = mix(c, dry, smoothstep(2.5, 5.0, h));\n"
    "  c = mix(c, rock, smoothstep(0.25, 0.55, slope));\n"
    "  c = mix(c, snow, smoothstep(6.5, 8.5, h));\n"
    // deserts: sahara, arabia, northern mexico, atacama
    "  float des = 0.0;\n"
    "  des = max(des, (1.0-smoothstep(30.0,33.0,lat)) * smoothstep(13.0,16.0,lat) * smoothstep(-18.0,-14.0,lon) * (1.0-smoothstep(36.0,40.0,lon)));\n"
    "  des = max(des, smoothstep(14.0,18.0,lat)*(1.0-smoothstep(30.0,34.0,lat))*smoothstep(35.0,38.0,lon));\n"
    "  des = max(des, smoothstep(22.0,25.0,lat)*(1.0-smoothstep(32.0,35.0,lat))*smoothstep(-116.0,-112.0,lon)*(1.0-smoothstep(-103.0,-100.0,lon)));\n"
    "  des = max(des, smoothstep(-30.0,-26.0,lat)*(1.0-smoothstep(-16.0,-14.0,lat))*smoothstep(-72.0,-70.0,lon)*(1.0-smoothstep(-66.0,-64.0,lon)));\n"
    "  c = mix(c, desert, des * smoothstep(0.12, 0.5, h) * (1.0 - smoothstep(5.0, 7.0, h)));\n"
    "  float cold = smoothstep(52.0, 64.0, abs(lat));\n"
    "  c = mix(c, tundra, cold * smoothstep(0.3,1.0,h));\n"
    "  c = mix(c, snow, smoothstep(64.0, 70.0, abs(lat)) * 0.8);\n"
    "  float diff = max(dot(n, u_sun), 0.0);\n"
    // PBR: ground is a rough dielectric; rock/snow slightly smoother than vegetation
    "  float rough = mix(0.95, 0.75, smoothstep(0.25, 0.6, slope) * 0.6 + smoothstep(6.5, 8.5, h) * 0.4);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    "  float viewdist = distance(v_wpos, u_campos);\n"
    "  vec3 lit = sun_shade(srgb_to_linear(c), n, vdir, v_wpos, rough, 0.0, viewdist);\n"
    // map style
    "  vec4 tt = texture(u_terr, v_uv);\n"
    "  int nid = int(tt.r * 255.0 + 0.5);\n"
    "  float dev = tt.g * 255.0 / 80.0;\n"
    "  float rid = tt.b, border = 0.0;\n"
    "  if (u_regions > 0.5) { border += float(abs(texture(u_terr, v_uv + vec2(u_terr_texel.x,0)).b - rid) > 0.001); border += float(abs(texture(u_terr, v_uv + vec2(0,u_terr_texel.y)).b - rid) > 0.001); }\n"
    "  vec3 parch = vec3(0.87,0.80,0.64) * (0.85 + 0.15*nz);\n"
    "  vec3 owner = nid > 0 ? u_nation_colors[min(nid,10)] : parch;\n"
    "  vec3 mapc = mix(parch, owner, nid > 0 ? 0.55 : 0.0);\n"
    "  mapc *= 0.75 + 0.25 * diff;\n"
    "  mapc = mix(mapc, mapc*0.45, smoothstep(6.0, 9.0, h)*0.6);\n"
    "  float coastline = 1.0 - smoothstep(0.05, 0.28, h);\n"
    "  mapc = mix(mapc, vec3(0.35,0.28,0.18), coastline*0.8);\n"
    "  if (u_regions > 0.5) { vec3 devc = mix(vec3(0.55,0.50,0.40), vec3(0.95,0.85,0.45), clamp(dev/3.0,0.0,1.0)); mapc = mix(mapc, devc, 0.55); mapc = mix(mapc, vec3(0.15,0.10,0.05), min(border,1.0)*0.8); }\n"
    "  vec3 col = mix(lit, srgb_to_linear(mapc) * u_daylight, u_mapness);\n"
    "  col = fog_apply(col, viewdist, u_mapness, srgb_to_linear(u_sky));\n"
    "  fragColor = vec4(col, 1.0);\n"
    "}\n";


static void build_lod(int lod) {
    int n = CHUNK >> lod, vn = n + 1, nverts = vn*vn + 4*vn, k = 0;
    float *v = malloc(nverts * 3 * sizeof(float));
    for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[k++] = x/(float)n; v[k++] = y/(float)n; v[k++] = 0; }
    int skirt0 = vn*vn;
    for (int i = 0; i < vn; ++i) { v[k++] = i/(float)n; v[k++] = 0; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = i/(float)n; v[k++] = 1; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = 0; v[k++] = i/(float)n; v[k++] = 1; }
    for (int i = 0; i < vn; ++i) { v[k++] = 1; v[k++] = i/(float)n; v[k++] = 1; }
    int nidx = n*n*6 + 4*n*6, m = 0;
    unsigned *idx = malloc(nidx * sizeof(unsigned));
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

void terrain_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 3, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 3*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_vp", "u_origin", "u_size", "u_flatten", "u_hm", "u_geo", "u_k", "u_terr", "u_texel", "u_texel_world", "u_campos", "u_mapness", "u_sky", "u_nation_colors", "u_daylight", "u_regions", "u_terr_texel", GLSL_LIGHT_UNIFORMS, 0 };
    terrain_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = terrain_vs, .fs = terrain_fs, .layout = &layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1 });
    for (int i = 0; i < LODS; ++i) build_lod(i);
    tex_height = rhi_texture_make(LM_W, LM_H, RHI_FMT_R32F, RHI_FILTER_LINEAR, heightmap);
    tex_territory = rhi_texture_make(TERR_W, TERR_H, RHI_FMT_RGB8, RHI_FILTER_NEAREST, 0);
    for (int cy = 0; cy < CHUNKS_Y; ++cy) for (int cx = 0; cx < CHUNKS_X; ++cx) {
        chunk_t *c = &chunks[cy*CHUNKS_X+cx]; float mn = 1e9f, mx = -1e9f;
        for (int y = cy*CHUNK; y <= (cy+1)*CHUNK && y < LM_H; ++y) for (int x = cx*CHUNK; x <= (cx+1)*CHUNK && x < LM_W; ++x) { float h = heightmap[y*LM_W+x]; mn = minf(mn, h); mx = maxf(mx, h); }
        c->minh = mn; c->maxh = mx; c->water_only = mx < -0.05f;
        c->x0 = lon2x(LON0 + cx*CHUNK/(float)CELLS_PER_DEG); c->x1 = c->x0 + CHUNK/(float)CELLS_PER_DEG*WORLD_K;
        c->z0 = lat2z(LAT1 - cy*CHUNK/(float)CELLS_PER_DEG); c->z1 = c->z0 + CHUNK/(float)CELLS_PER_DEG*WORLD_K;
    }
}
const rhi_texture_t *terrain_heightmap_texture(void) { return &tex_height; }

void render_territory_upload(void) {
    static unsigned char *rgb; if (!rgb) rgb = malloc(TERR_W*TERR_H*3);   // R nation+1, G development*80, B region id+1
    for (int i = 0; i < TERR_W*TERR_H; ++i) { rgb[i*3] = territory[i]; rgb[i*3+1] = devmap ? devmap[i] : 0; rgb[i*3+2] = regionmap ? regionmap[i] : 0; }
    rhi_texture_update(&tex_territory, rgb);
    territory_dirty = 0;
}

static int aabb_visible(float planes[6][4], float x0, float y0, float z0, float x1, float y1, float z1) {
    for (int i = 0; i < 6; ++i) { const float *p = planes[i];
        float px = p[0] > 0 ? x1 : x0, py = p[1] > 0 ? y1 : y0, pz = p[2] > 0 ? z1 : z0;
        if (p[0]*px + p[1]*py + p[2]*pz + p[3] < 0) return 0; }
    return 1;
}

void terrain_draw(float planes[6][4]) {
    rhi_pipeline_bind(&terrain_prg);
    rhi_set_mat4(UNI(terrain_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_i1(UNI(terrain_prg,"u_hm"), 0); rhi_set_i1(UNI(terrain_prg,"u_terr"), 1);
    rhi_texture_bind(0, &tex_height);
    rhi_texture_bind(1, &tex_territory);
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
        if (c->water_only || !aabb_visible(planes, c->x0, minf(c->minh,-3.5f), c->z0, c->x1, maxf(c->maxh,0.5f), c->z1)) continue;
        float dx = maxf(maxf(c->x0 - RF.campos.x, 0), RF.campos.x - c->x1), dz = maxf(maxf(c->z0 - RF.campos.z, 0), RF.campos.z - c->z1);
        float d = sqrtf(dx*dx + dz*dz + RF.campos.y*RF.campos.y*0.5f);
        int lod = 0; while (lod < LODS-1 && d > lod_dist[lod]) lod++;
        if (RF.mapness > 0.99f && lod < 2) lod = 2;
        rhi_set_f2(loc_origin, c->x0, c->z0);
        rhi_draw(&terrain_geo[lod], 0);                 // rhi_draw caches the geometry binding across chunks
        terrain_chunks_drawn++; terrain_tris_drawn += terrain_geo[lod].nindices/3;
    }
}
