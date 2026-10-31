#include "camera.h"

#include <stdio.h>
#include <string.h>

/* Petari AreaForm.cpp: cube 45-54 + 104-125 (centre origin, 1000 x scale), cylinder 241-313 (base origin, radius
 * 500 x scale_x, height 500 x scale_y along R * Y), sphere 160-166 + 277-285 (radius 500 x scale_x). Rotation order
 * as every placement: R = Rz * Ry * Rx (degrees). */
static int inside(const CamArea *a, V3 p)
{
    V3 d = v3(p.x - a->pos[0], p.y - a->pos[1], p.z - a->pos[2]);
    if (a->shape == 2)
        return vlen(d) < 500.0f * a->scale[0];
    /* its axes kept by the rotation (its bits): every area is asked every frame, six sines / cosines each */
    static struct { unsigned int k[3]; int ok; V3 x, y, z; } c[64];
    unsigned int key[3];
    memcpy(key, a->rot, 12);
    typeof(&c[0]) e = &c[(key[0] * 2654435761u ^ key[1] * 2246822519u ^ key[2] * 3266489917u) >> 26];
    if (!e->ok || e->k[0] != key[0] || e->k[1] != key[1] || e->k[2] != key[2]) {
        const float k = 3.14159265f / 180.0f;
        float cx = cosf(a->rot[0] * k), sx = sinf(a->rot[0] * k), cy = cosf(a->rot[1] * k), sy = sinf(a->rot[1] * k);
        float cz = cosf(a->rot[2] * k), sz = sinf(a->rot[2] * k);
        e->x = v3(cz * cy, sz * cy, -sy);                                    /* columns of Rz * Ry * Rx */
        e->y = v3(cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx);
        e->z = v3(cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx);
        memcpy(e->k, key, 12); e->ok = 1;
    }
    V3 x = e->x, y = e->y, z = e->z;
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
static V3 type_eye, type_up;          /* the type camera's own previous output (Tower, WonderPlanet read it) */
static V3 type_watch;                 /* the camera man's watch point at a camera change (CameraFollow::reset) */
static const CamTarget *world_t;      /* the target in the world (map checks) */
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
    world_t = t;
    if (!zones || p->zone == 0) {
        type_pose(p, t, move, eye, watch, up);
        return;
    }
    const float *m = zones[p->zone];
    CamTarget l = *t;
    l.pos = to_local(m, t->pos, 1); l.front = to_local(m, t->front, 0); l.up = to_local(m, t->up, 0);
    l.fly_dir = to_local(m, t->fly_dir, 0);
    V3 se = type_eye, su = type_up, sw = type_watch;
    type_eye = to_local(m, se, 1);
    type_up = to_local(m, su, 0);
    type_watch = to_local(m, sw, 1);
    type_pose(p, &l, to_local(m, move, 0), eye, watch, up);
    type_eye = se;
    type_up = su;
    type_watch = sw;
    *eye = to_world(m, *eye, 1);
    *watch = to_world(m, *watch, 1);
    *up = to_world(m, *up, 0);
}

void camera_init(const CamParam *c, int nc, const CamArea *a, int na, const float (*z)[16], const CamTarget *t)
{
    cams = c; ncams = nc; areas = a; nareas = na; zones = z;
    null_cam.type = CAM_XZ_PARA; null_cam.dist = 1200.0f; null_cam.angle_b = 0.3f; null_cam.fovy = 45.0f;
    null_cam.camint = 0; null_cam.vpanuse = 1; null_cam.vpanaxis[1] = 1.0f; /* setNullCamera: resetParameter */
    null_cam.upper = 0.3f; null_cam.lower = 0.1f; null_cam.uplay = 500.0f; null_cam.lplay = 800.0f;
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

/* CameraHeightArrange (CameraHeightArrange.cpp), the mVPan of the types that createVPanObj (Parallel, Tower, Follow).
 * Run in the zone frame, its global axis there vpanaxis (getGlobalAxis: the zone x vpanaxis in the world); the up
 * vectors pass through (arrangeHeight's blendRate 1). Params as CameraManGame::applyParameter (upper, lower, uplay,
 * lplay, vpanuse, vpanaxis). ponytail: gndint / pushdelay / pushdelaylow / udown are not in CAMS: their chunk defaults
 * (ExParam::init 160 / 120 / 120 / 120); isFastRise / isFastDrop (MarioActor) are not decompiled: false; one state for
 * every camera (each Petari camera keeps its own mRiseEaseTime over a reset) */
enum { JUMP_NOMOVE, JUMP_RISE, JUMP_FALL };
#define VP_GNDINT 160
#define VP_RISE_DELAY 120
#define VP_DROP_DELAY 120
#define VP_MAX_RISE_EASE 120
static struct {
    int jumping, start_jump, end_jump;
    V3 next_watch, next_pos, cur_watch, cur_pos; /* mNextParam, mCurrParam */
    float base_h, target_h, watch_h, pos_h, watch_off, pos_off;
    int state, state_time, catchup, chase_time, rise_time, drop_time, rise_ease_time;
} vp;

static int near_zero(V3 v) /* MR::isNearZero (0.001) */
{
    return fabsf(v.x) <= 0.001f && fabsf(v.y) <= 0.001f && fabsf(v.z) <= 0.001f;
}
static V3 at_height(V3 v, V3 g, float h) { return vadd(vsub(v, vmul(g, vdot(g, v))), vmul(g, h)); }

static void vp_reset_jump(void) { vp.jumping = vp.start_jump = vp.end_jump = 0; } /* resetJump */

static void vp_update_jump(const CamTarget *t) /* updateJump */
{
    vp.start_jump = !vp.jumping && t->jumping;
    vp.end_jump = vp.jumping && !t->jumping;
    vp.jumping = t->jumping;
}

static V3 vp_watch_pos(const CamParam *p, V3 w) /* calcWatchPos: the watch height along the axis kept while jumping */
{
    V3 g = vec(p->vpanaxis);
    if (!(p->vpanuse && vp.jumping)) vp.base_h = vdot(g, w);
    return at_height(w, g, vp.base_h);
}

static void vp_reset(const CamParam *p, const CamTarget *t, V3 move, V3 eye, V3 watch) /* reset */
{
    V3 g = vec(p->vpanaxis);
    vp.base_h = vdot(g, watch);
    vp.target_h = vdot(g, t->pos);
    vp.pos_h = vdot(g, eye);
    vp.watch_h = vp.base_h;
    vp.watch_off = vp.pos_off = 0.0f;
    vp.state = JUMP_NOMOVE;
    vp.state_time = vp.catchup = vp.chase_time = vp.rise_time = vp.drop_time = 0;
    vp.next_watch = vp.cur_watch = watch;
    vp.next_pos = vp.cur_pos = eye;
    if (t->jumping) vp.state = vdot(g, move) >= 0.0f ? JUMP_RISE : JUMP_FALL;
}

static void vp_calc_pose(V3 g) /* calcPose */
{
    vp.cur_watch = at_height(vp.next_watch, g, vp.watch_h + vp.watch_off);
    vp.cur_pos = at_height(vp.next_pos, g, vp.pos_h + vp.pos_off);
}

static void vp_height_and_offset(V3 g) /* updateHeightAndOffset: sLocalOffsetRateScale 0.05 (vpanuse) */
{
    vp.watch_off += -vp.watch_off * 0.05f;
    vp.pos_off += -vp.pos_off * 0.05f;
    vp.watch_h += 0.05f * (vdot(g, vp.next_watch) - vp.watch_h);
    vp.pos_h += 0.05f * (vdot(g, vp.next_pos) - vp.pos_h);
}

/* calcOffset: how far along the axis the target is past the screen line at focal scale s (+ above, - below) */
static float vp_offset(const CamParam *p, const CamTarget *t, V3 g, float s)
{
    float fovy = p->flags & 2 ? p->fovy : 45.0f;
    float ang = atan2f(tanf(fovy * PI * 0.5f / 180.0f) * s, 1.0f);
    V3 front = vsub(vp.cur_watch, vp.cur_pos);
    if (near_zero(front)) return 0.0f;
    front = vnorm(front);
    V3 up = vnorm(g), side = vcross(up, front);
    if (near_zero(side)) return 0.0f;
    side = vnorm(side);
    up = vcross(front, side);
    float c = vdot(up, g);
    if (fabsf(c) < 0.1f) return 0.0f;
    V3 to = vsub(t->pos, vp.cur_pos);
    return (vdot(up, to) - vdot(front, to) * tanf(ang)) / c;
}

static void vp_upper(const CamParam *p, const CamTarget *t, V3 g) /* updateUpper */
{
    float k = (float)vp.rise_ease_time / VP_MAX_RISE_EASE;
    if (k > 1.0f) k = 1.0f;
    k = clampf((sinf(k * PI + PI / 2) + 1.0f) * 0.5f, 0.0f, 1.0f);
    float off = vp_offset(p, t, g, p->upper * k);
    if (off > 0.0f) {
        float rate = VP_RISE_DELAY > 0 ? (float)vp.rise_time / VP_RISE_DELAY : 1.0f;
        float wo = vp.watch_off + off * rate, lag = wo - vp.pos_off;
        vp.watch_off = wo;
        if (lag > p->uplay) vp.pos_off += lag - p->uplay;
        vp_calc_pose(g);
        vp.catchup = 1;
        if (++vp.rise_time > VP_RISE_DELAY) vp.rise_time = VP_RISE_DELAY;
    } else if (--vp.rise_time < 0) {
        vp.rise_time = 0;
        vp.rise_ease_time = 0;
    }
    if (vp.rise_time > 0) vp.rise_ease_time++;
    if (vp.rise_ease_time > VP_MAX_RISE_EASE) vp.rise_ease_time = VP_MAX_RISE_EASE;
}

static void vp_lower(const CamParam *p, const CamTarget *t, V3 g) /* updateLower */
{
    float off = vp_offset(p, t, g, -p->lower);
    if (off < 0.0f) {
        if (vp.catchup || vp.target_h > vdot(g, t->pos)) {
            float rate = VP_DROP_DELAY > 0 ? (float)vp.drop_time / VP_DROP_DELAY : 1.0f;
            float wo = vp.watch_off + off * rate, lag = vp.pos_off - wo;
            vp.watch_off = wo;
            if (lag > p->lplay) vp.pos_off -= lag - p->lplay;
            vp_calc_pose(g);
            if (++vp.drop_time > VP_DROP_DELAY) vp.drop_time = VP_DROP_DELAY;
        }
    } else if (--vp.drop_time < 0) {
        vp.drop_time = 0;
    }
}

/* arrange: checkState, arrangeHeight (the type's pose in mNextParam), arrangeVPan; the pose out of mCurrParam */
static void vp_arrange(const CamParam *p, const CamTarget *t, V3 move, V3 *eye, V3 *watch)
{
    if (!p->vpanuse) return;
    V3 g = vec(p->vpanaxis);
    float lm = vdot(g, move); /* getLastMove */
    switch (vp.state) {
    case JUMP_NOMOVE:
        if (vp.start_jump) { vp.state = JUMP_RISE; vp.state_time = 0; }
        break;
    case JUMP_RISE:
        if (!vp.jumping) { vp.state = JUMP_NOMOVE; vp.state_time = 0; }
        else if (lm < 0.0f) { vp.state = JUMP_FALL; vp.state_time = 0; }
        break;
    case JUMP_FALL:
        if (!vp.jumping) { vp.state = JUMP_NOMOVE; vp.state_time = 0; }
        else if (lm > 0.0f) { vp.state = JUMP_RISE; vp.state_time = 0; }
        break;
    }
    vp.next_watch = *watch;
    vp.next_pos = *eye;
    if (vp.state == JUMP_NOMOVE) {
        if (vp.state_time == 0) vp.chase_time = 0;
        vp.catchup = 0;
        if ((float)vp.chase_time / VP_GNDINT > 0.5f) vp.rise_ease_time = 0; /* chase */
        vp_height_and_offset(g);
        vp.target_h = vdot(g, t->pos);
    } else {
        vp_height_and_offset(g);
    }
    vp_calc_pose(g);
    vp_upper(p, t, g);
    vp_lower(p, t, g);
    vp.state_time++;
    vp.chase_time++;
    *watch = vp.cur_watch;
    *eye = vp.cur_pos;
}

/* CameraFollow's own state: mPos / mWatchPos (zone frame, before the arrange), mAngleXSpeed */
static V3 fl_pos, fl_watch;
static float fl_pitch_speed;
/* The pad's rounds and reset (CameraLocalUtil testCameraPadTriggerRoundLeft / Right / Reset: the Wii D-pad and C;
 * camera_input sets them for the next camera_update). CameraFollow: startRound towards a yaw at sRoundAddition 0.08
 * (0.2 for a reset, behind Mario), sRoundInterval pi / 4, each side valid while the line to its eye is clear
 * (checkValidityOfRound). CameraTower: whole intervals off its ideal yaw, as many as its yaw speed allows.
 * CameraParallel (num1 1): 8 intervals round, 0.08 a frame (twice that back to 0 on a reset). */
#define ROUND_INTERVAL (PI / 4.0f)
static int in_left, in_right, in_reset;
static int sj_on, sj_frame; /* the subjective camera (camera_fp) */
static int fl_rounding, fl_valid_l, fl_valid_r;
static float fl_yaw_target, fl_yaw_speed;
static int tw_n, tw_rounding, tw_max;
static int pa_rounding;
static float pa_angle, pa_target, pa_add = 0.08f;
#define LOOK_DOWN (75.0f * PI / 180.0f) /* sLookDownAngle */

static int map_line(V3 from, V3 dir) /* Collision::checkStrikeLineToMap (world); none given: a hit */
{
    return !world_t->map_line || world_t->map_line(from, vadd(from, dir));
}

/* CameraFollow::arrangeHeight (CameraFollow.cpp:248-325): reflectRotOffset, then mAngleXSpeed eased towards looking
 * down 75 degrees while no wall is ahead (isWallForward) and no ground 500 ahead (checkDescent); else it fades 0.99.
 * ponytail: polar angles in the zone frame (Petari: the world's; the same in zone 0) */
static void follow_height(const CamParam *p, const CamTarget *t, V3 w, V3 *eye)
{
    V3 d = vsub(*eye, w);
    float len = vlen(d), ax = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) + fl_pitch_speed, ay = atan2f(d.x, d.z);
    if (ax > LOOK_DOWN) ax = LOOK_DOWN;
    *eye = vadd(w, polar(len, ax, ay));
    const CamTarget *wt = world_t;
    if (p->num1 <= 0 || vdot(vsub(w, *eye), t->front) < 0.0f ||
        map_line(vadd(wt->pos, vmul(wt->up, 100.0f)), vmul(wt->front, 500.0f)) || /* sColCheckHeight, sSlopeFrontDist */
        map_line(vadd(wt->pos, vadd(vmul(wt->front, 500.0f), vmul(wt->up, 100.0f))), vmul(wt->up, -500.0f)) ||
        ax > LOOK_DOWN)
        fl_pitch_speed *= 0.99f;
    else
        fl_pitch_speed += (LOOK_DOWN - ax - fl_pitch_speed) * 0.02f;
}

static void type_pose(const CamParam *p, const CamTarget *t, V3 move, V3 *eye, V3 *watch, V3 *up)
{
    V3 Y = v3(0, 1, 0);
    V3 w = watch_point(p, t, move);
    int type = p->type == 255 ? CAM_XZ_PARA : p->type;
    int vpan = type == CAM_XZ_PARA || type == CAM_TOWER || type == CAM_FOLLOW; /* the types with createVPanObj */
    if (fresh) { fl_rounding = fl_valid_l = fl_valid_r = 0; tw_n = tw_rounding = 0; pa_rounding = 0; pa_angle = pa_target = 0.0f; }
    if (vpan && fresh) { /* reset (CameraParallel / CameraTower: resetJump, calcWatchPos; CameraFollow.cpp:45-64) */
        vp_reset_jump();
        if (type == CAM_FOLLOW) {
            fl_pitch_speed = 0.0f;
            fl_pos = type_eye;
            fl_watch = type_watch;
            vp_reset(p, t, move, type_eye, type_watch);
        } else {
            vp_watch_pos(p, w);
        }
    }
    if (vpan) { /* calc: updateJump, calcWatchPos */
        vp_update_jump(t);
        w = vp_watch_pos(p, w);
    }
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
        tw_max = (int)((lag + ROUND_INTERVAL / 2) / ROUND_INTERVAL); /* arrangeHorizontalPose's maxDivNum */
        if (in_reset) { tw_n = 0; tw_rounding = 1; }
        else if (in_left && tw_n > -tw_max) { tw_n--; tw_rounding = 1; }
        else if (in_right && tw_n < tw_max) { tw_n++; tw_rounding = 1; }
        float yi = atan2f(ri.x, ri.z) + tw_n * ROUND_INTERVAL, yp = atan2f(rp.x, rp.z);
        float yaw;
        if (tw_rounding) { /* arrangeRoundingPose: towards the interval at sRoundAddition a frame */
            float dy = wrap_pi(yi - yp);
            yaw = yp + clampf(dy, -0.08f, 0.08f);
            if (fabsf(dy) <= 0.08f) tw_rounding = 0;
        } else {
            yaw = yi + clampf(wrap_pi(yp - yi), -lag, lag);
        }
        float hl = 0.95f * vlen(rp) + 0.05f * vlen(ri);  /* sAttenuation 0.05 */
        *eye = v3(w.x + sinf(yaw) * hl, ideal.y, w.z + cosf(yaw) * hl);
        break;
    }
    case CAM_FOLLOW: { /* CameraFollow.cpp:66-130, 179-227 from mPos (no C-stick rounding: calcRotPos), arrangeHeight */
        float dmax = p->axis[0], dmin = p->axis[1];
        V3 r = vsub(fl_pos, w); /* constrainDistance */
        float d = vlen(r);
        if (fl_rounding) { /* calcRotPos: the yaw converges on the round's target, the rest kept */
            float rpitch = atan2f(r.y, sqrtf(r.x * r.x + r.z * r.z)), ryaw = atan2f(r.x, r.z);
            float dy = wrap_pi(fl_yaw_target - ryaw);
            ryaw += clampf(dy, -fl_yaw_speed, fl_yaw_speed);
            if (fabsf(dy) <= fl_yaw_speed) fl_rounding = 0;
            fl_pos = *eye = vadd(w, polar(d, rpitch, ryaw));
            fl_watch = w;
            follow_height(p, t, w, eye);
            goto follow_valid;
        }
        if (d < dmin) r = vmul(vnorm(near_zero(r) ? vsub(fl_pos, fl_watch) : r), dmin);
        else if (d >= dmax) r = vmul(vnorm(r), dmax + (d - dmax) * 0.5f);
        d = vlen(r);
        float pitch = atan2f(r.y, sqrtf(r.x * r.x + r.z * r.z)), yaw = atan2f(r.x, r.z);
        float top = p->angle_a + p->angle_b;
        if (pitch > top) pitch += (top - pitch) * 0.5f;
        if (pitch < p->angle_a) pitch += (p->angle_a - pitch) * 0.5f;
        V3 back = vmul(t->front, -1.0f);
        float behind = atan2f(back.x, back.z);
        V3 hm = vsub(move, vmul(t->up, vdot(move, t->up))); /* CameraFollow.cpp:221: the move off the target's up */
        float speed = t->jumping ? 0.0f : p->dist * (PI / 180.0f) * clampf(vlen(hm) / 15.0f, 0.0f, 1.0f);
        if (in_reset) { fl_yaw_target = behind; fl_yaw_speed = 0.2f; fl_rounding = 1; } /* constrainAngleY: startRound */
        else yaw += clampf(wrap_pi(behind - yaw), -speed, speed);
        fl_pos = *eye = vadd(w, polar(d, pitch, yaw));
        if ((in_left && fl_valid_l) || (in_right && fl_valid_r)) { /* checkShiftStateToRound */
            fl_yaw_target = yaw + (in_left ? -ROUND_INTERVAL : ROUND_INTERVAL);
            fl_yaw_speed = 0.08f;
            fl_rounding = 1;
        }
        fl_watch = w;
        follow_height(p, t, w, eye);
    follow_valid: { /* checkValidityOfRound: the eye an interval round either way, its line clear of the map */
            V3 rr = vsub(*eye, w);
            float rd = vlen(rr), rpt = atan2f(rr.y, sqrtf(rr.x * rr.x + rr.z * rr.z)), ry = atan2f(rr.x, rr.z);
            fl_valid_l = !map_line(*eye, vsub(vadd(w, polar(rd, rpt, ry - ROUND_INTERVAL)), *eye));
            fl_valid_r = !map_line(*eye, vsub(vadd(w, polar(rd, rpt, ry + ROUND_INTERVAL)), *eye));
        }
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
    case CAM_OBJ_PARALLEL: { /* CameraObjParallel.cpp: mtx = target (side, up, front) x RotY(angleB) x RotX(angleA);
                              * the eye dist along its Z from the watch point, its Y the up (dist, angleA, angleB) */
        V3 y = t->up, z = t->front, x = vcross(y, z);
        float sa = sinf(p->angle_a), ca = cosf(p->angle_a), sb = sinf(p->angle_b), cb = cosf(p->angle_b);
        V3 zc = vadd(vadd(vmul(x, ca * sb), vmul(y, -sa)), vmul(z, ca * cb));
        *eye = vadd(w, vmul(zc, p->dist));
        *up = vnorm(vadd(vadd(vmul(x, sa * sb), vmul(y, ca)), vmul(z, sa * cb)));
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
        if (p->num1 == 1) { /* calcRound (mIsEnableValid) */
            if (pa_rounding) {
                pa_angle += clampf(pa_target - pa_angle, -pa_add, pa_add);
                if (pa_angle == pa_target) pa_rounding = 0;
            } else if (in_reset) {
                pa_target = 0.0f; pa_rounding = 1; pa_add = 0.16f;
            } else if (in_left || in_right) {
                int n = (pa_angle < 0.0f ? -1 : 1) * (int)((ROUND_INTERVAL / 2 + fabsf(pa_angle)) / ROUND_INTERVAL) + (in_left ? -1 : 1);
                if (n < -4) { n += 8; pa_angle += 2 * PI; }
                if (n > 4) { n -= 8; pa_angle -= 2 * PI; }
                pa_target = n * ROUND_INTERVAL; pa_rounding = 1; pa_add = 0.08f;
            }
        }
        *eye = vadd(w, polar(p->dist < 300.0f ? 300.0f : p->dist, p->angle_b, p->angle_a - PI * 0.5f + pa_angle));
        break;
    }
    if (vpan) { /* mVPan->arrange (CameraParallel / CameraTower reset it from their first pose) */
        if (fresh && type != CAM_FOLLOW) vp_reset(p, t, move, *eye, *watch);
        vp_arrange(p, t, move, eye, watch);
    }
    if (vlen(vsub(*eye, *watch)) < 300.0f) /* OnlyCamera.cpp:80-95: keep the watch point 300 away */
        *eye = vadd(*watch, vmul(vnorm(vsub(*eye, *watch)), 300.0f));
}

/* A launch star's flight: SpinDriverCamera follows its own event camera (the star's camera set in the bcam, not
 * ported); ponytail: a chase camera 1300 behind Mario along the path and 400 above him, eased 0.1 a frame; once he
 * lands, the area camera takes over through camint (30 frames). */
static int ev_on, ev_ending, ev_first, ev_n, ev_i, ev_t; /* the event cameras running (cams[ev_first + i]) */
static V3 ev_up, ev_front, ev_pos;                         /* their target matrix (CameraTargetMtx) */

unsigned camera_class_hash(const char *name)
{
    unsigned h = 0;
    while (*name) h = h * 31 + (unsigned char)*name++;
    return h;
}

/* the event cameras of class cls whose num, shifted right by `shift`, is set: one after another from the first */
static int ev_begin(unsigned cls, int set, int shift, V3 up, V3 front, V3 pos)
{
    ev_first = -1; ev_n = 0;
    for (int i = 0; i < ncams; i++)
        if (cams[i].kind == 'e' && cams[i].evclass == cls && (cams[i].num >> shift) == set) {
            if (ev_first < 0) ev_first = i;
            ev_n++;
        }
    if (ev_first < 0) return 0;
    ev_on = 1; ev_ending = 0; ev_i = 0; ev_t = 0;
    ev_up = vnorm(up); ev_front = vnorm(vsub(front, vmul(ev_up, vdot(front, ev_up)))); ev_pos = pos;
    cur = -3; /* the camera change: blend over the event camera's camint */
    timer = 0; camint = cams[ev_first].camint; fresh = 1;
    type_eye = f_eye; type_watch = f_watch; type_up = f_up;
    return 1;
}

void camera_event_target(V3 dir, V3 pos) /* SpinDriverCamera::updateTargetMatrix */
{
    if (!ev_on || vlen(dir) < 1e-6f) return;
    V3 d = vnorm(dir);
    ev_front = rotate_between(ev_up, d, ev_front);
    ev_up = d;
    ev_pos = pos;
}

int camera_event_start(unsigned cls, int set, V3 up, V3 front, V3 pos) { return ev_begin(cls, set, 4, up, front, pos); }

int camera_actor_start(const char *cls, const char *event, int set, V3 up, V3 front, V3 pos)
{
    char name[96];
    snprintf(name, sizeof name, "%s%s%s", cls, set & 0x8000 ? "共通" : "固有", event);
    return ev_begin(camera_class_hash(name), set, 0, up, front, pos);
}

/* MR::getMultiActorCameraFrames: the actor camera's EventFrame (evfrm), 0 if it has none */
int camera_actor_frames(const char *cls, const char *event, int set)
{
    char name[96];
    snprintf(name, sizeof name, "%s%s%s", cls, set & 0x8000 ? "共通" : "固有", event);
    unsigned h = camera_class_hash(name);
    for (int i = 0; i < ncams; i++)
        if (cams[i].kind == 'e' && cams[i].evclass == h && cams[i].num == set) return cams[i].evfrm > 0 ? cams[i].evfrm : 0;
    return 0;
}

void camera_event_end(void) { if (ev_on) ev_ending = 1; }

void camera_event_stop(void) { if (ev_on) { ev_on = 0; cur = -3; } }

static int tk_on;
static V3 tk_pos, tk_up, tk_eye;
static float tk_h, tk_d;

void camera_talk_start(V3 pos, V3 up, float height, float dist)
{
    tk_pos = pos; tk_up = vnorm(up); tk_h = height; tk_d = dist;
    if (tk_on) return;
    tk_on = 1; tk_eye = f_eye; /* reset: from the camera man's position */
    cur = -3; timer = 0; camint = 120; fresh = 1; /* CameraParamChunkExParam's camint */
    type_eye = f_eye; type_watch = f_watch; type_up = f_up;
}

void camera_talk_end(void) { if (tk_on) { tk_on = 0; cur = -3; } }

/* MR::startGlobalEventCamera on a programmable event camera (declareEventCameraProgrammable, setProgrammableCameraParam:
 * its watch point, the eye's direction and distance from it, the up): that fixed pose blended in over `frames`;
 * endGlobalEventCamera(frames): back to the area camera over those frames */
static int pr_on, back_camint = -1;
static V3 pr_eye, pr_watch, pr_up;
void camera_prog_start(V3 watch, V3 eye, V3 up, int frames)
{
    pr_eye = eye; pr_watch = watch; pr_up = vnorm(up);
    if (pr_on) return;
    pr_on = 1;
    cur = -3; timer = 0; camint = frames; fresh = 1;
    type_eye = f_eye; type_watch = f_watch; type_up = f_up;
}
void camera_prog_end(int frames) { if (pr_on) { pr_on = 0; cur = -3; back_camint = frames; } }
/* CameraDirector::endEvent: the event camera popped at once, the view interpolated back to the game camera (reset from
 * the view) over CameraManEvent::sendFinishInterpolateFrame's frames: the chunk's camendint (eflag.enableEndErpFrame),
 * its camint (eflag.enableErpFrame), else frame, else sDefaultBlendFrame 60 */
void camera_event_end_now(int frame)
{
    if (!ev_on) return;
    const CamParam *p = &cams[ev_first + ev_i];
    ev_on = 0; cur = -3;
    back_camint = p->flags >> 7 ? (p->flags >> 7) - 1 : p->flags & 64 ? p->camint : frame >= 0 ? frame : 60;
    printf("camera: event end, back over %d\n", back_camint);
}

static int talk_pose(const CamTarget *t, V3 *eye, V3 *watch, V3 *up) /* CameraTalk::calc */
{
    V3 u = rotate_part(tk_up, t->up, tk_up, 0.5f);
    V3 pos = vadd(vmul(t->pos, 0.5f), vmul(tk_pos, 0.5f)), front = vsub(t->pos, tk_pos);
    front = vlen(front) > 1e-6f ? vnorm(front) : v3(0, 0, 1);
    V3 side = vcross(u, front);
    if (vlen(side) < 1e-6f) return 0;
    side = vnorm(side);
    front = vcross(side, u);
    V3 diff = vsub(tk_eye, t->pos);
    float orient = vdot(vcross(diff, front), u);
    float b = vdot(front, diff) >= 0.0f ? (orient >= 0.0f ? -0.7f : 0.7f) : (orient >= 0.0f ? -PI / 2 : PI / 2);
    /* mtx (side, up, front, pos) x rotY(b) x rotX(-x axis, 0.12) x trans(0, 0, dist) */
    V3 v = v3(tk_d * cosf(0.12f) * sinf(b), tk_d * sinf(0.12f), tk_d * cosf(0.12f) * cosf(b));
    V3 base = vadd(pos, vadd(vadd(vmul(side, v.x), vmul(u, v.y)), vmul(front, v.z)));
    *watch = vadd(pos, vmul(u, tk_h));
    *eye = vadd(base, vmul(u, tk_h));
    *up = u;
    tk_eye = *eye;
    return 1;
}

static void blend_to(V3 e, V3 w, V3 u) /* CameraViewInterpolator: (timer / camint)^2, then the 0.3 smoothing */
{
    float r = 1.0f;
    if (camint > 0 && timer < camint) { timer++; r = (float)timer / camint; r *= r; }
    i_eye = vadd(i_eye, vmul(vsub(e, i_eye), r));
    i_watch = vadd(i_watch, vmul(vsub(w, i_watch), r));
    i_up = vnorm(vadd(i_up, vmul(vsub(u, i_up), r)));
    f_eye = vadd(vmul(f_eye, 0.7f), vmul(i_eye, 0.3f));
    f_watch = vadd(vmul(f_watch, 0.7f), vmul(i_watch, 0.3f));
    f_up = vnorm(vadd(vmul(f_up, 0.7f), vmul(i_up, 0.3f)));
}


static int shake_frame[7] = {25, 25, 25, 25, 25, 25, 25}; /* sSinglyMaxFrame 25: ended */
static float shake_off;

void camera_shake(int power) { if (shake_frame[power] >= 25) shake_frame[power] = 0; }

float camera_shake_y(void) { return shake_off; }

static void shake_update(void) /* CameraShaker::movement: CameraShakeTask::movement, CameraShakePatternSingly::update */
{
    static const float intensity[7] = {0.08f, 0.2f, 0.5f, 1.0f, 3.0f, 6.0f, 9.0f};
    float y = 0.0f;
    for (int i = 0; i < 7; i++) {
        if (shake_frame[i] >= 25) continue;
        int left = 25 - ++shake_frame[i]; /* at 25 the task ends: no offset */
        if (left > 0)
            y += intensity[i] * sinf(3.1415927f * 4.0f * left / 25) * sinf(3.1415927f * 0.5f * left / 25);
    }
    shake_off = y * 30.0f / 456.0f;
}

static int start_cam = -1; /* tryStartPosCamera: its CAMS index while on */
void camera_start_pos(int zone, int id)
{
    start_cam = -1;
    for (int i = 0; i < ncams; i++)
        if (cams[i].kind == 's' && cams[i].zone == zone && cams[i].num == (unsigned)(id & 0xFFFF)) start_cam = i;
}
void camera_start_pos_end(void) { start_cam = -1; }
static void update(const CamTarget *t);
void camera_update(const CamTarget *t)
{
    update(t);
    in_left = in_right = in_reset = 0;
    sj_frame = sj_on ? (sj_frame < 20 ? sj_frame + 1 : 20) : (sj_frame > 0 ? sj_frame - 1 : 0);
}

/* CameraDirector::controlCameraSE: the pad's round / reset this frame, if the current camera allows it (CAMIN_*
 * bits: the sound to play); taken by the next camera_update */
int camera_input(int left, int right, int reset)
{
    if (ev_on || tk_on || cur < -1) return 0;
    const CamParam *p = param();
    int type = p->type == 255 ? CAM_XZ_PARA : p->type;
    int el = 0, er = 0, ereset = 0;
    if (type == CAM_FOLLOW) { el = fl_valid_l; er = fl_valid_r; ereset = 1; }
    else if (type == CAM_TOWER) { el = tw_n > -tw_max; er = tw_n < tw_max; ereset = 1; }
    else if (type == CAM_XZ_PARA) el = er = ereset = p->num1 == 1;
    int se = 0;
    if (left) { se |= el ? CAMIN_MOVE : CAMIN_NG; in_left = el; }
    if (right) { se |= er ? CAMIN_MOVE : CAMIN_NG; in_right = er; }
    if (reset) { se |= ereset ? CAMIN_RESET | CAMIN_MOVE : CAMIN_NG; in_reset = ereset; }
    return se;
}

static void update(const CamTarget *t)
{
    shake_update();
    int a = -1;
    for (int i = nareas - 1; i >= 0; i--)
        if (inside(&areas[i], t->pos)) { a = i; break; }
    /* no area: keep the current camera (the ground-triangle cameras are not ported), the default one at first */
    int want = start_cam >= 0 ? start_cam : a >= 0 && areas[a].cam != 0xFFFF ? areas[a].cam : (cur == -2 || cur == -3 ? -1 : cur);
    V3 move = vsub(t->pos, last_target);
    last_target = t->pos;
    if (pr_on) { blend_to(pr_eye, pr_watch, pr_up); type_eye = pr_eye; type_up = pr_up; fresh = 0; return; }
    if (tk_on) {
        V3 e, w, u;
        if (talk_pose(t, &e, &w, &u)) { type_eye = e; type_up = u; fresh = 0; }
        else { e = i_eye; w = i_watch; u = i_up; }
        blend_to(e, w, u);
        return;
    }
    if (ev_on) { /* MultiEventCamera::update: the next one after its evfrm frames; endForceAtLanding */
        if (ev_ending && !t->jumping && !t->flying) { ev_on = 0; cur = -3; }
        else {
            const CamParam *p = &cams[ev_first + ev_i];
            if (p->evfrm >= 0 && ev_t >= p->evfrm && ev_i < ev_n - 1) {
                ev_i++; ev_t = 0;
                timer = 0; camint = cams[ev_first + ev_i].camint; fresh = 1;
                type_eye = f_eye; type_watch = f_watch; type_up = f_up;
            }
            ev_t++;
            CamTarget et = *t;
            et.pos = ev_pos; et.up = ev_up; et.front = ev_front; et.flying = 0;
            V3 e, w, u;
            zone_pose(&cams[ev_first + ev_i], &et, move, &e, &w, &u);
            fresh = 0;
            type_eye = e; type_up = u;
            blend_to(e, w, u);
            return;
        }
    }
    if (want != cur) { /* CameraManGame.cpp:250-285: reset the new camera from the current pose, blend camint */
        int first = cur == -2;
        cur = want;
        timer = 0;
        camint = first ? 0 : back_camint >= 0 ? back_camint : param()->camint;
        back_camint = -1;
        fresh = 1;
#ifdef BENCH
        printf("camera %d %s\n", cur, camera_type_name());
#endif
        if (first) {
            V3 e, w, u;
            type_eye = vadd(t->pos, vadd(vmul(t->up, 600.0f), vmul(t->front, -1200.0f)));
            type_up = t->up;
            type_watch = t->pos;
            zone_pose(param(), t, move, &e, &w, &u);
            i_eye = f_eye = e; i_watch = f_watch = w; i_up = f_up = u;
            type_eye = e; type_up = u;
        } else {
            type_eye = f_eye; type_watch = f_watch;
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
}

/* CameraSubjective (MarioFpView's first-person view): 120 up from Mario's feet, his frame turned by the stick (sPadStickPlay
 * 0.3; the angles' speeds fade 0.9 a frame, gain a tenth of the stick, x 3: pitch -45..90, yaw -80..80 degrees), the
 * watch point 200 ahead; CameraDirector::calcSubjective blends it in and out over sSubjectiveInterpolateFrame 20
 * (half a cosine). ponytail: one blend for position and turn (Petari starts the turn 10 frames later) */
static V3 sj_pos, sj_x, sj_y, sj_z;
static float sj_ax, sj_ay, sj_vx, sj_vy, sj_sx, sj_sy;
void camera_fp(int on, V3 pos, V3 up, V3 front, int at_once)
{
    if (on && !sj_on) { /* CameraSubjective::reset */
        sj_pos = vadd(pos, vmul(up, 120.0f));
        sj_y = up; sj_z = front; sj_x = vnorm(vcross(up, front));
        sj_ax = sj_ay = sj_vx = sj_vy = 0.0f;
    }
    sj_on = on;
    if (!on && at_once) sj_frame = 0;
}
void camera_fp_look(float sx, float sy) { sj_sx = sx; sj_sy = sy; }
static float stick_play(float v) { return v < -0.3f ? clampf((v + 0.3f) / 0.7f, -1, 1) : v > 0.3f ? clampf((v - 0.3f) / 0.7f, -1, 1) : 0.0f; }
static void fp_calc(V3 *eye, V3 *watch, V3 *up)
{
    float ax = -stick_play(sj_sy), ay = -stick_play(sj_sx); /* the stick up looks up, right looks right */
    sj_ax = clampf(sj_ax + sj_vx * 3.0f, -45.0f, 90.0f);
    sj_ay = clampf(sj_ay + sj_vy * 3.0f, -80.0f, 80.0f);
    sj_vx = clampf(sj_vx * 0.9f + ax / 10.0f, -1.0f, 2.0f);
    sj_vy = clampf(sj_vy * 0.9f + ay / 10.0f, -1.0f, 2.0f);
    float a = sj_ax * PI / 180.0f, b = sj_ay * PI / 180.0f;
    V3 z = v3(cosf(a) * sinf(b), -sinf(a), cosf(a) * cosf(b)), y = v3(sinf(a) * sinf(b), cosf(a), sinf(a) * cosf(b));
    V3 zw = vadd(vadd(vmul(sj_x, z.x), vmul(sj_y, z.y)), vmul(sj_z, z.z));
    V3 yw = vadd(vadd(vmul(sj_x, y.x), vmul(sj_y, y.y)), vmul(sj_z, y.z));
    *eye = sj_pos; *watch = vadd(sj_pos, vmul(zw, 200.0f)); *up = vnorm(yw);
}

static int ovr;
static V3 o_eye, o_watch, o_up;
static float o_fovy;
void camera_set_override(int on, V3 eye, V3 watch, V3 up, float fovy)
{
    ovr = on; o_eye = eye; o_watch = watch; o_up = up; o_fovy = fovy;
    if (!on) return;
    f_eye = eye; f_watch = watch; f_up = up; f_fovy = fovy; /* the follow camera starts from there afterwards */
}
/* endAnimCamera(frame): from the anim camera's last pose back to the game camera (reset from it) over frame (its event
 * has no chunk: sendFinishInterpolateFrame takes the frame, -1 sDefaultBlendFrame 60) */
void camera_override_end(int frame)
{
    if (!ovr) return;
    ovr = 0;
    i_eye = f_eye = o_eye; i_watch = f_watch = o_watch; i_up = f_up = o_up; f_fovy = o_fovy;
    cur = -3; back_camint = frame >= 0 ? frame : 60;
    printf("camera: anim end, back over %d\n", back_camint);
}

void camera_pose(V3 *eye, V3 *watch, V3 *up, float *fovy)
{
    if (ovr) { *eye = o_eye; *watch = o_watch; *up = o_up; *fovy = o_fovy; return; }
    *eye = f_eye; *watch = f_watch; *up = f_up; *fovy = f_fovy;
    if (sj_frame > 0) {
        static V3 se, sw, su;
        static int calced = -1;
        if (calced != sj_frame || sj_on) { fp_calc(&se, &sw, &su); calced = sj_frame; }
        float r = 0.5f - 0.5f * cosf(PI * sj_frame / 20.0f);
        *eye = vadd(f_eye, vmul(vsub(se, f_eye), r));
        *watch = vadd(f_watch, vmul(vsub(sw, f_watch), r));
        *up = vnorm(vadd(f_up, vmul(vsub(su, f_up), r)));
    }
}

int camera_current(void) { return cur < ncams ? cur : -1; }

const char *camera_type_name(void)
{
    static const char *n[] = {"XZ PARA", "TOWER", "FOLLOW", "WONDER PLANET", "EYEPOS FIX", "POINT FIX",
                              "EYEPOS FIX THERE", "CHARMED FIX", "TOWER POS", "FRONT AND BACK", "TRUNDLE", "CUBE PLANET", "OBJ PARALLEL"};
    const CamParam *p = param();
    return p->type == 255 ? "OTHER" : n[p->type];
}
