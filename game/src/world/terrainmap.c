// Authored terrain maps (see terrainmap.h, game/docs/DESIGN_TERRAIN.md).
// Sections: loading · sampling (height, normal) · the ground-material rule · folding the
// authored height into the base world.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "terrainmap.h"
#include "world.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../assets/asset.h"
#include "terraintiles.h"

// stb_image is compiled exactly once in the unity build (the framework owns the
// implementation); including the header again here expands it a second time, so just declare
// the three entry points this file uses.
extern unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len,
                                            int *x, int *y, int *channels_in_file, int desired_channels);
extern void           stbi_image_free(void *retval_from_stbi_load);
extern const char    *stbi_failure_reason(void);

terrainmap_t TM;

// The maps are equirectangular over the whole playable extent. If a map ever covers a
// different window, this is the one place that has to know.
#define TM_LON0 LON0
#define TM_LON1 LON1
#define TM_LAT0 LAT0
#define TM_LAT1 LAT1

// Sea level in the depth map and how much height the range above it carries. The supplied
// bathymetry puts the shoreline at mid grey; TERRAIN_HEIGHT is the world height of pure white,
// chosen so the highest ground matches the procedural world the ports were placed against.
#define TM_SEA_LEVEL    0.52f
#define TM_HEIGHT_SCALE 9.5f
#define TM_DEPTH_SCALE  6.0f     // world units from sea level down to pure black

static unsigned char *tm_read_image(const char *dir, const char *file, int *w, int *h, int *c, int want) {
    char path[512]; snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG_DEBUG("terrain", "no %s (the procedural world stands in)", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *bytes = n > 0 ? (unsigned char *)malloc((size_t)n) : NULL;
    size_t got = bytes ? fread(bytes, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!bytes || got != (size_t)n) { free(bytes); LOG_WARN("terrain", "cannot read %s", path); return NULL; }
    int ch = 0;
    unsigned char *px = stbi_load_from_memory(bytes, (int)n, w, h, &ch, want);
    free(bytes);
    if (!px) { LOG_WARN("terrain", "%s: %s", path, stbi_failure_reason()); return NULL; }
    *c = want;
    LOG_INFO("terrain", "%s loaded (%dx%d, %d channels, %.1f MB)", file, *w, *h, want, (double)(*w) * (*h) * want / 1e6);
    return px;
}

void terrainmap_load(const char *dir) {
    char fallback[512], probe[600];
    if (!dir) {   // the per-worktree terrain maps first, the shared folder as the fallback
        snprintf(fallback, sizeof fallback, "%s/terrain", asset_root());
        snprintf(probe, sizeof probe, "%s/depth_map_terrain.png", fallback);
        FILE *pf = fopen(probe, "rb");
        if (pf) fclose(pf);
        else snprintf(fallback, sizeof fallback, "%s/../terrain", asset_root());
        dir = fallback;
    }
    double t0 = diag_now();
    memset(&TM, 0, sizeof TM);
    TM.lon0 = TM_LON0; TM.lon1 = TM_LON1; TM.lat0 = TM_LAT0; TM.lat1 = TM_LAT1;
    TM.sea_level = TM_SEA_LEVEL; TM.height_scale = TM_HEIGHT_SCALE;
    TM.depth  = tm_read_image(dir, "depth_map_terrain.png",  &TM.dw, &TM.dh, &TM.dc, 1);
    TM.normal = tm_read_image(dir, "normal_map_terrain.png", &TM.nw, &TM.nh, &TM.nc, 3);
    TM.atlas  = tm_read_image(dir, "terrain_textures.png",   &TM.aw, &TM.ah, &TM.ac, 3);
    TM.loaded = TM.depth != NULL;
    if (TM.loaded)
        LOG_INFO("terrain", "authored terrain in use: depth %dx%d, normals %s, atlas %s (%dx%d cells), %.0f ms",
                 TM.dw, TM.dh, TM.normal ? "yes" : "no", TM.atlas ? "yes" : "no",
                 TERRAIN_ATLAS_X, TERRAIN_ATLAS_Y, (diag_now() - t0) * 1000);
    else
        LOG_INFO("terrain", "no authored terrain under %s: the procedural world is unchanged", dir);
}

void terrainmap_free(void) {
    if (TM.depth)  stbi_image_free(TM.depth);
    if (TM.normal) stbi_image_free(TM.normal);
    if (TM.atlas)  stbi_image_free(TM.atlas);
    memset(&TM, 0, sizeof TM);
}

// bilinear sample of a single channel, clamped at the edges
static float tm_bilinear(const unsigned char *px, int w, int h, int c, int ch, float u, float v) {
    float fx = clampf(u, 0.f, 1.f) * (w - 1), fy = clampf(v, 0.f, 1.f) * (h - 1);
    int x0 = (int)fx, y0 = (int)fy, x1 = mini(x0 + 1, w - 1), y1 = mini(y0 + 1, h - 1);
    float tx = fx - x0, ty = fy - y0;
    const unsigned char *r0 = px + ((size_t)y0 * w) * c, *r1 = px + ((size_t)y1 * w) * c;
    float a = r0[x0 * c + ch], b = r0[x1 * c + ch], d = r1[x0 * c + ch], e = r1[x1 * c + ch];
    return lerpf(lerpf(a, b, tx), lerpf(d, e, tx), ty) / 255.f;
}

static int tm_uv(float lon, float lat, float *u, float *v) {
    if (!TM.loaded) return 0;
    if (lon < TM.lon0 || lon > TM.lon1 || lat < TM.lat0 || lat > TM.lat1) return 0;
    *u = (lon - TM.lon0) / (TM.lon1 - TM.lon0);
    *v = (TM.lat1 - lat) / (TM.lat1 - TM.lat0);     // image row 0 is the northern edge
    return 1;
}

float terrainmap_height(float lon, float lat, float fallback) {
    float u, v;
    if (!tm_uv(lon, lat, &u, &v)) return fallback;
    float g = tm_bilinear(TM.depth, TM.dw, TM.dh, TM.dc, 0, u, v);
    return g >= TM.sea_level ? (g - TM.sea_level) / (1.f - TM.sea_level) * TM.height_scale
                             : -(TM.sea_level - g) / TM.sea_level * TM_DEPTH_SCALE;
}

int terrainmap_normal(float lon, float lat, float out[3]) {
    float u, v;
    if (!TM.normal || !tm_uv(lon, lat, &u, &v)) return 0;
    float nx = tm_bilinear(TM.normal, TM.nw, TM.nh, TM.nc, 0, u, v) * 2.f - 1.f;
    float ny = tm_bilinear(TM.normal, TM.nw, TM.nh, TM.nc, 1, u, v) * 2.f - 1.f;
    float nz = tm_bilinear(TM.normal, TM.nw, TM.nh, TM.nc, 2, u, v) * 2.f - 1.f;
    // tangent-space RGB: +x east, +y north (which is -z in world), +z up
    float wx = nx, wy = nz, wz = -ny;
    float l = sqrtf(wx*wx + wy*wy + wz*wz);
    if (l < 1e-4f) return 0;
    out[0] = wx / l; out[1] = wy / l; out[2] = wz / l;
    return 1;
}

// Which ground an exposed fragment is. Deliberately the same rule the terrain shader runs, so
// anything on the CPU that needs to know what it is standing on agrees with what is drawn.
int terrainmap_ground(float height, float slope, float lat) {
    float a = fabsf(lat);
    if (height < 0.35f)             return GROUND_SAND;         // beach
    if (slope > 0.55f)              return GROUND_ROCK;         // cliff
    if (height > 7.0f)              return GROUND_SHINGLE;      // bare high ground
    if (slope > 0.32f)              return GROUND_GRAVEL;
    if (a > 58.f)                   return GROUND_MOSS;         // tundra
    if (a < 12.f)                   return height > 2.5f ? GROUND_SOIL : GROUND_MEADOW;
    if (a > 15.f && a < 33.f)       return height < 1.6f ? GROUND_DUNE : GROUND_CRACKED;   // desert belt
    return height > 3.5f ? GROUND_TURF : GROUND_GRASS;
}

// Fold the authored geography in from the streamed tiles (see terrainmap.h). The decisions
// live in terraintiles_bake; this driver supplies what it must not touch - a protection mask
// over every historical harbour basin, so no island can land on a port's water - and repairs
// what a mask change invalidates: the carved straits are forced open again and the signed
// coast-distance field is rebuilt before navigation reads it.
#include "../data/geo_ports.h"

int terrainmap_bake_geography(const char *tiles_dir) {
    if (!heightmap || !landmask) return 0;
    { const char *on = getenv("MARE_TERRAIN_GEO");
      if (on && atoi(on) == 0) { LOG_INFO("terrain", "geography fold off (MARE_TERRAIN_GEO=0)"); return 0; } }
    char fallback[512], probe[600];
    if (!tiles_dir) {   // the per-worktree tile set first, the shared one as the fallback (terraintiles.c)
        snprintf(fallback, sizeof fallback, "%s/terrain/tiles", asset_root());
        snprintf(probe, sizeof probe, "%s/height.json", fallback);
        FILE *pf = fopen(probe, "rb");
        if (pf) fclose(pf);
        else snprintf(fallback, sizeof fallback, "%s/../terrain/tiles", asset_root());
        tiles_dir = fallback;
    }
    unsigned char *protect = (unsigned char *)calloc(LM_W * LM_H, 1);
    if (protect)
        for (int p = 0; p < GEO_NPORTS; ++p) {
            int cx = lon2cx(geo_ports[p].lon), cy = lat2cy(geo_ports[p].lat);
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (x >= 0 && y >= 0 && x < LM_W && y < LM_H) protect[y * LM_W + x] = 1;
            }
        }
    int flips = 0;
    double lm0 = 0, lmax0 = 0; long nl = 0;      // land height statistics, so a fold that flattens the world is visible in the log
    for (int i = 0; i < LM_W * LM_H; ++i) if (landmask[i]) { lm0 += heightmap[i]; if (heightmap[i] > lmax0) lmax0 = heightmap[i]; nl++; }
    int changed = terraintiles_bake(tiles_dir, heightmap, landmask, LM_W, LM_H,
                                    LON0, LAT1, (float)CELLS_PER_DEG, protect, &flips);
    { double lm1 = 0, lmax1 = 0; for (int i = 0; i < LM_W * LM_H; ++i) if (landmask[i]) { lm1 += heightmap[i]; if (heightmap[i] > lmax1) lmax1 = heightmap[i]; }
      LOG_INFO("terrain", "land height mean %.2f max %.2f before the fold, %.2f / %.2f after", nl ? lm0 / nl : 0, lmax0, nl ? lm1 / nl : 0, lmax1); }
    free(protect);
    if (flips) {   // the mask changed (MARE_TERRAIN_ISLANDS experiment): repair what depends on it
        world_recarve_straits();
        world_recompute_coastdist();
    }
    if (changed)
        LOG_INFO("terrain", "geography fold done: %d cells (%d new land)%s", changed, flips,
                 flips ? ", straits recarved, coast distance rebuilt" : "");
    return changed;
}

void terrainmap_apply_to_world(void) {
    if (!TM.loaded || !heightmap) return;
    // Opt-in: folding the 1376x768 depth map in replaces the procedural relief with something
    // *smoother* than it, because that map is a shaded world overview and not a DEM. It stays
    // here because it is the right hook for a real one. MARE_TERRAIN_MAP=1.
    { const char *on = getenv("MARE_TERRAIN_MAP");
      if (!on || atoi(on) == 0) { LOG_INFO("terrain", "authored height fold off (MARE_TERRAIN_MAP=1 enables it)"); return; } }
    double t0 = diag_now();
    int changed = 0;
    for (int y = 0; y < LM_H; ++y) for (int x = 0; x < LM_W; ++x) {
        int i = y * LM_W + x;
        float lon = cx2lon(x), lat = cy2lat(y);
        float h = terrainmap_height(lon, lat, heightmap[i]);
        int land_now = heightmap[i] > 0.f, land_map = h > 0.f;
        if (land_now != land_map) continue;          // disagreement: keep the coastline the ports know
        heightmap[i] = h; changed++;
    }
    LOG_INFO("terrain", "authored heights folded into %d of %d cells (%.0f%%) in %.0f ms",
             changed, LM_W * LM_H, 100.0 * changed / (LM_W * LM_H), (diag_now() - t0) * 1000);
}
