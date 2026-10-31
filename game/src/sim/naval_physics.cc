// mare::game::sim::NavalPhysics - see naval_physics.h. Pure arithmetic: no rendering, no
// assets, no globals from the rest of the game, which is what lets naval_physics_test link it
// on its own and prove the fixed step.
#include <cmath>
#include <cstring>

extern "C" {
#include "naval_physics.h"
}

namespace mare::game::sim {

static inline float npf_clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float npf_wrap(float a) { while (a > 3.14159265f) a -= 6.28318531f; while (a < -3.14159265f) a += 6.28318531f; return a; }

// Hydrodynamics, in the only terms that matter at this scale: a hull goes where she points
// far more easily than sideways. Longitudinal drag sets her top speed for a given thrust;
// the (much larger) lateral drag is what makes her carve a turn instead of sliding through
// it, and is the whole of "a ship is not a hovercraft".
#define NP_DRAG_FWD   0.42f     // per second, per unit of speed (quadratic term below)
#define NP_DRAG_LAT   3.40f     // the underwater body resisting sideslip
#define NP_DRAG_YAW   2.10f     // and resisting rotation
#define NP_THRUST_K   1.60f     // how hard she is driven towards the commanded speed
#define NP_RESTITUTION 0.18f    // oak on oak: mostly a thud, a little bounce
#define NP_FRICTION    0.35f    // tangential loss in a contact (rigging fouling, planks scraping)
#define NP_SLOP        0.004f   // penetration left unresolved, so resting contacts do not jitter
#define NP_CORRECT     0.55f    // share of the remaining penetration pushed out per step

typedef struct body_t {
    float x, z, h;                 // the fixed-step state
    float px, pz, ph;              // the step before it (nphys_pose interpolates between them)
    float vx, vz, yr;              // velocity and yaw rate
    float len, beam, draft;
    float inv_m, inv_i;            // a static hull would be 0; every ship here has mass
    float cmd_v, cmd_h, cmd_turn;  // what the helm asked for
    float heel;                    // smoothed |yaw rate| x speed: how hard she is leaning
    int   used, aground;
} body_t;

class NavalPhysics {
public:
    void reset() { n_ = 0; acc_ = 0; steps_ = 0; contacts_ = 0; coast_on_ = 0; }
    int  add(float x, float z, float h, float len, float beam, float draft, float tonnes);
    void drive(int id, float v, float h, float turn) {
        if (!valid(id)) return;
        b_[id].cmd_v = v; b_[id].cmd_h = h; b_[id].cmd_turn = turn > 0.f ? turn : 0.3f;
    }
    void set_coast(float ax, float az, float nx, float nz, float amp, float freq, float slope, int on) {
        cax_ = ax; caz_ = az; cnx_ = nx; cnz_ = nz; camp_ = amp; cfreq_ = freq; cslope_ = slope; coast_on_ = on;
    }
    void advance(float dt);
    void pose(int id, float *x, float *z, float *h) const;
    void velocity(int id, float *vx, float *vz, float *yr) const {
        if (!valid(id)) { *vx = *vz = *yr = 0; return; }
        *vx = b_[id].vx; *vz = b_[id].vz; *yr = b_[id].yr;
    }
    float speed(int id) const {
        if (!valid(id)) return 0.f;
        const body_t &b = b_[id];
        return b.vx * cosf(b.h) + b.vz * (-sinf(b.h));
    }
    float heel(int id) const { return valid(id) ? b_[id].heel : 0.f; }
    int   aground(int id) const { return valid(id) ? b_[id].aground : 0; }
    void  teleport(int id, float x, float z, float h) {
        if (!valid(id)) return;
        body_t &b = b_[id];
        b.x = b.px = x; b.z = b.pz = z; b.h = b.ph = h; b.vx = b.vz = b.yr = 0.f;
    }
    int   steps_last() const { return steps_; }
    int   contacts_last() const { return contacts_; }
    float alpha() const { return npf_clamp(acc_ / NPHYS_STEP, 0.f, 1.f); }
    int   count() const { return n_; }

private:
    int  valid(int id) const { return id >= 0 && id < n_ && b_[id].used; }
    void step();
    void integrate(body_t &b);
    void collide_ships();
    void collide_coast();
    // the compound hull: three discs down the keel, at these fractions of the length from the
    // centre, each of radius `beam * 0.5` shrunk a little at the ends where the hull narrows
    static void part(const body_t &b, int k, float *px, float *pz, float *pr) {
        static const float at[NPHYS_PARTS] = { 0.30f, 0.f, -0.30f };
        static const float rs[NPHYS_PARTS] = { 0.62f, 1.00f, 0.78f };
        float c = cosf(b.h), s = -sinf(b.h), d = at[k] * b.len;
        *px = b.x + c * d; *pz = b.z + s * d; *pr = b.beam * 0.5f * rs[k];
    }
    float coast_depth(float x, float z) const {   // water depth at a point; negative inland
        if (!coast_on_) return 1e6f;
        float dx = x - cax_, dz = z - caz_;
        float v = dx * cnx_ + dz * cnz_, u = -dx * cnz_ + dz * cnx_;
        float w = camp_ * (sinf(u * cfreq_) + 0.5f * sinf(u * cfreq_ * 2.3f + 1.7f) + 0.25f * sinf(u * cfreq_ * 5.1f + 0.4f));
        return -(v - w) * cslope_;
    }

    body_t b_[NPHYS_MAX_BODIES]; int n_ = 0;
    float acc_ = 0.f; int steps_ = 0, contacts_ = 0;
    float cax_ = 0, caz_ = 0, cnx_ = 0, cnz_ = -1, camp_ = 0, cfreq_ = 0.01f, cslope_ = 0.15f; int coast_on_ = 0;
};

NavalPhysics &physics() { static NavalPhysics p; return p; }

int NavalPhysics::add(float x, float z, float h, float len, float beam, float draft, float tonnes) {
    if (n_ >= NPHYS_MAX_BODIES) return -1;
    body_t &b = b_[n_];
    memset(&b, 0, sizeof b);
    b.x = b.px = x; b.z = b.pz = z; b.h = b.ph = h;
    b.len = len > 0.1f ? len : 0.1f; b.beam = beam > 0.05f ? beam : 0.05f; b.draft = draft;
    float m = tonnes > 1.f ? tonnes : 1.f;
    b.inv_m = 1.f / m;
    // a box of len x beam about its centre: I = m (l^2 + b^2) / 12
    b.inv_i = 1.f / (m * (b.len * b.len + b.beam * b.beam) / 12.f);
    b.cmd_h = h; b.cmd_turn = 0.3f;
    b.used = 1;
    return n_++;
}

// One fixed sub-step. Order is fixed and never varies: drive and drag, integrate, then
// resolve contacts against other hulls and against the shore.
void NavalPhysics::step() {
    const float dt = NPHYS_STEP;
    for (int i = 0; i < n_; ++i) if (b_[i].used) { b_[i].px = b_[i].x; b_[i].pz = b_[i].z; b_[i].ph = b_[i].h; }
    for (int i = 0; i < n_; ++i) if (b_[i].used) integrate(b_[i]);
    collide_ships();
    collide_coast();
}

void NavalPhysics::integrate(body_t &b) {
    const float dt = NPHYS_STEP;
    float c = cosf(b.h), s = -sinf(b.h);                 // forward
    float rx = -s, rz = c;                               // starboard
    float vf = b.vx * c + b.vz * s;                      // through the water, forward
    float vl = b.vx * rx + b.vz * rz;                    // and sideways
    // thrust towards the commanded speed; a ship cannot be driven astern by her sails, so the
    // command is never negative here - the caller asks for a speed she can make
    float thrust = (b.cmd_v - vf) * NP_THRUST_K;
    // drag: linear plus quadratic, much harder across than along
    float df = -(NP_DRAG_FWD * vf + 0.28f * vf * fabsf(vf));
    float dl = -(NP_DRAG_LAT * vl + 0.9f * vl * fabsf(vl));
    float af = thrust + df, al = dl;
    b.vx += (af * c + al * rx) * dt;
    b.vz += (af * s + al * rz) * dt;
    // the helm: turn towards the ordered course at no more than the class's rate, and only as
    // well as she is moving - a ship with no way on does not answer her rudder
    float err = npf_wrap(b.cmd_h - b.h);
    float authority = npf_clamp(fabsf(vf) / (0.35f * b.len + 1e-3f), 0.08f, 1.f);
    float want_yr = npf_clamp(err * 2.2f, -b.cmd_turn, b.cmd_turn) * authority;
    b.yr += (want_yr - b.yr) * npf_clamp(NP_DRAG_YAW * dt, 0.f, 1.f);
    b.x += b.vx * dt; b.z += b.vz * dt; b.h = npf_wrap(b.h + b.yr * dt);
    // how hard she is leaning: the centripetal force on a hull in a turn, smoothed
    float want_heel = npf_clamp(fabsf(b.yr) * fabsf(vf) * 2.6f, 0.f, 1.f);
    b.heel += (want_heel - b.heel) * npf_clamp(2.5f * dt, 0.f, 1.f);
}

// Ship against ship: a broadphase on the bounding circle, then every disc of one hull against
// every disc of the other. Each contact is pushed apart by inverse mass and given a normal
// impulse with restitution, tangential friction, and the yaw its lever arm implies.
void NavalPhysics::collide_ships() {
    contacts_ = 0;
    for (int i = 0; i < n_; ++i) {
        if (!b_[i].used) continue;
        for (int j = i + 1; j < n_; ++j) {
            if (!b_[j].used) continue;
            body_t &A = b_[i], &B = b_[j];
            float dx = B.x - A.x, dz = B.z - A.z;
            float rr = 0.55f * (A.len + B.len);
            if (dx * dx + dz * dz > rr * rr) continue;        // broadphase
            for (int a = 0; a < NPHYS_PARTS; ++a) for (int c2 = 0; c2 < NPHYS_PARTS; ++c2) {
                float ax, az, ar, bx, bz, br;
                part(A, a, &ax, &az, &ar); part(B, c2, &bx, &bz, &br);
                float nx = bx - ax, nz = bz - az;
                float d2 = nx * nx + nz * nz, sum = ar + br;
                if (d2 >= sum * sum || d2 < 1e-9f) continue;
                float d = sqrtf(d2); nx /= d; nz /= d;
                float pen = sum - d;
                contacts_++;
                float inv_sum = A.inv_m + B.inv_m;
                if (inv_sum < 1e-9f) continue;
                // positional correction, leaving a sliver so resting hulls do not buzz
                float corr = (pen > NP_SLOP ? (pen - NP_SLOP) : 0.f) * NP_CORRECT / inv_sum;
                A.x -= nx * corr * A.inv_m; A.z -= nz * corr * A.inv_m;
                B.x += nx * corr * B.inv_m; B.z += nz * corr * B.inv_m;
                // the contact point and the arms from each centre
                float cxp = ax + nx * ar, czp = az + nz * ar;
                float r1x = cxp - A.x, r1z = czp - A.z, r2x = cxp - B.x, r2z = czp - B.z;
                // velocity of the contact point on each body (v + w x r, in 2D)
                float v1x = A.vx - A.yr * r1z, v1z = A.vz + A.yr * r1x;
                float v2x = B.vx - B.yr * r2z, v2z = B.vz + B.yr * r2x;
                float rvx = v2x - v1x, rvz = v2z - v1z;
                float vn = rvx * nx + rvz * nz;
                if (vn > 0.f) continue;                        // already separating
                float rn1 = r1x * nz - r1z * nx, rn2 = r2x * nz - r2z * nx;
                float denom = inv_sum + rn1 * rn1 * A.inv_i + rn2 * rn2 * B.inv_i;
                if (denom < 1e-9f) continue;
                float jn = -(1.f + NP_RESTITUTION) * vn / denom;
                A.vx -= jn * nx * A.inv_m; A.vz -= jn * nz * A.inv_m; A.yr -= jn * rn1 * A.inv_i;
                B.vx += jn * nx * B.inv_m; B.vz += jn * nz * B.inv_m; B.yr += jn * rn2 * B.inv_i;
                // friction along the contact, capped by Coulomb against the normal impulse
                float tx = -nz, tz = nx;
                float vt = (v2x - v1x) * tx + (v2z - v1z) * tz;
                float rt1 = r1x * tz - r1z * tx, rt2 = r2x * tz - r2z * tx;
                float dent = inv_sum + rt1 * rt1 * A.inv_i + rt2 * rt2 * B.inv_i;
                if (dent < 1e-9f) continue;
                float jt = -vt / dent, cap = NP_FRICTION * fabsf(jn);
                jt = npf_clamp(jt, -cap, cap);
                A.vx -= jt * tx * A.inv_m; A.vz -= jt * tz * A.inv_m; A.yr -= jt * rt1 * A.inv_i;
                B.vx += jt * tx * B.inv_m; B.vz += jt * tz * B.inv_m; B.yr += jt * rt2 * B.inv_i;
            }
        }
    }
}

// Ship against the coast. Each hull disc asks the shore profile how deep the water under it
// is; where that is less than her draft she is touching the ground. She is pushed back out to
// seaward, her way into the beach is killed, and the ground drags on that end of her - which
// is what slews a ship broadside-on when she takes the putty.
void NavalPhysics::collide_coast() {
    if (!coast_on_) { for (int i = 0; i < n_; ++i) b_[i].aground = 0; return; }
    const float dt = NPHYS_STEP;
    for (int i = 0; i < n_; ++i) {
        body_t &b = b_[i];
        if (!b.used) continue;
        b.aground = 0;
        for (int k = 0; k < NPHYS_PARTS; ++k) {
            float px, pz, pr;
            part(b, k, &px, &pz, &pr);
            float depth = coast_depth(px, pz);
            float need = b.draft;
            if (depth >= need) continue;
            b.aground = 1;
            float over = need - depth;                       // how far up the shelf she is
            // seaward is -normal; the profile's slope turns "too shallow" into a distance
            float push = over / (cslope_ > 1e-4f ? cslope_ : 1e-4f);
            float mx = -cnx_, mz = -cnz_;
            b.x += mx * push * 0.35f; b.z += mz * push * 0.35f;
            float vn = b.vx * cnx_ + b.vz * cnz_;            // her way towards the land
            if (vn > 0.f) { b.vx -= cnx_ * vn * 1.0f; b.vz -= cnz_ * vn * 1.0f; }
            // the ground holding that end of her: drag, and the yaw it gives
            float rx = px - b.x, rz = pz - b.z;
            float fx = -b.vx * 2.2f, fz = -b.vz * 2.2f;
            b.vx += fx * dt; b.vz += fz * dt;
            b.yr += (rx * fz - rz * fx) * b.inv_i * dt * 0.5f;
            b.yr *= 0.92f;
        }
    }
}

void NavalPhysics::advance(float dt) {
    steps_ = 0;
    if (dt <= 0.f) return;
    acc_ += dt;
    while (acc_ >= NPHYS_STEP && steps_ < NPHYS_MAX_STEPS) { step(); acc_ -= NPHYS_STEP; steps_++; }
    if (acc_ > NPHYS_STEP * NPHYS_MAX_STEPS) acc_ = 0.f;   // a long stall: drop the backlog rather than chase it
}

void NavalPhysics::pose(int id, float *x, float *z, float *h) const {
    if (!valid(id)) { *x = *z = *h = 0.f; return; }
    const body_t &b = b_[id];
    float a = npf_clamp(acc_ / NPHYS_STEP, 0.f, 1.f);
    *x = b.px + (b.x - b.px) * a;
    *z = b.pz + (b.z - b.pz) * a;
    *h = b.ph + npf_wrap(b.h - b.ph) * a;
}

} // namespace mare::game::sim

extern "C" {
void  nphys_reset(void) { mare::game::sim::physics().reset(); }
int   nphys_add(float x, float z, float h, float len, float beam, float draft, float tonnes) { return mare::game::sim::physics().add(x, z, h, len, beam, draft, tonnes); }
void  nphys_drive(int id, float v, float h, float turn) { mare::game::sim::physics().drive(id, v, h, turn); }
void  nphys_set_coast(float ax, float az, float nx, float nz, float amp, float freq, float slope, int on) { mare::game::sim::physics().set_coast(ax, az, nx, nz, amp, freq, slope, on); }
void  nphys_advance(float dt) { mare::game::sim::physics().advance(dt); }
void  nphys_pose(int id, float *x, float *z, float *h) { mare::game::sim::physics().pose(id, x, z, h); }
void  nphys_velocity(int id, float *vx, float *vz, float *yr) { mare::game::sim::physics().velocity(id, vx, vz, yr); }
float nphys_speed(int id) { return mare::game::sim::physics().speed(id); }
float nphys_heel(int id) { return mare::game::sim::physics().heel(id); }
int   nphys_aground(int id) { return mare::game::sim::physics().aground(id); }
void  nphys_teleport(int id, float x, float z, float h) { mare::game::sim::physics().teleport(id, x, z, h); }
int   nphys_steps_last(void) { return mare::game::sim::physics().steps_last(); }
int   nphys_contacts_last(void) { return mare::game::sim::physics().contacts_last(); }
float nphys_alpha(void) { return mare::game::sim::physics().alpha(); }
int   nphys_count(void) { return mare::game::sim::physics().count(); }
}
