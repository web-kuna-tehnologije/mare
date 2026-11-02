// Headless checks for the naval physics foundation (sim/naval_physics.h, dev.txt P1.3 1-6).
// No GL, no assets: the module is arithmetic, so the whole of it can be exercised here.
//
//   build.bat test / ctest -R physics_test
#include <cstdio>
#include <cmath>
#include <cstdlib>

extern "C" {
#include "../src/sim/naval_physics.h"
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void run(float total, float chunk) {
    float t = 0;
    while (t < total - 1e-6f) { float d = chunk < total - t ? chunk : total - t; nphys_advance(d); t += d; }
}

// 1 + 2: the state after N fixed steps must not depend on how the caller sliced the time.
static void test_fixed_step() {
    float ax, az, ah, bx, bz, bh;
    const float TOTAL = 8.f;
    nphys_reset();
    int a = nphys_add(0, 0, 0, 20, 6, 1.2f, 1000);
    nphys_drive(a, 1.2f, 0.6f, 0.05f);
    run(TOTAL, 1.f / 30.f);
    int steps_a = (int)lroundf(TOTAL * NPHYS_HZ);
    nphys_pose(a, &ax, &az, &ah);
    nphys_reset();
    int b = nphys_add(0, 0, 0, 20, 6, 1.2f, 1000);
    nphys_drive(b, 1.2f, 0.6f, 0.05f);
    run(TOTAL, 1.f / 240.f);
    nphys_pose(b, &bx, &bz, &bh);
    // both runs executed the same whole steps, so the poses agree to float noise
    CHECK(fabsf(ax - bx) < 1e-3f && fabsf(az - bz) < 1e-3f && fabsf(ah - bh) < 1e-4f,
          "30 Hz and 240 Hz callers diverged: (%.5f %.5f %.5f) vs (%.5f %.5f %.5f)", ax, az, ah, bx, bz, bh);
    CHECK(ax > 1.f, "the ship did not make way: x = %.3f", ax);
    printf("  fixed step: %d sub-steps either way, pose %.4f %.4f %.4f\n", steps_a, ax, az, ah);
    // and a frame of zero length changes nothing (a paused battle)
    float px, pz, ph;
    nphys_pose(b, &px, &pz, &ph);
    nphys_advance(0.f);
    nphys_pose(b, &bx, &bz, &bh);
    CHECK(px == bx && pz == bz && ph == bh, "a zero-length advance moved the ship");
    CHECK(nphys_steps_last() == 0, "a zero-length advance ran %d steps", nphys_steps_last());
}

// 3: the drawn pose is interpolated, so it advances smoothly between fixed steps rather than
// standing still and then jumping.
static void test_interpolation() {
    nphys_reset();
    int a = nphys_add(0, 0, 0, 20, 6, 1.2f, 1000);
    nphys_drive(a, 2.f, 0.f, 0.05f);
    run(4.f, 1.f / 120.f);
    float x0, z0, h0, x1, z1, h1, x2, z2, h2;
    nphys_pose(a, &x0, &z0, &h0);
    nphys_advance(1.f / 600.f); nphys_pose(a, &x1, &z1, &h1);   // well under one sub-step
    nphys_advance(1.f / 600.f); nphys_pose(a, &x2, &z2, &h2);
    CHECK(x1 > x0 && x2 > x1, "the pose did not advance between fixed steps: %.6f %.6f %.6f", x0, x1, x2);
    float a0 = nphys_alpha();
    CHECK(a0 >= 0.f && a0 <= 1.f, "alpha out of range: %.3f", a0);
    printf("  interpolation: %.6f -> %.6f -> %.6f, alpha %.3f\n", x0, x1, x2, a0);
}

// 4 + 5: two hulls driven into each other must not pass through, and must end up apart.
static void test_ship_ship() {
    nphys_reset();
    int a = nphys_add(-18, 0, 0, 20, 6, 1.2f, 800);          // steering east
    int b = nphys_add(18, 0, 3.14159265f, 20, 6, 1.2f, 800); // steering west
    nphys_drive(a, 2.0f, 0.f, 0.05f);
    nphys_drive(b, 2.0f, 3.14159265f, 0.05f);
    float closest = 1e9f; int touched = 0; const int fails0 = fails;   // break on *this* test's failures only
    for (int i = 0; i < 1200; ++i) {
        nphys_advance(1.f / 60.f);
        if (nphys_contacts_last() > 0) touched = 1;
        float ax, az, ah, bx, bz, bh;
        nphys_pose(a, &ax, &az, &ah); nphys_pose(b, &bx, &bz, &bh);
        float d = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
        if (d < closest) closest = d;
        CHECK(!(d < 2.f), "hulls interpenetrated to %.3f units apart at step %d", d, i);
        if (fails > fails0) break;
    }
    CHECK(touched, "the two ships never registered a contact");
    CHECK(closest < 16.f, "they never actually met: closest %.2f", closest);
    printf("  ship-ship: contact made, closest centres %.2f units (hulls are 20 long)\n", closest);
}

// 6: a ship standing into the beach grounds and stops instead of sailing up it.
static void test_coast() {
    nphys_reset();
    // shoreline through the origin, the land to +z; 0.15 depth units per unit seaward
    nphys_set_coast(0, 0, 0, 1, 0, 0.01f, 0.15f, 1);
    int a = nphys_add(0, -40, -1.5707963f, 20, 6, 1.2f, 800);   // heading -z... towards the land is +z
    nphys_drive(a, 2.5f, 1.5707963f, 0.08f);                     // steer at the beach
    float z_max = -1e9f;
    for (int i = 0; i < 2400; ++i) {
        nphys_advance(1.f / 60.f);
        float x, z, h; nphys_pose(a, &x, &z, &h);
        if (z > z_max) z_max = z;
    }
    // she may touch, but she cannot get her middle onto dry land: the draft needs 1.2 of
    // depth, which the profile only has seaward of z = -8
    CHECK(z_max < -2.f, "the ship sailed up the beach to z = %.2f", z_max);
    CHECK(nphys_aground(a), "the ship never reported being aground");
    printf("  ship-coast: grounded, furthest inland z = %.2f (land starts at 0)\n", z_max);
}

// the compound hull is three discs, so a ship struck on the bow must swing - not just stop
static void test_yaw_from_contact() {
    nphys_reset();
    int a = nphys_add(0, 0, 0, 20, 6, 1.2f, 800);
    int b = nphys_add(14, -4.5f, 0, 20, 6, 1.2f, 800);   // abeam by less than the two half-beams: she will be grazed
    nphys_drive(a, 2.0f, 0.f, 0.05f);
    nphys_drive(b, 0.f, 0.f, 0.05f);
    float h0, x, z;
    nphys_pose(b, &x, &z, &h0);
    for (int i = 0; i < 900; ++i) nphys_advance(1.f / 60.f);
    float h1; nphys_pose(b, &x, &z, &h1);
    CHECK(fabsf(h1 - h0) > 0.01f, "a glancing blow on the bow produced no yaw (%.4f -> %.4f)", h0, h1);
    printf("  compound hull: struck on the bow, she swung %.1f degrees\n", (h1 - h0) * 57.2958f);
}

// 7: the surface under the hull gives her an attitude - a wave along her length pitches her,
// one across her rolls her, a flat sea does neither
static float test_surface(float x, float z, void *u) { int mode = *(int *)u; return mode == 1 ? 0.8f * sinf(x * 0.3f) : mode == 2 ? 0.6f * sinf(z * 0.6f) : 0.f; }
static void test_buoyancy() {
    nphys_reset();
    int mode = 0; nphys_set_surface(test_surface, &mode);
    int a = nphys_add(2.0f, 0, 0, 20, 6, 1.2f, 800);          // heading +x: pitch answers a wave along x
    for (int i = 0; i < 240; ++i) nphys_advance(1.f / 60.f);
    float hv, pt, rl; nphys_attitude(a, &hv, &pt, &rl);
    CHECK(fabsf(pt) < 1e-3f && fabsf(rl) < 1e-3f, "a flat sea gave an attitude: pitch %.4f roll %.4f", pt, rl);
    mode = 1; for (int i = 0; i < 240; ++i) nphys_advance(1.f / 60.f);
    nphys_attitude(a, &hv, &pt, &rl);
    CHECK(fabsf(pt) > 0.02f, "a wave along the keel did not pitch her: %.4f", pt);
    CHECK(fabsf(rl) < 0.01f, "a wave along the keel rolled her: %.4f", rl);
    mode = 2; for (int i = 0; i < 240; ++i) nphys_advance(1.f / 60.f);
    nphys_attitude(a, &hv, &pt, &rl);
    CHECK(fabsf(rl) > 0.02f, "a wave across the beam did not roll her: %.4f", rl);
    printf("  buoyancy: five-point attitude - roll %.3f on a beam wave (pitch %.3f there, as it should be)\n", rl, pt);
    nphys_set_surface(0, 0);
}

// 8: a shot fired at a hull hits it and nothing else; one fired at the open sea splashes
static void test_ballistics() {
    nphys_reset();
    int a = nphys_add(0, 0, 0, 20, 6, 1.2f, 800);
    int b = nphys_add(0, 30, 0, 20, 6, 1.2f, 800);            // thirty units abeam to starboard
    nphys_fire(a, 0, 1.5f, 3.f, 0.f, 0.03f, 1.f, 60.f);       // at b, nearly flat
    nphys_fire(a, 0, 1.5f, -3.f, 0.f, 0.08f, -1.f, 60.f);     // the other way, into empty sea (a 2.5 s flight)
    int hits = 0, splashes = 0, hitbody = -2;
    for (int i = 0; i < 600; ++i) {
        nphys_advance(1.f / 60.f);
        nphys_event_t ev[16]; int n = nphys_poll_events(ev, 16);
        for (int k = 0; k < n; ++k) { if (ev[k].kind == NPHYS_EV_HIT) { hits++; hitbody = ev[k].body; } else splashes++; }
    }
    CHECK(hits == 1, "expected one hit, got %d", hits);
    CHECK(hitbody == b, "the hit landed on body %d, not %d", hitbody, b);
    CHECK(splashes == 1, "expected one splash, got %d", splashes);
    CHECK(nphys_shots() == 0, "%d shots still in the air", nphys_shots());
    printf("  ballistics: 1 hit on the target hull, 1 splash in open water, nothing left flying\n");
}

// 9: an impulse off the centre pushes her the other way and turns her
static void test_recoil() {
    nphys_reset();
    int a = nphys_add(0, 0, 0, 20, 6, 1.2f, 800);
    float vx, vz, yr;
    nphys_impulse(a, 6.f, 3.f, 0.f, -400.f);                  // a broadside to starboard from a gun forward: the recoil is to port
    nphys_velocity(a, &vx, &vz, &yr);
    CHECK(vz < -1e-4f, "the recoil did not push her to port: vz %.5f", vz);
    CHECK(fabsf(yr) > 1e-5f, "an off-centre impulse gave no yaw: %.6f", yr);
    printf("  recoil: vz %.4f, yaw rate %.5f from an impulse 6 units forward of the centre\n", vz, yr);
}

// 0.22.17 (P1.3 "component / zone damage model"): a blow between wind and water starts her
// leaking and the leak takes her down; a blow aloft costs her rig and her speed, not her life.
static void test_damage() {
    nphys_reset();
    int a = nphys_add(0, 0, 0, 19.f, 5.f, 1.4f, 500.f);
    nphys_damage_t d;
    nphys_damage_get(a, &d);
    CHECK(d.hull == 0.f && d.flood == 0.f && !d.sunk, "a fresh hull is already damaged");
    // aloft: the rig suffers, she slows, she does not flood
    nphys_damage_add(a, 2, 0.30f);
    nphys_damage_get(a, &d);
    CHECK(d.rig > 0.3f && d.hull == 0.f, "a blow aloft damaged the wrong thing: rig %.2f hull %.2f", d.rig, d.hull);
    CHECK(nphys_speed_factor(a) < 1.f, "a wounded rig did not slow her: %.3f", nphys_speed_factor(a));
    CHECK(d.masts_down >= 1, "no mast went by the board at rig %.2f", d.rig);
    run(2.f, 0.05f);
    nphys_damage_get(a, &d);
    CHECK(d.flood == 0.f, "a blow aloft made her leak: flood %.3f", d.flood);
    printf("  damage: rig %.2f, %d mast(s) down, speed factor %.2f, dry\n", d.rig, d.masts_down, nphys_speed_factor(a));
    // between wind and water: she makes water and in the end she goes
    int b = nphys_add(100.f, 0, 0, 19.f, 5.f, 1.4f, 500.f);
    for (int i = 0; i < 12; ++i) nphys_damage_add(b, 0, 0.12f);
    nphys_damage_get(b, &d);
    CHECK(d.hull > 0.9f, "twelve balls below the line left the hull at %.2f", d.hull);
    float flood0 = d.flood;
    run(2.f, 0.05f);
    nphys_damage_get(b, &d);
    CHECK(d.flood > flood0, "a holed hull is not making water: %.3f -> %.3f", flood0, d.flood);
    // advance() runs at most NPHYS_MAX_STEPS in one call, so sim time is fed in frame-sized
    // slices, as the game feeds it. A hull holed through floods in about twenty seconds.
    for (int i = 0; i < 1600 && !nphys_sunk(b); ++i) nphys_advance(0.05f);
    CHECK(nphys_sunk(b), "a hull holed through never sank: flood %.3f", d.flood);
    CHECK(nphys_wrecks() >= 1, "the wreck was not counted");
    CHECK(nphys_speed_factor(b) == 0.f, "a sunk hull still makes way");
    nphys_damage_get(b, &d);
    printf("  damage: holed hull %.2f flooded to %.2f and sank; %d wreck(s)\n", d.hull, d.flood, nphys_wrecks());
}

int main(void) {
    printf("physics_test: naval physics foundation (P1.3 1-9) + the damage model\n");
    test_buoyancy();
    test_ballistics();
    test_recoil();
    test_fixed_step();
    test_interpolation();
    test_ship_ship();
    test_coast();
    test_yaw_from_contact();
    test_damage();
    if (fails) { printf("physics_test: %d failure(s)\n", fails); return 1; }
    printf("physics_test: all checks passed\n");
    return 0;
}
