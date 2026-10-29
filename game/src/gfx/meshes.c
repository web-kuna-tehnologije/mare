// Procedural meshes: ships (hull loft, masts, billowed sails, nation stripe & flag) in two
// LODs, and town pieces (house, roof, tower, wall, pier, flagpole). One instanced program
// renders them all: material 1 vertices take the per-instance colour.

#include <math.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"

const rhi_texture_t *terrain_heightmap_texture(void);

mesh_t ship_mesh[SHIP_COUNT][2];
mesh_t mesh_house, mesh_roof, mesh_tower, mesh_wall, mesh_pier, mesh_flagpole, mesh_puff;
static rhi_pipeline_t meshes_prg;

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
#define GLSL_CLOTH \
    "  float cm = a_rm.y * 32.0; float cwq = floor(cm + 1e-4); float cw = cwq / 31.0; float cu = fract(cm + 1e-4) * 2.0 - 1.0;\n" \
    "  float isc = i_fx.y > 0.5 ? 1.0 : 0.0; vec3 cnrm = a_nrm;\n" \
    "  if (isc > 0.5 && cwq > 0.5) {\n" \
    "    float wxs = i_fx.z * c - i_fx.w * s, wzs = i_fx.z * s + i_fx.w * c;\n"   /* the wind in the ship's frame: along (+ = from astern), across (+ = to starboard) */ \
    "    float wstr = length(i_fx.zw);\n" \
    "    float way = clamp(i_misc.x, 0.0, 1.2) * 0.5;\n"                          /* how much way she has on: moored 0.12, under sail 0.3-0.5 */ \
    "    float hw = a_rm.x * i_scol.x;\n" \
    "    float ph0 = i_pos.x * 0.7 + i_pos.z * 0.5;\n" \
    "    if (a_mat > 0.5) {\n" \
    /* a flag: this vertex sits cw of the way along a ribbon of length hw that the mesh trails \
       aft; swing it to the apparent wind (her own way adds a head wind), droop it in a light \
       air, ripple it along its length */ \
    "      vec2 aw = vec2(wxs - way * 0.8, wzs); float al = length(aw);\n" \
    "      vec2 fd = al > 1e-3 ? aw / al : vec2(-1.0, 0.0);\n" \
    "      float L = hw * cw, lift = clamp(al / 0.6, 0.0, 1.0);\n" \
    "      vec3 side = vec3(-fd.y, 0.0, fd.x);\n" \
    "      float fph = u_time * (4.0 + 6.0 * al) - cw * 9.0 + ph0;\n" \
    "      p.x += L * (fd.x + 1.0); p.z += L * fd.y;\n" \
    "      p.y -= L * 0.6 * (1.0 - lift) * cw;\n" \
    "      p += side * (L * 0.10 * sin(fph) * lift) + vec3(0.0, L * 0.05 * sin(fph * 1.3 + 1.0), 0.0);\n" \
    "      cnrm = normalize(side + vec3(0.0, 0.35 * cos(fph), 0.0));\n" \
    "    } else {\n" \
    /* a sail: pressure is the wind through the canvas. A square sail (normal along the hull) \
       also draws from the ship's own way, so a ship under sail is never shown with slack \
       canvas; a lateen is sheeted to one side and so always holds a little shape */ \
    "      float press = wxs * cnrm.x + wzs * cnrm.z;\n" \
    "      float fill = abs(cnrm.x) > 0.5 ? press + way * 0.9 : (abs(press) < 0.15 ? 0.15 : press);\n" \
    "      float mag = clamp(abs(fill), 0.0, 1.4), dir = fill < 0.0 ? -1.0 : 1.0;\n" \
    "      float fn = 0.3 + 0.7 * min(mag, 1.0);\n" \
    "      float prof = sin(3.14159 * min(cw * 0.82, 1.0));\n"              /* zero at the yard, fullest two thirds down, held at the foot */ \
    "      float across = 0.35 + 0.65 * cos(cu * 1.5708);\n"               /* the leeches are held by their bowlines */ \
    "      float belly = hw * 0.28 * fn * prof * across;\n" \
    "      if (dir < 0.0 && abs(cnrm.x) > 0.5) belly *= 0.25 + 0.5 * abs(cu);\n"   /* aback: the mast stops the middle of the sail */ \
    "      belly *= dir * (1.0 + 0.06 * sin(u_time * 1.3 + ph0));\n" \
    "      float luff = 1.0 - clamp(mag / 0.35, 0.0, 1.0);\n" \
    "      float fph = u_time * (3.0 + 4.0 * wstr) + cu * 6.0 + cw * 4.0 + ph0;\n" \
    "      float flut = hw * (0.012 + 0.06 * luff) * cw * sin(fph);\n" \
    "      p += cnrm * (belly + flut);\n" \
    "      p.y += hw * 0.01 * cw * sin(fph * 1.31 + 2.2);\n" \
    "      vec3 ct = vec3(-cnrm.z, 0.0, cnrm.x);\n"                        /* across the panel, the way cu grows */ \
    "      cnrm = normalize(cnrm - vec3(0.0, 0.45 * dir * fn * cos(3.14159 * cw * 0.82) * across, 0.0) + ct * (0.6 * dir * fn * sin(cu * 1.5708) * prof));\n" \
    "    }\n" \
    "  }\n"

static const char *meshes_vs =
    "in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;\n"
    "in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx;\n"   // xyz+heading | scale+rgb | sway, highlight, bob, x-stretch | dither, reserved
    "uniform mat4 u_vp; uniform float u_time;\n"
    "uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;\n"
    "uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;\n"
    "out vec3 v_nrm; out vec3 v_col; out vec3 v_wpos; out float v_sel; out vec2 v_rm; out float v_dither;\n"
    GLSL_WAVES
    "void main(){\n"
    "  v_dither = i_fx.x;\n"
    "  float h = i_pos.w; float c = cos(h), s = sin(h);\n"
    "  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;\n"
    GLSL_CLOTH
    /* Sea motion: roll about the keel plus a slower pitch, two frequencies each so the period \
       never reads as a loop. i_misc.x carries how hard the ship is working (moored, under way, \
       or caught in a storm), so anchored ships barely stir while a storm throws them about. */ \
    "  float ph = i_pos.x*3.0 + i_pos.z*2.0;\n"
    "  float roll  = i_misc.x * (sin(u_time*1.30 + ph) + 0.4*sin(u_time*2.10 + ph*1.7)) * 0.045;\n"
    "  float pitch = i_misc.x * (sin(u_time*0.90 + ph*0.6) + 0.3*sin(u_time*1.70 + ph)) * 0.022;\n"
    "  float cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch);\n"
    "  p = vec3(p.x, p.y*cr - p.z*sr, p.y*sr + p.z*cr);\n"
    "  p = vec3(p.x*cp - p.y*sp, p.x*sp + p.y*cp, p.z);\n"
    "  vec3 n = vec3(cnrm.x, cnrm.y*cr - cnrm.z*sr, cnrm.y*sr + cnrm.z*cr);\n"
    "  n = vec3(n.x*cp - n.y*sp, n.x*sp + n.y*cp, n.z);\n"
    "  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;\n"
    "  wp.y += i_misc.z * (sin(u_time*1.10 + i_pos.x*2.0) * 0.02 + sin(u_time*0.47 + i_pos.z*1.3) * 0.012);\n"
    /* Floating instances (i_misc.z, the bob flag) ride the Gerstner swell the sea pass draws:
       the hull is lifted to the surface at its own position and sheared by the surface slope, so
       a ship pitches into a trough instead of sailing through it. The shadow pass applies the
       same transform, so the shadow stays under the ship. */ \
    "  float wamp = u_wave_amp * i_misc.z;\n"
    "  if (wamp > 0.0) wamp *= wave_depth_fade(u_hm, i_pos.xz, u_geo, u_wk, u_wave_scale, u_local_depth);\n"
    "  vec3 wtx, wtz; float wcrest;\n"
    "  vec3 wdisp = wave_sum(i_pos.xz, wamp, u_wave_scale, 0.0, wtx, wtz, wcrest);\n"
    "  vec3 wup = normalize(cross(wtz, wtx));\n"
    "  wp.y += wdisp.y - (wup.x * (wp.x - i_pos.x) + wup.z * (wp.z - i_pos.z)) / max(wup.y, 0.35);\n"
    "  v_nrm = vec3(n.x*c + n.z*s, n.y, -n.x*s + n.z*c);\n"
    "  v_col = mix(a_col, i_scol.yzw, a_mat);\n"
    "  v_wpos = wp; v_sel = i_misc.y; v_rm = vec2(mix(a_rm.x, 0.9, isc), mix(a_rm.y, 0.0, isc));\n"
    "  gl_Position = u_vp * vec4(wp, 1.0);\n"
    "}\n";
static const char *meshes_fs =
    "in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel; in vec2 v_rm; in float v_dither;\n"
    "uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform sampler2D u_grain;\n"
    "out vec4 fragColor;\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_NMAP GLSL_FOG GLSL_DITHER
    "void main(){\n"
    "  dither_clip(v_dither);\n"
    "  vec3 n = normalize(v_nrm);\n"
    "  vec3 albedo = srgb_to_linear(v_col);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    // Two-sided shading: sails, flags and roof slopes are single-quad surfaces drawn without
    // backface culling, so flip the flat normal towards the viewer instead of letting whole
    // faces fall into the ambient-only floor.
    "  if (dot(n, vdir) < 0.0) n = -n;\n"
    // triplanar surface grain, fading with distance so far meshes keep their clean silhouette;
    // rougher materials (wood, cloth, stone) get more relief than painted ones
    "  float dist0 = distance(v_wpos, u_campos);\n"
    "  float grain = v_rm.x * 0.42 * (1.0 - smoothstep(30.0, 120.0, dist0));\n"
    "  if (grain > 0.01) n = nmap_triplanar(u_grain, v_wpos * 1.6, n, 1.0, grain);\n"
    "  float dist = dist0;\n"
    "  vec3 c = sun_shade(albedo, n, vdir, v_wpos, v_rm.x, v_rm.y, dist);\n"
    "  c = mix(c, srgb_to_linear(vec3(1.0,0.95,0.6)) * 1.6, v_sel * 0.35);\n"   // selection tint
    "  c = fog_apply(c, dist, u_mapness, srgb_to_linear(u_sky));\n"
    "  fragColor = vec4(c, 1.0);\n"
    "}\n";

// depth-only variant for the shadow cascades: same instance transform, no shading
static const char *depth_vs =
    "in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;\n"
    "in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx;\n"
    "uniform mat4 u_vp; uniform float u_time;\n"
    "uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;\n"
    "uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_wk;\n"
    "out float v_dither;\n"
    GLSL_WAVES
    "void main(){\n"
    "  v_dither = i_fx.x;\n"
    "  float h = i_pos.w; float c = cos(h), s = sin(h);\n"
    "  vec3 p = a_pos * i_scol.x; if (i_misc.w > 0.0) p.x *= i_misc.w;\n"
    GLSL_CLOTH
    /* Sea motion: roll about the keel plus a slower pitch, two frequencies each so the period \
       never reads as a loop. i_misc.x carries how hard the ship is working (moored, under way, \
       or caught in a storm), so anchored ships barely stir while a storm throws them about. */ \
    "  float ph = i_pos.x*3.0 + i_pos.z*2.0;\n"
    "  float roll  = i_misc.x * (sin(u_time*1.30 + ph) + 0.4*sin(u_time*2.10 + ph*1.7)) * 0.045;\n"
    "  float pitch = i_misc.x * (sin(u_time*0.90 + ph*0.6) + 0.3*sin(u_time*1.70 + ph)) * 0.022;\n"
    "  float cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch);\n"
    "  p = vec3(p.x, p.y*cr - p.z*sr, p.y*sr + p.z*cr);\n"
    "  p = vec3(p.x*cp - p.y*sp, p.x*sp + p.y*cp, p.z);\n"
    "  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + i_pos.xyz;\n"
    "  wp.y += i_misc.z * (sin(u_time*1.10 + i_pos.x*2.0) * 0.02 + sin(u_time*0.47 + i_pos.z*1.3) * 0.012);\n"
    /* Floating instances (i_misc.z, the bob flag) ride the Gerstner swell the sea pass draws:
       the hull is lifted to the surface at its own position and sheared by the surface slope, so
       a ship pitches into a trough instead of sailing through it. The shadow pass applies the
       same transform, so the shadow stays under the ship. */ \
    "  float wamp = u_wave_amp * i_misc.z;\n"
    "  if (wamp > 0.0) wamp *= wave_depth_fade(u_hm, i_pos.xz, u_geo, u_wk, u_wave_scale, u_local_depth);\n"
    "  vec3 wtx, wtz; float wcrest;\n"
    "  vec3 wdisp = wave_sum(i_pos.xz, wamp, u_wave_scale, 0.0, wtx, wtz, wcrest);\n"
    "  vec3 wup = normalize(cross(wtz, wtx));\n"
    "  wp.y += wdisp.y - (wup.x * (wp.x - i_pos.x) + wup.z * (wp.z - i_pos.z)) / max(wup.y, 0.35);\n"
    "  gl_Position = u_vp * vec4(wp, 1.0);\n"
    "}\n";
// Gunsmoke: the same instanced geometry, but shaded as a soft translucent volume instead of a
// solid surface - alpha falls off towards the silhouette, so the sphere reads as a puff rather
// than a ball. Blended, depth-tested but not depth-written, drawn after the sea.
static const char *smoke_fs =
    "in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel; in vec2 v_rm; in float v_dither;\n"
    "uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness;\n"
    "out vec4 fragColor;\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_FOG
    "void main(){\n"
    "  vec3 n = normalize(v_nrm);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    "  if (dot(n, vdir) < 0.0) n = -n;\n"
    "  float facing = max(dot(n, vdir), 0.0);\n"
    "  vec3 albedo = srgb_to_linear(v_col);\n"
    "  float sh = shadow_at(v_wpos, n, max(dot(n, u_sun), 0.0), distance(v_wpos, u_campos));\n"
    // soft, mostly ambient lighting: smoke scatters rather than reflects
    "  vec3 c = albedo * (u_ambsky * 0.9 + u_ambgnd * 0.4 + u_suncol * 0.16 * sh * max(dot(n, u_sun), 0.0));\n"
    "  c = fog_apply(c, distance(v_wpos, u_campos), u_mapness, srgb_to_linear(u_sky));\n"
    "  fragColor = vec4(c, smoothstep(0.0, 0.75, facing) * 0.72);\n"
    "}\n";
static rhi_pipeline_t smoke_prg;

static const char *depth_fs =
    "in float v_dither;\n"
    "out vec4 fragColor;\n"
    GLSL_DITHER
    "void main(){ dither_clip(v_dither); fragColor = vec4(1.0); }\n";   // shadows cross-fade with the meshes
static rhi_pipeline_t depth_prg;

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

static void build_buildings(void) {
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
static void build_puff(void) {
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

void meshes_init(void) {
    static const char *uniforms[] = { "u_vp", "u_time", "u_campos", "u_sky", "u_mapness", "u_grain",
                                      "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hm", "u_geo", "u_wk", GLSL_WAVE_UNIFORMS, GLSL_LIGHT_UNIFORMS, 0 };
    static const char *depth_uniforms[] = { "u_vp", "u_time", "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hm", "u_geo", "u_wk", GLSL_WAVE_UNIFORMS, 0 };
    meshes_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = meshes_vs, .fs = meshes_fs, .layout = &mesh_layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 1 });
    depth_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = depth_vs, .fs = depth_fs, .layout = &mesh_layout, .uniforms = depth_uniforms, .depth_test = 1, .depth_write = 1 });
    smoke_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = meshes_vs, .fs = smoke_fs, .layout = &mesh_layout, .uniforms = uniforms, .depth_test = 1, .depth_write = 0, .blend = RHI_BLEND_ALPHA });
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) build_ship(t, l);
    build_buildings();
    build_puff();
}

// the wave uniforms every instanced pipeline needs so floating instances ride the same surface
// the sea pass draws (render.h: sea_wave_amp / sea_wave_scale / sea_local_depth)
static void meshes_bind_waves(const rhi_pipeline_t *p) {
    sea_bind_waves(p);
    rhi_set_f1(rhi_uniform(p,"u_wave_amp"), sea_amp_now());   // ships ride exactly the zoom-scaled field the sea draws
    rhi_set_f1(rhi_uniform(p,"u_wave_scale"), sea_wave_scale);
    rhi_set_f1(rhi_uniform(p,"u_local_depth"), sea_local_depth);
    rhi_set_i1(rhi_uniform(p,"u_hm"), 4); rhi_texture_bind(4, terrain_heightmap_texture());
    rhi_set_f4(rhi_uniform(p,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(rhi_uniform(p,"u_wk"), WORLD_K);
}

void meshes_bind(void) {
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

void meshes_bind_smoke(void) {
    rhi_pipeline_bind(&smoke_prg);
    meshes_bind_waves(&smoke_prg);
    rhi_set_mat4(UNI(smoke_prg,"u_vp"), &RF.vp.x.x);
    rhi_set_f1(UNI(smoke_prg,"u_time"), RF.time);
    rhi_set_f3(UNI(smoke_prg,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(smoke_prg,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(smoke_prg,"u_mapness"), RF.mapness);
    light_bind(&smoke_prg);
}

void meshes_bind_depth(const mat44 *lightvp) {
    rhi_pipeline_bind(&depth_prg);
    meshes_bind_waves(&depth_prg);
    rhi_set_mat4(UNI(depth_prg,"u_vp"), &lightvp->x.x);
    rhi_set_f1(UNI(depth_prg,"u_time"), RF.time);
}
