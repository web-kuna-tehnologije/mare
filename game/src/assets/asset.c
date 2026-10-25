// Asset runtime (see asset.h): descriptor parsing and validation, registry + cache, glTF import
// through gltf.c, scale / pivot / forward-axis normalisation, material rules, base-colour
// texture baking into vertex colours, LOD assembly and import validation.
//
// Texture decoding uses stb_image, which v3 already compiles into the game (image(stb).h);
// the headless test defines STB_IMAGE_IMPLEMENTATION itself. Define ASSET_NO_STB to build
// without any image decoding (textures are then ignored).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "asset.h"
#include "gltf.h"
#include "gjson.h"
#include "../core/diag.h"
#if !defined(ASSET_NO_STB) && !defined(STBI_INCLUDE_STB_IMAGE_H)
#include "../../../ext/ext-image/ext-image/3rd/stb_image.h"   // declarations only unless the includer asked for the implementation
#endif

#define ASSET_VERT_FLOATS 12        // must equal MB_FLOATS (gpu.h); checked by the renderer at compile time
#define ASSET_MAX_WORLD_SIZE 1000.f // import validation: a normalised asset larger than this is a units mistake

static asset_t assets[ASSET_MAX];
static int nassets;
static char asset_root_path[ASSET_PATH];

// --------------------------------------------------------------------------
// small helpers

static int asset_str_icontains(const char *hay, const char *needle) {
    if (!needle[0] || !strcmp(needle, "*")) return 1;
    size_t n = strlen(needle);
    for (; *hay; ++hay) {
        size_t i = 0;
        while (i < n && hay[i] && ((hay[i] | 32) == (needle[i] | 32))) ++i;
        if (i == n) return 1;
    }
    return 0;
}
static void asset_path_dir(const char *path, char *dir, size_t cap) {
    snprintf(dir, cap, "%s", path);
    char *s = strrchr(dir, '/');
#ifdef _WIN32
    char *b = strrchr(dir, '\\');
    if (!s || (b && b > s)) s = b;
#endif
    if (s) *s = 0; else snprintf(dir, cap, ".");
}
static char *asset_read_text(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)len + 1);
    size_t rd = fread(s, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(s); return NULL; }
    s[len] = 0; *n = (size_t)len;
    return s;
}
static int asset_file_exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) fclose(f); return f != NULL; }

const char *asset_type_name(asset_type_t t) {
    static const char *names[ASSET_TYPE_COUNT] = { "prop", "building", "ship", "ship_part", "terrain" };
    return (unsigned)t < ASSET_TYPE_COUNT ? names[t] : "?";
}

// --------------------------------------------------------------------------
// descriptor

#define DFAIL(...) do { snprintf(err, errcap, __VA_ARGS__); return 0; } while (0)

static int asset_parse_enum(const gjson_t *j, int tok, const char *const *names, int n, int dflt) {
    if (tok < 0) return dflt;
    for (int i = 0; i < n; ++i) if (jstreq(j, tok, names[i])) return i;
    return -1;
}

int asset_desc_parse(const char *json, size_t n, const char *dir, asset_desc_t *d, char *err, size_t errcap) {
    static const char *const type_names[] = { "prop", "building", "ship", "ship_part", "terrain" };
    static const char *const pivot_names[] = { "keep", "center", "base_center" };
    static const char *const fit_names[] = { "none", "height", "length", "width", "max" };
    static const char *const fwd_names[] = { "+x", "+z", "-x", "-z" };
    static const char *const coll_names[] = { "none", "aabb", "sphere" };
    memset(d, 0, sizeof *d);
    d->scale_factor = 1;
    snprintf(d->dir, sizeof d->dir, "%s", dir ? dir : ".");
    gjson_t j;
    if (gjson_parse(&j, json, (int)n) < 0 || j.t[0].type != JS_OBJ) { free(j.t); DFAIL("descriptor is not a JSON object"); }
    int ok = 0;
    jstrcpy(&j, jget(&j, 0, "name"), d->name, sizeof d->name);
    int t;
    if ((t = asset_parse_enum(&j, jget(&j, 0, "type"), type_names, ASSET_TYPE_COUNT, ASSET_PROP)) < 0) { snprintf(err, errcap, "unknown asset type"); goto done; }
    d->type = (asset_type_t)t;
    char file[ASSET_PATH] = "";
    jstrcpy(&j, jget(&j, 0, "file"), file, sizeof file);
    int lods = jget(&j, 0, "lods");
    if (lods >= 0) {
        if (j.t[lods].type != JS_ARR || jlen(&j, lods) < 1 || jlen(&j, lods) > ASSET_MAX_LODS) { snprintf(err, errcap, "'lods' must be an array of 1..%d entries", ASSET_MAX_LODS); goto done; }
        d->nlods = jlen(&j, lods);
        for (int i = 0; i < d->nlods; ++i) {
            int l = jat(&j, lods, i);
            int lf = jget(&j, l, "file");
            if (lf >= 0) jstrcpy(&j, lf, d->lod[i].file, sizeof d->lod[i].file);
            else snprintf(d->lod[i].file, sizeof d->lod[i].file, "%s", file);
            jstrcpy(&j, jget(&j, l, "node"), d->lod[i].node, sizeof d->lod[i].node);
            d->lod[i].dist = (float)jnum(&j, jget(&j, l, "dist"), 0);
        }
    } else {
        d->nlods = 1;
        snprintf(d->lod[0].file, sizeof d->lod[0].file, "%s", file);
    }
    int sc = jget(&j, 0, "scale");
    if (sc >= 0) {
        if (j.t[sc].type == JS_NUM) d->scale_factor = (float)jnum(&j, sc, 1);
        else {
            if ((t = asset_parse_enum(&j, jget(&j, sc, "fit"), fit_names, 5, FIT_NONE)) < 0) { snprintf(err, errcap, "unknown scale.fit (height|length|width|max)"); goto done; }
            d->fit = (asset_fit_t)t;
            d->fit_size = (float)jnum(&j, jget(&j, sc, "size"), 0);
            d->scale_factor = (float)jnum(&j, jget(&j, sc, "factor"), 1);
        }
    }
    if ((t = asset_parse_enum(&j, jget(&j, 0, "pivot"), pivot_names, 3, PIVOT_BASE_CENTER)) < 0) { snprintf(err, errcap, "unknown pivot (keep|center|base_center)"); goto done; }
    d->pivot = (asset_pivot_t)t;
    jfloats(&j, jget(&j, 0, "pivot_offset"), d->pivot_offset, 3);
    if ((d->forward = asset_parse_enum(&j, jget(&j, 0, "forward"), fwd_names, 4, 0)) < 0) { snprintf(err, errcap, "unknown forward axis (+x|+z|-x|-z)"); goto done; }
    int mats = jget(&j, 0, "materials");
    if (mats >= 0) {
        if (jlen(&j, mats) > ASSET_MAX_MATRULES) { snprintf(err, errcap, "too many material rules (max %d)", ASSET_MAX_MATRULES); goto done; }
        d->nmat = jlen(&j, mats);
        for (int i = 0; i < d->nmat; ++i) {
            int m = jat(&j, mats, i); asset_matrule_t *r = &d->mat[i];
            jstrcpy(&j, jget(&j, m, "match"), r->match, sizeof r->match);
            if (!r->match[0]) snprintf(r->match, sizeof r->match, "*");
            if (jget(&j, m, "rough") >= 0) { r->has_rough = 1; r->rough = (float)jnum(&j, jget(&j, m, "rough"), 0.8); }
            if (jget(&j, m, "metal") >= 0) { r->has_metal = 1; r->metal = (float)jnum(&j, jget(&j, m, "metal"), 0); }
            if (jget(&j, m, "color") >= 0) { r->has_color = 1; r->color[0] = r->color[1] = r->color[2] = 1; jfloats(&j, jget(&j, m, "color"), r->color, 3); }
            r->instance_color = jbool(&j, jget(&j, m, "instance_color"), 0);
            r->bake_texture = jbool(&j, jget(&j, m, "bake_texture"), 1);
        }
    }
    int coll = jget(&j, 0, "collision");
    if (coll >= 0) {
        if ((t = asset_parse_enum(&j, jget(&j, coll, "type"), coll_names, 3, COLL_NONE)) < 0) { snprintf(err, errcap, "unknown collision type (none|aabb|sphere)"); goto done; }
        d->coll = (asset_coll_t)t;
        d->coll_radius = (float)jnum(&j, jget(&j, coll, "radius"), 0);
        jfloats(&j, jget(&j, coll, "half"), d->coll_half, 3);
    }
    ok = asset_desc_validate(d, err, errcap);
done:
    free(j.t);
    return ok;
}

int asset_desc_validate(const asset_desc_t *d, char *err, size_t errcap) {
    if (!d->name[0]) DFAIL("descriptor has no 'name'");
    if (strpbrk(d->name, "/\\ \t\"")) DFAIL("asset name '%s' may not contain spaces or slashes", d->name);
    if (d->nlods < 1 || d->nlods > ASSET_MAX_LODS) DFAIL("'%s': %d LODs (1..%d allowed)", d->name, d->nlods, ASSET_MAX_LODS);
    for (int i = 0; i < d->nlods; ++i) {
        if (!d->lod[i].file[0]) DFAIL("'%s': LOD %d has no file (set 'file' at the top level or per LOD)", d->name, i);
        if (strstr(d->lod[i].file, "..")) DFAIL("'%s': LOD %d file escapes the asset directory", d->name, i);
        if (i > 0 && d->lod[i].dist <= d->lod[i-1].dist) DFAIL("'%s': LOD distances must increase (LOD %d: %.1f <= %.1f)", d->name, i, d->lod[i].dist, d->lod[i-1].dist);
    }
    if (d->lod[0].dist != 0) DFAIL("'%s': LOD 0 must start at distance 0", d->name);
    if (d->fit != FIT_NONE && !(d->fit_size > 0)) DFAIL("'%s': scale.size must be > 0 when scale.fit is set", d->name);
    if (!(d->scale_factor > 0)) DFAIL("'%s': scale factor must be > 0", d->name);
    for (int c = 0; c < 3; ++c) if (!(d->pivot_offset[c] == d->pivot_offset[c])) DFAIL("'%s': pivot_offset is not a number", d->name);
    return 1;
}

// --------------------------------------------------------------------------
// registry

const char *asset_root(void) {
    if (asset_root_path[0]) return asset_root_path;
    const char *env = getenv("MARE_ASSETS");
    static const char *candidates[] = { "game/assets", "../game/assets", "../../game/assets", "../../../game/assets", "assets" };
    char probe[ASSET_PATH];
    if (env) { snprintf(asset_root_path, sizeof asset_root_path, "%s", env); return asset_root_path; }
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; ++i) {
        snprintf(probe, sizeof probe, "%s/manifest.json", candidates[i]);
        if (asset_file_exists(probe)) { snprintf(asset_root_path, sizeof asset_root_path, "%s", candidates[i]); return asset_root_path; }
    }
    snprintf(asset_root_path, sizeof asset_root_path, "game/assets");
    return asset_root_path;
}

int asset_register_desc(const char *desc_path) {
    size_t n; char *json = asset_read_text(desc_path, &n);
    if (!json) { LOG_ERROR("asset", "cannot read descriptor %s", desc_path); return -1; }
    if (nassets >= ASSET_MAX) { free(json); LOG_ERROR("asset", "registry full (%d)", ASSET_MAX); return -1; }
    char dir[ASSET_PATH], err[256] = "";
    asset_path_dir(desc_path, dir, sizeof dir);
    asset_t *a = &assets[nassets];
    memset(a, 0, sizeof *a);
    int ok = asset_desc_parse(json, n, dir, &a->desc, err, sizeof err);
    free(json);
    if (!ok) { LOG_ERROR("asset", "%s: %s", desc_path, err); return -1; }
    if (asset_find(a->desc.name)) { LOG_ERROR("asset", "%s: duplicate asset name '%s'", desc_path, a->desc.name); return -1; }
    LOG_DEBUG("asset", "registered '%s' (%s, %d LOD%s) from %s", a->desc.name, asset_type_name(a->desc.type), a->desc.nlods, a->desc.nlods == 1 ? "" : "s", desc_path);
    return nassets++;
}

int asset_registry_load(const char *manifest_path) {
    size_t n; char *json = asset_read_text(manifest_path, &n);
    if (!json) { LOG_WARN("asset", "no asset manifest at %s (authored assets disabled)", manifest_path); return 0; }
    gjson_t j;
    int count = 0;
    if (gjson_parse(&j, json, (int)n) < 0) { LOG_ERROR("asset", "%s: invalid JSON", manifest_path); goto done; }
    {
        char dir[ASSET_PATH]; asset_path_dir(manifest_path, dir, sizeof dir);
        int list = jget(&j, 0, "assets");
        for (int i = 0; i < jlen(&j, list); ++i) {
            char rel[ASSET_PATH], path[ASSET_PATH * 2];
            jstrcpy(&j, jat(&j, list, i), rel, sizeof rel);
            snprintf(path, sizeof path, "%s/%s", dir, rel);
            if (asset_register_desc(path) >= 0) count++;
        }
        LOG_INFO("asset", "manifest %s: %d of %d assets registered", manifest_path, count, jlen(&j, list));
    }
done:
    free(j.t); free(json);
    return count;
}

int asset_count(void) { return nassets; }
asset_t *asset_at(int index) { return index >= 0 && index < nassets ? &assets[index] : NULL; }
asset_t *asset_find(const char *name) {
    for (int i = 0; i < nassets; ++i) if (!strcmp(assets[i].desc.name, name)) return &assets[i];
    return NULL;
}
asset_t *asset_get(const char *name) {
    asset_t *a = asset_find(name);
    if (!a) { static int warned; if (!warned++) LOG_WARN("asset", "unknown asset '%s'", name); return NULL; }
    if (a->state == ASSET_UNLOADED) asset_load(a);
    return a->state == ASSET_LOADED ? a : NULL;
}
size_t asset_cpu_bytes(void) { size_t s = 0; for (int i = 0; i < nassets; ++i) s += assets[i].cpu_bytes; return s; }
size_t asset_gpu_bytes(void) { size_t s = 0; for (int i = 0; i < nassets; ++i) s += assets[i].gpu_bytes; return s; }

float asset_lod_bias = 1.f;   // global LOD quality knob, see asset.h

int asset_pick_lod(const asset_t *a, float d) {
    int l = 0;
    while (l + 1 < a->nlods && d >= a->lod[l + 1].dist * asset_lod_bias) ++l;
    return l;
}

// --------------------------------------------------------------------------
// build: glTF -> normalised builder vertices per LOD

#define ASSET_MAX_TEXBAKE 16
typedef struct texbake_t { unsigned char *px; int w, h; const gltf_model_t *model; int image; } texbake_t;   // decoded base colour texture, keyed by (model, image)

static void asset_forward_rotate(int fwd, float *v) {   // model forward axis -> +x (heading 0)
    float x = v[0], z = v[2];
    switch (fwd) { case 1: v[0] = z; v[2] = -x; break; case 2: v[0] = -x; v[2] = -z; break; case 3: v[0] = -z; v[2] = x; break; default: break; }
}
static const asset_matrule_t *asset_find_rule(const asset_desc_t *d, const char *matname) {
    for (int i = 0; i < d->nmat; ++i) if (asset_str_icontains(matname, d->mat[i].match)) return &d->mat[i];
    return NULL;
}
static int asset_prim_in_lod(const gltf_prim_t *p, const asset_lod_desc_t *l) {
    return !l->node[0] || !strcmp(p->node_name, l->node);
}

// bounds of the primitives selected for LOD descriptor `l`, after the forward rotation
static int asset_lod_bounds(const gltf_model_t *m, const asset_lod_desc_t *l, int fwd, float *bmin, float *bmax) {
    int any = 0;
    bmin[0] = bmin[1] = bmin[2] = 1e30f; bmax[0] = bmax[1] = bmax[2] = -1e30f;
    for (int p = 0; p < m->nprims; ++p) {
        if (!asset_prim_in_lod(&m->prims[p], l)) continue;
        for (int i = 0; i < m->prims[p].nindices; ++i) {
            float v[3]; memcpy(v, m->pos + m->idx[m->prims[p].first_index + i] * 3, 12);
            asset_forward_rotate(fwd, v);
            for (int c = 0; c < 3; ++c) { if (v[c] < bmin[c]) bmin[c] = v[c]; if (v[c] > bmax[c]) bmax[c] = v[c]; }
            any = 1;
        }
    }
    return any;
}

static texbake_t *asset_tex_decode(gltf_model_t *m, int image, texbake_t *cache, int *ncache) {
    for (int i = 0; i < *ncache; ++i) if (cache[i].model == m && cache[i].image == image) return cache[i].px ? &cache[i] : NULL;
    if (*ncache >= ASSET_MAX_TEXBAKE) { LOG_WARN("asset", "texture cache full (%d): image %d not baked", ASSET_MAX_TEXBAKE, image); return NULL; }
    texbake_t *t = &cache[(*ncache)++];
    memset(t, 0, sizeof *t); t->model = m; t->image = image;
#ifndef ASSET_NO_STB
    if (image >= 0 && image < m->nimages && m->images[image].data) {
        double t0 = diag_now();
        int n;
        t->px = stbi_load_from_memory(m->images[image].data, (int)m->images[image].size, &t->w, &t->h, &n, 3);
        if (!t->px) LOG_WARN("asset", "image %d (%s, %zu bytes): %s", image, m->images[image].mime, m->images[image].size, stbi_failure_reason());
        else LOG_DEBUG("asset", "decoded image %d: %dx%d %s in %.0f ms", image, t->w, t->h, m->images[image].mime, (diag_now() - t0) * 1000);
    }
#else
    (void)m;
#endif
    return t->px ? t : NULL;
}
static void asset_tex_sample(const texbake_t *t, float u, float v, float *rgb) {   // nearest, repeat; sRGB 0..1
    u -= floorf(u); v -= floorf(v);
    int x = (int)(u * t->w), y = (int)(v * t->h);
    if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1;
    const unsigned char *p = t->px + ((size_t)y * t->w + x) * 3;
    rgb[0] = p[0] / 255.f; rgb[1] = p[1] / 255.f; rgb[2] = p[2] / 255.f;
}

// resolved shading for one glTF material under the descriptor's rules
typedef struct matres_t { float color[3], rough, metal, slot; int bake_tex, image; } matres_t;
static matres_t asset_resolve_material(const asset_desc_t *d, const gltf_model_t *m, int mat) {
    matres_t r = { {1,1,1}, 0.85f, 0.f, 0.f, 1, -1 };
    const char *name = "default";
    if (mat >= 0 && mat < m->nmats) {
        const gltf_material_t *g = &m->mats[mat];
        name = g->name;
        memcpy(r.color, g->base_color, 12);
        r.rough = g->rough; r.metal = g->metal; r.image = g->base_tex;
        // glTF's defaults are metallic 1 / roughness 1, which no exporter means literally (it is
        // what you get when a material has no pbrMetallicRoughness block or only a texture):
        // treat that as a plain matte dielectric rather than rendering a chrome church.
        if (g->metal >= 0.999f && g->rough >= 0.999f) { r.metal = 0; r.rough = 0.85f; }
    }
    const asset_matrule_t *rule = asset_find_rule(d, name);
    if (rule) {
        if (rule->has_rough) r.rough = rule->rough;
        if (rule->has_metal) r.metal = rule->metal;
        if (rule->has_color) memcpy(r.color, rule->color, 12);
        r.slot = rule->instance_color ? 1.f : 0.f;
        r.bake_tex = rule->bake_texture;
    }
    return r;
}

// builds LOD `li` of asset `a` from model `m`; transform = forward rotation, then (p - pivot) * scale + offset
static int asset_build_lod(asset_t *a, int li, gltf_model_t *m, const float *pivot, float scale, texbake_t *tex, int *ntex) {
    const asset_desc_t *d = &a->desc;
    const asset_lod_desc_t *ld = &d->lod[li];
    asset_lod_t *L = &a->lod[li];
    int nv = 0, ni = 0, total = 0;
    for (int p = 0; p < m->nprims; ++p) if (asset_prim_in_lod(&m->prims[p], ld)) total += m->prims[p].nindices;
    if (!total) { snprintf(a->error, sizeof a->error, "LOD %d: no geometry%s%s in %s", li, ld->node[0] ? " for node " : "", ld->node, ld->file); return 0; }
    // vertices are re-emitted per primitive: material data is per vertex in the builder format
    int *remap = (int *)malloc((size_t)m->nverts * sizeof *remap);
    L->verts = (float *)malloc((size_t)total * ASSET_VERT_FLOATS * sizeof(float));   // upper bound, shrunk below
    L->idx = (unsigned *)malloc((size_t)total * sizeof *L->idx);
    for (int p = 0; p < m->nprims; ++p) {
        const gltf_prim_t *pr = &m->prims[p];
        if (!asset_prim_in_lod(pr, ld)) continue;
        matres_t mr = asset_resolve_material(d, m, pr->material);
        texbake_t *t = mr.bake_tex && mr.image >= 0 ? asset_tex_decode(m, mr.image, tex, ntex) : NULL;
        for (int i = 0; i < m->nverts; ++i) remap[i] = -1;
        for (int i = 0; i < pr->nindices; ++i) {
            unsigned src = m->idx[pr->first_index + i];
            if (remap[src] < 0) {
                float *o = L->verts + (size_t)nv * ASSET_VERT_FLOATS;
                float pos[3], nrm[3], col[3];
                memcpy(pos, m->pos + src * 3, 12); memcpy(nrm, m->nrm + src * 3, 12);
                asset_forward_rotate(d->forward, pos); asset_forward_rotate(d->forward, nrm);
                for (int c = 0; c < 3; ++c) o[c] = (pos[c] - pivot[c]) * scale + d->pivot_offset[c];
                memcpy(o + 3, nrm, 12);
                for (int c = 0; c < 3; ++c) col[c] = m->col[src * 3 + c] * mr.color[c];
                if (t) { float tc[3]; asset_tex_sample(t, m->uv[src * 2], m->uv[src * 2 + 1], tc); for (int c = 0; c < 3; ++c) col[c] *= tc[c]; }
                memcpy(o + 6, col, 12);
                o[9] = mr.slot; o[10] = mr.rough; o[11] = mr.metal;
                remap[src] = nv++;
            }
            L->idx[ni++] = (unsigned)remap[src];
        }
    }
    free(remap);
    L->verts = (float *)realloc(L->verts, (size_t)nv * ASSET_VERT_FLOATS * sizeof(float));
    L->nverts = nv; L->nidx = ni; L->dist = ld->dist; L->gpu = 0;
    a->cpu_bytes += (size_t)nv * ASSET_VERT_FLOATS * sizeof(float) + (size_t)ni * sizeof(unsigned);
    return 1;
}

int asset_build(asset_t *a) {
    const asset_desc_t *d = &a->desc;
    double t0 = diag_now();
    a->error[0] = 0; a->cpu_bytes = 0; a->nlods = 0;
    // load each distinct file once (LODs by node name share one GLB)
    gltf_model_t models[ASSET_MAX_LODS]; int model_of[ASSET_MAX_LODS], nmodels = 0;
    memset(models, 0, sizeof models);
    int ok = 1;
    for (int i = 0; i < d->nlods && ok; ++i) {
        model_of[i] = -1;
        for (int k = 0; k < i; ++k) if (!strcmp(d->lod[k].file, d->lod[i].file)) { model_of[i] = model_of[k]; break; }
        if (model_of[i] >= 0) continue;
        char path[ASSET_PATH * 2];
        snprintf(path, sizeof path, "%s/%s", d->dir, d->lod[i].file);
        if (!gltf_load_file(path, &models[nmodels])) { snprintf(a->error, sizeof a->error, "%s: %s", d->lod[i].file, models[nmodels].error); ok = 0; break; }
        model_of[i] = nmodels++;
    }
    texbake_t tex[ASSET_MAX_TEXBAKE]; int ntex = 0;
    float pivot[3] = {0,0,0}, scale = d->scale_factor;
    if (ok) {
        // normalisation is derived from LOD 0 so every LOD lands in the same place at the same size
        float bmin[3], bmax[3];
        if (!asset_lod_bounds(&models[model_of[0]], &d->lod[0], d->forward, bmin, bmax)) { snprintf(a->error, sizeof a->error, "LOD 0: no geometry%s%s", d->lod[0].node[0] ? " for node " : "", d->lod[0].node); ok = 0; }
        else {
            float ext[3] = { bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2] };
            float e = d->fit == FIT_HEIGHT ? ext[1] : d->fit == FIT_LENGTH ? ext[0] : d->fit == FIT_WIDTH ? ext[2] : fmaxf(ext[0], fmaxf(ext[1], ext[2]));
            if (d->fit != FIT_NONE) {
                if (!(e > 1e-6f)) { snprintf(a->error, sizeof a->error, "cannot fit: the model is flat along the fit axis"); ok = 0; }
                else scale = d->fit_size / e;
            }
            if (d->pivot == PIVOT_CENTER) for (int c = 0; c < 3; ++c) pivot[c] = (bmin[c] + bmax[c]) * 0.5f;
            if (d->pivot == PIVOT_BASE_CENTER) { pivot[0] = (bmin[0] + bmax[0]) * 0.5f; pivot[1] = bmin[1]; pivot[2] = (bmin[2] + bmax[2]) * 0.5f; }
            for (int c = 0; c < 3 && ok; ++c) {
                a->bmin[c] = (bmin[c] - pivot[c]) * scale + d->pivot_offset[c];
                a->bmax[c] = (bmax[c] - pivot[c]) * scale + d->pivot_offset[c];
                if (a->bmax[c] - a->bmin[c] > ASSET_MAX_WORLD_SIZE) { snprintf(a->error, sizeof a->error, "normalised size %.0f world units is implausible (check scale / units)", a->bmax[c] - a->bmin[c]); ok = 0; }
            }
        }
    }
    for (int i = 0; i < d->nlods && ok; ++i) {
        ok = asset_build_lod(a, i, &models[model_of[i]], pivot, scale, tex, &ntex);
        if (ok) a->nlods++;
    }
    for (int i = 0; i < ntex; ++i) if (tex[i].px) {
#ifndef ASSET_NO_STB
        stbi_image_free(tex[i].px);
#endif
    }
    for (int i = 0; i < nmodels; ++i) gltf_free(&models[i]);
    if (ok) {
        // import validation on the result + derived collision shape
        a->radius = 0;
        for (int c = 0; c < 3; ++c) a->radius = fmaxf(a->radius, fmaxf(fabsf(a->bmin[c]), fabsf(a->bmax[c])));
        a->radius *= 1.7321f;   // corner of the box
        for (int i = 1; i < a->nlods; ++i)
            if (a->lod[i].nidx > a->lod[i-1].nidx) LOG_WARN("asset", "'%s': LOD %d has more triangles than LOD %d (%d > %d)", d->name, i, i-1, a->lod[i].nidx/3, a->lod[i-1].nidx/3);
        a->coll_radius = d->coll_radius > 0 ? d->coll_radius : fmaxf(a->bmax[0] - a->bmin[0], a->bmax[2] - a->bmin[2]) * 0.5f;
        for (int c = 0; c < 3; ++c) a->coll_half[c] = d->coll_half[c] > 0 ? d->coll_half[c] : (a->bmax[c] - a->bmin[c]) * 0.5f;
        a->scale = scale;
        a->state = ASSET_LOADED;
        a->load_ms = (diag_now() - t0) * 1000;
        LOG_INFO("asset", "loaded '%s' (%s): %d LOD%s, LOD0 %d tris, size %.2fx%.2fx%.2f, scale %.4f, %.1f MB, %.0f ms",
                 d->name, asset_type_name(d->type), a->nlods, a->nlods == 1 ? "" : "s", a->lod[0].nidx / 3,
                 a->bmax[0] - a->bmin[0], a->bmax[1] - a->bmin[1], a->bmax[2] - a->bmin[2], scale, a->cpu_bytes / (1024.0 * 1024.0), a->load_ms);
        for (int i = 1; i < a->nlods; ++i) LOG_DEBUG("asset", "  '%s' LOD %d: %d tris from %.0f units", d->name, i, a->lod[i].nidx / 3, a->lod[i].dist);
    } else {
        for (int i = 0; i < ASSET_MAX_LODS; ++i) { free(a->lod[i].verts); free(a->lod[i].idx); }
        memset(a->lod, 0, sizeof a->lod);
        a->nlods = 0; a->cpu_bytes = 0;
        a->state = ASSET_FAILED;
        LOG_ERROR("asset", "'%s' failed to load: %s", d->name, a->error);
    }
    return ok;
}

int asset_load(asset_t *a) {
    if (a->state == ASSET_LOADED) return 1;
    if (a->state == ASSET_FAILED) return 0;   // do not retry every frame
    return asset_build(a);
}
void asset_free_cpu(asset_t *a, int lod) {
    asset_lod_t *L = &a->lod[lod];
    a->cpu_bytes -= (size_t)L->nverts * ASSET_VERT_FLOATS * sizeof(float) + (size_t)L->nidx * sizeof(unsigned);
    free(L->verts); free(L->idx); L->verts = NULL; L->idx = NULL;
}
void asset_unload(asset_t *a) {   // CPU data only: GPU meshes are owned and reclaimed by the renderer
    for (int i = 0; i < ASSET_MAX_LODS; ++i) { free(a->lod[i].verts); free(a->lod[i].idx); }
    memset(a->lod, 0, sizeof a->lod);
    a->nlods = 0; a->cpu_bytes = 0; a->gpu_bytes = 0; a->state = ASSET_UNLOADED; a->error[0] = 0;
}
void asset_unload_all(void) { for (int i = 0; i < nassets; ++i) asset_unload(&assets[i]); }
