/* Host test (gcc): build with engine/test/run.sh. Values worked out by hand from Petari's rules. */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/gravity.h"

static int near3(const float a[3], float x, float y, float z) { return fabsf(a[0] - x) + fabsf(a[1] - y) + fabsf(a[2] - z) < 1e-4f; }

int main(void)
{
    /* the slice's two gravities (tools/slice.py GRAV for HeavensDoorGalaxy 2) */
    Gravity g[2] = {
        {0, 1, 0, 0, 8200.0f, 0.0f, {0, 0, 0}, {0, 1, 0}},
        {1, 1, 1, 0, 2300.0f, 0.0f, {0, 3305.241f, 0}, {0, 1, 0}},
    };
    float v[3];
    /* on the planet's side: only the point gravity reaches -> towards the centre */
    assert(gravity_at(g, 2, (float[3]){3000, 0, 0}, v) && near3(v, -1, 0, 0));
    /* inside the plane gravity's sphere: priority 1 wins -> straight down */
    assert(gravity_at(g, 2, (float[3]){500, 3400, 0}, v) && near3(v, 0, -1, 0));
    /* outside every range: no gravity */
    assert(!gravity_at(g, 2, (float[3]){9000, 0, 0}, v) && near3(v, 0, 0, 0));
    /* two point gravities of equal priority add up: midway between them the pulls cancel, off-axis they sum */
    Gravity two[2] = {{0, 0, 0, 0, -1, 0, {-1000, 0, 0}, {0}}, {0, 0, 0, 0, -1, 0, {1000, 0, 0}, {0}}};
    assert(gravity_at(two, 2, (float[3]){0, 1000, 0}, v) && near3(v, 0, -1, 0));
    /* inverse flips the pull */
    Gravity inv = {0, 0, 0, 1, -1, 0, {0, 0, 0}, {0}};
    assert(gravity_at(&inv, 1, (float[3]){0, 500, 0}, v) && near3(v, 0, 1, 0));
    puts("test_gravity ok");
    return 0;
}
