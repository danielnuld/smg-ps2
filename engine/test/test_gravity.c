/* Host test (gcc): build with engine/test/run.sh. Values worked out by hand from Petari's rules. */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/gravity.h"

static int near3(const float a[3], float x, float y, float z) { return fabsf(a[0] - x) + fabsf(a[1] - y) + fabsf(a[2] - z) < 1e-4f; }

/* axis-aligned gravity: axes scaled by sx, sy, sz, translation t */
static Gravity mk(int kind, int flags, int prio, float range, float sx, float sy, float sz, float tx, float ty, float tz)
{
    Gravity g = {kind, flags, prio, 0, range, 0, {{sx, 0, 0}, {0, sy, 0}, {0, 0, sz}, {tx, ty, tz}}, {2000, 0, 0, 0}};
    return g;
}

int main(void)
{
    float v[3];
    /* the Gateway pair: point (range 8200) + plane (sphere range 2300, priority 1) */
    Gravity g[2] = {mk(GRAV_POINT, 0, 0, 8200, 1, 1, 1, 0, 0, 0), mk(GRAV_PLANE, 0, 1, 2300, 1, 1, 1, 0, 3305.241f, 0)};
    assert(gravity_at(g, 2, (float[3]){3000, 0, 0}, v) && near3(v, -1, 0, 0));      /* only the point reaches */
    assert(gravity_at(g, 2, (float[3]){500, 3400, 0}, v) && near3(v, 0, -1, 0));    /* priority 1 wins */
    assert(!gravity_at(g, 2, (float[3]){9000, 0, 0}, v) && near3(v, 0, 0, 0));      /* out of every range */
    /* equal priorities add up: midway between two points the pulls cancel sideways */
    Gravity two[2] = {mk(GRAV_POINT, 0, 0, -1, 1, 1, 1, -1000, 0, 0), mk(GRAV_POINT, 0, 0, -1, 1, 1, 1, 1000, 0, 0)};
    assert(gravity_at(two, 2, (float[3]){0, 1000, 0}, v) && near3(v, 0, -1, 0));
    Gravity inv = mk(GRAV_POINT, 0, 0, -1, 1, 1, 1, 0, 0, 0);
    inv.inverse = 1;
    assert(gravity_at(&inv, 1, (float[3]){0, 500, 0}, v) && near3(v, 0, 1, 0));
    /* plane in a box of half extents 1000 x 500 x 1000 centred at y 500: down inside, nothing outside */
    Gravity box = mk(GRAV_PLANE_BOX, 3, 0, -1, 1000, 500, 1000, 0, 500, 0);
    assert(gravity_at(&box, 1, (float[3]){900, 10, -900}, v) && near3(v, 0, -1, 0));
    assert(!gravity_at(&box, 1, (float[3]){1100, 10, 0}, v));
    /* plane in a cylinder r 500, h 1000 */
    Gravity cyl = mk(GRAV_PLANE_CYLINDER, 0, 0, -1, 1, 1, 1, 0, 0, 0);
    cyl.p[1] = 500; cyl.p[2] = 1000;
    assert(gravity_at(&cyl, 1, (float[3]){300, 900, 0}, v) && near3(v, 0, -1, 0));
    assert(!gravity_at(&cyl, 1, (float[3]){600, 900, 0}, v) && !gravity_at(&cyl, 1, (float[3]){0, -10, 0}, v));
    /* cube of half extents 500, all faces: face, edge and corner regions; inside has none */
    Gravity cube = mk(GRAV_CUBE, 63, 0, -1, 500, 500, 500, 0, 0, 0);
    assert(gravity_at(&cube, 1, (float[3]){0, 800, 0}, v) && near3(v, 0, -1, 0));
    assert(gravity_at(&cube, 1, (float[3]){-900, 100, 0}, v) && near3(v, 1, 0, 0));
    assert(gravity_at(&cube, 1, (float[3]){600, 600, 0}, v) && near3(v, -0.7071068f, -0.7071068f, 0));
    assert(gravity_at(&cube, 1, (float[3]){600, 600, 600}, v) && near3(v, -0.5773503f, -0.5773503f, -0.5773503f));
    assert(!gravity_at(&cube, 1, (float[3]){0, 0, 0}, v));
    cube.flags = 63 & ~4; /* +Y face off: above the cube there is no gravity */
    assert(!gravity_at(&cube, 1, (float[3]){0, 800, 0}, v));
    /* disk r 1000, normal +Y, one side, edge gravity on */
    Gravity disk = mk(GRAV_DISK, 2, 0, -1, 1, 1, 0, 0, 0, 0);
    disk.m[2][2] = 1; disk.p[0] = 1000; disk.p[1] = -1;
    assert(gravity_at(&disk, 1, (float[3]){500, 200, 0}, v) && near3(v, 0, -1, 0));
    assert(gravity_at(&disk, 1, (float[3]){2000, 0, 0}, v) && near3(v, -1, 0, 0));
    assert(!gravity_at(&disk, 1, (float[3]){500, -200, 0}, v)); /* underside, not both sides */
    /* segment 1000 long along +Y from the origin, edges valid */
    Gravity seg = mk(GRAV_SEGMENT, 3, 0, -1, 1, 1, 1, 0, 0, 0);
    seg.p[0] = 1000; seg.p[1] = -1;
    assert(gravity_at(&seg, 1, (float[3]){300, 500, 0}, v) && near3(v, -1, 0, 0));
    assert(gravity_at(&seg, 1, (float[3]){0, 1500, 0}, v) && near3(v, 0, -1, 0));
    seg.flags = 1; /* top edge off */
    assert(!gravity_at(&seg, 1, (float[3]){0, 1500, 0}, v));
    /* torus: radius 1000, disk 400 (inner rim 600), axis +Y, both sides, both edges */
    Gravity tor = mk(GRAV_TORUS, 1 | 3 << 1, 0, -1, 1, 1, 1, 0, 0, 0);
    tor.p[0] = 1000; tor.p[1] = 400;
    assert(gravity_at(&tor, 1, (float[3]){800, 300, 0}, v) && near3(v, 0, -1, 0));   /* over the ring */
    assert(gravity_at(&tor, 1, (float[3]){800, -300, 0}, v) && near3(v, 0, 1, 0));   /* under it */
    assert(gravity_at(&tor, 1, (float[3]){1500, 0, 0}, v) && near3(v, -1, 0, 0));    /* outer edge */
    assert(gravity_at(&tor, 1, (float[3]){200, 0, 0}, v) && near3(v, 1, 0, 0));      /* inner edge */
    tor.flags = 0; /* one side, no edges */
    assert(!gravity_at(&tor, 1, (float[3]){800, -300, 0}, v) && !gravity_at(&tor, 1, (float[3]){1500, 10, 0}, v));
    /* cone: base radius 1000 at the origin, apex at y 1000, bottom on */
    Gravity con = mk(GRAV_CONE, 1, 0, -1, 1000, 1000, 1000, 0, 0, 0);
    con.p[0] = 0;
    assert(gravity_at(&con, 1, (float[3]){0, 1500, 0}, v) && near3(v, 0, -1, 0));    /* above the apex */
    assert(gravity_at(&con, 1, (float[3]){500, -200, 0}, v) && near3(v, 0, 1, 0));   /* under the base */
    assert(gravity_at(&con, 1, (float[3]){1000, 1000, 0}, v) && near3(v, -0.7071068f, -0.7071068f, 0)); /* side */
    con.flags = 0;
    assert(!gravity_at(&con, 1, (float[3]){500, -200, 0}, v));
    /* wire: an L of 3 points after the record, range 1000 */
    struct { Gravity g; float pts[9]; } wire = {mk(GRAV_WIRE, 0, 0, 1000, 1, 1, 1, 0, 0, 0), {0, 0, 0, 1000, 0, 0, 1000, 1000, 0}};
    wire.g.p[0] = 0; wire.g.p[1] = 3;
    assert(gravity_at(&wire.g, 1, (float[3]){500, 300, 0}, v) && near3(v, 0, -1, 0));
    assert(gravity_at(&wire.g, 1, (float[3]){1300, 500, 0}, v) && near3(v, -1, 0, 0));
    assert(!gravity_at(&wire.g, 1, (float[3]){500, 2500, 0}, v)); /* 1000+ from both segments */
    /* two equal pulls that cancel give no direction; a gravity switched off is left out */
    assert(!gravity_at(two, 2, (float[3]){0, 0, 0}, v));
    unsigned char off[2] = {1, 0};
    gravity_off = off;
    assert(gravity_at(two, 2, (float[3]){0, 0, 0}, v) && near3(v, 1, 0, 0));
    gravity_off = 0;
    { /* the grid (n <= 2048) against the walk over all (n > 2048: none): 300 point / disk / cube / plane (sphere, box,
       * cylinder) gravities spread over a galaxy, the same answers at points anywhere and near them; the walk's array
       * padded with gravities of priority -5 (never taken) */
        static Gravity many[2049];
        static float want[3000][4], pts[3000][3];
        static const int kinds[6] = {GRAV_POINT, GRAV_DISK, GRAV_CUBE, GRAV_PLANE, GRAV_PLANE_BOX, GRAV_PLANE_CYLINDER};
        unsigned seed = 1;
#define RND(a, b) ((a) + ((b) - (a)) * ((seed = seed * 1103515245u + 12345u) >> 8 & 0xFFFF) / 65535.0f)
        for (int i = 0; i < 300; i++) {
            int kind = kinds[i % 6];
            float s = RND(100, 1500), pr = RND(0, 3), rg = RND(200, 4000);
            float x = RND(-60000, 60000), y = RND(-20000, 20000), z = RND(-60000, 60000);
            many[i] = mk(kind, kind == GRAV_CUBE ? 63 : kind == GRAV_PLANE_BOX ? 3 : kind == GRAV_PLANE_CYLINDER ? 0 : 2,
                         (int)pr, rg, s, s * 0.5f, s, x, y, z);
            if (kind == GRAV_PLANE_CYLINDER) { many[i].p[1] = s; many[i].p[2] = 2 * s; }
        }
        for (int i = 300; i < 2049; i++) many[i] = mk(GRAV_POINT, 0, -5, 0, 1, 1, 1, 1e8f, 0, 0);
        for (int k = 0; k < 3000; k++) {
            if (k & 1) { pts[k][0] = RND(-70000, 70000); pts[k][1] = RND(-25000, 25000); pts[k][2] = RND(-70000, 70000); continue; }
            const Gravity *q = &many[k % 300];
            for (int a = 0; a < 3; a++) pts[k][a] = q->m[3][a] + RND(-6000, 6000);
        }
        for (int k = 0; k < 3000; k++) want[k][3] = (float)gravity_at(many, 2049, pts[k], want[k]);
        int hits = 0;
        for (int k = 0; k < 3000; k++) {
            int ok = gravity_at(many, 300, pts[k], v);
            assert(ok == (int)want[k][3] && v[0] == want[k][0] && v[1] == want[k][1] && v[2] == want[k][2]);
            hits += ok;
        }
        printf("gravity grid: 3000 points, %d with gravity, the same as the walk over all\n", hits);
#undef RND
    }
    puts("test_gravity ok");
    return 0;
}
