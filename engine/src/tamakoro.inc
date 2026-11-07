/* Tamakoro.cpp (TamakoroWithTutorial: the star ball Mario rides), TamakoroTutorial.cpp (its SignBoard),
 * SpherePadController / SphereAccelSensorController (the Wii remote's tilt) and AudTamakoroBgmCtrl after Petari.
 * The ball (binder 150, gravity 2, its Body / Hit 150 and Bind 165 sensors) waits pushing Mario (StandByBind; with a
 * tutorial StandByTutorial till its talk lets him ride); Mario touching Bind from within 60 degrees of its top is bound
 * (AutoRushBegin: requestBind): BindStart (40, the jump arc onto it, SlideStomachRecover) or straight BindStartLand
 * (14: BallLand, SE_SM_RIDE_START, its SW_A on, BGM_TAMAKORO), then the tutorial or Wait. Wait / Fall / Jump /
 * BumpWall / Land roll it by the tilt's move vector (0.4 on the ground, 0.2 in the air), A jumps (40 up, 7 along the
 * tilt), walls and hard landings bounce it with their effects, SEs, rumbles and shakes; its moment rolls the ball,
 * its Ring stays upright leaning 45 degrees to the tilt; Mario stands on it posed by the tilt (タマコロ移動 blended by
 * speed, his upper body turned about Spine2). The JumpHole / SphereRailDash / BallRail / BallOpener nerves answer
 * those objects' messages (tmk_msg). END_BALL_BIND (BallOpener): BindEnd, SwingRopeSpin and the force jump off, the
 * ball hidden, its Break effect, then its power star from its place once Mario lands (or 90). The tutorial (arg0 -1):
 * First pushes Mario off till he comes within 400 (Common_TamakoroTutorial000 force talk), then the ride; on the ball
 * WaitRaiseTalk / WaitRaiseStable / RaiseOk / RaiseOkTalk / FinalTalk, then Wait and onGameEventFlag
 * タマコロチュートリアル; once that flag is on, Common_TamakoroTutorial007 offers to skip it. A SignBoard rolled
 * over falls (DownFront / DownBack) and gets up. One ball a scene (every placement has one).
 * ponytail: the PS2 pad has no accelerometer: the left stick stands for the remote's tilt (SphereAccelSensorController::
 * clacXY's angles: x right, y forward, Tamakoro.cpp:665 addVelocityOperate) and the tutorial's raise check
 * (TamakoroTutorial.cpp:120 isNearAngleDegree(hRaiseAcc, mPadAccel)) takes the pad as held up; BGM_TAMAKORO is a
 * rendered stream: no track mutes / tempo / pitch by speed (AudTamakoroBgmCtrl::control); no PlayerActionGuidance
 * layout (TamakoroTutorial.cpp:113, the Tamakoro*GuidanceCube areas); the SignBoard's CSV shadow not drawn */
enum { TMK_STANDBYTUTORIAL, TMK_STANDBYBIND, TMK_BINDSTART, TMK_BINDSTARTLAND, TMK_TUTORIAL, TMK_WAIT, TMK_FALL, TMK_JUMP,
       TMK_BUMPWALL, TMK_LAND, TMK_JUMPHOLE, TMK_JUMPHOLESETUP, TMK_JUMPHOLELAUNCH, TMK_DASHRAIL, TMK_DASHRAILEND, TMK_RIDERAIL,
       TMK_RIDERAILFASTSTART, TMK_RIDERAILFAST, TMK_RIDERAILFASTEND, TMK_BINDEND, TMK_DEAD };
enum { TMT_FIRST, TMT_DENYTALK, TMT_FIRSTFORCETALK, TMT_WAITRIDETALK, TMT_WAITRIDE, TMT_WAITRAISETALK, TMT_WAITRAISESTABLE,
       TMT_RAISEOK, TMT_RAISEOKTALK, TMT_FINALTALK, TMT_WAIT, TMT_DIRECTRIDE, TMT_TRAMPLEFRONT, TMT_TRAMPLEBACK, TMT_RECOVERFRONT,
       TMT_RECOVERBACK };
/* the messages other objects send it (receiveOtherMsg) */
enum { TMKM_SET_UP_JUMP_HOLE, TMKM_SHOOT_JUMP_HOLE, TMKM_END_JUMP_HOLE, TMKM_END_RAIL_DASH, TMKM_END_BALL_RAIL, TMKM_END_BALL_BIND };
#define TMK_R 150.0f /* mBaseRadius */
static struct {
    int row, nrv, st, hidden, sensors, bind_sensor, bind_off, ctrl_off, air, bgm_steps, star_id;
    V3 pos, vel, grav, fix, gn, wn, gp, wp;
    int ground, wall, wall_row; /* wall_row: the placement its wall is on, -1 the map */
    Quat ball;                 /* mBallRotateQuat */
    V3 my, mz;                 /* mMarioRotateQuat (its Y, Z) */
    V3 ring_up, kick, bind_req, mario_up, mario_pos, moment, mario_front, dir_mario, accel_dir;
    float accel_rate, upper_y, off, off_v;
    const Model *md;
    Anim *a;
    u32 lv;
    char after[24];            /* changeAnimationE's second BCK: played once the first is over */
    char fx_blur[4096];
    /* TamakoroTutorial (mTutorial, its SignBoard): row -1 none */
    int trow, tnrv, tst, a4, a5, push_on, talk, talk_auto;
    V3 tpos, tup, tfront;
    const Model *tmd;
    Anim *ta;
} tmk = {.row = -1, .trow = -1};
static int tmk_riding(void) /* MarioActor _934 with the ball its _924: bound from requestBind to BindEnd */
{
    return tmk.row >= 0 && tmk.nrv != TMK_STANDBYTUTORIAL && tmk.nrv != TMK_STANDBYBIND && tmk.nrv != TMK_BINDEND && tmk.nrv != TMK_DEAD;
}
static void tmk_set(int n) { tmk.nrv = n; tmk.st = -1; }
static void tmt_set(int n) { tmk.tnrv = n; tmk.tst = -1; }
static void tmk_se(u32 id) { se_start(id, &tmk.pos.x); }
static int tmk_zero(V3 v) { return v_near0(v, 0.001f); } /* MR::isNearZero */
static int tmk_normz(V3 *v) { if (vlen(*v) <= 0.0000038f) { *v = v3(0, 0, 0); return 1; } *v = vnorm(*v); return 0; } /* normalizeOrZero: 1 if it was zero */
static V3 tmk_vert_zx(V3 g) /* makeAxisVerticalZX */
{
    V3 v = vtangent(v3(0, 0, 1), vnorm(g));
    if (tmk_zero(v)) v = vtangent(v3(1, 0, 0), vnorm(g));
    return vnorm(v);
}
static V3 tmk_turn_vec_deg(V3 from, V3 to, float deg) /* turnVecToVecDegree(.., TVec3f(0, 1, 0)) */
{
    float raw = acosf(clampf(vdot(from, to), -1.0f, 1.0f)), ang = deg * 0.017453293f;
    if (raw < ang) ang = raw;
    if (raw <= 0.0000038f) return from;
    V3 v;
    V3 ax = vcross(from, to);
    if (vdot(from, to) < 0.0f && vlen(ax) < 1e-6f) v = rot_axis(from, v3(0, 1, 0), ang); /* isSameDirection: about the axis */
    else v = rot_axis(from, vnorm(ax), ang);
    return tmk_zero(v) ? from : vnorm(v);
}
static void tmk_blend_up_front(V3 *y, V3 *z, V3 up, V3 front, float ru, float rf) /* blendQuatUpFront on Mario's quat */
{
    V3 ax = vcross(*y, up);
    float c = clampf(vdot(*y, up), -1.0f, 1.0f);
    if (vlen(ax) > 1e-6f) {
        float a = acosf(c) * ru;
        ax = vnorm(ax);
        *y = vnorm(rot_axis(*y, ax, a)); *z = vnorm(rot_axis(*z, ax, a));
    }
    V3 tf = vtangent(front, *y);
    if (tmk_normz(&tf)) return;
    ax = vcross(*z, tf);
    c = clampf(vdot(*z, tf), -1.0f, 1.0f);
    if (vlen(ax) > 1e-6f) {
        float a = acosf(c) * rf;
        *z = vnorm(rot_axis(*z, vnorm(ax), a));
    }
    *z = vnorm(vtangent(*z, *y));
}
static void tmk_moment_rot(void) /* rotateQuatMoment: the ball turned by its moment (world axis) */
{
    float r = vlen(tmk.moment);
    if (r > 1e-9f) tmk.ball = q_norm(q_mul(q_axis(vmul(tmk.moment, 1.0f / r), r), tmk.ball));
}
static V3 tmk_moment_roll(V3 vel, V3 up) /* calcMomentRollBall */
{
    if (tmk_normz(&up)) return v3(0, 0, 0);
    return vmul(vcross(up, vel), 1.0f / TMK_R);
}
static float tmk_norm01(float x, float lo, float hi) /* MR::normalize */
{
    float r = hi - lo;
    if (fabsf(r) < 0.001f) return x < lo ? 0.0f : 1.0f;
    return (clampf(x, lo, hi) - lo) / r;
}
/* SpherePadController::calcDirSphereMove(.., gravity, 0) with the tilt as the stick (see the head comment) */
static float tmk_move_vector(V3 *dir)
{
    float x = pad_lx(), y = pad_ly(), mag = sqrtf(x * x + y * y);
    if (mag >= 0.0001f) {
        V3 eye, watch, cu;
        float fovy;
        camera_pose(&eye, &watch, &cu, &fovy);
        V3 dz = vnorm(vsub(eye, watch)), dx = vnorm(vcross(cu, dz)), dy = vcross(dz, dx); /* getCameraInvViewMtx */
        if (vdot(dy, vmul(tmk.grav, -1.0f)) < 0.0f) { dx = vmul(dx, -1.0f); dy = vmul(dy, -1.0f); dz = vmul(dz, -1.0f); }
        Quat r = q_between(dy, vmul(tmk.grav, -1.0f));
        dx = q_rot(r, dx); dz = q_rot(r, dz);
        *dir = vsub(vmul(dx, x), vmul(dz, y));
        mag = vlen(*dir);
        *dir = mag > 0.0f ? vmul(*dir, 1.0f / mag) : v3(0, 0, 0);
    } else *dir = v3(0, 0, 0);
    return mag > 1.0f ? 1.0f : mag;
}
static int tmk_jump_power(void) { return pad_pressed(PAD_CROSS); } /* calcJumpPower: testCorePadTriggerA */

/* Mario's BCKs (startBckPlayer / startBckPlayerJ / setBckRatePlayer / setBckBlendWeight) */
static void tmk_mbck(const char *bck, const char *after) /* startBckPlayer(bck, after): the BCK, as it loops itself */
{
    char n[24];
    int mode = 0;
    snprintf(n, sizeof n, "%s", bck);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (anim_clip_info(n, &mode) < 0) printf("tamakoro: no Mario clip %s\n", n);
    mario_play(n, 1.0f, mode == 2 || mode == 4);
    tmk.after[0] = 0;
    if (after && strcmp(after, bck)) {
        snprintf(tmk.after, sizeof tmk.after, "%s", after);
        for (char *c = tmk.after; *c; c++) *c = (char)tolower((unsigned char)*c);
    }
}
static void tmk_mrate(float r) { if (mario_anim) mario_anim->speed = r; } /* setBckRatePlayer */
static int tmk_mstopped(void) { return !mario_anim || (!mario_anim->loop && mario_anim->done); } /* isBckStoppedPlayer */
static void tmk_mweight(float a, float b, float c, float d) { const float w[4] = {a, b, c, d}; m_force_weight(w); } /* setBckBlendWeight */

static void tmk_fx_hit_cls(const char *cls, const char *name, V3 at, V3 dir) /* emitEffectHit: makeMtxUpNoSupportPos(dir, at) */
{
    float m[16];
    V3 y = vnorm(dir), s = fabsf(y.z) >= fabsf(y.x) && fabsf(y.z) >= fabsf(y.y) ? v3(0, 1, 0) : v3(0, 0, 1);
    V3 x = vnorm(vcross(y, s)), z = vcross(x, y);
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z; m[4] = y.x; m[5] = y.y; m[6] = y.z; m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = at.x; m[13] = at.y; m[14] = at.z;
    fx_named(cls, name, m, NULL, NULL);
}
static void tmk_fx_hit(const char *name, V3 at, V3 dir) { tmk_fx_hit_cls("TamakoroWithTutorial", name, at, dir); }
static void tmk_base(float m[16]) /* setBaseTRMtx(mBallRotateQuat) */
{
    V3 x, y, z;
    q_axes(tmk.ball, &x, &y, &z);
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z; m[4] = y.x; m[5] = y.y; m[6] = y.z; m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = tmk.pos.x; m[13] = tmk.pos.y; m[14] = tmk.pos.z;
}
static void tmk_fx(const char *name, const void *key) { float m[16]; tmk_base(m); fx_named("TamakoroWithTutorial", name, m, tmk.a ? (const float (*)[16])tmk.a->world : NULL, key); }

/* the binder's results (Binder: hit power, fix reaction) */
static float tmk_hit_ground(void) { if (!tmk.ground) return 0.0f; float d = -vdot(tmk.vel, tmk.gn); return d > 0.0f ? d : 0.0f; } /* calcHitPowerToGround */
static float tmk_hit_wall(void) { if (!tmk.wall) return 0.0f; float d = -vdot(tmk.vel, tmk.wn); return d > 0.0f ? d : 0.0f; }   /* calcHitPowerToWall */
static int tmk_rebound(float a2, float a3, float a4) /* reboundVelocityFromCollision */
{
    if (!tmk.ground && !tmk.wall && tmk_zero(tmk.fix)) return 0;
    V3 n = tmk.fix;
    if (tmk_zero(n)) return 0;
    n = vnorm(n);
    float d = vdot(n, tmk.vel);
    if (d < -a3) { tmk.vel = vsub(tmk.vel, vmul(n, d)); tmk.vel = vmul(tmk.vel, a4); tmk.vel = vsub(tmk.vel, vmul(n, d * a2)); return 1; }
    if (d < 0.0f) tmk.vel = vsub(tmk.vel, vmul(n, d));
    return 0;
}
static void tmk_vel_normal(float fric) /* updateVelocityNormal */
{
    tmk_rebound(0.0f, 0.0f, 1.0f);
    tmk.vel = vadd(tmk.vel, vmul(tmk.grav, 2.0f)); /* addVelocityToGravity sGravityAccel */
    tmk.vel = vmul(tmk.vel, tmk.ground ? fric : 0.995f);
}
static void tmk_add_operate(void) /* addVelocityOperate */
{
    tmk.accel_rate = tmk_move_vector(&tmk.accel_dir);
    tmk.vel = vadd(tmk.vel, vmul(tmk.accel_dir, tmk.accel_rate * (tmk.ground ? 0.4f : 0.2f)));
}
static void tmk_move_bck(float sp) /* updateMoveBckBlend */
{
    float w0 = 0, w1 = 0, w2 = 0, w3 = 0, rate;
    if (sp < 0.25f) { w1 = tmk_norm01(sp, 0.0f, 0.25f); w0 = 1.0f - w1; rate = 1.0f + (1.0f - 1.0f) * w1; }
    else if (sp < 6.0f) { w2 = tmk_norm01(sp, 0.25f, 6.0f); w1 = 1.0f - w2; rate = 1.0f + (2.0f - 1.0f) * w2; }
    else if (sp < 18.0f) { w3 = tmk_norm01(sp, 6.0f, 18.0f); w2 = 1.0f - w3; rate = 2.0f + (3.0f - 2.0f) * w3; }
    else { w3 = 1.0f; rate = sp / 18.0f * 3.0f; if (rate > 4.0f) rate = 4.0f; }
    tmk_mweight(w0, w1, w2, w3);
    tmk_mrate(rate);
}
static void tmk_squat_bck(float sp) /* updateSquatBckBlend */
{
    float w1 = tmk_norm01(sp, 0.0f, 6.0f), w0 = 1.0f - w1;
    tmk_mweight(w0, w1, 0.0f, 0.0f);
    tmk_mrate(1.0f + (2.0f - 1.0f) * w1);
}
static void tmk_mario_pose(float deg) /* updateMarioPose */
{
    V3 vh = vtangent(tmk.vel, vnorm(tmk.grav));
    if (tmk_zero(vh)) { if (tmk.accel_rate > 0.001f) turn_on_plane(&tmk.mario_front, tmk.accel_dir, tmk.grav, cosf(deg * tmk.accel_rate * 0.017453293f)); }
    else turn_on_plane(&tmk.mario_front, vh, tmk.grav, cosf(deg * 0.017453293f));
    V3 xd = vcross(tmk.my, tmk.mz), zd = tmk.mz;
    if (tmk.accel_rate > 0.0f) {
        float d = tmk_norm01(1.0f - vdot(tmk.accel_dir, zd), 0.1f, 1.0f);
        d *= vdot(tmk.accel_dir, xd) >= 0.0f ? 1.0f : -1.0f;
        tmk.upper_y += d * 5.0f; /* sMarioUpperYAccel */
    }
    tmk.upper_y *= 0.9f; /* sMarioUpperYFric */
    V3 up = vmul(tmk.grav, -1.0f);
    tmk.mario_up = vmul(tmk.grav, -1.0f);
    V3 ax = vcross(tmk.accel_dir, tmk.grav);
    if (!tmk_normz(&ax)) { up = rot_deg(up, ax, tmk.accel_rate * 35.0f); tmk.mario_up = rot_deg(tmk.mario_up, ax, tmk.accel_rate * 20.0f); }
    blend_sphere(tmk.dir_mario, up, &tmk.dir_mario, 0.02f); /* sStandDirInterRate */
    if (tmk_normz(&tmk.dir_mario)) tmk.dir_mario = vmul(tmk.grav, -1.0f);
    tmk_blend_up_front(&tmk.my, &tmk.mz, tmk.mario_up, tmk.mario_front, 0.1f, 0.25f); /* sMarioUpAdjustRate, sMarioFrontAdjustRate */
}
static void tmk_mario_offset(void) /* updateMarioOffset */
{
    tmk.off_v = (tmk.off_v - 1.0f) * 0.99f;
    tmk.off = clampf(tmk.off + tmk.off_v, 0.0f, 150.0f);
}
static int tmk_use_offset(void) { return tmk.nrv != TMK_BINDSTART && tmk.nrv != TMK_BINDSTARTLAND; } /* isUseMarioOffset */
static int tmk_ride_rail(void) { return tmk.nrv == TMK_RIDERAIL || tmk.nrv == TMK_RIDERAILFASTSTART || tmk.nrv == TMK_RIDERAILFAST || tmk.nrv == TMK_RIDERAILFASTEND; }
static int tmk_enemy_attack_on(void) /* isEnableEnemyAttack (= isEnableBallBind) */
{
    return tmk.nrv == TMK_WAIT || tmk.nrv == TMK_FALL || tmk.nrv == TMK_JUMP || tmk.nrv == TMK_BUMPWALL || tmk.nrv == TMK_DASHRAILEND || tmk.nrv == TMK_LAND;
}
static int tmk_need_tutorial(void) { return tmk.trow >= 0 && tmk.tnrv == TMT_WAITRIDE; } /* isNeedTutorial */
static void tmk_bgm_start(void) /* startStageBGM BGM_TAMAKORO(_2), mBgmCtrl->init */
{
    bgm_start(on_stage("HellProminenceGalaxy") ? "BGM_TAMAKORO_2" : "BGM_TAMAKORO");
    tmk.bgm_steps = 0;
}
static int tmk_try_fall(void) { if (tmk.air > 5) { tmk_set(TMK_FALL); return 1; } return 0; } /* sFallStartTime */
static int tmk_try_jump(void) { if (tmk_jump_power() && tmk.ground) { tmk_set(TMK_JUMP); return 1; } return 0; }
static int tmk_bump_msg(int ground); /* sendMsgToWallSensor(ACTMES_BALL_DASH_WALL / GROUND): a map object takes it (none yet) */
static int tmk_try_bump_wall(void)
{
    if (tmk_hit_wall() > 10.0f) { /* sNeedBumpWallPower */
        float reb = -0.6f;
        if (tmk.nrv == TMK_JUMP && tmk_bump_msg(0)) reb = 0.3f;
        tmk_rebound(reb, 0.0f, 1.0f);
        tmk.off_v = 2.0f;
        tmk_set(TMK_BUMPWALL);
        return 1;
    }
    return 0;
}
static int tmk_try_land(void)
{
    float p = tmk_hit_ground();
    if (p > 20.0f) { /* sNeedLandPower */
        float reb = 0.0f;
        if (tmk.nrv == TMK_JUMP && tmk_bump_msg(1)) reb = 0.3f;
        tmk.off_v = -p; tmk.off -= p;
        tmk_rebound(reb, 0.0f, 1.0f);
        tmk_set(TMK_LAND);
        return 1;
    }
    if (tmk.st > 2 && tmk.ground) { tmk.off_v = -p; tmk_set(TMK_WAIT); return 1; } /* sLandTime */
    return 0;
}
static void tmk_reaction(void) /* reactionCollision */
{
    float wp = tmk_hit_wall(), gp = tmk_hit_ground(), hp = wp >= gp ? wp : gp;
    if (wp > 5.0f) tmk_fx_hit("WallHit", tmk.wp, tmk.wn);
    if (gp > 25.0f) tmk_fx_hit("Land", tmk.gp, tmk.gn);
    if (wp > 5.0f) { tmk_se(SE_SM_IRONSPH_HIT); se_start(SE_PV_GUARD, &mario->pos.x); }
    if (hp > 60.0f) { pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_STRONG); }
    else if (hp > 25.0f) { pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); }
    else if (hp > 5.0f) { pad_rumble(RUMBLE_WEAK); camera_shake(SHAKE_WEAK); }
}
static void tmk_tutorial_request_start(void);
static void tmk_tutorial_request_end(void);
static void tmk_exe(void)
{
    int st = ++tmk.st;
    switch (tmk.nrv) {
    case TMK_STANDBYTUTORIAL: case TMK_STANDBYBIND: tmk_vel_normal(0.95f); break;
    case TMK_BINDSTART: {
        if (st == 0) {
            tmk_mbck("SlideStomachRecover", NULL);
            pad_rumble(RUMBLE_WEAK);
            tmk.dir_mario = vmul(tmk.grav, -1.0f);
            if (tmk_normz(&tmk.mario_front)) tmk.mario_front = tmk_vert_zx(tmk.grav);
            tmk_fx_hit("TamakoroLand", tmk.mario_pos, vsub(tmk.mario_pos, tmk.pos));
        }
        float t = fminf((float)st / 40.0f, 1.0f); /* calcNerveRate sBindStartTime */
        V3 expect = vadd(tmk.pos, vmul(tmk.dir_mario, TMK_R)), h = vadd(vmul(tmk.bind_req, 1.0f - t), vmul(expect, t));
        t = t * 2.0f - 1.0f;
        V3 upv = vmul(tmk.grav, -(t * t * -150.0f + TMK_R)); /* sBindStartJumpHeight */
        tmk_blend_up_front(&tmk.my, &tmk.mz, vmul(tmk.grav, -1.0f), tmk.mario_front, 0.1f, 0.1f);
        tmk.mario_pos = vadd(h, upv);
        if (st > 40) tmk_set(TMK_BINDSTARTLAND);
        break;
    }
    case TMK_BINDSTARTLAND:
        if (st == 0) {
            if (!tmk_need_tutorial()) tmk_bgm_start();
            tmk_mbck("Land", "BallLand");
            pad_rumble(RUMBLE_MIDDLE);
            tmk.dir_mario = vsub(tmk.mario_pos, tmk.pos);
            if (tmk_normz(&tmk.dir_mario)) tmk.dir_mario = vmul(tmk.grav, -1.0f);
            tmk.mario_pos = vadd(tmk.pos, vmul(tmk.dir_mario, TMK_R));
            tmk.mario_up = vmul(vsub(tmk.dir_mario, tmk.grav), 0.5f);
            if (tmk_normz(&tmk.mario_up)) tmk.mario_up = vmul(tmk.grav, -1.0f);
            tmk_fx_hit("TamakoroLand", tmk.mario_pos, vsub(tmk.mario_pos, tmk.pos));
            se_start(SE_PV_CATCH, &mario->pos.x);
            tmk_se(SE_SM_RIDE_START);
            if (sc->objs[tmk.row].sw[SW_A] >= 0) sw_set(&sc->objs[tmk.row], SW_A, 1);
        }
        tmk_blend_up_front(&tmk.my, &tmk.mz, tmk.mario_up, tmk.mario_front, 0.1f, 0.1f);
        if (st > 14) { /* sBindStartLandTime */
            if (tmk_need_tutorial()) { tmk_tutorial_request_start(); tmk_set(TMK_TUTORIAL); }
            else { tmk_tutorial_request_end(); tmk_set(TMK_WAIT); }
        }
        break;
    case TMK_TUTORIAL: {
        if (st == 0) { m_anim("タマコロ移動"); tmk.vel = v3(0, 0, 0); tmk.accel_dir = v3(0, 0, 0); tmk.accel_rate = 0; tmk.moment = v3(0, 0, 0); tmk.kick = v3(0, 0, 0); }
        tmk.accel_rate = tmk_move_vector(&tmk.accel_dir);
        tmk.kick = vadd(tmk.kick, vmul(tmk.accel_dir, tmk.accel_rate * 0.5f)); /* sTutorialAccel */
        V3 mo = tmk_moment_roll(tmk.kick, vmul(tmk.grav, -1.0f));
        tmk.moment = vadd(vmul(tmk.moment, 0.75f), vmul(mo, 0.25f));
        tmk.kick = vmul(tmk.kick, 0.95f); /* sTutorialFric */
        tmk_move_bck(vlen(tmk.moment) * TMK_R);
        tmk_mario_pose(3.0f); tmk_mario_offset();
        break;
    }
    case TMK_WAIT:
        if (st == 0) m_anim("タマコロ移動");
        tmk_reaction(); tmk_add_operate(); tmk_vel_normal(0.99f); /* sGroundFric */
        tmk_move_bck(vlen(tmk.moment) * TMK_R);
        tmk_mario_pose(3.0f); tmk_mario_offset();
        if (tmk_try_jump() || tmk_try_fall()) break;
        break;
    case TMK_FALL:
        if (st == 0) tmk_mbck("Fall", "BallFall");
        tmk.off = clampf(tmk.off + 3.0f, 0.0f, 150.0f); /* sFallOffsetSpeed, sMarioJumpOffsetMax */
        tmk_reaction();
        if (!tmk_try_land()) { tmk_add_operate(); tmk_vel_normal(0.99f); tmk_mario_pose(3.0f); }
        break;
    case TMK_JUMP:
        if (st == 0) {
            tmk_mbck("BallJump", "BallJump");
            se_start(SE_PV_JUMP_M, &mario->pos.x);
            tmk_se(SE_SM_BALLOONSPH_JUMP);
            tmk.vel = vadd(tmk.vel, vmul(tmk.grav, -40.0f - vdot(tmk.vel, tmk.grav))); /* addVelocityToGravity sJumpPowerV */
            tmk.vel = vadd(tmk.vel, vmul(tmk.accel_dir, tmk.accel_rate * 7.0f));    /* addVelocityMoveToDirection sJumpPowerH */
        }
        tmk.off = clampf(tmk.off + 4.0f, 0.0f, 150.0f); /* sJumpOffsetSpeed */
        tmk_reaction();
        if (tmk_try_bump_wall() || tmk_try_land()) break;
        tmk_add_operate(); tmk_vel_normal(0.99f); tmk_mario_pose(3.0f);
        break;
    case TMK_BUMPWALL: case TMK_LAND:
        if (st == 0) {
            if (tmk.nrv == TMK_BUMPWALL) { tmk_mbck("BallHit", NULL); pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); }
            else { tmk_mbck("Land", NULL); pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); tmk_se(SE_SM_IRONSPH_LAND); }
        }
        tmk_reaction(); tmk_add_operate(); tmk_vel_normal(tmk.nrv == TMK_LAND ? 0.88f : 0.99f); /* sLandFric */
        tmk_mario_pose(3.0f); tmk_mario_offset();
        if (!tmk_try_jump() && st > 15) tmk_set(TMK_WAIT);
        break;
    case TMK_JUMPHOLE:
        if (st == 0) { pad_rumble(RUMBLE_WEAK); tmk.bind_off = 1; m_anim("タマコロしゃがみ"); tmk.accel_dir = v3(0, 0, 0); tmk.accel_rate = 0; tmk.off_v = 0; }
        tmk_squat_bck(vlen(tmk.moment) * TMK_R);
        tmk_mario_pose(3.0f); tmk_mario_offset();
        break;
    case TMK_JUMPHOLESETUP:
        if (st == 0) pad_rumble(RUMBLE_WEAK);
        tmk_squat_bck(0.0f);
        tmk_mario_pose(3.0f); tmk_mario_offset();
        break;
    case TMK_JUMPHOLELAUNCH:
        if (st == 0) { tmk_mbck("BallJump", NULL); pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_STRONG); }
        tmk_fx("BigJumpBlur", tmk.fx_blur); /* emitEffect, kept till endJumpHoleLaunch's deleteEffect */
        tmk.ctrl_off = 1;
        tmk.off = clampf(tmk.off + 3.0f, 0.0f, 150.0f);
        tmk_mario_pose(3.0f);
        break;
    case TMK_DASHRAIL:
        if (st == 0) { tmk_mbck("BallRoll", NULL); se_start(SE_PV_JUMP_JOY, &mario->pos.x); tmk_se(SE_SM_IRONSPH_RAILDASH); pad_rumble(RUMBLE_STRONG); tmk.accel_dir = v3(0, 0, 0); tmk.accel_rate = 0; }
        tmk.ctrl_off = 1;
        tmk_mario_pose(10.0f); tmk_mario_offset();
        tmk_mrate(vlen(tmk.vel) / 60.0f); /* sDashBallRollSpeed */
        break;
    case TMK_DASHRAILEND:
        if (st == 0) tmk.off_v = 0;
        if (st < 40) tmk_mrate(vlen(tmk.vel) / 60.0f); /* sDashRailTime */
        if (st == 40) tmk_mbck("Fall", "BallFall");
        tmk.off = clampf(tmk.off + 3.0f, 0.0f, 150.0f);
        if (tmk_try_bump_wall() || tmk_try_land()) break;
        tmk_vel_normal(0.99f); /* sDashRailFric */
        tmk_mario_pose(10.0f);
        break;
    case TMK_RIDERAIL: case TMK_RIDERAILFASTSTART: case TMK_RIDERAILFAST: case TMK_RIDERAILFASTEND: {
        int n = tmk.nrv;
        if (st == 0) {
            if (n == TMK_RIDERAIL) { m_anim("タマコロ移動"); pad_rumble(RUMBLE_MIDDLE); }
            if (n == TMK_RIDERAILFASTSTART) { tmk_mbck("BallRollStart", NULL); tmk_mrate(1.5f); } /* sRollBckRate */
            if (n == TMK_RIDERAILFAST) { tmk_mbck("BallRoll", NULL); tmk_mrate(1.5f); }
            if (n == TMK_RIDERAILFASTEND) { tmk_mbck("BallRollEnd", NULL); tmk_mrate(1.5f); }
        }
        if (n == TMK_RIDERAILFAST || n == TMK_RIDERAILFASTSTART) tmk.ctrl_off = 1;
        tmk.accel_dir = tmk.vel; /* updateRideRail */
        tmk.accel_rate = tmk_normz(&tmk.accel_dir) ? 0.5f : 0.0f;
        float sp = vlen(tmk.moment) * TMK_R;
        tmk_mario_pose(15.0f); tmk_mario_offset();
        if (n == TMK_RIDERAIL) { tmk_move_bck(sp); if (sp > 30.0f) tmk_set(TMK_RIDERAILFASTSTART); } /* sRaidRailFastSpeed */
        else if (n == TMK_RIDERAILFASTSTART) { if (tmk_mstopped()) tmk_set(TMK_RIDERAILFAST); }
        else if (n == TMK_RIDERAILFAST) { tmk_mrate(vlen(tmk.vel) / 60.0f); if (sp < 30.0f) tmk_set(TMK_RIDERAILFASTEND); }
        else if (tmk_mstopped()) tmk_set(TMK_RIDERAIL);
        break;
    }
    case TMK_BINDEND:
        if (st == 0) {
            pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_STRONG);
            tmk.accel_dir = v3(0, 0, 0); tmk.accel_rate = 0;
            if (sc->objs[tmk.row].sw[SW_A] >= 0) sw_set(&sc->objs[tmk.row], SW_A, 0);
            tmk_fx("Break", NULL);
            tmk_se(SE_SM_RIDE_END);
            se_start(SE_PV_JUMP_S, &mario->pos.x);
            bgm_stop(10);
        }
        if ((st > 12 && mario->grounded) || st > 90) { /* sForceBindEndTime */
            bgm_return();
            note_star_row = tmk.row; note_star_at = tmk.pos; row_kill(tmk.row); /* requestAppearPowerStar(this, mPosition) */
            printf("tamakoro: bind end, its star at %.0f %.0f %.0f\n", tmk.pos.x, tmk.pos.y, tmk.pos.z);
            tmk_set(TMK_DEAD);
        }
        break;
    default: break;
    }
}
static int tmk_request_end_bind(void) /* requestEndBind */
{
    if (tmk.nrv == TMK_BINDEND || tmk.nrv == TMK_STANDBYBIND) return 0;
    tmk_mbck("SwingRopeSpin", NULL);
    mario->up = up_at(mario->pos, mario->up);
    m_force_free_jump(mario, vadd(vmul(tmk.mario_front, -5.0f), vmul(tmk.grav, -35.0f))); /* endBindAndPlayerForceJump sEndBindFrontPower, sEndBindJumpPower */
    tmk.upper_y = 0.0f;
    tmk.hidden = 1; tmk.sensors = 0; tmk.vel = v3(0, 0, 0);
    tmk_set(TMK_BINDEND);
    return 1;
}
/* receiveOtherMsg: the objects' messages to its Body sensor; 1 taken */
static int tmk_msg(int msg)
{
    if (tmk.row < 0) return 0;
    switch (msg) {
    case TMKM_SET_UP_JUMP_HOLE: if (tmk.nrv == TMK_JUMPHOLE) { tmk_set(TMK_JUMPHOLESETUP); return 1; } return 0;
    case TMKM_SHOOT_JUMP_HOLE: if (tmk.nrv == TMK_JUMPHOLESETUP) { tmk_set(TMK_JUMPHOLELAUNCH); return 1; } return 0;
    case TMKM_END_JUMP_HOLE:
        if (tmk.nrv != TMK_JUMPHOLELAUNCH) return 0;
        tmk.bind_off = 0; tmk_fx("BigJumpLand", NULL); tmk_set(TMK_FALL); /* endJumpHoleLaunch: onBind, BigJumpBlur off */
        return 1;
    case TMKM_END_RAIL_DASH: if (tmk.nrv == TMK_DASHRAIL) { tmk_set(TMK_DASHRAILEND); return 1; } return 0;
    case TMKM_END_BALL_RAIL: if (tmk_ride_rail()) { tmk_set(TMK_WAIT); return 1; } return 0;
    case TMKM_END_BALL_BIND: return tmk_request_end_bind();
    }
    return 0;
}
/* FallDownBridge.cpp after Petari: a bridge that tips over about its foot (its local Z; at its X 400 x scale end when
 * it leans the other way). A jumping Tamakoro hitting it as a wall (ACTMES_BALL_DASH_WALL, its wall normal not along
 * the bridge: |x . n| >= 0.1) pushes it 0.45 degrees a frame (SE_OJ_CHAL_FD_BRIDGE_HIT, Quake 80); control: pulled
 * by sin(|angle| - 9.090277) x 0.06, kept within 0..90 degrees (bouncing x -0.5, its END SE and shakes), x 0.99 a
 * frame; once fallen past 9.09 degrees it stays down. Its collision follows its matrix. */
#define FDB_MAX 4
static struct { int row, quake, done; float ang, vel; float m0[16]; u32 lv; } fdbs[FDB_MAX];
static int nfdbs;
static void fdb_init(const Scene *s)
{
    nfdbs = 0;
    int t = scene_type(s, "FallDownBridge");
    for (u32 i = 0; t >= 0 && i < s->nobjs && nfdbs < FDB_MAX; i++)
        if ((int)s->objs[i].type == t) {
            typeof(&fdbs[0]) b = &fdbs[nfdbs++];
            memset(b, 0, sizeof *b); b->row = (int)i; b->quake = -1;
            Inst *in = row_inst((int)i);
            if (in) memcpy(b->m0, in->m, 64);
            else { float one[3] = {1, 1, 1}; mat_translate_rot_scale(b->m0, s->objs[i].pos, s->objs[i].rot, s->objs[i].scale); (void)one; }
        }
}
static void update_fdbs(void) /* control, calcAndSetBaseMtx, exeQuake */
{
    for (int k = 0; k < nfdbs; k++) {
        typeof(&fdbs[0]) b = &fdbs[k];
        float v = sinf((fabsf(b->ang) - 9.090277f) * 0.017453293f);
        if (b->ang < 0.0f) v = -v;
        b->vel += v * 0.06f;
        b->ang += b->vel;
        if (!b->done && fabsf(b->vel) >= 0.06f) b->lv = se_level(SE_OJ_LV_CHAL_FD_BRIDGE_FL, &b->m0[12], b->lv);
        int hit = 0;
        if (b->ang > 90.0f) { b->ang = 90.0f; hit = 1; }
        if (b->ang < 0.0f) { b->ang = 0.0f; hit = 1; }
        if (hit) {
            if (fabsf(b->vel) > 1.0f) { se_start(SE_OJ_CHAL_FD_BRIDGE_END, &b->m0[12]); camera_shake(SHAKE_NORMAL); } /* sShakeCameraNormalVelocity */
            else if (fabsf(b->vel) > 0.5f) camera_shake(SHAKE_WEAK);
            b->vel *= -0.5f;
            if (fabsf(b->vel) < 0.06f) { b->vel = 0.0f; if (fabsf(b->ang) > 9.090277f && !b->done) { b->done = 1; printf("fall down bridge %d: down at %.1f degrees\n", b->row, b->ang); } }
        }
        if (fabsf(b->ang) < 0.1f && fabsf(b->vel) < 0.06f) b->ang = b->vel = 0.0f;
        b->vel *= 0.99f;
        if (b->quake >= 0 && ++b->quake > 80) b->quake = -1; /* exeQuake: sQuakeTime */
        Inst *in = row_inst(b->row);
        if (!in) continue;
        float sx = vlen(v3(b->m0[0], b->m0[1], b->m0[2])), cx = b->ang >= 0.0f ? 0.0f : sx * 400.0f; /* (local: the scale's x) */
        float r = b->ang * 0.017453293f, c = cosf(r), s = sinf(r), loc[16];
        mat_identity(loc); /* T(c) x rotZ x T(-c) */
        loc[0] = c; loc[1] = s; loc[4] = -s; loc[5] = c;
        loc[12] = cx - c * cx; loc[13] = -s * cx;
        float *m0 = b->m0, w[16];
        float n0[16]; memcpy(n0, m0, 64); /* the placement's rotation without its scale */
        for (int j = 0; j < 3; j++) { float l = vlen(v3(n0[4 * j], n0[4 * j + 1], n0[4 * j + 2])); if (l > 1e-6f) for (int q = 0; q < 3; q++) n0[4 * j + q] /= l; }
        mat_mul(w, n0, loc);
        for (int j = 0; j < 3; j++) { float l = vlen(v3(m0[4 * j], m0[4 * j + 1], m0[4 * j + 2])); for (int q = 0; q < 3; q++) w[4 * j + q] *= l; }
        memcpy(in->m, w, 64);
    }
}
static int fdb_hit(int row) /* receiveOtherMsg ACTMES_BALL_DASH_WALL from the ball */
{
    for (int k = 0; k < nfdbs; k++) {
        typeof(&fdbs[0]) b = &fdbs[k];
        if (b->row != row) continue;
        if (b->done || b->quake >= 0 || !tmk.wall) return 0;
        V3 n = tmk.wn, x = vnorm(v3(b->m0[0], b->m0[1], b->m0[2]));
        tmk_normz(&n);
        float d = vdot(x, n);
        if (fabsf(d) < 0.1f) return 0;
        se_start(SE_OJ_CHAL_FD_BRIDGE_HIT, &b->m0[12]);
        b->vel += d < 0.0f ? -0.45f : 0.45f;
        b->quake = 0;
        printf("fall down bridge %d: hit, %.2f\n", row, b->vel);
        return 1;
    }
    return 0;
}
/* sendMsgToWallSensor(ACTMES_BALL_DASH_WALL / GROUND, Body): to the part its wall is on (both messages go to the wall's
 * sensor): ConcreteBlock (SimpleBreakableObj mBreakFlags 0x180), WoodBox / PunchBox (doHit), FallDownBridge (the wall one) */
static int tmk_bump_msg(int ground)
{
    if (!tmk.wall || tmk.wall_row < 0 || row_dead[tmk.wall_row]) return 0;
    Inst *in = row_inst(tmk.wall_row);
    if (!in) return 0;
    const Row *r = &sc->objs[in->row];
    const char *name = sc->types + 32 * r->type;
    if (in->breaks == 8 || (in->breaks == 1 && (!strcmp(name, "WoodBox") || !strcmp(name, "PunchBox")))) {
        if (in->breaks == 8 && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) return 0; /* setStateBreak: isValidSwitchA && !isOnSwitchA */
        printf("tamakoro: ball dash %s breaks %s row %d\n", ground ? "ground" : "wall", name, in->row);
        break_inst(in, 1);
        game_rebind();
        return 1;
    }
    if (!ground && !strcmp(name, "FallDownBridge")) return fdb_hit(in->row);
    return 0;
}

/* JumpHole.cpp / BallOpener.cpp after Petari: a hole the Tamakoro rolls into. Its body sensor (ATYPE_JUMP_HOLE, 200)
 * takes the ball's ACTMES_SPHERE_PLAYER_BINDED when the ball's centre is within 200 of its axis and 160 along it
 * (sLaunchValidHeight): the ball's velocity is kept as the bind's and zeroed. SetCenter: the ball held in the hole's
 * BindCone (400 deep, 200 wide, along the gravity at its place: bindSpereToCone, updateBindPositionAndVelocity 0.9,
 * gravity 1.2 a frame, kept within 200 - its radius, damped 0.98 -> 0.7 over steps 40-90), its velocity the move;
 * settled more than sLaunchFixTime frames (JumpHole 10, BallOpener 20) or after 180: JumpHole SetUp (SET_UP_JUMP_HOLE,
 * 45 steps) then RailMove (SHOOT_JUMP_HOLE: a parabola from the ball to its rail's end, its height by the rail's start,
 * gravity 1.2; at its end the velocity of its last step and END_JUMP_HOLE); BallOpener Open (SET_UP_JUMP_HOLE, at 45
 * END_BALL_BIND, SE_OJ_BALL_OPN_OPEN, SE_SY_TAMAKORO_GOAL, its SW_A). Their collision (JumpHole / BallOpener.kcl)
 * stays a static instance. */
enum { HO_WAIT, HO_SETCENTER, HO_SETUP, HO_RAILMOVE };
#define HO_MAX 4
typedef struct { V3 pos, dir, vertex; float height, radius, gen; } HoCone; /* BindCone */
static struct {
    int row, opener, nrv, st, bound, settled;
    V3 pos, grav, bvel, init_vel;
    HoCone cone;
    /* JumpHole's launch */
    V3 lstart, lgrav, ldir;
    float ldist, lquad, llin, ldur, ltime;
    u32 lv;
} hos[HO_MAX];
static int nhos;
static void ho_cone_set(HoCone *c, V3 pos, V3 dir) /* setPosition / setDirection: calcVertexPosition */
{
    c->pos = pos; c->dir = dir; c->vertex = vadd(pos, vmul(dir, c->height));
}
static int ho_cone_in(const HoCone *c, V3 p) /* BindCone::isInPoint */
{
    float h = vdot(c->dir, vsub(p, c->pos)), r = c->radius - h * (c->radius / c->height);
    V3 d = vsub(p, vadd(c->pos, vmul(c->dir, h)));
    return vdot(d, d) < r * r;
}
/* MR::bindSpereToCone: 1 bound; *pos, *n its result's position and normal (BindResult: (0, 0, 0) when unset) */
static int ho_bind_cone(V3 *pos, V3 *n, V3 mv, V3 sp, float sr, const HoCone *c)
{
    V3 nx = vadd(sp, mv);
    float eh = c->gen / c->radius * sr;
    V3 radial = vtangent(vsub(nx, c->pos), c->dir);
    *pos = v3(0, 0, 0); *n = v3(0, 0, 0);
    if (tmk_zero(radial)) {
        radial = vtangent(mv, c->dir);
        if (tmk_zero(radial)) {
            float h = vdot(c->dir, vsub(c->vertex, nx));
            if (-sr < h && h < 0.0f) { *pos = vadd(c->vertex, vmul(c->dir, sr)); *n = c->dir; return 1; }
            if (0.0f <= h && h < eh) { *pos = vsub(c->vertex, vmul(c->dir, eh)); *n = vmul(c->dir, -1.0f); return 1; }
            *pos = nx;
            return 0;
        }
    }
    radial = vadd(vmul(vnorm(radial), c->radius), c->pos);
    V3 gd = vmul(vsub(radial, c->vertex), 1.0f / c->gen);
    float along = vdot(gd, vsub(nx, c->vertex));
    int within = 0.0f < along && along < c->gen;
    along = clampf(along, 0.0f, c->gen);
    V3 closest = vadd(c->vertex, vmul(gd, along)), cd = vsub(nx, closest);
    float dist = vlen(cd);
    cd = dist > 0.0f ? vmul(cd, 1.0f / dist) : v3(0, 0, 0);
    int was = ho_cone_in(c, sp), is = ho_cone_in(c, nx);
    if (was) {
        float bd = vdot(c->dir, vsub(c->pos, sp)) - sr, ma = vdot(c->dir, mv);
        V3 bp = vadd(sp, vmul(mv, ma - bd));
        bp = vtangent(vsub(bp, c->pos), c->dir); /* killElement(bp - mPosition, mDirection) */
        if (vdot(bp, bp) < c->radius * c->radius && vdot(c->dir, vsub(c->vertex, nx)) < eh) {
            *pos = vsub(c->vertex, vmul(c->dir, eh)); *n = vmul(c->dir, -1.0f);
            return 1;
        }
    }
    if (within && was != is) { *n = vmul(cd, -1.0f); return 1; } /* (Petari leaves its position unset here: (0, 0, 0)) */
    if (dist < sr) { *pos = vadd(nx, vmul(cd, sr - dist)); *n = cd; return 1; }
    *pos = nx;
    return 0;
}
static void ho_bind_hole(int h) /* bindHole: the ball (tmk) moved in the cone, its velocity set to the move */
{
    typeof(&hos[0]) o = &hos[h];
    V3 prev = tmk.pos, pos, n;
    o->bvel = vadd(o->bvel, vmul(o->grav, 1.2f)); /* sGravityAcc */
    int b = ho_bind_cone(&pos, &n, o->bvel, prev, TMK_R, &o->cone);
    if (b) { float d = vdot(o->bvel, n); if (d < 0.0f) o->bvel = vadd(o->bvel, vmul(n, -d * 0.9f)); } /* updateBindPositionAndVelocity sHoleRefrecRate */
    V3 lv = vtangent(o->bvel, o->grav), lo = vtangent(vsub(pos, o->pos), o->grav);
    float lim = 200.0f - TMK_R; /* sHoleRadius */
    if (vdot(lv, lo) > 0.0f && vdot(lo, lo) > lim * lim) {
        float dist = vlen(lo);
        lo = vmul(lo, 1.0f / dist);
        pos = vsub(pos, vmul(lo, dist - lim));
        V3 tg = vsub(lv, vmul(lo, vdot(lv, lo)));
        tg = tmk_zero(tg) ? o->grav : vnorm(tg);
        o->bvel = vadd(vsub(o->bvel, lv), vmul(tg, vlen(lv)));
    }
    float r = (float)(o->st - 40) / 50.0f; /* calcNerveValue(40, 90, 0.98, 0.7) */
    r = clampf(r, 0.0f, 1.0f);
    o->bvel = vmul(o->bvel, 0.98f + (0.7f - 0.98f) * r);
    tmk.vel = vsub(pos, prev);
    V3 d = vsub(pos, prev);
    if (vdot(d, d) < 0.1f * 0.1f) o->bvel = v3(0, 0, 0);
}
static int ho_binded(int h) /* receiveOtherMsg ACTMES_SPHERE_PLAYER_BINDED from the ball's Body: 1 taken */
{
    typeof(&hos[0]) o = &hos[h];
    if (o->nrv != HO_WAIT || o->bound) return 0;
    V3 off = vsub(o->pos, tmk.pos), lat = vtangent(off, o->grav);
    V3 ax = vsub(off, lat);
    if (vdot(lat, lat) > 200.0f * 200.0f || vdot(ax, ax) > 160.0f * 160.0f) return 0; /* sHoleRadius, sLaunchValidHeight */
    o->bound = 1;
    o->init_vel = o->bvel = tmk.vel;
    tmk.vel = v3(0, 0, 0);
    return 1;
}
static void ho_parabolic(float *acc, float *vel, float mx, float end) /* MR::calcParabolicFunctionParam */
{
    float d = mx * (mx - end);
    if (d <= 0.0f) { *acc = -mx; *vel = end + mx; return; }
    if (fabsf(end) < 0.0001f) { *acc = mx * -4.0f; *vel = mx * 4.0f; return; }
    float r = sqrtf(d), t1 = (mx + r) / end, t2 = (mx - r) / end, t = 0.0f <= t1 && t1 <= 1.0f ? t1 : 0.0f <= t2 && t2 <= 1.0f ? t2 : 1.0f;
    *acc = -mx / (t * t);
    *vel = -2.0f * t * *acc;
}
static void ho_parabola(int h) /* initParabola */
{
    typeof(&hos[0]) o = &hos[h];
    const Row *r = &sc->objs[o->row];
    u32 n = 0; int closed = 0;
    const float *rail = r->path >= 0 && scene_blob_g ? find_rail(scene_blob_g, r->zone, r->path, &n, &closed) : NULL;
    V3 start = rail ? rail_at(rail, n, 0.0f) : o->pos, end = rail ? v3(rail[3 * n - 3], rail[3 * n - 2], rail[3 * n - 1]) : o->pos, p = tmk.pos;
    o->lgrav = vmul(up_at(end, vmul(o->grav, -1.0f)), -1.0f); /* calcGravityVector at its end */
    if (tmk_zero(o->lgrav)) o->lgrav = v3(0, -1, 0);
    float sh = vdot(o->lgrav, vsub(start, p)), eh = vdot(o->lgrav, vsub(end, p));
    o->ldir = vsub(vsub(end, p), vmul(o->lgrav, eh));
    o->ldist = vlen(o->ldir);
    o->ldir = o->ldist > 0.0f ? vmul(o->ldir, 1.0f / o->ldist) : v3(0, 0, 0);
    ho_parabolic(&o->lquad, &o->llin, sh, eh); /* calcParabolicFunctionParam */
    o->lstart = p;
    o->ldur = sqrtf(fabsf(2.0f * o->lquad / 1.2f));
}
static void ho_set(int h, int n) { hos[h].nrv = n; hos[h].st = -1; }
static void update_holes(void) /* JumpHole / BallOpener: control, their nerves */
{
    for (int h = 0; h < nhos; h++) {
        typeof(&hos[0]) o = &hos[h];
        if (!row_shown(o->row)) continue;
        int st = ++o->st;
        switch (o->nrv) {
        case HO_WAIT:
            if (o->bound) { ho_bind_hole(h); ho_set(h, HO_SETCENTER); }
            break;
        case HO_SETCENTER:
            if (st == 0) o->settled = 0;
            ho_bind_hole(h);
            if (vdot(o->bvel, o->bvel) < 0.0001f) o->settled++; else o->settled = 0;
            if (o->opener && o->settled == 1) camera_shake(SHAKE_WEAK);
            o->lv = se_level(o->opener ? SE_OJ_LV_BALL_OPN_SETTING : SE_OJ_LV_JUMP_HOLE_SETTING, &o->pos.x, o->lv);
            if (o->settled > (o->opener ? 20 : 10) || st > 180) { /* sLaunchFixTime, sForceSetCenterTime */
                ho_set(h, HO_SETUP);
                if (!o->opener) o->bvel = v3(0, 0, 0);
            }
            break;
        case HO_SETUP: /* JumpHole SetUp / BallOpener Open */
            if (st == 0) tmk_msg(TMKM_SET_UP_JUMP_HOLE);
            if (o->opener) {
                if (st == 45) { /* sSetUpTime */
                    tmk.vel = v3(0, 0, 0);
                    tmk_msg(TMKM_END_BALL_BIND);
                    o->bound = 0;
                    se_start(SE_OJ_BALL_OPN_OPEN, &o->pos.x); se_start(SE_SY_TAMAKORO_GOAL, NULL);
                    if (sc->objs[o->row].sw[SW_A] >= 0) sw_set(&sc->objs[o->row], SW_A, 1);
                    printf("ball opener %d: open\n", o->row);
                    ho_set(h, HO_WAIT);
                }
            } else if (st > 45) ho_set(h, HO_RAILMOVE);
            break;
        case HO_RAILMOVE: {
            if (st == 0) {
                o->ltime = 0.0f;
                ho_parabola(h);
                camera_shake(SHAKE_NORMAL);
                se_start(SE_OJ_JUMP_HOLE_FLIP, &o->pos.x); se_start(SE_PV_JUMP_JOY, &mario->pos.x); se_start(SE_PM_JUMP_LONG, &mario->pos.x);
                tmk_msg(TMKM_SHOOT_JUMP_HOLE);
            }
            float hz = o->ldist * o->ltime, vt = o->ltime * (o->llin + o->lquad * o->ltime);
            V3 p = vadd(vadd(o->lstart, vmul(o->lgrav, vt)), vmul(o->ldir, hz)), prev = tmk.pos;
            tmk.pos = p;
            o->ltime += 1.0f / o->ldur;
            if (o->ltime >= 1.0f) {
                tmk.vel = vsub(p, prev);
                tmk_msg(TMKM_END_JUMP_HOLE);
                o->bound = 0;
                printf("jump hole %d: launched to %.0f %.0f %.0f\n", o->row, p.x, p.y, p.z);
                ho_set(h, HO_WAIT);
            }
            break;
        }
        }
    }
}
static void ho_init(const Scene *s) /* JumpHole / BallOpener::init, initAfterPlacement */
{
    nhos = 0;
    int tj = scene_type(s, "JumpHole"), to = scene_type(s, "BallOpener");
    for (u32 i = 0; i < s->nobjs && nhos < HO_MAX; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != tj && (int)r->type != to) continue;
        typeof(&hos[0]) o = &hos[nhos++];
        memset(o, 0, sizeof *o);
        o->row = (int)i; o->opener = (int)r->type == to; o->pos = row_pos(r);
        o->grav = vmul(up_at(o->pos, v3(0, 1, 0)), -1.0f); /* calcGravityVector */
        o->cone.height = 400.0f; o->cone.radius = 200.0f; /* sHoleDepth, sHoleRadius */
        o->cone.gen = sqrtf(400.0f * 400.0f + 200.0f * 200.0f);
        ho_cone_set(&o->cone, o->pos, o->grav);
        ho_set(nhos - 1, HO_WAIT);
    }
}
static int ho_try_bind(void) /* attackSensor: isBindedJumpHole (its Body on an ATYPE_JUMP_HOLE sensor, 200) */
{
    for (int h = 0; h < nhos; h++)
        if (row_shown(hos[h].row) && vlen(vsub(tmk.pos, hos[h].pos)) < TMK_R + 200.0f && ho_binded(h)) {
            printf("tamakoro: in %s row %d\n", hos[h].opener ? "ball opener" : "jump hole", hos[h].row);
            return 1;
        }
    return 0;
}
/* BallRail.cpp / SphereRailDash.cpp after Petari: rails the Tamakoro rides. BallRail (arg0 acceleration x 1000, 1.5;
 * arg1 deceleration x 1000, 0.995): its bind sensor (100 at 100 up) follows the rail's nearest place to Mario (Wait); the
 * ball touching it, not moving away and not under it (SPHERE_PLAYER_BINDED: else stopped), is taken: SetUp (from its
 * place to 150 under the rail point over 15 steps eased in, the rider at 3 a step, a strong rumble at 15, 45 steps),
 * Run (along the rail by the gravity's pull on its direction x the acceleration, slowed by the deceleration, at least
 * 6 and only forward; the ball 150 under it), at the goal its velocity the rail's direction x the speed and
 * END_BALL_RAIL; NoBind 60 steps. SphereRailDash (arg0 200 unused, arg1 the end speed 50): its body (200 at the rail's
 * start) binds the ball, RailMove along the rail by a cubic in time (from the ball's speed along it to the end speed,
 * duration 3 x length / (speed + 2 x end)), the ball's offset from the rail's start fading to none, its side velocity
 * x 0.85 a frame; at the goal the ball there at end speed along the rail and END_RAIL_DASH. */
typedef struct { const float *p; u32 n; float len, coord, speed; int dir; } TmRail; /* RailRider: dir +1 to the end */
static float tmr_total(const float *p, u32 n)
{
    float l = 0.0f;
    for (u32 i = 0; i + 1 < n; i++) l += vlen(vsub(v3(p[3 * i + 3], p[3 * i + 4], p[3 * i + 5]), v3(p[3 * i], p[3 * i + 1], p[3 * i + 2])));
    return l;
}
static int tmr_init(TmRail *r, int row)
{
    const Row *q = &sc->objs[row];
    int closed = 0;
    r->p = q->path >= 0 && scene_blob_g ? find_rail(scene_blob_g, q->zone, q->path, &r->n, &closed) : NULL;
    if (!r->p || r->n < 2) return 0;
    r->len = tmr_total(r->p, r->n); r->coord = 0.0f; r->speed = 0.0f; r->dir = 1;
    return 1;
}
static V3 tmr_pos(const TmRail *r) { return rail_at(r->p, r->n, r->coord); }
static V3 tmr_dir(const TmRail *r) /* getRailDirection (the way it goes) */
{
    float a = r->coord, b = r->coord + 1.0f;
    if (b > r->len) { a = r->len - 1.0f; b = r->len; }
    V3 d = vsub(rail_at(r->p, r->n, b), rail_at(r->p, r->n, a));
    d = vlen(d) > 1e-6f ? vnorm(d) : v3(0, 0, 1);
    return r->dir > 0 ? d : vmul(d, -1.0f);
}
static void tmr_move(TmRail *r) { r->coord = clampf(r->coord + r->dir * r->speed, 0.0f, r->len); } /* moveRailRider */
static int tmr_goal(const TmRail *r) { return r->dir > 0 ? r->coord >= r->len : r->coord <= 0.0f; } /* isRailReachedGoal */
enum { TMRB_WAIT, TMRB_SETUP, TMRB_RUN, TMRB_NOBIND };
enum { TMRD_WAIT, TMRD_RAILMOVE };
#define TMR_MAX 8
static struct {
    int row, dash, nrv, st, bound;
    TmRail r;
    float acc, dec, end_speed, b0, b4, b8;
    int bc;
    V3 pos, grav, p94, off, side;
} tmrs[TMR_MAX];
static int ntmrs;
static void tmr_set(int i, int n) { tmrs[i].nrv = n; tmrs[i].st = -1; }
static int tmr_binded(int i) /* receiveOtherMsg ACTMES_SPHERE_PLAYER_BINDED: 1 taken */
{
    typeof(&tmrs[0]) o = &tmrs[i];
    if (o->dash) { o->bound = 1; return 1; }
    if (o->nrv != TMRB_WAIT) return 0;
    V3 v = tmk.vel, d = vsub(tmk.pos, vadd(o->pos, vmul(o->grav, -100.0f))); /* its sensor: 100 up */
    if (vdot(d, v) > 0.0f) return 0;
    if (vdot(o->grav, d) > 0.0f) { tmk.vel = v3(0, 0, 0); return 0; }
    o->bound = 1;
    return 1;
}
static V3 tmr_sensor(int i) { return tmrs[i].dash ? tmrs[i].pos : vadd(tmrs[i].pos, vmul(tmrs[i].grav, -100.0f)); }
static float tmr_radius(int i) { return tmrs[i].dash ? 200.0f : 100.0f; }
static void update_tmrs(void)
{
    for (int i = 0; i < ntmrs; i++) {
        typeof(&tmrs[0]) o = &tmrs[i];
        int st = ++o->st;
        if (o->dash) {
            if (o->nrv == TMRD_WAIT) {
                if (!o->bound) continue;
                o->r.coord = 0.0f; o->r.dir = 1;
                o->off = vsub(tmk.pos, tmr_pos(&o->r));
                V3 d = tmr_dir(&o->r), v = tmk.vel;
                float sp = vdot(d, v);
                if (sp < 0.0f) { sp = 0.0f; o->side = v; } else o->side = vsub(v, vmul(d, sp));
                float e = o->end_speed, dur = 3.0f * o->r.len / (sp + 2.0f * e), acc = 2.0f * (e - sp) / dur;
                o->b0 = -acc / (2.0f * dur) / 3.0f; o->b4 = acc / 2.0f; o->b8 = sp;
                o->bc = (int)(dur + (dur - (int)dur > 0.0001f ? 1.0f : 0.0f));
                if (o->bc < 0) o->bc = 0;
                tmr_set(i, TMRD_RAILMOVE);
                st = ++o->st;
            }
            if (o->nrv == TMRD_RAILMOVE) {
                if (st == 0) tmk.vel = v3(0, 0, 0);
                if (st + 1 >= o->bc) o->r.coord = o->r.len;
                else { float t = (float)(st + 1); o->r.coord = clampf(t * (o->b8 + t * (o->b4 + o->b0 * t)), 0.0f, o->r.len); }
                float prog = o->r.coord / o->r.len;
                o->off = vadd(o->off, o->side);
                o->side = vmul(o->side, 0.85f);
                V3 p = vadd(tmr_pos(&o->r), vmul(o->off, 1.0f - prog)); /* (multInLine2: x (1 - progress)) */
                tmk.vel = vsub(p, tmk.pos);
                if (tmr_goal(&o->r)) {
                    tmk.pos = p;
                    tmk.vel = vmul(tmr_dir(&o->r), o->end_speed);
                    tmk_msg(TMKM_END_RAIL_DASH);
                    o->bound = 0;
                    printf("sphere rail dash %d: end at %.0f %.0f %.0f\n", o->row, p.x, p.y, p.z);
                    tmr_set(i, TMRD_WAIT);
                }
            }
            continue;
        }
        switch (o->nrv) {
        case TMRB_WAIT:
            o->r.coord = w_rail_nearest(o->r.p, o->r.n, mario->pos); /* moveCoordToNearestPos */
            o->pos = tmr_pos(&o->r);
            if (o->bound) tmr_set(i, TMRB_SETUP);
            break;
        case TMRB_SETUP: {
            if (st == 0) { o->p94 = tmk.pos; tmk.vel = v3(0, 0, 0); o->r.speed = 0.0f; }
            o->r.coord = clampf(o->r.coord + 3.0f * o->r.dir, 0.0f, o->r.len); /* moveCoord 3 */
            o->pos = tmr_pos(&o->r);
            V3 to = vadd(o->pos, vmul(o->grav, -TMK_R)), p;
            if (st < 15) { float t = ease_in_f((float)st / 15.0f); p = vadd(vmul(o->p94, 1.0f - t), vmul(to, t)); } /* calcNerveEaseInRate */
            else p = to;
            if (st == 15) pad_rumble(RUMBLE_STRONG);
            tmk.vel = vsub(p, tmk.pos);
            if (st > 45) tmr_set(i, TMRB_RUN);
            break;
        }
        case TMRB_RUN: {
            if (st == 0) o->r.speed = 0.0f;
            V3 d = tmr_dir(&o->r);
            float a = vdot(vmul(o->grav, o->acc), d), sp = o->r.speed + a; /* accelerateRailCoordSpeed */
            if (sp < 0.0f) { sp = -sp; o->r.dir = -o->r.dir; }
            o->r.speed = sp * o->dec; /* slowDownRailCoordSpeed */
            if (o->r.dir < 0 || o->r.speed < 6.0f) { o->r.dir = 1; o->r.speed = 6.0f; }
            tmr_move(&o->r);
            o->pos = tmr_pos(&o->r);
            tmk.vel = vsub(vadd(o->pos, vmul(o->grav, -TMK_R)), tmk.pos);
            if (tmr_goal(&o->r)) {
                tmk.vel = vmul(tmr_dir(&o->r), o->r.speed);
                tmk_msg(TMKM_END_BALL_RAIL);
                o->bound = 0;
                printf("ball rail %d: end at %.0f %.0f %.0f, speed %.1f\n", o->row, o->pos.x, o->pos.y, o->pos.z, o->r.speed);
                tmr_set(i, TMRB_NOBIND);
            }
            break;
        }
        case TMRB_NOBIND: if (st > 60) tmr_set(i, TMRB_WAIT); break;
        }
    }
}
static void tmr_init_all(const Scene *s) /* BallRail / SphereRailDash::init */
{
    ntmrs = 0;
    int tb = scene_type(s, "BallRail"), td_ = scene_type(s, "SphereRailDash");
    for (u32 i = 0; i < s->nobjs && ntmrs < TMR_MAX; i++) {
        const Row *q = &s->objs[i];
        if ((int)q->type != tb && (int)q->type != td_) continue;
        typeof(&tmrs[0]) o = &tmrs[ntmrs];
        memset(o, 0, sizeof *o);
        o->row = (int)i; o->dash = (int)q->type == td_;
        if (!tmr_init(&o->r, (int)i)) { printf("tamakoro: rail row %d without its rail\n", (int)i); continue; }
        o->pos = tmr_pos(&o->r);
        o->grav = vmul(up_at(o->pos, v3(0, 1, 0)), -1.0f); /* onCalcGravity */
        o->acc = q->arg[0] != -1 ? q->arg[0] / 1000.0f : 1.5f; /* getJMapInfoArg0NoInit mAcceleration / 1000 */
        o->dec = q->arg[1] != -1 ? q->arg[1] / 1000.0f : 0.995f;
        o->end_speed = q->arg[1] != -1 ? (float)q->arg[1] : 50.0f; /* SphereRailDash arg1 _A0 */
        tmr_set(ntmrs, o->dash ? TMRD_WAIT : TMRB_WAIT);
        ntmrs++;
    }
}
static int tmr_try_bind(int dash) /* attackSensor: isBindedSphereDash / isBindedBallRail */
{
    for (int i = 0; i < ntmrs; i++)
        if (tmrs[i].dash == dash && vlen(vsub(tmk.pos, tmr_sensor(i))) < TMK_R + tmr_radius(i) && tmr_binded(i)) {
            printf("tamakoro: on %s row %d\n", dash ? "sphere rail dash" : "ball rail", tmrs[i].row);
            return 1;
        }
    return 0;
}
static void tmk_control(void) /* control */
{
    if (tmk.ground || tmk_ride_rail() || tmk.nrv == TMK_DASHRAIL || tmk.nrv == TMK_JUMPHOLE) { /* updateMoment: isGroundMomentBall */
        V3 up = tmk.ground ? tmk.gn : vmul(tmk.grav, -1.0f);
        V3 mo = tmk_moment_roll(tmk.vel, up);
        tmk.moment = vadd(vmul(tmk.moment, 0.75f), vmul(mo, 0.25f));
        float mag = vlen(tmk.moment); /* startRotateLevelSound */
        if (mag > 0.001f) {
            tmk.lv = se_level(tmk.nrv == TMK_RIDERAIL ? SE_SM_LV_IRONSPH_ROLL_RAIL : SE_SM_LV_IRONSPH_ROLL1, &tmk.pos.x, tmk.lv);
            int lvl = (int)(mag * 10000.0f / 6.2831855f); /* modifySe_Takezawa: linerMax(level, 100, 1) */
            if (tmk.lv) se_volpitch(tmk.lv, lvl <= 0 ? 0.0f : lvl >= 100 ? 1.0f : lvl / 100.0f, 1.0f);
        }
    } else tmk.moment = vmul(tmk.moment, 0.98f);
    tmk_moment_rot();
    if (tmk.ground) tmk.air = 0; else tmk.air++; /* updateAirTime */
    V3 up = vmul(tmk.grav, -1.0f); /* updateRingUpVec */
    if (!tmk_zero(tmk.accel_dir)) {
        V3 v = vadd(vmul(tmk.accel_dir, tmk.accel_rate), vmul(tmk.grav, -1.0f));
        tmk_normz(&v);
        up = tmk_turn_vec_deg(up, v, 45.0f); /* sRingMaxDegree */
    }
    tmk.ring_up = tmk_turn_vec_deg(tmk.ring_up, up, 4.0f); /* sRingMaxAngleSpeed */
    if (tmk.bgm_steps >= 0) tmk.bgm_steps++; /* AudTamakoroBgmCtrl::control (see the head comment) */
    tmk.ctrl_off = 0;
}
static void tmk_bind_move(void) /* LiveActor::movement's binder: the move against the map */
{
    if (tmk.bind_off) { tmk.pos = vadd(tmk.pos, tmk.vel); tmk.ground = tmk.wall = 0; tmk.fix = v3(0, 0, 0); return; }
    V3 to = vadd(tmk.pos, tmk.vel), np = m_bind(tmk.pos, tmk.vel, TMK_R);
    tmk.fix = b_fix; (void)to;
    int fl, wl;
    bind_contacts(vmul(tmk.grav, -1.0f), &fl, &wl);
    tmk.ground = fl >= 0; tmk.wall = wl >= 0;
    if (tmk.ground) { tmk.gn = tri_n(&b_tri[fl]); tmk.gp = vsub(np, vmul(tmk.gn, TMK_R)); }
    tmk.wall_row = -1;
    if (tmk.wall) { tmk.wn = tri_n(&b_tri[wl]); tmk.wp = vsub(np, vmul(tmk.wn, TMK_R)); if (b_tri[wl].inst >= 0 && b_tri[wl].inst < ninsts) tmk.wall_row = insts[b_tri[wl].inst].row; }
    tmk.pos = np;
}
static void tmk_update_mario_mtx(void) /* updateBindActorMatrix (ACTMES_UPDATE_BASEMTX) */
{
    if (tmk_use_offset()) {
        tmk.mario_pos = vadd(tmk.pos, vmul(tmk.dir_mario, TMK_R));
        if (tmk.off > 0.0f) tmk.mario_pos = vadd(tmk.mario_pos, vmul(tmk.grav, -tmk.off));
    }
}

/* MarioActor's side: AutoRushBegin on its Bind sensor (requestBind) */
static int tmk_try(Player *m)
{
    if (tmk.row < 0 || tmk.nrv != TMK_STANDBYBIND || !tmk.bind_sensor || !row_shown(tmk.row)) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    if (vlen(vsub(c, tmk.pos)) >= TMK_R + 15.0f + HIT_BODY_R) return 0;
    V3 to = vsub(c, tmk.pos);
    tmk_normz(&to);
    float cp = vdot(to, vmul(tmk.grav, -1.0f));
    if (cp < cosf(60.0f * 0.017453293f)) return 0; /* sBindableUpperDegree */
    if (!mode_rush("Tamakoro")) return 0;
    tmk.mz = m->face; tmk.my = m->up; /* makeQuatRotateDegree(getPlayerRotate) */
    tmk.mario_pos = m->pos;
    tmk.mario_front = vtangent(tmk.mz, vnorm(tmk.grav));
    if (tmk_normz(&tmk.mario_front)) tmk.mario_front = tmk_vert_zx(tmk.grav);
    tmk.bind_req = tmk.mario_pos;
    tmk.upper_y = 0; tmk.accel_rate = 0; tmk.off = 0; tmk.off_v = 0;
    tmk.accel_dir = v3(0, 0, 0); tmk.vel = v3(0, 0, 0);
    tmk.bind_sensor = 0; /* invalidateHitSensor Bind */
    V3 vel = vadd(m->hvel, vmul(m->up, m->vup));
    tmk_normz(&vel);
    float cv = -vdot(to, vel);
    if (cv > cosf(40.0f * 0.017453293f) && cp > cosf(35.0f * 0.017453293f)) tmk_set(TMK_BINDSTARTLAND); /* sLandStartVelocityDegree, sLandStartPositionDegree */
    else {
        tmk_set(TMK_BINDSTART);
        V3 d = vtangent(vsub(tmk.pos, tmk.mario_pos), vnorm(tmk.grav));
        if (!tmk_normz(&d)) tmk.mario_front = vdot(d, tmk.mario_front) > 0.0f ? d : vmul(d, -1.0f);
    }
    if (!tmk_need_tutorial()) bgm_stop(40);
    m->hip = 0; m->spin_timer = 0;
    printf("tamakoro: Mario bound (nrv %d)\n", tmk.nrv);
    return 1;
}
static int tmk_hold(Player *m) /* Mario bound: setPlayerBaseMtx(mMarioRotateQuat at mMarioPos) */
{
    if (!tmk_riding()) return 0;
    tmk_update_mario_mtx();
    m->pos = tmk.mario_pos; m->up = tmk.my; m->face = tmk.mz;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    if (tmk.after[0] && mario_anim && !mario_anim->loop && mario_anim->done) { /* changeAnimationE's second BCK */
        char n[24];
        int mode = 0;
        snprintf(n, sizeof n, "%s", tmk.after);
        tmk.after[0] = 0;
        anim_clip_info(n, &mode);
        mario_play(n, 1.0f, mode == 2 || mode == 4);
    }
    return 1;
}
static int tmk_enemy_attack(int msg, V3 from) /* MarioActor::receiveMsgEnemyAttack while bound: to the ball's sensor */
{
    if (msg != ATK_EXPLOSION || !tmk_enemy_attack_on()) return 0; /* receiveMsgEnemyAttack: isMsgExplosionAttack */
    pad_rumble(RUMBLE_STRONG);
    V3 d = vtangent(vsub(tmk.pos, from), vnorm(tmk.grav)); /* setVelocitySeparateHV 20 / 40 */
    tmk_normz(&d);
    tmk.vel = vsub(vmul(d, 20.0f), vmul(tmk.grav, 40.0f));
    se_start(SE_PV_UPSET, &mario->pos.x);
    return 1;
}
static int tmk_offence = 0; /* mario_offence answering for the ball's Body sensor (sendMsgPlayerTrample) */
static int tmk_trample_on(V3 c, float r) /* attackSensor: an enemy's sensor on its Body while it may attack */
{
    if (!tmk_riding() || !tmk_enemy_attack_on() || vlen(vsub(c, tmk.pos)) >= TMK_R + r) return 0;
    tmk_offence = 1;
    return 1;
}
static void tmk_trample_reaction(void) /* sendMsgPlayerTrample taken: the stamp's bounce */
{
    float dv = vdot(tmk.vel, tmk.grav);
    if (dv < 15.0f) { /* sStampReactionInvalidSpeed */
        dv = -30.0f - dv; /* sStampReactionV */
        tmk.vel = vadd(tmk.vel, vmul(tmk.grav, dv));
        pad_rumble(RUMBLE_WEAK);
        camera_shake(SHAKE_NORMAL_WEAK);
    }
}
static V3 tmk_body(void) { return tmk.pos; }
static float tmk_reach(void) { return tmk_riding() ? TMK_R + 55.0f : 0.0f; } /* its Body sensor's tryGetItem (a coin's 55) */
static int tmk_shot(V3 p) { return tmk.row >= 0 && tmk.sensors && vlen(vsub(p, tmk.pos)) <= TMK_R; } /* isMsgStarPieceReflect */

/* TamakoroTutorial */
static void tmt_action(const char *name) /* startAction on its SignBoard */
{
    char n[48];
    snprintf(n, sizeof n, "tamakorotutorial/%s", name);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (tmk.ta && tmk.ta->skin) { tmk.ta->clip_id = -1; anim_start(tmk.ta, n, 0); }
}
static void tmt_revived(void) /* RevivalFront, setBckFrameAndStop(getBckFrameMax) */
{
    int mode;
    tmt_action("RevivalFront");
    if (tmk.ta && tmk.ta->skin) { tmk.ta->frame = (float)anim_clip_info("tamakorotutorial/revivalfront", &mode); tmk.ta->speed = 0.0f; }
}
static int tmt_action_end(void) { return !tmk.ta || !tmk.ta->skin || tmk.ta->clip_id < 0 || tmk.ta->done; } /* isActionEnd */
static void tmk_tutorial_request_start(void) { if (tmk.talk >= 0) talk_forward_node(tmk.talk); tmt_set(TMT_WAITRAISETALK); } /* requestStart */
static void tmk_tutorial_request_end(void) { if (tmk.trow >= 0) tmt_set(TMT_DIRECTRIDE); } /* requestEnd */
static void tmk_start_ride(void) { if (tmk.nrv == TMK_STANDBYTUTORIAL) tmk_set(TMK_STANDBYBIND); } /* startRide */
static void tmk_request_tutorial_end(void) /* requestTutorialEnd */
{
    if (tmk.nrv != TMK_TUTORIAL) return;
    tmk.vel = v3(0, 0, 0);
    tmk_bgm_start();
    tmk_set(TMK_WAIT);
}
static void tmt_timer_sound(int step) /* startTimerSound */
{
    if (step == 20) se_start(SE_SY_SURF_TUTORIAL_TIMER, NULL);
    if (step == 40) se_start(SE_SY_SURF_TUTORIAL_TIMER2, NULL);
}
static void tmt_exe(void)
{
    if (tmk.trow < 0) return;
    int st = ++tmk.tst;
    const int raised = 1; /* isNearAngleDegree(hRaiseAcc, mPadAccel, hRaiseCheckDegree): see the head comment */
    float m[16];
    basis_matrix(m, tmk.tpos, tmk.tup, tmk.tfront, 0);
    talk_place(tmk.talk, m); talk_place(tmk.talk_auto, m);
    switch (tmk.tnrv) {
    case TMT_FIRST:
        if (st == 0) { tmk.push_on = 1; tmt_revived(); }
        if (!gd_flag_is_on("タマコロチュートリアル")) {
            float dt = vlen(vsub(mario->pos, tmk.tpos)), db = vlen(vsub(mario->pos, tmk.pos));
            if (mario->grounded && starter < 0 && (dt < 400.0f || db < 400.0f)) tmt_set(TMT_FIRSTFORCETALK); /* hDistToStartTutorial */
        } else {
            if (st == 0) { tmk.push_on = 0; tmk_start_ride(); }
            if (talk_near_at_end(tmk.talk_auto) && tk_yesno == 1) { /* tryTalkSelectLeft */
                talk_reset_node(tmk.talk); talk_forward_node(tmk.talk);
                tmt_set(TMT_WAITRIDE);
            }
        }
        break;
    case TMT_DENYTALK:
        if (talk_force_at_end(tmk.talk_auto, 1)) { talk_reset_node(tmk.talk_auto); tmt_set(TMT_FIRST); }
        break;
    case TMT_FIRSTFORCETALK:
        if (st == 0) talk_reset_node(tmk.talk);
        if (talk_force_at_end(tmk.talk, 1)) tmt_set(TMT_WAITRIDETALK);
        break;
    case TMT_WAITRIDETALK:
        if (st == 0) talk_forward_node(tmk.talk);
        if (talk_force_at_end(tmk.talk, 1)) { tmk.push_on = 0; tmk_start_ride(); tmt_set(TMT_WAITRIDE); }
        break;
    case TMT_WAITRIDE:
        if (st == 0) talk_forward_node(tmk.talk);
        talk_near(tmk.talk);
        break;
    case TMT_WAITRAISETALK:
        talk_force(tmk.talk, 0); /* tryTalkForceWithoutDemo */
        if (st > 90 && raised) { se_start(SE_SY_SURF_TUTORIAL_OK, NULL); tmt_set(TMT_WAITRAISESTABLE); }
        break;
    case TMT_WAITRAISESTABLE:
        talk_force_at_end(tmk.talk, 0);
        if (!raised) { tmt_set(TMT_WAITRAISETALK); se_start(SE_SY_SURF_TUTORIAL_NG, NULL); }
        else {
            tmt_timer_sound(st);
            if (st > 60) { se_start(SE_SY_SURF_TUTORIAL_GONEXT, NULL); tmt_set(TMT_RAISEOK); }
        }
        break;
    case TMT_RAISEOK:
        if (st > 60) tmt_set(TMT_RAISEOKTALK);
        break;
    case TMT_RAISEOKTALK:
        if (st == 0) talk_forward_node(tmk.talk);
        if (talk_force_at_end(tmk.talk, 0)) tmt_set(TMT_FINALTALK);
        break;
    case TMT_FINALTALK:
        if (st == 0) talk_forward_node(tmk.talk);
        if (talk_force_at_end(tmk.talk, 0)) {
            if (tmk.talk >= 0 && tk_next_node(&talkers[tmk.talk]) != TK_NONE) tmt_set(TMT_FINALTALK); /* isExistNextNode */
            else { tmk_request_tutorial_end(); gd_flag_try_on("タマコロチュートリアル"); tmt_set(TMT_WAIT); }
        }
        break;
    case TMT_WAIT: case TMT_DIRECTRIDE:
        if (tmk.tnrv == TMT_DIRECTRIDE && st == 0) { tmk_request_tutorial_end(); gd_flag_try_on("タマコロチュートリアル"); tmt_set(TMT_WAIT); }
        else if (tmk.a5 && talk_near_at_end(tmk.talk)) { /* tryForwardNode */
            if (tmk.talk >= 0 && tk_next_node(&talkers[tmk.talk]) != TK_NONE) talk_forward_node(tmk.talk);
        }
        break;
    case TMT_TRAMPLEFRONT: case TMT_TRAMPLEBACK:
        if (st == 0) { se_start(SE_OJ_SIGN_BOARD_TRAMP_BALL, &tmk.tpos.x); tmt_action(tmk.tnrv == TMT_TRAMPLEFRONT ? "DownFront" : "DownBack"); }
        if (tmt_action_end() && st > 60 && !tmk.a4) tmt_set(tmk.tnrv == TMT_TRAMPLEFRONT ? TMT_RECOVERFRONT : TMT_RECOVERBACK);
        else tmk.a4 = 0;
        break;
    case TMT_RECOVERFRONT: case TMT_RECOVERBACK:
        if (st == 0) { se_start(SE_OJ_SIGN_BOARD_RECOVER, &tmk.tpos.x); tmt_action(tmk.tnrv == TMT_RECOVERFRONT ? "RevivalFront" : "RevivalBack"); }
        if (tmt_action_end()) tmt_set(TMT_WAIT);
        break;
    }
}
static void tmt_sensors(void) /* its body (enemy, 100 at 100 up) and push (200, updateHitSensor) sensors */
{
    if (tmk.trow < 0) return;
    V3 body = vadd(tmk.tpos, vmul(tmk.tup, 100.0f));
    int trample = tmk.tnrv == TMT_TRAMPLEFRONT || tmk.tnrv == TMT_TRAMPLEBACK;
    if (tmk.push_on && !trample) { /* push: at the ball's place raised to Mario's centre along its up */
        V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), p = vadd(tmk.pos, vmul(tmk.tup, vdot(tmk.tup, vsub(mc, tmk.pos))));
        mario_push_out(p, 200.0f);
    }
    if (!trample) mario_push_out(body, 100.0f);
    if (tmk.sensors && vlen(vsub(body, tmk.pos)) < TMK_R + 100.0f) tmk.a4 = 1; /* attackSensor: isSensorRide */
    if (tmk_riding() && tmk_enemy_attack_on() && vlen(vsub(body, tmk.pos)) < TMK_R + 100.0f && /* the ball's trample */
        (tmk.tnrv == TMT_WAIT || tmk.tnrv == TMT_RECOVERFRONT || tmk.tnrv == TMT_RECOVERBACK)) {
        tmt_set(vdot(tmk.tfront, vsub(tmk.pos, body)) > 0.0f ? TMT_TRAMPLEBACK : TMT_TRAMPLEFRONT);
        tmk_fx_hit_cls("TamakoroTutorial", "HitMarkNormal", vmul(vadd(body, tmk.pos), 0.5f), tmk.tup); /* emitEffectHitBetweenSensors */
        tmk_trample_reaction();
        return;
    }
    if (!tmk_riding()) {
        int msg = mario_offence(NULL, body, 100.0f, vmul(tmk.tup, -1.0f));
        if (msg == MSG_TRAMPLE) { se_start(SE_OJ_SIGN_BOARD_TRAMP_PLR, &tmk.tpos.x); mario_trample(); }
        else if (msg == MSG_HIPDROP) { /* sendMsgAwayJump */
            V3 a = vtangent(vsub(mario->pos, tmk.tpos), tmk.tup);
            mario->hip = 0;
            m_power_jump(mario, vadd(vmul(vlen(a) > 1e-6f ? vnorm(a) : tmk.tfront, 10.0f), vmul(mario->up, 25.0f)));
        }
    }
}

static void tmk_models(const Scene *s)
{
    tmk.md = scene_model(s, "TamakoroWithTutorial");
    tmk.tmd = scene_model(s, "TamakoroTutorial");
}
static void tmk_init(const Scene *s) /* Tamakoro::init / TamakoroTutorial::init */
{
    memset(&tmk, 0, sizeof tmk); tmk.row = tmk.trow = -1; tmk.talk = tmk.talk_auto = -1;
    int t = scene_type(s, "TamakoroWithTutorial");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        V3 x, z;
        row_axes(r, &x, &z);
        tmk.row = (int)i; tmk.pos = row_pos(r);
        tmk.ball = q_from_axes(x, vcross(z, x), z); /* makeQuatFromRotate */
        tmk.grav = vmul(up_at(tmk.pos, v3(0, 1, 0)), -1.0f);
        tmk.ring_up = v3(0, 1, 0); tmk.my = v3(0, 1, 0); tmk.mz = v3(0, 0, 1); tmk.mario_up = v3(0, 1, 0); tmk.mario_front = v3(0, 0, 1);
        tmk.sensors = 1; tmk.bind_sensor = 1; tmk.bgm_steps = -1;
        tmk_models(s);
        tmk.a = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(tmk.a, 0, sizeof(Anim));
        tmk.star_id = -1;
        int ps = scene_type(s, "PowerStar");
        for (u32 k = 0; ps >= 0 && k < s->nobjs; k++)
            if ((int)s->objs[k].type == ps && s->objs[k].arg[7] == (int)i) tmk.star_id = s->objs[k].arg[0];
        tmk_set(TMK_STANDBYBIND);
        ho_init(s);
        fdb_init(s);
        tmr_init_all(s);
        int tt = scene_type(s, "TamakoroTutorial"); /* initChildObj(mTutorial, rIter, 0): its zone's */
        for (u32 k = 0; tt >= 0 && k < s->nobjs; k++) {
            const Row *q = &s->objs[k];
            if ((int)q->type != tt || q->zone != r->zone) continue;
            V3 qx, qz;
            row_axes(q, &qx, &qz);
            tmk.trow = (int)k; tmk.tpos = row_pos(q); tmk.tup = vcross(qz, qx); tmk.tfront = qz;
            tmk.ta = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(tmk.ta, 0, sizeof(Anim));
            tmk_set(TMK_STANDBYTUTORIAL);
            if (q->arg[0] == -1) { /* omitTutorial -1: Common_TamakoroTutorial000 */
                tmk.talk = talk_new("Common_TamakoroTutorial000", -1, (int)k, 180.0f, 400.0f);
                tmt_set(TMT_FIRST); tmk.a5 = 0;
            } else {
                char lab[32];
                tmk.talk = talk_row_label((int)k, lab) ? talk_new(lab, (int)k, (int)k, 180.0f, 400.0f) : -1;
                tmt_set(TMT_WAIT); tmk.a5 = 1; tmk.push_on = 0;
                tmk_start_ride();
            }
            tmk.talk_auto = talk_new("Common_TamakoroTutorial007", -1, (int)k, 180.0f, 400.0f); /* createTalkCtrlDirectOnRootNodeAutomatic */
            if (tmk.talk_auto >= 0) talkers[tmk.talk_auto].root_auto = 1;
            break;
        }
        printf("tamakoro: row %d at %.0f %.0f %.0f, model %s, star %d, tutorial row %d (talks %d %d), bva %d\n", tmk.row, tmk.pos.x, tmk.pos.y,
               tmk.pos.z, tmk.md ? "yes" : "-", tmk.star_id, tmk.trow, tmk.talk, tmk.talk_auto, tmk.star_id > 0 && game_scenario_stars(tmk.star_id));
        break;
    }
}
static void update_tmk(void)
{
    if (tmk.row < 0 || tmk.nrv == TMK_DEAD) return;
    update_holes();
    update_fdbs();
    update_tmrs();
    if (tmk.nrv != TMK_BINDEND && !row_shown(tmk.row)) return;
    tmk.grav = vmul(up_at(tmk.pos, vmul(tmk.grav, -1.0f)), -1.0f); /* onCalcGravity */
    tmt_exe();
    tmk_exe();
    if (tmk.nrv == TMK_DEAD) return;
    tmk_control();
    tmk_bind_move();
    if (tmk_enemy_attack_on()) { /* isEnableBallBind: isBindedSphereDash, isBindedJumpHole, isBindedBallRail */
        if (tmr_try_bind(1)) tmk_set(TMK_DASHRAIL);
        else if (ho_try_bind()) tmk_set(TMK_JUMPHOLE);
        else if (tmr_try_bind(0)) tmk_set(TMK_RIDERAIL);
    }
    if (tmk.nrv == TMK_STANDBYBIND || tmk.nrv == TMK_STANDBYTUTORIAL) mario_push_out(tmk.pos, TMK_R); /* attackSensor: isEnablePushPlayer */
    tmt_sensors();
    if (tmk.ta && tmk.ta->skin && tmk.ta->clip_id >= 0) anim_update(tmk.ta);
}
static void tmk_draw(void)
{
    if (tmk.row < 0) return;
    if (tmk.trow >= 0 && tmk.tmd) { /* its SignBoard */
        float m[16];
        basis_matrix(m, tmk.tpos, tmk.tup, tmk.tfront, 0);
        use_row_light(tmk.trow, tmk.tpos, LIGHT_STRONG);
        if (tmk.tmd->skin && tmk.ta) {
            if (tmk.ta->skin != tmk.tmd->skin) { anim_bind(tmk.ta, tmk.tmd->skin); if (tmk.tnrv == TMT_FIRST) tmt_revived(); }
            if (tmk.ta->clip_id < 0) anim_pose(tmk.ta);
            draw_skin(tmk.tmd, m, tmk.ta);
        } else render_model(tmk.tmd, m, 0x80, 0x80, 0x80);
    }
    if (tmk.hidden || tmk.nrv == TMK_DEAD || !tmk.md || (tmk.nrv != TMK_BINDEND && !row_shown(tmk.row))) return;
    float b[16];
    tmk_base(b);
    use_row_light(tmk.row, tmk.pos, LIGHT_STRONG);
    if (!tmk.md->skin || !tmk.a) { render_model(tmk.md, b, 0x80, 0x80, 0x80); return; }
    if (tmk.a->skin != tmk.md->skin) anim_bind(tmk.a, tmk.md->skin);
    anim_pose(tmk.a);
    if (tmk.a->skin->njoints > 4) { /* ringMtxCallBack: the Ring joint upright, its front the tilt */
        float *w = tmk.a->world[4];
        V3 t = v3(b[0] * w[12] + b[4] * w[13] + b[8] * w[14] + b[12], b[1] * w[12] + b[5] * w[13] + b[9] * w[14] + b[13],
                  b[2] * w[12] + b[6] * w[13] + b[10] * w[14] + b[14]);
        V3 y = vnorm(tmk.ring_up), x, z;
        if (tmk_zero(tmk.accel_dir) || fabsf(vdot(vnorm(tmk.accel_dir), y)) > 0.99999f) { /* makeMtxUpNoSupport */
            int big = fabsf(y.z) >= fabsf(y.x) && fabsf(y.z) >= fabsf(y.y);
            x = vnorm(vcross(y, big ? v3(0, 1, 0) : v3(0, 0, 1)));
        } else x = vnorm(vcross(y, tmk.accel_dir)); /* makeMtxUpFront */
        z = vcross(x, y);
        V3 bx = v3(b[0], b[1], b[2]), by = v3(b[4], b[5], b[6]), bz = v3(b[8], b[9], b[10]), d = vsub(t, tmk.pos);
        V3 cols[4] = {x, y, z, d}; /* model space: the base's inverse (a rotation) on the world axes */
        for (int c = 0; c < 4; c++) { w[4 * c] = vdot(cols[c], bx); w[4 * c + 1] = vdot(cols[c], by); w[4 * c + 2] = vdot(cols[c], bz); }
    }
    anim_skin(tmk.a);
    { static char bk[40]; snprintf(bk, sizeof bk, "tamakorowithtutorial/tamakoro"); bva_key = bk; } /* setBvaFrameAndStop(hasPowerStar) */
    bva_frame = tmk.star_id > 0 && game_scenario_stars(tmk.star_id) ? 1.0f : 0.0f;
    draw_skin(tmk.md, b, tmk.a);
    if (!tmk.hidden) draw_shadow(tmk.pos, vmul(tmk.grav, -1.0f), v3(b[8], b[9], b[10]), TMK_R, TMK_R, 0.0f, TMK_R, 1000.0f); /* initShadowVolumeSphere 150 */
}
static void tmk_upper(void) /* setPlayerUpperRotateY: Spine2 turned about its X (MarioAnimator::setUpperRotateY) */
{
    if (!tmk_riding() || tmk.upper_y == 0.0f || !mario_anim || !mario_anim->skin || mario_anim->skin->njoints <= JOINT_SPINE2) return;
    anim_joint_rotx(mario_anim, JOINT_SPINE2, tmk.upper_y * 0.017453293f);
}

/* Test builds (XF=-DTMK_TEST, TamakoroExLv1Galaxy/1): Mario walked to the ball (the tutorial's talks paged by A), onto it,
 * then rolled with the stick ahead, a jump, and its END_BALL_BIND at 2000 frames (as a BallOpener would send) */
int game_test_tmk(int f)
{
    if (tmk.row < 0) return -1;
    static int bound_at = -1, ended = 0;
    test_nolost = 1;
    if (f % 30 == 0)
        printf("tk test f %d: talk %d/%d/%d/%d nrv %d st %d tut %d/%d pos %.0f %.0f %.0f vel %.1f ground %d wall %d mario %.0f %.0f %.0f clip %s life %d\n", f, td.nrv, td.st, td.st_kind, talk_active(), tmk.nrv, tmk.st,
               tmk.tnrv, tmk.tst, tmk.pos.x, tmk.pos.y, tmk.pos.z, vlen(tmk.vel), tmk.ground, tmk.wall, mario->pos.x, mario->pos.y, mario->pos.z,
               mario_anim && mario_anim->clip_id >= 0 ? sc->clips[mario_anim->clip_id].name : "-", life);
    pad_force = 0; pad_force_stick = 0;
    if (talk_active()) { pad_force = (f % 45) < 5 ? PAD_CROSS : 0; return tmk.nrv; }
    if (tmk.nrv == TMK_STANDBYTUTORIAL) { /* walk to the ball */
        V3 d = vtangent(vsub(tmk.pos, mario->pos), mario->up);
        if (vlen(d) > 300.0f) { mario->pos = vadd(mario->pos, vmul(vnorm(d), 10.0f)); }
        return tmk.nrv;
    }
    if (tmk.nrv == TMK_STANDBYBIND) { /* dropped on top */
        if (f % 120 == 0) { mario->pos = vadd(tmk.pos, vmul(tmk.grav, -(TMK_R + 120.0f))); mario->hvel = v3(0, 0, 0); mario->vup = -2.0f; mario->grounded = 0; }
        return tmk.nrv;
    }
    if (tmk.nrv == TMK_WAIT && bound_at < 0) bound_at = f; /* (the tutorial over) */
    if (bound_at >= 0 && !ended) {
        int t = f - bound_at;
        pad_force_stick = 1; pad_force_lx = t < 150 ? 0.0f : 0.3f; pad_force_ly = 0.4f;
        if (t == 120) pad_force = PAD_CROSS;
#ifdef TMK_HOLES /* the ball dropped 300 over each JumpHole in turn, then the BallOpener: its star */
        static int hole = -1, next_at = 60;
        if (hole >= 0 && hole < nhos) pad_force_stick = 0;
        static int done[HO_MAX];
        if (t >= next_at && tmk.nrv == TMK_WAIT) {
            hole = -1;
            for (int h = 0; h < nhos && hole < 0; h++) if (!done[h] && !hos[h].opener) hole = h;
            for (int h = 0; h < nhos && hole < 0; h++) if (!done[h]) hole = h;
        }
        if (hole >= 0 && !done[hole] && t >= next_at && tmk.nrv == TMK_WAIT) {
            done[hole] = 1;
            tmk.pos = vsub(hos[hole].pos, vmul(hos[hole].grav, 300.0f)); tmk.vel = v3(0, 0, 0);
            printf("tk test f %d: ball over %s row %d\n", f, hos[hole].opener ? "ball opener" : "jump hole", hos[hole].row);
            next_at = t + 120;
        }
        if (tmk.nrv == TMK_BINDEND) ended = 1;
#elif defined(TMK_DASH) /* the ball jumping at each ConcreteBlock / FallDownBridge in turn (from 450 before its -X / 650 before its +X face) */
        static int k = -1, next_at = 60;
        pad_force_stick = 0;
        if (t >= next_at && (tmk.nrv == TMK_WAIT || tmk.nrv == TMK_LAND || tmk.nrv == TMK_BUMPWALL)) {
            int tc = scene_type(sc, "ConcreteBlock"), tf = scene_type(sc, "FallDownBridge"), found = -1;
            for (u32 i = (u32)(k + 1); i < sc->nobjs && found < 0; i++) if ((int)sc->objs[i].type == tc || (int)sc->objs[i].type == tf) found = (int)i;
            if (found < 0) { ended = 1; printf("tk test f %d: END_BALL_BIND %d\n", f, tmk_msg(TMKM_END_BALL_BIND)); }
            else {
                k = found;
                const Row *r = &sc->objs[k];
                V3 x, z, c = row_pos(r);
                row_axes(r, &x, &z);
                V3 up = vmul(tmk.grav, -1.0f), h = vnorm(vtangent(x, up));
                int fdb = (int)r->type == tf;
                if (fdb) h = vmul(h, -1.0f); /* (it falls away from its +X face: hit from there) */
                tmk.pos = vadd(vadd(c, vmul(h, fdb ? -650.0f : -450.0f * r->scale[0])), vmul(up, fdb ? 900.0f : 160.0f)); /* (the bridge over the blocks by its +X face) */
                tmk.vel = vmul(h, 25.0f);
                tmk_set(TMK_JUMP);
                printf("tk test f %d: ball jumping at %s row %d\n", f, fdb ? "fall down bridge" : "concrete block", k);
                next_at = t + 150;
            }
        }
#elif defined(TMK_RAILS) /* the ball put on each SphereRailDash / BallRail start in turn, then END_BALL_BIND */
        static int rail = -1, next_at = 60;
        if (t >= next_at && tmk.nrv == TMK_WAIT) {
            if (++rail < ntmrs) {
                typeof(&tmrs[0]) o = &tmrs[rail];
                V3 st = rail_at(o->r.p, o->r.n, 0.0f);
                tmk.pos = o->dash ? st : vsub(st, vmul(o->grav, 260.0f)); tmk.vel = v3(0, 0, 0);
                printf("tk test f %d: ball at %s row %d\n", f, o->dash ? "sphere rail dash" : "ball rail", o->row);
                next_at = t + 150;
            } else { ended = 1; printf("tk test f %d: END_BALL_BIND %d\n", f, tmk_msg(TMKM_END_BALL_BIND)); }
        }
        if (rail >= 0) pad_force_stick = 0;
#else
        if (t == 420) { ended = 1; pad_force_stick = 0; printf("tk test f %d: END_BALL_BIND %d\n", f, tmk_msg(TMKM_END_BALL_BIND)); }
#endif
    }
    if (ended && tmk.nrv == TMK_DEAD && f % 20 == 0) { /* to its star */
        int ps = scene_type(sc, "PowerStar");
        for (u32 i = 0; ps >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ps && sc->objs[i].arg[7] == tmk.row) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    }
    return tmk.nrv;
}
