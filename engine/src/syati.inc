/* syati.inc (#included by game.c after race.inc: sy_*): NPC/Syati.cpp and MapObj/PrizeRing.cpp after Petari.
 * Syati (the ring mission's: a rail) waits (WaitReverse / WaitDeepSea) till its SW_A (the SwitchCube by it), then its
 * 開始デモ (requestStartDemoMarioPuppetable): the wipe closes, Mario put at sMarioMoveLocalOffsetRing / DeepSea off its
 * placement (PlayerPoseSetterInWater: SwimWait, his up to 450 over it, his front its gravity), 30 frames, the wipe
 * opens on its 会話 camera, the forced talk (sTalkOffset balloon), ReadyToStart (resetScore, the 開始デモ camera,
 * TurnReverse, SE_SM_SYATI_TURN), CountDown (Swim at 0.5, RaceManagerLayout's count and go without the record panes,
 * SE_SY_RACE_COUNT_DOWN every 60, then endDemo and SE_SY_RACE_START), Swim along its rail (point arg1 its speed, 3 when
 * Mario is 5000 off; the pose slerped 0.04 to 600 ahead with a roll by the turn), EmitRing (Screw, SE_SM_SYATI_ROLL)
 * 600 ahead when within 600 of a next point whose arg0 is 0, its PrizeRings numbered by those left; each passed ring
 * (Mario's touch within 300) a life and its number SE; all of them: WaitStarAppeared (Star, the power star from its
 * Head joint, killed once out); the rail's end first: Turn, WaitAllRingDisappear, its 再挑戦デモ (the wipes, Failure,
 * the forced retry talk, forceKillPlayerByWaterRace: 水中レース負け). Without a rail (TalkSyati): HideOnShore, then
 * (arg1 0) WaitOnShore (Delight) once the stage's star is out or (arg1 1) WaitTalkNormal / TalkNormal (StartWait /
 * StartTalk within its talk distance 700), its talk near Mario. Its eyes: Open, Blink every 120..240 + its length.
 * Its head, body and tail sensors (200 / 230 / 150) flip Mario (FlipMaximum, SE_SM_SYATI_TRAMPLED).
 * PrizeRing: Start (SE_OJ_PRIZE_RING_APPEAR, Start), Loop (its life: 800, arg3), Timeout (Timeout.brk's 321 frames,
 * SE_OJ_PRIZE_RING_BLINK at its flash frames, SE_OJ_PRIZE_RING_DISAPPEAR), Pass (End, a middle rumble, the number's
 * system SE, SE_SY_TOTAL_COMPLETE for the last), hidden till killed; Number.bva at the number - 1.
 * Its Head joint turned to Mario on the shore (calcHeadJoint: sy_head_turn); its sensors in the HitSensor list and
 * pushing the walkers' (sendMsgPush); OceanPhantomCave's retry: the stage BGM stopped / MBGM_GALAXY_05.
 * ponytail: no BTK / BRK colours on these models (PrizeRing at Loop's frame 0: slice.py BRK_POSE; the BRK's lengths
 * kept), no flat-model shadow volume (ShadowVolumeOval: no model volume, as Snakehead's), tryPlayerKillTakingActor
 * (the port's damageDropThrowMemoSensor is empty: drop_carried), pushes only to the walkers (their receiveMsgPush) */
static int sy_bgm_05(void) /* isPlayingStageBgmName("STM_GALAXY_05") */
{
    const char *t = bgm_track("STM_GALAXY_05");
    return bgm_stage_playing() && t && !strcmp(bgm_cur, t);
}
enum { SY_WAIT, SY_FADEOUT_START, SY_BLANK_START, SY_FADEIN_START, SY_TALK_START, SY_READY, SY_COUNT, SY_SWIM, SY_EMIT,
       SY_STAR, SY_END, SY_WAIT_RINGS, SY_FADEOUT_RETRY, SY_BLANK_RETRY, SY_FADEIN_RETRY, SY_TALK_RETRY, SY_KILL, SY_HIDE,
       SY_WAIT_SHORE, SY_WAIT_TALK, SY_TALK_NORMAL, SY_DEMO_START };
enum { PR_DEAD, PR_START, PR_LOOP, PR_TIMEOUT, PR_READY_PASS, PR_PASS, PR_READY_KILL };
#define SY_MAX 4
typedef struct { int nrv, step, num, life, hidden; V3 pos, nf, nu; Anim a; } PRing;
typedef struct {
    int row, ring, nrv, step, next, req, swim, hide, nrings, life, count, curpt, blink, k, cam, layout, took, dead, shown, sensors;
    const char *req_demo, *eye;    /* the demo its request waits for; its eyes' BVA (Open / Blink) */
    float eye_f;
    RmRail rail;
    V3 pos, pos0, grav, v9c, off; /* mPosition, the pose setter's _0, mGravity, _9C, mMarioMoveLocalOffsetVec */
    Quat q, q0;                   /* _8C, the pose setter's _C */
    float bal[16];                /* mBalloonFollowMtx */
    u32 lv;
} Syati;
static Syati sys[SY_MAX];
static int nsys;
static const Scene *sy_scene;
static PRing sy_rings[5]; /* (one ring mission a scenario: the first Syati's group) */
static int sy_miss_water;

static int sy_took(int row) { for (int i = 0; sy_scene == sc && i < nsys; i++) if (sys[i].row == row) return sys[i].took; return 0; }
static Inst *sy_inst(const Syati *s) { return row_inst(s->row); }
static void sy_set(Syati *s, int n) { s->next = n; }
static int sy_is(const Syati *s, int n) { return s->nrv == n; }
static void sy_bck(Syati *s, const char *clip, int interp) /* startBck(WithInterpole) */
{
    Inst *in = sy_inst(s);
    char n[48];
    if (!in || !in->anim) return;
    snprintf(n, sizeof n, "%s/%s", sc->types + 32 * sc->objs[s->row].type, clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    anim_start(in->anim, n, interp);
    in->anim->speed = 1.0f;
}
static int sy_bck_stopped(const Syati *s) { const Inst *in = sy_inst(s); return !in || !in->anim || (!in->anim->loop && in->anim->done); }
static void sy_bck_rate(Syati *s, float r) { Inst *in = sy_inst(s); if (in && in->anim) in->anim->speed = r; }
static void sy_request(Syati *s, const char *demo, int n) /* requestStartDemoMarioPuppetable: WaitDemoStart till it starts */
{
    if (s->req >= 0) return;
    s->req = n; s->req_demo = demo;
    sy_set(s, SY_DEMO_START);
}
static void sy_cam(Syati *s, const char *ev, int on) /* startMultiActorCameraTargetSelf / endMultiActorCamera */
{
    if (row_camset[s->row] == -1) return;
    V3 x, y, z;
    q_axes(s->q, &x, &y, &z);
    if (on) s->cam = camera_actor_start(sc->types + 32 * sc->objs[s->row].type, ev, (unsigned short)row_camset[s->row], y, z, s->pos);
    else if (s->cam) { camera_event_stop(); s->cam = 0; }
}
static void sy_mtx(const Syati *s, float m[16], Quat q, V3 pos) /* the quaternion's axes and pos */
{
    V3 x, y, z;
    q_axes(q, &x, &y, &z);
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z; m[4] = y.x; m[5] = y.y; m[6] = y.z; m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
    (void)s;
}
static V3 sy_xform(const float m[16], V3 v) { return v3(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12], m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13], m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]); }
static void sy_balloon(Syati *s, V3 off) /* setupBalloonFollowMtx */
{
    sy_mtx(s, s->bal, s->q, s->pos);
    V3 p = sy_xform(s->bal, off);
    s->bal[12] = p.x; s->bal[13] = p.y; s->bal[14] = p.z;
}
static void sy_pose_mario(Syati *s) /* PlayerPoseSetterInWater::update: Mario at its offset, his up to 450 over it */
{
    float m[16];
    sy_mtx(s, m, s->q0, s->pos0);
    V3 p = sy_xform(m, s->off), up = vsub(vadd(s->pos0, vmul(v3(m[4], m[5], m[6]), 450.0f)), p);
    rm_player_at(p, up, vmul(up_at(mario->pos, mario->up), -1.0f)); /* makeMtxUpFrontPos(forward, getPlayerGravity) */
}
static void sy_mario_bck(const char *clip, const char *demo) { rm.bck = clip; rm.bck_demo = demo; } /* startBckPlayer while its demo holds him */
static void sy_blink(Syati *s) /* updateBlink */
{
    if (--s->blink < 0) { s->eye = "syati/blink"; s->eye_f = 0; s->blink = 9 + 120 + rand() % 121; } /* (Blink.bva's end) + getRandom(120, 240) */
    else if (!strcmp(s->eye, "syati/blink") && s->eye_f >= 9.0f) s->eye = "syati/open";
}
static int sy_bva(const Inst *in, const char **key, float *frame) /* its eyes (startBva Open / Blink) */
{
    for (int i = 0; sy_scene == sc && i < nsys; i++)
        if (sys[i].row == in->row) { *key = sys[i].eye; *frame = sys[i].eye_f; return 1; }
    return 0;
}

/* --- PrizeRing */
static void prg_set(PRing *p, int n) { p->nrv = n; p->step = 0; }
static void prg_bck(PRing *p, const char *clip) { char n[40]; snprintf(n, sizeof n, "prizering/%s", clip); anim_start(&p->a, n, 0); }
static int prg_passed(const PRing *p) { return p->nrv == PR_READY_PASS || p->nrv == PR_PASS || p->nrv == PR_READY_KILL; } /* isPassed */
static void prg_sound(const PRing *p) /* playSound: the number's system SE */
{
    static const u32 se[5] = {0, SE_SY_GET_PRIZE_RING_5, SE_SY_GET_PRIZE_RING_4, SE_SY_GET_PRIZE_RING_3, SE_SY_GET_PRIZE_RING_2};
    se_start(p->num < 5 && se[p->num] ? se[p->num] : SE_SY_GET_PRIZE_RING_1, NULL);
    if (p->num == 1) se_start(SE_SY_TOTAL_COMPLETE, NULL);
}
static void prg_exe(PRing *p)
{
    static const float flash[9] = {10, 60, 110, 160, 210, 240, 270, 300, -1}; /* sOnTriggerFlashFrames */
    int st = p->step;
    switch (p->nrv) {
    case PR_START:
        if (st == 0) { se_start(SE_OJ_PRIZE_RING_APPEAR, &p->pos.x); prg_bck(p, "start"); }
        if (st > 0 && p->a.done) prg_set(p, PR_LOOP);
        break;
    case PR_LOOP:
        if (st == 0) prg_bck(p, "loop");
        if (st == p->life) prg_set(p, PR_TIMEOUT);
        break;
    case PR_TIMEOUT: /* Timeout.brk: 321 frames */
        for (int i = 0; i < 9; i++) if ((float)st <= flash[i] && flash[i] < (float)st + 1.0f) se_start(SE_OJ_PRIZE_RING_BLINK, &p->pos.x);
        if (st >= 321) { se_start(SE_OJ_PRIZE_RING_DISAPPEAR, &p->pos.x); prg_set(p, PR_DEAD); return; }
        break;
    case PR_PASS:
        if (st == 0) { prg_bck(p, "end"); pad_rumble(RUMBLE_MIDDLE); prg_sound(p); }
        if (st > 0 && p->a.done) prg_set(p, PR_READY_KILL);
        break;
    case PR_READY_KILL:
        if (st == 0) p->hidden = 1;
        break;
    }
    if (p->nrv != PR_DEAD) p->step++;
}

/* --- Syati */
static void sy_kill_rings(void) { for (int i = 0; i < 5; i++) sy_rings[i].nrv = PR_DEAD; }
static int sy_rings_alive(void) { int n = 0; for (int i = 0; i < 5; i++) n += sy_rings[i].nrv != PR_DEAD; return n; }
static PRing *sy_dead_ring(void) { for (int i = 0; i < 5; i++) if (sy_rings[i].nrv == PR_DEAD) return &sy_rings[i]; return NULL; }
static V3 sy_rail_at(const Syati *s, float c, V3 *dir) { V3 d; V3 p = nrail_at(s->rail.pts, s->rail.np, s->rail.cl, c, &d); if (dir) *dir = d; return p; }
static void sy_sync_numbers(Syati *s) /* syncNumRingLeftToActiveRings */
{
    for (int i = 0; i < 5; i++) if (sy_rings[i].nrv != PR_DEAD && !prg_passed(&sy_rings[i])) sy_rings[i].num = s->nrings - s->count;
}
static void sy_reset_score(Syati *s) /* resetScore */
{
    s->count = 0; s->curpt = -1;
    if (s->k >= 0) talk_reset_node(s->k);
    sy_kill_rings();
    s->rail.coord = rr_point_coord(&s->rail, 0); /* moveCoordToRailPoint 0 */
}
static int sy_ready_to_emit(const Syati *s) /* isReadyToEmitRing */
{
    if (s->count == s->nrings || !sy_dead_ring()) return 0;
    int cur = rr_point_no(&s->rail), n = rr_npoints(&s->rail), nx = cur + 1 < n ? cur + 1 : (s->rail.cl ? 0 : cur);
    if (cur == s->curpt) return 0;
    float d = rr_point_coord(&s->rail, nx) - s->rail.coord; /* calcDistanceToNextRailPoint */
    if (nx == 0 && s->rail.cl) d = s->rail.len - s->rail.coord;
    if (600.0f < d) return 0;
    return rr_point_arg(&s->rail, nx, 0) == 0; /* the next point's arg0 */
}
static void sy_emit(Syati *s) /* emitRing: 600 ahead on its rail, upright to its gravity, numbered by those left */
{
    PRing *p = sy_dead_ring();
    if (!p) return;
    V3 d, at = sy_rail_at(s, s->rail.coord + 600.0f, &d);
    p->pos = at; p->nu = vmul(s->grav, -1.0f); /* calcMtxFromGravityAndZAxis */
    p->nf = vnorm(vtangent(d, p->nu));
    if (vlen(p->nf) < 1e-3f) p->nf = d;
    const Model *md = scene_model(sc, "PrizeRing");
    if (md && md->skin && p->a.skin != md->skin) anim_bind(&p->a, md->skin);
    p->hidden = 0; p->life = 800; prg_set(p, PR_START); /* appear (mLifeTime 800) */
    p->num = s->nrings - s->count;
    if (s->life > 0) p->life = s->life;
    s->curpt = rr_point_no(&s->rail);
    printf("syati row %d: ring %d out at %d %d %d" "\n", s->row, p->num, (int)at.x, (int)at.y, (int)at.z);
}
static void sy_update_rings(Syati *s) /* updateNumRingPassed (control) */
{
    int passed = 0;
    for (int i = 0; i < 5; i++) {
        PRing *p = &sy_rings[i];
        if (p->nrv == PR_DEAD) continue;
        if (p->nrv == PR_READY_PASS) {
            prg_set(p, PR_PASS); passed = 1; s->count++;
            if (life < max_life) life++; /* incPlayerLife(1) */
            printf("syati row %d: ring passed, %d of %d" "\n", s->row, s->count, s->nrings);
        }
        if (p->nrv != PR_READY_KILL) continue;
        p->nrv = PR_DEAD;
        if (s->count == s->nrings) { sy_kill_rings(); sy_set(s, SY_STAR); }
    }
    if (passed && s->count != s->nrings) sy_sync_numbers(s);
}
static void sy_pose_by_rail(Syati *s) /* updatePoseByRail */
{
    V3 dir, rd;
    V3 a = sy_rail_at(s, s->rail.coord + 600.0f, NULL), b = sy_rail_at(s, s->rail.coord, &rd);
    dir = vsub(a, b);
    V3 rp = vnorm(vtangent(rd, s->grav)), dp = vtangent(dir, s->grav);
    dp = vlen(dp) > 1e-6f ? vnorm(dp) : rp;
    s->v9c = vadd(vmul(s->v9c, 0.96f), vmul(dp, 0.04f)); /* vecBlend(_9C, dp, 0.04) */
    V3 axis = vcross(rp, s->v9c);
    axis = vlen(axis) > 1e-6f ? vnorm(axis) : v3(0, 0, 0);
    Quat roll = q_ident();
    V3 up = vmul(s->grav, -1.0f);
    float dot = vdot(rp, s->v9c), rad = acosf(fminf(fmaxf(dot, -1.0f), 1.0f));
    if (dot < 1.0f) {
        float d2 = vdot(axis, s->grav);
        if (d2 < 0.0f) roll = q_axis(v3(0, 0, 1), -rad); /* setEulerZ */
        else if (d2 > 0.0f) roll = q_axis(v3(0, 0, 1), rad);
    }
    if (vlen(vcross(vnorm(dir), up)) > 1e-4f || vdot(dir, up) < 0) { /* !isSameDirection */
        Quat r = q_mul(q_front_up(dir, up), roll);
        s->q = q_slerp(s->q, r, 0.04f);
    }
}
static void sy_swim_common(Syati *s) /* updateSwimCommon */
{
    int sp = rr_point_arg(&s->rail, rr_point_no(&s->rail), 1); /* getRailPointArg1NoInit (-1 none) */
    if (vlen(vsub(s->pos, mario->pos)) >= 5000.0f) sp = 3;
    s->rail.speed = (float)sp; s->rail.dir = 1;
    rr_move(&s->rail); /* moveCoordAndFollowTrans */
    s->pos = sy_rail_at(s, s->rail.coord, NULL);
    sy_pose_by_rail(s);
    if (s->layout && !menu_race_go_playing()) { menu_race_kill(); s->layout = 0; }
    s->lv = se_level(SE_SM_LV_SYATI_SWIM, &s->pos.x, s->lv);
}
static void sy_head(const Syati *s, V3 *p, int joint, V3 off) /* a joint's place (copyJointPos / its sensor) */
{
    const Inst *in = sy_inst(s);
    *p = s->pos;
    if (!in || !in->anim || !in->anim->skin || joint >= in->anim->skin->njoints) return;
    const float *w = in->anim->world[joint];
    V3 l = v3(w[0] * off.x + w[4] * off.y + w[8] * off.z + w[12], w[1] * off.x + w[5] * off.y + w[9] * off.z + w[13], w[2] * off.x + w[6] * off.y + w[10] * off.z + w[14]);
    *p = sy_xform(in->m, l);
}
static void sy_exe(Syati *s)
{
    int st = s->step, ring = s->swim != 0;
    switch (s->nrv) {
    case SY_WAIT:
        if (st == 0) { sy_bck(s, ring ? "waitreverse" : "waitdeepsea", 0); s->eye = "syati/open"; }
        if (sw_on(&sc->objs[s->row], SW_A)) sy_request(s, "開始デモ", SY_FADEOUT_START);
        break;
    case SY_FADEOUT_START: case SY_FADEOUT_RETRY: /* exeFadeoutBeforeTalk */
        if (st == 0) { menu_wipe(1, -1, 0); break; }
        if (!menu_wipe_active()) sy_set(s, s->nrv == SY_FADEOUT_START ? SY_BLANK_START : SY_BLANK_RETRY);
        break;
    case SY_BLANK_START: case SY_BLANK_RETRY: /* exeWaitBlank */
        if (st == 0) {
            s->off = ring ? v3(6.0f, -33.0f, -776.0f) : v3(6.0f, -176.0f, -590.0f); /* sMarioMoveLocalOffsetRing / DeepSea */
            sy_pose_mario(s);
            sy_mario_bck("swimwait", s->req_demo);
            s->q = s->q0; /* makeQuatFromRotate */
        }
        if (st == 30) sy_set(s, s->nrv == SY_BLANK_START ? SY_FADEIN_START : SY_FADEIN_RETRY);
        break;
    case SY_FADEIN_START: case SY_FADEIN_RETRY: /* exeFadeinBeforeTalk */
        if (st == 0) {
            menu_wipe(0, -1, 0); sy_cam(s, "会話", 1); sy_bck(s, "talk", 0);
            if (s->nrv == SY_FADEIN_RETRY && on_stage("OceanPhantomCaveGalaxy") && !sy_bgm_05()) bgm_stop(60); /* stopStageBGM(0x3C) */
        }
        sy_pose_mario(s);
        if (!menu_wipe_active()) sy_set(s, s->nrv == SY_FADEIN_START ? SY_TALK_START : SY_TALK_RETRY);
        break;
    case SY_TALK_START: /* exeTalkStartMission */
        if (st == 0) sy_balloon(s, ring ? v3(16.0f, 192.0f, -300.0f) : v3(47.0f, 179.0f, -300.0f)); /* sTalkOffset / DeepSea */
        sy_blink(s);
        sy_pose_mario(s);
        if (talk_force_at_end(s->k, 0)) { sy_cam(s, "会話", 0); sy_set(s, SY_READY); }
        break;
    case SY_READY: /* exeReadyToStart */
        if (st == 0) {
            sy_reset_score(s);
            sy_cam(s, "開始デモ", 1);
            sy_bck(s, "turnreverse", 0); s->eye = "syati/open";
            se_start(SE_SM_SYATI_TURN, &s->pos.x);
            sy_mario_bck("swimwait", "開始デモ");
        }
        sy_pose_mario(s);
        if (st > 0 && sy_bck_stopped(s)) sy_set(s, SY_COUNT);
        break;
    case SY_COUNT: /* exeCountDown */
        if (st == 0) {
            sy_bck(s, "swim", 0);
            menu_race_appear(); s->layout = 1;
            menu_race_count_and_go();
            menu_race_hide_records();
        }
        if (st % 60 == 0) se_start(SE_SY_RACE_COUNT_DOWN, NULL);
        sy_bck_rate(s, 0.5f);
        sy_swim_common(s);
        sy_pose_mario(s);
        if (!menu_race_counting()) {
            pd_end("開始デモ"); rm.bck = NULL;
            sy_cam(s, "開始デモ", 0);
            se_start(SE_SY_RACE_START, NULL);
            sy_set(s, SY_SWIM);
        }
        break;
    case SY_SWIM:
        if (st == 0) sy_bck(s, "swim", 0);
        sy_bck_rate(s, 0.5f);
        sy_swim_common(s);
        if (sy_ready_to_emit(s)) sy_set(s, SY_EMIT);
        else if (rr_reached_goal(&s->rail)) sy_set(s, SY_END);
        break;
    case SY_EMIT: /* exeEmitRing (again at once when the next is ready: its first step anew) */
        if (st == 0) { sy_bck(s, "screw", 0); se_start(SE_SM_SYATI_ROLL, &s->pos.x); sy_emit(s); }
        sy_swim_common(s);
        if (sy_ready_to_emit(s)) sy_set(s, SY_EMIT);
        else if (st > 0 && sy_bck_stopped(s)) sy_set(s, SY_SWIM);
        break;
    case SY_STAR: /* exeWaitStarAppeared */
        if (st == 0) {
            sy_bck(s, "star", 0);
            V3 h;
            sy_head(s, &h, 2, v3(0, 0, 0)); /* copyJointPos Head */
            note_star_row = s->row; note_star_at = h; s->took = 1; sw_changed = 1; /* requestAppearPowerStar(this, mStarSpawnType, its head) */
            printf("syati row %d: its star out at %d %d %d" "\n", s->row, (int)h.x, (int)h.y, (int)h.z);
        }
        if (st > 1 && star_appeared()) { s->dead = 1; printf("syati row %d: killed" "\n", s->row); }
        break;
    case SY_END: /* exeReachToEnd */
        if (st == 0) sy_bck(s, "turn", 0);
        if (st > 0 && sy_bck_stopped(s)) sy_set(s, SY_WAIT_RINGS);
        break;
    case SY_WAIT_RINGS: /* exeWaitAllRingDisappear */
        if (st == 0) { sy_bck(s, ring ? "waitreverse" : "waitdeepsea", 0); if (s->k >= 0) { talk_reset_node(s->k); talk_forward_node(s->k); } }
        if (sy_rings_alive() == 0 && s->count != s->nrings) sy_request(s, "再挑戦デモ", SY_FADEOUT_RETRY);
        break;
    case SY_TALK_RETRY: /* exeTalkRetryMission */
        if (st == 0) {
            sy_balloon(s, ring ? v3(16.0f, 192.0f, -300.0f) : v3(47.0f, 179.0f, -300.0f)); sy_bck(s, "failure", 0);
            if (on_stage("OceanPhantomCaveGalaxy") && !sy_bgm_05()) bgm_start("MBGM_GALAXY_05"); /* startStageBGM */
        }
        sy_pose_mario(s);
        sy_blink(s);
        if (talk_force_at_end(s->k, 0)) { pd_end("再挑戦デモ"); rm.bck = NULL; sy_cam(s, "会話", 0); sy_set(s, SY_KILL); }
        break;
    case SY_KILL: /* exeForceKill: forceKillPlayerByWaterRace */
        if (st == 0) { sy_miss_water = miss_race = 1; miss(); printf("syati row %d: Mario lost: forceKillPlayerByWaterRace" "\n", s->row); }
        break;
    case SY_HIDE: /* exeHideOnShore */
        if (st == 0) {
            s->shown = 0; s->sensors = 0;
            if (s->k >= 0) talkers[s->k].dist = 700.0f; /* setDistanceToTalk */
            sy_balloon(s, ring ? v3(61.0f, 384.0f, 250.0f) : v3(152.0f, 240.0f, 250.0f)); /* sTalkOffsetOnShore / DelightDeepSea */
        }
        if (s->hide == 0 && star_appeared()) sy_set(s, SY_WAIT_SHORE);
        else if (s->hide == 1) sy_set(s, SY_WAIT_TALK);
        break;
    case SY_WAIT_SHORE:
        if (st == 0) { s->shown = 1; s->sensors = 1; sy_bck(s, ring ? "delight" : "delightdeepsea", 0); s->eye = "syati/open"; }
        sy_blink(s);
        talk_near(s->k);
        break;
    case SY_WAIT_TALK:
        if (st == 0) { s->shown = 1; s->sensors = 1; sy_bck(s, "startwait", 15); s->eye = "syati/open"; }
        sy_blink(s);
        talk_near(s->k);
        if (s->k >= 0 && tk_near(&talkers[s->k], -1.0f)) sy_set(s, SY_TALK_NORMAL);
        break;
    case SY_TALK_NORMAL:
        if (st == 0) sy_bck(s, "starttalk", 15);
        sy_blink(s);
        talk_near(s->k);
        if (s->k >= 0 && !tk_near(&talkers[s->k], -1.0f)) sy_set(s, SY_WAIT_TALK);
        break;
    }
}
static void sy_sensor(Syati *s) /* its head, body and tail sensors (EnemySimple) in the HitSensor list; attackSensor: Mario
                                  * FlipMaximum, any other sensor sendMsgPush (the walkers' receiveMsgPush: wp_receive) */
{
    if (!s->sensors || !s->shown || s->dead) return;
    V3 c[3];
    const float r[3] = {200.0f, 230.0f, 150.0f};
    sy_head(s, &c[0], 6, v3(0.0f, -60.0f, 0.0f)); /* Jaw */
    c[1] = s->pos;
    sy_head(s, &c[2], 13, v3(0, 0, 0));           /* Spin2 */
    for (int j = 0; j < 3; j++) hs_add(c[j], r[j], HS_ENEMY, s->row);
    for (int j = 0; j < 3; j++)
        if (mario_touch(c[j], r[j])) { if (mario_attack(ATK_FLIP_MAXIMUM, c[j])) se_start(SE_SM_SYATI_TRAMPLED, &s->pos.x); break; }
    static Kuribo a; /* the sender's actor (its mPosition) */
    a.pos = s->pos;
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        WSen o[5];
        if (k->state == KURIBO_DEAD || !row_shown(k->row) || (k->clipped && !test_noclip) || k->host) continue;
        int n = wp_sensors(k, o);
        for (int x = 0; x < n; x++)
            for (int j = 0; j < 3; j++) {
                if (vlen(vsub(o[x].c, c[j])) >= o[x].r + r[j]) continue;
                WSen sa = {c[j], r[j], 0, 1, 0, 0};
                if (wp_receive(k, &o[x], &a, &sa)) printf("syati row %d: pushed walker row %d" "\n", s->row, k->row);
            }
    }
}
static int sy_head_turn(Inst *in) /* calcHeadJoint (the Head joint's delegator) in WaitOnShore / WaitTalkNormal / TalkNormal:
                            * turnQuat of its front (_8C's Z) to Mario's direction flattened on Y, by pi / 6 at most,
                            * concatenated onto the joint's matrix (its children with it); 1 when it posed the model */
{
    Syati *s = NULL;
    for (int i = 0; sy_scene == sc && i < nsys; i++) if (sys[i].row == in->row) s = &sys[i];
    if (!s || !(s->nrv == SY_WAIT_SHORE || s->nrv == SY_WAIT_TALK || s->nrv == SY_TALK_NORMAL) || !in->anim || !in->anim->skin) return 0;
    V3 d = vsub(mario->pos, s->pos);
    if (vlen(d) < 1e-6f) return 0;
    d = vnorm(d); d.y = 0.0f;
    if (vlen(d) < 1e-6f) return 0;
    d = vnorm(d);
    V3 z;
    q_axes(s->q, NULL, NULL, &z);
    z = vnorm(z);
    float between = acosf(fminf(fmaxf(vdot(z, d), -1.0f), 1.0f)), rate = between <= 3.14159265f / 6.0f ? 1.0f : (3.14159265f / 6.0f) / between;
    V3 ax = vcross(z, d);
    anim_pose(in->anim);
    if (vlen(ax) > 1e-6f && in->anim->skin->njoints > 2) { /* the rotation in the joint's own frame: about its matrix x ax */
        const float *w = in->anim->world[2];
        anim_joint_rot_v(in->anim, 2, vnorm(v3(w[0] * ax.x + w[4] * ax.y + w[8] * ax.z, w[1] * ax.x + w[5] * ax.y + w[9] * ax.z,
                                                w[2] * ax.x + w[6] * ax.y + w[10] * ax.z)), between * rate);
    }
    anim_skin(in->anim);
    return 1;
}
static void sy_init(void)
{
    nsys = 0; sy_scene = sc; sy_miss_water = 0;
    memset(sy_rings, 0, sizeof sy_rings);
    int ts = scene_type(sc, "Syati"), tt = scene_type(sc, "TalkSyati");
    for (u32 i = 0; i < sc->nobjs && nsys < SY_MAX; i++) {
        const Row *r = &sc->objs[i];
        if ((ts < 0 || (int)r->type != ts) && (tt < 0 || (int)r->type != tt)) continue;
        Syati *s = &sys[nsys];
        memset(s, 0, sizeof *s);
        s->row = (int)i; s->k = -1; s->req = -1; s->next = -1; s->blink = 240; s->nrings = 5; s->life = -1; s->curpt = -1;
        s->eye = "syati/open"; s->shown = 1; s->sensors = 1;
        s->swim = r->arg[2]; s->hide = r->arg[1]; /* getJMapInfoArg2NoInit (mSwimMode -1), Arg1 (mHideOnShoreMode -1) */
        s->pos = s->pos0 = row_pos(r);
        V3 x, z;
        row_axes(r, &x, &z);
        s->q = s->q0 = q_from_axes(x, vcross(z, x), z); /* makeQuatFromRotate */
        s->grav = vmul(up_at(s->pos, vcross(z, x)), -1.0f); /* calcGravity */
        if ((int)r->type == ts) { /* initRings, initRailRider, moveCoordAndTransToRailPoint 0 */
            s->ring = 1;
            if (r->arg[0] != -1) s->nrings = r->arg[0];
            if (r->arg[3] != -1) s->life = r->arg[3];
            if (rr_init(&s->rail, r)) { s->rail.coord = rr_point_coord(&s->rail, 0); s->pos = sy_rail_at(s, s->rail.coord, NULL); }
        }
        s->nrv = s->rail.pts ? SY_WAIT : SY_HIDE;
        printf("syati row %u (%s): %d rings, swim mode %d, shore mode %d, rail %d points" "\n", i, sc->types + 32 * r->type, s->nrings,
               s->swim, s->hide, rr_npoints(&s->rail));
        nsys++;
    }
}
#ifdef SYATI_TEST
static void game_test_sy(Syati *s);
#endif
static void update_syati(void)
{
    if (sy_scene != sc) sy_init();
    if (!nsys) return;
    if (!rm.on && !nrn) menu_race_update(); /* (RaceManagerLayout's movement: update_race's when it has a race) */
    for (int i = 0; i < nsys; i++) {
        Syati *s = &sys[i];
        Inst *in = sy_inst(s);
#ifdef SYATI_TEST
        game_test_sy(s);
#endif
        if (s->dead) { if (in) in->invisible = 1; continue; } /* kill */
        if (s->k < 0) { /* initTalking: createTalkCtrl (SyatiRing), setDistanceToTalk 2500; TalkSyati: onRootNodeAutomatic */
            char lab[32];
            if (talk_row_label(s->row, lab)) {
                s->k = talk_new(lab, s->row, s->row, 0.0f, 2500.0f);
                if (s->k >= 0 && !s->ring) talkers[s->k].root_auto = 1;
                printf("syati row %d: talk %s (%d)" "\n", s->row, lab, s->k);
            }
            sy_balloon(s, v3(0, 0, 0));
        }
        if (s->k >= 0) talk_place(s->k, s->bal);
        if (s->req >= 0 && pd_try_start(s->req_demo, s->row)) { s->next = s->req; s->req = -1; } /* its demo started: the nerve */
        if (s->next >= 0) { s->nrv = s->next; s->step = 0; s->next = -1; }
        sy_exe(s);
        s->step++;
        if (s->next >= 0) { s->nrv = s->next; s->step = 0; s->next = -1; printf("syati row %d: nerve %d" "\n", s->row, s->nrv); }
        if (s->ring) sy_update_rings(s); /* control */
        sy_sensor(s);
        s->eye_f += 1.0f;
        if (in) { /* calcAndSetBaseMtx: its quaternion at its place */
            const Row *r = &sc->objs[s->row];
            sy_mtx(s, in->m, s->q, s->pos);
            for (int j = 0; j < 12; j++) in->m[j] *= r->scale[j / 4];
            V3 x, y, z;
            q_axes(s->q, &x, &y, &z);
            in->nu = y; in->nf = z;
            in->invisible = !s->shown;
        }
    }
    for (int i = 0; i < 5; i++) { /* the rings' movement; their sensor on Mario */
        PRing *p = &sy_rings[i];
        if (p->nrv == PR_DEAD) continue;
        if (!prg_passed(p) && mario_touch(p->pos, 300.0f)) prg_set(p, PR_READY_PASS); /* attackSensor */
        prg_exe(p);
        if (p->a.skin) anim_update(&p->a);
    }
}
static void draw_syati_rings(void)
{
    const Model *md = scene_model(sc, "PrizeRing");
    if (sy_scene != sc || !md || !md->skin) return;
    for (int i = 0; i < 5; i++) {
        PRing *p = &sy_rings[i];
        if (p->nrv == PR_DEAD || p->hidden || !p->a.skin) continue;
        float m[16];
        basis_matrix(m, p->pos, p->nu, p->nf, 0);
        float c[3] = {m[12], m[13], m[14]};
        if (!render_visible(c, 400.0f)) continue;
        use_row_light(-1, p->pos, LIGHT_STRONG);
        bva_key = "prizering/number"; bva_frame = (float)(p->num - 1); /* setNumber: setBvaFrameAndStop */
        draw_skin(md, m, &p->a);
    }
}
static int sy_miss_anim(void) { int w = sy_miss_water; sy_miss_water = 0; return w; } /* forceKill(1): 水中レース負け */

#ifdef SYATI_TEST
/* XF=-DSYATI_TEST (OceanRingGalaxy/1): once Mario is free, into the SwitchCube by the Syati; A through its talks; in
 * the swim Mario put in each ring as it comes out (=2: kept near it over its path, in none: the retry); then on the star */
static void game_test_sy(Syati *s)
{
    static int free_at = -1, t;
#if SYATI_TEST == 3 /* (OceanRingGalaxy/2) the TalkSyati: Mario put 400 in front and 400 aside once free, A every 30 frames */
    if (s->ring) return;
    pad_force = 0;
    if (free_at < 0) { if (!game_test_free()) return; free_at = t; printf("syati test: free" "\n"); }
    if (++t - free_at == 60) { V3 x, z; q_axes(s->q, &x, NULL, &z); mario->pos = vadd(s->pos, vadd(vmul(z, 400.0f), vmul(x, 400.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* (45 degrees off its front: the head turns 30) */
    if (t - free_at > 90 && (t % 30) < 3) pad_force = PAD_CROSS;
    if (t % 60 == 0) printf("syati test t %d: talk syati nerve %d shown %d | talk %d" "\n", t, s->nrv, s->shown, td.nrv);
    return;
#endif
    if (!s->ring) return;
    pad_force = 0;
    if (free_at < 0) { if (!game_test_free()) return; free_at = t; printf("syati test: free" "\n"); }
    t++;
    if (s->nrv == SY_WAIT && t - free_at > 60) {
        int ty = scene_type(sc, "SwitchCube");
        for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].sw[SW_A] == sc->objs[s->row].sw[SW_A]) {
                mario->pos = vadd(row_pos(&sc->objs[i]), v3(0, 300.0f, 0)); mario->hvel = v3(0, 0, 0); mario->vup = 0; break;
            }
    }
    if (td.nrv != TD_WAIT && (t % 30) < 3) pad_force = PAD_CROSS;
#if SYATI_TEST != 2
    if (s->nrv == SY_SWIM || s->nrv == SY_EMIT) { /* in a ring out, else 1500 behind the Syati (it keeps its speed) */
        int in = 0;
        for (int i = 0; i < 5 && !in; i++)
            if (sy_rings[i].nrv == PR_LOOP) { mario->pos = vsub(sy_rings[i].pos, vmul(mario->up, 80.0f)); in = 1; }
        if (!in) { V3 z; q_axes(s->q, NULL, NULL, &z); mario->pos = vsub(s->pos, vmul(z, 1500.0f)); }
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
    }
#else
    if (s->nrv == SY_SWIM || s->nrv == SY_EMIT) { /* 1500 behind the Syati and 800 over its path: near it, no ring */
        V3 y, z;
        q_axes(s->q, NULL, &y, &z);
        mario->pos = vadd(vsub(s->pos, vmul(z, 1500.0f)), vmul(y, 800.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
    }
#endif
    if (s->took && !pd.name)
        for (int i = 0; i < ninsts; i++)
            if (insts[i].item == ITEM_STAR && star_wait[insts[i].row] == s->row + 1 && row_shown(insts[i].row) && !row_dead[insts[i].row]) { /* its star */
                mario->pos = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]); mario->hvel = v3(0, 0, 0); mario->vup = 0; break;
            }
    if (t % 60 == 0)
        printf("syati test t %d: nerve %d step %d coord %d count %d rings %d | Mario %d %d %d swim %d talk %d life %d" "\n", t, s->nrv, s->step,
               (int)s->rail.coord, s->count, sy_rings_alive(), (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z, swimming, td.nrv, life);
}
#endif
