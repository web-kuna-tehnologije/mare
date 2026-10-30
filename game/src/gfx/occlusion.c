// Terrain occlusion (see occlusion.h).
#include <math.h>
#include <stdlib.h>
#include "occlusion.h"
#include "../core/types.h"

#define OCC_STEP        ((float)WORLD_K / CELLS_PER_DEG)   // one heightmap cell
#define OCC_STEP_GROW   0.02f    // step grows with the distance marched: far occluders are big
#define OCC_MARGIN      0.25f    // the ground must clear the ray by this much to count ...
#define OCC_MARGIN_PER_UNIT 0.004f   // ... plus this per unit of distance from the camera (terrain LOD)
#define OCC_EYE_GUARD   1.5f     // never test the last units in front of the eye (it may sit in a hillside)

int occlusion_enabled = 1;
int occ_samples, occ_tests;
static float occ_eye[3], occ_ceiling = 1e9f;

void occ_frame_begin(const float eye[3], float ceiling) {
    occ_eye[0] = eye[0]; occ_eye[1] = eye[1]; occ_eye[2] = eye[2];
    occ_ceiling = ceiling;
    occ_samples = occ_tests = 0;
}

int occ_ray_hidden(occ_height_fn h, float px, float py, float pz, float skip, const float *box) {
    float dx = occ_eye[0] - px, dy = occ_eye[1] - py, dz = occ_eye[2] - pz;
    float L = sqrtf(dx*dx + dz*dz);
    if (L < skip + OCC_EYE_GUARD + OCC_STEP) return 0;          // too close to hide behind anything
    if (py >= occ_ceiling) return 0;                            // already above every hill
    float end = L - OCC_EYE_GUARD, step = OCC_STEP;
    for (float s = skip; s < end; s += step, step += OCC_STEP * OCC_STEP_GROW) {
        float t = s / L, x = px + dx*t, z = pz + dz*t, y = py + dy*t;
        if (dy > 0 && y > occ_ceiling) return 0;                // the ray climbed out of the terrain band
        if (box && x >= box[0] && x <= box[2] && z >= box[1] && z <= box[3]) continue;
        occ_samples++;
        float margin = OCC_MARGIN + OCC_MARGIN_PER_UNIT * (L - s);
        if (h(x, z) - margin > y) return 1;
    }
    return 0;
}

int occ_sphere_hidden(occ_height_fn h, float x, float z, float r, float top) {
    occ_tests++;
    float dx = occ_eye[0] - x, dz = occ_eye[2] - z, L = sqrtf(dx*dx + dz*dz);
    if (L < 1e-3f) return 0;
    float nx = -dz / L, nz = dx / L;                            // lateral, as seen from the eye
    if (!occ_ray_hidden(h, x, top, z, r, NULL)) return 0;
    if (!occ_ray_hidden(h, x + nx*r, top, z + nz*r, r * 0.5f, NULL)) return 0;
    return occ_ray_hidden(h, x - nx*r, top, z - nz*r, r * 0.5f, NULL);
}
