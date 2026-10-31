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
    float closest = 1e9f; int touched = 0;
    for (int i = 0; i < 1200; ++i) {
        nphys_advance(1.f / 60.f);
        if (nphys_contacts_last() > 0) touched = 1;
        float ax, az, ah, bx, bz, bh;
        nphys_pose(a, &ax, &az, &ah); nphys_pose(b, &bx, &bz, &bh);
        float d = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
        if (d < closest) closest = d;
        CHECK(!(d < 2.f), "hulls interpenetrated to %.3f units apart at step %d", d, i);
        if (fails) break;
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

int main(void) {
    printf("physics_test: naval physics foundation (P1.3 1-6)\n");
    test_fixed_step();
    test_interpolation();
    test_ship_ship();
    test_coast();
    test_yaw_from_contact();
    if (fails) { printf("physics_test: %d failure(s)\n", fails); return 1; }
    printf("physics_test: all checks passed\n");
    return 0;
}
