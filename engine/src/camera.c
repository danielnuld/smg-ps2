#include "camera.h"

#include <stdio.h>

/* Petari AreaForm.cpp: cube 45-54 + 104-125 (centre origin, 1000 x scale), cylinder 241-313 (base origin, radius
 * 500 x scale_x, height 500 x scale_y along R * Y), sphere 160-166 + 277-285 (radius 500 x scale_x). Rotation order
 * as every placement: R = Rz * Ry * Rx (degrees). */
static int inside(const CamArea *a, V3 p)
{
    V3 d = v3(p.x - a->pos[0], p.y - a->pos[1], p.z - a->pos[2]);
    if (a->shape == 2)
        return vlen(d) < 500.0f * a->scale[0];
    const float k = 3.14159265f / 180.0f;
    float cx = cosf(a->rot[0] * k), sx = sinf(a->rot[0] * k), cy = cosf(a->rot[1] * k), sy = sinf(a->rot[1] * k);
    float cz = cosf(a->rot[2] * k), sz = sinf(a->rot[2] * k);
    V3 x = v3(cz * cy, sz * cy, -sy);                                    /* columns of Rz * Ry * Rx */
    V3 y = v3(cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx);
    V3 z = v3(cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx);
    if (a->shape == 1) {
        float h = vdot(d, y);
        return h >= 0.0f && h <= 500.0f * a->scale[1] && vlen(vtangent(d, y)) < 500.0f * a->scale[0];
    }
    float lx = vdot(d, x), ly = vdot(d, y), lz = vdot(d, z);
    float hx = 500.0f * a->scale[0], hy = 500.0f * a->scale[1], hz = 500.0f * a->scale[2];
    return lx >= -hx && lx < hx && ly >= -hy && ly < hy && lz >= -hz && lz < hz;
}

int camera_area_at(const CamArea *a, int n, V3 p)
{
    for (int i = n - 1; i >= 0; i--)
        if (inside(&a[i], p))
            return i;
    return -1;
}

/* --- runtime (Petari CameraManGame, Camera*, CameraLocalUtil, CameraHeightArrange, CameraViewInterpolator) --- */

static const CamParam *cams;
static const CamArea *areas;
static int ncams, nareas;
static const float (*zones)[16];
static int cur = -2;                  /* active CAMS index, -1 = default (null) camera, -2 = none yet */
static CamParam null_cam;             /* CameraHolder default: XZ_PARA with CameraParamChunk defaults */
static V3 look_off, last_target;      /* smoothed loffset / loffsetv (makeWatchPoint) */
static float vpan_h;                  /* smoothed watch height along the vpan axis */
static int vpan_init;
static V3 type_eye, type_up;          /* the type camera's own previous output (Tower, Follow, WonderPlanet read it) */
static int fresh;                     /* first frame of a camera (its reset()) */
static V3 cube_up;                    /* CameraCubePlanet::mUp */
static V3 i_eye, i_watch, i_up;       /* interpolated towards the type camera over camint frames */
static int timer, camint;
static V3 f_eye, f_watch, f_up;       /* final, after the 0.3 per-frame smoothing */
static float f_fovy = 45.0f;

#define PI 3.14159265f
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static V3 vec(const float *f) { return v3(f[0], f[1], f[2]); }
static V3 polar(float len, float pitch, float yaw) /* CameraCalc.cpp:25-29 */
{
    return v3(len * cosf(pitch) * sinf(yaw), len * sinf(pitch), len * cosf(pitch) * cosf(yaw));
}
static const CamParam *param(void) { return cur >= 0 ? &cams[cur] : &null_cam; }

/* rotate v about unit axis k by angle a (TQuat4f::setRotate(axis, angle) + transform), Rodrigues */
static V3 rot_axis(V3 v, V3 k, float a)
{
    float c = cosf(a), s = sinf(a);
    return vadd(vadd(vmul(v, c), vmul(vcross(k, v), s)), vmul(k, vdot(k, v) * (1.0f - c)));
}

/* rotate v by `rate` of the shortest arc taking unit a to unit b (TQuat4f::setRotate(a, b, rate)) */
static V3 rotate_part(V3 a, V3 b, V3 v, float rate)
{
    V3 ax = vcross(a, b);
    float s = vlen(ax);
    if (s < 1e-6f) return v;
    return rot_axis(v, vmul(ax, 1.0f / s), atan2f(s, vdot(a, b)) * rate);
}

/* rotate v by the rotation that takes unit a to unit b (TQuat4f::setRotate + transform), Rodrigues */
static V3 rotate_between(V3 a, V3 b, V3 v)
{
    V3 ax = vcross(a, b);
    float s = vlen(ax), c = vdot(a, b);
    if (s < 1e-6f) return v;
    ax = vmul(ax, 1.0f / s);
    return vadd(vadd(vmul(v, c), vmul(vcross(ax, v), s)), vmul(ax, vdot(ax, v) * (1.0f - c)));
}

/* zone frame: rotation columns m[0..2], m[4..6], m[8..10] (orthonormal), translation m[12..14] */
static V3 to_local(const float *m, V3 v, int point)
{
    if (point) v = vsub(v, v3(m[12], m[13], m[14]));
    return v3(vdot(v, vec(m)), vdot(v, vec(m + 4)), vdot(v, vec(m + 8)));
}
static V3 to_world(const float *m, V3 v, int point)
{
    V3 w = vadd(vadd(vmul(vec(m), v.x), vmul(vec(m + 4), v.y)), vmul(vec(m + 8), v.z));
    return point ? vadd(w, v3(m[12], m[13], m[14])) : w;
}

static void type_pose(const CamParam *p, const CamTarget *t, V3 move, V3 *eye, V3 *watch, V3 *up);

/* type_pose in the camera's zone frame: target and the type's previous output in, the pose back out */
static void zone_pose(const CamParam *p, const CamTarget *t, V3 move, V3 *eye, V3 *watch, V3 *up)
{
    if (!zones || p->zone == 0) {
        type_pose(p, t, move, eye, watch, up);
        return;
    }
    const float *m = zones[p->zone];
    CamTarget l = {to_local(m, t->pos, 1), to_local(m, t->front, 0), to_local(m, t->up, 0), t->jumping, t->flying,
                   to_local(m, t->fly_dir, 0)};
    V3 se = type_eye, su = type_up;
    type_eye = to_local(m, se, 1);
    type_up = to_local(m, su, 0);
    type_pose(p, &l, to_local(m, move, 0), eye, watch, up);
    type_eye = se;
    type_up = su;
    *eye = to_world(m, *eye, 1);
    *watch = to_world(m, *watch, 1);
    *up = to_world(m, *up, 0);
}

void camera_init(const CamParam *c, int nc, const CamArea *a, int na, const float (*z)[16], const CamTarget *t)
{
    cams = c; ncams = nc; areas = a; nareas = na; zones = z;
    null_cam.type = CAM_XZ_PARA; null_cam.dist = 1200.0f; null_cam.angle_b = 0.3f; null_cam.fovy = 45.0f;
    null_cam.camint = 0; null_cam.vpanuse = 1; null_cam.vpanaxis[1] = 1.0f; null_cam.uplay = 300.0f;
    last_target = t->pos;
    camera_update(t);
}

/* CameraLocalUtil::makeWatchPoint (CameraLocalUtil.cpp:219-242): woffset in the zone frame, loffset / loffsetv in
 * the target frame, eased by how far the target moved. */
static V3 watch_point(const CamParam *p, const CamTarget *t, V3 move)
{
    float k = p->flags & 4 ? 1.0f : clampf(vlen(move) * (0.1f / 15.0f), 0.0f, 1.0f);
    V3 want = vadd(vmul(t->front, p->loffset), vmul(t->up, p->loffsetv));
    look_off = vadd(look_off, vmul(vsub(want, look_off), k));
    return vadd(vadd(t->pos, vec(p->woffset)), look_off);
}

/* CameraHeightArrange, reduced: the watch height along vpanaxis is frozen while jumping and eased at 0.05.
 * ponytail: the upper / lower screen pushes are one clamp at uplay instead of the push state machine. */
static V3 vertical_pan(const CamParam *p, const CamTarget *t, V3 w)
{
    if (!p->vpanuse) return w;
    V3 g = vnorm(vec(p->vpanaxis));
    if (vlen(g) < 0.5f) g = v3(0, 1, 0);
    float h = vdot(g, w);
    if (!vpan_init) { vpan_h = h; vpan_init = 1; }
    if (!t->jumping) vpan_h += 0.05f * (h - vpan_h);
    vpan_h = clampf(vpan_h, h - p->uplay, h + p->uplay);
    return vadd(w, vmul(g, vpan_h - h));
}

static void type_pose(const CamParam *p, const CamTarget *t, V3 move, V3 *eye, V3 *watch, V3 *up)
{
    V3 Y = v3(0, 1, 0);
    V3 w = watch_point(p, t, move);
    int type = p->type == 255 ? CAM_XZ_PARA : p->type;
    int still = type == CAM_WONDER_PLANET || type == CAM_EYEPOS_FIX || type == CAM_POINT_FIX ||
                type == CAM_EYEPOS_FIX_THERE || type == CAM_CHARMED_FIX || type == CAM_CUBE_PLANET;
    if (!still) w = vertical_pan(p, t, w);
    *watch = w;
    *up = Y;
    switch (type) {
    case CAM_TOWER: { /* CameraTower.cpp:63-237 (no C-stick rounding) */
        V3 axis = vnorm(vec(p->axis));
        if (vlen(axis) < 0.5f) axis = Y;
        V3 f = vtangent(vsub(w, vec(p->wpoint)), axis);
        if (vlen(f) < 1e-3f) { *eye = type_eye; break; }
        f = vnorm(f);
        f = vadd(vmul(f, cosf(p->angle_b)), vmul(Y, sinf(p->angle_b)));
        V3 ideal = vadd(w, vmul(f, p->dist));
        V3 ri = vsub(ideal, w), rp = vsub(type_eye, w);
        ri.y = rp.y = 0.0f;
        float lag = PI * (1.0f - p->angle_a) * 0.5f;     /* the camera keeps its yaw until it lags more than this */
        float yi = atan2f(ri.x, ri.z), yp = atan2f(rp.x, rp.z);
        float yaw = yi + clampf(wrap_pi(yp - yi), -lag, lag);
        float hl = 0.95f * vlen(rp) + 0.05f * vlen(ri);  /* sAttenuation 0.05 */
        *eye = v3(w.x + sinf(yaw) * hl, ideal.y, w.z + cosf(yaw) * hl);
        break;
    }
    case CAM_FOLLOW: { /* CameraFollow.cpp:102-225 (no C-stick rounding, no wall / height arrange) */
        float dmax = p->axis[0], dmin = p->axis[1];
        V3 r = vsub(type_eye, w);
        float d = vlen(r), pitch = atan2f(r.y, sqrtf(r.x * r.x + r.z * r.z)), yaw = atan2f(r.x, r.z);
        if (d < dmin) d = dmin;
        else if (d >= dmax) d = dmax + (d - dmax) * 0.5f;
        float top = p->angle_a + p->angle_b;
        if (pitch > top) pitch += (top - pitch) * 0.5f;
        if (pitch < p->angle_a) pitch += (p->angle_a - pitch) * 0.5f;
        V3 back = vmul(t->front, -1.0f);
        float behind = atan2f(back.x, back.z);
        V3 hm = v3(move.x, 0, move.z);
        float speed = t->jumping ? 0.0f : p->dist * (PI / 180.0f) * clampf(vlen(hm) / 15.0f, 0.0f, 1.0f);
        yaw += clampf(wrap_pi(behind - yaw), -speed, speed);
        *eye = vadd(w, polar(d, pitch, yaw));
        break;
    }
    case CAM_WONDER_PLANET: { /* CameraWonderPlanet.cpp:52-109: dragged along in the target frame */
        float dmin = p->axis[0], dmax = p->axis[1];
        V3 z = vnorm(vtangent(t->front, t->up)), x = vcross(t->up, z);
        V3 r = vsub(type_eye, w);
        V3 l = v3(vdot(r, x), vdot(r, t->up), vdot(r, z));
        float len = vlen(l), pitch = atan2f(l.y, sqrtf(l.x * l.x + l.z * l.z)), yaw = atan2f(l.x, l.z);
        if (pitch < p->angle_a) pitch += (p->angle_a - pitch) * 0.05f;
        len = clampf(len, dmin, dmax);
        l = polar(len, pitch, yaw);
        *eye = vadd(w, vadd(vadd(vmul(x, l.x), vmul(t->up, l.y)), vmul(z, l.z)));
        V3 pf = vnorm(vsub(w, type_eye)), nf = vnorm(vsub(w, *eye));
        *up = vnorm(rotate_between(pf, nf, type_up));
        if (vlen(*up) < 0.5f) *up = t->up;
        break;
    }
    case CAM_EYEPOS_FIX: /* CameraFixedPoint.cpp:20-60 */
        *eye = vec(p->wpoint);
        if (p->num1 == 2) *up = t->up;
        break;
    case CAM_POINT_FIX: { /* CameraFix.cpp + CamTranslatorFix: all fixed; eye = wpoint + polar(dist, axis.x, axis.y) */
        *watch = vadd(vec(p->wpoint), vec(p->woffset));
        *eye = vadd(vec(p->wpoint), polar(p->dist, p->axis[0], p->axis[1]));
        V3 u = vnorm(vec(p->up)), f = vnorm(vsub(*watch, *eye));
        if (vlen(u) < 0.5f) u = Y;
        if (fabsf(vdot(f, u)) > 0.98f) u = v3(u.y, u.z, u.x);
        *up = u;
        break;
    }
    case CAM_EYEPOS_FIX_THERE: { /* CameraFixedThere.cpp: the eye stays where the last camera left it */
        *eye = type_eye;
        V3 f = vnorm(vsub(w, type_eye));
        /* num1 0 (CameraType_GravityUp): up against gravity unless looking within 30 degrees of it */
        *up = p->num1 == 0 && fabsf(vdot(f, t->up)) <= 0.866f ? t->up : type_up;
        break;
    }
    case CAM_CHARMED_FIX: { /* CameraCharmedFix.cpp: base point = axis, offset wpoint in the frame looking at Mario */
        V3 f = vsub(t->pos, vec(p->axis));
        V3 u = vnorm(vec(p->up));
        if (vlen(u) < 0.5f) u = Y;
        if (vlen(f) < 1e-3f) { *eye = type_eye; break; }
        f = vnorm(f);
        V3 side = vcross(u, f);
        if (vlen(side) < 1e-3f) { *eye = type_eye; break; }
        side = vnorm(side);
        u = vcross(f, side);
        *eye = vadd(t->pos, vadd(vadd(vmul(side, p->wpoint[0]), vmul(u, p->wpoint[1])), vmul(f, p->wpoint[2])));
        *watch = vsub(*eye, vmul(f, p->wpoint[2]));
        *up = u;
        break;
    }
    case CAM_TOWER_POS: { /* CameraTowerPos.cpp: dist = up.x, pitch angleB, yaw speed pi (1 - angleA) / 2 */
        V3 axis = vnorm(vec(p->axis));
        if (vlen(axis) < 0.5f) axis = Y;
        V3 f = vtangent(vsub(w, vec(p->wpoint)), axis);
        if (vlen(f) < 1e-3f) { *eye = type_eye; break; }
        f = vnorm(f);
        f = rot_axis(f, vnorm(vcross(f, Y)), p->angle_b);
        V3 ideal = vadd(w, vmul(f, p->up[0]));
        if (fresh) { *eye = ideal; break; }
        /* arrangeHorizontalPose: yaw towards the ideal at most the speed per frame, pitch blended at 0.05 */
        V3 ri = vsub(ideal, w), rp = vsub(type_eye, w);
        float yi = atan2f(ri.x, ri.z), yp = atan2f(rp.x, rp.z), sp = PI * (1.0f - p->angle_a) * 0.5f;
        float yaw = yp + clampf(wrap_pi(yi - yp), -sp, sp);
        float pi_ = atan2f(ri.y, sqrtf(ri.x * ri.x + ri.z * ri.z)), pp = atan2f(rp.y, sqrtf(rp.x * rp.x + rp.z * rp.z));
        *eye = vadd(w, polar(p->up[0], pp + 0.05f * wrap_pi(pi_ - pp), yaw));
        break;
    }
    case CAM_FRONT_AND_BACK: { /* CameraFrontAndBack.cpp: zone Z turned so X lies on the axis, spun by angleA
                                * degrees about it, dipped towards Mario by at most angleB degrees */
        V3 axis = vnorm(vec(p->axis));
        if (vlen(axis) < 0.5f) axis = v3(1, 0, 0);
        V3 dir = vtangent(vsub(t->pos, vec(p->wpoint)), axis);
        if (vlen(dir) < 1e-3f) { *eye = type_eye; *up = type_up; break; }
        V3 f = rotate_between(v3(1, 0, 0), axis, v3(0, 0, 1)), u = rotate_between(v3(1, 0, 0), axis, Y);
        float spin = PI * p->angle_a / 180.0f, dip = PI * p->angle_b / 180.0f;
        f = rot_axis(f, axis, spin);
        u = rot_axis(u, axis, spin);
        V3 d = vnorm(dir);
        float ang = acosf(clampf(vdot(d, f), -1.0f, 1.0f)), rate = ang > dip ? dip / ang : 1.0f;
        V3 nf = rotate_part(f, d, f, rate);
        u = rotate_part(f, d, u, rate);
        *eye = vadd(t->pos, vmul(nf, p->dist));
        *up = u;
        break;
    }
    case CAM_TRUNDLE: { /* CameraTrundle.cpp: rolls around the axis through wpoint; dist, angleA / angleB, up.x */
        V3 side = vnorm(vec(p->axis));
        if (vlen(side) < 0.5f) { *eye = type_eye; break; }
        V3 rel = vsub(t->pos, vec(p->wpoint));
        V3 u = vsub(rel, vmul(side, vdot(side, rel)));
        V3 z = vcross(u, side);
        if (vlen(z) < 1e-3f) { *eye = type_eye; *up = type_up; break; }
        z = rot_axis(vnorm(z), side, -p->angle_a);
        u = vcross(side, z);
        if (fabsf(p->up[0]) > 1e-6f) {
            float rate = clampf(vdot(rel, side) / p->up[0], -1.0f, 1.0f);
            z = rot_axis(z, vnorm(u), p->angle_b * rate);
        }
        *eye = vsub(w, vmul(z, p->dist));
        *up = vnorm(u);
        break;
    }
    case CAM_CUBE_PLANET: { /* CameraCubePlanet.cpp: up eased to the gravity's (angleB per frame), angleA pitch */
        if (fresh) cube_up = t->up;
        cube_up = vnorm(rotate_part(cube_up, t->up, cube_up, p->angle_b));
        V3 u = cube_up, f = vtangent(vsub(w, type_eye), u);
        if (vlen(f) < 1e-3f) { *eye = type_eye; *up = u; break; }
        f = vnorm(f);
        V3 side = vnorm(vcross(u, f));
        u = rot_axis(u, side, p->angle_a);
        f = rot_axis(f, side, p->angle_a);
        *eye = vsub(w, vmul(f, p->dist));
        *up = u;
        break;
    }
    default: /* CAM_XZ_PARA, CameraParallel.cpp:61-87, CamTranslatorParallel.cpp:12 */
        *eye = vadd(w, polar(p->dist < 300.0f ? 300.0f : p->dist, p->angle_b, p->angle_a - PI * 0.5f));
        break;
    }
    if (vlen(vsub(*eye, *watch)) < 300.0f) /* OnlyCamera.cpp:80-95: keep the watch point 300 away */
        *eye = vadd(*watch, vmul(vnorm(vsub(*eye, *watch)), 300.0f));
}

/* A launch star's flight: SpinDriverCamera follows its own event camera (the star's camera set in the bcam, not
 * ported); ponytail: a chase camera 1300 behind Mario along the path and 400 above him, eased 0.1 a frame; once he
 * lands, the area camera takes over through camint (30 frames). */
static int was_flying;
static V3 fly_eye;

void camera_update(const CamTarget *t)
{
    int a = -1;
    for (int i = nareas - 1; i >= 0; i--)
        if (inside(&areas[i], t->pos)) { a = i; break; }
    /* no area: keep the current camera (the ground-triangle cameras are not ported), the default one at first */
    int want = a >= 0 && areas[a].cam != 0xFFFF ? areas[a].cam : (cur == -2 ? -1 : cur);
    V3 move = vsub(t->pos, last_target);
    last_target = t->pos;
    if (want != cur) { /* CameraManGame.cpp:250-285: reset the new camera from the current pose, blend camint */
        int first = cur == -2;
        cur = want;
        timer = 0;
        camint = first ? 0 : param()->camint;
        fresh = 1;
#ifdef BENCH
        printf("camera %d %s\n", cur, camera_type_name());
#endif
        if (first) {
            V3 e, w, u;
            type_eye = vadd(t->pos, vadd(vmul(t->up, 600.0f), vmul(t->front, -1200.0f)));
            type_up = t->up;
            zone_pose(param(), t, move, &e, &w, &u);
            i_eye = f_eye = e; i_watch = f_watch = w; i_up = f_up = u;
            type_eye = e; type_up = u;
        } else {
            type_eye = f_eye;
            type_up = f_up;
        }
    }
    const CamParam *p = param();
    V3 e, w, u;
    zone_pose(p, t, move, &e, &w, &u);
    fresh = 0;
    type_eye = e;
    type_up = u;
    /* CameraViewInterpolator.cpp:275-327, on positions: r = (timer / camint)^2 */
    float r = 1.0f;
    if (camint > 0 && timer < camint) { timer++; r = (float)timer / camint; r *= r; }
    i_eye = vadd(i_eye, vmul(vsub(e, i_eye), r));
    i_watch = vadd(i_watch, vmul(vsub(w, i_watch), r));
    i_up = vnorm(vadd(i_up, vmul(vsub(u, i_up), r)));
    float fovy = p->flags & 2 ? p->fovy : 45.0f;
    if (p->flags & 8) { f_eye = i_eye; f_watch = i_watch; f_up = i_up; f_fovy = fovy; return; } /* antibluroff */
    /* CameraViewInterpolator.cpp:214-241: final 0.3 smoothing (0.1 for fovy) */
    f_eye = vadd(vmul(f_eye, 0.7f), vmul(i_eye, 0.3f));
    f_watch = vadd(vmul(f_watch, 0.7f), vmul(i_watch, 0.3f));
    f_up = vnorm(vadd(vmul(f_up, 0.7f), vmul(i_up, 0.3f)));
    f_fovy = 0.9f * f_fovy + 0.1f * fovy;
    if (t->flying) {
        V3 d = vlen(t->fly_dir) > 1e-3f ? vnorm(t->fly_dir) : t->front;
        V3 want = vadd(vsub(t->pos, vmul(d, 1300.0f)), vmul(t->up, 400.0f));
        if (!was_flying) fly_eye = f_eye;
        fly_eye = vadd(fly_eye, vmul(vsub(want, fly_eye), 0.1f));
        f_eye = fly_eye; f_watch = t->pos; f_up = t->up;
        i_eye = f_eye; i_watch = f_watch; i_up = f_up;
        was_flying = 1;
    } else if (was_flying) { /* landed: blend from the flight pose into the area camera */
        was_flying = 0;
        timer = 0;
        camint = 30;
    }
}

void camera_pose(V3 *eye, V3 *watch, V3 *up, float *fovy)
{
    *eye = f_eye; *watch = f_watch; *up = f_up; *fovy = f_fovy;
}

int camera_current(void) { return cur < ncams ? cur : -1; }

const char *camera_type_name(void)
{
    static const char *n[] = {"XZ PARA", "TOWER", "FOLLOW", "WONDER PLANET", "EYEPOS FIX", "POINT FIX",
                              "EYEPOS FIX THERE", "CHARMED FIX", "TOWER POS", "FRONT AND BACK", "TRUNDLE", "CUBE PLANET"};
    const CamParam *p = param();
    return p->type == 255 ? "OTHER" : n[p->type];
}
