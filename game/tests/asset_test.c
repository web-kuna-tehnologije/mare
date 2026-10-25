// Headless asset-runtime test: builds a GLB in memory, loads it through the glTF loader and
// checks baked transforms, generated normals, materials, primitive ranges and the validation
// paths; then runs the asset runtime (descriptors, registry, scale/pivot/forward normalisation,
// material rules, texture baking, LODs) on files written to the working directory. No
// framework, no GL.
//
//   cc game/tests/asset_test.c -o asset_test -lm -O2 && ./asset_test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "../../ext/ext-image/ext-image/3rd/stb_image.h"
#include "../src/core/diag.c"
#include "../src/assets/gltf.c"
#include "../src/assets/asset.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define FEQ(a, b) (fabsf((a) - (b)) < 1e-4f)

// --------------------------------------------------------------------------
// build a GLB: JSON chunk + BIN chunk with 4-byte alignment, per spec

static unsigned char *glb_build(const char *json, const void *bin, unsigned bin_len, unsigned *out_len) {
    unsigned jlen = (unsigned)strlen(json), jpad = (4 - jlen % 4) % 4, bpad = (4 - bin_len % 4) % 4;
    unsigned total = 12 + 8 + jlen + jpad + (bin_len ? 8 + bin_len + bpad : 0);
    unsigned char *p = (unsigned char *)calloc(1, total), *w = p;
    unsigned u;
    memcpy(w, "glTF", 4); w += 4;
    u = 2; memcpy(w, &u, 4); w += 4;
    memcpy(w, &total, 4); w += 4;
    u = jlen + jpad; memcpy(w, &u, 4); w += 4;
    u = 0x4E4F534A; memcpy(w, &u, 4); w += 4;
    memcpy(w, json, jlen); memset(w + jlen, ' ', jpad); w += jlen + jpad;
    if (bin_len) {
        u = bin_len + bpad; memcpy(w, &u, 4); w += 4;
        u = 0x004E4942; memcpy(w, &u, 4); w += 4;
        memcpy(w, bin, bin_len);
    }
    *out_len = total;
    return p;
}

// Test scene: mesh 0 = one triangle in the XZ plane (no NORMAL attribute -> generated +y),
// material 0; mesh 1 = one triangle with explicit +z normals, material 1, u16 indices.
// Node 0 holds mesh 0 scaled by 2 and translated (1,0,0); node 1 (child of 0) holds mesh 1
// rotated 180 degrees about Y via a quaternion, so its +z normals must bake to -z.
static const float tri0_pos[] = { 0,0,0,  1,0,0,  0,0,-1 };                  // CCW seen from +y
static const float tri1_pos[] = { 0,0,0,  1,0,0,  1,1,0 };
static const float tri1_nrm[] = { 0,0,1,  0,0,1,  0,0,1 };
static const unsigned short tri1_idx[] = { 0, 1, 2, 0 };                     // padded to 8 bytes

#define BIN_TRI1_POS  (sizeof tri0_pos)
#define BIN_TRI1_NRM  (BIN_TRI1_POS + sizeof tri1_pos)
#define BIN_TRI1_IDX  (BIN_TRI1_NRM + sizeof tri1_nrm)
#define BIN_LEN       (BIN_TRI1_IDX + sizeof tri1_idx)

static const char *scene_json =
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
    "\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":["
      "{\"mesh\":0,\"translation\":[1,0,0],\"scale\":[2,2,2],\"children\":[1]},"
      "{\"mesh\":1,\"rotation\":[0,1,0,0]}],"
    "\"meshes\":["
      "{\"name\":\"deck\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]},"
      "{\"name\":\"sail\",\"primitives\":[{\"attributes\":{\"POSITION\":1,\"NORMAL\":2},\"indices\":3,\"material\":1}]}],"
    "\"materials\":["
      "{\"name\":\"wood\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.3,0.1,1],\"roughnessFactor\":0.8,\"metallicFactor\":0}},"
      "{\"name\":\"brass\",\"doubleSided\":true,\"pbrMetallicRoughness\":{\"metallicFactor\":1,\"roughnessFactor\":0.35}}],"
    "\"buffers\":[{\"byteLength\":114}],"
    "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
      "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
      "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":36},"
      "{\"buffer\":0,\"byteOffset\":108,\"byteLength\":6}],"
    "\"accessors\":["
      "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
      "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
      "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
      "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";

static void test_glb_scene(void) {
    unsigned char bin[BIN_LEN];
    memcpy(bin, tri0_pos, sizeof tri0_pos);
    memcpy(bin + BIN_TRI1_POS, tri1_pos, sizeof tri1_pos);
    memcpy(bin + BIN_TRI1_NRM, tri1_nrm, sizeof tri1_nrm);
    memcpy(bin + BIN_TRI1_IDX, tri1_idx, sizeof tri1_idx);

    unsigned len;
    unsigned char *glb = glb_build(scene_json, bin, BIN_LEN, &len);
    gltf_model_t m;
    int ok = gltf_load_memory(glb, len, NULL, &m);
    CHECK(ok, "GLB load failed: %s", m.error);
    if (!ok) { free(glb); return; }

    CHECK(m.nverts == 6, "nverts=%d, want 6", m.nverts);
    CHECK(m.nidx == 6, "nidx=%d, want 6", m.nidx);
    CHECK(m.nprims == 2, "nprims=%d, want 2", m.nprims);
    CHECK(m.nmats == 2, "nmats=%d, want 2", m.nmats);

    // node 0: scale 2, translate (1,0,0): vertex (1,0,0) -> (3,0,0)
    CHECK(FEQ(m.pos[0], 1) && FEQ(m.pos[3], 3) && FEQ(m.pos[8], -2),
          "mesh0 bake wrong: v0.x=%f v1.x=%f v2.z=%f", m.pos[0], m.pos[3], m.pos[8]);
    // generated normal for the XZ triangle: +y
    CHECK(FEQ(m.nrm[0], 0) && FEQ(m.nrm[1], 1) && FEQ(m.nrm[2], 0),
          "generated normal (%f,%f,%f), want +y", m.nrm[0], m.nrm[1], m.nrm[2]);
    // node 1 inherits node 0 (scale 2, tx 1) then rotates 180 deg about y: v0 (0,0,0) stays
    // at the translation (1,0,0); v1 (1,0,0) rotates to (-1,0,0), scales and offsets to (-1,0,0)
    CHECK(FEQ(m.pos[3*3+0], 1) && FEQ(m.pos[4*3+0], -1) && FEQ(m.pos[4*3+1], 0),
          "mesh1 bake wrong: v0.x=%f v1.x=%f", m.pos[9], m.pos[12]);
    CHECK(FEQ(m.nrm[3*3+2], -1), "rotated normal z=%f, want -1", m.nrm[11]);

    CHECK(!strcmp(m.mats[0].name, "wood") && FEQ(m.mats[0].rough, 0.8f) && FEQ(m.mats[0].metal, 0)
          && FEQ(m.mats[0].base_color[0], 0.5f), "material 0 wrong");
    CHECK(!strcmp(m.mats[1].name, "brass") && FEQ(m.mats[1].metal, 1) && m.mats[1].double_sided
          && FEQ(m.mats[1].base_color[0], 1), "material 1 wrong");
    CHECK(m.prims[0].material == 0 && m.prims[1].material == 1, "prim material refs wrong");
    CHECK(!strcmp(m.prims[0].mesh_name, "deck") && !strcmp(m.prims[1].mesh_name, "sail"), "prim mesh names wrong");
    CHECK(m.prims[1].first_index == 3 && m.prims[1].nindices == 3, "prim ranges wrong");
    CHECK(m.idx[3] == 3 && m.idx[5] == 5, "indices not rebased");
    CHECK(FEQ(m.bmin[0], -1) && FEQ(m.bmax[0], 3) && FEQ(m.bmax[1], 2), "bounds wrong: x [%f,%f] ymax %f", m.bmin[0], m.bmax[0], m.bmax[1]);
    CHECK(FEQ(m.col[0], 1), "default vertex colour not white");

    gltf_free(&m);
    free(glb);
}

static void b64_encode(const void *data, int n, char *out) {   // n must be a multiple of 3
    static const char *b64c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *p = (const unsigned char *)data; int o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned)p[i] << 16 | (unsigned)p[i+1] << 8 | p[i+2];
        out[o++] = b64c[v >> 18]; out[o++] = b64c[v >> 12 & 63]; out[o++] = b64c[v >> 6 & 63]; out[o++] = b64c[v & 63];
    }
    out[o] = 0;
}
static void write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { failures++; printf("FAIL: cannot write %s\n", path); return; }
    fwrite(data, 1, n, f); fclose(f);
}

// a .gltf (plain JSON) with a base64 data: URI buffer, loaded through a temp file
static void test_gltf_data_uri(void) {
    // one triangle: positions (0,0,0)(1,0,0)(0,1,0) as f32 -> 36 bytes
    float pos[] = { 0,0,0, 1,0,0, 0,1,0 };
    char b64[80];
    b64_encode(pos, 36, b64);
    char json[1024];
    snprintf(json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}", b64);

    const char *path = "asset_test_tmp.gltf";
    FILE *f = fopen(path, "wb");
    if (!f) { failures++; printf("FAIL: cannot write %s\n", path); return; }
    fputs(json, f);
    fclose(f);
    gltf_model_t m;
    int ok = gltf_load_file(path, &m);
    CHECK(ok, ".gltf data-uri load failed: %s", m.error);
    if (ok) {
        CHECK(m.nverts == 3 && m.nidx == 3, "data-uri model wrong: %d verts %d idx", m.nverts, m.nidx);
        CHECK(m.nprims == 1 && m.prims[0].material == -1, "default material not -1");
        CHECK(FEQ(m.nrm[2], 1), "generated normal z=%f, want +1", m.nrm[2]);
        gltf_free(&m);
    }
    remove(path);
}

static void expect_fail(const char *what, const char *json, const void *bin, unsigned bin_len) {
    unsigned len;
    unsigned char *glb = glb_build(json, bin, bin_len, &len);
    gltf_model_t m;
    int ok = gltf_load_memory(glb, len, NULL, &m);
    CHECK(!ok && m.error[0], "%s: expected a validation error, got ok=%d", what, ok);
    if (ok) gltf_free(&m);
    free(glb);
}

static void test_validation(void) {
    gltf_model_t m;
    CHECK(!gltf_load_memory("nonsense", 8, NULL, &m) && m.error[0], "garbage input accepted");
    CHECK(!gltf_load_memory("{\"asset\":{\"version\":\"1.0\"}}", 26, NULL, &m), "glTF 1.0 accepted");

    unsigned char glb_trunc[16];
    memcpy(glb_trunc, "glTF", 4);
    unsigned u = 2; memcpy(glb_trunc + 4, &u, 4);
    u = 1000; memcpy(glb_trunc + 8, &u, 4);   // claims 1000 bytes, has 16
    CHECK(!gltf_load_memory(glb_trunc, 16, NULL, &m), "truncated GLB accepted");

    float pos[] = { 0,0,0, 1,0,0, 0,1,0 };
    // accessor past the end of its buffer
    expect_fail("accessor out of bounds",
        "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"buffers\":[{\"byteLength\":16}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":16}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}",
        pos, 16);
    // no POSITION
    expect_fail("missing POSITION",
        "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}],\"buffers\":[],\"bufferViews\":[],\"accessors\":[]}",
        NULL, 0);
    // required extension (Draco)
    expect_fail("required extension",
        "{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]}",
        NULL, 0);
    // empty scene
    expect_fail("empty model",
        "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[]}],\"nodes\":[]}", NULL, 0);
}

static void test_oob_index(void) {   // indices referencing vertex 9 of a 3-vertex mesh
    float pos[] = { 0,0,0, 1,0,0, 0,1,0 };
    unsigned idx[] = { 0, 1, 9 };
    unsigned char bin[48];
    memcpy(bin, pos, 36);
    memcpy(bin + 36, idx, 12);
    expect_fail("index out of range",
        "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"buffers\":[{\"byteLength\":48}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}]}",
        bin, 48);
}

// --------------------------------------------------------------------------
// asset runtime

static void test_descriptor(void) {
    asset_desc_t d; char err[256];
    const char *good =
        "{\"name\":\"church\",\"type\":\"building\",\"file\":\"church.glb\","
        "\"lods\":[{\"node\":\"Church_LOD0\",\"dist\":0},{\"node\":\"Church_LOD1\",\"dist\":40},{\"file\":\"far.glb\",\"dist\":120}],"
        "\"scale\":{\"fit\":\"height\",\"size\":0.55},\"pivot\":\"base_center\",\"forward\":\"+z\",\"pivot_offset\":[0,0.1,0],"
        "\"materials\":[{\"match\":\"roof\",\"rough\":0.7,\"color\":[0.6,0.3,0.2],\"instance_color\":true},{\"match\":\"*\",\"metal\":0}],"
        "\"collision\":{\"type\":\"aabb\",\"half\":[1,2,3]}}";
    int ok = asset_desc_parse(good, strlen(good), "assets/church", &d, err, sizeof err);
    CHECK(ok, "descriptor parse failed: %s", err);
    if (ok) {
        CHECK(!strcmp(d.name, "church") && d.type == ASSET_BUILDING, "name/type");
        CHECK(d.nlods == 3 && !strcmp(d.lod[0].file, "church.glb") && !strcmp(d.lod[1].node, "Church_LOD1") && !strcmp(d.lod[2].file, "far.glb") && FEQ(d.lod[2].dist, 120), "lods");
        CHECK(d.fit == FIT_HEIGHT && FEQ(d.fit_size, 0.55f) && d.pivot == PIVOT_BASE_CENTER && d.forward == 1 && FEQ(d.pivot_offset[1], 0.1f), "scale/pivot/forward");
        CHECK(d.nmat == 2 && d.mat[0].has_rough && !d.mat[0].has_metal && d.mat[0].has_color && d.mat[0].instance_color && d.mat[0].bake_texture && d.mat[1].has_metal, "material rules");
        CHECK(d.coll == COLL_AABB && FEQ(d.coll_half[2], 3), "collision");
        CHECK(!strcmp(d.dir, "assets/church"), "dir");
    }
    // defaults: single LOD from "file", scale 1, base_center pivot, +x forward
    const char *minimal = "{\"name\":\"crate\",\"file\":\"crate.glb\"}";
    ok = asset_desc_parse(minimal, strlen(minimal), ".", &d, err, sizeof err);
    CHECK(ok && d.nlods == 1 && !strcmp(d.lod[0].file, "crate.glb") && d.fit == FIT_NONE && FEQ(d.scale_factor, 1) && d.pivot == PIVOT_BASE_CENTER && d.forward == 0 && d.type == ASSET_PROP,
          "minimal descriptor defaults (%s)", err);
    const char *numscale = "{\"name\":\"crate\",\"file\":\"crate.glb\",\"scale\":0.01}";
    ok = asset_desc_parse(numscale, strlen(numscale), ".", &d, err, sizeof err);
    CHECK(ok && d.fit == FIT_NONE && FEQ(d.scale_factor, 0.01f), "numeric scale");
    // validation failures
    static const char *bad[] = {
        "{\"file\":\"a.glb\"}",                                                         // no name
        "{\"name\":\"a\"}",                                                             // no file
        "{\"name\":\"a b\",\"file\":\"a.glb\"}",                                        // space in name
        "{\"name\":\"a\",\"file\":\"../x.glb\"}",                                       // escapes dir
        "{\"name\":\"a\",\"file\":\"a.glb\",\"type\":\"spaceship\"}",                   // unknown type
        "{\"name\":\"a\",\"file\":\"a.glb\",\"lods\":[{\"dist\":0},{\"dist\":0}]}",     // non-increasing LOD distance
        "{\"name\":\"a\",\"file\":\"a.glb\",\"lods\":[{\"dist\":5}]}",                  // LOD 0 not at 0
        "{\"name\":\"a\",\"file\":\"a.glb\",\"scale\":{\"fit\":\"height\"}}",           // fit without size
        "{\"name\":\"a\",\"file\":\"a.glb\",\"scale\":-2}",                             // negative scale
        "{\"name\":\"a\",\"file\":\"a.glb\",\"pivot\":\"middle\"}",                     // unknown pivot
        "{\"name\":\"a\",\"file\":\"a.glb\",\"forward\":\"up\"}",                       // unknown forward
        "[1,2,3]",                                                                      // not an object
        "{\"name\":\"a\",\"file\":\"a.glb\",",                                          // truncated JSON
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) {
        err[0] = 0;
        ok = asset_desc_parse(bad[i], strlen(bad[i]), ".", &d, err, sizeof err);
        CHECK(!ok && err[0], "bad descriptor %zu accepted: %s", i, bad[i]);
    }
}

// Writes an asset directory: a .gltf with two LOD nodes and a 2x1 BMP base colour texture
// (left texel red, right texel blue), plus a descriptor, then loads it through the registry.
//   Thing_LOD0: quad-ish triangle, x 0..4, y 1..3 (so the base is NOT at y=0), z 0..2, uv 0.25 / 0.75
//   Thing_LOD1: one small triangle
//   glTF forward is +z in the descriptor, fit height 1.0, base_center pivot
static void test_asset_build(void) {
    // BMP 2x1, 24-bit: 54-byte header + one row (6 bytes + 2 padding)
    unsigned char bmp[63] = {0};   // 62-byte file + 1 pad byte so the base64 input is a multiple of 3
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = 62; bmp[10] = 54; bmp[14] = 40; bmp[18] = 2; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24;
    bmp[54] = 0; bmp[55] = 0; bmp[56] = 255;      // texel 0: BGR -> red
    bmp[57] = 255; bmp[58] = 0; bmp[59] = 0;      // texel 1: blue
    // 62 bytes is not a multiple of 3: pad to 63 (stb ignores trailing bytes)
    char b64_img[100]; b64_encode(bmp, 63, b64_img);

    float bin[] = {
        0,1,0,  4,1,0,  0,3,2,    // LOD0 positions (3 verts)
        0,0,0,  1,0,0,  0,1,0,    // LOD1 positions
        0.25f,0.5f, 0.75f,0.5f, 0.25f,0.5f,   // LOD0 uvs: red, blue, red
    };
    char b64_bin[200]; b64_encode(bin, sizeof bin, b64_bin);
    char json[2048];
    snprintf(json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
        "\"nodes\":[{\"name\":\"Thing_LOD0\",\"mesh\":0},{\"name\":\"Thing_LOD1\",\"mesh\":1,\"translation\":[5,3,2]}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":2},\"material\":0}]},"
                    "{\"primitives\":[{\"attributes\":{\"POSITION\":1},\"material\":1}]}],"
        "\"materials\":[{\"name\":\"Walls\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"baseColorFactor\":[1,1,0.5,1]}},"
                       "{\"name\":\"RoofTiles\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.5,0.5,1],\"metallicFactor\":1,\"roughnessFactor\":1}}],"
        "\"textures\":[{\"source\":0}],\"images\":[{\"mimeType\":\"image/bmp\",\"uri\":\"data:image/bmp;base64,%s\"}],"
        "\"buffers\":[{\"byteLength\":%d,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                       "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                       "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}]}",
        b64_img, (int)sizeof bin, b64_bin);
    write_file("asset_test_thing.gltf", json, strlen(json));
    const char *desc =
        "{\"name\":\"thing\",\"type\":\"prop\",\"file\":\"asset_test_thing.gltf\","
        "\"lods\":[{\"node\":\"Thing_LOD0\",\"dist\":0},{\"node\":\"Thing_LOD1\",\"dist\":50}],"
        "\"scale\":{\"fit\":\"height\",\"size\":1.0},\"pivot\":\"base_center\",\"forward\":\"+z\",\"pivot_offset\":[0,0.5,0],"
        "\"materials\":[{\"match\":\"roof\",\"rough\":0.6,\"metal\":0.2,\"color\":[1,0,0],\"instance_color\":true}]}";
    write_file("asset_test_thing.asset.json", desc, strlen(desc));
    const char *manifest = "{\"assets\":[\"asset_test_thing.asset.json\",\"missing.asset.json\"]}";
    write_file("asset_test_manifest.json", manifest, strlen(manifest));

    int n = asset_registry_load("asset_test_manifest.json");
    CHECK(n == 1, "registry registered %d assets, want 1", n);
    CHECK(asset_count() == 1 && asset_find("thing") && !asset_find("nothing"), "registry lookup");
    asset_t *a = asset_get("thing");
    CHECK(a != NULL, "asset_get failed: %s", asset_find("thing") ? asset_find("thing")->error : "?");
    if (!a) return;
    CHECK(a->state == ASSET_LOADED && a->nlods == 2, "state/lods");
    // LOD0 extent: x 0..4, y 1..3, z 0..2. Forward +z -> +x maps (x,y,z) to (z, y, -x):
    // x' 0..2, y 1..3, z' -4..0. Fit height 1.0 -> scale 0.5. base_center pivot = (1, 1, -2).
    CHECK(FEQ(a->scale, 0.5f), "scale %f, want 0.5", a->scale);
    CHECK(FEQ(a->bmin[0], -0.5f) && FEQ(a->bmax[0], 0.5f) && FEQ(a->bmin[1], 0.5f) && FEQ(a->bmax[1], 1.5f) && FEQ(a->bmin[2], -1) && FEQ(a->bmax[2], 1),
          "bounds x[%f %f] y[%f %f] z[%f %f]", a->bmin[0], a->bmax[0], a->bmin[1], a->bmax[1], a->bmin[2], a->bmax[2]);
    asset_lod_t *L = &a->lod[0];
    CHECK(L->nverts == 3 && L->nidx == 3, "LOD0 %d verts %d idx", L->nverts, L->nidx);
    // vertex 0 (0,1,0) -> rotated (0,1,0) -> (0-1, 1-1, 0+2)*0.5 + (0,0.5,0) = (-0.5, 0.5, 1)
    CHECK(FEQ(L->verts[0], -0.5f) && FEQ(L->verts[1], 0.5f) && FEQ(L->verts[2], 1.f), "v0 = (%f,%f,%f)", L->verts[0], L->verts[1], L->verts[2]);
    // walls: texture red * factor (1,1,0.5) -> (1,0,0); vertex 1 samples blue -> (0,0,0.5); default material -> matte dielectric, slot 0
    CHECK(FEQ(L->verts[6], 1) && FEQ(L->verts[7], 0) && FEQ(L->verts[8], 0), "v0 colour (%f,%f,%f), want red", L->verts[6], L->verts[7], L->verts[8]);
    CHECK(FEQ(L->verts[12+6], 0) && FEQ(L->verts[12+8], 0.5f), "v1 colour (%f,%f,%f), want (0,0,0.5)", L->verts[18], L->verts[19], L->verts[20]);
    CHECK(FEQ(L->verts[9], 0) && FEQ(L->verts[10], 0.85f) && FEQ(L->verts[11], 0), "walls slot/rough/metal %f %f %f", L->verts[9], L->verts[10], L->verts[11]);
    // LOD1: "roof" rule matches RoofTiles case-insensitively: colour override red, rough 0.6, metal 0.2, instance slot 1
    asset_lod_t *L1 = &a->lod[1];
    CHECK(L1->nverts == 3 && FEQ(L1->dist, 50), "LOD1");
    CHECK(FEQ(L1->verts[6], 1) && FEQ(L1->verts[7], 0) && FEQ(L1->verts[9], 1) && FEQ(L1->verts[10], 0.6f) && FEQ(L1->verts[11], 0.2f),
          "roof rule: col %f %f slot %f rough %f metal %f", L1->verts[6], L1->verts[7], L1->verts[9], L1->verts[10], L1->verts[11]);
    // LOD1's node carries a translation (LOD objects are often scattered around the source
    // scene); the pivot is therefore computed per LOD, which cancels it. Local triangle
    // (0,0,0)(1,0,0)(0,1,0), forward +z -> x'=z, z'=-x: bounds x' 0, y 0..1, z' -1..0,
    // base_center pivot (0, ymin, -0.5). v1 (1,0,0) -> (0,0,-1) -> (0, 0.5, -0.25).
    CHECK(FEQ(L1->verts[12+0], 0.f) && FEQ(L1->verts[12+1], 0.5f) && FEQ(L1->verts[12+2], -0.25f), "LOD1 v1 = (%f,%f,%f)", L1->verts[12], L1->verts[13], L1->verts[14]);
    CHECK(asset_pick_lod(a, 0) == 0 && asset_pick_lod(a, 49.9f) == 0 && asset_pick_lod(a, 50) == 1 && asset_pick_lod(a, 1e9f) == 1, "lod pick");
    asset_lod_bias = 2.f;    // detail held twice as far: the switch moves from 50 to 100
    CHECK(asset_pick_lod(a, 99) == 0 && asset_pick_lod(a, 100) == 1, "lod bias 2");
    asset_lod_bias = 0.5f;   // coarser sooner: the switch moves to 25
    CHECK(asset_pick_lod(a, 24) == 0 && asset_pick_lod(a, 25) == 1, "lod bias 0.5");
    asset_lod_bias = 1.f;
    CHECK(a->radius > 1.5f && a->coll_half[1] > 0.49f && a->coll_radius > 0.9f, "radius/collision derivation");
    CHECK(a->cpu_bytes == 6 * 12 * sizeof(float) + 6 * sizeof(unsigned) && asset_cpu_bytes() == a->cpu_bytes, "cpu bytes %zu", a->cpu_bytes);
    asset_free_cpu(a, 0);
    CHECK(a->lod[0].verts == NULL && a->cpu_bytes == 3 * 12 * sizeof(float) + 3 * sizeof(unsigned), "free_cpu accounting");
    asset_unload_all();
    CHECK(a->state == ASSET_UNLOADED && asset_cpu_bytes() == 0 && asset_count() == 1, "unload keeps the registration");
    CHECK(asset_get("thing") == a && a->state == ASSET_LOADED, "reload after unload");

    // a failing asset is remembered as failed (no retry storm) and asset_get returns NULL
    const char *desc2 = "{\"name\":\"ghost\",\"file\":\"does_not_exist.glb\"}";
    write_file("asset_test_ghost.asset.json", desc2, strlen(desc2));
    CHECK(asset_register_desc("asset_test_ghost.asset.json") == 1, "register second asset");
    CHECK(asset_get("ghost") == NULL && asset_find("ghost")->state == ASSET_FAILED && asset_find("ghost")->error[0], "failed asset");
    // a LOD node that does not exist is an import error
    const char *desc3 = "{\"name\":\"nonode\",\"file\":\"asset_test_thing.gltf\",\"lods\":[{\"node\":\"Nope\",\"dist\":0}]}";
    write_file("asset_test_nonode.asset.json", desc3, strlen(desc3));
    asset_register_desc("asset_test_nonode.asset.json");
    CHECK(asset_get("nonode") == NULL && strstr(asset_find("nonode")->error, "Nope"), "missing LOD node: %s", asset_find("nonode")->error);
    asset_unload_all();
    remove("asset_test_thing.gltf"); remove("asset_test_thing.asset.json"); remove("asset_test_manifest.json");
    remove("asset_test_ghost.asset.json"); remove("asset_test_nonode.asset.json");
}

int main(void) {
    diag_init(&(diag_config_t){ .logdir = "test_logs", .console_level = LOG_ERROR });
    test_glb_scene();
    test_gltf_data_uri();
    test_validation();
    test_oob_index();
    test_descriptor();
    test_asset_build();
    if (failures) { printf("asset_test: %d FAILURES\n", failures); return 1; }
    printf("asset_test: all checks passed\n");
    return 0;
}
