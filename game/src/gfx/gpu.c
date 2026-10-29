// Mesh helpers (see gpu.h): mesh builder (flat normals), instanced mesh upload/draw via the RHI,
// growable instance lists.
#include <stdlib.h>
#include <math.h>
#include "gpu.h"

// ---------------------------------------------------------------------------
// mesh builder

static void mb_push(mb_t *m, const float *p, const float *n, float r, float g, float b, float mat) {
    if (m->n + 1 > m->cap) { m->cap = m->cap ? m->cap*2 : 1024; m->v = realloc(m->v, m->cap*MB_FLOATS*sizeof(float)); }
    float *o = m->v + (m->n++)*MB_FLOATS;
    o[0]=p[0]; o[1]=p[1]; o[2]=p[2]; o[3]=n[0]; o[4]=n[1]; o[5]=n[2]; o[6]=r; o[7]=g; o[8]=b; o[9]=mat;
    o[10]= m->rough > 0 ? m->rough : 0.8f;   // a zeroed builder means "matte", never a mirror
    o[11]= m->metal;
}
void mb_tri(mb_t *m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float r, float g, float b, float mat) {
    float ux = bx-ax, uy = by-ay, uz = bz-az, vx = cx-ax, vy = cy-ay, vz = cz-az;
    float n[3] = { uy*vz - uz*vy, uz*vx - ux*vz, ux*vy - uy*vx };
    float l = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if (l > 1e-9f) { n[0]/=l; n[1]/=l; n[2]/=l; } else n[1] = 1;
    if (m->nrm_blend > 0) {
        for (int i = 0; i < 3; ++i) n[i] = n[i] * (1 - m->nrm_blend) + m->nrm[i] * m->nrm_blend;
        float q = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if (q > 1e-9f) { n[0]/=q; n[1]/=q; n[2]/=q; }
    }
    float a[3] = {ax,ay,az}, bb[3] = {bx,by,bz}, c[3] = {cx,cy,cz};
    mb_push(m, a, n, r,g,b,mat); mb_push(m, bb, n, r,g,b,mat); mb_push(m, c, n, r,g,b,mat);
}
void mb_quad(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat) {
    mb_tri(m, a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2], r,g,b_,mat);
    mb_tri(m, a[0],a[1],a[2], c[0],c[1],c[2], d[0],d[1],d[2], r,g,b_,mat);
}
void mb_quad2(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat) {
    mb_quad(m, a, b, c, d, r, g, b_, mat);   // one quad: shading is two-sided, a mirrored copy would z-fight
}
// A quad whose metalness - which cloth repurposes as "how free is this vertex to move"
// (meshes.c) - is interpolated from the a/b edge to the c/d edge. A sail built from strips of
// ordinary quads tears along every seam, because the two vertices meeting at a seam would carry
// their own strip's weight and so flutter apart.
void mb_quad_grad(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3],
                  float w_ab, float w_cd, float r, float g, float b_, float mat) {
    float keep = m->metal;
    float n[3] = { 0, 1, 0 };
    {   // one flat normal for the whole quad, as mb_tri would compute for a-b-c
        float ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2];
        float vx = c[0]-a[0], vy = c[1]-a[1], vz = c[2]-a[2];
        n[0] = uy*vz - uz*vy; n[1] = uz*vx - ux*vz; n[2] = ux*vy - uy*vx;
        float l = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (l > 1e-9f) { n[0]/=l; n[1]/=l; n[2]/=l; } else { n[0]=0; n[1]=1; n[2]=0; }
    }
    const float *v[4] = { a, b, c, d };
    const float w[4] = { w_ab, w_ab, w_cd, w_cd };
    const int tri[6] = { 0,1,2, 0,2,3 };
    for (int i = 0; i < 6; ++i) { m->metal = w[tri[i]]; mb_push(m, v[tri[i]], n, r, g, b_, mat); }
    m->metal = keep;
}

void mb_quad_cloth(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], const float n[3],
                   float code_a, float code_b, float code_c, float code_d, float halfw, float r, float g, float b_, float mat) {
    float keep_m = m->metal, keep_r = m->rough;
    const float *v[4] = { a, b, c, d };
    const float w[4] = { code_a, code_b, code_c, code_d };
    const int tri[6] = { 0,1,2, 0,2,3 };
    m->rough = halfw > 0.f ? halfw : 0.001f;    // mb_push treats 0 as "unset"
    for (int i = 0; i < 6; ++i) { m->metal = w[tri[i]]; mb_push(m, v[tri[i]], n, r, g, b_, mat); }
    m->metal = keep_m; m->rough = keep_r;
}

void mb_box(mb_t *m, float cx, float cy, float cz, float hx, float hy, float hz, float r, float g, float b, float mat) {
    float p[8][3]; for (int i = 0; i < 8; ++i) { p[i][0] = cx + (i&1 ? hx : -hx); p[i][1] = cy + (i&2 ? hy : -hy); p[i][2] = cz + (i&4 ? hz : -hz); }
    mb_quad(m, p[0],p[2],p[3],p[1], r,g,b,mat); mb_quad(m, p[4],p[5],p[7],p[6], r,g,b,mat);
    mb_quad(m, p[0],p[4],p[6],p[2], r,g,b,mat); mb_quad(m, p[1],p[3],p[7],p[5], r,g,b,mat);
    mb_quad(m, p[2],p[6],p[7],p[3], r,g,b,mat); mb_quad(m, p[0],p[1],p[5],p[4], r,g,b,mat);
}

// ---------------------------------------------------------------------------
// meshes & instancing

static const rhi_attr_t mesh_attrs[] = {
    { "a_pos", 0, 3,  0, 0 }, { "a_nrm", 0, 3, 12, 0 }, { "a_col", 0, 3, 24, 0 }, { "a_mat", 0, 1, 36, 0 },
    { "a_rm", 0, 2, 40, 0 },                                      // roughness, metalness
    { "i_pos", 1, 4,  0, 1 }, { "i_scol", 1, 4, 16, 1 }, { "i_misc", 1, 4, 32, 1 }, { "i_fx", 1, 4, 48, 1 },
};
const rhi_layout_t mesh_layout = { mesh_attrs, 9, { MB_FLOATS*sizeof(float), sizeof(inst_t) } };

mesh_t mesh_upload(mb_t *m) {
    mesh_t r = { rhi_geometry_make(&mesh_layout, m->v, m->n, 0, 0) };
    free(m->v); m->v = 0; m->n = m->cap = 0;
    return r;
}
mesh_t mesh_upload_indexed(const float *verts, int nverts, const unsigned *idx, int nidx) {
    mesh_t r = { rhi_geometry_make(&mesh_layout, verts, nverts, idx, nidx) };
    return r;
}
void mesh_set_instances(mesh_t *m, const inst_t *inst, int n) {
    m->ninst = n;
    if (n > 0) rhi_geometry_instances(&m->geo, inst, n);
}
void mesh_draw(mesh_t *m) {
    if (m->ninst > 0) rhi_draw(&m->geo, m->ninst);
}
inst_t *instlist_push(instlist_t *l) {
    if (l->n >= l->cap) { l->cap = l->cap ? l->cap*2 : 256; l->v = realloc(l->v, l->cap*sizeof(inst_t)); }
    return &l->v[l->n++];
}
