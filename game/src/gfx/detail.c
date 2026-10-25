// Procedural detail normal maps (DESIGN_RENDERER.md step 7). The game has no texture assets,
// so both maps are generated at init from tileable value noise:
//
// * waves: a tileable ocean-wave normal map; the sea samples it at two scrolling scales and
//   blends the gradients, replacing the old three hand-written sine waves.
// * grain: a generic surface-relief normal map (plank/plaster scale bumps) applied to the
//   procedural meshes by triplanar projection, since they carry no UVs.
//
// Normals are stored as RGB8 (xyz * 0.5 + 0.5), +Z up in tangent space.

#include <stdlib.h>
#include <math.h>
#include "render.h"
#include "../core/util.h"

static rhi_texture_t tex_waves, tex_grain;
const rhi_texture_t *detail_waves(void) { return &tex_waves; }
const rhi_texture_t *detail_grain(void) { return &tex_grain; }

// tileable value noise: lattice gradients hashed on a wrapping grid
static float vnoise(float x, float y, int period, unsigned seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx*fx*(3-2*fx); fy = fy*fy*(3-2*fy);
    float v[4];
    for (int i = 0; i < 4; ++i) {
        unsigned h = seed;
        h ^= (unsigned)(((xi + (i&1)) % period + period) % period) * 0x9E3779B1u;
        h ^= (unsigned)(((yi + (i>>1)) % period + period) % period) * 0x85EBCA6Bu;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
        v[i] = (h & 0xFFFF) / 65535.f;
    }
    return lerpf(lerpf(v[0], v[1], fx), lerpf(v[2], v[3], fx), fy);
}

// height field -> normal map texture (height in units of texels * bump)
static rhi_texture_t bake_normals(int n, float bump, float (*height)(float u, float v)) {
    unsigned char *px = malloc((size_t)n * n * 3);
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        float e = 1.f / n;
        float u = x * e, v = y * e;
        float hl = height(u - e, v), hr = height(u + e, v), hd = height(u, v - e), hu = height(u, v + e);
        float nx = (hl - hr) * bump, ny = (hd - hu) * bump;
        float l = sqrtf(nx*nx + ny*ny + 1);
        unsigned char *o = px + (y*(size_t)n + x) * 3;
        o[0] = (unsigned char)((nx/l * 0.5f + 0.5f) * 255);
        o[1] = (unsigned char)((ny/l * 0.5f + 0.5f) * 255);
        o[2] = (unsigned char)((1.f/l * 0.5f + 0.5f) * 255);
    }
    rhi_texture_t t = rhi_texture_make(n, n, RHI_FMT_RGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT, px);
    free(px);
    return t;
}

static float wave_height(float u, float v) {     // choppy sea: ridged fbm of tileable noise
    float h = 0, amp = 1, freq = 4;
    for (int o = 0; o < 4; ++o) {
        float x = u * freq, y = v * freq;
        float w = vnoise(x, y, (int)freq, 0xC0FFEEu + o * 7919u);
        h += (1.f - fabsf(w * 2 - 1)) * amp;     // ridged: sharper crests
        amp *= 0.55f; freq *= 2;
    }
    return h;
}

static float grain_height(float u, float v) {    // plank/plaster relief: anisotropic fbm
    float h = vnoise(u * 24, v * 6, 24, 0xBEEFu) * 0.6f      // stretched = planking
            + vnoise(u * 48, v * 48, 48, 0xF00Du) * 0.4f;    // isotropic = plaster bumps
    return h;
}

void detail_init(void) {
    tex_waves = bake_normals(256, 24.f, wave_height);
    tex_grain = bake_normals(128, 10.f, grain_height);
}
