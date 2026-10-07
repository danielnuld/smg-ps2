/* camera_area_at: shapes, rotation, last match wins. */
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../src/camera.h"

int main(void)
{
    CamArea a[3] = {
        {0, 1, 0, {0, 0, 0}, {0, 0, 0}, {2, 2, 2}},      /* box: |x|, |y|, |z| < 1000 */
        {1, 1, 1, {0, 0, 0}, {0, 0, 90}, {1, 4, 1}},     /* cylinder rotated 90 about Z: axis -X, 0..2000 long, r 500 */
        {2, 1, 2, {5000, 0, 0}, {0, 0, 0}, {1, 1, 1}},   /* sphere r 500 at x 5000 */
    };
    assert(camera_area_at(a, 3, v3(900, 0, 0)) == 0);
    assert(camera_area_at(a, 3, v3(-900, 0, 0)) == 1);   /* inside both: the last one wins */
    assert(camera_area_at(a, 3, v3(-1500, 100, 0)) == 1);
    assert(camera_area_at(a, 3, v3(1500, 0, 0)) == -1);
    assert(camera_area_at(a, 3, v3(5300, 0, 300)) == 2);
    assert(camera_area_at(a, 3, v3(0, 0, 0)) == 1);       /* cylinder base on the origin: h = 0 is inside */
    /* every ported camera type, active through one area covering everything, with Mario walking a circle: the pose
     * stays finite, the eye apart from the watch point once settled (the 0.3 smoothing lags) and the up unit */
    for (int type = 0; type <= CAM_CUBE_PLANET; type++) {
        static CamParam c;
        memset(&c, 0, sizeof c);
        c.type = type; c.dist = 1200; c.angle_a = 0.3f; c.angle_b = 0.2f; c.fovy = 45; c.camint = 30; c.vpanuse = 1;
        c.wpoint[0] = 0; c.wpoint[1] = 0; c.wpoint[2] = 2000;
        c.axis[0] = 0.2f; c.axis[1] = 1; c.axis[2] = 0; c.up[0] = 1500; c.up[1] = 1; c.woffset[1] = 100;
        c.vpanaxis[1] = 1; c.uplay = 300; c.lplay = 800;
        if (type == CAM_CHARMED_FIX) { c.axis[0] = 0; c.axis[1] = 3000; c.axis[2] = 0; c.up[0] = 0; c.up[1] = 1;
                                       c.wpoint[2] = -800; }
        CamArea all = {0, 0, 0, {0, 0, 0}, {0, 0, 0}, {100, 100, 100}};
        CamTarget t = {v3(1000, 0, 0), v3(0, 0, 1), v3(0, 1, 0), 0};
        camera_init(&c, 1, &all, 1, NULL, &t);
        for (int f = 0; f < 240; f++) {
            float a = f * 0.02f;
            t.pos = v3(1000 * cosf(a), 50 * sinf(3 * a), 1000 * sinf(a));
            t.front = v3(-sinf(a), 0, cosf(a));
            t.jumping = (f / 40) & 1;
            camera_update(&t);
            V3 e, w, u;
            float fov;
            camera_pose(&e, &w, &u, &fov);
            float d = vlen(vsub(e, w));
            if (!(isfinite(e.x) && isfinite(e.y) && isfinite(e.z) && isfinite(w.x) && (f < 30 || d > 100.0f) && fabsf(vlen(u) - 1) < 0.01f)) {
                printf("camera type %d (%s) frame %d: eye %g %g %g watch %g %g %g up len %g\n", type,
                       camera_type_name(), f, e.x, e.y, e.z, w.x, w.y, w.z, vlen(u));
                return 1;
            }
        }
    }
    printf("test_camera ok\n");
    return 0;
}
