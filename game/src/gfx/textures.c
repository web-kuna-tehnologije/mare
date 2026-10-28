// Streamed textures: PNG/JPEG files decoded off the render thread and uploaded inside the
// frame's upload budget (DESIGN_JOBS.md). A caller asks once and polls: until the texture is
// resident it draws with whatever fallback it has (the sea uses its procedural wave map), so a
// slow disk costs a second of lower fidelity, never a stall.
//
//   IO lane      read the file
//   DECODE lane  stb_image decode to 8-bit RGB
//   MAIN lane    rhi_texture_make (GL context), then the CPU copy is freed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#if !defined(STBI_INCLUDE_STB_IMAGE_H)
#include "../../../ext/ext-image/ext-image/3rd/stb_image.h"
#endif

#define TEX_MAX 32
enum { TEX_UNUSED, TEX_LOADING, TEX_READY, TEX_FAILED };
typedef struct texstream_t {
    char path[512]; rhi_format_t fmt; rhi_filter_t filter; rhi_wrap_t wrap;
    int state;
    unsigned char *bytes; size_t size;      // file contents (IO -> DECODE)
    unsigned char *px; int w, h;            // decoded (DECODE -> MAIN)
    rhi_texture_t tex;
    double t0;
} texstream_t;
static texstream_t texs[TEX_MAX]; static int ntexs;

static void tex_stage_upload(void *p) {     // MAIN
    texstream_t *t = (texstream_t *)p;
    if (t->px) {
        t->tex = rhi_texture_make(t->w, t->h, t->fmt, t->filter, t->wrap, t->px);
        stbi_image_free(t->px); t->px = NULL;
        t->state = TEX_READY;
        LOG_INFO("gfx", "texture %s: %dx%d resident after %.0f ms", t->path, t->w, t->h, (diag_now() - t->t0) * 1000);
    } else {
        t->state = TEX_FAILED;
        LOG_WARN("gfx", "texture %s: not loaded (%s)", t->path, t->bytes ? "decode failed" : "cannot read");
        free(t->bytes); t->bytes = NULL;
    }
}
static void tex_stage_decode(void *p) {     // DECODE
    texstream_t *t = (texstream_t *)p;
    if (t->bytes) {
        int n;
        t->px = stbi_load_from_memory(t->bytes, (int)t->size, &t->w, &t->h, &n, 3);
        if (!t->px) LOG_WARN("gfx", "texture %s: %s", t->path, stbi_failure_reason());
        free(t->bytes); t->bytes = NULL; t->size = 0;
    }
    jobs_submit(JOB_LANE_MAIN, tex_stage_upload, t, 0);
}
static void tex_stage_read(void *p) {       // IO
    texstream_t *t = (texstream_t *)p;
    FILE *f = fopen(t->path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        if (len > 0) { t->bytes = (unsigned char *)malloc((size_t)len); t->size = fread(t->bytes, 1, (size_t)len, f); }
        fclose(f);
    }
    jobs_submit(JOB_LANE_DECODE, tex_stage_decode, t, 0);
}

int texture_request(const char *path, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap) {
    for (int i = 0; i < ntexs; ++i) if (!strcmp(texs[i].path, path)) return i;
    if (ntexs >= TEX_MAX) { LOG_WARN("gfx", "texture table full (%d): %s", TEX_MAX, path); return -1; }
    texstream_t *t = &texs[ntexs];
    memset(t, 0, sizeof *t);
    snprintf(t->path, sizeof t->path, "%s", path);
    t->fmt = fmt; t->filter = filter; t->wrap = wrap; t->state = TEX_LOADING; t->t0 = diag_now();
    jobs_submit(JOB_LANE_IO, tex_stage_read, t, 0);
    return ntexs++;
}
const rhi_texture_t *texture_get(int handle) {
    if (handle < 0 || handle >= ntexs || texs[handle].state != TEX_READY) return NULL;
    return &texs[handle].tex;
}
int texture_state(int handle) { return handle < 0 || handle >= ntexs ? TEX_FAILED : texs[handle].state; }
int texture_count(void) { return ntexs; }
const char *texture_path(int handle) { return handle < 0 || handle >= ntexs ? "" : texs[handle].path; }
void texture_size(int handle, int *w, int *h) { int ok = handle >= 0 && handle < ntexs && texs[handle].state == TEX_READY; if (w) *w = ok ? texs[handle].w : 0; if (h) *h = ok ? texs[handle].h : 0; }
