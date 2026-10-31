// mare::engine::render::StreamView (see render.h): streaming view (game/docs/DESIGN_STREAMING.md
// tasks 3 and 7): feeds the world streamer with
// the camera, composes the finest resident tiles into one R32F "detail window" the terrain
// shader adds on top of the base heightmap, marks tiles that have authored meshes, draws the
// tile grid as a debug overlay (the list of grids still to model, in place, in the world), and
// publishes the streaming metrics.
//
// The window is composed with jobs_parallel_for on the WORKER lane from the main thread, so it
// never races a tile eviction (evictions only happen inside stream_update, also on the main
// thread); at 512x512 it costs well under a millisecond across the pool and is only rebuilt
// when the residency changes or the camera leaves the inner half of the window.
#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdlib>
#include <cmath>

extern "C" {
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "lod.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../world/world.h"
#include "../world/stream.h"
#include "../world/terrain_chunks.h"
#include "../world/rivers.h"
#include "../world/roads.h"
#include "terrain_atlas.h"
#include "../assets/asset.h"
}

#define SV_RES     512          // the near ring's texels per edge (and the fill grain)
#define SV_RES_FAR 1024         // the far ring's: at 320 units that is 0.31 units a texel, three per land-mask cell, enough to carry the shore profile (0.22)
#define SV_MAX_SLOTS 64         // worker slots the tally array covers (jobs_slot_count())
#define SV_MIN_RECOMPOSE_S 0.20     // residency bursts (a tile a frame) fold into one rebuild

// Two clipmap rings (DESIGN_STREAMING.md follow-up 3). The near window carries the tactical
// (L3) detail around the camera; the far ring carries the coastal (L2) detail out to the
// regional view, so pulling back no longer drops every bit of relief at the window's edge.
// Each extent is set by Nyquist against its level's detail frequency in stream.c: L3 runs at
// 6 cycles/degree (wavelength 0.67 world units, needs <= 0.33 per texel), L2 at 3 (1.33 units,
// needs <= 0.67). At 512 texels that is 80 and 320 world units.
#define SV_NEAR_EXTENT  80.f        // 0.156 world units per texel
#define SV_FAR_EXTENT  320.f        // 0.625 world units per texel
enum { SV_NEAR = 0, SV_FAR, SV_RINGS };

int stream_show_tiles;               // dashboard toggle: draw the tile grid overlay

namespace mare::engine::render {

class StreamView {
public:
    void update();
    void push_tiles();
    void draw_debug();
    const rhi_texture_t *detail_tex(int ring);
    const rhi_texture_t *shore_tex(int ring);
    const rhi_texture_t *river_tex(int ring);
    void   detail_rect(int ring, float out[4]);
    float  detail_texel(int ring);
    int    rings() { return SV_RINGS; }
    double window_ms() { return sv_window_ms; }
    int    unmodeled() { return sv_tiles_unmodeled; }

private:
    typedef struct sv_ring_t {
        rhi_texture_t tex;
        rhi_texture_t shore_tex;         // the shore mask (0.22): R cliff, G beach, B marsh, A exposure
        rhi_texture_t river_tex;         // the rivers (0.22): R water, G/B flow direction, A width
        float   *buf;
        unsigned char *shore;            // RGBA8, same grid
        unsigned char *river;            // RGBA8, same grid
        int      res;                    // texels per edge
        float    extent;                 // world units covered
        int      max_level;              // finest tile level this ring may sample (Nyquist, above)
        float    x0, z0;                 // origin (world)
        int      on;
        unsigned seen_residency;
        double   last_compose;
    } sv_ring_t;
    sv_ring_t sv_ring[SV_RINGS];

    int    sv_inited = 0;
    unsigned sv_seen_assets = 0;
    unsigned sv_world_seed = 0xFFFFFFFFu;
    double sv_window_ms = 0;          // both rings together, last rebuild
    int    sv_tiles_unmodeled = 0;    // wanted land leaves without an authored mesh (dashboard)

    // one row band of a ring; reads tile payloads which only ever appear (never vanish) while the
    // main thread sits in the parallel_for below
    typedef struct sv_fill_t { float *buf; unsigned char *shore; unsigned char *river; const river_grid_t *rg; const road_grid_t *rdg; int res; float x0, z0, step; int max_level;
                               float port_xz[96][3]; int nports;   // land anchor x, z and the wall radius to keep (the harbours inside the ring: coast polish keeps clear of them)
                               int src[SV_MAX_SLOTS][3]; } sv_fill_t;   // per worker slot: texels answered by a chunk, by a chunk with maps, cliff texels (no atomics: the bands are disjoint)
    static void sv_fill_rows(void *arg, int begin, int end, int slot);
    double sv_ring_compose(sv_ring_t *r, float cx, float cz);
    void sv_sync_authored();
};

StreamView &stream_view() { static StreamView s; return s; }

// Coast shaping (0.19 band, 0.22 profile - see sv_fill_rows). The base heightfield is a
// 16-cells-per-degree raster, so its waterline is a 7 km stair; the ring reshapes the ground
// within two and a half cells of the coastline as a smooth profile of the distance to it,
// fading out at the harbours. The simulation never reads the rings: presentation only.
// Towns sample the base heightfield for their ground (gfx/scene.cc town_build), so under a
// town's footprint the ring must leave the ground alone or the houses float or sink. The
// footprint is the fort wall: rad 0.55 + 0.3 x size around the *land* anchor, +0.35 for the
// wall (0.22: it used to be a 1.6 .. 3.2 unit disc around the harbour water, which also
// froze every island within twenty kilometres of a port - Malta is Valletta - as a box).
#define SV_PORT_FADE_W  0.6f     // world units over which the profile comes back outside the wall
// The coastal fringe (cells of coast distance) where the authored relief *replaces* the
// procedural one instead of only raising it: the 0.17 cliff field lifts the first inland
// cells into a wall wherever it is high, and GEBCO knows which coasts are really cliffs.
#define SV_FRINGE_IN    1.5f
#define SV_FRINGE_OUT   5.0f

static inline float smoothstep_(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f); return t * t * (3.f - 2.f * t); }

void StreamView::sv_fill_rows(void *arg, int begin, int end, int slot) {
    sv_fill_t *f = (sv_fill_t *)arg;
    const float cell = WORLD_K / CELLS_PER_DEG;
    int src_chunk = 0, src_maps = 0, src_cliff = 0;   // which source answered, tallied per band
    for (int j = begin; j < end; ++j) for (int i = 0; i < f->res; ++i) {
        float wx = f->x0 + i * f->step, wz = f->z0 + j * f->step, h = 0.f;
        float lon = x2lon(wx), lat = z2lat(wz);
        float base = world_height(wx, wz);
        float sd = stream_delta_at_level(wx, wz, f->max_level);
        // The authored Earth where its chunk is resident - the delta is what it says the ground
        // should be, minus what the base map already draws - and the procedural detail
        // elsewhere, so a ring is never empty while chunks are still coming in. The export's
        // coastline disagrees with the base in places, so the delta is clamped to the base's
        // side of the waterline: relief and bathymetry, never a landform rising through the
        // sea or a hole opening in a coast the ports were placed against. geochunks answers
        // from the finest resident dataset (a regional build before the overview).
        gc_sample_t gs;
        int fine = geochunks_sample(lon, lat, &gs);
        if (fine) { h = gs.height; src_chunk++; if (gs.has_maps) src_maps++; }
        float d;
        if (fine) {
            // the authored map's broad relief carries the synthesized ridges on top (0.17): the
            // source's land band is compressed and smooth, the ridges are what makes it a range
            // and, as in the bake, the authored map may only raise land and only deepen sea: a
            // loose coastline neither flattens a hill nor lifts a sea floor to the surface
            d = (base > 0.f ? maxf(h, base) : minf(h, base)) - base + 0.7f * sd;
        } else
            d = sd;
        // how far this texel is from a harbour: 0 at the quay, 1 beyond SV_PORT_FADE
        float portw = 1.f;
        for (int p = 0; p < f->nports && portw > 0.f; ++p) {
            float dp = dist2d(wx, wz, f->port_xz[p][0], f->port_xz[p][1]);
            float u = (dp - f->port_xz[p][2]) / SV_PORT_FADE_W;
            portw *= u <= 0.f ? 0.f : u >= 1.f ? 1.f : u * u * (3.f - 2.f * u);
        }
        // Signed distance to the coast in cells, bilinear over the land-mask cells so it is
        // continuous: +land, -sea, zero exactly on the base map's shoreline. Everything below
        // is a function of this one number, which is what makes the shore smooth - the raster
        // only ever enters as *where* the line is, never as the shape of the ground beside it.
        // Its gradient is the inland direction, which is where the land cover is read from:
        // the chunk sample under a shoreline texel is as likely to say "ocean" as "sand".
        float dcs = 1e9f, gxd = 0.f, gzd = 0.f;
        {
            float cxf = (lon - LON0) * CELLS_PER_DEG - 0.5f, cyf = (LAT1 - lat) * CELLS_PER_DEG - 0.5f;
            int cx0 = (int)floorf(cxf), cy0 = (int)floorf(cyf);
            if (cx0 >= 2 && cy0 >= 2 && cx0 + 3 < LM_W && cy0 + 3 < LM_H) {
                float tx = cxf - cx0, ty = cyf - cy0;
                float d00 = coastdist_f[cy0 * LM_W + cx0],       d10 = coastdist_f[cy0 * LM_W + cx0 + 1];
                float d01 = coastdist_f[(cy0 + 1) * LM_W + cx0], d11 = coastdist_f[(cy0 + 1) * LM_W + cx0 + 1];
                dcs = lerpf(lerpf(d00, d10, tx), lerpf(d01, d11, tx), ty);
                gxd = lerpf(d10 - d00, d11 - d01, ty);           // d(dcs)/d(cell x): +x is east, +cell y is south = +world z
                gzd = lerpf(d01 - d00, d11 - d10, tx);
                // Rounding the raster's corners (0.22). The zero line of the distance field is
                // the land mask's cell contour, so a small island is a box with a beach round
                // it. A box-filtered copy of the field (3 x 3 taps, 1.2 cells apart) is smaller
                // at a convex land corner and larger in a concave bay; taking the *minimum* of
                // the two cuts the corners off the land and leaves every bay as it is - the
                // coast only ever recedes, by under half a cell, so no water the navigation
                // grid sails becomes visual land and no harbour moves (they are protected
                // below anyway). The simulation never sees this: it is the drawn shore.
                float acc = 0.f;
                for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                    float sx = cxf + ox * 1.2f, sy = cyf + oy * 1.2f;
                    int ix = (int)floorf(sx), iy = (int)floorf(sy); float fx2 = sx - ix, fy2 = sy - iy;
                    const float *r0 = coastdist_f + (size_t)iy * LM_W + ix, *r1 = r0 + LM_W;
                    acc += lerpf(lerpf(r0[0], r0[1], fx2), lerpf(r1[0], r1[1], fx2), fy2);
                }
                float blurred = acc / 9.f;
                if (blurred < dcs) dcs = blurred;
            }
        }
        float adc = fabsf(dcs);
        unsigned char shore[4] = { 0, 0, 0, 0 };
        // The coastal fringe (1.5 .. 5 cells inland): the authored relief *replaces* the
        // procedural cliff field instead of only raising it, so a shore the data says is gentle
        // is not walled by 0.17's cliff noise.
        if (fine && portw > 0.001f && dcs > 0.f && adc < SV_FRINGE_OUT) {
            float u = (adc - SV_FRINGE_IN) / (SV_FRINGE_OUT - SV_FRINGE_IN);
            float wc = (1.f - (u <= 0.f ? 0.f : u >= 1.f ? 1.f : u * u * (3.f - 2.f * u))) * portw;
            if (wc > 0.001f) d = lerpf(d, maxf(h, 0.20f) - base + 0.4f * sd, wc);
        }
        // The shore itself (0.22): a *profile* of the distance to the coastline, shaped by what
        // the chunk data says the coast is.
        //
        // The base heightfield steps from +0.12 to -0.30 across one cell at the waterline - a
        // seven-kilometre stair the camera reads as a wall - and small islands are lifted to a
        // flat 0.55 so they clear the water. Inside two and a half cells of the line the ground
        // is reshaped instead:
        //   * a beach coast: wadeable water (-0.08 at the line) deepening to the shelf, and sand
        //     rising gently to ~0.22 before the real relief returns; the strand is wide where
        //     the export's splat is sandy and the ground gentle, narrow where it is not, and it
        //     curves because it is a function of the Euclidean coast distance, not the raster;
        //   * a cliff coast: where the export's normal is steep or its weights are rock at the
        //     shoreline (the 0.17 cliff noise only where no data answers): the face rises inside
        //     the first cell to a shoulder, then the relief, and the water off it drops
        //     steeply - Madeira, not Normandy;
        //   * a marsh coast (wetland / mangrove classes): nearly flat on both sides, mud under a
        //     hand of water running far out, no strand and no surf;
        //   * every lifted island gets the beach ring and keeps its plateau behind it.
        // The shoreline position does not move (the profile is zero at dcs = 0, and harbours are
        // protected), so navigation and the ports see exactly the coast they were placed on.
        // Reach: five cells to seaward (the base shelf is six cells deep, so its cell-shaped
        // edge is replaced by the round one), six inland - a lifted island is under ~80 cells
        // and its centre is never more than five from the water, so the whole of it rises as
        // one dome from beach ring to top instead of flattening where the band gives up.
        float reach = dcs < 0.f ? 5.0f : 6.0f;
        if (adc < reach && portw > 0.001f) {
            float w = (1.f - smoothstep_(reach * 0.66f, reach, adc)) * portw;
            // Is this a cliff coast? Only where there is high ground to make one of. The cliff
            // noise alone turned the Venetian lagoon into a limestone wall; now it needs land of
            // some height within a cell, and a real mountain coast (the Dinaric shore) is a cliff
            // whatever the noise says - with deep water off it, because that is what a mountain
            // plunging into the sea has.
            float near_land = maxf(maxf(base, world_height(wx + cell, wz)), maxf(world_height(wx - cell, wz), maxf(world_height(wx, wz + cell), world_height(wx, wz - cell))));
            float cliff = world_fbm(lon * 0.7f + 55.f, lat * 0.7f + 5.f, 3, G.seed + 77);   // world.c compute_heightmap's field
            float cl = clampf((cliff - 0.50f) / 0.22f, 0.f, 1.f) * smoothstep_(0.35f, 0.9f, near_land);
            cl = maxf(cl, smoothstep_(1.2f, 2.6f, near_land));
            float sandy = 0.5f, marsh = 0.f;
            // the data's say: read one cell inland of the texel (the shoreline sample itself is
            // often the ocean class), the slope from the export's normal, the weights, the class
            gc_sample_t gl;
            float glen = sqrtf(gxd * gxd + gzd * gzd);
            float ilon = lon, ilat = lat;
            if (glen > 1e-5f) { ilon += gxd / glen * 1.0f / CELLS_PER_DEG; ilat -= gzd / glen * 1.0f / CELLS_PER_DEG; }
            if (geochunks_sample(ilon, ilat, &gl) && gl.has_maps) {
                float sl = sqrtf(gl.normal[0] * gl.normal[0] + gl.normal[2] * gl.normal[2]) / maxf(gl.normal[1], 0.05f);   // rise over run, drawn world
                float cl_data = clampf(smoothstep_(0.10f, 0.40f, sl) * 0.85f + gl.splat[1] * 0.45f, 0.f, 1.f) * smoothstep_(0.25f, 0.8f, maxf(near_land, gl.height));
                int b = gl.biome;
                marsh = (b == GC_BIOME_WETLAND || b == GC_BIOME_MANGROVE) ? 1.f : 0.f;
                if (b == GC_BIOME_WATER && gl.height < 0.25f) marsh = 0.6f;        // a lagoon or a delta lake behind the line
                sandy = clampf(gl.splat[2] * 1.2f + (1.f - smoothstep_(0.02f, 0.12f, sl)) * 0.45f, 0.f, 1.f) * (1.f - cl_data);
                // the world package classifies the coast itself (coast.r8: 1 sand beach, 2 rock
                // cliff, 3 rocky coast, 4 coastal water); where it has an opinion it decides,
                // the slope and the weights only shade it. Read at the texel too: the inland
                // sample may already be "not coast".
                gc_sample_t gh; int cc = gl.coast;
                if (!cc && geochunks_sample(lon, lat, &gh)) cc = gh.coast;
                if (cc == 1)      { sandy = maxf(sandy, 0.85f); cl_data *= 0.25f; }
                else if (cc == 2) { cl_data = maxf(cl_data, 0.9f); sandy *= 0.1f; }
                else if (cc == 3) { cl_data = maxf(cl_data, 0.55f); sandy *= 0.3f; }
                // the noise field keeps a voice only where the data is undecided
                cl = maxf(cl * (1.f - smoothstep_(0.05f, 0.3f, sandy)) * 0.5f, cl_data);
                cl *= 1.f - marsh;
            }
            if (cl > 0.5f) src_cliff++;
            // the contour wiggle: shortest wavelength four ring texels (finer aliases into a
            // sawtooth); weaker on cliffs, which are straighter than beaches
            float wf = minf(40.f, 1.f / (8.f * f->step / WORLD_K));
            float wig = (world_fbm(lon * wf + 3.f, lat * wf + 11.f, 2, 0x5A17u) - 0.5f) * 0.12f * (1.f - 0.5f * cl) * (1.f - 0.7f * marsh);
            // the strand's width: a sandy gentle coast runs its beach 1.6x as far, a steep one half
            float bw = lerpf(0.55f, 1.6f, sandy);
            float target;
            if (dcs < 0.f) {
                // the sea floor: wadeable at the line, a gentle sandy shelf, then the drop to
                // the base's shelf depth by five cells; off a cliff the bottom falls away at once
                float sn = -dcs;
                float shelf = (0.06f + 0.24f * cl) + (0.30f + 0.9f * cl) * (1.f - expf(-sn * (0.55f + 1.4f * cl) / (0.6f + 0.4f * bw)));
                float drop  = 1.1f * smoothstep_(2.5f, 5.0f, sn);          // SHELF_DEPTH, where the base takes over
                target = -(shelf + drop);
                // where the authored bathymetry is deeper, let it take over away from the line
                if (fine && h < target) target = lerpf(target, h, smoothstep_(1.0f, 4.5f, sn));
                // the marsh: mud under a hand of water, out to the shelf edge
                float mud = -(0.03f + 0.14f * (1.f - expf(-sn * 0.45f)) + drop);
                target = lerpf(target, mud, marsh);
            } else {
                float relief = fine ? maxf(h, base) : base;
                // a beach coast: sand ramping up, then the relief returning as a *dome* - the
                // ground rises as the square root of the distance to the water, so a lifted
                // island (a flat 0.55 inside its coast) comes up as a rounded hill, and a
                // mainland shore has a gentle approach instead of a step
                float beach  = 0.04f + 0.30f * (1.f - expf(-dcs * 0.75f / bw));
                float dome   = relief * sqrtf(smoothstep_(0.0f, reach, dcs));
                float gentle = maxf(beach, dome);
                // a cliff coast: a steep face in the first cell and a half, rising only as high as
                // the land behind it allows (plus a lip), then a rounded shoulder into the relief
                float face   = 0.05f + minf(2.2f, relief + 0.35f) * smoothstep_(0.05f, 1.4f, dcs);
                float steep  = lerpf(face, maxf(face, relief), smoothstep_(1.2f, reach, dcs));
                target = lerpf(gentle, steep, cl);
                // the marsh: a few metres above the water for a long way inland
                float flat = 0.03f + 0.10f * (1.f - expf(-dcs * 0.5f)) + relief * smoothstep_(2.5f, reach, dcs);
                target = lerpf(target, flat, marsh);
            }
            target += wig;
            d = lerpf(d, target - base, w);
            // the shore mask the shaders read: what kind of coast this is, fading with the
            // profile's own weight so it is zero wherever the ground is left alone
            shore[0] = (unsigned char)(255.f * clampf(cl * w, 0.f, 1.f));
            shore[1] = (unsigned char)(255.f * clampf(sandy * (1.f - cl) * (1.f - marsh) * w, 0.f, 1.f));
            shore[2] = (unsigned char)(255.f * clampf(marsh * w, 0.f, 1.f));
            shore[3] = (unsigned char)(255.f * clampf(portw * (1.f - marsh) * w, 0.f, 1.f));
        }
        // Rivers (world/rivers.h): the channel is cut into the ring - a flat bed hw wide, banks
        // sloping back over another 1.5 hw - and the river texture says where the water is and
        // which way it runs. Deeper for a wider river (0.02 .. 0.06 units: 9 .. 27 m before the
        // world's vertical exaggeration). Not inside the shore band: the mouth meets the sea's
        // own profile there, and the river texture carries the water across it.
        unsigned char river[4] = { 0, 0, 0, 0 };
        if (f->rg && dcs > -1.5f) {
            float rd = 0, rhw = 0, rdx = 1, rdz = 0; int rcls = 0;
            if (rivers_grid_query(f->rg, wx, wz, &rd, &rhw, &rdx, &rdz, &rcls)) {
                // a stream narrower than a ring texel falls between the samples: it is drawn at
                // 0.6 of a texel and faded by how much narrower it really is
                float rw = maxf(rhw, 0.6f * f->step);
                if (rd < rw * 2.5f) {
                    float wk = portw * smoothstep_(0.0f, 1.2f, dcs);   // fades into the shore profile at the mouth
                    // A navigable reach is cut as a real channel: the bed goes *below sea level*,
                    // so the sea pass fills it and a hull floats in it - the Thames at London,
                    // the Danube at Belgrade, the Sava at Zagreb. The same river higher up is
                    // only painted on the ground: a bed forced down to sea level through a
                    // mountain would be a canyon, not a river.
                    float lowland = rcls ? 1.f - smoothstep_(1.3f, 2.6f, base) : 0.f;
                    if (lowland > 0.01f) {
                        float bed  = rcls == 1 ? -0.34f : -0.20f;       // deep enough for the draught the class carries
                        float bank = smoothstep_(0.72f * rw, 2.3f * rw, rd);
                        float target = lerpf(bed, maxf(base, 0.05f), bank);
                        d = lerpf(d, target - base, wk * lowland);
                    }
                    // Canyons (0.22.2). Where a sizeable river runs through high ground it has cut a
                    // gorge: a flat floor with the stream on it, walls that rise steeply within a
                    // quarter width, deeper the higher the land stands. The walls are written into
                    // the shore mask's cliff channel, so terrain.frag paints them as bare rock with
                    // scree at the foot - the same material the sea cliffs use. Small streams keep
                    // their shallow bed: a brook does not carve a canyon.
                    float hill  = rcls ? 1.f - lowland : smoothstep_(1.0f, 2.0f, base);
                    float gorge = hill * smoothstep_(0.04f, 0.22f, rhw) * wk;
                    if (gorge > 0.01f) {
                        float depth = 0.28f + 0.42f * clampf(base - 0.9f, 0.f, 2.f);
                        float wall  = smoothstep_(0.85f * rw, 1.30f * rw, rd);
                        d -= depth * (1.f - wall) * gorge;
                        float rock = (1.f - smoothstep_(1.7f * rw, 2.5f * rw, rd)) * smoothstep_(0.35f * rw, 0.9f * rw, rd) * gorge;
                        unsigned char rc = (unsigned char)(255.f * clampf(rock, 0.f, 1.f));
                        if (rc > shore[0]) shore[0] = rc;
                    }
                    if (lowland < 0.99f) {   // the painted stream: a shallow bed with water drawn on it
                        float k = 1.f - lowland;
                        float bankp = clampf((rd - rw) / (1.5f * rw), 0.f, 1.f);
                        float cut = (0.02f + 0.6f * rhw) * (1.f - bankp * bankp * (3.f - 2.f * bankp));
                        d -= cut * wk * k;
                        float water = (1.f - smoothstep_(0.80f, 1.10f, rd / rw)) * clampf(rhw / (0.35f * f->step), 0.22f, 1.f) * k;
                        river[0] = (unsigned char)(255.f * clampf(water * (dcs > -1.0f ? 1.f : 0.f) * portw, 0.f, 1.f));
                    }
                    river[1] = (unsigned char)(127.5f + 127.f * rdx); river[2] = (unsigned char)(127.5f + 127.f * rdz);
                }
            }
        }
        // Roads (world/roads.h) share the texture: its A channel is how much of a road this
        // texel is. A track is a hairline, drawn at half a texel so it survives the sampling,
        // and the class brightens it; nothing is carved - a cart road follows the ground.
        if (f->rdg && dcs > 0.2f) {
            float rd, rhw; int rcls;
            if (roads_grid_query(f->rdg, wx, wz, &rd, &rhw, &rcls)) {
                float rw = maxf(rhw, 0.6f * f->step);
                float road = (1.f - smoothstep_(0.7f * rw, 1.2f * rw, rd)) * (0.75f + 0.125f * rcls) * portw;
                river[3] = (unsigned char)(255.f * clampf(road, 0.f, 1.f));
            }
        }
        f->buf[j * f->res + i] = d;
        memcpy(f->shore + ((size_t)j * f->res + i) * 4, shore, 4);
        memcpy(f->river + ((size_t)j * f->res + i) * 4, river, 4);
    }
    // a band's own tally, folded in once (the rows are disjoint, so this is a plain add per
    // worker rather than a contended counter per texel)
    int k = (unsigned)slot < SV_MAX_SLOTS ? slot : 0;
    f->src[k][0] += src_chunk; f->src[k][1] += src_maps; f->src[k][2] += src_cliff;
}

// Rebuild one ring when the residency changed or the camera left its inner half. Returns the
// milliseconds spent (0 when nothing had to be done).
double StreamView::sv_ring_compose(sv_ring_t *r, float cx, float cz) {
    int moved = !r->on || fabsf(cx - (r->x0 + r->extent * 0.5f)) > r->extent * 0.25f
                       || fabsf(cz - (r->z0 + r->extent * 0.5f)) > r->extent * 0.25f;
    unsigned resgen = stream_residency_generation() ^ (geochunks_generation() * 2246822519u);
    int changed = r->seen_residency != resgen;
    if (!(moved || changed) || RF.time - r->last_compose <= SV_MIN_RECOMPOSE_S) return 0;
    r->last_compose = RF.time;
    r->seen_residency = resgen;
    double t0 = diag_now();
    float step = r->extent / (r->res - 1);
    sv_fill_t f;                                 // snapped to the sample grid so detail never swims
    f.buf = r->buf; f.shore = r->shore; f.river = r->river; f.res = r->res;
    f.x0 = floorf((cx - r->extent * 0.5f) / step) * step;
    f.z0 = floorf((cz - r->extent * 0.5f) / step) * step;
    f.step = step; f.max_level = r->max_level;
    // the river segments over this ring, binned for the workers (after the origin is known)
    river_grid_t rg; rivers_grid_build(&rg, f.x0 - 1.f, f.z0 - 1.f, r->extent + 2.f, 1.0f); f.rg = rg.nidx ? &rg : nullptr;
    road_grid_t rdg; roads_grid_build(&rdg, f.x0 - 1.f, f.z0 - 1.f, r->extent + 2.f, 1.0f); f.rdg = rdg.nidx ? &rdg : nullptr;
    memset(f.src, 0, sizeof f.src);
    f.nports = 0;   // every harbour the ring touches: the shoreline stays as placed around it
    for (int i = 0; i < G.nports && f.nports < 96; ++i) {
        const port_t *p = &G.ports[i];
        if (p->size <= 0 && !p->lx && !p->lz) continue;
        float keep = (0.55f + 0.3f * (p->size > 0 ? p->size : 0)) + 0.35f, margin = keep + SV_PORT_FADE_W;
        float px = p->lx ? p->lx : p->x, pz = p->lz ? p->lz : p->z;    // the town stands on the land anchor
        if (px < f.x0 - margin || px > f.x0 + r->extent + margin || pz < f.z0 - margin || pz > f.z0 + r->extent + margin) continue;
        f.port_xz[f.nports][0] = px; f.port_xz[f.nports][1] = pz; f.port_xz[f.nports][2] = keep; f.nports++;
    }
    jobs_parallel_for(JOB_LANE_WORKER, sv_fill_rows, &f, r->res, 32);
    {   // what the ring holds: the range of the delta and how much of it the shore work touched
        float mn = 1e9f, mx = -1e9f; int big = 0, nriver = 0;
        for (int i = 0; i < r->res * r->res; ++i) { float v = r->buf[i]; mn = minf(mn, v); mx = maxf(mx, v); big += v < -0.5f; nriver += r->river[i * 4] > 64; }
        int sf = 0, sc = 0, su = 0;
        for (int k = 0; k < SV_MAX_SLOTS; ++k) { sf += f.src[k][0]; sc += f.src[k][1]; su += f.src[k][2]; }
        LOG_DEBUG("stream", "ring %s composed at %.1f,%.1f: delta %.2f..%.2f, %d texels lowered over 0.5, %d harbours; %d texels from the chunks (%d with maps), %d cliff-shore texels, %d river texels from %d segments",
                  r->extent < 100.f ? "near" : "far", f.x0, f.z0, mn, mx, big, f.nports, sf, sc, su, nriver, rg.nidx);
    }
    LOG_DEBUG("stream", "ring %s rivers: %d segments in the grid", r->extent < 100.f ? "near" : "far", rg.nidx);
    { int nr = 0; for (int i = 0; i < r->res * r->res; ++i) nr += r->river[i * 4] > 64; LOG_DEBUG("stream", "ring %s rivers: %d water texels", r->extent < 100.f ? "near" : "far", nr); }
    rivers_grid_free(&rg); roads_grid_free(&rdg);
    rhi_texture_update(&r->tex, r->buf);
    rhi_texture_update(&r->shore_tex, r->shore);
    rhi_texture_update(&r->river_tex, r->river);
    r->x0 = f.x0; r->z0 = f.z0; r->on = 1;
    return (diag_now() - t0) * 1000;
}

void StreamView::sv_sync_authored() {
    char name[64];
    sv_tiles_unmodeled = 0;
    for (int i = 0; i < stream_tile_count(); ++i) {
        stream_tile_t *t = stream_tile_at_index(i);
        if (t->level < STREAM_L_COASTAL || t->water_only) continue;
        snprintf(name, sizeof name, "tile_l%d_%d_%d", t->level, t->tx, t->ty);
        stream_set_authored(t->level, t->tx, t->ty, asset_find(name) != NULL);
    }
}

void StreamView::update() {
    if (!sv_inited) {
        sv_inited = 1;
        static const float extent[SV_RINGS] = { SV_NEAR_EXTENT, SV_FAR_EXTENT };
        static const int   lvl[SV_RINGS]    = { STREAM_L_TACTICAL, STREAM_L_COASTAL };
        static const int   res[SV_RINGS]    = { SV_RES, SV_RES_FAR };
        for (int i = 0; i < SV_RINGS; ++i) {
            sv_ring[i].res = res[i];
            sv_ring[i].buf = (float *)malloc((size_t)res[i] * res[i] * sizeof(float));
            sv_ring[i].tex = rhi_texture_make(res[i], res[i], RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
            sv_ring[i].shore = (unsigned char *)calloc((size_t)res[i] * res[i] * 4, 1);
            sv_ring[i].shore_tex = rhi_texture_make(res[i], res[i], RHI_FMT_RGBA8, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, sv_ring[i].shore);
            sv_ring[i].river = (unsigned char *)calloc((size_t)res[i] * res[i] * 4, 1);
            sv_ring[i].river_tex = rhi_texture_make(res[i], res[i], RHI_FMT_RGBA8, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, sv_ring[i].river);
            sv_ring[i].extent = extent[i]; sv_ring[i].max_level = lvl[i]; sv_ring[i].last_compose = -1e9;
        }
        const char *e = getenv("MARE_STREAM_TILES"); if (e) stream_show_tiles = atoi(e) != 0;   // overlay on for unattended captures
    }
    if (sv_world_seed != G.seed) {   // new game or a loaded save: detail is keyed to the world seed
        sv_world_seed = G.seed;
        stream_init(G.seed, "tiles");
        sv_seen_assets = (unsigned)-1;
        for (int i = 0; i < SV_RINGS; ++i) { sv_ring[i].on = 0; sv_ring[i].seen_residency = 0; }
    }
    stream_update(cam.tx, cam.tz, cam.dist);
    // The road network needs the ports and the inland settlements, which exist only once the
    // game is set up, and its classes follow the trade - so it is built here, on the first
    // frame they are there, and again every ROAD_REBUILD_DAYS (the rings recompose on the
    // residency generation, so a rebuild bumps it)
    if (G.nports > 0 && (roads_last_build_day() < -1e8 || G.day - roads_last_build_day() > ROAD_REBUILD_DAYS || roads_last_build_day() > G.day + 1)) {
        roads_generate(); for (int i = 0; i < SV_RINGS; ++i) sv_ring[i].seen_residency = 0;
    }
    // Page the terrain chunks around the look-at point. An overview chunk is 13 degrees
    // across, so one or two cover any view; the radius follows the zoom so the far ring
    // (320 units = 20 degrees) always has its chunks, and a regional build under the camera
    // pages its finer chunks inside the same radius.
    geochunks_update(x2lon(cam.tx), z2lat(cam.tz), clampf(cam.dist / WORLD_K * 0.5f + 1.f, 1.f, 14.f));
    chunkatlas_update();
    if (sv_seen_assets != asset_generation()) { sv_seen_assets = asset_generation(); sv_sync_authored(); }
    sv_tiles_unmodeled = 0;   // wanted land leaves without an authored mesh: the "still to model" list
    for (int i = 0; i < stream_tile_count(); ++i) { const stream_tile_t *t = stream_tile_at_index(i);
        sv_tiles_unmodeled += t->wanted && !t->water_only && !t->authored && t->level == STREAM_L_TACTICAL; }

    // At most one ring is rebuilt per frame (the near one first: it is what the player is
    // looking at), so a residency change never pays for both composes in the same frame.
    { double ms = sv_ring_compose(&sv_ring[SV_NEAR], cam.tx, cam.tz);
      if (ms == 0) ms = sv_ring_compose(&sv_ring[SV_FAR], cam.tx, cam.tz);
      if (ms > 0) sv_window_ms = ms; }

    // metrics (task 7)
    static int m_gc_res = -1, m_gc_mb, m_gc_load, m_gc_ev, m_ca_slots;
    if (m_gc_res < 0) { m_gc_res = metric_id("terrain.chunks_resident"); m_gc_mb = metric_id("terrain.chunks_mb");
                        m_gc_load = metric_id("terrain.chunks_loading"); m_gc_ev = metric_id("terrain.chunks_evicted"); m_ca_slots = metric_id("terrain.atlas_slots"); }
    { gc_stats_t gs; geochunks_stats(&gs);
      metric_set(m_gc_res, gs.resident); metric_set(m_gc_mb, gs.bytes / (1024.0 * 1024.0));
      metric_set(m_gc_load, gs.loading); metric_set(m_gc_ev, gs.evicted); metric_set(m_ca_slots, chunkatlas_resident()); }
    static int m_res = -1, m_want, m_load, m_mb, m_evict, m_disk, m_gen, m_win, m_unmod;
    if (m_res < 0) { m_res = metric_id("stream.resident"); m_want = metric_id("stream.wanted"); m_load = metric_id("stream.loading");
                     m_mb = metric_id("stream.mb"); m_evict = metric_id("stream.evicted"); m_disk = metric_id("stream.disk_hits");
                     m_gen = metric_id("stream.generated"); m_win = metric_id("stream.window_ms"); m_unmod = metric_id("stream.unmodeled"); }
    stream_stats_t s; stream_stats(&s);
    metric_set(m_res, s.resident); metric_set(m_want, s.wanted); metric_set(m_load, s.loading);
    metric_set(m_mb, s.bytes / (1024.0 * 1024.0)); metric_set(m_evict, s.evicted);
    metric_set(m_disk, s.disk_hits); metric_set(m_gen, s.generated); metric_set(m_win, sv_window_ms);
    metric_set(m_unmod, sv_tiles_unmodeled);
}

// terrain.c binds these per ring: the texture, (x0, z0, 1/extent, 1/extent), and the world
// size of one texel (the shader differentiates the delta for its normals).
const rhi_texture_t *StreamView::detail_tex(int ring) { return (unsigned)ring < SV_RINGS && sv_ring[ring].on ? &sv_ring[ring].tex : 0; }
const rhi_texture_t *StreamView::shore_tex(int ring) { return (unsigned)ring < SV_RINGS && sv_ring[ring].on ? &sv_ring[ring].shore_tex : 0; }
const rhi_texture_t *StreamView::river_tex(int ring) { return (unsigned)ring < SV_RINGS && sv_ring[ring].on ? &sv_ring[ring].river_tex : 0; }
void StreamView::detail_rect(int ring, float out[4]) {
    const sv_ring_t *r = &sv_ring[(unsigned)ring < SV_RINGS ? ring : 0];
    out[0] = r->x0; out[1] = r->z0; out[2] = 1.f / r->extent; out[3] = 1.f / r->extent;
}
float  StreamView::detail_texel(int ring) { return (unsigned)ring < SV_RINGS ? sv_ring[ring].extent / (sv_ring[ring].res - 1) : SV_NEAR_EXTENT / (SV_RES - 1); }

// Authored terrain tiles (`tile_l<level>_<tx>_<ty>` assets): drawn through the normal asset
// path. Called inside the scene_assets_begin/end bracket (scene_build_ports).
void StreamView::push_tiles() {
    if (cam.mapness > 0.5f) return;
    for (int i = 0; i < stream_tile_count(); ++i) {
        stream_tile_t *t = stream_tile_at_index(i);
        if (!t->authored || !t->wanted || t->level < STREAM_L_COASTAL) continue;
        float x0, z0, x1, z1; stream_tile_rect(t, &x0, &z0, &x1, &z1);
        float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        float dd = dist2d(cx, cz, cam.pos.x, cam.pos.z), d = sqrtf(dd * dd + cam.pos.y * cam.pos.y);
        if (d > (x1 - x0) * 3.f) continue;
        char name[64]; snprintf(name, sizeof name, "tile_l%d_%d_%d", t->level, t->tx, t->ty);
        asset_t *a = asset_find(name);
        if (!a) continue;
        if (a->state == ASSET_UNLOADED) asset_request(a);
        if (a->state != ASSET_LOADED) continue;
        inst_t in = { cx, 0, cz, 0, 1.f, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        scene_asset_instance(a, in, lod_key(LODK_MISC, i, 1), d);
    }
}

// Debug overlay: the wanted set as frames at the tile bounds - green resident (bright when an
// authored mesh exists), yellow loading, blue on disk, red missing; wanted land leaves without
// an authored mesh get a diagonal cross: the grids still to model, shown in place.
void StreamView::draw_debug() {
    if (!stream_show_tiles || cam.mapness > 0.95f) return;
    for (int i = 0; i < stream_tile_count(); ++i) {
        const stream_tile_t *t = stream_tile_at_index(i);
        if (!t->wanted || t->water_only) continue;
        float x0, z0, x1, z1; stream_tile_rect(t, &x0, &z0, &x1, &z1);
        float lift = 0.4f + 0.15f * (STREAM_LEVELS - 1 - t->level);
        unsigned col = t->state == TILE_READY  ? (t->authored ? rgb(90, 255, 120) : rgb(60, 170, 80))
                     : t->state == TILE_QUEUED ? rgb(230, 200, 60)
                     : t->on_disk              ? rgb(80, 130, 230) : rgb(210, 70, 50);
        unsigned c = rgb_linear(col);
        // each edge in segments that follow the ground, so frames read on hills and over water alike
        int segs = t->level >= STREAM_L_COASTAL ? 8 : 4;
        static const int ex[5] = { 0, 1, 1, 0, 0 }, ez[5] = { 0, 0, 1, 1, 0 };   // corner ring
        for (int e = 0; e < 4; ++e) {
            float ax = ex[e] ? x1 : x0, az = ez[e] ? z1 : z0;
            float bx = ex[e+1] ? x1 : x0, bz = ez[e+1] ? z1 : z0;
            float px = ax, pz = az, py = maxf(world_height(ax, az), 0.f) + lift;
            for (int s = 1; s <= segs; ++s) {
                float qx = ax + (bx - ax) * s / segs, qz = az + (bz - az) * s / segs;
                float qy = maxf(world_height(qx, qz), 0.f) + lift;
                dd_line(vec3(px, py, pz), vec3(qx, qy, qz), dd_opts().with_color(c));
                px = qx; py = qy; pz = qz;
            }
        }
        if (t->level == STREAM_L_TACTICAL && !t->authored) {   // a placeholder where a mesh could go
            unsigned m = rgb_linear(rgb(200, 90, 60));
            float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, cy = maxf(world_height(cx, cz), 0.f) + lift;
            dd_line(vec3(x0, maxf(world_height(x0, z0), 0.f) + lift, z0), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x1, maxf(world_height(x1, z0), 0.f) + lift, z0), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x1, maxf(world_height(x1, z1), 0.f) + lift, z1), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x0, maxf(world_height(x0, z1), 0.f) + lift, z1), vec3(cx, cy, cz), dd_opts().with_color(m));
        }
    }
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
void streamview_update(void) { mare::engine::render::stream_view().update(); }
void streamview_push_tiles(void) { mare::engine::render::stream_view().push_tiles(); }
void streamview_draw_debug(void) { mare::engine::render::stream_view().draw_debug(); }
const rhi_texture_t *streamview_detail_tex(int ring) { return mare::engine::render::stream_view().detail_tex(ring); }
const rhi_texture_t *streamview_shore_tex(int ring) { return mare::engine::render::stream_view().shore_tex(ring); }
const rhi_texture_t *streamview_river_tex(int ring) { return mare::engine::render::stream_view().river_tex(ring); }
void  streamview_detail_rect(int ring, float out[4]) { mare::engine::render::stream_view().detail_rect(ring, out); }
float streamview_detail_texel(int ring) { return mare::engine::render::stream_view().detail_texel(ring); }
int   streamview_rings(void) { return mare::engine::render::stream_view().rings(); }
double streamview_window_ms(void) { return mare::engine::render::stream_view().window_ms(); }
int   streamview_unmodeled(void) { return mare::engine::render::stream_view().unmodeled(); }
}
