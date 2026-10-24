// Procedural meshes: ships (hull loft, masts, billowed sails, nation stripe & flag) in two
// LODs, and town pieces (house, roof, tower, wall, pier, flagpole). One instanced program
// renders them all: material 1 vertices take the per-instance colour.

#include <math.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"

mesh_t ship_mesh[SHIP_COUNT][2];
mesh_t mesh_house, mesh_roof, mesh_tower, mesh_wall, mesh_pier, mesh_flagpole;
static rhi_pipeline_t meshes_prg;

static const char *meshes_vs =
    "in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat;\n"
    "in vec4 i_pos; in vec4 i_scol; in vec4 i_misc;\n"   // xyz+heading | scale+rgb | sway, highlight, bob, x-stretch
    "uniform mat4 u_vp; uniform float u_time;\n"
    "out vec3 v_nrm; out vec3 v_col; out vec3 v_wpos; out float v_sel;\n"
    "void main(){\n"
    "  float h = i_pos.w; float c = cos(h), s = sin(h);\n"
    "  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;\n"
    "  float roll = i_misc.x * sin(u_time*1.3 + i_pos.x*3.0 + i_pos.z*2.0) * 0.06;\n"
    "  float cr = cos(roll), sr = sin(roll);\n"
    "  p = vec3(p.x, p.y*cr - p.z*sr, p.y*sr + p.z*cr);\n"
    "  vec3 n = vec3(a_nrm.x, a_nrm.y*cr - a_nrm.z*sr, a_nrm.y*sr + a_nrm.z*cr);\n"
    "  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;\n"
    "  wp.y += i_misc.z * sin(u_time*1.1 + i_pos.x*2.0) * 0.02;\n"
    "  v_nrm = vec3(n.x*c + n.z*s, n.y, -n.x*s + n.z*c);\n"
    "  v_col = mix(a_col, i_scol.yzw, a_mat);\n"
    "  v_wpos = wp; v_sel = i_misc.y;\n"
    "  gl_Position = u_vp * vec4(wp, 1.0);\n"
    "}\n";
static const char *meshes_fs =
    "in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel;\n"
    "uniform vec3 u_sun; uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform float u_daylight;\n"
    "out vec4 fragColor;\n"
    GLSL_FOG
    "void main(){\n"
    "  vec3 n = normalize(v_nrm);\n"
    "  float diff = max(dot(n, u_sun), 0.0);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    "  float spec = pow(max(dot(reflect(-u_sun, n), vdir), 0.0), 24.0) * 0.25;\n"
    "  vec3 c = v_col * (0.35 + 0.7*diff) + spec;\n"
    "  c = mix(c, vec3(1.0,0.95,0.6), v_sel * 0.35);\n"
    "  c = fog_apply(c * u_daylight, distance(v_wpos, u_campos), u_mapness, u_sky);\n"
    "  fragColor = vec4(c, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------
// ships. local frame: forward +x, up +y, starboard +z, length ~1

float ship_scale(int type) { static const float s[SHIP_COUNT] = { 0.7f, 1.0f, 1.2f, 1.25f, 1.3f, 1.55f }; return s[type] * 1.35f; }

static void hull_profile(int type, float t, float *halfw, float *depth, float *deck) {
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

#define V3_(x,y,z) (float[3]){ (x), (y), (z) }
#define MZ(v) (float[3]){ (v)[0], (v)[1], -(v)[2] }

static void build_ship(int type, int lod) {
    mb_t m = {0};
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
            float s0[3] = {pt,pk*0.55f,pw*0.985f}, s1[3] = {t,k*0.55f,w*0.985f}, s2[3] = {t,k*0.75f,w*0.975f}, s3[3] = {pt,pk*0.75f,pw*0.975f};
            mb_quad(&m, s0,s1,s2,s3, 1,1,1, 1); mb_quad(&m, MZ(s0),MZ(s3),MZ(s2),MZ(s1), 1,1,1, 1);
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
        for (int s = 0; s < courses; ++s) {
            float y0 = yd + mh * (courses == 1 ? 0.18f : s == 0 ? 0.18f : 0.62f), y1 = yd + mh * (courses == 1 ? 0.92f : s == 0 ? 0.56f : 0.92f);
            float hw = (s == 0 ? 0.15f : 0.11f) * (type == SHIP_SLOOP ? 0.8f : 1.f), bx = mx + 0.01f;
            float a[3] = {bx,y0,-hw}, b[3] = {bx,y0,hw}, c[3] = {bx,y1,hw}, d[3] = {bx,y1,-hw}, mid[3] = {bx + 0.045f, (y0+y1)*0.5f, 0};
            if (lod == 0) {   // billowed: fan of 4 triangles around a centre pushed forward, both sides
                const float *q[4] = { a, b, c, d };
                for (int e = 0; e < 4; ++e) { const float *p0 = q[e], *p1 = q[(e+1)%4];
                    mb_tri(&m, p0[0],p0[1],p0[2], p1[0],p1[1],p1[2], mid[0],mid[1],mid[2], sail[0],sail[1],sail[2], 0);
                    mb_tri(&m, p1[0],p1[1],p1[2], p0[0],p0[1],p0[2], mid[0],mid[1],mid[2], sail[0],sail[1],sail[2], 0); }
            } else mb_quad2(&m, a, b, c, d, sail[0],sail[1],sail[2], 0);
        }
    }
    if (lod == 0) {
        mb_box(&m, 0.56f, 0.08f, 0, 0.08f, 0.006f, 0.006f, mast[0],mast[1],mast[2], 0);   // bowsprit
        float fx = mast_x[nmasts > 1 ? 1 : 0], fy = yd + mast_h[nmasts > 1 ? 1 : 0];
        mb_quad2(&m, V3_(fx,fy,0), V3_(fx-0.09f,fy-0.01f,0), V3_(fx-0.09f,fy+0.04f,0), V3_(fx,fy+0.05f,0), 1,1,1, 1);   // flag
    }
    ship_mesh[type][lod] = mesh_upload(&m);
}

static void build_buildings(void) {
    mb_t m = {0};
    mb_box(&m, 0,0.5f,0, 0.5f,0.5f,0.5f, 1,1,1, 1); mesh_house = mesh_upload(&m);
    { float a[3] = {-0.55f,0,-0.55f}, b[3] = {0.55f,0,-0.55f}, c[3] = {0.55f,0,0.55f}, d[3] = {-0.55f,0,0.55f}, r0[3] = {-0.55f,0.45f,0}, r1[3] = {0.55f,0.45f,0};
      mb_quad(&m, a,b,r1,r0, 1,1,1, 1); mb_quad(&m, d,r0,r1,c, 1,1,1, 1);
      mb_tri(&m, a[0],a[1],a[2], r0[0],r0[1],r0[2], d[0],d[1],d[2], 1,1,1, 1); mb_tri(&m, b[0],b[1],b[2], c[0],c[1],c[2], r1[0],r1[1],r1[2], 1,1,1, 1);
      mesh_roof = mesh_upload(&m); }
    mb_box(&m, 0,0.5f,0, 0.3f,0.5f,0.3f, 1,1,1, 1);
    { float tp[3] = {0,1.45f,0}, r = 0.35f, g = 0.2f, b = 0.2f;
      mb_tri(&m, -0.36f,1,-0.36f, tp[0],tp[1],tp[2], 0.36f,1,-0.36f, r,g,b, 0); mb_tri(&m, 0.36f,1,-0.36f, tp[0],tp[1],tp[2], 0.36f,1,0.36f, r,g,b, 0);
      mb_tri(&m, 0.36f,1,0.36f, tp[0],tp[1],tp[2], -0.36f,1,0.36f, r,g,b, 0);  mb_tri(&m, -0.36f,1,0.36f, tp[0],tp[1],tp[2], -0.36f,1,-0.36f, r,g,b, 0); }
    mesh_tower = mesh_upload(&m);
    mb_box(&m, 0,0.5f,0, 0.5f,0.5f,0.08f, 0.55f,0.52f,0.48f, 0);
    for (int i = 0; i < 4; ++i) mb_box(&m, -0.375f + i*0.25f, 1.08f, 0, 0.07f,0.08f,0.09f, 0.5f,0.47f,0.44f, 0);
    mesh_wall = mesh_upload(&m);
    mb_box(&m, 0,0.5f,0, 0.5f,0.05f,0.12f, 0.45f,0.35f,0.22f, 0);
    for (int i = 0; i < 3; ++i) { mb_box(&m, -0.4f + i*0.4f, 0.25f, -0.1f, 0.02f,0.25f,0.02f, 0.3f,0.22f,0.14f, 0); mb_box(&m, -0.4f + i*0.4f, 0.25f, 0.1f, 0.02f,0.25f,0.02f, 0.3f,0.22f,0.14f, 0); }
    mesh_pier = mesh_upload(&m);
    mb_box(&m, 0,0.5f,0, 0.015f,0.5f,0.015f, 0.8f,0.8f,0.8f, 0);
    mb_quad2(&m, V3_(0,1,0), V3_(0.25f,0.98f,0), V3_(0.25f,0.85f,0), V3_(0,0.83f,0), 1,1,1, 1);
    mesh_flagpole = mesh_upload(&m);
}

void meshes_init(void) {
    static const char *uniforms[] = { "u_vp", "u_time", "u_sun", "u_campos", "u_sky", "u_mapness", "u_daylight", 0 };
    meshes_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = meshes_vs, .fs = meshes_fs, .layout = &mesh_layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1 });
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) build_ship(t, l);
    build_buildings();
}

void meshes_bind(void) {
    rhi_pipeline_bind(&meshes_prg);
    rhi_set_mat4(UNI(meshes_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f1(UNI(meshes_prg,"u_time"), RF.time);
    rhi_set_f3(UNI(meshes_prg,"u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
    rhi_set_f3(UNI(meshes_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(meshes_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(meshes_prg,"u_mapness"), RF.mapness);
    rhi_set_f1(UNI(meshes_prg,"u_daylight"), RF.daylight);
}
