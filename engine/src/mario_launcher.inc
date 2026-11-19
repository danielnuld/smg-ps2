/* MarioLauncher.cpp (the cannon Mario rides, its MarioLauncherShell and MarioLauncherLayout) after Petari.
 * Wait: Mario touching its Bind sensor (80 at 20 up) is bound (AutoRushBegin: doReady). Ready: EarthenPipeReady,
 * SE_PM_MARIO_LAUNCHER_PREP, his matrix blended to the launcher's over the clip. PlayerIn: EarthenPipeIn at 4 x,
 * SE_PV_CATCH; doPrep: Bind off, the camera target 150 up the launcher (Front: 200 along its Z too), Mario hidden.
 * Prep: at 10 its Active (arg0 1: LookUp) and SE_OJ_MARIO_LAUNCHER_IN, at 26 SE_OJ_MARIO_LAUNCHER_BEND, the clip over:
 * doAim (SE_SY_MARIO_LAUNCHER_IN, the launcher camera, SW_B on). Aim: its model hidden, the sight at the pointer,
 * SE_SY_LV_MARIO_LAUNCHER_AIM; A: doEject (the shell from the camera target along the pointer's ray at sEjectSpeed
 * 40, CannonFlyStart, a strong shake and rumble, LocusSmoke, the flight camera over 60), B: doReject (Reject: Mario
 * shown at 18, thrown 10 up and 10 back at 20 with SE_PV_JUMP_S, Bind on and Wait at 30). Flight: SE_PM_MARIO_
 * LAUNCHER_LAUNCH / SE_PV_SHOOT_UP, SE_PM_LV_MARIO_LAUNCHER_FLY, CannonFlyLoop and SE_PV_JUMP_JOY at 65, the model back
 * at 90, DieCannonFly at 300, at 360 forceKillPlayerByAbyss (Kill); the shell's binder (80 at 180 along its flight;
 * the launcher's own faces let through: isBreak) bound, or in water: doLanding (WallHit, endBindAndPlayerJump of the
 * last move x 0.4, MarioActor::endRush's landing type 1 with his last bound move as the jump, Land: the flight camera
 * ends, Wait). No MarioLauncherAttractor is placed in any scenario (out/census.json): calcAttractMarioLauncherOrZero
 * is zero, so the shell's gravity stays zero, its flight straight and the Gravity effect never emitted.
 * The launcher camera is CameraDPD (CAM_TYPE_DPD, CameraType_UpdateWithTarget: 120 up the target, turned -elevation
 * about X (0.62, Up: pi / 2), by the blended pointer -x * 0.8 (Up: 0.3) about Y and y * 0.3 about X, looking 1000
 * ahead), the flight camera CameraObjParallel about the player (dist 900, angleA 1.4, angleB pi): both carried by
 * camera_prog_start every frame.
 * ponytail: the PS2 pointer (the right stick's cursor) is always in the screen (MarioLauncher.cpp:231, 520
 * isStarPointerInScreen); CameraDPD's fovy (38 + 0.05 x: MarioLauncher has no Z zoom) is the prog camera's; no
 * requestMarioLauncherGuidance layout (MarioLauncher.cpp:233); the Shell sensor's receiveMsgPush does not punch its
 * pusher back (MarioLauncher.cpp:394: the port's pushes have no sender);
 * ACTMES_LAUNCHER_BREAKABLE (:677) is answered by no class in Petari; MarioLauncherLayout's Sight (Sight.arc, one
 * 64 x 64 quad) is not in tools/layout.py LAYOUTS (a texture in every scene's font chunk): the pointer's cursor marks
 * the aim until it is packed (ml_draw_sight draws it then) */
enum { ML_WAIT, ML_READY, ML_PLAYERIN, ML_PREP, ML_AIM, ML_FLIGHT, ML_KILL, ML_LAND, ML_REJECT };
#define ML_MAX 8
static struct {
    int row, nrv, st, up_type, bound, hidden, sight, test_a, test_b;
    V3 p0, u0, f0;          /* mPlayerBaseMtx: Mario's as he was bound */
    V3 lp, lx, ly, lz;      /* mLauncherBaseMtx */
    V3 tp;                  /* mCameraTargetMtx's place (its axes the launcher's) */
    V3 pp, pu, pf;          /* mPlayerMtx: place, Y, Z */
    V3 sp, sv, sy, sz, reb; /* the shell: place, velocity, mBaseMtx's Y / Z, mReboundDir */
    V3 head;                /* Mario's head vector as the bind ended (_1FC) */
    float rx, ry, bx, by;   /* CameraDPD mRealPointPos / mBlendPointPos */
    float mx;               /* the launcher's m[12] while its model is hidden */
    u32 lv_aim, lv_fly;
    short rows[ML_MAX];
    int n;
} mla = {.row = -1};

static void ml_set(int n) { mla.nrv = n; mla.st = -1; }
static int ml_flying(void) { return mla.row >= 0 && mla.bound && mla.nrv == ML_FLIGHT; }
static int ml_bound(void) { return mla.row >= 0 && mla.bound; }
static V3 ml_head(void) { return mla.head; }
static void ml_show_model(Inst *in, int on) /* showModel / hideModel */
{
    if (!in || on == !mla.hidden) return;
    if (on) in->m[12] = mla.mx; else { mla.mx = in->m[12]; in->m[12] = 1e9f; }
    mla.hidden = !on;
}
static void ml_init(const Scene *s)
{
    int t = scene_type(s, "MarioLauncher");
    memset(&mla, 0, sizeof mla);
    mla.row = -1;
    for (u32 i = 0; t >= 0 && i < s->nobjs && mla.n < ML_MAX; i++)
        if (s->objs[i].type == t) mla.rows[mla.n++] = (short)i;
}
static void ml_player(V3 p, V3 y, V3 z) { mla.pp = p; mla.pu = y; mla.pf = z; }

/* the launcher camera: CameraDPD::calc (CameraType_UpdateWithTarget) on mCameraTargetMtx */
static void ml_aim_camera(void)
{
    float px = (cur_x - 320.0f) / 320.0f, py = (cur_y - 224.0f) / 224.0f; /* getCorePadPointingPos: -1 .. 1, y down */
    mla.rx = mla.rx * 0.9f + px * 0.1f; mla.ry = mla.ry * 0.9f + py * 0.1f;
    px = fminf(fmaxf(mla.rx, -1.0f), 1.0f); py = fminf(fmaxf(mla.ry, -1.0f), 1.0f);
    float sp = clamp01(sqrtf((px - mla.bx) * (px - mla.bx) + (py - mla.by) * (py - mla.by)) / 1.4142135f);
    float t = (1.0f - sp * 0.05f) * 0.99f; /* mSpeedBlendRate, mBlendFriction */
    mla.bx = mla.bx * t + px * (1.0f - t); mla.by = mla.by * t + py * (1.0f - t);
    /* mMtx x RotX(-mAngleX) x RotY(-x * mAngleXRange) x RotX(y * mAngleYRange): its Y and Z */
    float ax = mla.up_type ? 1.5707963f : 0.62f, rx = mla.up_type ? 0.3f : 0.8f, ry = 0.3f;
    float a = mla.by * ry, b = -mla.bx * rx;
    V3 y = v3(0, cosf(a), sinf(a)), z = v3(0, -sinf(a), cosf(a));                       /* RotX(a) */
    y = v3(y.x * cosf(b) + y.z * sinf(b), y.y, -y.x * sinf(b) + y.z * cosf(b));          /* RotY(b) */
    z = v3(z.x * cosf(b) + z.z * sinf(b), z.y, -z.x * sinf(b) + z.z * cosf(b));
    y = v3(y.x, y.y * cosf(ax) + y.z * sinf(ax), -y.y * sinf(ax) + y.z * cosf(ax));      /* RotX(-ax) */
    z = v3(z.x, z.y * cosf(ax) + z.z * sinf(ax), -z.y * sinf(ax) + z.z * cosf(ax));
    V3 up = vadd(vadd(vmul(mla.lx, y.x), vmul(mla.ly, y.y)), vmul(mla.lz, y.z));
    V3 fr = vadd(vadd(vmul(mla.lx, z.x), vmul(mla.ly, z.y)), vmul(mla.lz, z.z));
    V3 eye = vadd(mla.tp, vmul(mla.ly, 120.0f)); /* mDist */
    camera_prog_start(vadd(eye, vmul(fr, 1000.0f)), eye, up, 0);
}
/* the flight camera: CameraObjParallel about the player (his base matrix the shell's) */
static void ml_flight_camera(void)
{
    V3 y = mla.pu, z = mla.pf;
    float sa = sinf(1.4f), ca = cosf(1.4f); /* angleA 1.4, angleB pi (sin 0, cos -1) */
    V3 zc = vadd(vmul(y, -sa), vmul(z, -ca));
    camera_prog_start(mla.pp, vadd(mla.pp, vmul(zc, 900.0f)), vadd(vmul(y, ca), vmul(z, -sa)), 60);
}

static void ml_ready(int row) /* doReady (receiveOtherMsg AutoRushBegin) */
{
    Inst *in = row_inst(row);
    Player *m = mario;
    mla.row = row; mla.bound = 1; mla.hidden = 0; mla.sight = 0; mla.reb = v3(0, 0, 0);
    mla.up_type = sc->objs[row].arg[0] == 1; /* LauncherType_Up */
    mla.p0 = m->pos; mla.u0 = m->up; mla.f0 = m->face;
    mla.lp = v3(in->m[12], in->m[13], in->m[14]);
    mla.lx = vnorm(v3(in->m[0], in->m[1], in->m[2])); mla.ly = vnorm(v3(in->m[4], in->m[5], in->m[6])); mla.lz = vnorm(v3(in->m[8], in->m[9], in->m[10]));
    ml_player(mla.p0, mla.u0, mla.f0);
    drop_damage(); /* tryPlayerKillTakingActor */
    m->hvel = v3(0, 0, 0); m->vup = 0; m->hip = 0; m->spin_timer = 0; m->jumping = 0; m->land_type = 0;
    ml_set(ML_READY);
    printf("mario launcher row %d: ready\n", row);
}

static void ml_landing(int damage) /* doLanding(msg): damage the msg's (endBindAndPlayerDamageMsg), else the jump */
{
    Player *m = mario;
    if (!ml_bound()) return; /* isPlayerInBind */
    Inst *in = row_inst(mla.row);
    const Row *r = &sc->objs[mla.row];
    if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0);
    if (mario_anim) anim_play(mario_anim, "wallhit", 1.0f, 0, 15); /* !isBindedGround (the shell has no gravity: never) */
    /* MarioActor::endRush (mode 2: tryForceFreeJump), its front the base matrix's Z; ATYPE_MARIO_LAUNCHER_BIND: _3E 1,
     * mJumpVec = _938 (his last bound move) */
    mla.bound = 0; mla.head = mla.pu;
    mario_hidden = 0;
    m->pos = mla.pp;
    m->up = up_at(m->pos, m->up);
    if (!near0(vtangent(mla.pf, m->up))) m->face = vnorm(vtangent(mla.pf, m->up));
    m_force_free_jump(m, vmul(mla.reb, 0.4f)); /* sLandingSpeedRatio */
    if (!damage && !v_near0(mla.reb, 0.001f)) { m->land_type = 1; m_set_jv(m, mla.reb); }
    ml_show_model(in, 1);
    if (in) obj_play(in, "Wait", 1, 0);
    printf("mario launcher row %d: landing at %d (%d %d %d)\n", mla.row, mla.st, (int)m->pos.x, (int)m->pos.y, (int)m->pos.z);
    ml_set(ML_LAND);
}

static void ml_nerve(void)
{
    Player *m = mario;
    Inst *in = row_inst(mla.row);
    const Row *r = &sc->objs[mla.row];
    int st = ++mla.st;
    mla.sight = 0;
    if (!in) return;
    switch (mla.nrv) {
    case ML_READY: {
        if (st == 0) { if (mario_anim) mario_play("earthenpipeready", 1.0f, 0); se_start(SE_PM_MARIO_LAUNCHER_PREP, &mla.lp.x); }
        int fr = anim_clip_info("earthenpipeready", NULL);
        float u = fr > 0 ? fminf((float)st / fr, 1.0f) : 1.0f; /* blendMtx by calcNerveRate(frame max) */
        ml_player(vadd(vmul(mla.p0, 1.0f - u), vmul(mla.lp, u)), vnorm(vadd(vmul(mla.u0, 1.0f - u), vmul(mla.ly, u))),
                  vadd(vmul(mla.f0, 1.0f - u), vmul(mla.lz, u)));
        if (!mario_anim || ((mario_anim->done || !m_clip_is("earthenpipeready")) && st > 0)) ml_set(ML_PLAYERIN); /* isBckStoppedPlayer (a demo may have taken his clip) */
        break;
    }
    case ML_PLAYERIN:
        if (st == 0) { if (mario_anim) mario_play("earthenpipein", 4.0f, 0); mario_se(SE_PV_CATCH); } /* sEarthenPipeInScale */
        if (!mario_anim || ((mario_anim->done || !m_clip_is("earthenpipein")) && st > 0)) { /* doPrep */
            row_turn[mla.row] = 2; /* invalidateHitSensor Bind */
            mla.tp = vadd(mla.lp, vmul(mla.ly, 150.0f)); /* sCameraYOffset */
            if (!mla.up_type) mla.tp = vadd(mla.tp, vmul(mla.lz, 200.0f)); /* sCameraZOffset */
            mario_hidden = 1;
            ml_set(ML_PREP);
        }
        break;
    case ML_PREP: {
        if (st == 10) { obj_play(in, mla.up_type ? "LookUp" : "Active", 0, 0); se_start(SE_OJ_MARIO_LAUNCHER_IN, &mla.lp.x); } /* sStartActiveFrame */
        if (st == 26) se_start(SE_OJ_MARIO_LAUNCHER_BEND, &mla.lp.x); /* sPrepFrame */
        int over = in->anim ? in->anim->done : st > 10 + anim_clip_info(mla.up_type ? "mariolauncher/lookup" : "mariolauncher/active", NULL);
        if (st > 10 && over) { /* doAim */
            se_start(SE_SY_MARIO_LAUNCHER_IN, NULL);
            mla.rx = mla.ry = mla.bx = mla.by = 0.0f; /* CameraDPD::reset */
            camera_prog_end(0);
            ml_aim_camera(); /* startLauncherCamera (0 frames), setLauncherCameraAngle */
            camera_reset();
            ml_player(mla.tp, mla.ly, mla.lz);
            if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
            mla.sight = 1;
            ml_set(ML_AIM);
            { V3 g = up_at(mla.lp, m->up);
              printf("mario launcher row %d: aim (its Y %d %d %d, up %d %d %d x 100)\n", mla.row, (int)(mla.ly.x * 100), (int)(mla.ly.y * 100), (int)(mla.ly.z * 100),
                     (int)(g.x * 100), (int)(g.y * 100), (int)(g.z * 100)); }
        }
        break;
    }
    case ML_AIM: {
        if (st == 0) ml_show_model(in, 0);
        mla.sight = 1; cur_idle = 0; /* mLayout->appearAt(the pointer): shown while it aims */
        mla.lv_aim = se_level(SE_SY_LV_MARIO_LAUNCHER_AIM, NULL, mla.lv_aim);
        int a = pad_pressed(PAD_CROSS) || mla.test_a, b = pad_pressed(PAD_CIRCLE) || mla.test_b;
        mla.test_a = mla.test_b = 0;
        if (a) { /* doEject */
            V3 back = mla.up_type ? vmul(mla.lz, -1.0f) : mla.ly, o, d;
            cursor_ray(&o, &d); /* calcWorldRayDirectionFromScreen(the pointer) */
            mla.sp = mla.tp; mla.sv = vmul(d, 40.0f); /* sEjectSpeed */
            mla.sy = d; /* makeMtxUpFront(shootDir, -back) */
            mla.sz = near0(vtangent(back, d)) ? vnorm(vtangent(mla.lx, d)) : vmul(vnorm(vtangent(back, d)), -1.0f);
            mla.reb = v3(0, 0, 0);
            mario_hidden = 0;
            if (mario_anim) mario_play("cannonflystart", 1.0f, 0);
            camera_shake(SHAKE_STRONG); pad_rumble(RUMBLE_STRONG);
            mla.sight = 0;
            camera_prog_end(0); /* endLauncherCamera, startLauncherFlightCamera(60) */
            ml_flight_camera();
            ml_set(ML_FLIGHT);
            printf("mario launcher row %d: eject %d %d %d (x 1000)\n", mla.row, (int)(d.x * 1000), (int)(d.y * 1000), (int)(d.z * 1000));
            break;
        }
        if (b) { /* doReject */
            if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0);
            ml_player(vadd(mla.lp, vmul(mla.ly, 150.0f)), mla.ly, mla.lz);
            ml_show_model(in, 1);
            obj_play(in, "Wait", 1, 0);
            camera_prog_end(-1); /* endLauncherCamera */
            camera_reset();
            mla.sight = 0;
            ml_set(ML_REJECT);
            printf("mario launcher row %d: reject\n", mla.row);
            break;
        }
        ml_aim_camera();
        break;
    }
    case ML_FLIGHT: {
        if (st == 0) { se_start(SE_PM_MARIO_LAUNCHER_LAUNCH, &mla.lp.x); mario_se(SE_PV_SHOOT_UP); }
        mla.lv_fly = se_level(SE_PM_LV_MARIO_LAUNCHER_FLY, &mla.pp.x, mla.lv_fly);
        if (st == 65) { mario_se(SE_PV_JUMP_JOY); if (mario_anim) anim_play(mario_anim, "cannonflyloop", 1.0f, 1, 15); } /* sLoopStart, sInterpolateTest1 */
        if (st == 300 && mario_anim) anim_play(mario_anim, "diecannonfly", 1.0f, 0, 15); /* sDieStart */
        if (st == 90) ml_show_model(in, 1); /* sDispLaunch */
        if (st >= 360) { /* sKillFrame: forceKillPlayerByAbyss */
            printf("mario launcher row %d: killed in flight\n", mla.row);
            miss_abyss = 1; miss();
            ml_set(ML_KILL);
            if (!miss_running()) { /* a build without the miss events: he is at the start again, the bind over */
                mla.bound = 0; camera_prog_end(-1); row_turn[mla.row] = 1; mla.row = -1;
            }
            break;
        }
        /* doFlight: the shell's binder (sShellRadius 80 at sShellOffset 180 up its base matrix) bound: the landing;
         * else calcBaseMtx (mReboundDir its move), mPlayerMtx the shell's; in water: the landing */
        V3 np = vadd(mla.sp, mla.sv);
        Hit h[8];
        int n = kcl_query(vadd(np, vmul(mla.sy, 180.0f)), 80.0f, h, 8), hit = 0;
        for (int i = 0; i < n; i++)
            if (h[i].inst != (int)(in - insts)) { /* isBreak: its own faces let through */
                if (!hit) printf("mario launcher row %d: bound on %s\n", mla.row, h[i].inst >= 0 && h[i].inst < ninsts ? sc->types + 32 * sc->objs[insts[h[i].inst].row].type : "the map");
                hit = 1;
            }
        if (hit) { ml_landing(0); break; }
        mla.reb = vsub(np, mla.sp);
        mla.sp = np;
        ml_player(mla.sp, mla.sy, mla.sz);
        { float fm[16]; basis_matrix(fm, vadd(mla.sp, vmul(mla.sy, HIT_BODY_H)), mla.sy, mla.sz, 0); fx_named("MarioLauncher", "LocusSmoke", fm, NULL, &mla); }
        ml_flight_camera();
        float dep;
        if (in_water(mla.sp, up_at(mla.sp, m->up), &dep)) ml_landing(0);
        break;
    }
    case ML_KILL: break; /* (the shell stays) */
    case ML_LAND:
        if (st >= 1) { /* sLandFrame: endLauncherFlightCamera */
            camera_prog_end(-1);
            row_turn[mla.row] = 1; /* exeWait: validateHitSensor Bind */
            mla.row = -1;
        }
        break;
    case ML_REJECT:
        if (st == 0) se_start(SE_SY_MARIO_LAUNCHER_OUT, NULL);
        if (st == 18) mario_hidden = 0; /* sRejectFrame - 2: showPlayer */
        if (st == 20) { /* endBindAndPlayerJump(Y x sRejectJumpY - Z x sRejectJumpZ) */
            mla.bound = 0;
            m->pos = mla.pp; m->up = up_at(m->pos, mla.pu);
            if (!near0(vtangent(mla.pf, m->up))) m->face = vnorm(vtangent(mla.pf, m->up));
            m_force_free_jump(m, vsub(vmul(mla.ly, 10.0f), vmul(mla.lz, 10.0f)));
            mario_se(SE_PV_JUMP_S);
        }
        if (st == 30) { row_turn[mla.row] = 1; mla.row = -1; } /* sWaitFrame: validateHitSensor Bind, Wait */
        break;
    }
}

/* each frame: the Bind sensors by SW_A (listenStageSwitchOnOffA: invalid until it is on), Mario's touch, the nerve.
 * row_turn: Bind 0 not set, 1 valid, 2 invalid; row_step: SW_A as last seen */
static void ml_update(void)
{
    Player *m = mario;
    for (int k = 0; k < mla.n; k++) {
        int row = mla.rows[k];
        const Row *r = &sc->objs[row];
        if (!row_turn[row]) { row_turn[row] = r->sw[SW_A] >= 0 ? 2 : 1; row_step[row] = 0; }
        if (r->sw[SW_A] >= 0) {
            int on = sw_on(r, SW_A);
            if (on != row_step[row]) { row_step[row] = (unsigned short)on; if (mla.row != row) row_turn[row] = on ? 1 : 2; } /* onSwitchA / offSwitchA */
        }
        if (mla.row >= 0 || row_turn[row] != 1 || !row_shown(row)) continue;
        Inst *in = row_inst(row);
        if (!in || m->launch >= 0 || pipe_seq || mario_hidden || demo_on || warp.from >= 0 || jb.inst >= 0 || coc.inst >= 0) continue;
        V3 y = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H)), s = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(y, 20.0f)); /* sSensorOffsetY */
        if (vlen(vsub(c, s)) >= 80.0f + HIT_BODY_R || !mode_rush("MarioLauncher")) continue; /* sSensorRadius */
        ml_ready(row);
    }
    if (mla.row >= 0) ml_nerve();
}

/* Mario while bound: his base matrix is mPlayerMtx (ACTMES_UPDATE_BASEMTX) */
static int ml_bind(Player *m)
{
    if (!ml_bound()) return 0;
    m->pos = mla.pp;
    if (!near0(mla.pu)) m->up = vnorm(mla.pu);
    if (!near0(vtangent(mla.pf, m->up))) m->face = vnorm(vtangent(mla.pf, m->up));
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    return 1;
}

/* MarioLauncherLayout: Telescope's Sight at the pointer (appearAt) */
static void ml_draw_sight(void)
{
    if (mla.row >= 0 && mla.sight) lay_draw("Sight", -1, 1.0f, (cur_x - 320.0f) / TK_KX, (224.0f - cur_y) / TK_KY);
}

#ifdef LAUNCHER_TEST
/* test builds: Mario put on the first shown MarioLauncher (XF=-DLAUNCHER_N=k: the k-th), the pointer moved off the
 * middle while it aims, A after 240 frames of aim (XF=-DLAUNCHER_B: B instead) */
#ifndef LAUNCHER_N
#define LAUNCHER_N 0
#endif
int game_test_launcher(void)
{
    static int phase, t;
    if (phase == 0 && game_test_free() && mla.row < 0) {
        int k = 0;
        for (int i = 0; i < mla.n; i++) {
            Inst *in = row_inst(mla.rows[i]);
            if (!in || !row_shown(mla.rows[i]) || k++ != LAUNCHER_N) continue;
            if (row_turn[mla.rows[i]] == 2) row_turn[mla.rows[i]] = 1; /* (its SW_A) */
            V3 y = vnorm(v3(in->m[4], in->m[5], in->m[6]));
            mario->up = up_at(v3(in->m[12], in->m[13], in->m[14]), mario->up);
            mario->pos = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(y, 60.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            printf("launcher test: Mario onto row %d (arg0 %d) at %d %d %d\n", mla.rows[i], (int)sc->objs[mla.rows[i]].arg[0], (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z);
            phase = 1;
        }
    } else if (phase == 1 && mla.row >= 0 && mla.nrv == ML_AIM) {
        cur_x = 400.0f; cur_y = 180.0f;
        if (++t == 240) {
#ifdef LAUNCHER_B
            mla.test_b = 1;
#else
            mla.test_a = 1;
#endif
            phase = 2;
        }
    } else if (phase == 2 && ++t % 30 == 0)
        printf("launcher test: nrv %d st %d bound %d land_type %d grounded %d at %d %d %d\n", mla.row >= 0 ? mla.nrv : -1, mla.st, mla.bound, mario->land_type, mario->grounded,
               (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z);
    return phase * 100 + (mla.row >= 0 ? mla.nrv : 9);
}
#endif
