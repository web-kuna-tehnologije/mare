// Headless asset-runtime test: builds a GLB in memory, loads it through the glTF loader and
// checks baked transforms, generated normals, materials, primitive ranges and the validation
// paths. No framework, no GL.
//
//   cc game/tests/asset_test.c -o asset_test -lm -O2 && ./asset_test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../src/assets/gltf.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

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
    CHECK(NEAR(m.pos[0], 1) && NEAR(m.pos[3], 3) && NEAR(m.pos[8], -2),
          "mesh0 bake wrong: v0.x=%f v1.x=%f v2.z=%f", m.pos[0], m.pos[3], m.pos[8]);
    // generated normal for the XZ triangle: +y
    CHECK(NEAR(m.nrm[0], 0) && NEAR(m.nrm[1], 1) && NEAR(m.nrm[2], 0),
          "generated normal (%f,%f,%f), want +y", m.nrm[0], m.nrm[1], m.nrm[2]);
    // node 1 inherits node 0 (scale 2, tx 1) then rotates 180 deg about y: v0 (0,0,0) stays
    // at the translation (1,0,0); v1 (1,0,0) rotates to (-1,0,0), scales and offsets to (-1,0,0)
    CHECK(NEAR(m.pos[3*3+0], 1) && NEAR(m.pos[4*3+0], -1) && NEAR(m.pos[4*3+1], 0),
          "mesh1 bake wrong: v0.x=%f v1.x=%f", m.pos[9], m.pos[12]);
    CHECK(NEAR(m.nrm[3*3+2], -1), "rotated normal z=%f, want -1", m.nrm[11]);

    CHECK(!strcmp(m.mats[0].name, "wood") && NEAR(m.mats[0].rough, 0.8f) && NEAR(m.mats[0].metal, 0)
          && NEAR(m.mats[0].base_color[0], 0.5f), "material 0 wrong");
    CHECK(!strcmp(m.mats[1].name, "brass") && NEAR(m.mats[1].metal, 1) && m.mats[1].double_sided
          && NEAR(m.mats[1].base_color[0], 1), "material 1 wrong");
    CHECK(m.prims[0].material == 0 && m.prims[1].material == 1, "prim material refs wrong");
    CHECK(!strcmp(m.prims[0].mesh_name, "deck") && !strcmp(m.prims[1].mesh_name, "sail"), "prim mesh names wrong");
    CHECK(m.prims[1].first_index == 3 && m.prims[1].nindices == 3, "prim ranges wrong");
    CHECK(m.idx[3] == 3 && m.idx[5] == 5, "indices not rebased");
    CHECK(NEAR(m.bmin[0], -1) && NEAR(m.bmax[0], 3) && NEAR(m.bmax[1], 2), "bounds wrong: x [%f,%f] ymax %f", m.bmin[0], m.bmax[0], m.bmax[1]);
    CHECK(NEAR(m.col[0], 1), "default vertex colour not white");

    gltf_free(&m);
    free(glb);
}

// a .gltf (plain JSON) with a base64 data: URI buffer, loaded through a temp file
static void test_gltf_data_uri(void) {
    // one triangle: positions (0,0,0)(1,0,0)(0,1,0) as f32 -> 36 bytes
    float pos[] = { 0,0,0, 1,0,0, 0,1,0 };
    static const char *b64c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char b64[80]; int o = 0;
    const unsigned char *p = (const unsigned char *)pos;
    for (int i = 0; i < 36; i += 3) {
        unsigned v = (unsigned)p[i] << 16 | (unsigned)p[i+1] << 8 | p[i+2];
        b64[o++] = b64c[v >> 18]; b64[o++] = b64c[v >> 12 & 63]; b64[o++] = b64c[v >> 6 & 63]; b64[o++] = b64c[v & 63];
    }
    b64[o] = 0;
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
        CHECK(NEAR(m.nrm[2], 1), "generated normal z=%f, want +1", m.nrm[2]);
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

int main(void) {
    test_glb_scene();
    test_gltf_data_uri();
    test_validation();
    test_oob_index();
    if (failures) { printf("asset_test: %d FAILURES\n", failures); return 1; }
    printf("asset_test: all checks passed\n");
    return 0;
}
