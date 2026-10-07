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
    puts("test_gravity ok");
    return 0;
}
