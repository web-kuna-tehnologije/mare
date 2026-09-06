// GL helpers (see gpu.h): program creation with attribute binding and uniform caching, GL error
// logging, mesh builder (flat normals), instanced mesh upload/draw, growable instance lists.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gpu.h"
#include "glsl.h"
#include "../core/diag.h"

program_t gpu_program(const char *vs, const char *fs, const char **attribs, const char **uniforms) {
    program_t p = {0};
    const char *v[2] = { GLSL_VERSION, vs }, *f[2] = { GLSL_VERSION, fs };
    unsigned vsh = shader('v', 2, v), fsh = shader('f', 2, f);
    p.id = glCreateProgram();
    glAttachShader(p.id, vsh); glAttachShader(p.id, fsh);
    for (int i = 0; attribs[i]; ++i) glBindAttribLocation(p.id, i, attribs[i]);
    glLinkProgram(p.id);
    GLint ok; glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p.id, sizeof log, 0, log); LOG_FATAL("gpu", "shader link failed: %s", log); }
    int nattr = 0; while (attribs[nattr]) nattr++;
    LOG_DEBUG("gpu", "program %u linked (%d attribs, %d uniforms)", p.id, nattr, p.nuniforms);
    glDeleteShader(vsh); glDeleteShader(fsh);
    for (int i = 0; uniforms && uniforms[i] && p.nuniforms < 24; ++i) { p.names[p.nuniforms] = uniforms[i]; p.locs[p.nuniforms] = glGetUniformLocation(p.id, uniforms[i]); p.nuniforms++; }
    return p;
}
// logs and clears pending GL errors; returns their count
int gpu_check_errors(const char *where) {
    int n = 0; GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR && n < 8) { LOG_ERROR("gpu", "GL error 0x%04x after %s", (unsigned)e, where); n++; }
    return n;
}

int gpu_uniform(const program_t *p, const char *name) {
    for (int i = 0; i < p->nuniforms; ++i) if (p->names[i] == name || !strcmp(p->names[i], name)) return p->locs[i];
    return glGetUniformLocation(p->id, name);
}

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

mesh_t mesh_upload(mb_t *m) {
    mesh_t r = {0};
    glGenVertexArrays(1, &r.vao); glBindVertexArray(r.vao);
    glGenBuffers(1, &r.vbo); glBindBuffer(GL_ARRAY_BUFFER, r.vbo);
    glBufferData(GL_ARRAY_BUFFER, m->n*10*sizeof(float), m->v, GL_STATIC_DRAW);
    int stride = 10*sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, 0, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, 0, stride, (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, 0, stride, (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, 0, stride, (void*)(9*sizeof(float)));
    glGenBuffers(1, &r.ibo); glBindBuffer(GL_ARRAY_BUFFER, r.ibo);
    r.capacity = 256; glBufferData(GL_ARRAY_BUFFER, r.capacity*sizeof(inst_t), 0, GL_STREAM_DRAW);
    for (int a = 0; a < 3; ++a) { glEnableVertexAttribArray(4+a); glVertexAttribPointer(4+a, 4, GL_FLOAT, 0, sizeof(inst_t), (void*)(size_t)(a*4*sizeof(float))); glVertexAttribDivisor(4+a, 1); }
    glBindVertexArray(0);
    r.nverts = m->n;
    free(m->v); m->v = 0; m->n = m->cap = 0;
    return r;
}
void mesh_draw(mesh_t *m, const inst_t *inst, int n) {
    if (n <= 0) return;
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->ibo);
    if (n > m->capacity) { m->capacity = n*2; glBufferData(GL_ARRAY_BUFFER, m->capacity*sizeof(inst_t), 0, GL_STREAM_DRAW); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, n*sizeof(inst_t), inst);
    glDrawArraysInstanced(GL_TRIANGLES, 0, m->nverts, n);
    glBindVertexArray(0);
}
inst_t *instlist_push(instlist_t *l) {
    if (l->n >= l->cap) { l->cap = l->cap ? l->cap*2 : 256; l->v = realloc(l->v, l->cap*sizeof(inst_t)); }
    return &l->v[l->n++];
}
