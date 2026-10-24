// Mesh helpers (see gpu.h): mesh builder (flat normals), instanced mesh upload/draw via the RHI,
// growable instance lists.
#include <stdlib.h>
#include <math.h>
#include "gpu.h"

// ---------------------------------------------------------------------------
// mesh builder

static void mb_push(mb_t *m, const float *p, const float *n, float r, float g, float b, float mat) {
    if (m->n + 1 > m->cap) { m->cap = m->cap ? m->cap*2 : 1024; m->v = realloc(m->v, m->cap*10*sizeof(float)); }
    float *o = m->v + (m->n++)*10;
    o[0]=p[0]; o[1]=p[1]; o[2]=p[2]; o[3]=n[0]; o[4]=n[1]; o[5]=n[2]; o[6]=r; o[7]=g; o[8]=b; o[9]=mat;
}
void mb_tri(mb_t *m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float r, float g, float b, float mat) {
    float ux = bx-ax, uy = by-ay, uz = bz-az, vx = cx-ax, vy = cy-ay, vz = cz-az;
    float n[3] = { uy*vz - uz*vy, uz*vx - ux*vz, ux*vy - uy*vx };
    float l = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if (l > 1e-9f) { n[0]/=l; n[1]/=l; n[2]/=l; } else n[1] = 1;
    float a[3] = {ax,ay,az}, bb[3] = {bx,by,bz}, c[3] = {cx,cy,cz};
    mb_push(m, a, n, r,g,b,mat); mb_push(m, bb, n, r,g,b,mat); mb_push(m, c, n, r,g,b,mat);
}
void mb_quad(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat) {
    mb_tri(m, a[0],a[1],a[2], b[0],b[1],b[2], c[0],c[1],c[2], r,g,b_,mat);
    mb_tri(m, a[0],a[1],a[2], c[0],c[1],c[2], d[0],d[1],d[2], r,g,b_,mat);
}
void mb_quad2(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat) {
    mb_quad(m, a, b, c, d, r, g, b_, mat); mb_quad(m, d, c, b, a, r, g, b_, mat);
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
    { "i_pos", 1, 4,  0, 1 }, { "i_scol", 1, 4, 16, 1 }, { "i_misc", 1, 4, 32, 1 },
};
const rhi_layout_t mesh_layout = { mesh_attrs, 7, { 10*sizeof(float), sizeof(inst_t) } };

mesh_t mesh_upload(mb_t *m) {
    mesh_t r = { rhi_geometry_make(&mesh_layout, m->v, m->n, 0, 0) };
    free(m->v); m->v = 0; m->n = m->cap = 0;
    return r;
}
void mesh_draw(mesh_t *m, const inst_t *inst, int n) {
    if (n <= 0) return;
    rhi_geometry_instances(&m->geo, inst, n);
    rhi_draw(&m->geo, n);
}
inst_t *instlist_push(instlist_t *l) {
    if (l->n >= l->cap) { l->cap = l->cap ? l->cap*2 : 256; l->v = realloc(l->v, l->cap*sizeof(inst_t)); }
    return &l->v[l->n++];
}
