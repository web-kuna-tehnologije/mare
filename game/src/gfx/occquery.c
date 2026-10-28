// GPU occlusion queries (see occquery.h).
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "occquery.h"
#include "render.h"
#include "camera.h"
#include "lod.h"
#include "detailview.h"
#include "../core/util.h"
#include "../core/diag.h"

#define OCCQ_TABLE   8192         // instances tracked (power of two)
#define OCCQ_MAX     768          // proxies drawn per frame; the rest count as visible
#define OCCQ_INFLATE 0.06f        // bounding box grown by this fraction (+ OCCQ_PAD units) so the proxy covers the mesh
#define OCCQ_PAD     0.03f
#define OCCQ_STALE   4            // frames without a collected answer before an instance counts as visible again
#define OCCQ_TIMEOUT 90           // frames a query may stay unanswered before it is abandoned and reissued (a driver that never answers must not pin "hidden")
#define OCCQ_MOVE_POS 0.05f       // camera translation per frame that counts as "moving" (world units)
#define OCCQ_MOVE_ANG 0.002f      // and rotation (radians)

typedef struct occq_entry_t {
    unsigned key; unsigned q;
    int frame_seen, frame_answer, frame_issued;   // last frame requested; last frame an answer was collected; frame the pending query was issued
    unsigned char visible, pending, wanted;
    short asset;
    inst_t in;                             // the instance as last requested (position, heading, scale)
} occq_entry_t;

int occq_enabled = 1;
int occq_hidden, occq_queried, occq_tracked, occq_moving, occq_pending, occq_timeouts;
static occq_entry_t occq_table[OCCQ_TABLE];
static int frame_no, active;
static vec3 prev_pos, prev_fwd;
static rhi_pipeline_t proxy_prg; static rhi_geometry_t proxy_geo; static int proxy_init;

static const char *proxy_vs =
    "in vec3 a_pos;\n"
    "uniform mat4 u_vp; uniform vec4 u_pos; uniform vec4 u_bmin; uniform vec4 u_bmax;\n"   // u_pos: xyz + heading, u_bmin/u_bmax: bounds (w: scale)
    "void main(){\n"
    "  vec3 p = mix(u_bmin.xyz, u_bmax.xyz, a_pos) * u_bmin.w;\n"
    "  float c = cos(u_pos.w), s = sin(u_pos.w);\n"
    "  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + u_pos.xyz;\n"
    "  gl_Position = u_vp * vec4(wp, 1.0);\n"
    "}\n";
static const char *proxy_fs = "out vec4 fragColor; void main(){ fragColor = vec4(1.0); }\n";
static const char *proxy_uniforms[] = { "u_vp", "u_pos", "u_bmin", "u_bmax", 0 };

static void proxy_make(void) {
    // unit cube as 12 triangles in [0,1]^3: the vertex shader lerps the bounds with it
    static const float v[36*3] = {
        0,0,0, 1,0,0, 1,1,0,  0,0,0, 1,1,0, 0,1,0,   0,0,1, 1,1,1, 1,0,1,  0,0,1, 0,1,1, 1,1,1,
        0,0,0, 0,1,0, 0,1,1,  0,0,0, 0,1,1, 0,0,1,   1,0,0, 1,1,1, 1,1,0,  1,0,0, 1,0,1, 1,1,1,
        0,0,0, 0,0,1, 1,0,1,  0,0,0, 1,0,1, 1,0,0,   0,1,0, 1,1,0, 1,1,1,  0,1,0, 1,1,1, 0,1,1 };
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 3, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 3 * sizeof(float), 0 } };
    proxy_geo = rhi_geometry_make(&layout, v, 36, NULL, 0);
    proxy_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = proxy_vs, .fs = proxy_fs, .layout = &layout, .uniforms = proxy_uniforms, .depth_test = 1, .depth_write = 0 });
    proxy_init = 1;
    LOG_INFO("gpu", "occlusion queries %s", rhi_occlusion_supported() ? "available: authored buildings are tested against the depth buffer" : "unsupported by this driver");
}

static occq_entry_t *slot(unsigned key, int create) {
    unsigned h = key * 0x9E3779B1u;
    for (int probe = 0; probe < 32; ++probe) {
        occq_entry_t *e = &occq_table[(h + probe) & (OCCQ_TABLE - 1)];
        if (e->key == key) return e;
        if (!e->key) { if (!create) return NULL; memset(e, 0, sizeof *e); e->key = key; e->visible = 1; occq_tracked++; return e; }
        if (create && !e->pending && frame_no - e->frame_seen > 120) { occq_tracked--; memset(e, 0, sizeof *e); e->key = key; e->visible = 1; occq_tracked++; return e; }   // recycle a long-unseen entry
    }
    return NULL;   // cluster full: stateless (visible)
}

void occq_reset(void) { for (int i = 0; i < OCCQ_TABLE; ++i) { occq_table[i].key = 0; occq_table[i].pending = 0; } occq_tracked = 0; }

void occq_frame_begin(void) {
    frame_no++;
    occq_hidden = occq_queried = 0;
    // camera motion: compare with last frame's eye and look direction
    float dp = len3(sub3(cam.pos, prev_pos)), da = len3(sub3(cam.fwd, prev_fwd));
    occq_moving = dp > OCCQ_MOVE_POS || da > OCCQ_MOVE_ANG;
    prev_pos = cam.pos; prev_fwd = cam.fwd;
    static int env_read; if (!env_read) { env_read = 1; const char *e = getenv("MARE_OCCQ"); if (e) { occq_enabled = atoi(e) != 0; LOG_INFO("gpu", "GPU occlusion queries %s (MARE_OCCQ)", occq_enabled ? "on" : "off"); } }
    int tier = lod_tier();
    active = occq_enabled && rhi_occlusion_supported() && cam.mapness < 0.5f && (tier <= TIER_BATTLE || detailview_mode() != DV_OFF);
    // collect answers that came in
    occq_pending = 0;
    for (int i = 0; i < OCCQ_TABLE; ++i) {
        occq_entry_t *e = &occq_table[i];
        if (!e->key || !e->pending) continue;
        int passed;
        if (rhi_query_result(e->q, &passed)) { e->visible = (unsigned char)passed; e->pending = 0; e->frame_answer = frame_no; }
        else if (frame_no - e->frame_issued > OCCQ_TIMEOUT) { e->pending = 0; e->visible = 1; occq_timeouts++; }
        else occq_pending++;
    }
}

int occq_active(void) { return active; }

int occq_visible(unsigned key) {
    if (!active || occq_moving || occq_enabled == 2) return 1;
    const occq_entry_t *e = slot(key, 0);
    if (!e || e->visible) return 1;
    if (frame_no - e->frame_answer > OCCQ_STALE) return 1;    // the answer is old: do not trust it
    return 0;
}

void occq_request(unsigned key, const asset_t *a, const inst_t *in) {
    if (!active) return;
    occq_entry_t *e = slot(key, 1);
    if (!e) return;
    if (e->frame_seen == frame_no) return;                    // two LODs of one instance: one query
    e->frame_seen = frame_no; e->wanted = 1; e->asset = (short)(a - asset_at(0)); e->in = *in;
}

void occq_flush(void) {
    if (!active) return;
    if (!proxy_init) proxy_make();
    if (!rhi_occlusion_supported()) return;
    static int p_occq = -1; if (p_occq < 0) p_occq = prof_id("gfx.occ_queries");
    prof_begin(p_occq);
    rhi_pipeline_bind(&proxy_prg);
    rhi_color_write(0);
    rhi_set_mat4(rhi_uniform(&proxy_prg, "u_vp"), &RF.vp.x.x);
    int l_pos = rhi_uniform(&proxy_prg, "u_pos"), l_bmin = rhi_uniform(&proxy_prg, "u_bmin"), l_bmax = rhi_uniform(&proxy_prg, "u_bmax");
    for (int i = 0; i < OCCQ_TABLE && occq_queried < OCCQ_MAX; ++i) {
        occq_entry_t *e = &occq_table[i];
        if (!e->key || !e->wanted) continue;
        e->wanted = 0;
        if (e->pending) continue;                             // last query still in flight: keep its answer
        const asset_t *a = asset_at(e->asset);
        if (!a || a->state != ASSET_LOADED) continue;
        float sc = e->in.scale > 0 ? e->in.scale : 1.f;
        float ex = (a->bmax[0] - a->bmin[0]) * OCCQ_INFLATE + OCCQ_PAD, ey = (a->bmax[1] - a->bmin[1]) * OCCQ_INFLATE + OCCQ_PAD, ez = (a->bmax[2] - a->bmin[2]) * OCCQ_INFLATE + OCCQ_PAD;
        // the eye inside (or nearly inside) the box: the near plane clips the proxy, so skip it
        float r = a->radius * sc + 1.f;
        if (dist2d(cam.pos.x, cam.pos.z, e->in.x, e->in.z) < r && cam.pos.y < e->in.y + (a->bmax[1] + ey) * sc + 1.f) { e->visible = 1; continue; }
        if (!e->q) e->q = rhi_query_make();
        rhi_set_f4(l_pos, e->in.x, e->in.y, e->in.z, e->in.heading);
        rhi_set_f4(l_bmin, a->bmin[0] - ex, a->bmin[1] - ey, a->bmin[2] - ez, sc);
        rhi_set_f4(l_bmax, a->bmax[0] + ex, a->bmax[1] + ey, a->bmax[2] + ez, 0.f);
        rhi_query_begin(e->q); rhi_draw(&proxy_geo, 0); rhi_query_end();
        e->pending = 1; e->frame_issued = frame_no; occq_queried++;
    }
    for (int i = 0; i < OCCQ_TABLE; ++i) occq_table[i].wanted = 0;  // past the cap: visible this frame, tried again next
    rhi_color_write(1);
    prof_end(p_occq);
}
