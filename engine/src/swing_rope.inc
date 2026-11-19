/* swing_rope.inc: SwingRope.cpp / SledRopePoint.cpp after Petari (#included by game.c; swr_*).
 * A rope 100 x scale y long hung from its place: a SwingRopePoint every sPointInterval 50 down its gravity and one
 * SledRopePoint (the hang point: a SwingRopePoint whose updatePos goes through a Binder of radius 50).
 * Stop: still. Free: gravity 1 on every point, restricted head to tail, friction 0.8 (head) .. 0.92 (tail); Stop once
 * every point is under 0.1 a component and upright (0.9999). FreeInvalid (after a jump off): Free until Mario lands.
 * Bind (AutoRushBegin on its binder sensor, 200 at the foot of Mario's place on the rope's line; not on the ground,
 * not swimming, only in Stop / Free): the grab coord his place along the gravity (sHangCoordMin 100 .. length - 200),
 * the hang point set there with his velocity, SE_PV_CATCH, SE_PM_GRAB_OBJ, its actor camera. BindSlideDownStart
 * (SwingRopeSlideDownStart) / BindSlideDown (SwingRopeSlideDown, interp 10): the coord down 0.5 a frame faster to 10
 * until length - 200, then BindStretch; BindStretch <-> BindLoose by the slack (over 5 loose, under 1 stretched).
 * The hang point: the stick x 0.5 (loose 0.8) and SwingRopeAccel / SwingRopeSwingWait / SwingRopeWait, the rebound
 * 0.7 off the map's faces, gravity 0.8 (loose 0.6), restricted to the coord from the base, friction 0.995,
 * SE_OJ_LV_ROPE_SWING_WIND_1, SE_OJ_ROPE_CREAK_H when its speed / 40 passes 20 %. Mario's base matrix is the hang
 * point's. The points above him follow loose (restricted both ways) or are laid straight along the hang point's up
 * (eased in over sStepFixUpperPointPos 10); those below hang from his left foot (FootL + up x -20 + front x 10).
 * A: tryJump (the point's speed along the gravity 25 .. 50, across it at most 35, under 5 the stick x 5;
 * SE_PV_JUMP_S, SE_PM_JUMP_M, SwingRopeSpin, endBindAndPlayerWeakGravityJump, FreeInvalid). Its ride sensor (150, 50
 * under the hang point) takes the items while he rides. Drawn as three strips along the points (SwingRope.bti:
 * slice.py TEX_ONLY), one quad a strip in Stop; a surface circle shadow 50 under its tail (bound: the hang point).
 * ponytail: one SwingRope a scene (the game places one, HeavenlyBeachZone: 28 points); the Binder is one push out of
 * the faces the moved sphere touches, not Binder::bind's stepped move (Binder.cpp); startLevelSound's level
 * parameter is not passed (se_level has none) */
#define SWR_PTS 32
#define SWR_IV 50.0f /* sPointInterval */
enum { SWR_STOP, SWR_FREE, SWR_FREEINVALID, SWR_SLIDESTART, SWR_SLIDE, SWR_STRETCH, SWR_LOOSE };
static struct {
    const Scene *sc;
    int row, nrv, step, n, stretched, stretch_t, accel_t, lvl, nplane;
    float len, coord, slide;
    V3 base, grav, foot, plane[8];
    RopePt p[SWR_PTS], sled;
    u32 lv;
    u16 tex;
} swr;
static int swr_ride; /* Mario on it (mRider) */

static void swr_reset(void) { swr.sc = NULL; swr_ride = 0; }
static void swr_set(int n) { swr.nrv = n; swr.step = -1; }
static void swr_init(void) /* init, initPoints */
{
    swr.sc = sc; swr.row = -1; swr_ride = 0;
    int t = scene_type(sc, "SwingRope");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type != t) continue;
        const Model *md = scene_model(sc, "SwingRope");
        swr.row = (int)i; swr.tex = md && md->vu_ngroups ? md->vu[0].tex : NO_TEX;
        swr.base = row_pos(r); swr.len = r->scale[1] * 100.0f;
        swr.grav = vmul(up_at(swr.base, v3(0, 1, 0)), -1.0f);
        swr.n = (int)(swr.len / SWR_IV + 0.01f); /* (the row's scale comes through its zone's matrix: 13.9999) */
        if (swr.n > SWR_PTS) swr.n = SWR_PTS;
        for (int k = 0; k < swr.n; k++)
            swr.p[k] = (RopePt){vadd(swr.base, vmul(swr.grav, SWR_IV * (k + 1))), v3(0, 0, 0), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1)};
        swr.sled = (RopePt){swr.base, v3(0, 0, 0), v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1)};
        swr.accel_t = swr.lvl = 0; swr.lv = 0; swr.nplane = 0;
        swr_set(SWR_FREE);
        break;
    }
}
static void rp_restrict(RopePt *p, V3 anchor, float len) /* SwingRopePoint::restrict: held only past len */
{
    V3 d = vsub(vadd(p->pos, p->vel), anchor), n;
    if (vlen(d) < 1e-6f) n = vmul(p->up, -1.0f);
    else { n = vnorm(d); p->up = vmul(n, -1.0f); }
    if (vdot(d, d) >= len * len) p->vel = vsub(p->vel, vsub(d, vmul(n, len)));
}
static float swr_no(float coord) /* calcPointNo */
{
    float k = coord / SWR_IV - 1.0f;
    return k < 0.0f ? 0.0f : k > swr.n - 1.0f ? swr.n - 1.0f : k;
}
static float swr_fric(int i) /* calcFriction: sBindPointFrictionRate 0.7 .. 0.8, sFreePointFrictionRate 0.8 .. 0.92 */
{
    return swr_ride ? i * (0.8f - 0.7f) / (swr.n - 1) + 0.7f : i * (0.92f - 0.8f) / (swr.n - 1) + 0.8f;
}
static void swr_gravity(void) /* addPointGravity (sPointGravity 1) */
{
    for (int i = 0; i < swr.n; i++) swr.p[i].vel = vadd(swr.p[i].vel, swr.grav);
}
static void swr_to_tail(int i, V3 anchor, float len) /* restrictPointToTail */
{
    for (; i < swr.n; i++) {
        rp_restrict(&swr.p[i], anchor, len);
        anchor = vadd(swr.p[i].pos, swr.p[i].vel);
        len = SWR_IV;
    }
}
static void swr_to_head(int i, V3 anchor, float len) /* restrictPointToHead */
{
    for (; i >= 0; i--) {
        rp_restrict(&swr.p[i], anchor, len);
        anchor = vadd(swr.p[i].pos, swr.p[i].vel);
        len = SWR_IV;
    }
}
static void swr_free(void) /* exeFree */
{
    swr_gravity();
    swr_to_tail(0, swr.base, SWR_IV);
    V3 f = swr.p[0].front;
    int stop = 1;
    for (int i = 0; i < swr.n; i++) {
        RopePt *p = &swr.p[i];
        rp_update(p, f, swr_fric(i));
        f = p->front;
        if (fabsf(p->vel.x) > 0.1f || fabsf(p->vel.y) > 0.1f || fabsf(p->vel.z) > 0.1f || p->up.y < 0.9999f) stop = 0; /* isAllPointsStop */
    }
    if (stop) {
        for (int i = 0; i < swr.n; i++) swr.p[i].vel = v3(0, 0, 0);
        swr_set(SWR_STOP);
    }
}
static void swr_foot(void) /* updateFootPos (the foot coord it also keeps is read by nothing drawn) */
{
    swr.foot = mario_joint_at(6, v3(0.0f, -20.0f, 10.0f)); /* FootL; sFootPosOffsetY, Z */
}
static int swr_jump(void) /* tryJump */
{
    if (!pad_pressed(PAD_CROSS)) return 0;
    V3 g = swr.grav, front = swr.sled.front;
    V3 jy = vmul(g, vdot(swr.sled.vel, g));
    if (vlen(jy) < 25.0f) jy = vmul(g, -25.0f);      /* sJumpSpeedMinY */
    else if (vlen(jy) > 50.0f) jy = vmul(g, -50.0f); /* sJumpSpeedMaxY */
    V3 jx = vtangent(swr.sled.vel, g);
    if (vlen(jx) > 35.0f) { front = vnorm(jx); jx = vmul(front, 35.0f); } /* sJumpSpeedMaxXZ */
    else if (vlen(jx) < 5.0f && tz_stick_on()) { /* sJumpSpeedMinXZ, sJumpSpeedRate 5 */
        front = tz_world_stick();
        jx = vtangent(vmul(front, 5.0f), g);
    } else if (vlen(jx) > 0.001f) front = vnorm(jx);
    camera_event_end(); /* endActorCameraAtLanding */
    se_start(SE_PV_JUMP_S, &mario->pos.x); se_start(SE_PM_JUMP_M, &mario->pos.x);
    if (mario_anim) mario_play("swingropespin", 1.0f, 0);
    mario->face = front; /* setPlayerFrontTargetVec */
    mario->up = up_at(mario->pos, mario->up);
    m_power_jump(mario, vadd(jx, jy)); /* endBindAndPlayerWeakGravityJump */
    swr_ride = 0;
    swr_set(SWR_FREEINVALID);
    return 1;
}
static void swr_sled_bind(void) /* SledRopePoint::updatePos's Binder(radius 50, 8 planes)::bind(velocity) */
{
    V3 c = vadd(swr.sled.pos, swr.sled.vel);
    swr.nplane = 0;
    kcl_cand_begin(c, SWR_IV + 64.0f);
    for (int it = 0; it < 3; it++) {
        Hit h[8];
        int n = kcl_query(c, SWR_IV, h, 8), d = 0;
        if (!n) break;
        for (int i = 0; i < n; i++) {
            if (h[i].dist > h[d].dist) d = i;
            if (swr.nplane < 8) swr.plane[swr.nplane++] = h[i].n;
        }
        c = vadd(c, vmul(h[d].n, h[d].dist + 0.01f));
    }
    kcl_cand_end();
    swr.sled.pos = c;
}
static void swr_play(const char *clip, int loop, int interp) /* startBckPlayer */
{
    if (!mario_anim) return;
    m_jp = NULL;
    anim_play(mario_anim, clip, 1.0f, loop, interp);
}
static void swr_hang(void) /* updateHangPoint */
{
    RopePt *s = &swr.sled;
    int loose = swr.nrv == SWR_LOOSE;
    V3 stick = tz_world_stick(), front = s->front;
    if (vdot(stick, stick) > 1e-6f) {
        front = vadd(vmul(s->front, 0.9f), vmul(stick, 0.1f)); /* vecBlend, sHangPointBlendRateAxisZ */
        s->vel = vadd(s->vel, vmul(stick, loose ? 0.8f : 0.5f)); /* sHangAccelRateLoose / Stretch */
        if (swr.accel_t == 0) swr_play("swingropeaccel", 0, 10);
        else if (mario_anim && !mario_anim->loop && mario_anim->done) swr_play("swingropeswingwait", 1, 10);
        swr.accel_t++;
    } else if (swr.accel_t > 0) { /* sStepWaitAfterSwing 0 */
        swr_play("swingropewait", 1, 10);
        swr.accel_t = 0;
    }
    for (int i = 0; i < swr.nplane; i++) { /* SledRopePoint::bindToMapCollision(sHangPointReboundRate 0.7) */
        float d = vdot(s->vel, swr.plane[i]);
        if (d < 0.0f) s->vel = vadd(s->vel, vmul(swr.plane[i], -1.7f * d));
    }
    swr.grav = vmul(up_at(s->pos, vmul(swr.grav, -1.0f)), -1.0f);
    s->vel = vadd(s->vel, vmul(swr.grav, loose ? 0.6f : 0.8f)); /* sHangPointGravityLoose / Stretcn */
    rp_restrict(s, swr.base, swr.coord);
    V3 v = s->vel; /* updatePosAndAxis(front, sHangPointFrictionRate): the move through its binder */
    swr_sled_bind();
    s->vel = v3(0, 0, 0);
    rp_update(s, front, 1.0f);
    s->vel = vmul(v, 0.995f);
    swr_foot();
    int lvl = (int)(vlen(s->vel) / 40.0f * 100.0f);
    lvl = lvl > 100 ? 100 : lvl < 0 ? 0 : lvl;
    swr.lv = se_level(SE_OJ_LV_ROPE_SWING_WIND_1, &s->pos.x, swr.lv);
    if (swr.lvl < 20 && 20 <= lvl) se_start(SE_OJ_ROPE_CREAK_H, &s->pos.x);
    swr.lvl = lvl;
}
static void swr_loose_upper(void) /* updateLooseUpperPoints */
{
    swr_gravity();
    swr_to_tail(0, swr.base, SWR_IV);
    int gi = (int)swr_no(swr.coord);
    swr_to_head(gi, swr.sled.pos, swr.coord - (gi + 1) * SWR_IV);
    swr_to_tail(0, swr.base, SWR_IV);
    V3 f = swr.sled.front;
    for (int i = 0; i <= gi; i++) {
        rp_update(&swr.p[i], f, swr_fric(i));
        f = swr.p[i].front;
    }
}
static void swr_stretch_upper(void) /* updateStretchHangUpperPoints */
{
    if (swr.stretch_t < 10) swr_loose_upper(); /* sStepFixUpperPointPos */
    int gi = (int)swr_no(swr.coord);
    V3 f = swr.sled.front;
    for (int i = 0; i <= gi; i++) {
        RopePt *p = &swr.p[i];
        V3 pos = vadd(swr.base, vmul(swr.sled.up, -SWR_IV * (i + 1)));
        if (swr.stretch_t < 10) {
            float t = sinf((float)(swr.stretch_t + 1) / 10.0f * 3.14159265f / 2.0f); /* getEaseOutValue */
            pos = vadd(vmul(pos, t), vmul(p->pos, 1.0f - t));
        }
        p->pos = pos; p->vel = v3(0, 0, 0); p->up = swr.sled.up; /* setAndUpdatePosAndAxis */
        rp_update(p, f, 1.0f);
        f = p->front;
    }
}
static void swr_hang_lower(void) /* updateHangLowerPoints, updateHangLowerPointPos */
{
    const RopePt *s = &swr.sled;
    int ni = (int)swr_no(swr.coord) + 1;
    if (ni > swr.n - 1) return;
    swr.p[ni] = (RopePt){swr.foot, v3(0, 0, 0), s->side, s->up, s->front};
    swr_to_tail(ni + 1, swr.foot, SWR_IV);
    for (int i = ni + 1; i < swr.n; i++) {
        V3 f = vadd(vmul(swr.p[i].front, 0.7f), vmul(swr.p[i - 1].front, 0.3f)); /* sFootPointAxisBlendRate */
        rp_update(&swr.p[i], f, swr_fric(i));
    }
    if (ni + 1 > swr.n - 1) return; /* (the coord stops 200 short of the tail: never) */
    V3 up = vnorm(vadd(vmul(s->up, 0.5f), vmul(swr.p[ni + 1].up, 0.5f))); /* sPointBlendRateAxisZ */
    V3 front = vnorm(vadd(vmul(s->front, 0.5f), vmul(swr.p[ni + 1].front, 0.5f)));
    V3 side = vcross(up, front);
    if (vlen(side) > 0.001f) { /* makeAxisUpFront */
        side = vnorm(side);
        swr.p[ni].side = side; swr.p[ni].up = up; swr.p[ni].front = vcross(side, up);
    }
}
static int swr_stretch(void) /* updateStretch */
{
    if (swr_jump()) return 1;
    swr_hang();
    swr_gravity();
    swr_stretch_upper();
    swr_hang_lower();
    swr.stretch_t++;
    return 0;
}
static int swr_loose(void) /* updateLoose */
{
    if (swr_jump()) return 1;
    swr_hang();
    swr_loose_upper();
    swr_hang_lower();
    return 0;
}
static int swr_is_stretched(void) /* isStretched: sCoordLooseToStretch */
{
    return fabsf(swr.coord - vlen(vsub(swr.base, swr.sled.pos))) < 1.0f;
}
static int swr_slide_down(void) /* updateSlideDown */
{
    float max = swr.len - 200.0f; /* sHangCoordMaxOffset */
    swr.slide -= 0.5f; /* sHangPointDownAccel */
    if (swr.slide < -10.0f) swr.slide = -10.0f; /* sHangPointDownSpeedMax */
    swr.coord -= swr.slide;
    swr.coord = swr.coord < 100.0f ? 100.0f : swr.coord > max ? max : swr.coord; /* sHangCoordMin */
    if (swr.stretched) {
        if (swr_stretch()) return 1;
    } else {
        if (swr_loose()) return 1;
        if (swr_is_stretched()) { swr.stretched = 1; swr.stretch_t = 0; }
    }
    if (swr.coord >= max) {
        swr.coord = max; swr.slide = 0.0f;
        swr_set(SWR_STRETCH);
        return 1;
    }
    return 0;
}
static void swr_step(void) /* the Bind nerves: with Mario (swr_hold) */
{
    int st = ++swr.step;
    switch (swr.nrv) {
    case SWR_SLIDESTART:
        if (st == 0) { swr_play("swingropeslidedownstart", 0, 0); swr.slide = 0.0f; }
        if (!swr_slide_down() && (!mario_anim || mario_anim->done)) swr_set(SWR_SLIDE);
        break;
    case SWR_SLIDE:
        if (st == 0) swr_play("swingropeslidedown", 1, 10); /* sAnimInterpoleFrame */
        swr_slide_down();
        break;
    case SWR_STRETCH:
        if (st == 0) swr.stretch_t = 0;
        if (!swr_stretch() && swr.coord - vlen(vsub(swr.base, swr.sled.pos)) > 5.0f) swr_set(SWR_LOOSE); /* sCoordStretchToLoose */
        break;
    case SWR_LOOSE:
        if (!swr_loose() && swr_is_stretched()) swr_set(SWR_STRETCH);
        break;
    }
}
#ifdef SWINGROPE_TEST
/* plain build XF=-DSWINGROPE_TEST (HeavenlyBeachGalaxy/1): Mario dropped by the rope's middle at frame 1000 (and after
 * each jump off, once landed 200 frames), the stick held from 200 frames after the bind, A at SWR_TEST_A (XF=-DSWR_TEST_A=1500: a long ride) */
#ifndef SWR_TEST_A
#define SWR_TEST_A 500
#endif
static void swr_test(void)
{
    static int bound_at = -1, off_at = -1000;
    static const char *const nn[] = {"Stop", "Free", "FreeInvalid", "SlideDownStart", "SlideDown", "Stretch", "Loose"};
    int f = (int)game_frame;
    if (f < 1000) return;
    test_nolost = 1;
#if SWINGROPE_TEST + 0 == 2 /* XF=-DSWINGROPE_TEST=2: the camera on the rope from its side */
    {
        V3 mid = vadd(swr.base, vmul(swr.grav, swr.len * 0.6f));
        camera_set_override(1, vadd(mid, v3(500.0f, 200.0f, 1800.0f)), mid, vmul(swr.grav, -1.0f), 45.0f);
    }
#endif
    if (!swr_ride && bound_at < 0 && f - off_at >= 400 && f % 200 == 0) {
        mario->pos = vadd(vadd(swr.base, vmul(swr.grav, swr.len * 0.4f)), v3(120.0f, 0.0f, 0.0f));
        mario->up = up_at(mario->pos, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        camera_start_pos_end();
        printf("swingrope test f %d: Mario dropped by the rope%c", f, 10);
    }
    if (swr_ride && bound_at < 0) bound_at = f;
    if (!swr_ride && bound_at >= 0) { off_at = f; bound_at = -1; pad_force = 0; pad_force_stick = 0; printf("swingrope test f %d: off, jump %.1f %.1f %.1f%c", f, mario->hvel.x, mario->vup, mario->hvel.z, 10); }
    if (swr_ride) {
        int t = f - bound_at;
        pad_force_stick = t >= 200 && t < SWR_TEST_A; pad_force_lx = 1.0f; pad_force_ly = 0.0f;
        pad_force = t == SWR_TEST_A ? PAD_CROSS : 0;
    }
    if (f % 20 == 0)
        printf("swingrope test f %d: %s step %d ride %d coord %.0f sled (%d %d %d) speed %.1f tail (%d %d %d) clip %s shown %d tex %d%c", f, nn[swr.nrv],
               swr.step, swr_ride, swr.coord, (int)swr.sled.pos.x, (int)swr.sled.pos.y, (int)swr.sled.pos.z, vlen(swr.sled.vel),
               (int)swr.p[swr.n - 1].pos.x, (int)swr.p[swr.n - 1].pos.y, (int)swr.p[swr.n - 1].pos.z,
               mario_anim && mario_anim->clip_id >= 0 ? sc->clips[mario_anim->clip_id].name : "-", row_shown(swr.row), swr.tex, 10);
}
#endif
static void swr_update(void)
{
    if (swr.sc != sc) swr_init();
    if (swr.row < 0 || !row_shown(swr.row)) return;
#ifdef SWINGROPE_TEST
    swr_test();
#endif
    if (swr_ride) return; /* its nerve runs with Mario (swr_hold) */
    swr.step++;
    if (swr.nrv == SWR_FREE || swr.nrv == SWR_FREEINVALID) {
        int inv = swr.nrv == SWR_FREEINVALID;
        swr_free();
        if (inv && mario->grounded) swr_set(SWR_FREE); /* exeFreeInvalid: isOnGroundPlayer */
    }
    /* receiveOtherMsg AutoRushBegin: its binder sensor (200) at the foot of Mario's place on the rope's line
     * (updateHitSensor: calcPerpendicFootToLineInside) */
    if ((swr.nrv != SWR_STOP && swr.nrv != SWR_FREE) || mario->grounded || swimming || tz_ride >= 0 || mario->launch >= 0 ||
        pole_ride >= 0 || wb_ride >= 0) return;
    float t = vdot(vsub(mario->pos, swr.base), swr.grav) / swr.len;
    V3 sp = vadd(swr.base, vmul(swr.grav, swr.len * (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t)));
    if (vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), sp)) >= 200.0f + HIT_BODY_R || !mode_rush("SwingRope")) return;
    float max = swr.len - 200.0f, c;
    swr.stretched = 0;
    swr.grav = vmul(up_at(mario->pos, vmul(swr.grav, -1.0f)), -1.0f);
    c = vdot(vsub(mario->pos, swr.base), swr.grav); /* his foot on the line, along the gravity */
    swr.coord = c < 100.0f ? 100.0f : c > max ? max : c;
    swr.sled.front = fabsf(mario->pos.y - swr.base.y) < 1.0f ? v3(0, -1, 0) : mario->face; /* sTransDiffMinY */
    rp_set_info(&swr.sled, mario->pos, vadd(mario->hvel, vmul(mario->up, mario->vup)), swr.base, swr.coord);
    swr_foot();
    swr_ride = 1;
    mario->hip = 0; mario->spin_timer = 0; mario->jumping = 0; mario->hvel = v3(0, 0, 0); mario->vup = 0;
    se_start(SE_PV_CATCH, &mario->pos.x); se_start(SE_PM_GRAB_OBJ, &mario->pos.x);
    if (row_camset[swr.row] != -1) { /* startActorCameraNoTarget */
        int ok = camera_actor_start("SwingRope", "", (unsigned short)row_camset[swr.row], mario->up, mario->face, mario->pos);
        (void)ok;
#ifdef SWINGROPE_TEST
        printf("swingrope test: actor camera set %d started %d%c", row_camset[swr.row], ok, 10);
#endif
    }
    swr_set(SWR_SLIDESTART);
}
/* while Mario rides: its nerve, then his base matrix (updateRideMtx: the hang point's side, up, front, position) */
static int swr_hold(Player *m)
{
    if (!swr_ride) return 0;
    swr_step();
    if (!swr_ride) return 0;
    m->pos = swr.sled.pos; m->up = swr.sled.up; m->face = swr.sled.front;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    return 1;
}
/* attackSensor: its ride sensor "body" (150, sSensorRiderOffsetY -50 along the hang point's up) takes the items */
static float swr_reach(void) { return swr_ride ? 150.0f + 55.0f : 0.0f; }
static V3 swr_body(void) { return vadd(swr.sled.pos, vmul(swr.sled.up, -50.0f)); }

static void swr_strip(float x1, float y1, float x2, float y2) /* drawLine / drawLineAtHanging; Stop: drawPolygon */
{
    float pos[(SWR_PTS + 1) * 2][3], uv[(SWR_PTS + 1) * 2][2];
    u16 idx[SWR_PTS * 6];
    int stop = swr.nrv == SWR_STOP, n = stop ? 1 : swr.n;
    for (int i = 0; i <= n; i++) {
        const RopePt *q = &swr.p[stop || i == 0 ? 0 : i - 1];
        V3 c = i == 0 ? swr.base : stop ? v3(swr.base.x, swr.base.y - swr.len, swr.base.z) : q->pos;
        float tv = stop ? i * (swr.len / SWR_IV) * 0.13f : 0.13f * i; /* sTexRate */
        for (int e = 0; e < 2; e++) {
            V3 p = vadd(c, vadd(vmul(q->side, e ? x2 : x1), vmul(q->front, e ? y2 : y1)));
            pos[i * 2 + e][0] = p.x; pos[i * 2 + e][1] = p.y; pos[i * 2 + e][2] = p.z;
            uv[i * 2 + e][0] = (float)e; uv[i * 2 + e][1] = tv;
        }
    }
    for (int i = 0; i < n; i++) {
        u16 a = (u16)(i * 2), *x = &idx[i * 6];
        x[0] = a; x[1] = (u16)(a + 1); x[2] = (u16)(a + 2); x[3] = (u16)(a + 1); x[4] = (u16)(a + 2); x[5] = (u16)(a + 3);
    }
    render_mesh_c((const float (*)[3])pos, (const float (*)[2])uv, (n + 1) * 2, idx, n * 2, swr.tex, 3, 0x80808080u);
}
static void swr_draw(void)
{
    if (swr.sc != sc || swr.row < 0 || !row_shown(swr.row)) return;
    /* initShadowSurfaceCircle(50) at mShadowDropPos: under the tail (bound: the hang point), at the rope's foot height */
    const V3 *q = swr_ride ? &swr.sled.pos : &swr.p[swr.n - 1].pos;
    draw_shadow(v3(q->x, swr.base.y - swr.len, q->z), vmul(swr.grav, -1.0f), swr.p[0].front, 50.0f, 50.0f, 0.0f, 50.0f, 1000.0f);
    if (swr.tex == NO_TEX) return; /* (a pack sliced before SwingRope was TEX_ONLY) */
    /* sDrawWidthX 30, Z 40, sDrawWidthLongOffsetX 13, ShortOffsetX 3, ShortOffsetZ 3; the colours all white */
    swr_strip(-30.0f, 43.0f, 33.0f, -43.0f);
    swr_strip(-33.0f, -43.0f, 30.0f, 43.0f);
    swr_strip(43.0f, -3.0f, -43.0f, -3.0f);
}
