// Wind model (see wind.h): latitude bands with seasonal ITCZ drift and noise wobble.
#include <math.h>
#include "wind.h"
#include "world.h"
#include "../core/util.h"
#include "../core/balance.h"

void wind_at(float wx, float wz, float day, float *dx, float *dz, float *strength) {
    float lat = z2lat(wz), lon = x2lon(wx);
    float season = sinf((day / 365.25f) * 6.2831853f);       // +1 ~ april, -1 ~ october
    float l = lat + 2.5f * season;                             // the ITCZ drifts with the seasons
    float ang, str, a = fabsf(l);
    if (a < 5)       { ang = (l >= 0 ? -1.f : 1.f) * 1.5708f; str = 0.35f; }           // doldrums
    else if (a < 30) { ang = (l > 0) ? 3.1416f*1.10f : 3.1416f*0.90f; str = 1.1f; }    // trade winds blow west
    else if (a < 38) { ang = (l > 0) ? 3.1416f*0.75f : 3.1416f*1.25f; str = 0.55f; }   // horse latitudes
    else if (a < 65) { ang = (l > 0) ? 0.15f : -0.15f; str = 1.2f; }                    // westerlies blow east
    else             { ang = 3.1416f; str = 0.9f; }                                      // polar easterlies
    float n = world_vnoise(lon*0.08f + day*0.01f, lat*0.08f, 4242) - 0.5f;
    ang += n * 0.9f;
    str *= 0.8f + 0.4f * world_vnoise(lon*0.05f - day*0.02f, lat*0.05f + day*0.005f, 777);
    *dx = cosf(ang); *dz = -sinf(ang); *strength = str;
}

float wind_factor(float hx, float hz, float wdx, float wdz, float wstr) {
    float d = hx*wdx + hz*wdz; // 1 = running downwind, -1 = beating into it
    return clampf(0.78f + 0.30f * d * wstr, WIND_MIN_FACTOR, WIND_MAX_FACTOR);
}
