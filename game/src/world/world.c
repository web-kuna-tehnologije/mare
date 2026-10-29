// Static world (see world.h). Sections: noise · even-odd scanline rasterizer and strait carving ·
// multi-source BFS coast distance · heightfield from coast distance + fbm · height/land lookups ·
// territory map · init (asserts that LM_W/LM_H match the extents).
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "world.h"
#include "../core/util.h"
#include "../data/geo_coast.h"
#include "../core/diag.h"

unsigned char *landmask;
short         *coastdist;
float         *heightmap;
unsigned char *territory;
int            territory_dirty = 1;

// ---------------------------------------------------------------------------
// noise

static inline float hash2(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.f;
}
float world_vnoise(float x, float y, unsigned seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx*fx*(3-2*fx); fy = fy*fy*(3-2*fy);
    float a = hash2(ix,iy,seed),   b = hash2(ix+1,iy,seed);
    float c = hash2(ix,iy+1,seed), d = hash2(ix+1,iy+1,seed);
    return lerpf(lerpf(a,b,fx), lerpf(c,d,fx), fy);
}
float world_fbm(float x, float y, int oct, unsigned seed) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; ++i) { s += a * world_vnoise(x, y, seed + i*77); n += a; x *= 2.03f; y *= 1.97f; a *= 0.5f; }
    return s / n;
}

// ---------------------------------------------------------------------------
// rasterization (even-odd scanline fill)

static void raster_polygon(const float *pts, unsigned char value) {
    int n = 0; while (pts[n*2] != END) n++;
    if (n < 3) return;
    float xs[512];
    for (int cy = 0; cy < LM_H; ++cy) {
        float lat = cy2lat(cy);
        int nx = 0;
        for (int i = 0; i < n; ++i) {
            float x0 = pts[i*2], y0 = pts[i*2+1], x1 = pts[((i+1)%n)*2], y1 = pts[((i+1)%n)*2+1];
            if ((lat >= y0 && lat < y1) || (lat >= y1 && lat < y0)) {
                float t = (lat - y0) / (y1 - y0);
                if (nx < 512) xs[nx++] = x0 + t * (x1 - x0);
            }
        }
        for (int i = 1; i < nx; ++i) { float v = xs[i]; int j = i-1; while (j >= 0 && xs[j] > v) { xs[j+1] = xs[j]; j--; } xs[j+1] = v; }
        for (int i = 0; i + 1 < nx; i += 2) {
            int ca = (int)ceilf((xs[i]   - LON0) * CELLS_PER_DEG - 0.5f);
            int cb = (int)floorf((xs[i+1] - LON0) * CELLS_PER_DEG - 0.5f);
            ca = maxi(ca, 0); cb = mini(cb, LM_W-1);
            for (int cx = ca; cx <= cb; ++cx) landmask[cy*LM_W+cx] = value;
        }
    }
}

static void raster_strait(const float *pts, float halfwidth) {
    int n = 0; while (pts[n*2] != END) n++;
    float hw = halfwidth * CELLS_PER_DEG;
    for (int i = 0; i + 1 < n; ++i) {
        float ax = (pts[i*2]-LON0)*CELLS_PER_DEG,   ay = (LAT1-pts[i*2+1])*CELLS_PER_DEG;
        float bx = (pts[i*2+2]-LON0)*CELLS_PER_DEG, by = (LAT1-pts[i*2+3])*CELLS_PER_DEG;
        int x0 = (int)floorf(minf(ax,bx)-hw-1), x1 = (int)ceilf(maxf(ax,bx)+hw+1);
        int y0 = (int)floorf(minf(ay,by)-hw-1), y1 = (int)ceilf(maxf(ay,by)+hw+1);
        for (int y = maxi(y0,0); y <= mini(y1,LM_H-1); ++y) for (int x = maxi(x0,0); x <= mini(x1,LM_W-1); ++x) {
            float px = x + 0.5f, py = y + 0.5f, dx = bx-ax, dy = by-ay, l2 = dx*dx+dy*dy;
            float t = l2 > 0 ? clampf(((px-ax)*dx+(py-ay)*dy)/l2, 0, 1) : 0;
            float qx = ax + t*dx - px, qy = ay + t*dy - py;
            if (qx*qx+qy*qy <= hw*hw) landmask[y*LM_W+x] = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// coast distance (multi-source BFS, 8-neighbourhood)

static void compute_coastdist(void) {
    int N = LM_W*LM_H;
    int *queue = malloc(N * sizeof(int)); int qh = 0, qt = 0;
    for (int i = 0; i < N; ++i) coastdist[i] = 0x7fff;
    for (int y = 0; y < LM_H; ++y) for (int x = 0; x < LM_W; ++x) {
        int i = y*LM_W+x, v = landmask[i], edge = 0;
        for (int dy = -1; dy <= 1 && !edge; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int xx = x+dx, yy = y+dy; if (xx < 0 || yy < 0 || xx >= LM_W || yy >= LM_H) continue;
            if (landmask[yy*LM_W+xx] != v) { edge = 1; break; }
        }
        if (edge) { coastdist[i] = 1; queue[qt++] = i; }
    }
    while (qh < qt) {
        int i = queue[qh++], x = i % LM_W, y = i / LM_W; short d = coastdist[i];
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int xx = x+dx, yy = y+dy; if (xx < 0 || yy < 0 || xx >= LM_W || yy >= LM_H) continue;
            int j = yy*LM_W+xx;
            if (coastdist[j] == 0x7fff) { coastdist[j] = d+1; queue[qt++] = j; }
        }
    }
    for (int i = 0; i < N; ++i) { if (coastdist[i] == 0x7fff) coastdist[i] = 200; if (!landmask[i]) coastdist[i] = -coastdist[i]; }
    free(queue);
}

// ---------------------------------------------------------------------------
// heightfield

static void compute_heightmap(unsigned seed) {
    for (int y = 0; y < LM_H; ++y) for (int x = 0; x < LM_W; ++x) {
        int i = y*LM_W+x;
        float lon = cx2lon(x), lat = cy2lat(y);
        float n1 = world_fbm(lon*0.35f, lat*0.35f, 4, seed);
        float n2 = world_fbm(lon*0.12f+31, lat*0.12f+17, 5, seed+99);
        float n3 = world_fbm(lon*1.2f, lat*1.2f, 3, seed+7);
        int d = coastdist[i];
        float h;
        if (d > 0) {
            float base = minf(d, 40) / 40.f;
            h = 0.12f + 3.2f * powf(base, 0.75f) * (0.5f + 0.5f*n1);
            float m = maxf(0, n2 - 0.52f) / 0.48f;
            float inland = clampf((d - 5) / 10.f, 0, 1);
            h += 9.0f * m * m * inland * (0.6f + 0.4f*n3);
            if (fabsf(lat) > 66) h *= 0.6f;
        } else {
            float dd = minf(-d, 30) / 30.f;
            h = -0.25f - 4.0f * powf(dd, 0.7f) - 0.3f*n3;
        }
        heightmap[i] = h;
    }
}

void world_recarve_straits(void) {
    for (unsigned i = 0; i < sizeof(geo_straits)/sizeof(geo_straits[0]); ++i)
        raster_strait(geo_straits[i].pts, geo_straits[i].halfwidth);
    // any water cell left holding a land height (a fold put an island into a carved channel)
    // goes back under the surface, so the sea draws over it and ships can pass
    for (int i = 0; i < LM_W*LM_H; ++i) if (!landmask[i] && heightmap[i] > 0.f) heightmap[i] = -0.4f;
}
void world_recompute_coastdist(void) { compute_coastdist(); }

// Small islands must clear the water. The heightfield ramps land up from the coast over ~5
// cells, so a one-or-two-cell island tops out around 0.25 - under the 0.35 the sea shader
// treats as land - and renders as a drowned platform with a surf ring (the Azores have done
// this quietly all along; the Dalmatian and Aegean chains made it obvious). Any land component
// small enough to live inside that ramp is lifted to a solid island height; mainland beaches
// keep their low fringe, which is what makes a shoreline read.
#define ISLAND_MAX_CELLS 80
#define ISLAND_MIN_H     0.55f
static void raise_small_islands(void) {
    int N = LM_W * LM_H, raised = 0, islands = 0;
    int *stack = malloc(N * sizeof(int));
    unsigned char *seen = calloc(N, 1);
    int cells[ISLAND_MAX_CELLS];
    for (int i = 0; i < N; ++i) {
        if (!landmask[i] || seen[i]) continue;
        int sp = 0, n = 0;
        stack[sp++] = i; seen[i] = 1;
        while (sp) {                         // flood the whole component; record only the first few cells
            int c = stack[--sp], x = c % LM_W, y = c / LM_W;
            if (n < ISLAND_MAX_CELLS) cells[n] = c;
            n++;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                int xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= LM_W || yy >= LM_H) continue;
                int j = yy * LM_W + xx;
                if (landmask[j] && !seen[j]) { seen[j] = 1; stack[sp++] = j; }
            }
        }
        if (n > ISLAND_MAX_CELLS) continue;  // a continent keeps its beach ramp
        islands++;
        for (int k = 0; k < n; ++k)
            if (heightmap[cells[k]] < ISLAND_MIN_H) { heightmap[cells[k]] = ISLAND_MIN_H; raised++; }
    }
    free(seen); free(stack);
    LOG_INFO("world", "%d small islands lifted clear of the water (%d cells)", islands, raised);
}

float world_height(float wx, float wz) {
    float fx = (x2lon(wx) - LON0) * CELLS_PER_DEG - 0.5f, fy = (LAT1 - z2lat(wz)) * CELLS_PER_DEG - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int xa = mini(maxi(x0,0),LM_W-1), xb = mini(maxi(x0+1,0),LM_W-1), ya = mini(maxi(y0,0),LM_H-1), yb = mini(maxi(y0+1,0),LM_H-1);
    float a = heightmap[ya*LM_W+xa], b = heightmap[ya*LM_W+xb], c = heightmap[yb*LM_W+xa], d = heightmap[yb*LM_W+xb];
    return lerpf(lerpf(a,b,tx), lerpf(c,d,tx), ty);
}
int world_is_land(float wx, float wz) {
    int cx = lon2cx(x2lon(wx)), cy = lat2cy(z2lat(wz));
    if (cx < 0 || cy < 0 || cx >= LM_W || cy >= LM_H) return 1;
    return landmask[cy*LM_W+cx];
}

// ---------------------------------------------------------------------------
// territory (for the map view): nation of the nearest settled port within its influence

void world_compute_territory(void) {
    for (int ty = 0; ty < TERR_H; ++ty) for (int tx = 0; tx < TERR_W; ++tx) {
        int mx = tx*TERR_DIV + TERR_DIV/2, my = ty*TERR_DIV + TERR_DIV/2, i = ty*TERR_W+tx;
        territory[i] = 0;
        if (!landmask[my*LM_W+mx]) continue;
        float lon = cx2lon(mx), lat = cy2lat(my), best = 1e9f; int bn = 0;
        for (int p = 0; p < G.nports; ++p) {
            const port_t *pt = &G.ports[p]; if (pt->size <= 0) continue;
            float r = 1.2f + 1.1f * pt->size + pt->building[B_FORT]*0.4f;
            float dx = (pt->lon - lon) * cosf(lat*0.01745f), dy = pt->lat - lat, d = sqrtf(dx*dx+dy*dy);
            if (d < r && d < best) { best = d; bn = pt->nation + 1; }
        }
        territory[i] = (unsigned char)bn;
    }
    territory_dirty = 2;
}

// ---------------------------------------------------------------------------

void world_init(unsigned seed) {
    double t0 = diag_now();
    if ((int)((LON1-LON0)*CELLS_PER_DEG) != LM_W || (int)((LAT1-LAT0)*CELLS_PER_DEG) != LM_H) LOG_FATAL("world", "LM_W/LM_H (%d,%d) must match the world extents (%d,%d)", LM_W, LM_H, (int)((LON1-LON0)*CELLS_PER_DEG), (int)((LAT1-LAT0)*CELLS_PER_DEG));
    landmask  = calloc(LM_W*LM_H, 1);
    coastdist = calloc(LM_W*LM_H, sizeof(short));
    heightmap = calloc(LM_W*LM_H, sizeof(float));
    territory = calloc(TERR_W*TERR_H, 1);
    for (unsigned i = 0; i < sizeof(geo_land)/sizeof(geo_land[0]); ++i)  raster_polygon(geo_land[i], 1);
    for (unsigned i = 0; i < sizeof(geo_water)/sizeof(geo_water[0]); ++i) raster_polygon(geo_water[i], 0);
    for (unsigned i = 0; i < sizeof(geo_straits)/sizeof(geo_straits[0]); ++i) raster_strait(geo_straits[i].pts, geo_straits[i].halfwidth);
    compute_coastdist();
    compute_heightmap(seed);
    raise_small_islands();
    int land = 0; for (int i = 0; i < LM_W*LM_H; ++i) land += landmask[i];
    LOG_INFO("world", "world %dx%d rasterized in %.1f ms: %d land polygons, %d water bodies, %d straits, %.1f%% land", LM_W, LM_H, (diag_now()-t0)*1000, (int)(sizeof(geo_land)/sizeof(geo_land[0])), (int)(sizeof(geo_water)/sizeof(geo_water[0])), (int)(sizeof(geo_straits)/sizeof(geo_straits[0])), 100.0*land/(LM_W*LM_H));
}
