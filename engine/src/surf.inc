/* surf.inc (#included by game.c after race.inc: sr_*, pcs_*): Ride/SurfRay.cpp, Ride/SurfRayTutorial.cpp and
 * PenguinCoach.cpp's surfing modes (arg0 2 Behavior_Tutorial, 3 Behavior_Race) after Petari.
 * SurfRay (one a scene; at スタート位置（サーフィン） on a retry): WaitPlayer (FirstWait, its shadow's water place) till
 * Mario is within 1300, TurnToWait (FirstWaitTurn, SE_SM_SURF_RAY_TURNTO_MR), Wait. Mario touching its ride sensor
 * (260) while not rising is bound (AutoRushBegin): its SW_A on (TimerSwitch / GroupSwitchWatcher bring the RaceRail
 * in: startRaceImmediately), Wait / SurfRideLoop, then Tutorial (the coach's lecture) or Ready. Ride: calcGravity,
 * updateToWater (tryInWater's seven probes, the jump-in SEs and rumbles by its air time, the push up by the water's
 * depth and wave), updateInfo (the binder's rebounds: ground 0.3 / wall 0.8, SE_SM_SURF_RAY_HIT_WALL, its speed split
 * along its front), updateToMap (gravity 0.1 in water / 0.8, its base up blended to the ground / water / -gravity),
 * updateAccel (A +0.5, else x 0.99, at most 40; Ripple / SwimSplash / RunDashSplash by speed), updateSound (MOVE0-3,
 * WIND, CURVE levels), updateRotate (the tilt's steer: twist and roll, Mario's three tracks blended by it, its front
 * turned about its up by -rotZ x 0.00028). Nerves RideAccel / RideFree (and their Twist starts on a quick tilt
 * change), RideJump / RideJumpHigh (a swing on the ground, in water or within 15 frames of air: 15 + its speed's part,
 * over 23 high), RideFall (45 frames in the air), RideLand (SE_SM_SURF_RAY_LANDW), Tutorial (the lectures' clips by
 * mLectureIdx), WipeOut / WipeIn (past ワープ位置's z in the tutorial area: the circle wipe, back at スタート位置) and
 * Ready (the race's count). Mario on it at its Center joint + 55 up, 12 ahead (updateRiderMtx); its ride sensor takes
 * the items (tryGetItem). The race's messages: RACE_READY (Ready), RACE_START (RideFree), RACE_RESET (Mario let go
 * in his wait, the ray killed). SurfRayTutorial (the coach's): AllStart (TUTORIAL_START: Mario put on the ray, the
 * talk's node 0), Straight / TurnLeft / TurnRight (Start: the node's talk 150 frames; Failure: 10 frames of the pose
 * go on; Upkeep: 90 more with the timer SEs, back on 10 off it; Success: its talk), AllEnd (TUTORIAL_PASS).
 * PenguinCoach 2 / 3: Prep (its talk; at the end the fade: the tutorial's Yes (チュートリアル camera, コーチチュートリアル
 * 位置), No: いいえ選択移動位置, or with the flag on the omit (TUTORIAL_OMIT); the race coach's Yes: Mario at スタート位置
 * （サーフィン）), Tutorial (onGameEventFlag サーフィンチュートリアル at its end), Race, Praise (resetAudience: コーチレース終了
 * 後位置, its forced talk under the 会話 camera once the wipe is open), Wait; branchFunc 2 the flag, 3 the race done,
 * 4 / 5 its time under 90 / 180 s, eventFunc 2 forceKillPlayerByGroundRace.
 * ponytail: the remote's accelerometer (getCorePadAcceleration) is the left stick: x its x, y its y, z -sqrt(1 - x^2 -
 * y^2) (SurfRay.cpp:741 updateRotate, SurfRayTutorial.cpp:313-341); isPadSwing is the square button (as Mario's spin);
 * no SurfingGuidance (TiltGuidance: the remote's picture, SurfRayTutorial.cpp:68), no ActorJointCtrl (its tail's
 * dynamicjointctrl.bcsv), no BTK rate (control's setBtkRate) / Blink BTP, no ShadowSurfaceOval on the water
 * (initShadowFromCSV: its alpha kept, not drawn), the level SEs' parameter as their volume / 1000 (no per-sound
 * AudSoundObject modifier), no star pointer modes, startTalkingSequence (the fade's talk hold) */
enum { SRN_WAITPLAYER, SRN_TURNTOWAIT, SRN_WAIT, SRN_RIDEACCEL, SRN_RIDEACCELTWIST, SRN_RIDEFREE, SRN_RIDEFREETWIST, SRN_RIDEJUMP,
       SRN_RIDEJUMPHIGH, SRN_RIDEFALL, SRN_RIDELAND, SRN_TUTORIAL, SRN_WIPEOUT, SRN_WIPEIN, SRN_READY };
enum { SRM_TUTORIAL_START, SRM_TUTORIAL_NEXT, SRM_TUTORIAL_PREV, SRM_TUTORIAL_PASS, SRM_TUTORIAL_OMIT };
#define SR_CENTER 2 /* SurfRay.bdl's Center joint */
static struct {
    const Scene *scene;
    int row, nrv, step, next, dead, rider, in_water, air, in_tut_area, in_tut, lecture;
    int ground, wall;
    float speed, ortho_speed, steer_rate, steer_accel, tilt, shadow_a, rotz, twist[8];
    V3 pos, vel, ortho, front, up, side, base_up, base_side, gnorm, warp, wnorm, grav, gn, wn, shadow;
    int has_warp;
    WInfo wi;
    float em[16];      /* mEffectHostMtx (Ripple) */
    u32 lv[8], fx_on; char fxkey[3];  /* fx_on: 1 Ripple, 2 SwimSplash, 4 RunDashSplash (tryEmitEffect / tryDeleteEffect) */
} sr = {.row = -1};
static void sr_set(int n) { sr.next = n; }
static Inst *sr_inst(void) { return sr.row >= 0 ? row_inst(sr.row) : NULL; }
static int sr_riding(void) { return sr.scene == sc && sr.row >= 0 && sr.rider && !sr.dead; } /* MR::isPlayerInBind with the ray its rush host */
static float sr_liner(float x, float lo, float hi, float a, float b) /* MR::getLinerValueFromMinMax */
{
    return a + (b - a) * ((clampf(x, lo, hi) - lo) / (hi - lo));
}
static V3 sr_accel(void) /* getCorePadAcceleration (see the head comment) */
{
    float x = pad_lx(), y = pad_ly(), r = x * x + y * y;
    if (r > 1.0f) { float s = 1.0f / sqrtf(r); x *= s; y *= s; r = 1.0f; }
    return v3(x, y, -sqrtf(1.0f - r));
}
static int sr_a(void) { return pad_held(PAD_CROSS); } /* testCorePadButtonA */
static void sr_se(u32 id, float lvl) /* startSound(this, name[, lvl]) */
{
    u32 h = se_start(id, &sr.pos.x);
    if (h && lvl >= 0.0f) se_volpitch(h, clampf(lvl / 1000.0f, 0.0f, 1.0f), 1.0f);
}
static void sr_lvse(int k, u32 id, float lvl) /* startLevelSound(this, name, lvl) */
{
    sr.lv[k] = se_level(id, &sr.pos.x, sr.lv[k]);
    if (sr.lv[k]) se_volpitch(sr.lv[k], clampf(lvl / 1000.0f, 0.0f, 1.0f), 1.0f);
}
static void sr_bck1(const char *clip) /* startBck: its BCK's own loop mode */
{
    Inst *in = sr_inst();
    if (!in) return;
    char n[48]; int mode = 0;
    snprintf(n, sizeof n, "surfray/%s", clip);
    anim_clip_info(n, &mode);
    obj_play(in, clip, mode == 2 || mode == 4, 6);
}
static int sr_bck_stopped(void) { const Inst *in = sr_inst(); return !in || !in->anim || (!in->anim->loop && in->anim->done); } /* isBckStopped */
static void sr_mbck(const char *clip) /* startBckPlayer(name): its BCK as it loops */
{
    char n[32]; int mode = 0;
    snprintf(n, sizeof n, "%s", clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    anim_clip_info(n, &mode);
    mario_play(n, 1.0f, mode == 2 || mode == 4);
}
static void sr_mbckj(const char *jp) { m_anim(jp); } /* startBckPlayerJ */
static int sr_mstopped(void) { return !mario_anim || (!mario_anim->loop && mario_anim->done); } /* isBckStoppedPlayer */
static int sr_mone_stopped(void) { return mario_anim && !mario_anim->loop && mario_anim->done; } /* isBckOneTimeAndStoppedPlayer */
static void sr_mweight(float a, float b, float c) { const float w[4] = {a, b, c, 0.0f}; m_force_weight(w); } /* setBckBlendWeight */
static void sr_place(const char *name) /* MR::resetPosition(this, name): its place and rotation */
{
    const float *g = scene_gpos(name);
    if (!g) return;
    float m[16], one[3] = {1, 1, 1}, rot[3] = {g[3], g[4], g[5]};
    mat_translate_rot_scale(m, g, rot, one);
    sr.pos = v3(g[0], g[1], g[2]);
    sr.side = vnorm(v3(m[0], m[1], m[2])); sr.up = vnorm(v3(m[4], m[5], m[6])); sr.front = vnorm(v3(m[8], m[9], m[10]));
}
static void sr_reset_all(void) /* resetAllInfo (its rotation zeroed: the axes kept as resetPosition left them) */
{
    sr.base_up = sr.up; sr.base_side = sr.side;
    sr.rotz = 0; sr.vel = sr.ortho = v3(0, 0, 0);
    sr.in_water = 0; sr.speed = sr.ortho_speed = sr.steer_rate = sr.steer_accel = 0; sr.air = 0;
    sr.fx_on = 0;
}
static int sr_shadow_pos(V3 *out, V3 start, V3 dir) /* calcWaterShadowPos */
{
    *out = start;
    V3 s = start;
    for (int i = 0; i < 10; i++) {
        WInfo w;
        if (water_obj(&w, s)) { water_info(&w, s, dir, 0); *out = w.surf_pos; return 1; }
        s = vadd(s, vmul(dir, 100.0f));
    }
    return 0;
}
static int sr_try_in_water(void) /* tryInWater */
{
    if (water_obj(&sr.wi, sr.pos)) return 1;
    if (water_obj(&sr.wi, vadd(vmul(sr.front, 100.0f), sr.pos))) return 1;
    if (water_obj(&sr.wi, vadd(vmul(sr.up, 150.0f), sr.pos))) return 1;
    V3 ug = vmul(sr.grav, -1.0f);
    if (water_obj(&sr.wi, vadd(vmul(rot_axis(sr.front, ug, -1.5707964f), 150.0f), sr.pos))) return 1;
    if (water_obj(&sr.wi, vadd(vmul(rot_axis(sr.front, ug, 1.5707964f), 150.0f), sr.pos))) return 1;
    if (water_obj(&sr.wi, vadd(vmul(sr.side, 150.0f), sr.pos))) return 1;
    if (water_obj(&sr.wi, vadd(vmul(sr.side, -150.0f), sr.pos))) return 1;
    return 0;
}
static int sr_wi_in(void) { return sr.wi.row >= 0 && sr.wi.kind; } /* WaterInfo::isInWater */
static void sr_calc_gravity(void) { sr.grav = vmul(up_at(sr.pos, vmul(sr.grav, -1.0f)), -1.0f); } /* MR::calcGravity */
static void sr_to_water(void) /* updateToWater */
{
    if (sr_try_in_water()) {
        if (!sr.in_water) {
            float ol = vlen(sr.ortho), lvl = sr_liner(ol, 0.0f, 20.0f, 100.0f, 1000.0f);
            if (sr.air > 30) pad_rumble(RUMBLE_MIDDLE);
            if (sr.air > 80 && ol > 20.0f) sr_se(SE_SM_SURF_RAY_JUMPIN2, -1.0f);
            else if (sr.air > 5 && ol > 10.0f) sr_se(SE_SM_SURF_RAY_JUMPIN1, lvl);
            else sr_se(SE_SM_SURF_RAY_JUMPIN0, lvl);
        }
        sr.in_water = 1; sr.air = 0;
        if (vlen(sr.vel) > 5.0f) pad_rumble(RUMBLE_WEAK); /* sSpeedToRumble */
        water_info(&sr.wi, sr.pos, sr.grav, 0);
        sr.wnorm = sr.wi.surf_n;
        float ip = vdot(sr.grav, sr.vel);
        if (ip > 0.0f && sr.wi.floor < ip) sr.vel = vsub(sr.vel, vmul(sr.grav, ip * 1.5f));
        else sr.vel = vsub(sr.vel, vmul(sr.grav, (sr.wi.depth + sr.wi.wave) * 0.01f));
    } else {
        if (sr.in_water) { sr_se(SE_SM_SURF_RAY_JUMPOUT1, sr_liner(sr.speed, 0.0f, 30.0f, 200.0f, 1000.0f)); sr.in_water = 0; }
        sr.air++;
    }
    WInfo w;
    if (water_obj(&w, vadd(sr.pos, vmul(sr.grav, 20.0f)))) sr.shadow_a -= 10.0f; /* sShadowAlphaSpeed */
    else if (sr_shadow_pos(&sr.shadow, sr.pos, sr.grav)) sr.shadow_a += 10.0f;
    else sr.shadow_a -= 10.0f;
    sr.shadow_a = clampf(sr.shadow_a, 0.0f, 64.0f); /* sShadowAlphaMax */
}
static int sr_rebound(float a2, float a3, float a4, V3 n) /* reboundVelocityFromCollision (on that face) */
{
    float d = vdot(n, sr.vel);
    if (d < -a3) { sr.vel = vsub(sr.vel, vmul(n, d)); sr.vel = vmul(sr.vel, a4); sr.vel = vsub(sr.vel, vmul(n, d * a2)); return 1; }
    if (d < 0.0f) sr.vel = vsub(sr.vel, vmul(n, d));
    return 0;
}
static void sr_info(void) /* updateInfo */
{
    if (sr.ground) {
        sr_lvse(0, SE_SM_LV_SURF_RAY_LAND, sr_liner(sr.speed, 0.0f, 40.0f, 100.0f, 1000.0f));
        if (sr_rebound(0.3f, 10.0f, 1.0f, sr.gn) && vlen(sr.vel) > 5.0f) pad_rumble(RUMBLE_MIDDLE);
    }
    if (sr.wall) {
        if (vdot(sr.vel, sr.wn) < -10.0f) sr_se(SE_SM_SURF_RAY_HIT_WALL, -1.0f);
        if (sr_rebound(0.8f, 10.0f, 1.0f, sr.wn) && vlen(sr.vel) > 5.0f) pad_rumble(RUMBLE_MIDDLE);
    }
    sr.speed = vdot(sr.front, sr.vel);
    sr.ortho = vsub(sr.vel, vmul(sr.front, sr.speed));
    sr.ortho_speed = vlen(sr.ortho);
}
static void sr_to_map(void) /* updateToMap */
{
    V3 g = vmul(sr.grav, sr.in_water ? 0.1f : 0.8f);
    sr.ortho = vadd(sr.ortho, g);
    V3 o;
    if (sr.ground || sr.wall) {
        if (sr.ground) { sr.gnorm = sr.gn; if (blend_sphere(sr.base_up, sr.gnorm, &o, 0.1f)) sr.base_up = o; } /* sBlendRatioAxisUpGround */
        if (sr.wall) {
            V3 v = sr.vel, w;
            if (vlen(v) > 1e-6f) v = vnorm(v);
            float l = vlen(v); v = vtangent(v, sr.wn); if (vlen(v) > 1e-6f) v = vmul(vnorm(v), l); /* turnVecToPlane */
            l = vlen(v); v = vtangent(v, sr.grav); if (vlen(v) > 1e-6f) v = vmul(vnorm(v), l);
            w = vadd(vmul(sr.front, 0.9f), vmul(v, 0.1f)); /* vecBlend sBlendRatioAxisFrontWall */
            sr.base_side = vcross(sr.base_up, w);
        }
    } else if (sr.in_water) { if (blend_sphere(sr.base_up, sr.wnorm, &o, 0.1f)) sr.base_up = o; }
    else if (blend_sphere(sr.base_up, vmul(sr.grav, -1.0f), &o, 0.02f)) sr.base_up = o; /* sBlendRatioAxisUpAir */
    V3 f = vcross(sr.base_side, sr.base_up);
    if (vlen(f) > 1e-6f) sr.front = vnorm(f);
}
static void sr_fx(u32 on) { sr.fx_on = on; } /* tryEmitEffect the one(s) in `on`, tryDeleteEffect the others */
static void sr_accel_upd(void) /* updateAccel */
{
    if (sr_a()) sr.speed += 0.5f; else sr.speed *= 0.99f;
    sr.speed = clampf(sr.speed, 0.0f, 40.0f);
    if (!sr_wi_in()) sr_fx(0);
    else if (sr.speed > 20.0f) sr_fx(4);     /* sSpeedToEmitEffectFast */
    else if (sr.speed > 6.0f) sr_fx(2);      /* sSpeedToEmitEffect */
    else {
        sr_fx(1);
        V3 up = vmul(sr.grav, -1.0f), x = vcross(up, sr.front); /* makeMtxUpFrontPos */
        if (vlen(x) > 1e-6f) {
            x = vnorm(x);
            V3 z = vcross(x, up);
            mat_identity(sr.em);
            sr.em[0] = x.x; sr.em[1] = x.y; sr.em[2] = x.z; sr.em[4] = up.x; sr.em[5] = up.y; sr.em[6] = up.z;
            sr.em[8] = z.x; sr.em[9] = z.y; sr.em[10] = z.z;
            sr.em[12] = sr.wi.surf_pos.x; sr.em[13] = sr.wi.surf_pos.y; sr.em[14] = sr.wi.surf_pos.z;
        }
    }
    sr.ortho = vmul(sr.ortho, sr.ground || sr.wall ? 0.9f : 0.995f);
    if (v_near0(sr.ortho, 0.1f)) sr.ortho = v3(0, 0, 0);
    if (sr_wi_in() && sr.nrv != SRN_RIDEJUMP) sr.ortho = vmul(sr.ortho, 0.95f);
}
static int sr_rotate_start(void) /* isRotateStart */
{
    if (sr.in_tut) return sr.lecture == 7 || sr.lecture == 11;
    return sr.speed > 3.0f || sr_a();
}
static void sr_rotate(void) /* updateRotate */
{
    float accel_x = 0.0f;
    if (sr_rotate_start()) {
        V3 a = sr_accel();
        float steer;
        if (a.z > -0.7f || fabsf(a.x) > 0.15f) {
            float tw = a.x, az = clampf(-a.z, 0.0f, 1.0f);
            if (tw > 0.0f) tw += az; else if (tw < 0.0f) tw -= az;
            steer = clampf(tw, -1.5f, 1.5f);
            accel_x = a.x;
            if (accel_x < 0.0f) sr.tilt += (clampf(accel_x, -1.0f, 0.0f) - sr.tilt) * 0.3f;
            else if (accel_x > 0.0f) sr.tilt += (clampf(accel_x, 0.0f, 1.0f) - sr.tilt) * 0.3f;
        } else {
            sr.tilt += (0.0f - sr.tilt) * 0.3f;
            steer = sr.steer_accel * 0.95f;
        }
        sr.steer_accel = steer;
        sr.steer_rate = clampf(sr.steer_rate + steer, -30.0f, 30.0f);
    } else {
        sr.tilt += (0.0f - sr.tilt) * 0.3f;
        sr.steer_accel = 0.0f;
    }
    sr.steer_rate *= 0.95f;
    if (fabsf(sr.steer_rate) < 0.1f) sr.steer_rate = 0.0f;
    sr.rotz = clampf(sr.rotz * 0.7f + sr.steer_rate, -70.0f, 70.0f);
    float t = clampf(sr.tilt * 1.2f, -1.0f, 1.0f);
    if (t > 0.0f) sr_mweight(1.0f - t, 0.0f, t);
    else sr_mweight(t + 1.0f, -t, 0.0f);
    sr.front = rot_axis(sr.front, sr.base_up, -sr.rotz * 0.00028f); /* setRotate(mBaseUp, -rotZ x 0.00028) */
    for (int i = 7; i >= 1; i--) sr.twist[i] = sr.twist[i - 1];
    sr.twist[0] = accel_x;
}
static void sr_sound(void) /* updateSound */
{
    float s = sr.speed, lvl;
    if (s > 2.5f && s < 25.0f) {
        lvl = s < 14.0f ? sr_liner(s, 2.5f, 7.5f, 0.1f, 1.0f) : sr_liner(s, 14.0f, 25.0f, 1.0f, 0.0f);
        sr_lvse(1, SE_SM_LV_SURF_RAY_MOVE0, lvl * 1000.0f);
    }
    if (sr.in_water && s > 7.5f) sr_lvse(2, SE_SM_LV_SURF_RAY_MOVE1, sr_liner(s, 7.5f, 14.0f, 0.1f, 1.0f) * 1000.0f);
    if (sr.in_water && s > 14.0f) sr_lvse(3, SE_SM_LV_SURF_RAY_MOVE2, sr_liner(s, 14.0f, 25.0f, 0.1f, 1.0f) * 1000.0f);
    if (s > 25.0f && sr.in_water) sr_lvse(4, SE_SM_LV_SURF_RAY_MOVE3, sr_liner(s, 25.0f, 35.0f, 0.1f, 1.0f) * 1000.0f);
    float v = vlen(sr.vel);
    if (v > 32.0f) sr_lvse(5, SE_SM_SURF_RAY_MOVE_WIND, sr_liner(v, 32.0f, 50.0f, 0.1f, 1.0f) * 1000.0f);
    if (sr.in_water && fabsf(sr.rotz) / 70.0f > 0.45f) sr_lvse(6, SE_SM_LV_SURF_RAY_CURVE, sr_liner(s, 0.0f, 25.0f, 100.0f, 1000.0f));
}
static int sr_try_jump_fall(void) /* tryJumpOrFall */
{
    if (pad_pressed(PAD_SQUARE) && (sr.ground || sr.wall || sr_wi_in() || sr.air < 15)) { /* isPadSwing */
        float f0 = (sr.speed / 40.0f) * (((70.0f - fabsf(sr.rotz)) * 5.0f) / 70.0f) + 15.0f;
        V3 v1 = sr_wi_in() ? sr.wi.surf_n : sr.gnorm;
        v1 = vmul(v1, f0);
        if (vdot(sr.ortho, sr.grav) > 0.0f) sr.ortho = vtangent(sr.ortho, sr.grav); /* vecKillElement */
        sr.ortho = vadd(sr.ortho, v1);
        if (vlen(sr.ortho) > 25.0f) sr.ortho = vmul(vnorm(sr.ortho), 25.0f);
        sr_set(vlen(sr.ortho) > 23.0f ? SRN_RIDEJUMPHIGH : SRN_RIDEJUMP);
        return 1;
    }
    if (sr.air > 45) { sr_set(SRN_RIDEFALL); return 1; }
    return 0;
}
static int sr_twist_start(void) /* isTwistStart */
{
    float mx = 0.0f;
    for (int i = 1; i < 8; i++) mx = fmaxf(mx, fabsf(sr.twist[0] - sr.twist[i]));
    return mx > 0.2f; /* sTwistStartDiffMin */
}
static void sr_rider_mtx(void) /* updateRiderMtx: Mario at the Center joint, 55 along its Y and 12 along its Z */
{
    Inst *in = sr_inst();
    if (!in || !in->anim || !in->anim->skin || in->anim->skin->njoints <= SR_CENTER) {
        mario->pos = sr.pos; mario->up = sr.up; mario->face = sr.front;
        return;
    }
    anim_pose(in->anim);
    float w[16];
    mat_mul(w, in->m, in->anim->world[SR_CENTER]);
    V3 y = v3(w[4], w[5], w[6]), z = v3(w[8], w[9], w[10]);
    mario->pos = vadd(v3(w[12], w[13], w[14]), vadd(vmul(y, 55.0f), vmul(z, 12.0f)));
    if (vlen(y) > 1e-6f) mario->up = vnorm(y);
    V3 f = vtangent(z, mario->up);
    if (vlen(f) > 1e-6f) mario->face = vnorm(f);
}
static void sr_binder(void) /* LiveActor::movement's binder (initBinder 200, 50 up) */
{
    V3 up = vmul(sr.grav, -1.0f), c = vadd(sr.pos, vmul(up, 50.0f));
    V3 np = m_bind(c, sr.vel, 200.0f);
    int fl, wl;
    bind_contacts(up, &fl, &wl);
    sr.ground = fl >= 0; sr.wall = wl >= 0;
    if (sr.ground) sr.gn = tri_n(&b_tri[fl]);
    if (sr.wall) sr.wn = tri_n(&b_tri[wl]);
    sr.pos = vsub(np, vmul(up, 50.0f));
}
static int sr_update_ride(void) /* updateRide: 1 when it went to WipeOut */
{
    if (sr.in_tut_area && sr.has_warp && sr.pos.z > sr.warp.z) { sr.in_tut_area = 0; sr_set(SRN_WIPEOUT); return 1; }
    sr_calc_gravity();
    sr_to_water();
    sr_info();
    sr_to_map();
    sr_accel_upd();
    sr_sound();
    sr_rotate();
    return 0;
}
static int sr_update_accel(void) { if (sr_update_ride() || sr_try_jump_fall()) return 1; if (!sr_a()) { sr_set(SRN_RIDEFREE); return 1; } return 0; }
static int sr_update_free(void) { if (sr_update_ride() || sr_try_jump_fall()) return 1; if (sr_a()) { sr_set(SRN_RIDEACCEL); return 1; } return 0; }
static void sr_exe(void)
{
    int st = sr.step;
    switch (sr.nrv) {
    case SRN_WAITPLAYER:
        if (st == 0) sr_bck1("firstwait");
        sr_shadow_pos(&sr.shadow, sr.pos, sr.grav);
        if (vlen(vsub(mario->pos, sr.pos)) < 1300.0f) sr_set(SRN_TURNTOWAIT); /* sDistancePlayerNear */
        break;
    case SRN_TURNTOWAIT:
        if (st == 0) { sr_bck1("firstwaitturn"); sr_se(SE_SM_SURF_RAY_TURNTO_MR, -1.0f); }
        if (st > 0 && sr_bck_stopped()) sr_set(SRN_WAIT);
        break;
    case SRN_WAIT: if (st == 0) sr_bck1("wait"); break;
    case SRN_RIDEACCEL:
        if (st == 0) { sr_mbckj("サーフィン（加速）"); sr_bck1("rundash"); }
        if (!sr_update_accel() && sr_twist_start()) sr_set(SRN_RIDEACCELTWIST);
        break;
    case SRN_RIDEACCELTWIST:
        if (st == 0) sr_mbckj("サーフィン傾き開始（加速）");
        if (!sr_update_accel() && st > 0 && sr_mstopped()) sr_set(SRN_RIDEACCEL);
        break;
    case SRN_RIDEFREE:
        if (st == 0) { sr_bck1("run"); sr_mbckj("サーフィン"); }
        if (!sr_update_free() && sr_twist_start()) sr_set(SRN_RIDEFREETWIST);
        break;
    case SRN_RIDEFREETWIST:
        if (st == 0) sr_mbckj("サーフィン傾き開始");
        if (!sr_update_free() && st > 0 && sr_mstopped()) sr_set(SRN_RIDEFREE);
        break;
    case SRN_RIDEJUMP: case SRN_RIDEJUMPHIGH:
        if (st == 0) {
            sr_mbckj(sr.nrv == SRN_RIDEJUMP ? "サーフィンジャンプ" : "サーフィンハイジャンプ");
            sr_bck1("jump");
            se_start(sr.nrv == SRN_RIDEJUMP ? SE_PV_JUMP_S : SE_PV_JUMP_JOY, &mario->pos.x);
        }
        if (!sr_update_ride() && vdot(sr.vel, sr.grav) > 0.0f) sr_set(SRN_RIDEFALL);
        break;
    case SRN_RIDEFALL:
        if (st == 0) { sr_mbckj("サーフィン落下"); sr_bck1("fall"); }
        if (!sr_update_ride() && vdot(sr.vel, sr.grav) > 0.0f && sr.in_water) { sr_set(SRN_RIDELAND); sr_se(SE_SM_SURF_RAY_LANDW, -1.0f); }
        break;
    case SRN_RIDELAND:
        if (st == 0) { sr_mbckj("サーフィン着地"); sr_bck1("land"); }
        if (!sr_update_ride() && !sr_try_jump_fall() && st > 0 && sr_bck_stopped()) sr_set(SRN_RIDEFREE);
        break;
    case SRN_TUTORIAL: { /* exeTutorial */
        V3 f = sr.front;
        sr_update_ride();
        sr.ortho = vmul(sr.ortho, 0.5f);
        sr.front = f;
        sr.speed = 0.0f;
        sr.rotz *= 0.85f;
        Inst *in = sr_inst();
        int one = in && in->anim && !in->anim->loop && in->anim->done; /* isBckOneTimeAndStopped */
        switch (sr.lecture) {
        case 0:
            if (st == 0) { sr_mbckj("サーフィン着地"); sr_bck1("land"); }
            if (st > 0 && sr_mone_stopped()) sr_mbck("SurfLectureUnbalance");
            if (st > 0 && one) sr_bck1("lectureunbalance");
            break;
        case 2: if (st == 0) { sr_mbck("SurfLectureUnbalance"); sr_bck1("lectureunbalance"); } break;
        case 3: if (st == 0) { sr_mbck("SurfRide"); sr_bck1("wait"); } break;
        case 4: case 8: case 12:
            if (st == 0) { sr_mbck("SurfLectureAnswer"); sr_bck1("lectureanswer"); sr_se(SE_SM_SURF_RAY_JUMPOUT1, -1.0f); }
            if (st > 0 && sr_mone_stopped()) sr_mbck("SurfRideLoop");
            if (st > 0 && one) sr_bck1("wait");
            break;
        case 5: case 6: case 9: case 10: if (st == 0) { sr_mbck("SurfRideLoop"); sr_bck1("wait"); } break;
        case 7: case 11: {
            if (st == 0) sr_mbckj("サーフィン傾き開始");
            float r = fabsf((sr.rotz / 70.0f) * 0.6f);
            if (sr.rotz < 0.0f) sr_mweight(1.0f - r, r, 0.0f); else sr_mweight(1.0f - r, 0.0f, r);
            break;
        }
        default: break;
        }
        break;
    }
    case SRN_WIPEOUT:
        if (st == 0) menu_wipe_circle(1, -1); /* closeWipeCircle */
        if (!sr_update_ride() && st > 0 && !menu_wipe_active()) {
            sr_place("スタート位置（サーフィン）"); sr_reset_all();
            sr_mbck("SurfRide");
            camera_reset(); /* resetCameraMan */
            sr_set(SRN_WIPEIN);
            printf("surf ray: warped to the start (%d %d %d)" "\n", (int)sr.pos.x, (int)sr.pos.y, (int)sr.pos.z);
        }
        break;
    case SRN_WIPEIN:
        if (st == 0) menu_wipe_circle(0, -1); /* openWipeCircle */
        if (st > 0 && !menu_wipe_active()) sr_set(SRN_RIDEFREE);
        break;
    case SRN_READY:
        if (st == 0) { camera_start_pos_end(); sr_bck1("waitracestart"); sr_mbck("SurfRide"); sr_fx(1); } /* endStartPosCamera, Ripple on */
        sr_calc_gravity(); sr_to_water(); sr_info(); sr_to_map(); sr_sound();
        sr.ortho = vmul(sr.ortho, 0.5f); sr.speed = 0.0f; sr.rotz = 0.0f;
        break;
    }
}
static void sr_kill(void) /* ACTMES_RACE_RESET: endBindAndPlayerWait, resetAllInfo, Wait, kill */
{
    sr.rider = 0;
    sr_reset_all();
    sr.dead = 1;
    mario_play("wait", 1.0f, 1);
    Inst *in = sr_inst();
    if (in) in->invisible = 1;
    printf("surf ray: race reset, Mario let go, killed" "\n");
}
/* RaceManager's messages to the rush actor (race.inc): 0 RACE_READY, 1 RACE_START, 2 RACE_GOAL, 3 RACE_RESET */
static int sr_rush_msg(int msg)
{
    if (!sr_riding()) return 0;
    if (msg == 0) sr_set(SRN_READY);
    else if (msg == 1) sr_set(SRN_RIDEFREE);
    else if (msg == 3) sr_kill();
    else return 0;
    return 1;
}
static void sr_msg(int msg) /* receiveOtherMsg: the tutorial's (sendMsgToGroupMember from the coach) */
{
    if (sr.scene != sc || sr.row < 0 || sr.dead) return;
    switch (msg) {
    case SRM_TUTORIAL_START: rm_player_at(sr.pos, sr.up, sr.front); mario->grounded = 0; sr.in_tut = 1; break; /* setPlayerPos(mPosition) */
    case SRM_TUTORIAL_NEXT: sr.lecture++; sr_set(SRN_TUTORIAL); break;
    case SRM_TUTORIAL_PREV: sr.lecture--; sr_set(SRN_TUTORIAL); break;
    case SRM_TUTORIAL_PASS: sr.in_tut = 0; sr_set(SRN_RIDEFREE); break;
    case SRM_TUTORIAL_OMIT:
        sr.in_tut_area = 0;
        sr_place("スタート位置（サーフィン）"); sr_reset_all();
        rm_player_at(sr.pos, sr.up, sr.front); mario->grounded = 0;
        sr_mbck("SurfRide");
        camera_reset();
        break;
    }
    printf("surf ray: message %d, lecture %d" "\n", msg, sr.lecture);
}
static void sr_init(void)
{
    memset(&sr, 0, sizeof sr);
    sr.scene = sc; sr.row = -1; sr.next = -1;
    int ty = scene_type(sc, "SurfRay");
    for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type != ty) continue;
        sr.row = (int)i;
        float m[16], one[3] = {1, 1, 1};
        mat_translate_rot_scale(m, r->pos, r->rot, one);
        sr.pos = row_pos(r);
        sr.side = vnorm(v3(m[0], m[1], m[2])); sr.up = vnorm(v3(m[4], m[5], m[6])); sr.front = vnorm(v3(m[8], m[9], m[10]));
        if (seq_is_retry()) sr_place("スタート位置（サーフィン）"); /* hasRetryGalaxySequence: resetPosition */
        sr.grav = vmul(up_at(sr.pos, sr.up), -1.0f);
        sr.gnorm = sr.wnorm = sr.base_up = sr.up;
        sr_reset_all();
        const float *w = scene_gpos("ワープ位置（サーフィン）"); /* tryFindNamePos */
        if (w) { sr.warp = v3(w[0], w[1], w[2]); sr.has_warp = 1; }
        sr.in_tut_area = !seq_is_retry();
        sr.nrv = SRN_WAITPLAYER;
        printf("surf ray: row %u at %d %d %d, warp %d (z %d), retry %d" "\n", i, (int)sr.pos.x, (int)sr.pos.y, (int)sr.pos.z, sr.has_warp, (int)sr.warp.z, seq_is_retry());
        break;
    }
}
/* MarioActor's side: the ride sensor (260) touched, not rising (receiveOtherMsg isMsgAutoRushBegin) */
static int sr_try(Player *m)
{
    if (sr.scene != sc || sr.row < 0 || sr.dead || sr.rider || !row_shown(sr.row) || miss_running()) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    if (vlen(vsub(c, sr.pos)) >= 260.0f + HIT_BODY_R) return 0;
    if (m->vup > 0.0f && !m->grounded) return 0; /* getPlayerVelocity()->y > 0 */
    if (!mode_rush("SurfRay")) return 0;
    sr.rider = 1;
    sr.tilt = 0.0f;
    const Row *r = &sc->objs[sr.row];
    if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1); /* onSwitchA */
    Inst *in = sr_inst();
    if (in) obj_play(in, "wait", 1, 0); /* startBckNoInterpole */
    mario_play("surfrideloop", 1.0f, 1);
    m->hip = 0; m->spin_timer = 0; m->hvel = v3(0, 0, 0); m->vup = 0;
    sr_set(sr.in_tut ? SRN_TUTORIAL : SRN_READY);
    printf("surf ray: Mario bound (%s)" "\n", sr.in_tut ? "tutorial" : "ready");
    return 1;
}
static int sr_hold(Player *m) /* Mario bound: ACTMES_UPDATE_BASEMTX (updateRiderMtx) */
{
    if (!sr_riding()) return 0;
    if (miss_running()) { sr.rider = 0; return 0; } /* (a miss ends the bind) */
    sr_rider_mtx();
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    m_center(m);
    return 1;
}
static float sr_reach(void) { return sr_riding() ? 260.0f + 55.0f : 0.0f; } /* its ride sensor's tryGetItem (a coin's 55) */
static V3 sr_body(void) { return sr.pos; }
#ifdef SURF_TEST
static void game_test_surf(int f);
#endif
static void update_surf(void)
{
    if (sr.scene != sc) sr_init();
#ifdef SURF_TEST
    game_test_surf(game_frame);
#endif
    if (sr.row < 0 || sr.dead || !row_shown(sr.row)) return;
    if (sr.next >= 0) { sr.nrv = sr.next; sr.step = 0; sr.next = -1; }
    sr_exe();
    sr.step++;
    if (sr.next >= 0) { sr.nrv = sr.next; sr.step = 0; sr.next = -1; printf("surf ray: nerve %d" "\n", sr.nrv); }
    if (sr.nrv >= SRN_RIDEACCEL) sr_binder(); /* (its binder while ridden: waiting it lies on the water) */
    /* control: mVelocity = front x speed + ortho */
    sr.vel = vadd(vmul(sr.front, sr.speed), sr.ortho);
    /* calcAndSetBaseMtx */
    V3 s = vcross(sr.base_up, sr.front);
    if (vlen(s) > 1e-6f) sr.base_side = vnorm(s);
    V3 u = vcross(sr.front, sr.base_side);
    if (vlen(u) > 1e-6f) sr.base_up = vnorm(u);
    sr.up = rot_axis(sr.base_up, sr.front, sr.rotz * 0.017453293f);
    s = vcross(sr.up, sr.front);
    if (vlen(s) > 1e-6f) sr.side = vnorm(s);
    sr.up = vnorm(vcross(sr.front, sr.side));
    Inst *in = sr_inst();
    if (in) {
        mat_identity(in->m);
        in->m[0] = sr.side.x; in->m[1] = sr.side.y; in->m[2] = sr.side.z;
        in->m[4] = sr.up.x; in->m[5] = sr.up.y; in->m[6] = sr.up.z;
        in->m[8] = sr.front.x; in->m[9] = sr.front.y; in->m[10] = sr.front.z;
        in->m[12] = sr.pos.x; in->m[13] = sr.pos.y; in->m[14] = sr.pos.z;
        in->nu = sr.up; in->nf = sr.front;
        const float (*world)[16] = in->anim ? (const float (*)[16])in->anim->world : NULL;
        if (sr.fx_on & 1) fx_named("SurfRay", "Ripple", sr.em, NULL, &sr.fxkey[0]);
        if (sr.fx_on & 2) fx_named("SurfRay", "SwimSplash", in->m, world, &sr.fxkey[1]);
        if (sr.fx_on & 4) fx_named("SurfRay", "RunDashSplash", in->m, world, &sr.fxkey[2]);
    }
    if (sr_riding()) sr_rider_mtx(); /* (the base matrix's own UPDATE_BASEMTX) */
}

/* --- SurfRayTutorial (the coach's mTutorial) */
enum { ST_ALLSTART, ST_STRAIGHT_START, ST_STRAIGHT_FAIL, ST_STRAIGHT_KEEP, ST_STRAIGHT_OK, ST_LEFT_START, ST_LEFT_FAIL, ST_LEFT_KEEP,
       ST_LEFT_OK, ST_RIGHT_START, ST_RIGHT_FAIL, ST_RIGHT_KEEP, ST_RIGHT_OK, ST_ALLEND };
static struct { int nrv, step, next, pass, change, n28, k; V3 acc; } srt;
static void srt_node(int n) { talk_reset_node(srt.k); for (int i = 0; i < n; i++) talk_forward_node(srt.k); } /* resetAndForwardNode */
static int srt_ok_straight(void) { return fabsf(srt.acc.x) < 0.25f && fabsf(srt.acc.y) < 0.45f; }
static int srt_stand(void) { return srt.acc.y < -0.5f; }                     /* isFailureStand: sPadAccelTurnLimitY */
static int srt_ok_left(void) { return !(srt.acc.x > -0.65f) && !srt_stand(); }  /* isFailureTwistLeftMore: sPadAccelTurnMinX */
static int srt_ok_right(void) { return !(srt.acc.x < 0.65f) && !srt_stand(); }
static void srt_next(void) /* nextTutorial: the next nerve, TUTORIAL_NEXT */
{
    if (srt.nrv < ST_ALLEND) srt.next = srt.nrv + 1;
    sr_msg(SRM_TUTORIAL_NEXT);
}
static void srt_prev(void) /* prevTutorial: Upkeep back to Failure, TUTORIAL_PREV */
{
    if (srt.nrv == ST_STRAIGHT_KEEP || srt.nrv == ST_LEFT_KEEP || srt.nrv == ST_RIGHT_KEEP) srt.next = srt.nrv - 1;
    sr_msg(SRM_TUTORIAL_PREV);
}
static void srt_timer(int step, int n) /* startTimerSound (its second argument: ponytail, the SE's parameter) */
{
    (void)n;
    if (step == 20) se_start(SE_SY_SURF_TUTORIAL_TIMER, NULL);
    if (step == 43) se_start(SE_SY_SURF_TUTORIAL_TIMER2, NULL);
    if (step == 66) se_start(SE_SY_SURF_TUTORIAL_TIMER3, NULL);
}
static int srt_update(void) /* update */
{
    srt.acc = sr_accel();
    if (srt.next >= 0) { srt.nrv = srt.next; srt.step = 0; srt.next = -1; }
    int st = srt.step, n = srt.nrv;
    int k = srt.k;
    switch (n) {
    case ST_ALLSTART:
        if (st == 0) { pad_rumble(RUMBLE_MIDDLE); sr_msg(SRM_TUTORIAL_START); srt_node(0); }
        if (talk_force_at_end(k, 0)) srt_next(); /* (mSurfingGuidance->activate) */
        break;
    case ST_STRAIGHT_START: case ST_LEFT_START: case ST_RIGHT_START:
        if (st == 0) { pad_rumble(RUMBLE_MIDDLE); srt_node(n == ST_STRAIGHT_START ? 1 : n == ST_LEFT_START ? 3 : 5); }
        talk_force(k, 0);
        if (st > 150) srt_next(); /* isGreaterStep sStepTalk */
        break;
    case ST_STRAIGHT_FAIL: case ST_LEFT_FAIL: case ST_RIGHT_FAIL: {
        if (st == 0) srt.change = 0;
        int ok = n == ST_STRAIGHT_FAIL ? srt_ok_straight() : n == ST_LEFT_FAIL ? srt_ok_left() : srt_ok_right();
        if (ok) { if (srt.change++ == 10) srt_next(); } else srt.change = 0; /* sStepToOK */
        talk_force(k, 0);
        break;
    }
    case ST_STRAIGHT_KEEP: case ST_LEFT_KEEP: case ST_RIGHT_KEEP: {
        if (st == 0) { srt.change = 0; pad_rumble(RUMBLE_WEAK); se_start(SE_SY_SURF_TUTORIAL_OK, NULL); }
        srt_timer(st, srt.n28);
        int ok = n == ST_STRAIGHT_KEEP ? srt_ok_straight() : n == ST_LEFT_KEEP ? srt_ok_left() : srt_ok_right();
        if (ok) {
            srt.change = 0;
            if (st == 90) { se_start(n == ST_RIGHT_KEEP ? SE_SY_SURF_TUTORIAL_GONEXT2 : SE_SY_SURF_TUTORIAL_GONEXT, NULL); srt.n28++; srt_next(); break; } /* sStepToSuccess */
        } else if (srt.change++ == 10) { srt_prev(); se_start(SE_SY_SURF_TUTORIAL_NG, NULL); }
        talk_force(k, 0);
        break;
    }
    case ST_STRAIGHT_OK: case ST_LEFT_OK: case ST_RIGHT_OK:
        if (st == 0) { pad_rumble(RUMBLE_MIDDLE); srt_node(n == ST_STRAIGHT_OK ? 2 : n == ST_LEFT_OK ? 4 : 6); }
        if (talk_force_at_end(k, 0)) srt_next();
        break;
    case ST_ALLEND:
        if (st == 0) { pad_rumble(RUMBLE_MIDDLE); srt_node(7); }
        if (talk_force_at_end(k, 0) && !srt.pass) { sr_msg(SRM_TUTORIAL_PASS); srt.pass = 1; }
        break;
    }
    srt.step++;
    if (srt.next >= 0 && srt.next != n) printf("surf tutorial: nerve %d" "\n", srt.next);
    return srt.pass;
}

/* --- PenguinCoach, arg0 2 (Behavior_Tutorial) / 3 (Behavior_Race) */
enum { PCS_PREP, PCS_FADE, PCS_TUTORIAL, PCS_RACE, PCS_PRAISE, PCS_WAIT };
static struct { const Scene *scene; int row, beh, nrv, step, next, done, cam, k; } pcs = {.row = -1};
static void pcs_set(int n) { pcs.next = n; }
static int pcs_of(int row) /* the surfing coach (its state set up at its first talk frame) */
{
    if (pcs.scene != sc) {
        memset(&pcs, 0, sizeof pcs); pcs.scene = sc; pcs.row = -1; pcs.next = -1; pcs.k = -1;
        int ty = scene_type(sc, "PenguinCoach");
        for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && (sc->objs[i].arg[0] == 2 || sc->objs[i].arg[0] == 3)) { pcs.row = (int)i; pcs.beh = sc->objs[i].arg[0]; break; }
        memset(&srt, 0, sizeof srt); srt.next = -1; srt.k = -1;
        if (pcs.row >= 0) {
            srt.k = talk_new("Common_SurfingTutorial000", -1, pcs.row, 430.0f, 350.0f); /* createTalkCtrlDirect (caps.mMessageOffset) */
            printf("penguin coach row %d: surfing mode %d, tutorial talk %d" "\n", pcs.row, pcs.beh, srt.k);
        }
    }
    return pcs.row >= 0 && pcs.row == row;
}
static void pcs_npc_pos(Inst *in, const char *name) /* MR::setNPCActorPos */
{
    const float *g = scene_gpos(name);
    if (!g || !in) return;
    float m[16], one[3] = {1, 1, 1}, rot[3] = {g[3], g[4], g[5]};
    mat_translate_rot_scale(m, g, rot, one);
    for (int j = 0; j < 12; j++) m[j] *= in->s;
    memcpy(in->m, m, 64);
    in->nu = vnorm(v3(m[4], m[5], m[6])); in->nf = vnorm(v3(m[8], m[9], m[10]));
}
static void pcs_player_pos(const char *name) /* MR::setPlayerPos */
{
    const float *g = scene_gpos(name);
    if (!g) return;
    float m[16], one[3] = {1, 1, 1}, rot[3] = {g[3], g[4], g[5]};
    mat_translate_rot_scale(m, g, rot, one);
    rm_player_at(v3(g[0], g[1], g[2]), v3(m[4], m[5], m[6]), v3(m[8], m[9], m[10]));
    mario->grounded = 0;
}
static void pcs_cam(Inst *in, const char *ev, int on) /* startMultiActorCameraTargetSelf / endMultiActorCamera */
{
    if (on) pcs.cam = camera_actor_start("PenguinCoach", ev, (unsigned short)row_camset[pcs.row], in->nu, in->nf, v3(in->m[12], in->m[13], in->m[14]));
    else if (pcs.cam) { camera_event_stop(); pcs.cam = 0; }
}
/* update_talkers' turn for the coach: its nerves' talks (1 handled) */
static int pcs_talk(int k, Inst *in)
{
    if (!pcs_of(in->row)) return 0;
    pcs.k = k;
    if (pcs.beh == 2 && !pcs.done) talkers[k].read_auto = 0; /* offReadNodeAutomatic */
    if (pcs.next >= 0) { pcs.nrv = pcs.next; pcs.step = 0; pcs.next = -1; }
    int st = pcs.step;
    switch (pcs.nrv) {
    case PCS_PREP:
        if (pcs.beh == 3 && td.st == k && td.nrv == TD_TALK && !pcs.cam) pcs_cam(in, "会話", 1); /* isTalkStart */
        if (talk_near_at_end(k)) {
            if (pcs.beh == 2 || tk_yesno == 1) { menu_wipe(1, -1, 0); pcs_set(PCS_FADE); } /* closeWipeFade */
            else { pcs_cam(in, "会話", 0); talkers[k].state = 0; } /* tryTalkSelectRight: endMultiActorCamera 会話 */
        }
        break;
    case PCS_FADE:
        if (menu_wipe_active()) break;
        menu_wipe(0, -1, 0); /* openWipeFade (endTalkingSequence) */
        if (pcs.beh == 2) {
            if (gd_flag_is_on("サーフィンチュートリアル")) {
                if (tk_yesno == 1) { pcs_cam(in, "チュートリアル", 1); pcs_npc_pos(in, "コーチチュートリアル位置"); pcs_set(PCS_TUTORIAL); }
                else { pcs_npc_pos(in, "コーチレース終了後位置"); sr_msg(SRM_TUTORIAL_OMIT); pcs_set(PCS_RACE); }
            } else if (tk_yesno == 1) { pcs_cam(in, "チュートリアル", 1); pcs_npc_pos(in, "コーチチュートリアル位置"); pcs_set(PCS_TUTORIAL); }
            else { talkers[k].state = 0; pcs_player_pos("いいえ選択移動位置"); pcs_set(PCS_PREP); } /* clearTalkState */
        } else {
            pcs_player_pos("スタート位置（サーフィン）");
            pcs_cam(in, "会話", 0);
            pcs_set(PCS_RACE);
        }
        printf("penguin coach row %d: fade over, yes %d -> %d" "\n", pcs.row, tk_yesno, pcs.next);
        break;
    case PCS_TUTORIAL:
        if (srt_update()) {
            pcs_set(PCS_RACE);
            pcs_cam(in, NULL, 0); /* endMultiActorCamera チュートリアル (120) */
            gd_flag_try_on("サーフィンチュートリアル");
            printf("penguin coach row %d: tutorial passed" "\n", pcs.row);
        }
        break;
    case PCS_RACE: break;
    case PCS_PRAISE:
        if (st == 0) pcs_cam(in, "会話", 1);
        if (!menu_wipe_active() && talk_force_at_end(k, 1)) { pcs_cam(in, NULL, 0); pcs_set(PCS_WAIT); talkers[k].state = 0; }
        break;
    case PCS_WAIT: talk_near(k); break;
    }
    pcs.step++;
    if (pcs.next >= 0 && pcs.next != pcs.nrv) printf("penguin coach row %d: nerve %d" "\n", pcs.row, pcs.next);
    return 1;
}
static void pcs_reset_audience(void) /* resetAudience (RaceManager::resetRace) */
{
    if (pcs.scene != sc || pcs.row < 0) return;
    pcs.done = 1;
    if (pcs.k >= 0) talkers[pcs.k].read_auto = 1; /* onReadNodeAutomatic */
    pcs_set(PCS_PRAISE);
    if (pcs.beh == 2) pcs_npc_pos(row_inst(pcs.row), "コーチレース終了後位置"); /* setFinishPos */
    printf("penguin coach row %d: race over, praise" "\n", pcs.row);
}
static int pcs_branch(u32 v) /* branchFunc 2-5 */
{
    if (v == 2) return gd_flag_is_on("サーフィンチュートリアル");
    if (v == 3) return pcs.done;
    if (v == 4) return rm.time < 90 * 60;
    if (v == 5) return rm.time < 180 * 60;
    return 0;
}

#ifdef SURF_TEST
/* Test builds (plain XF "-DSURF_TEST=1 -DPACKS_OVERLAY=..." SurfingLv1Galaxy/1: the coach's talk (A, Yes), the
 * lecture (the stick held as each one asks), the tutorial course (A held) to the warp, the race carried along the judge
 * rail, the praise talk, its star; =2 SurfingLv2Galaxy/1: the coach's Yes, Mario on the ray, the race carried, the star) */
static void game_test_surf(int f)
{
    static int last = -1;
    test_nolost = 1;
    pad_force = 0; pad_force_stick = 0;
    if (f % 60 == 0 || sr.nrv != last)
        printf("surf test f %d: ray nrv %d pos %d %d %d speed %.1f water %d rider %d coach %d tut %d/%d race %d pd %s mario %d %d %d" "\n",
               f, sr.nrv, (int)sr.pos.x, (int)sr.pos.y, (int)sr.pos.z, sr.speed, sr.in_water, sr.rider, pcs.nrv, srt.nrv, sr.lecture, rm.nrv,
               pd.name ? pd.name : "-", (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z);
    last = sr.nrv;
    if (pcs.row < 0 || f < 200) return;
    static int slct;
    if (td.nrv != TD_SLCT) slct = 0;
    if (td.nrv == TD_SLCT) { /* Yes: the pointer on it, then A (as the race tests) */
        if (++slct == 60) printf("surf test: pointer on BoxRight %d" "%c", menu_test_point("BoxRight"), 10);
        pad_force = slct == 80 ? PAD_CROSS : 0;
        return;
    }
    if (talk_active() && !(pcs.nrv == PCS_TUTORIAL && (srt.nrv % 4 == 2 || srt.nrv % 4 == 3) && srt.nrv < ST_ALLEND)) { pad_force = (f % 45) < 3 ? PAD_CROSS : 0; }
    if (pcs.nrv == PCS_PREP && !talk_active()) { /* by the coach */
        Inst *c = row_inst(pcs.row);
        if (c) { V3 cp = v3(c->m[12], c->m[13], c->m[14]); if (vlen(vsub(mario->pos, cp)) > 300.0f) rm_player_at(vadd(cp, vmul(c->nf, 250.0f)), c->nu, vmul(c->nf, -1.0f)); }
        pad_force = (f % 40) < 3 ? PAD_CROSS : 0; /* A at it */
        return;
    }
    if (pcs.nrv == PCS_TUTORIAL) { /* the pose each lecture asks */
        pad_force_stick = 1; pad_force_lx = 0.0f; pad_force_ly = 0.0f;
        if (srt.nrv == ST_LEFT_FAIL || srt.nrv == ST_LEFT_KEEP) pad_force_lx = -0.9f;
        if (srt.nrv == ST_RIGHT_FAIL || srt.nrv == ST_RIGHT_KEEP) pad_force_lx = 0.9f;
        return;
    }
    if (pcs.beh == 2 && sr_riding() && sr.in_tut_area && sr.nrv >= SRN_RIDEACCEL && sr.nrv <= SRN_RIDELAND) { /* the tutorial course: A held, then to the warp line */
        pad_force |= PAD_CROSS;
        static int t0 = -1;
        if (t0 < 0) t0 = f;
        if (f - t0 > 600 && sr.has_warp) { sr.pos = v3(sr.warp.x, sr.pos.y, sr.warp.z + 50.0f); printf("surf test f %d: put past the warp line" "\n", f); }
        return;
    }
    if (rm.nrv == RM_RACE && sr_riding()) { /* carried along the judge rail */
        static float c = 0.0f;
        RmRail *rr = &rm.rail;
        c = fminf(c + 60.0f, rr->len + 200.0f);
        V3 d, p = nrail_at(rr->pts, rr->np, rr->cl, fminf(c, rr->len), &d);
        if (c > rr->len) p = vadd(p, vmul(vnorm(d), c - rr->len));
        for (int k = 0; k < 200; k++) { /* (the judge rail runs over the water: on its surface below) */
            WInfo w;
            V3 q = vadd(p, vmul(sr.grav, 100.0f * (float)k));
            if (water_obj(&w, q)) { water_info(&w, q, sr.grav, 0); p = w.surf_pos; break; }
        }
        sr.pos = p; sr.front = vnorm(vtangent(d, sr.up)); sr.vel = v3(0, 0, 0); sr.ortho = v3(0, 0, 0);
        return;
    }
    if (pcs.nrv == PCS_WAIT && coach_took(pcs.row) && f % 20 == 0) { /* to its star */
        int ps = scene_type(sc, "PowerStar");
        for (u32 i = 0; ps >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ps) { rm_player_at(row_pos(&sc->objs[i]), mario->up, mario->face); }
    }
}
#endif
