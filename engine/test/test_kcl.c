/* Host test for kcl.c on the real planet KCL (fixture written by tools/kcl_fixture.py:
 * planet.kcl = tools/slice.py kcl_to_le output; planet_prisms.txt = "index centroid[3] face_normal[3]" per line). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/kcl.h"

int main(void)
{
    FILE *f = fopen("fixtures/planet.kcl", "rb");
    if (!f) { puts("test_kcl skipped (no fixture)"); return 0; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *k = malloc(n);
    assert(fread(k, 1, n, f) == (size_t)n); fclose(f);
    FILE *t = fopen("fixtures/planet_prisms.txt", "r");
    int idx; float c[3], nr[3], checked = 0;
    KclHit hits[32];
    while (fscanf(t, "%d %f %f %f %f %f %f", &idx, &c[0], &c[1], &c[2], &nr[0], &nr[1], &nr[2]) == 7) {
        /* sphere of radius 30 whose centre sits 20 above the triangle's centroid: the prism is hit on its face, 10 deep */
        float p[3] = {c[0] + nr[0] * 20, c[1] + nr[1] * 20, c[2] + nr[2] * 20};
        int m = kcl_check_sphere(k, p, 30.0f, hits, 32), found = 0;
        for (int i = 0; i < m; i++)
            if (hits[i].prism == idx) {
                assert(hits[i].flag == 1 && fabsf(hits[i].dist - 10.0f) < 0.05f);
                const float *fn = kcl_face_normal(k, idx);
                assert(fabsf(fn[0] - nr[0]) + fabsf(fn[1] - nr[1]) + fabsf(fn[2] - nr[2]) < 1e-4f);
                found = 1;
            }
        assert(found);
        /* 100 above: out of the 30 radius */
        float q[3] = {c[0] + nr[0] * 100, c[1] + nr[1] * 100, c[2] + nr[2] * 100};
        m = kcl_check_sphere(k, q, 30.0f, hits, 32);
        for (int i = 0; i < m; i++) assert(hits[i].prism != idx);
        checked++;
    }
    assert(checked == 3);
    /* far outside the planet's box: nothing */
    assert(kcl_check_sphere(k, (float[3]){0, 9000, 0}, 50.0f, hits, 32) == 0);
    puts("test_kcl ok");
    return 0;
}
