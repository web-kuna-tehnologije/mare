// glTF 2.0 / GLB loader - see gltf.h for scope. JSON via gjson.h, base64 for data: URIs,
// accessor readers that widen every component type to float/u32, and a scene walk that bakes
// node transforms into flat vertex streams.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "gltf.h"
#include "gjson.h"
}

// --------------------------------------------------------------------------
// loader state

#define GLTF_MAX_VERTS   (4 * 1024 * 1024)   // import validation: reject runaway models
#define GLTF_MAX_BUFFERS 16

typedef struct gbuf_t { unsigned char *data; size_t size; int owned; } gbuf_t;
typedef struct gld_t {
    gjson_t j;
    int buffers, views, accessors, meshes, materials, nodes, scenes, images, textures;   // root token indices
    gbuf_t buf[GLTF_MAX_BUFFERS]; int nbuf;
    const char *dir;
    gltf_model_t *m;
} gld_t;

typedef struct bake_t {
    float *pos, *nrm, *col, *uv; int nverts, vcap;   // vcap counts vertices, shared by all four streams
    unsigned *idx; int nidx, icap;
    gltf_prim_t *prims; int nprims, pcap;
} bake_t;

namespace mare::engine::assets {

class Gltf {
public:
    int  load_file(const char *path, gltf_model_t *m);
    int  load_memory(const void *data, size_t size, const char *dir, gltf_model_t *m);
    void release(gltf_model_t *m);

private:
    static int gfail(gld_t *g, const char *fmt, const char *a);
    static int b64val(char c);
    static unsigned char *b64_decode(const char *s, int n, size_t *out_n);
    static unsigned char *read_whole_file(const char *path, size_t *out_n);
    static gbuf_t *gltf_buffer(gld_t *g, int index);
    static int gltf_comp_size(int ct);
    static int gltf_type_comps(const gjson_t *j, int tok);
    static const unsigned char *gltf_accessor(gld_t *g, int ai, int *count, int *comps, int *ctype, int *stride);
    static int gltf_read_floats(gld_t *g, int ai, int want, float *out, int expect_count);
    static int gltf_read_indices(gld_t *g, int ai, unsigned **out);
    static void mat_identity(float *m);
    static void mat_mul(float *out, const float *a, const float *b);
    static void gltf_node_matrix(const gjson_t *j, int node, float *m);
    static void *gpush(void **v, int *n, int *cap, size_t elem, int add);
    static void bake_grow_verts(bake_t *bk, int add);
    static int gltf_bake_mesh(gld_t *g, bake_t *bk, int mesh_index, const float *M, const char *node_name);
    static int gltf_bake_node(gld_t *g, bake_t *bk, int node_index, const float *parent, int depth);
    static int gltf_parse(gld_t *g, const char *json, size_t json_len);
};

Gltf &gltf() { static Gltf g; return g; }

int Gltf::gfail(gld_t *g, const char *fmt, const char *a) {
    snprintf(g->m->error, sizeof g->m->error, fmt, a ? a : "");
    return 0;
}

int Gltf::b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
unsigned char *Gltf::b64_decode(const char *s, int n, size_t *out_n) {
    unsigned char *out = (unsigned char *)malloc((size_t)n / 4 * 3 + 3);
    size_t o = 0; int acc = 0, bits = 0;
    for (int i = 0; i < n; ++i) {
        int v = b64val(s[i]);
        if (v < 0) { if (s[i] == '=') break; continue; }
        acc = acc << 6 | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)(acc >> bits); }
    }
    *out_n = o;
    return out;
}

unsigned char *Gltf::read_whole_file(const char *path, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *data = (unsigned char *)malloc((size_t)n + 1);
    size_t rd = fread(data, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) { free(data); return NULL; }
    data[n] = 0;
    *out_n = (size_t)n;
    return data;
}

// resolve buffers[i] lazily: GLB BIN chunk (no uri), data: URI, or external file in g->dir
gbuf_t *Gltf::gltf_buffer(gld_t *g, int index) {
    if (index < 0 || index >= GLTF_MAX_BUFFERS) return NULL;
    if (g->buf[index].data) return &g->buf[index];
    int b = jat(&g->j, g->buffers, index);
    if (b < 0) return NULL;
    int uri = jget(&g->j, b, "uri");
    if (uri < 0) return NULL;   // BIN chunk is pre-filled for GLB; missing elsewhere is an error
    const gjson_t *j = &g->j;
    const char *s = j->s + j->t[uri].start; int n = j->t[uri].len;
    if (n > 5 && !strncmp(s, "data:", 5)) {
        const char *comma = (const char *)memchr(s, ',', (size_t)n);
        if (!comma) return NULL;
        g->buf[index].data = b64_decode(comma + 1, (int)(n - (comma + 1 - s)), &g->buf[index].size);
        g->buf[index].owned = 1;
        return &g->buf[index];
    }
    if (!g->dir) return NULL;
    char path[512], name[256];
    jstrcpy(j, uri, name, sizeof name);
    if (strstr(name, "..") || name[0] == '/' || strstr(name, "://")) return NULL;   // stay inside the asset dir
    snprintf(path, sizeof path, "%s/%s", g->dir, name);
    g->buf[index].data = read_whole_file(path, &g->buf[index].size);
    g->buf[index].owned = 1;
    return g->buf[index].data ? &g->buf[index] : NULL;
}

// --------------------------------------------------------------------------
// accessors

int Gltf::gltf_comp_size(int ct) {
    switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; }
    return 0;
}
int Gltf::gltf_type_comps(const gjson_t *j, int tok) {
    if (jstreq(j, tok, "SCALAR")) return 1;
    if (jstreq(j, tok, "VEC2")) return 2;
    if (jstreq(j, tok, "VEC3")) return 3;
    if (jstreq(j, tok, "VEC4")) return 4;
    if (jstreq(j, tok, "MAT4")) return 16;
    return 0;
}

// base pointer + stride for accessor `ai`; fills count/comps/comptype; NULL on any inconsistency
const unsigned char *Gltf::gltf_accessor(gld_t *g, int ai, int *count, int *comps, int *ctype, int *stride) {
    const gjson_t *j = &g->j;
    int a = jat(j, g->accessors, ai);
    if (a < 0) return NULL;
    if (jget(j, a, "sparse") >= 0) { gfail(g, "sparse accessors are not supported%s", NULL); return NULL; }
    *count = jint(j, jget(j, a, "count"), 0);
    *comps = gltf_type_comps(j, jget(j, a, "type"));
    *ctype = jint(j, jget(j, a, "componentType"), 0);
    int csz = gltf_comp_size(*ctype);
    if (*count <= 0 || !*comps || !csz) return NULL;
    int v = jat(j, g->views, jint(j, jget(j, a, "bufferView"), -1));
    if (v < 0) return NULL;
    gbuf_t *buf = gltf_buffer(g, jint(j, jget(j, v, "buffer"), 0));
    if (!buf) { gfail(g, "buffer data missing (external .bin not found or bad data: uri)%s", NULL); return NULL; }
    size_t off = (size_t)jint(j, jget(j, v, "byteOffset"), 0) + (size_t)jint(j, jget(j, a, "byteOffset"), 0);
    *stride = jint(j, jget(j, v, "byteStride"), 0);
    if (!*stride) *stride = csz * *comps;
    size_t need = off + (size_t)(*count - 1) * (size_t)*stride + (size_t)csz * (size_t)*comps;
    if (need > buf->size) { gfail(g, "accessor reads past the end of its buffer%s", NULL); return NULL; }
    return buf->data + off;
}

// read accessor `ai` as floats, `want` components per element (missing comps 0, extras dropped).
// Integer types are normalized when the accessor says so, else cast.
int Gltf::gltf_read_floats(gld_t *g, int ai, int want, float *out, int expect_count) {
    int count, comps, ctype, stride;
    const unsigned char *p = gltf_accessor(g, ai, &count, &comps, &ctype, &stride);
    if (!p || (expect_count && count != expect_count)) return 0;
    int a = jat(&g->j, g->accessors, ai);
    int norm = jbool(&g->j, jget(&g->j, a, "normalized"), 0);
    for (int i = 0; i < count; ++i, p += stride)
        for (int c = 0; c < want; ++c) {
            float v = 0;
            if (c < comps) switch (ctype) {
                case 5126: { float f; memcpy(&f, p + 4*c, 4); v = f; } break;
                case 5120: { signed char x = (signed char)p[c];                     v = norm ? (x < -127 ? -1.f : x / 127.f) : (float)x; } break;
                case 5121: { unsigned char x = p[c];                                v = norm ? x / 255.f : (float)x; } break;
                case 5122: { short x; memcpy(&x, p + 2*c, 2);                       v = norm ? (x < -32767 ? -1.f : x / 32767.f) : (float)x; } break;
                case 5123: { unsigned short x; memcpy(&x, p + 2*c, 2);              v = norm ? x / 65535.f : (float)x; } break;
                case 5125: { unsigned x; memcpy(&x, p + 4*c, 4);                    v = (float)x; } break;
            }
            out[i * want + c] = v;
        }
    return count;
}

int Gltf::gltf_read_indices(gld_t *g, int ai, unsigned **out) {   // returns count, 0 on error
    int count, comps, ctype, stride;
    const unsigned char *p = gltf_accessor(g, ai, &count, &comps, &ctype, &stride);
    if (!p || comps != 1) return 0;
    unsigned *idx = (unsigned *)malloc((size_t)count * 4);
    for (int i = 0; i < count; ++i, p += stride) {
        switch (ctype) {
            case 5121: idx[i] = p[0]; break;
            case 5123: { unsigned short x; memcpy(&x, p, 2); idx[i] = x; } break;
            case 5125: { unsigned x; memcpy(&x, p, 4); idx[i] = x; } break;
            default: free(idx); return 0;
        }
    }
    *out = idx;
    return count;
}

// --------------------------------------------------------------------------
// node transforms: column-major 4x4, glTF conventions (p' = M * p)

void Gltf::mat_identity(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
void Gltf::mat_mul(float *out, const float *a, const float *b) {   // out = a * b
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rI = 0; rI < 4; ++rI) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k*4 + rI] * b[c*4 + k];
            r[c*4 + rI] = s;
        }
    memcpy(out, r, 64);
}
void Gltf::gltf_node_matrix(const gjson_t *j, int node, float *m) {
    mat_identity(m);
    int mt = jget(j, node, "matrix");
    if (mt >= 0) { jfloats(j, mt, m, 16); return; }
    float t[3] = {0,0,0}, r[4] = {0,0,0,1}, s[3] = {1,1,1};
    jfloats(j, jget(j, node, "translation"), t, 3);
    jfloats(j, jget(j, node, "rotation"), r, 4);
    jfloats(j, jget(j, node, "scale"), s, 3);
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float rot[16]; mat_identity(rot);
    rot[0] = 1 - 2*(y*y + z*z); rot[1] = 2*(x*y + z*w);     rot[2] = 2*(x*z - y*w);
    rot[4] = 2*(x*y - z*w);     rot[5] = 1 - 2*(x*x + z*z); rot[6] = 2*(y*z + x*w);
    rot[8] = 2*(x*z + y*w);     rot[9] = 2*(y*z - x*w);     rot[10] = 1 - 2*(x*x + y*y);
    for (int c = 0; c < 3; ++c) for (int rI = 0; rI < 3; ++rI) rot[c*4 + rI] *= s[c];
    memcpy(m, rot, 64);
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
}

// --------------------------------------------------------------------------
// scene bake

void *Gltf::gpush(void **v, int *n, int *cap, size_t elem, int add) {   // grow-append, returns first new slot
    if (*n + add > *cap) { *cap = *cap ? *cap : 64; while (*cap < *n + add) *cap *= 2; *v = realloc(*v, (size_t)*cap * elem); }
    void *p = (char *)*v + (size_t)*n * elem;
    *n += add;
    return p;
}

void Gltf::bake_grow_verts(bake_t *bk, int add) {
    if (bk->nverts + add > bk->vcap) {
        bk->vcap = bk->vcap ? bk->vcap : 1024;
        while (bk->vcap < bk->nverts + add) bk->vcap *= 2;
        bk->pos = (float *)realloc(bk->pos, (size_t)bk->vcap * 12);
        bk->nrm = (float *)realloc(bk->nrm, (size_t)bk->vcap * 12);
        bk->col = (float *)realloc(bk->col, (size_t)bk->vcap * 12);
        bk->uv  = (float *)realloc(bk->uv,  (size_t)bk->vcap * 8);
    }
}

int Gltf::gltf_bake_mesh(gld_t *g, bake_t *bk, int mesh_index, const float *M, const char *node_name) {
    const gjson_t *j = &g->j;
    int mesh = jat(j, g->meshes, mesh_index);
    if (mesh < 0) return gfail(g, "node references a missing mesh%s", NULL);
    char mesh_name[64];
    jstrcpy(j, jget(j, mesh, "name"), mesh_name, sizeof mesh_name);
    int prims = jget(j, mesh, "primitives");
    for (int pi = 0; pi < jlen(j, prims); ++pi) {
        int prim = jat(j, prims, pi);
        if (jint(j, jget(j, prim, "mode"), 4) != 4)
            return gfail(g, "mesh '%s' uses a non-triangle primitive mode", mesh_name);
        int attrs = jget(j, prim, "attributes");
        int a_pos = jget(j, attrs, "POSITION");
        if (a_pos < 0) return gfail(g, "mesh '%s' primitive has no POSITION attribute", mesh_name);

        int count, comps, ctype, stride;
        if (!gltf_accessor(g, jint(j, a_pos, -1), &count, &comps, &ctype, &stride))
            return g->m->error[0] ? 0 : gfail(g, "mesh '%s' has a broken POSITION accessor", mesh_name);
        if (bk->nverts + count > GLTF_MAX_VERTS) return gfail(g, "model exceeds the vertex budget%s", NULL);

        int base = bk->nverts;
        bake_grow_verts(bk, count);
        float *pos = bk->pos + base*3, *nrm = bk->nrm + base*3, *col = bk->col + base*3, *uv = bk->uv + base*2;
        bk->nverts += count;

        if (!gltf_read_floats(g, jint(j, a_pos, -1), 3, pos, count))
            return g->m->error[0] ? 0 : gfail(g, "mesh '%s' POSITION data is unreadable", mesh_name);
        int a_nrm = jget(j, attrs, "NORMAL"), has_nrm = 0;
        if (a_nrm >= 0 && gltf_read_floats(g, jint(j, a_nrm, -1), 3, nrm, count)) has_nrm = 1;
        if (g->m->error[0]) return 0;
        int a_col = jget(j, attrs, "COLOR_0");
        if (a_col < 0 || !gltf_read_floats(g, jint(j, a_col, -1), 3, col, count))
            for (int i = 0; i < count * 3; ++i) col[i] = 1.f;
        if (g->m->error[0]) return 0;
        int a_uv = jget(j, attrs, "TEXCOORD_0");
        if (a_uv < 0 || !gltf_read_floats(g, jint(j, a_uv, -1), 2, uv, count))
            memset(uv, 0, (size_t)count * 8);
        if (g->m->error[0]) return 0;

        // bake the world transform; normals through the rotation+scale part, renormalized
        // (exact for uniform scale, a fine approximation for the mild non-uniform case)
        for (int i = 0; i < count; ++i) {
            float *p = pos + i*3, *n = nrm + i*3;
            float px = p[0], py = p[1], pz = p[2];
            p[0] = M[0]*px + M[4]*py + M[8]*pz  + M[12];
            p[1] = M[1]*px + M[5]*py + M[9]*pz  + M[13];
            p[2] = M[2]*px + M[6]*py + M[10]*pz + M[14];
            if (has_nrm) {
                float nx = n[0], ny = n[1], nz = n[2];
                n[0] = M[0]*nx + M[4]*ny + M[8]*nz;
                n[1] = M[1]*nx + M[5]*ny + M[9]*nz;
                n[2] = M[2]*nx + M[6]*ny + M[10]*nz;
                float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (l > 1e-8f) { n[0] /= l; n[1] /= l; n[2] /= l; }
            }
        }

        // indices (generated 0..n-1 when absent), rebased onto the shared streams
        unsigned *pidx = NULL; int nidx;
        int a_idx = jget(j, prim, "indices");
        if (a_idx >= 0) {
            nidx = gltf_read_indices(g, jint(j, a_idx, -1), &pidx);
            if (!nidx) return g->m->error[0] ? 0 : gfail(g, "mesh '%s' has unreadable indices", mesh_name);
        } else {
            nidx = count;
            pidx = (unsigned *)malloc((size_t)nidx * 4);
            for (int i = 0; i < nidx; ++i) pidx[i] = (unsigned)i;
        }
        if (nidx % 3) { free(pidx); return gfail(g, "mesh '%s' index count is not a multiple of 3", mesh_name); }
        for (int i = 0; i < nidx; ++i)
            if (pidx[i] >= (unsigned)count) { free(pidx); return gfail(g, "mesh '%s' has an index out of range", mesh_name); }

        int first = bk->nidx;
        unsigned *dst = (unsigned *)gpush((void **)&bk->idx, &bk->nidx, &bk->icap, 4, nidx);
        for (int i = 0; i < nidx; ++i) dst[i] = pidx[i] + (unsigned)base;
        free(pidx);

        if (!has_nrm) {   // flat normals from the baked triangles, area-weighted per vertex
            memset(nrm, 0, (size_t)count * 12);
            for (int i = 0; i < nidx; i += 3) {
                float *a = bk->pos + dst[i]*3, *b = bk->pos + dst[i+1]*3, *c = bk->pos + dst[i+2]*3;
                float e1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]}, e2[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
                float fn[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
                for (int k = 0; k < 3; ++k) for (int c3 = 0; c3 < 3; ++c3) bk->nrm[dst[i+k]*3 + c3] += fn[c3];
            }
            for (int i = 0; i < count; ++i) {
                float *n = nrm + i*3;
                float l = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (l > 1e-8f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 1; n[2] = 0; }
            }
        }

        gltf_prim_t *pr = (gltf_prim_t *)gpush((void **)&bk->prims, &bk->nprims, &bk->pcap, sizeof *pr, 1);
        pr->material = jint(j, jget(j, prim, "material"), -1);
        pr->first_index = first; pr->nindices = nidx;
        memcpy(pr->mesh_name, mesh_name, sizeof pr->mesh_name);
        snprintf(pr->node_name, sizeof pr->node_name, "%s", node_name);
    }
    return 1;
}

int Gltf::gltf_bake_node(gld_t *g, bake_t *bk, int node_index, const float *parent, int depth) {
    const gjson_t *j = &g->j;
    if (depth > 64) return gfail(g, "node hierarchy too deep (cycle?)%s", NULL);
    int node = jat(j, g->nodes, node_index);
    if (node < 0) return gfail(g, "scene references a missing node%s", NULL);
    float local[16], M[16];
    gltf_node_matrix(j, node, local);
    mat_mul(M, parent, local);
    int mesh = jget(j, node, "mesh");
    if (mesh >= 0) {
        char name[64];
        jstrcpy(j, jget(j, node, "name"), name, sizeof name);
        if (!gltf_bake_mesh(g, bk, jint(j, mesh, -1), M, name)) return 0;
    }
    int children = jget(j, node, "children");
    for (int i = 0; i < jlen(j, children); ++i)
        if (!gltf_bake_node(g, bk, jint(j, jat(j, children, i), -1), M, depth + 1)) return 0;
    return 1;
}

// --------------------------------------------------------------------------
// top level

int Gltf::gltf_parse(gld_t *g, const char *json, size_t json_len) {
    gltf_model_t *m = g->m;
    if (gjson_parse(&g->j, json, (int)json_len) < 0) return gfail(g, "invalid JSON%s", NULL);
    const gjson_t *j = &g->j;
    g->buffers   = jget(j, 0, "buffers");
    g->views     = jget(j, 0, "bufferViews");
    g->accessors = jget(j, 0, "accessors");
    g->meshes    = jget(j, 0, "meshes");
    g->materials = jget(j, 0, "materials");
    g->nodes     = jget(j, 0, "nodes");
    g->scenes    = jget(j, 0, "scenes");
    g->images    = jget(j, 0, "images");
    g->textures  = jget(j, 0, "textures");
    int asset = jget(j, 0, "asset");
    if (asset < 0 || !jstreq(j, jget(j, asset, "version"), "2.0"))
        return gfail(g, "not a glTF 2.0 asset%s", NULL);
    int req = jget(j, 0, "extensionsRequired");
    if (jlen(j, req) > 0) {
        char ext[64];
        jstrcpy(j, jat(j, req, 0), ext, sizeof ext);
        return gfail(g, "required extension '%s' is not supported", ext);   // Draco etc.
    }

    // materials
    m->nmats = jlen(j, g->materials);
    if (m->nmats) {
        m->mats = (gltf_material_t *)calloc((size_t)m->nmats, sizeof *m->mats);
        for (int i = 0; i < m->nmats; ++i) {
            gltf_material_t *mt = &m->mats[i];
            int mat = jat(j, g->materials, i);
            jstrcpy(j, jget(j, mat, "name"), mt->name, sizeof mt->name);
            mt->base_color[0] = mt->base_color[1] = mt->base_color[2] = mt->base_color[3] = 1;
            mt->rough = 1; mt->metal = 1;   // glTF defaults
            mt->double_sided = jbool(j, jget(j, mat, "doubleSided"), 0);
            mt->base_tex = -1;
            int pbr = jget(j, mat, "pbrMetallicRoughness");
            if (pbr >= 0) {
                jfloats(j, jget(j, pbr, "baseColorFactor"), mt->base_color, 4);
                mt->rough = (float)jnum(j, jget(j, pbr, "roughnessFactor"), 1);
                mt->metal = (float)jnum(j, jget(j, pbr, "metallicFactor"), 1);
                int tex = jat(j, g->textures, jint(j, jget(j, jget(j, pbr, "baseColorTexture"), "index"), -1));
                if (tex >= 0) mt->base_tex = jint(j, jget(j, tex, "source"), -1);
                if (mt->base_tex >= jlen(j, g->images)) mt->base_tex = -1;
            }
        }
    }

    // images: copied out as encoded bytes (bufferView, data: uri or external file)
    m->nimages = jlen(j, g->images);
    if (m->nimages) {
        m->images = (gltf_image_t *)calloc((size_t)m->nimages, sizeof *m->images);
        for (int i = 0; i < m->nimages; ++i) {
            gltf_image_t *im = &m->images[i];
            int img = jat(j, g->images, i);
            jstrcpy(j, jget(j, img, "name"), im->name, sizeof im->name);
            jstrcpy(j, jget(j, img, "mimeType"), im->mime, sizeof im->mime);
            int bv = jat(j, g->views, jint(j, jget(j, img, "bufferView"), -1));
            int uri = jget(j, img, "uri");
            if (bv >= 0) {
                gbuf_t *buf = gltf_buffer(g, jint(j, jget(j, bv, "buffer"), 0));
                size_t off = (size_t)jint(j, jget(j, bv, "byteOffset"), 0), len = (size_t)jint(j, jget(j, bv, "byteLength"), 0);
                if (!buf || off + len > buf->size) return gfail(g, "image '%s' bufferView is out of range", im->name);
                im->data = (unsigned char *)malloc(len ? len : 1);
                memcpy(im->data, buf->data + off, len);
                im->size = len;
            } else if (uri >= 0) {
                const char *s = j->s + j->t[uri].start; int n = j->t[uri].len;
                if (n > 5 && !strncmp(s, "data:", 5)) {
                    const char *comma = (const char *)memchr(s, ',', (size_t)n);
                    if (!comma) return gfail(g, "image '%s' has a malformed data: uri", im->name);
                    im->data = b64_decode(comma + 1, (int)(n - (comma + 1 - s)), &im->size);
                    const char *semi = (const char *)memchr(s, ';', (size_t)(comma - s));
                    if (semi && !im->mime[0]) snprintf(im->mime, sizeof im->mime, "%.*s", (int)(semi - s - 5), s + 5);
                } else if (g->dir) {
                    char name[256], path[512];
                    jstrcpy(j, uri, name, sizeof name);
                    if (strstr(name, "..") || name[0] == '/' || strstr(name, "://")) return gfail(g, "image uri '%s' escapes the asset directory", name);
                    snprintf(path, sizeof path, "%s/%s", g->dir, name);
                    im->data = read_whole_file(path, &im->size);
                    if (!im->data) return gfail(g, "image file '%s' not found", name);
                } else return gfail(g, "image '%s' needs an external file but no directory was given", im->name);
            }   // an image with neither is allowed (unused by any material)
        }
    }

    // bake the default scene (or scene 0, or - per spec - every root node when none given)
    bake_t bk; memset(&bk, 0, sizeof bk);
    float I[16]; mat_identity(I);
    int scene = jat(j, g->scenes, jint(j, jget(j, 0, "scene"), 0));
    int ok = 1;
    if (scene >= 0) {
        int roots = jget(j, scene, "nodes");
        for (int i = 0; ok && i < jlen(j, roots); ++i)
            ok = gltf_bake_node(g, &bk, jint(j, jat(j, roots, i), -1), I, 0);
    } else {
        for (int i = 0; ok && i < jlen(j, g->nodes); ++i)
            ok = gltf_bake_node(g, &bk, i, I, 0);   // no scenes: treat every node as a root
    }
    m->pos = bk.pos; m->nrm = bk.nrm; m->col = bk.col; m->uv = bk.uv; m->nverts = bk.nverts;
    m->idx = bk.idx; m->nidx = bk.nidx;
    m->prims = bk.prims; m->nprims = bk.nprims;
    if (!ok) return 0;
    if (!m->nverts || !m->nidx) return gfail(g, "model contains no triangles%s", NULL);

    // import validation: material references, finite geometry, bounds
    for (int i = 0; i < m->nprims; ++i)
        if (m->prims[i].material >= m->nmats)
            return gfail(g, "primitive references a missing material%s", NULL);
    m->bmin[0] = m->bmin[1] = m->bmin[2] = 1e30f;
    m->bmax[0] = m->bmax[1] = m->bmax[2] = -1e30f;
    for (int i = 0; i < m->nverts * 3; ++i) {
        float v = m->pos[i];
        if (!(v == v) || v > 1e30f || v < -1e30f) return gfail(g, "model contains non-finite positions%s", NULL);
        int c = i % 3;
        if (v < m->bmin[c]) m->bmin[c] = v;
        if (v > m->bmax[c]) m->bmax[c] = v;
    }
    return 1;
}

int Gltf::load_memory(const void *data, size_t size, const char *dir, gltf_model_t *m) {
    memset(m, 0, sizeof *m);
    gld_t g; memset(&g, 0, sizeof g);
    g.m = m; g.dir = dir;
    const unsigned char *p = (const unsigned char *)data;
    const char *json = NULL; size_t json_len = 0;
    int ok = 0;
    if (size >= 12 && !memcmp(p, "glTF", 4)) {   // GLB container
        unsigned version, total;
        memcpy(&version, p + 4, 4); memcpy(&total, p + 8, 4);
        if (version != 2) { gfail(&g, "unsupported GLB version%s", NULL); goto done; }
        if (total > size) { gfail(&g, "GLB file is truncated%s", NULL); goto done; }
        size_t off = 12;
        while (off + 8 <= total) {
            unsigned clen, ctype;
            memcpy(&clen, p + off, 4); memcpy(&ctype, p + off + 4, 4);
            off += 8;
            if (off + clen > total) { gfail(&g, "GLB chunk is truncated%s", NULL); goto done; }
            if (ctype == 0x4E4F534A) { json = (const char *)(p + off); json_len = clen; }       // 'JSON'
            else if (ctype == 0x004E4942) { g.buf[0].data = (unsigned char *)(p + off); g.buf[0].size = clen; }  // 'BIN'
            off += clen;
        }
        if (!json) { gfail(&g, "GLB has no JSON chunk%s", NULL); goto done; }
    } else {                                     // plain JSON .gltf
        json = (const char *)data; json_len = size;
    }
    ok = gltf_parse(&g, json, json_len);
done:
    for (int i = 0; i < GLTF_MAX_BUFFERS; ++i)
        if (g.buf[i].owned) free(g.buf[i].data);
    free(g.j.t);
    if (!ok) { char err[256]; memcpy(err, m->error, 256); release(m); memcpy(m->error, err, 256); }
    return ok;
}

int Gltf::load_file(const char *path, gltf_model_t *m) {
    memset(m, 0, sizeof *m);
    size_t size;
    unsigned char *data = read_whole_file(path, &size);
    if (!data) { snprintf(m->error, sizeof m->error, "cannot read '%s'", path); return 0; }
    char dir[512];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = 0; else { dir[0] = '.'; dir[1] = 0; }
    int ok = load_memory(data, size, dir, m);
    free(data);
    return ok;
}

void Gltf::release(gltf_model_t *m) {
    free(m->pos); free(m->nrm); free(m->col); free(m->uv);
    free(m->idx); free(m->prims); free(m->mats);
    for (int i = 0; i < m->nimages; ++i) free(m->images[i].data);
    free(m->images);
    memset(m, 0, sizeof *m);
}

} // namespace mare::engine::assets

// C ABI (gltf.h) for the unported callers
extern "C" {
int  gltf_load_file(const char *path, gltf_model_t *m) { return mare::engine::assets::gltf().load_file(path, m); }
int  gltf_load_memory(const void *data, size_t size, const char *dir, gltf_model_t *m) { return mare::engine::assets::gltf().load_memory(data, size, dir, m); }
void gltf_free(gltf_model_t *m) { mare::engine::assets::gltf().release(m); }
}
