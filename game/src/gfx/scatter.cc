// Hinterland props (see scatter.h). Sections: the meshes · placement from the land-use ring ·
// submission. mare::game::view::Scatter behind a C ABI, like the shipyard.
#include "../platform/v3cpp.hh"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_set>

extern "C" {
#include "scatter.h"
#include "gpu.h"
#include "render.h"
#include "camera.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../world/world.h"
}

int scatter_enabled = 1;
int scatter_count = 0;

namespace mare::game::view {

// Sizes sit between the map's scale and the ships': a tree the height of a texel would be a
// pixel from any camera that shows a field, a tree at the ship's scale (a fifth of a hull)
// towered over the towns' houses. So a grove is trees about a house high, spaced TREE_CELL
// apart on the district grid the shader's parcels use, drawn only at the close zooms; the
// painted crowns under them read as undergrowth.
#define TREE_CELL   0.16f        // orchard spacing (world units)
#define TREE_H      0.11f        // tree height (0.22.5: bigger - "increase size of trees")
#define WOOD_H      0.13f        // a forest tree; pines a little taller still
#define WOOD_P      0.62f        // share of forest texels that get a tree (a real forest, not a scatter)
#define HOUSE_W     0.020f       // a village house, wall to wall
#define VINE_H      0.022f       // a vine bush's height; rows follow the contour
#define SHEEP_L     0.018f       // a sheep, nose to tail
#define SCATTER_FAR 16.f         // camera distance past which the small props are not drawn (they are specks by then)
#define HOUSE_FAR   40.f         // ... the houses carry further
#define SCATTER_MAX 160000

class Scatter {
public:
    void rebuild(const unsigned char *lu, const unsigned char *forest, const float *delta, int res, float x0, float z0, float step);
    int  submit();
private:
    void build_meshes();
    static float smoothstep_(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
    static float hash2(float x, float y) { float v = sinf(x * 127.1f + y * 311.7f) * 43758.5453f; return v - floorf(v); }   // terrain.frag's hash()
    static void octa(mb_t *m, float cy, float rxz, float ry, const float *ca, const float *cb);
    static void cone(mb_t *m, float cy, float r, float h, const float *ca, const float *cb);
    mesh_t tree = {}, olive = {}, vine = {}, sheep = {}, pine = {}, oak = {}, house = {}; int built = 0;
    instlist_t li_tree = {}, li_olive = {}, li_vine = {}, li_sheep = {}, li_pine = {}, li_oak = {}, li_house = {};
};

Scatter &scatter() { static Scatter s; return s; }

// an octahedron crown: upper faces in colour `ca`, lower in `cb` (CCW seen from outside)
void Scatter::octa(mb_t *m, float cy, float rxz, float ry, const float *ca, const float *cb) {
    float T[3] = { 0, cy + ry, 0 }, B[3] = { 0, cy - ry, 0 };
    float E[3] = { rxz, cy, 0 }, W[3] = { -rxz, cy, 0 }, N[3] = { 0, cy, -rxz }, S[3] = { 0, cy, rxz };
    #define TRI(a, b, c, col) mb_tri(m, a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2], col[0], col[1], col[2], 0.f)
    TRI(T, S, E, ca); TRI(T, E, N, ca); TRI(T, N, W, ca); TRI(T, W, S, ca);
    TRI(B, E, S, cb); TRI(B, N, E, cb); TRI(B, W, N, cb); TRI(B, S, W, cb);
    #undef TRI
}

// a cone: six sides, lit side `ca`, shaded side `cb` by the normal the builder derives
void Scatter::cone(mb_t *m, float cy, float r, float h, const float *ca, const float *cb) {
    float T[3] = { 0, cy + h, 0 };
    for (int i = 0; i < 6; ++i) {
        float a0 = i * 1.0472f, a1 = (i + 1) * 1.0472f;
        float A[3] = { r * cosf(a0), cy, r * sinf(a0) }, B[3] = { r * cosf(a1), cy, r * sinf(a1) };
        const float *col = (i % 2) ? cb : ca;
        mb_tri(m, T[0], T[1], T[2], B[0], B[1], B[2], A[0], A[1], A[2], col[0], col[1], col[2], 0.f);
    }
}

void Scatter::build_meshes() {
    const float trunk[3] = { 0.36f, 0.26f, 0.16f }, crown_hi[3] = { 0.24f, 0.42f, 0.17f }, crown_lo[3] = { 0.16f, 0.30f, 0.12f };
    const float olive_hi[3] = { 0.50f, 0.56f, 0.40f }, olive_lo[3] = { 0.40f, 0.46f, 0.33f }, gnarl[3] = { 0.42f, 0.36f, 0.28f };
    const float post[3] = { 0.30f, 0.22f, 0.14f }, leaf[3] = { 0.25f, 0.41f, 0.18f };
    const float wool[3] = { 0.90f, 0.88f, 0.82f }, face[3] = { 0.22f, 0.20f, 0.18f };
    {   // a fruit tree: a slim trunk and a tall crown, one unit high
        mb_t m = mb_t{}; mb_material(&m, MB_WOOD);
        mb_box(&m, 0, 0.22f, 0, 0.040f, 0.22f, 0.040f, trunk[0], trunk[1], trunk[2], 0.f);
        mb_material(&m, 0.92f, 0.f);
        octa(&m, 0.70f, 0.40f, 0.34f, crown_hi, crown_lo);
        tree = mesh_upload(&m);
    }
    {   // an olive: a thick short trunk and a broad, flat grey-green crown
        mb_t m = mb_t{}; mb_material(&m, MB_WOOD);
        mb_box(&m, 0, 0.18f, 0, 0.065f, 0.18f, 0.065f, gnarl[0], gnarl[1], gnarl[2], 0.f);
        mb_material(&m, 0.92f, 0.f);
        octa(&m, 0.58f, 0.50f, 0.26f, olive_hi, olive_lo);
        olive = mesh_upload(&m);
    }
    {   // a vine: a post and the bush trained along the row (+x), one unit high
        mb_t m = mb_t{}; mb_material(&m, MB_WOOD);
        mb_box(&m, 0, 0.5f, 0, 0.06f, 0.5f, 0.06f, post[0], post[1], post[2], 0.f);
        mb_material(&m, 0.92f, 0.f);
        mb_box(&m, 0, 0.62f, 0, 1.3f, 0.30f, 0.22f, leaf[0], leaf[1], leaf[2], 0.f);
        vine = mesh_upload(&m);
    }
    {   // a pine (0.22.5): a bare trunk and three stacked cones, dark blue-green
        const float pine_hi[3] = { 0.20f, 0.38f, 0.22f }, pine_lo[3] = { 0.14f, 0.28f, 0.17f };
        mb_t m = mb_t{}; mb_material(&m, MB_WOOD);
        mb_box(&m, 0, 0.20f, 0, 0.030f, 0.20f, 0.030f, trunk[0], trunk[1], trunk[2], 0.f);
        mb_material(&m, 0.92f, 0.f);
        cone(&m, 0.30f, 0.30f, 0.34f, pine_hi, pine_lo);
        cone(&m, 0.50f, 0.24f, 0.30f, pine_hi, pine_lo);
        cone(&m, 0.70f, 0.16f, 0.30f, pine_hi, pine_lo);
        pine = mesh_upload(&m);
    }
    {   // a forest broadleaf (0.22.5): a taller trunk and a broad round crown of two octahedra
        mb_t m = mb_t{}; mb_material(&m, MB_WOOD);
        mb_box(&m, 0, 0.26f, 0, 0.038f, 0.26f, 0.038f, trunk[0], trunk[1], trunk[2], 0.f);
        mb_material(&m, 0.92f, 0.f);
        octa(&m, 0.66f, 0.42f, 0.34f, crown_hi, crown_lo);
        octa(&m, 0.60f, 0.30f, 0.26f, crown_lo, crown_lo);
        oak = mesh_upload(&m);
    }
    {   // a village house (0.22.5): stone walls one unit wide, a tiled roof, ridge along x
        const float wall[3] = { 0.74f, 0.70f, 0.62f }, tile[3] = { 0.62f, 0.36f, 0.26f };
        mb_t m = mb_t{}; mb_material(&m, MB_STONE);
        mb_box(&m, 0, 0.32f, 0, 0.5f, 0.32f, 0.36f, wall[0], wall[1], wall[2], 0.f);
        mb_material(&m, MB_TILE);
        float a[3] = { -0.56f, 0.62f, -0.42f }, b[3] = { 0.56f, 0.62f, -0.42f }, c[3] = { 0.56f, 0.62f, 0.42f }, d[3] = { -0.56f, 0.62f, 0.42f };
        float r0[3] = { -0.56f, 0.92f, 0 }, r1[3] = { 0.56f, 0.92f, 0 };
        mb_quad(&m, a, b, r1, r0, tile[0], tile[1], tile[2], 0.f); mb_quad(&m, d, r0, r1, c, tile[0], tile[1], tile[2], 0.f);
        mb_tri(&m, a[0], a[1], a[2], r0[0], r0[1], r0[2], d[0], d[1], d[2], wall[0], wall[1], wall[2], 0.f);
        mb_tri(&m, b[0], b[1], b[2], c[0], c[1], c[2], r1[0], r1[1], r1[2], wall[0], wall[1], wall[2], 0.f);
        house = mesh_upload(&m);
    }
    {   // a sheep: a woolly body and a dark head, one unit long
        mb_t m = mb_t{}; mb_material(&m, 0.95f, 0.f);
        mb_box(&m, 0, 0.42f, 0, 0.42f, 0.26f, 0.24f, wool[0], wool[1], wool[2], 0.f);
        mb_box(&m, 0.50f, 0.50f, 0, 0.13f, 0.13f, 0.11f, face[0], face[1], face[2], 0.f);
        sheep = mesh_upload(&m);
    }
    built = 1;
}

void Scatter::rebuild(const unsigned char *lu, const unsigned char *forest, const float *delta, int res, float x0, float z0, float step) {
    if (!scatter_enabled) return;
    if (!built) build_meshes();
    instlist_clear(&li_tree); instlist_clear(&li_olive); instlist_clear(&li_vine); instlist_clear(&li_sheep);
    instlist_clear(&li_pine); instlist_clear(&li_oak); instlist_clear(&li_house);
    std::unordered_set<long long> cells;
    double t0 = diag_now();
    int total = 0;
    auto lu_at = [&](float wx, float wz, int ch) -> int {   // nearest texel of the ring
        int i = (int)floorf((wx - x0) / step + 0.5f), j = (int)floorf((wz - z0) / step + 0.5f);
        if (i < 0 || j < 0 || i >= res || j >= res) return 0;
        return lu[((size_t)j * res + i) * 4 + ch];
    };
    auto ground = [&](float wx, float wz) -> float {   // bilinear in the ring, as the terrain samples it
        float fx = (wx - x0) / step, fz = (wz - z0) / step;
        int i = (int)floorf(fx), j = (int)floorf(fz); float tx = fx - i, tz = fz - j;
        float d = 0.f;
        if (i >= 0 && j >= 0 && i + 1 < res && j + 1 < res) {
            const float *r0 = delta + (size_t)j * res + i, *r1 = r0 + res;
            d = lerpf(lerpf(r0[0], r0[1], tx), lerpf(r1[0], r1[1], tx), tz);
        }
        return world_height(wx, wz) + d;
    };
    auto inst = [](float x, float y, float z, float heading, float scale) {
        inst_t in = inst_t{}; in.x = x; in.y = y; in.z = z; in.heading = heading; in.scale = scale;
        in.r = in.g = in.b = 1.f; return in;
    };
    for (int j = 1; j + 1 < res && total < SCATTER_MAX; ++j) for (int i = 1; i + 1 < res && total < SCATTER_MAX; ++i) {
        const unsigned char *t = lu + ((size_t)j * res + i) * 4;
        int R = t[0], Gm = t[1], B = t[2], A = t[3], F = forest ? forest[(size_t)j * res + i] : 0;
        if (B < 90 && Gm < 110 && A < 110 && F < 60 && !(R > 20 && R < 120)) continue;
        float wx = x0 + i * step, wz = z0 + j * step;
        float lon = x2lon(wx), lat = z2lat(wz);
        float medb = smoothstep_(35.f, 37.f, lat) * (1.f - smoothstep_(45.5f, 47.5f, lat)) * smoothstep_(-10.f, -6.f, lon) * (1.f - smoothstep_(36.f, 40.f, lon));
        // the district frame terrain.frag rotates its parcels into: one orientation per 1.5 units
        float dcx = floorf(wx / 1.5f), dcz = floorf(wz / 1.5f);
        float ang = hash2(dcx + 0.37f, dcz + 0.37f) * 3.14159f, ca = cosf(ang), sa = sinf(ang);
        if (A >= 110) {   // a village (0.22.5): houses on the shader's own grid, so roofs and paint agree
            float qx = wx * ca - wz * sa, qz = wx * sa + wz * ca;
            float hcx = 0.019f, hcz = 0.014f;
            float gx = floorf(qx / hcx), gz = floorf(qz / hcz);
            long long key = ((long long)(int)gx * 73856093LL) ^ ((long long)(int)gz * 19349663LL) ^ 0x5A5A5A5ALL ^ ((long long)(int)dcx * 83492791LL) ^ ((long long)(int)dcz * 2654435761LL);
            if (cells.insert(key).second && hash2(gx + 9.f, gz + 9.f) >= 0.42f) {
                float cqx = (gx + 0.5f) * hcx, cqz = (gz + 0.5f) * hcz;
                float hx_ = cqx * ca + cqz * sa, hz_ = -cqx * sa + cqz * ca;
                if (lu_at(hx_, hz_, 3) >= 110) {
                    float h = ground(hx_, hz_);
                    if (h > 0.04f) { *instlist_push(&li_house) = inst(hx_, h - 0.002f, hz_, -ang, HOUSE_W * (0.85f + 0.3f * hash2(gx + 11.f, gz + 11.f))); total++; }
                }
            }
            continue;
        }
        if (F >= 60 && B < 90) {   // the woods (0.22.5): a tree on most forest texels, pines high up and in the north
            float hp = hash2(i * 0.527f + 0.7f, j * 0.911f + 0.3f);
            if (hp < WOOD_P * (F / 255.f)) {
                float ox = (hash2(i + 21.f, j + 21.f) - 0.5f) * step, oz = (hash2(i + 22.f, j + 22.f) - 0.5f) * step;
                float h = ground(wx + ox, wz + oz);
                if (h > 0.04f) {
                    int conifer = (h > 1.6f || lat > 47.f || hash2(i + 23.f, j + 23.f) < 0.18f + 0.3f * clampf((h - 0.8f) / 1.2f, 0.f, 1.f));
                    float sz = (conifer ? WOOD_H * 1.15f : WOOD_H) * (0.75f + 0.5f * hash2(i + 24.f, j + 24.f));
                    *instlist_push(conifer ? &li_pine : &li_oak) = inst(wx + ox, h - 0.015f, wz + oz, hash2(i + 25.f, j + 25.f) * 6.2832f, sz); total++;
                }
            }
            continue;
        }
        if (B >= 90) {   // orchards and groves: one tree per TREE_CELL of the district grid
            float qx = wx * ca - wz * sa, qz = wx * sa + wz * ca;
            float gx = floorf(qx / TREE_CELL), gz = floorf(qz / TREE_CELL);
            long long key = ((long long)(int)gx * 73856093LL) ^ ((long long)(int)gz * 19349663LL) ^ ((long long)(int)dcx * 83492791LL) ^ ((long long)(int)dcz * 2654435761LL);
            if (cells.insert(key).second) {
                float jx = (hash2(gx + 1.f, gz + 1.f) - 0.5f) * 0.5f, jz = (hash2(gx + 2.f, gz + 2.f) - 0.5f) * 0.5f;
                float cqx = (gx + 0.5f + jx) * TREE_CELL, cqz = (gz + 0.5f + jz) * TREE_CELL;
                float tx = cqx * ca + cqz * sa, tz = -cqx * sa + cqz * ca;
                if (lu_at(tx, tz, 2) >= 90) {   // the tree stands inside the grove
                    float h = ground(tx, tz);
                    if (h > 0.04f) {
                        float s = TREE_H * (0.8f + 0.4f * hash2(gx + 3.f, gz + 3.f));
                        inst_t in = inst(tx, h - 0.004f, tz, hash2(gx + 4.f, gz + 4.f) * 6.2832f, s);
                        *instlist_push(medb > 0.5f ? &li_olive : &li_tree) = in; total++;
                    }
                }
            }
        } else if (Gm >= 110) {   // vineyards: a bush per texel or two, trained along the contour
            float hp = hash2(i * 0.731f + 0.1f, j * 0.377f + 0.2f);
            if (hp < 0.55f) {
                float hx = ground(wx + step, wz) - ground(wx - step, wz), hz = ground(wx, wz + step) - ground(wx, wz - step);
                float gl = sqrtf(hx * hx + hz * hz);
                float dx = gl > 1e-5f ? -hz / gl : ca, dz = gl > 1e-5f ? hx / gl : -sa;   // along the contour
                float heading = atan2f(-dz, dx);
                float ox = (hash2(i + 7.f, j + 7.f) - 0.5f) * step, oz = (hash2(i + 8.f, j + 8.f) - 0.5f) * step;
                float h = ground(wx + ox, wz + oz);
                if (h > 0.04f) { *instlist_push(&li_vine) = inst(wx + ox, h - 0.002f, wz + oz, heading, VINE_H * (0.85f + 0.3f * hp)); total++; }
            }
        } else {   // pasture: the thin fields round a village - the odd flock of sheep
            if (hash2(i * 0.913f + 0.3f, j * 0.577f + 0.4f) < 0.004f) {
                int n = 4 + (int)(hash2(i + 9.f, j + 9.f) * 4.f);
                for (int k = 0; k < n && total < SCATTER_MAX; ++k) {
                    float ox = (hash2(i + 10.f + k, j + 10.f) - 0.5f) * step * 1.6f, oz = (hash2(i + 11.f, j + 11.f + k) - 0.5f) * step * 1.6f;
                    float h = ground(wx + ox, wz + oz);
                    if (h > 0.04f) { *instlist_push(&li_sheep) = inst(wx + ox, h, wz + oz, hash2(i + 12.f + k, j + 12.f + k) * 6.2832f, SHEEP_L * (0.9f + 0.2f * hash2(i + 13.f, j + 13.f + k))); total++; }
                }
            }
        }
    }
    mesh_set_instances(&tree, li_tree.v, li_tree.n);
    mesh_set_instances(&olive, li_olive.v, li_olive.n);
    mesh_set_instances(&vine, li_vine.v, li_vine.n);
    mesh_set_instances(&sheep, li_sheep.v, li_sheep.n);
    mesh_set_instances(&pine, li_pine.v, li_pine.n);
    mesh_set_instances(&oak, li_oak.v, li_oak.n);
    mesh_set_instances(&house, li_house.v, li_house.n);
    scatter_count = total;
    LOG_DEBUG("stream", "hinterland props: %d orchard trees, %d olives, %d vines, %d sheep, %d forest broadleaves, %d pines, %d houses in %.1f ms", li_tree.n, li_olive.n, li_vine.n, li_sheep.n, li_oak.n, li_pine.n, li_house.n, (diag_now() - t0) * 1000);
}

int Scatter::submit() {
    if (!scatter_enabled || !built || cam.mapness > 0.5f || cam.dist > HOUSE_FAR) return 0;
    int n = 0;
    if (li_house.n) { mesh_draw(&house); n += li_house.n; }
    if (li_oak.n && cam.dist < HOUSE_FAR * 0.7f)  { mesh_draw(&oak);  n += li_oak.n; }
    if (li_pine.n && cam.dist < HOUSE_FAR * 0.7f) { mesh_draw(&pine); n += li_pine.n; }
    if (cam.dist > SCATTER_FAR) return n;
    if (li_tree.n)  { mesh_draw(&tree);  n += li_tree.n; }
    if (li_olive.n) { mesh_draw(&olive); n += li_olive.n; }
    if (li_vine.n)  { mesh_draw(&vine);  n += li_vine.n; }
    if (li_sheep.n) { mesh_draw(&sheep); n += li_sheep.n; }
    return n;
}

} // namespace mare::game::view

extern "C" {
void scatter_rebuild(const unsigned char *landuse, const unsigned char *forest, const float *delta, int res, float x0, float z0, float step) {
    mare::game::view::scatter().rebuild(landuse, forest, delta, res, x0, z0, step);
}
int scatter_submit(void) { return mare::game::view::scatter().submit(); }
}
