// mare::engine::render::Meshes (see render.h): procedural meshes: ships (hull loft, masts,
// billowed sails, nation stripe & flag) in two
// LODs, and town pieces (house, roof, tower, wall, pier, flagpole). One instanced program
// renders them all: material 1 vertices take the per-instance colour.

#include "../platform/v3cpp.hh"
#include <cmath>

extern "C" {
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../engine/shaders.h"
}

mesh_t ship_mesh[SHIP_COUNT][2];
mesh_t mesh_house, mesh_roof, mesh_tower, mesh_wall, mesh_pier, mesh_flagpole, mesh_puff;

namespace mare::engine::render {

class Meshes {
public:
    void init();
    float ship_scale(int type);
    void bind();
    void bind_smoke();
    void bind_depth(const mat44 *lightvp);

private:
    rhi_pipeline_t meshes_prg;

    // Cloth (gfx/shipyard.c). i_fx.y > 0.5 marks a canvas instance and i_fx.zw carries the true
    // wind at the ship (world xz, direction times strength). Each vertex's metalness is a cloth
    // code (gpu.h mb_cloth_code): 32 levels of how free it is - 0 bent to its spar, 1 loose - and
    // where it sits across its panel; its roughness is the panel's half-width in model units; its
    // normal is the direction the canvas bellies in. Nothing is integrated: the shape is the steady
    // state of a sheet pinned along one edge, pressed along its normal by the wind through it and
    // held at the foot by its sheets, plus a flutter that grows when the wind runs along the canvas
    // instead of into it (a luffing sail). A square sail pressed back by a head wind stops at its
    // mast - the middle holds, the leeches blow past. Flags stream in the apparent wind. Both the
    // main and the depth pass run this, so a sail's shadow is the sail's shape.


    // depth-only variant for the shadow cascades: same instance transform, no shading
    // Gunsmoke: the same instanced geometry, but shaded as a soft translucent volume instead of a
    // solid surface - alpha falls off towards the silhouette, so the sphere reads as a puff rather
    // than a ball. Blended, depth-tested but not depth-written, drawn after the sea.
    rhi_pipeline_t smoke_prg;

       // shadows cross-fade with the meshes
    rhi_pipeline_t depth_prg;

    static void hull_profile(int type, float t, float *halfw, float *depth, float *deck);
    void build_ship(int type, int lod);
    void build_buildings();
    void build_puff();
    void meshes_bind_waves(const rhi_pipeline_t *p);
};

Meshes &meshes() { static Meshes m; return m; }

// ---------------------------------------------------------------------------
// ships. local frame: forward +x, up +y, starboard +z, length ~1

float Meshes::ship_scale(int type) { static const float s[SHIP_COUNT] = { 0.7f, 1.0f, 1.2f, 1.25f, 1.3f, 1.55f }; return s[type] * 1.35f; }

void Meshes::hull_profile(int type, float t, float *halfw, float *depth, float *deck) {
    float u = t + 0.5f;   // 0 stern .. 1 bow
    float fullness = type == SHIP_FLUYT ? 1.15f : type == SHIP_GALLEON || type == SHIP_MERCHANT ? 1.05f : type == SHIP_SLOOP ? 0.8f : 0.9f;
    float w = powf(sinf(u * 3.14159f), 0.55f);
    if (u < 0.25f) w = maxf(w, 0.55f + u);
    *halfw = 0.13f * fullness * w;
    *depth = 0.07f * (0.6f + 0.4f*sinf(u*3.14159f));
    float sheer = 0.05f + 0.035f * sqr(u - 0.4f) * 4;
    if (u < 0.2f) sheer += (0.2f - u) * 0.25f;   // poop deck
    *deck = sheer;
}

// C compound-literal float[3] temporaries (not expressible in C++): a 3-float temporary that
// decays to const float*, alive for the full call expression like the C literal was for its block
struct F3Tmp { float v[3]; operator const float *() const { return v; } };
#define V3_(x,y,z) F3Tmp{ { (x), (y), (z) } }
#define MZ(v) F3Tmp{ { (v)[0], (v)[1], -(v)[2] } }

void Meshes::build_ship(int type, int lod) {
    mb_t m = {0};
    mb_material(&m, MB_WOOD);
    const float hull[3] = {0.30f,0.20f,0.12f}, dark[3] = {0.10f,0.08f,0.07f}, deck[3] = {0.62f,0.50f,0.34f}, sail[3] = {0.92f,0.89f,0.80f}, mast[3] = {0.36f,0.26f,0.16f};
    int N = lod == 0 ? 10 : 5;
    float pw, pd, pk; hull_profile(type, -0.5f, &pw, &pd, &pk); float pt = -0.5f;
    for (int i = 1; i <= N; ++i) {
        float t = -0.5f + i / (float)N, w, d, k; hull_profile(type, t, &w, &d, &k);
        if (i == N) w = 0.01f;
        float a0[3] = {pt,-pd,0}, a1[3] = {pt,-pd*0.45f,pw*0.8f}, a2[3] = {pt,0,pw}, a3[3] = {pt,pk,pw*0.95f};
        float b0[3] = {t,-d,0},   b1[3] = {t,-d*0.45f,w*0.8f},    b2[3] = {t,0,w},   b3[3] = {t,k,w*0.95f};
        mb_quad(&m, a0,b0,b1,a1, dark[0],dark[1],dark[2], 0); mb_quad(&m, a1,b1,b2,a2, dark[0],dark[1],dark[2], 0); mb_quad(&m, a2,b2,b3,a3, hull[0],hull[1],hull[2], 0);
        mb_quad(&m, MZ(a0),MZ(a1),MZ(b1),MZ(b0), dark[0],dark[1],dark[2], 0); mb_quad(&m, MZ(a1),MZ(a2),MZ(b2),MZ(b1), dark[0],dark[1],dark[2], 0); mb_quad(&m, MZ(a2),MZ(a3),MZ(b3),MZ(b2), hull[0],hull[1],hull[2], 0);
        mb_quad(&m, a3,b3,MZ(b3),MZ(a3), deck[0],deck[1],deck[2], 0);
        if (lod == 0) {   // nation stripe below the rail
            mb_material(&m, MB_PAINT);
            float s0[3] = {pt,pk*0.55f,pw*0.985f}, s1[3] = {t,k*0.55f,w*0.985f}, s2[3] = {t,k*0.75f,w*0.975f}, s3[3] = {pt,pk*0.75f,pw*0.975f};
            mb_quad(&m, s0,s1,s2,s3, 1,1,1, 1); mb_quad(&m, MZ(s0),MZ(s3),MZ(s2),MZ(s1), 1,1,1, 1);
            mb_material(&m, MB_WOOD);
        }
        pw = w; pd = d; pk = k; pt = t;
    }
    { float w, d, k; hull_profile(type, -0.5f, &w, &d, &k);   // transom
      mb_quad(&m, V3_(-0.5f,-d,0), V3_(-0.5f,0,-w), V3_(-0.5f,k,-w*0.95f), V3_(-0.5f,k,w*0.95f), hull[0],hull[1],hull[2], 0);
      mb_tri(&m, -0.5f,-d,0, -0.5f,k,w*0.95f, -0.5f,0,w, hull[0],hull[1],hull[2], 0); }
    int nmasts = type == SHIP_SLOOP ? 1 : 3;
    float mast_x[3] = { 0.22f, -0.05f, -0.30f }, mast_h[3] = { 0.38f, 0.45f, 0.30f };
    if (type == SHIP_SLOOP) { mast_x[0] = 0.02f; mast_h[0] = 0.42f; }
    if (type == SHIP_LINER || type == SHIP_GALLEON) { mast_h[0] *= 1.2f; mast_h[1] *= 1.2f; mast_h[2] *= 1.15f; }
    float yd = 0.06f;
    for (int i = 0; i < nmasts; ++i) {
        float mx = mast_x[i], mh = mast_h[i];
        if (lod == 0) mb_box(&m, mx, yd + mh*0.5f, 0, 0.008f, mh*0.5f, 0.008f, mast[0],mast[1],mast[2], 0);
        else mb_quad2(&m, V3_(mx,yd,-0.006f), V3_(mx,yd,0.006f), V3_(mx,yd+mh,0.006f), V3_(mx,yd+mh,-0.006f), mast[0],mast[1],mast[2], 0);
        int courses = lod == 0 ? 2 : 1;
        mb_material(&m, MB_CLOTH);
        for (int s = 0; s < courses; ++s) {
            float y0 = yd + mh * (courses == 1 ? 0.18f : s == 0 ? 0.18f : 0.62f), y1 = yd + mh * (courses == 1 ? 0.92f : s == 0 ? 0.56f : 0.92f);
            float hw = (s == 0 ? 0.15f : 0.11f) * (type == SHIP_SLOOP ? 0.8f : 1.f), bx = mx + 0.01f;
            float a[3] = {bx,y0,-hw}, b[3] = {bx,y0,hw}, c[3] = {bx,y1,hw}, d[3] = {bx,y1,-hw}, mid[3] = {bx + 0.045f, (y0+y1)*0.5f, 0};
            if (lod == 0) {   // billowed: fan of 4 triangles around a centre pushed forward, both sides
                mb_set_normal(&m, 1, 0, 0, 0.72f);   // read as one curved surface, not four wedges
                const float *q[4] = { a, b, c, d };
                for (int e = 0; e < 4; ++e) { const float *p0 = q[e], *p1 = q[(e+1)%4];
                    mb_tri(&m, p0[0],p0[1],p0[2], p1[0],p1[1],p1[2], mid[0],mid[1],mid[2], sail[0],sail[1],sail[2], 0);
                    mb_tri(&m, p1[0],p1[1],p1[2], p0[0],p0[1],p0[2], mid[0],mid[1],mid[2], sail[0],sail[1],sail[2], 0); }
                mb_auto_normal(&m);
            } else mb_quad2(&m, a, b, c, d, sail[0],sail[1],sail[2], 0);
        }
        mb_material(&m, MB_WOOD);
    }
    if (lod == 0) {
        mb_box(&m, 0.56f, 0.08f, 0, 0.08f, 0.006f, 0.006f, mast[0],mast[1],mast[2], 0);   // bowsprit
        float fx = mast_x[nmasts > 1 ? 1 : 0], fy = yd + mast_h[nmasts > 1 ? 1 : 0];
        mb_material(&m, MB_CLOTH);
        mb_quad2(&m, V3_(fx,fy,0), V3_(fx-0.09f,fy-0.01f,0), V3_(fx-0.09f,fy+0.04f,0), V3_(fx,fy+0.05f,0), 1,1,1, 1);   // flag
    }
    ship_mesh[type][lod] = mesh_upload(&m);
}

void Meshes::build_buildings() {
    mb_t m = {0};
    mb_material(&m, MB_STONE);
    mb_box(&m, 0,0.5f,0, 0.5f,0.5f,0.5f, 1,1,1, 1); mesh_house = mesh_upload(&m);
    mb_material(&m, MB_TILE);
    { float a[3] = {-0.55f,0,-0.55f}, b[3] = {0.55f,0,-0.55f}, c[3] = {0.55f,0,0.55f}, d[3] = {-0.55f,0,0.55f}, r0[3] = {-0.55f,0.45f,0}, r1[3] = {0.55f,0.45f,0};
      mb_quad(&m, a,b,r1,r0, 1,1,1, 1); mb_quad(&m, d,r0,r1,c, 1,1,1, 1);
      mb_tri(&m, a[0],a[1],a[2], r0[0],r0[1],r0[2], d[0],d[1],d[2], 1,1,1, 1); mb_tri(&m, b[0],b[1],b[2], c[0],c[1],c[2], r1[0],r1[1],r1[2], 1,1,1, 1);
      mesh_roof = mesh_upload(&m); }
    mb_material(&m, MB_STONE);
    mb_box(&m, 0,0.5f,0, 0.3f,0.5f,0.3f, 1,1,1, 1);
    { float tp[3] = {0,1.45f,0}, r = 0.35f, g = 0.2f, b = 0.2f;
      mb_tri(&m, -0.36f,1,-0.36f, tp[0],tp[1],tp[2], 0.36f,1,-0.36f, r,g,b, 0); mb_tri(&m, 0.36f,1,-0.36f, tp[0],tp[1],tp[2], 0.36f,1,0.36f, r,g,b, 0);
      mb_tri(&m, 0.36f,1,0.36f, tp[0],tp[1],tp[2], -0.36f,1,0.36f, r,g,b, 0);  mb_tri(&m, -0.36f,1,0.36f, tp[0],tp[1],tp[2], -0.36f,1,-0.36f, r,g,b, 0); }
    mesh_tower = mesh_upload(&m);
    mb_box(&m, 0,0.5f,0, 0.5f,0.5f,0.08f, 0.55f,0.52f,0.48f, 0);
    for (int i = 0; i < 4; ++i) mb_box(&m, -0.375f + i*0.25f, 1.08f, 0, 0.07f,0.08f,0.09f, 0.5f,0.47f,0.44f, 0);
    mesh_wall = mesh_upload(&m);
    mb_material(&m, MB_WOOD);
    mb_box(&m, 0,0.5f,0, 0.5f,0.05f,0.12f, 0.45f,0.35f,0.22f, 0);
    for (int i = 0; i < 3; ++i) { mb_box(&m, -0.4f + i*0.4f, 0.25f, -0.1f, 0.02f,0.25f,0.02f, 0.3f,0.22f,0.14f, 0); mb_box(&m, -0.4f + i*0.4f, 0.25f, 0.1f, 0.02f,0.25f,0.02f, 0.3f,0.22f,0.14f, 0); }
    mesh_pier = mesh_upload(&m);
    mb_box(&m, 0,0.5f,0, 0.015f,0.5f,0.015f, 0.8f,0.8f,0.8f, 0);
    mb_material(&m, MB_CLOTH);
    mb_quad2(&m, V3_(0,1,0), V3_(0.25f,0.98f,0), V3_(0.25f,0.85f,0), V3_(0,0.83f,0), 1,1,1, 1);
    mesh_flagpole = mesh_upload(&m);
}

// A low-poly sphere used as a gunsmoke puff: material 1, so the instance colour tints it, and
// smooth normals so it reads as a soft ball rather than a faceted rock.
void Meshes::build_puff() {
    mb_t m = {0};
    mb_material(&m, 0.97f, 0.f);
    const int SEG = 14, RING = 7;   // round enough to read as smoke at battle-view scale
    for (int j = 0; j < RING; ++j) {
        float t0 = j * 3.14159f / RING, t1 = (j+1) * 3.14159f / RING;
        for (int i = 0; i < SEG; ++i) {
            float p0 = i * 6.28318f / SEG, p1 = (i+1) * 6.28318f / SEG;
            float a[3] = { sinf(t0)*cosf(p0)*0.5f, cosf(t0)*0.5f, sinf(t0)*sinf(p0)*0.5f };
            float b[3] = { sinf(t1)*cosf(p0)*0.5f, cosf(t1)*0.5f, sinf(t1)*sinf(p0)*0.5f };
            float c[3] = { sinf(t1)*cosf(p1)*0.5f, cosf(t1)*0.5f, sinf(t1)*sinf(p1)*0.5f };
            float d[3] = { sinf(t0)*cosf(p1)*0.5f, cosf(t0)*0.5f, sinf(t0)*sinf(p1)*0.5f };
            float mid[3] = { (a[0]+b[0]+c[0]+d[0])*0.25f, (a[1]+b[1]+c[1]+d[1])*0.25f, (a[2]+b[2]+c[2]+d[2])*0.25f };
            mb_set_normal(&m, mid[0], mid[1], mid[2], 0.85f);
            mb_quad(&m, a, b, c, d, 1,1,1, 1);
        }
    }
    mb_auto_normal(&m);
    mesh_puff = mesh_upload(&m);
}

void Meshes::init() {
    static const char *uniforms[] = { "u_vp", "u_time", "u_campos", "u_sky", "u_mapness", "u_grain",
                                      "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hm", "u_geo", "u_wk", GLSL_WAVE_UNIFORMS, GLSL_LIGHT_UNIFORMS, 0 };
    static const char *depth_uniforms[] = { "u_vp", "u_time", "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hm", "u_geo", "u_wk", GLSL_WAVE_UNIFORMS, 0 };
    // (C compound literals -> named descriptors: C++ cannot take &T{...})
    const rhi_pipeline_desc_t meshes_desc{ .vs = shader_source("meshes/meshes.vert"), .fs = shader_source("meshes/meshes.frag"), .layout = &mesh_layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1 };
    meshes_prg = rhi_pipeline_make(&meshes_desc);
    const rhi_pipeline_desc_t depth_desc{ .vs = shader_source("meshes/depth.vert"), .fs = shader_source("meshes/depth.frag"), .layout = &mesh_layout, .uniforms = depth_uniforms, .depth_test = 1, .depth_write = 1 };
    depth_prg = rhi_pipeline_make(&depth_desc);
    const rhi_pipeline_desc_t smoke_desc{ .vs = shader_source("meshes/meshes.vert"), .fs = shader_source("meshes/smoke.frag"), .layout = &mesh_layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 0, .blend = RHI_BLEND_ALPHA };
    smoke_prg = rhi_pipeline_make(&smoke_desc);
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) build_ship(t, l);
    build_buildings();
    build_puff();
}

// the wave uniforms every instanced pipeline needs so floating instances ride the same surface
// the sea pass draws (render.h: sea_wave_amp / sea_wave_scale / sea_local_depth)
void Meshes::meshes_bind_waves(const rhi_pipeline_t *p) {
    sea_bind_waves(p);
    rhi_set_f1(rhi_uniform(p,"u_wave_amp"), sea_amp_now());   // ships ride exactly the zoom-scaled field the sea draws
    rhi_set_f1(rhi_uniform(p,"u_wave_scale"), sea_wave_scale);
    rhi_set_f1(rhi_uniform(p,"u_local_depth"), sea_local_depth);
    rhi_set_i1(rhi_uniform(p,"u_hm"), 4); rhi_texture_bind(4, terrain_heightmap_texture());
    rhi_set_f4(rhi_uniform(p,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(rhi_uniform(p,"u_wk"), WORLD_K);
}

void Meshes::bind() {
    rhi_pipeline_bind(&meshes_prg);
    meshes_bind_waves(&meshes_prg);
    rhi_set_i1(UNI(meshes_prg,"u_grain"), 2); rhi_texture_bind(2, detail_grain());
    rhi_set_mat4(UNI(meshes_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f1(UNI(meshes_prg,"u_time"), RF.time);
    rhi_set_f3(UNI(meshes_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(meshes_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(meshes_prg,"u_mapness"), RF.mapness);
    light_bind(&meshes_prg);
}

void Meshes::bind_smoke() {
    rhi_pipeline_bind(&smoke_prg);
    meshes_bind_waves(&smoke_prg);
    rhi_set_mat4(UNI(smoke_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f1(UNI(smoke_prg,"u_time"), RF.time);
    rhi_set_f3(UNI(smoke_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(smoke_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(smoke_prg,"u_mapness"), RF.mapness);
    light_bind(&smoke_prg);
}

void Meshes::bind_depth(const mat44 *lightvp) {
    rhi_pipeline_bind(&depth_prg);
    meshes_bind_waves(&depth_prg);
    rhi_set_mat4(UNI(depth_prg,"u_vp"), &lightvp->x.x);
    rhi_set_f1(UNI(depth_prg,"u_time"), RF.time);
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
float ship_scale(int type) { return mare::engine::render::meshes().ship_scale(type); }
void meshes_init(void) { mare::engine::render::meshes().init(); }
void meshes_bind(void) { mare::engine::render::meshes().bind(); }
void meshes_bind_smoke(void) { mare::engine::render::meshes().bind_smoke(); }
void meshes_bind_depth(const mat44 *lightvp) { mare::engine::render::meshes().bind_depth(lightvp); }
}
