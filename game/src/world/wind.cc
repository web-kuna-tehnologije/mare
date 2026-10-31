// mare::game::world::Wind (see wind.h): latitude bands with seasonal ITCZ drift and noise wobble.
#include <cmath>

extern "C" {
#include "wind.h"
#include "world.h"
#include "../core/util.h"
#include "../core/balance.h"
}

namespace mare::game::world {

class Wind {
public:
    void  at(float wx, float wz, float day, float *dx, float *dz, float *strength);
    float factor(float hx, float hz, float wdx, float wdz, float wstr);
};

Wind &wind() { static Wind w; return w; }

void Wind::at(float wx, float wz, float day, float *dx, float *dz, float *strength) {
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

// The point of sail (0.22.4). A square rig of the period makes its best speed with the wind
// on the quarter, runs a little slower dead downwind, holds half its speed on a reach, and
// can barely claw to windward: inside ~40 degrees of the wind's eye it is all but stopped
// (the fleet does not go astern - a route must still complete - but at WIND_HEAD_FACTOR it
// crawls until the wind veers). The strength scales how far the curve departs from the mean.
#define WIND_HEAD_FACTOR 0.18f
float Wind::factor(float hx, float hz, float wdx, float wdz, float wstr) {
    float d = clampf(hx*wdx + hz*wdz, -1.f, 1.f); // 1 = running downwind, -1 = beating into it
    float curve = d > 0.5f  ? 1.20f - 0.25f * (d - 0.5f) / 0.5f           // quarter 1.20 .. dead run 0.95
                : d > -0.3f ? 0.70f + 0.50f * (d + 0.3f) / 0.8f           // beam reach 0.89 .. close reach 0.70
                : d > -0.77f ? WIND_HEAD_FACTOR + (0.70f - WIND_HEAD_FACTOR) * (d + 0.77f) / 0.47f   // close-hauled, falling off fast
                :              WIND_HEAD_FACTOR;                           // in the eye
    float f = lerpf(0.85f, curve, clampf(0.35f + 0.65f * wstr, 0.f, 1.f));
    return clampf(f, WIND_HEAD_FACTOR, WIND_MAX_FACTOR);
}

} // namespace mare::game::world

// C ABI (wind.h) for the unported callers
extern "C" {
void  wind_at(float wx, float wz, float day, float *dx, float *dz, float *strength) { mare::game::world::wind().at(wx, wz, day, dx, dz, strength); }
float wind_factor(float hx, float hz, float wdx, float wdz, float wstr) { return mare::game::world::wind().factor(hx, hz, wdx, wdz, wstr); }
}
