/* camera_area_at: shapes, rotation, last match wins. */
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../src/camera.h"

/* a hollow sphere of radius SP_R at the origin, its gravity out to the shell (the map's faces inside it) */
#define SP_R 1500.0f
static int sp_gravity(V3 p, V3 *g) { float l = vlen(p); *g = l > 0 ? vmul(p, 1.0f / l) : v3(0, 0, 0); return l > 0; }
static int sp_poly(V3 a, V3 d, V3 *hp)
{
    float A = vdot(d, d), B = 2 * vdot(a, d), C = vdot(a, a) - SP_R * SP_R, D = B * B - 4 * A * C;
    if (A <= 0 || D < 0) return 0;
    float t = (-B + sqrtf(D)) / (2 * A); /* leaving the inside: the face met from its front */
    if (t < 0 || t > 1 || vdot(vadd(a, vmul(d, t)), d) <= 0) return 0;
    *hp = vadd(a, vmul(d, t));
    return 1;
}
static int sp_ball(V3 c, float r, V3 *n, float *depth, int max)
{
    float l = vlen(c);
    if (max < 1 || l + r <= SP_R || l <= 0) return 0;
    *n = vmul(c, -1.0f / l); *depth = l + r - SP_R;
    return 1;
}

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
    { /* a shake: 24 frames of offsets (CameraShakePatternSingly), none at the 25th; not restarted while it runs */
        CamTarget t = {v3(1000, 0, 0), v3(0, 0, 1), v3(0, 1, 0), 0};
        float most = 0;
        camera_shake(SHAKE_NORMAL);
        for (int f = 1; f <= 25; f++) {
            if (f == 10) camera_shake(SHAKE_NORMAL);
            camera_update(&t);
            most = fmaxf(most, fabsf(camera_shake_y()));
            assert(f < 25 || camera_shake_y() == 0.0f);
        }
        assert(most > 0.03f && most < 30.0f / 456.0f);
        camera_update(&t);
        assert(camera_shake_y() == 0.0f);
    }
    { /* CAM_INWARD_SPHERE (HeavensDoorInsideZone's c:003c: angleA 1400, dist 1400, angleB 300) inside a hollow sphere of
       * radius R with gravity out to its shell: Mario walks half round its inside; the eye stays inside, off the shell,
       * near him */
        static CamParam c;
        memset(&c, 0, sizeof c);
        c.type = CAM_INWARD_SPHERE; c.dist = 1400; c.angle_a = 1400; c.angle_b = 300; c.camint = 120; c.fovy = 45;
        camera_gravity = sp_gravity; camera_map_poly = sp_poly; camera_map_ball = sp_ball;
        CamArea all = {2, 0, 0, {0, 0, 0}, {0, 0, 0}, {100, 100, 100}};
        CamTarget t = {v3(0, SP_R, 0), v3(0, 0, 1), v3(0, -1, 0), 0};
        camera_init(&c, 1, &all, 1, NULL, &t);
        for (int f = 0; f < 600; f++) {
            float a = fminf(f * 15.0f / SP_R, 3.1f);
            t.pos = v3(0, SP_R * cosf(a), SP_R * sinf(a));
            t.up = vmul(t.pos, -1.0f / SP_R);
            t.front = v3(0, -sinf(a), cosf(a));
            camera_update(&t);
            V3 e, w, u;
            float fov;
            camera_pose(&e, &w, &u, &fov);
            if (f > 120 && !(vlen(e) < SP_R - 100 && vlen(vsub(e, t.pos)) < 2500 && fabsf(vlen(u) - 1) < 0.01f)) {
                printf("inward sphere frame %d: eye %g %g %g (r %g) mario %g %g %g\n", f, e.x, e.y, e.z, vlen(e),
                       t.pos.x, t.pos.y, t.pos.z);
                return 1;
            }
        }
        camera_gravity = NULL; camera_map_poly = NULL; camera_map_ball = NULL;
    }
    printf("test_camera ok\n");
    return 0;
}
