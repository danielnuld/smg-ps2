/* The trick rabbits Mario chases for a star (Petari NPC/TrickRabbitFreeRun.cpp, RabbitStateWaitStart.cpp,
 * RabbitStateCaught.cpp), on the Gateway rabbits' kit (GwRabbit: the walker body, WalkerStateRunaway as rr_state,
 * WalkerStateBlowDamage as RunawayRabbit's). One a scene.
 *
 * TrickRabbitFreeRun (ReverseKingdomGalaxy 1): Body 70 and Catch 30 at Spine, binder 60 / 60, its talk 120 up.
 * WaitStart (RabbitStateWaitStart, its talk action StartEvent, killed at the talk's end) -> the 逃走 demo ->
 * RunawayStart (Mario's BattleWait, Run, MBGM_GALAXY_02_CHASE, escapeFromPlayer 1.4 / 1 / 0.9 / 4 for 120 steps, the
 * hop sound at its Run's frame 3) -> Runaway (WalkerStateRunaway at sNormalAccel 0.7, sPressureAccel 1.5 with Mario
 * within sPressureRange 600 across, x sBrakeAccelRate 0.3 in water; the hop sound while it runs) -> BlowDamage (a
 * star piece: setVelocitySeparateHV 20 / 20) -> Runaway; Catch on Mario or his spin on its body in Runaway /
 * BlowDamage: forwardNode, Caught (stopStageBGM 60, RabbitStateCaught) -> PowerStarDemo (endDemo, startAfterBossBGM,
 * the star 200 over it, Wait; at the appear demo's end forwardNode) -> GiveUp (RabbitStateWaitStart, GiveUpTalk).
 *
 * RabbitStateWaitStart: Wait (gravity 0.1 off the ground; its talk within sInStartTalkRange 300; SmallTurn past 30
 * steps when Mario is not within 45 degrees of its front), SmallTurn (TurnSmall, 6 degrees a step, 30 steps),
 * HitReaction (a star piece or his spin on its body: Reaction and the two hit sounds), Trample (Press), Spin (his
 * spin storm within 600 of its body: Spin, turned 2 degrees a step), Pointing (the pointer's A on it: Pointing,
 * SE_SM_LV_RABBIT_POINT), Talk (its talk action, turned 30 degrees a step; at the talk's end dead or Wait).
 * RabbitStateCaught: the 捕まり demo; Caught (Caught / Mario's Catch, SE_SM_RABBIT_CAUGHT, Mario's base matrix blended
 * to its own over sMarioPoseBlendTime 5, then a jump of sCaughtJumpPower 20, gravity 1, 0.99) until on the ground
 * from sCaughtLandStartTime 20; CaughtLand (CaughtLand / CatchLand, the star model's TrickRabbitLand,
 * SE_SM_LV_RABBIT_STRUGGLE) until its BCK stops; CaughtEvent (CaughtWait / CatchWait, TrickRabbitWait, its talk
 * forced), dead at the talk's end.
 * ponytail: the Spine joint is 50 up its place (as the Gateway's rabbits); no SpotMarkLight, foot prints, Light
 * effect; no isBindedGroundBrake (the walkers' binder keeps no floor code); the push of Mario off its body while it
 * can not be caught is mario_push_out; no blow from other actors (isMsgToEnemyAttackBlow) */
enum { TF_WAITSTART, TF_TRYDEMO, TF_RUNSTART, TF_RUNAWAY, TF_BLOW, TF_CAUGHT, TF_STARDEMO, TF_GIVEUP };
enum { RWS_WAIT, RWS_SMALLTURN, RWS_HIT, RWS_TRAMPLE, RWS_SPIN, RWS_POINTING, RWS_TALK };
enum { TC_TRY, TC_CAUGHT, TC_LAND, TC_EVENT };
typedef struct {
    GwRabbit r;
    int on, kind;                     /* kind 1: TrickRabbit (its rail graph) */
    int ws, wst, kill_at_talk, storm; /* RabbitStateWaitStart: its nerve, step, _1C; Mario's spin storm a step ago */
    const char *talk_act;             /* mTalkActionName */
    int cs, cst;                      /* RabbitStateCaught */
    int star;                         /* 0, 1 its star asked for, 2 the appear demo seen, 3 over */
    u32 lvp;
    NpcReact re;
    int nostar, fix;                  /* RabbitStateCaught::unusePowerStarModel; RabbitStateWaitStart's _1D off */
    int nopiece; V3 fpos, fup, ffront; /* TrickRabbitSnow: !mIsValidAppearStarPiece, mFinishPos, mFinishQuat */
} Trk;
static Trk trk;

static V3 trk_spine(void) { return vadd(trk.r.k.pos, vmul(trk.r.k.qy, 50.0f)); }
static void trk_zero(void) { trk.r.k.vel = v3(0, 0, 0); }
static int trk_bck_pass(float frame) /* checkPassBckFrame */
{
    Inst *in = gw_inst(trk.r.k.row);
    float f = in && in->anim ? in->anim->frame : -1.0f;
    int pass = trk.r.bck < frame && f >= frame;
    trk.r.bck = f;
    return pass;
}
static void trk_jump_sound(void) /* startJumpSound */
{
    float dep;
    w_se(&trk.r.k, in_water(trk.r.k.pos, trk.r.k.qy, &dep) ? SE_SM_RABBIT_JUMP_WATER : SE_SM_RABBIT_JUMP);
}

static void rws_set(int n) { trk.ws = n; trk.wst = -1; }
static int rws_short(void) { return trk.r.talk >= 0 && talkers[trk.r.talk].talk == 1; } /* isShortTalk */
static int rws_enable(void) /* isEnableReaction */
{ return trk.ws == RWS_WAIT || trk.ws == RWS_SMALLTURN || trk.ws == RWS_TRAMPLE || (trk.ws == RWS_TALK && rws_short()); }
static int rws_try_talk(void)
{
    GwRabbit *r = &trk.r;
    if (r->talk < 0 || !gw_near_player(r->k.pos, 300.0f)) return 0; /* sInStartTalkRange */
    gw_talk_place(r->talk, r->k.row);
    if (!talk_near(r->talk)) return 0;
    rws_set(RWS_TALK);
    return 1;
}
static int rws_update(void) /* RabbitStateWaitStart: 1 once it is dead */
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    V3 body = trk_spine();
    trk.re.atk = 1; /* isMsgStarPieceAttack */
    int f = npc_react(&trk.re, body, 70.0f, k->qy, k->pos, 0.0f, 0);
    int storm = mario->spin_timer > 0 && mario->spin_t < 15 && vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), body)) < 70.0f + 600.0f;
    if (rws_enable()) {
        if (f & 1) rws_set(RWS_TRAMPLE);
        else if (f & (16 | 2)) rws_set(RWS_HIT);
        else if (storm && !trk.storm) rws_set(RWS_SPIN); /* isMsgSpinStormRange */
        else if (f & 4) rws_set(RWS_POINTING);           /* control: tryPointing */
    }
    trk.storm = storm;
    int st = ++trk.wst, ended = st > 1 && gw_stopped(k->row); /* isActionEnd */
    switch (trk.ws) {
    case RWS_WAIT:
        if (st == 0) { gw_ract(r, "Wait"); trk_zero(); }
        if (trk.fix) ; /* _1D */
        else if (!k->on_ground) gw_grav_vel(r, 0.1f); /* sGravityAccel */
        else trk_zero();
        if (rws_try_talk()) break;
        if (st > 30) { /* trySmallTurn: sSmallTurnTime, sSmallTurnStartDegree */
            V3 to = vtangent(vsub(mario->pos, k->pos), k->grav);
            if (vlen(to) > 1e-6f && vdot(vnorm(to), k->front) < cosf(45.0f * 0.0174533f)) rws_set(RWS_SMALLTURN);
        }
        break;
    case RWS_SMALLTURN:
        if (st == 0) gw_ract(r, "TurnSmall");
        gw_turn_to_player(r, 6.0f); trk_zero();
        if (!rws_try_talk() && st > 30) rws_set(RWS_WAIT);
        break;
    case RWS_HIT:
        if (st == 0) { gw_ract(r, "Reaction"); w_se(k, SE_SM_RABBIT_STAR_PIECE_HIT); w_se(k, SE_SV_RABBIT_STAR_PIECE_HIT); }
        trk_zero();
        if (ended) rws_set(RWS_WAIT);
        break;
    case RWS_TRAMPLE:
        if (st == 0) { gw_ract(r, "Press"); w_se(k, SE_SM_NPC_TRAMPLED); w_se(k, SE_SV_RABBIT_TRAMPLED); }
        trk_zero();
        if (ended) rws_set(RWS_WAIT);
        break;
    case RWS_SPIN:
        if (st == 0) { gw_ract(r, "Spin"); w_se(k, SE_SV_RABBIT_SPIN); w_se(k, SE_SM_RABBIT_SPIN); }
        gw_turn_to_player(r, 2.0f); trk_zero();
        if (ended) rws_set(RWS_WAIT);
        break;
    case RWS_POINTING:
        if (st == 0) { gw_ract(r, "Pointing"); dpd_hit_sound(); }
        trk.lvp = se_level(SE_SM_LV_RABBIT_POINT, &k->pos.x, trk.lvp);
        trk_zero();
        if ((rws_short() || !rws_try_talk()) && ended) rws_set(RWS_WAIT);
        break;
    case RWS_TALK:
        if (st == 0) { gw_ract(r, trk.talk_act); trk_zero(); }
        gw_turn_to_player(r, 30.0f);
        if (talk_near_at_end(r->talk)) {
            if (trk.kill_at_talk) return 1;
            rws_set(RWS_WAIT);
        }
        break;
    }
    return 0;
}
static int tc_update(void) /* RabbitStateCaught: 1 once it is dead */
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    if (trk.cs == TC_TRY) { /* requestStartDemoMarioPuppetable */
        if (!pd_try_start("捕まり", k->row)) return 0;
        trk.cs = TC_CAUGHT; trk.cst = -1;
    }
    int st = ++trk.cst;
    switch (trk.cs) {
    case TC_CAUGHT: {
        if (st == 0) {
            gw_ract(r, "Caught"); mario_play("catch", 1.0f, 0);
            trk_zero();
            w_se(k, SE_SM_RABBIT_CAUGHT);
            r->mpos = mario->pos; r->mup = mario->up; r->mfront = mario->face; /* setCaughtStartMarioPose */
        }
        if (!k->on_ground) gw_grav_vel(r, 1.0f); /* sCaughtGravityAccel */
        k->vel = vmul(k->vel, 0.99f);            /* sCaughtAirFreq */
        float b = st < 5 ? st / 5.0f : 1.0f;     /* blendBaseMatrixToMario(calcNerveRate(sMarioPoseBlendTime)) */
        r->mpos = vadd(r->mpos, vmul(vsub(k->pos, r->mpos), b));
        r->mup = vnorm(vadd(r->mup, vmul(vsub(k->qy, r->mup), b)));
        r->mfront = vnorm(vadd(r->mfront, vmul(vsub(k->qz, r->mfront), b)));
        if (st == 5) k->vel = vmul(k->grav, -20.0f); /* setVelocityJump(sCaughtJumpPower) */
        if (st >= 20 && k->on_ground) { trk_zero(); trk.cs = TC_LAND; trk.cst = -1; } /* sCaughtLandStartTime */
        break;
    }
    case TC_LAND:
        if (st == 0) {
            if (!trk.nostar) tos_appear(k->row, "TrickRabbitLand"); /* mPowerStarModel at mCaughtLandMarioBaseMtx (its own base) */
            gw_ract(r, "CaughtLand"); mario_play("catchland", 1.0f, 0);
        }
        r->mpos = k->pos; r->mup = k->qy; r->mfront = k->qz;
        k->lv = se_level(SE_SM_LV_RABBIT_STRUGGLE, &k->pos.x, k->lv);
        if (st > 1 && gw_stopped(k->row)) {
            if (r->talk < 0) { if (!trk.nostar) tos_row = -1; return 1; }
            trk.cs = TC_EVENT; trk.cst = -1;
        }
        break;
    case TC_EVENT:
        if (st == 0) {
            gw_ract(r, "CaughtWait"); mario_play("catchwait", 1.0f, 1);
            if (!trk.nostar) star_bck(tos_anim(), "PStar", "TrickRabbitWait");
            gw_talk_place(r->talk, k->row);
            talk_force(r->talk, 1); /* tryTalkForceWithoutDemoMarioPuppetableAtEnd */
        }
        r->mpos = k->pos; r->mup = k->qy; r->mfront = k->qz;
        k->lv = se_level(SE_SM_LV_RABBIT_STRUGGLE, &k->pos.x, k->lv);
        if (st > 0 && talk_end(r->talk)) { if (!trk.nostar) tos_row = -1; return 1; } /* mPowerStarModel->kill() */
        break;
    }
    return 0;
}
static int tf_catchable(void) { return trk.r.k.nrv == TF_RUNAWAY || trk.r.k.nrv == TF_BLOW; } /* isEnableCaught */
static void tf_caught(void) /* requestCaught */
{
    if (trk.r.talk >= 0) talk_forward_node(trk.r.talk);
    w_nrv(&trk.r.k, TF_CAUGHT);
    printf("trick rabbit row %d: caught\n", trk.r.k.row);
}
static void tf_set(int n) { w_nrv(&trk.r.k, n); printf("trick rabbit row %d: nerve %d\n", trk.r.k.row, n); }
#ifdef TRICK_TEST
static void trk_test(void);
#endif
static void tf_update(void)
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    int st = ++k->step;
    switch (k->nrv) {
    case TF_WAITSTART:
        if (st == 0) { trk.talk_act = "StartEvent"; trk.kill_at_talk = 1; rws_set(RWS_WAIT); } /* appear */
        if (rws_update()) tf_set(TF_TRYDEMO);
        break;
    case TF_TRYDEMO: /* requestStartDemoMarioPuppetable(逃走) */
        if (pd_try_start("逃走", k->row)) tf_set(TF_RUNSTART);
        break;
    case TF_RUNSTART:
        if (st == 0) { mario_play("battlewait", 1.0f, 1); gw_ract(r, "Run"); bgm_start("MBGM_GALAXY_02_CHASE"); } /* startEventBGM(EventBgmID_Chase) */
        w_move_turn(k, vsub(k->pos, mario->pos), 1.4f, 1.0f, 0.9f, 4.0f); /* escapeFromPlayer(sStartAwayParam) */
        if (trk_bck_pass(3.0f)) trk_jump_sound();
        if (st > 120) { pd_end("逃走"); tf_set(TF_RUNAWAY); }
        break;
    case TF_RUNAWAY: {
        if (st == 0) w_snrv(k, RS_WAIT); /* appear */
        rr_state(r);
        int pass = trk_bck_pass(3.0f);
        if (k->snrv == RS_RUNAWAY && pass) trk_jump_sound();
        float speed = 0.7f, dep; /* sNormalAccel */
        if (vlen(vtangent(vsub(mario->pos, k->pos), k->grav)) < 600.0f) speed = 1.5f; /* isNearPlayerHorizontal(sPressureRange): sPressureAccel */
        if (in_water(k->pos, k->qy, &dep)) speed *= 0.3f; /* isBindedGroundWater: sBrakeAccelRate */
        r->speed = speed;
        if (k->snrv == RS_RUNAWAY) { Inst *in = gw_inst(k->row); if (in && in->anim) in->anim->speed = speed < 0.9f ? 0.9f : speed > 1.4f ? 1.4f : speed; }
        break;
    }
    case TF_BLOW: /* WalkerStateBlowDamage, as RunawayRabbit's (rr_update) */
        if (k->snrv == 0) {
            if (st == 0) gw_ract(r, "Damage");
            k->vel = vmul(k->vel, 0.99f);
            gw_grav_vel(r, 1.0f);
            turn_on_plane(&k->front, vmul(k->vel, -1.0f), k->grav, cosf(15.0f * 0.0174533f));
            if (st > 5 && k->on_ground) { gw_ract(r, "DamageLand"); trk_zero(); w_snrv(k, 1); }
        } else if (++k->sstep > 30) w_nrv(k, TF_RUNAWAY);
        break;
    case TF_CAUGHT:
        if (st == 0) { bgm_stop(60); trk.cs = TC_TRY; }
        if (tc_update()) tf_set(TF_STARDEMO);
        break;
    case TF_STARDEMO:
        if (st == 0) {
            pd_end("捕まり");
            bgm_after_boss();
            note_star_row = k->row; note_star_at = vsub(k->pos, vmul(k->grav, 200.0f)); /* requestAppearPowerStar */
            trk.star = 1; sw_changed = 1;
            gw_ract(r, "Wait");
        }
        trk_zero();
        if (trk.star == 1 && pd.name && !strcmp(pd.name, "パワースター出現")) trk.star = 2;
        if (trk.star == 2 && !pd.name) { /* isEndPowerStarAppearDemo */
            trk.star = 3;
            if (r->talk >= 0) talk_forward_node(r->talk);
            tf_set(TF_GIVEUP);
        }
        break;
    case TF_GIVEUP:
        if (st == 0) { trk.talk_act = "GiveUpTalk"; trk.kill_at_talk = 0; rws_set(RWS_WAIT); }
        rws_update();
        break;
    }
    /* attackSensor: its Catch (30 at Spine) on Mario catches it; else, while it can not be caught, its body pushes him */
    if (tf_catchable()) {
        V3 spine = trk_spine();
        if (!dx_busy() && !pd.name && (mario_touch(spine, 30.0f) || mario_offence(NULL, spine, 70.0f, k->grav) == MSG_HITALL)) tf_caught(); /* isMsgPlayerSpinAttack on its Body */
    } else if (k->nrv != TF_CAUGHT) mario_push_out(trk_spine(), 70.0f);
    gw_move(r);
    if (k->nrv == TF_CAUGHT && trk.cs != TC_TRY) { mario->pos = r->mpos; mario->up = r->mup; mario->face = vnorm(vtangent(r->mfront, r->mup)); mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* setPlayerBaseMtx */
}

/* TrickRabbit (Petari NPC/TrickRabbit.cpp; CosmosGardenGalaxy 3), the rabbit that runs along its rail graph
 * (RailGraph.cpp, RailGraphIter.cpp, RailGraphUtil.cpp: its rail and the rails linked from it by Path_ID, their points
 * within 100 of each other one node, an edge between consecutive points with the first one's point args; slice.py
 * rgph_chunk bakes it: 'RGPH'). Edge args: arg0 1 a jump (arg1 its start time, 20; arg2 its time, by default the
 * parabola's length at 22 a step; arg3 its height, 100), else arg1 its speed in percent; arg7 -2 the start demo's
 * route, else its route level (-1 any): the level goes 0 -> 1 1200 steps after the demo (startRouteLevelControl).
 * WaitStart (RabbitStateWaitStart) -> the 逃走 demo: DemoRunaway / DemoJumpStart / DemoJump along the arg7 -2 edges,
 * DemoEnd (FollowMe, 30 steps) -> Wait (FollowMe; Mario within 1200: the nearest node with a route edge, the edge
 * away from him) -> Runaway (Run; 2.4 x speed toward the next node, kept within 100 .. 400 of the edge, 0.85; under
 * 125 from the node the next edge away from Mario; Wait past 1500, JumpStart on a jump edge, BrakeTurn past step 60
 * with Mario within 600 and ahead: the edge reversed, Brake, 0.93, 40 steps) -> JumpStart -> Jump (the binder off, a
 * ParabolicPath to the next node, SE_SM_RABBIT_THICKET through a thicket). A trample, a star piece or his hit on its
 * body: Tumble (FallDownLoop, GetUp after 20 on the ground: FallDownEnd, 20) or, in a Jump past step 1, FallDown
 * (Fall until the ground, FallDownLand 20, GetUpFromFallDown 20, ComebackRouteStart 8, ComebackRoute: a jump of 100
 * back to the next node). Catch on Mario or his spin attack: Caught (stopStageBGM 60, SE_SY_TOTAL_COMPLETE,
 * RabbitStateCaught) -> WaitPowerStarDemo -> GiveUp.
 * ponytail: no MultiEventCamera (the demo's camera), no ThicketJump effect; updateTime's 10800 step timer never runs
 * in Petari either (_E8 is never set); the ghost rabbit (_E1, いたずら幽霊ウサギ) is placed nowhere */
enum { TRM_WAITSTART = 20, TRM_TRYDEMO, TRM_DEMORUN, TRM_DEMOJUMPSTART, TRM_DEMOJUMP, TRM_DEMOEND, TRM_WAIT, TRM_RUNAWAY, TRM_JUMPSTART,
       TRM_JUMP, TRM_BRAKETURN, TRM_FALLDOWN, TRM_FALLDOWNLAND, TRM_GETUPFALL, TRM_COMEBACKSTART, TRM_COMEBACK, TRM_TUMBLE, TRM_GETUP,
       TRM_CAUGHT, TRM_STARDEMO, TRM_GIVEUP };
typedef struct { u16 a, b; s16 arg0, arg1, arg2, arg3, arg7, pad; } TgEdge; /* slice.py rgph_chunk */
static struct {
    const float *nodes; const TgEdge *edges; int nn, ne;
    int cur, prev, sel;                 /* RailGraphIter: _4, _10, mSelectedEdge */
    int cc, level, level_to, level_t, level_on; /* _CC, _D8, _DC, _D4, _E0 */
    float d0;                           /* _D0 */
    V3 p0, py, pz; float pa, pv, pl;    /* mParabolicPath */
} trg;
static V3 trg_node(int n) { const float *p = trg.nodes + 3 * n; return v3(p[0], p[1], p[2]); }
static int trg_other(int e, int n) { return trg.edges[e].a == n ? trg.edges[e].b : trg.edges[e].b == n ? trg.edges[e].a : -1; } /* getNextNode */
static int trg_watch(int e, int n) /* watchStartEdge (e -1) / watchNextEdge: a node's edges in the order they were connected */
{
    for (e++; e < trg.ne; e++) if (trg.edges[e].a == n || trg.edges[e].b == n) return e;
    return -1;
}
static V3 trg_next_pos(void) { return trg_node(trg_other(trg.sel, trg.cur)); } /* getNextNodePosition */
static int trg_near(V3 p, int route) /* getNearNodeIndex (no limit); route: RunnawayNodeSelecter (an edge whose arg7 is not -2) */
{
    float best = 3.4e38f;
    int bi = -1;
    for (int n = 0; n < trg.nn; n++) {
        int ok = !route;
        for (int e = trg_watch(-1, n); e >= 0 && !ok; e = trg_watch(e, n)) ok = trg.edges[e].arg7 != -2;
        float d = vlen(vsub(p, trg_node(n)));
        if (ok && d < best) { best = d; bi = n; }
    }
    return bi;
}
static void trg_set_node(int n) { trg.cur = n; trg.prev = -1; trg.sel = -1; }
static void trg_move_next(void) { int nx = trg_other(trg.sel, trg.cur); trg.prev = trg.cur; trg.cur = nx; } /* moveNodeNext */
static int trg_select_start(void) /* selectEdgeStartEvent */
{
    int b = 0;
    for (int e = trg_watch(-1, trg.cur); e >= 0; e = trg_watch(e, trg.cur))
        if (trg_other(e, trg.cur) != trg.prev && trg.edges[e].arg7 < -1) { trg.sel = e; b = 1; }
    return b;
}
static void trg_select_runaway(V3 vec, float f) /* selectEdgeRunnaway: by its route level, away from Mario, not the way it came, the most away */
{
    V3 dir = vlen(vec) > 1e-6f ? vnorm(vec) : v3(0, 0, 0);
    float best = -3.4e38f;
    int r29 = 0, r28 = 0, r27 = 0;
    for (int e = trg_watch(-1, trg.cur); e >= 0; e = trg_watch(e, trg.cur)) {
        int arg = trg.edges[e].arg7;
        if (arg < -1) continue;
        V3 ed = vsub(trg_node(trg_other(e, trg.cur)), trg_node(trg.cur));
        float dot = vlen(ed) > 1e-6f ? vdot(vnorm(ed), dir) : 0.0f;
        int v17 = arg == -1 || arg == trg.level, v18 = dot > f, fresh = trg_other(e, trg.cur) != trg.prev, take = 0;
        if (r29 && !v17) continue;
        if (!r29 && v17) take = 1;
        if (!take) { if (r28 && !v18) continue; if (!r28 && v18) take = 1; }
        if (!take) { if (r27 && !fresh) continue; if (!r27 && fresh) take = 1; }
        if (!take && best < dot) take = 1;
        if (take) { best = dot; r29 = v17; r28 = v18; r27 = fresh; trg.sel = e; }
    }
}
static void trm_parab(V3 start, V3 end, V3 up, float add) /* ParabolicPath::initFromUpVectorAddHeight */
{
    V3 d = vsub(end, start);
    float h = vdot(up, d), mx = add + (h < 0.0f ? 0.0f : h), disc = mx * (mx - h);
    V3 z = vsub(d, vmul(up, h));
    trg.p0 = start; trg.py = up;
    trg.pl = vlen(z); trg.pz = trg.pl > 1e-6f ? vmul(z, 1.0f / trg.pl) : v3(0, 0, 0);
    if (disc <= 0) { trg.pa = -mx; trg.pv = h + mx; } /* calcParabolicFunctionParam */
    else if (fabsf(h) < 1e-4f) { trg.pa = -4 * mx; trg.pv = 4 * mx; }
    else {
        float rt = sqrtf(disc), t1 = (mx + rt) / h, t2 = (mx - rt) / h;
        float t = t1 >= 0 && t1 <= 1 ? t1 : t2 >= 0 && t2 <= 1 ? t2 : 1.0f;
        trg.pa = -mx / (t * t); trg.pv = -2.0f * t * trg.pa;
    }
}
static V3 trm_parab_at(float t) { return vadd(trg.p0, vadd(vmul(trg.py, t * (trg.pv + trg.pa * t)), vmul(trg.pz, trg.pl * t))); } /* calcPosition */
static int trm_jump_time(void) /* calcDefaultJumpTime: 1 / calcPathSpeedFromAverageSpeed(22), the length in 10 parts */
{
    float len = 0.0f, f2 = 0.0f, f6 = trg.pl * 0.1f * trg.pl * 0.1f;
    for (int i = 1; i <= 10; i++) {
        float e = 0.1f * i, f4 = e * (trg.pa * e + trg.pv);
        len += sqrtf((f4 - f2) * (f4 - f2) + f6);
        f2 = f4;
    }
    return len > 0.0f ? (int)(len / 22.0f) : 0;
}
static void trm_set(int n) { w_nrv(&trk.r.k, n); printf("trick rabbit row %d: nerve %d (node %d -> %d)\n", trk.r.k.row, n, trg.cur, trg.sel >= 0 ? trg_other(trg.sel, trg.cur) : -1); }
static void trm_air(float air, float ground) /* gravity 1 off the ground, attenuateVelocity, reboundVelocityFromCollision(0, 0, 1) */
{
    GwRabbit *r = &trk.r;
    if (!r->k.on_ground) gw_grav_vel(r, 1.0f);
    r->k.vel = vmul(r->k.vel, r->k.on_ground ? ground : air);
    gw_rebound(r, 0.0f);
}
static void trm_run(void) /* doRunaway */
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    if (k->step == 0) gw_ract(r, "Run");
    if (trk_bck_pass(3.0f)) w_se(k, SE_SM_RABBIT_JUMP);
    V3 next = trg_next_pos();
    float arg = trg.edges[trg.sel].arg1 / 100.0f;
    if (arg < 0.0f) arg = 1.0f;
    if (!k->on_ground) gw_grav_vel(r, 1.0f);
    V3 h = vtangent(vsub(next, k->pos), k->grav); /* addMovingAccel(next - pos, 2.4 x arg) */
    trg.d0 = vlen(h);
    if (trg.d0 > 1e-6f) turn_on_plane(&k->front, vmul(h, 1.0f / trg.d0), k->grav, 0.98f);
    k->vel = vadd(k->vel, vmul(k->front, 2.4f * arg));
    V3 a = trg_node(trg.cur), ab = vsub(next, a); /* addKeepRouteRange(1, 100, 400) */
    float t = vdot(ab, ab) > 1e-6f ? vdot(vsub(k->pos, a), ab) / vdot(ab, ab) : 0.0f;
    V3 off = vtangent(vsub(t < 0.0f ? a : t > 1.0f ? next : vadd(a, vmul(ab, t)), k->pos), k->grav);
    float s = vlen(off), n = (s - 100.0f) / 300.0f;
    if (s > 1e-6f && n > 0.0f) k->vel = vadd(k->vel, vmul(off, (n > 1.0f ? 1.0f : n) / s));
    k->vel = vmul(k->vel, 0.85f);
    gw_rebound(r, 0.0f);
}
static void trm_jump_start(void) /* doJumpStart */
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    if (k->step == 0) gw_ract(r, "JumpStart");
    else if (r->act && !strcmp(r->act, "JumpStart") && gw_stopped(k->row)) gw_ract(r, "JumpStartLoop");
    turn_on_plane(&k->front, vsub(trg_next_pos(), k->pos), k->grav, 0.99f);
    if (!k->on_ground) gw_grav_vel(r, 1.0f);
    k->vel = vmul(k->vel, 0.88f);
    gw_rebound(r, 0.0f);
}
static int trm_thicket(V3 from, V3 dir) /* checkDivingThicket */
{
    if (!kcl_line(from, vadd(from, dir), NULL, NULL, NULL)) return 0;
    w_se(&trk.r.k, SE_SM_RABBIT_THICKET);
    return 1;
}
static int trm_jump(void) /* doJump, tryJumpEnd: 1 at its end (its last move its velocity) */
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    V3 was = k->pos;
    if (k->step == 0) { gw_ract(r, "Jump"); w_se(k, SE_SM_RABBIT_JUMP); }
    turn_on_plane(&k->front, vsub(trg_next_pos(), k->pos), k->grav, 0.9f);
    V3 to = trm_parab_at(trg.cc > 0 ? fminf((float)k->step / trg.cc, 1.0f) : 1.0f); /* calcNerveRate */
    if (!trm_thicket(vsub(k->pos, vmul(k->grav, 70.0f)), vsub(to, k->pos))) trm_thicket(vsub(to, vmul(k->grav, 70.0f)), vsub(k->pos, to));
    k->pos = to;
    if (k->step < trg.cc) return 0;
    k->vel = vsub(k->pos, was);
    return 1;
}
static int trm_catchable(void) /* isCaughtable */
{
    int n = trk.r.k.nrv;
    return n == TRM_WAIT || n == TRM_RUNAWAY || n == TRM_JUMPSTART || n == TRM_JUMP || n == TRM_BRAKETURN || n == TRM_TUMBLE || n == TRM_FALLDOWN ||
           n == TRM_GETUPFALL || n == TRM_COMEBACKSTART || n == TRM_COMEBACK || n == TRM_GETUP;
}
static int trm_trample(void) /* receiveMsgTrample (isTumbable; WaitStart / GiveUp take theirs in RabbitStateWaitStart) */
{
    Kuribo *k = &trk.r.k;
    int n = k->nrv;
    if (n != TRM_WAIT && n != TRM_RUNAWAY && n != TRM_JUMPSTART && n != TRM_BRAKETURN && n != TRM_JUMP) return 0;
    trm_set(n == TRM_JUMP && k->step > 1 ? TRM_FALLDOWN : TRM_TUMBLE);
    return 1;
}
static void trm_update(void)
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    int st = ++k->step, bind = 1;
    switch (k->nrv) {
    case TRM_WAITSTART:
        if (st == 0) { trk.talk_act = "StartEvent"; trk.kill_at_talk = 1; rws_set(RWS_WAIT); }
        if (rws_update()) trm_set(TRM_TRYDEMO);
        break;
    case TRM_TRYDEMO: /* tryStartDemoRunnaway */
        if (!pd_try_start("逃走", k->row)) break;
        bgm_start("MBGM_GALAXY_02_CHASE"); /* startEventBGM(EventBgmID_Chase) */
        trg_set_node(trg_near(k->pos, 0));
        trm_set(trg_select_start() ? TRM_DEMORUN : TRM_DEMOEND);
        break;
    case TRM_DEMORUN: case TRM_RUNAWAY: {
        int demo = k->nrv == TRM_DEMORUN;
        trm_run();
        if (trg.d0 < 125.0f) {
            trg_move_next();
            if (demo) { if (!trg_select_start()) { trm_set(TRM_DEMOEND); break; } }
            else trg_select_runaway(vsub(k->pos, mario->pos), 0.0f);
        }
        if (!demo && !gw_near_player(k->pos, 1500.0f)) { trm_set(TRM_WAIT); break; } /* tryStop */
        if (trg.edges[trg.sel].arg0 == 1) { trm_set(demo ? TRM_DEMOJUMPSTART : TRM_JUMPSTART); break; } /* isNextEdgeJump */
        if (!demo && st > 60 && gw_near_player(k->pos, 600.0f)) { /* tryBrakeTurn */
            V3 tp = vsub(mario->pos, k->pos), tn = vsub(trg_next_pos(), k->pos);
            if (vlen(tp) > 1e-6f && vlen(tn) > 1e-6f && vdot(vnorm(tn), vnorm(tp)) > 0.1f) {
                trg_move_next(); /* selectReverseEdge: at the next node, the same edge back */
                trm_set(TRM_BRAKETURN);
            }
        }
        break;
    }
    case TRM_DEMOJUMPSTART: case TRM_JUMPSTART: case TRM_COMEBACKSTART:
        if (st == 0) trg.cc = k->nrv == TRM_COMEBACKSTART ? 8 : trg.edges[trg.sel].arg1 >= 0 ? trg.edges[trg.sel].arg1 : 20; /* calcJumpStartTime */
        trm_jump_start();
        if (st > trg.cc) { trk_zero(); trm_set(k->nrv == TRM_DEMOJUMPSTART ? TRM_DEMOJUMP : k->nrv == TRM_JUMPSTART ? TRM_JUMP : TRM_COMEBACK); }
        break;
    case TRM_DEMOJUMP: case TRM_JUMP: case TRM_COMEBACK:
        if (st == 0) { /* setUpJumpParamFromJMap / setUpJumpParam */
            const TgEdge *e = &trg.edges[trg.sel];
            int own = k->nrv == TRM_COMEBACK;
            trm_parab(k->pos, trg_next_pos(), vmul(k->grav, -1.0f), own || e->arg3 < 0 ? 100.0f : (float)e->arg3);
            trg.cc = own || e->arg2 < 0 ? trm_jump_time() : e->arg2;
        }
        bind = 0; /* offBind until its end */
        if (trm_jump()) trm_set(k->nrv == TRM_DEMOJUMP ? TRM_DEMORUN : TRM_RUNAWAY);
        break;
    case TRM_DEMOEND:
        if (st == 0) gw_ract(r, "FollowMe");
        trm_air(0.85f, 0.85f); gw_turn_to_player(r, 10.0f); /* doWait */
        if (st == 30) { /* endDemoRunnaway */
            pd_end("逃走");
            trg.level_to = 1; trg.level_t = 1200; trg.level_on = 1; /* startRouteLevelControl(1, 0x4B0) */
            trm_set(TRM_WAIT);
        }
        break;
    case TRM_WAIT:
        if (st == 0) gw_ract(r, "FollowMe");
        trm_air(0.85f, 0.85f); gw_turn_to_player(r, 10.0f); /* doWait */
        if (gw_near_player(k->pos, 1200.0f)) { /* tryRunaway */
            trg_set_node(trg_near(k->pos, 1));
            trg_select_runaway(vsub(k->pos, mario->pos), -0.3f);
            if (trg.sel >= 0) trm_set(TRM_RUNAWAY);
        }
        break;
    case TRM_BRAKETURN:
        if (st == 0) gw_ract(r, "Brake");
        trm_air(0.93f, 0.93f);
        turn_on_plane(&k->front, vsub(trg_next_pos(), k->pos), k->grav, -1.0f);
        if (st > 40) trm_set(TRM_RUNAWAY);
        break;
    case TRM_FALLDOWN:
        if (st == 0) gw_ract(r, "Fall");
        trm_air(0.99f, 0.85f);
        if (k->on_ground) { gw_ract(r, "FallDownLand"); trk_zero(); trm_set(TRM_FALLDOWNLAND); } /* tryEndFallDown */
        break;
    case TRM_FALLDOWNLAND:
        if (r->act && !strcmp(r->act, "FallDownLand") && gw_stopped(k->row)) gw_ract(r, "FallDownLoop");
        if (st > 20) trm_set(TRM_GETUPFALL);
        break;
    case TRM_GETUPFALL: case TRM_GETUP:
        if (st == 0) gw_ract(r, "FallDownEnd");
        trk_zero();
        if (st > 20) trm_set(k->nrv == TRM_GETUP ? TRM_RUNAWAY : TRM_COMEBACKSTART);
        break;
    case TRM_TUMBLE:
        if (st == 0) gw_ract(r, "FallDownLoop");
        if (!k->on_ground) { gw_grav_vel(r, 1.0f); k->vel = vmul(k->vel, 0.99f); gw_rebound(r, 0.0f); }
        else trk_zero();
        if (st > 20 && k->on_ground) trm_set(TRM_GETUP);
        break;
    case TRM_CAUGHT:
        if (st == 0) { bgm_stop(60); se_start(SE_SY_TOTAL_COMPLETE, NULL); trk.cs = TC_TRY; }
        if (tc_update()) trm_set(TRM_STARDEMO);
        break;
    case TRM_STARDEMO: /* exeWaitPowerStarDemo */
        if (st == 0) {
            pd_end("捕まり");
            bgm_after_boss();
            note_star_row = k->row; note_star_at = vsub(k->pos, vmul(k->grav, 200.0f)); /* requestAppearPowerStar */
            trk.star = 1; sw_changed = 1;
            gw_ract(r, "Wait");
        }
        trk_zero();
        if (!k->on_ground) gw_grav_vel(r, 1.0f);
        if (gw_near_player(k->pos, 70.0f)) k->vel = vadd(k->vel, mario->face);
        gw_rebound(r, 0.0f);
        gw_turn_to_player(r, 30.0f);
        if (trk.star == 1 && pd.name && !strcmp(pd.name, "パワースター出現")) trk.star = 2;
        if (trk.star == 2 && !pd.name) { /* tryEndPowerStarDemo */
            trk.star = 3;
            trm_set(TRM_GIVEUP);
            if (r->talk >= 0) talk_forward_node(r->talk);
        }
        break;
    case TRM_GIVEUP:
        if (st == 0) { trk.talk_act = "GiveUpTalk"; trk.kill_at_talk = 0; rws_set(RWS_WAIT); }
        rws_update();
        break;
    }
    if (trg.level_on && trg.level != trg.level_to) { /* control: controlRouteLevel */
        if (!trg.level_t) { trg.level = trg.level_to; trg.level_on = 0; }
        else trg.level_t--;
    }
    /* attackSensor / receiveMsgPlayerAttack on its Body (70 at Spine): the Catch (30) on Mario or his spin attack
     * catch it; a trample, his hit or a star piece (trk_shot) tumble it; else its body pushes him */
    V3 spine = trk_spine();
    if (k->nrv != TRM_WAITSTART && k->nrv != TRM_GIVEUP) {
        int msg = mario_offence(NULL, spine, 70.0f, k->grav);
        if (trm_catchable() && !dx_busy() && !pd.name && (mario_touch(spine, 30.0f) || msg == MSG_HITALL)) { /* requestCaught */
            if (r->talk >= 0) talk_forward_node(r->talk);
            trm_set(TRM_CAUGHT);
            bind = 1;
        } else if (msg == MSG_TRAMPLE && trm_trample()) { w_se(k, SE_SM_NPC_TRAMPLED); w_se(k, SE_SV_RABBIT_TRAMPLED); mario_trample(); }
        else if (!trm_catchable() && k->nrv != TRM_CAUGHT) mario_push_out(spine, 70.0f);
    }
    if (bind) gw_move(r);
    else { /* offBind: its place is the path's */
        k->on_ground = k->on_wall = 0; r->air++;
        k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f);
        w_blend_quat(k, vmul(k->grav, -1.0f), k->front, 0.1f, 0.2f);
        gw_place(r);
    }
    if (k->nrv == TRM_CAUGHT && trk.cs != TC_TRY) { mario->pos = r->mpos; mario->up = r->mup; mario->face = vnorm(vtangent(r->mfront, r->mup)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
}
/* TrickRabbitFreeRunCollect (Petari NPC/TrickRabbitSnowCollect.cpp; SnowCapsuleGalaxy 1) and its child rabbits, the
 * TrickRabbitFreeRunChild rows of its zone (NPC/TrickRabbitSnow.cpp: initChildObj). Each rabbit a Trk of its own
 * (snc.r, from the arena), loaded into trk for its step (RabbitStateWaitStart / RabbitStateCaught run on trk).
 * TrickRabbitSnow: Body 70 and Catch 30 at Spine, binder 60 / 60, its talk (SnowRabbit) 120 up; WaitSnow (StartEvent,
 * turned to Mario 15 degrees a step) -> StartHideSnow (requestStartHideSnow: Run, escapeFromPlayer 1.4 / 1 / 0.9 / 4,
 * 120 steps) -> Vanish (Change, SE_SM_RABBIT_HIDE; at its end SE_SM_METAMORPHOSE_SMOKE, moved to its 隠れ位置, hidden)
 * -> HideSnow (turned from Mario 15; its SW_A on, or without one the SnowFloor uncovering it: ACTMES_ASK_HIDDEN_BY_SNOW
 * / ACTMES_NOTIFY_DISCOVER_SNOW) -> AppearFromSnow (Jump, up 25 and 20 ahead, gravity 1, 0.99, until the ground past
 * step 5) -> Runaway (WalkerStateRunaway at sNormalAccel 0.7, sPressureAccel 1.2 with Mario within 600 across) ->
 * BlowDamage (a star piece: 20 / 20) -> Runaway; Catch on Mario or his spin: Caught (RabbitStateCaught without the
 * star model), at its end the 捕まり demo over, forwardNode, SE_OJ_STAR_PIECE_BURST, GiveUp (RabbitStateWaitStart,
 * GiveUpTalk, _1D on), 6 star pieces (10 / 40) but for the last one. While one is being caught the others can not be
 * for 5 steps (noticeCaught: setNotCaughtable).
 * The collector: Wait (Mario within sInStartEventRange 700 on the ground) -> StartWipeOut (closeWipeFade, then the
 * 雪ウサギ開始 demo) -> StartWipeIn (BattleWait, the ウサギと会話 camera, Mario at マリオ位置, openWipeFade) -> StartTalk
 * (its talk, SnowRabbitCollect) -> WaitHideSnow (every rabbit hidden: the camera and the demo end) -> Start
 * (MBGM_GALAXY_02_CHASE, the timer arg0 seconds (180), its danger 20 s; SE_SY_E3_TIMER_COUNT_1 each second under 20 s,
 * _2 under 6 s, _4 under 2 s, _TIME_UP at 0; time up: the 雪ウサギ集め失敗 demo) -> FailedWipeOut (stopStageBGM 60) ->
 * FailedWipeIn (setFinishPosition, startLastStageBGM; the branch right) -> FailedTalk (at its end SE_PM_LAST_DAMAGE,
 * forceKillPlayerByGroundRace) -> End. Every rabbit in GiveUp (noticeGiveUp): SE_SY_TOTAL_COMPLETE, the 雪ウサギ集め成功
 * demo -> SuccessWipeOut (stopStageBGM 120) -> SuccessWipeIn (the branch left) -> SuccessTalk -> TakeOutStar (the
 * third rabbit's and the star model's TakeOutStarTrickRabbit) -> AppearPowerStar (the star out of the model,
 * startAfterBossBGM; at the appear demo's end the camera and the demo end) -> End.
 * ponytail: the i-th rabbit's 隠れ位置 is the i-th one of the GPOS chunk (the placement lists them by ChildObjId 0 .. 2
 * = the children's l_id: the chunk keeps no link); BombTimerLayout is not on main yet (PR #733): the count runs here
 * and nothing is drawn; no AppearSmoke effect, SpotMarkLight, foot prints (SW_B: clearFootPrint), Light effect; a
 * star piece on a rabbit that is not running away makes no hit sound; the star's demo is the port's own after the
 * success demo (appearPowerStarContinueCurrentDemo) */
#define SNC_MAX 4
enum { SNR_WAITSNOW, SNR_STARTHIDE, SNR_VANISH, SNR_HIDESNOW, SNR_APPEAR, SNR_RUNAWAY, SNR_BLOW, SNR_CAUGHT, SNR_GIVEUP };
enum { SNC_TRYDEMO, SNC_WAIT, SNC_STARTWIPEOUT, SNC_STARTWIPEIN, SNC_STARTTALK, SNC_WAITHIDE, SNC_START, SNC_FAILEDWIPEOUT, SNC_FAILEDWIPEIN,
       SNC_FAILEDTALK, SNC_SUCCESSWIPEOUT, SNC_SUCCESSWIPEIN, SNC_SUCCESSTALK, SNC_TAKEOUTSTAR, SNC_APPEARSTAR, SNC_END };
static struct {
    int on, row, n, talk;
    int nrv, step;
    int limit, rest, timing;      /* mTimeLimit (seconds); BombTimerLayout: its rest time, alive */
    int demo, star;               /* mIsDemo; 0, 1 its star asked for, 2 the appear demo seen, 3 over */
    const char *want; int next;   /* requestStartDemoMarioPuppetableWithoutCinemaFrame: the demo asked, its nerve */
    Trk *r;                       /* mRabbit (game_init: row_table) */
} snc;
static void snr_set(int n) { w_nrv(&trk.r.k, n); printf("snow rabbit row %d: nerve %d\n", trk.r.k.row, n); }
static void snc_set(int n) { snc.nrv = n; snc.step = -1; printf("snow rabbit collect: nerve %d\n", n); }
static void snc_request(const char *name, int next) { snc.want = name; snc.next = next; snc_set(SNC_TRYDEMO); }
static void snr_show(int on) { row_demo_hide[trk.r.k.row] = on ? 0 : 255; sw_changed = 1; } /* showModel / hideModel (the rebind) */
static const float *snr_hide_pos(int i) /* tryFindLinkNamePos(隠れ位置) */
{
    const u8 *q = sc->gpos;
    for (u32 j = 0; j < sc->ngpos; j++, q += 48 + 24)
        if (!strcmp((const char *)q, "隠れ位置") && i-- == 0) return (const float *)(q + 48);
    return NULL;
}
static void snr_base(V3 pos, V3 up, V3 front) /* resetPosition with its quat and front */
{
    Kuribo *k = &trk.r.k;
    k->pos = pos; k->qy = up; k->qz = front; k->front = front;
    k->grav = vmul(up, -1.0f);
    trk_zero();
}
static int snr_catchable(const Trk *t) { return t->r.notc == 0 && (t->r.k.nrv == SNR_RUNAWAY || t->r.k.nrv == SNR_BLOW); } /* isEnableCaught */
static void snr_finish(void) /* setFinishPosition */
{
    snr_base(trk.fpos, trk.fup, trk.ffront);
    snr_show(1);
    if (trk.r.talk >= 0) talk_forward_node(trk.r.talk);
    trk.fix = 1;
    snr_set(SNR_GIVEUP);
}
static void snr_update(int i)
{
    GwRabbit *r = &trk.r;
    Kuribo *k = &r->k;
    const Row *row = &sc->objs[k->row];
    if (r->notc > 0) r->notc--; /* control */
    int st = ++k->step;
    switch (k->nrv) {
    case SNR_WAITSNOW:
        if (st == 0) gw_ract(r, "StartEvent");
        gw_turn_to_player(r, 15.0f); trk_zero();
        break;
    case SNR_STARTHIDE:
        if (st == 0) gw_ract(r, "Run");
        w_move_turn(k, vsub(k->pos, mario->pos), 1.4f, 1.0f, 0.9f, 4.0f); /* escapeFromPlayer(sStartAwayParam) */
        if (trk_bck_pass(3.0f)) trk_jump_sound();
        if (st > 120) snr_set(SNR_VANISH);
        break;
    case SNR_VANISH: {
        if (st == 0) { gw_ract(r, "Change"); w_se(k, SE_SM_RABBIT_HIDE); trk_zero(); }
        if (st < 2 || !gw_stopped(k->row)) break; /* isBckStopped */
        w_se(k, SE_SM_METAMORPHOSE_SMOKE);
        const float *g = snr_hide_pos(i);
        if (g) {
            float m[16], one[3] = {1, 1, 1};
            mat_translate_rot_scale(m, g, g + 3, one);
            snr_base(v3(m[12], m[13], m[14]), vnorm(v3(m[4], m[5], m[6])), vnorm(v3(m[8], m[9], m[10]))); /* makeQuatAndFrontFromRotate, resetPosition */
        }
        trk_zero();
        snr_show(0); /* hideModelAndOnCalcAnim */
        snr_set(SNR_HIDESNOW);
        break;
    }
    case SNR_HIDESNOW:
        trk_zero();
        w_turn_to(k, vadd(k->pos, vsub(k->pos, mario->pos)), 15.0f); /* turnDirectionFromPlayerDegree */
        if (row->sw[SW_A] >= 0 && sw_on(row, SW_A)) { snr_show(1); snr_set(SNR_APPEAR); }
        break;
    case SNR_APPEAR:
        if (st == 0) {
            gw_ract(r, "Jump");
            k->vel = vadd(vmul(k->grav, -25.0f), vmul(k->front, 20.0f)); /* setVelocityJump(25), addVelocityMoveToDirection(mFrontVec, 20) */
            w_se(k, SE_SM_RABBIT_APPEAR); se_start(SE_SM_RUNAWAY_RABBIT_APP_ME, NULL);
        }
        gw_grav_vel(r, 1.0f);
        k->vel = vmul(k->vel, 0.99f); /* sBrakeAccelRate */
        if (st > 5 && k->on_ground) snr_set(SNR_RUNAWAY);
        break;
    case SNR_RUNAWAY: {
        if (st == 0) w_snrv(k, RS_WAIT); /* appear */
        rr_state(r);
        int pass = trk_bck_pass(3.0f);
        if (k->snrv == RS_RUNAWAY && pass) trk_jump_sound();
        r->speed = vlen(vtangent(vsub(mario->pos, k->pos), k->grav)) < 600.0f ? 1.2f : 0.7f; /* sPressureRange: sPressureAccel, sNormalAccel */
        break;
    }
    case SNR_BLOW: /* WalkerStateBlowDamage, as TrickRabbitFreeRun's */
        if (k->snrv == 0) {
            if (st == 0) gw_ract(r, "Damage");
            k->vel = vmul(k->vel, 0.99f);
            gw_grav_vel(r, 1.0f);
            turn_on_plane(&k->front, vmul(k->vel, -1.0f), k->grav, cosf(15.0f * 0.0174533f));
            if (st > 5 && k->on_ground) { gw_ract(r, "DamageLand"); trk_zero(); w_snrv(k, 1); }
        } else if (++k->sstep > 30) w_nrv(k, SNR_RUNAWAY);
        break;
    case SNR_CAUGHT:
        if (st == 0) trk.cs = TC_TRY;
        if (tc_update()) {
            pd_end("捕まり");
            if (r->talk >= 0) talk_forward_node(r->talk);
            w_se(k, SE_OJ_STAR_PIECE_BURST);
            trk.fix = 0; /* _1D */
            snr_set(SNR_GIVEUP);
            int all = 1; /* noticeGiveUp: every rabbit in GiveUp (this one's state is still in trk) */
            for (int j = 0; j < snc.n; j++) all &= j == i || snc.r[j].r.k.nrv == SNR_GIVEUP;
            if (all) {
                trk.nopiece = 1;
                if (!snc.demo) { /* startSuccessDemo */
                    snc.demo = 1;
                    se_start(SE_SY_TOTAL_COMPLETE, NULL);
                    snc_request("雪ウサギ集め成功", SNC_SUCCESSWIPEOUT);
                }
            }
            if (!trk.nopiece) launch_pieces(k->pos, vmul(k->grav, -1.0f), 6, 10.0f, 40.0f); /* appearStarPiece(6, sStarPieceHitAccelH, sStarPieceHitAccelV) */
        }
        break;
    case SNR_GIVEUP:
        if (st == 0) { trk.talk_act = "GiveUpTalk"; trk.kill_at_talk = 0; rws_set(RWS_WAIT); }
        rws_update();
        break;
    }
    /* attackSensor (none in HideSnow): its Catch (30 at Spine) on Mario or his spin on its Body catch it; else, while
     * it can not be caught, its body pushes him */
    if (k->nrv != SNR_HIDESNOW) {
        V3 spine = trk_spine();
        if (snr_catchable(&trk)) {
            if (!dx_busy() && !pd.name && (mario_touch(spine, 30.0f) || mario_offence(NULL, spine, 70.0f, k->grav) == MSG_HITALL)) { /* requestCaught */
                for (int j = 0; j < snc.n; j++) if (j != i) snc.r[j].r.notc = 5; /* noticeCaught: setNotCaughtable */
                drop_damage(); /* tryPlayerDropTakingActor */
                snr_set(SNR_CAUGHT);
            }
        } else if (k->nrv != SNR_CAUGHT) mario_push_out(spine, 70.0f);
    }
    gw_move(r);
    if (k->nrv == SNR_CAUGHT && trk.cs != TC_TRY) { mario->pos = r->mpos; mario->up = r->mup; mario->face = vnorm(vtangent(r->mfront, r->mup)); mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* setPlayerBaseMtx */
}
static void snc_player_at(void) /* setPlayerLinkPosAndWait(マリオ位置) */
{
    const float *g = scene_gpos("マリオ位置");
    float m[16], one[3] = {1, 1, 1};
    if (!g) return;
    mat_translate_rot_scale(m, g, g + 3, one);
    m_set_trans(v3(m[12], m[13], m[14])); mario->up = vnorm(v3(m[4], m[5], m[6])); mario->face = vnorm(v3(m[8], m[9], m[10]));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
}
static void snc_camera(void) /* startMultiActorCameraTargetPlayer(ウサギと会話) */
{
    if (row_camset[snc.row] != -1) camera_actor_start("TrickRabbitFreeRunCollect", "ウサギと会話", (unsigned short)row_camset[snc.row], mario->up, mario->face, mario->pos);
}
static void snc_camera_end(void) { if (row_camset[snc.row] != -1) camera_event_stop(); } /* endMultiActorCamera */
static void snc_finish(void) /* setFinishPosition */
{
    for (int i = 0; i < snc.n; i++) { trk = snc.r[i]; snr_finish(); snc.r[i] = trk; }
    snc_player_at();
}
static void snc_talk_place(void) /* its talk at mBaseMtx */
{
    const Row *row = &sc->objs[snc.row];
    float m[16], one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, row->pos, row->rot, one);
    talk_place(snc.talk, m);
}
static void snc_nerve(void)
{
    int st = ++snc.step;
    switch (snc.nrv) {
    case SNC_TRYDEMO:
        if (pd_try_start(snc.want, snc.row)) snc_set(snc.next);
        break;
    case SNC_WAIT:
        if (gw_near_player(row_pos(&sc->objs[snc.row]), 700.0f) && mario->grounded) snc_set(SNC_STARTWIPEOUT); /* sInStartEventRange, isOnGroundPlayer */
        break;
    case SNC_STARTWIPEOUT:
        if (st == 0) menu_wipe(1, -1, 0); /* closeWipeFade */
        if (menu_wipe_active()) break;
        snc_request("雪ウサギ開始", SNC_STARTWIPEIN);
        break;
    case SNC_STARTWIPEIN:
        if (st == 0) {
            drop_damage(); /* tryPlayerKillTakingActor */
            mario_play("battlewait", 1.0f, 1);
            snc_camera();
            snc_player_at(); /* setStartPosition */
            menu_wipe(0, -1, 0); /* openWipeFade */
        }
        if (!menu_wipe_active()) snc_set(SNC_STARTTALK);
        break;
    case SNC_STARTTALK:
        if (st == 0) snc_player_at();
        snc_talk_place();
        if (snc.talk < 0 || talk_force_at_end(snc.talk, 1)) snc_set(SNC_WAITHIDE); /* tryTalkForceWithoutDemoMarioPuppetableAtEnd */
        break;
    case SNC_WAITHIDE: {
        int all = 1;
        for (int i = 0; i < snc.n; i++) {
            Kuribo *k = &snc.r[i].r.k;
            if (st == 0 && k->nrv == SNR_WAITSNOW) { w_nrv(k, SNR_STARTHIDE); printf("snow rabbit row %d: nerve %d\n", k->row, SNR_STARTHIDE); } /* requestStartHideSnow */
            all &= k->nrv == SNR_HIDESNOW;
        }
        if (all) {
            snc_camera_end();
            pd_end("雪ウサギ開始");
            snc_set(SNC_START);
        }
        break;
    }
    case SNC_START:
        if (st == 0) {
            bgm_start("MBGM_GALAXY_02_CHASE"); /* startEventBGM(1): EventBgmID_Chase */
            snc.rest = snc.limit * 60; snc.timing = 1; /* setTimeLimit, mDangerTransFrame 20 * 60, appear */
        }
        if (snc.rest > 0 && !pd.name && !dx_busy()) snc.rest--; /* BombTimerLayout::update */
        if (life > 0 && !pd.name) { /* !isPlayerDead && !isPlayerConfrontDeath */
            int rest = snc.rest;
            if (rest == 0) se_start(SE_SY_E3_TIMER_TIME_UP, NULL);
            else if (rest % 60) ;
            else if (rest <= 120) se_start(SE_SY_E3_TIMER_COUNT_4, NULL);
            else if (rest <= 360) se_start(SE_SY_E3_TIMER_COUNT_2, NULL);
            else if (rest <= 1200) se_start(SE_SY_E3_TIMER_COUNT_1, NULL);
        }
        if (snc.rest == 0 && !snc.demo) { /* isReadyToTimeUp: startFailedDemo */
            snc.demo = 1;
            snc_request("雪ウサギ集め失敗", SNC_FAILEDWIPEOUT);
        }
        break;
    case SNC_FAILEDWIPEOUT:
        if (st == 0) { bgm_stop(60); menu_wipe(1, -1, 0); }
        if (menu_wipe_active()) break;
        change_item_status(8); /* curePlayerElementMode */
        drop_damage();
        snc.timing = 0; /* mTimerLayout->kill() */
        snc_set(SNC_FAILEDWIPEIN);
        break;
    case SNC_FAILEDWIPEIN:
        if (st == 0) {
            snc_finish();
            mario_play("battlewait", 1.0f, 1);
            snc_camera();
            bgm_return(); /* startLastStageBGM */
            menu_wipe(0, -1, 0);
        }
        if (menu_wipe_active()) break;
        gw_branch(snc.talk, 1, 0); /* forwardNodeNextBranchRight */
        snc_set(SNC_FAILEDTALK);
        break;
    case SNC_FAILEDTALK:
        snc_talk_place();
        if (snc.talk < 0 || talk_force_at_end(snc.talk, 1)) {
            snc_camera_end();
            pd_end("雪ウサギ集め失敗");
            se_start(SE_PM_LAST_DAMAGE, &mario->pos.x);
            miss_race = 1; miss(); /* forceKillPlayerByGroundRace */
            snc_set(SNC_END);
        }
        break;
    case SNC_SUCCESSWIPEOUT:
        if (st == 0) { bgm_stop(120); menu_wipe(1, -1, 0); }
        if (menu_wipe_active()) break;
        change_item_status(8);
        drop_damage();
        snc_finish();
        snc_set(SNC_SUCCESSWIPEIN);
        break;
    case SNC_SUCCESSWIPEIN:
        if (st == 0) {
            snc.timing = 0;
            mario_play("battlewait", 1.0f, 1);
            snc_camera();
            bgm_return();
            menu_wipe(0, -1, 0);
        }
        if (menu_wipe_active()) break;
        gw_branch(snc.talk, 1, 1); /* forwardNodeNextBranchLeft */
        snc_set(SNC_SUCCESSTALK);
        break;
    case SNC_SUCCESSTALK:
        snc_talk_place();
        if (snc.talk < 0 || talk_force_at_end(snc.talk, 1)) snc_set(SNC_TAKEOUTSTAR);
        break;
    case SNC_TAKEOUTSTAR: {
        GwRabbit *r = &snc.r[snc.n > 2 ? 2 : snc.n - 1].r; /* mRabbit[2] */
        if (st == 0) { tos_appear(r->k.row, "TakeOutStarTrickRabbit"); gw_ract(r, "TakeOutStarTrickRabbit"); }
        if (st > 1 && gw_stopped(r->k.row)) { gw_ract(r, "Wait"); snc_set(SNC_APPEARSTAR); } /* isActionEnd */
        break;
    }
    case SNC_APPEARSTAR:
        if (st == 0) {
            tos_kill(); /* the star out of the model's joint */
            note_star_row = snc.row; /* (its declarer: the collector) */
            pd_end("雪ウサギ集め成功"); /* (the star's own demo: appearPowerStarContinueCurrentDemo) */
            snc.star = 1; sw_changed = 1;
            bgm_after_boss();
        }
        if (snc.star == 1 && pd.name && !strcmp(pd.name, "パワースター出現")) snc.star = 2;
        if (snc.star == 2 && !pd.name) { /* isEndPowerStarAppearDemo */
            snc.star = 3;
            snc_camera_end();
            snc_set(SNC_END);
        }
        break;
    case SNC_END: break;
    }
}
/* SnowFloor::attackSensor on a rabbit's Body (70 at Spine): 1 hidden by the snow (ACTMES_ASK_HIDDEN_BY_SNOW: in
 * HideSnow without a SW_A), 0 not (the floor digs at its feet), -1 no such rabbit */
static int snc_snow_body(int i, V3 *body, V3 *up, int *row)
{
    if (!snc.on || i >= snc.n) return -1;
    const Kuribo *k = &snc.r[i].r.k;
    *body = vadd(k->pos, vmul(k->qy, 50.0f)); *up = k->qy; *row = k->row;
    return sc->objs[k->row].sw[SW_A] < 0 && k->nrv == SNR_HIDESNOW;
}
/* ACTMES_NOTIFY_DISCOVER_SNOW to the rabbit of that row: 1 it comes out; with body only its sensor's place (0);
 * -1 not a rabbit's row */
static int snc_snow_found(int row, V3 *body)
{
    for (int i = 0; snc.on && i < snc.n; i++) {
        Kuribo *k = &snc.r[i].r.k;
        if (k->row != row) continue;
        if (body) { *body = vadd(k->pos, vmul(k->qy, 50.0f)); return 0; }
        if (sc->objs[row].sw[SW_A] >= 0 || k->nrv != SNR_HIDESNOW) return 0;
        row_demo_hide[row] = 0; sw_changed = 1; /* showModel */
        w_nrv(k, SNR_APPEAR);
        printf("snow rabbit row %d: found under the snow\n", row);
        return 1;
    }
    return -1;
}
static int snc_moves(int row) /* setStartPosition / setFinishPosition: requestMovementOn(mRabbit[i]) in its own demos */
{
    if (!snc.on || !pd.name || (strncmp(pd.name, "雪ウサギ", strlen("雪ウサギ")) && !(snc.star && snc.star < 3))) return 0;
    for (int i = 0; i < snc.n; i++) if (snc.r[i].r.k.row == row) return 1;
    return 0;
}
static int snc_shot(V3 p) /* a star piece on a rabbit's Body: receiveMsgBlowDamage in Runaway (sAppearSnowJumpH / V 20) */
{
    for (int i = 0; i < snc.n; i++) {
        Kuribo *k = &snc.r[i].r.k;
        V3 spine = vadd(k->pos, vmul(k->qy, 50.0f));
        if (k->nrv != SNR_RUNAWAY || vlen(vsub(p, spine)) > 70.0f + 30.0f) continue;
        w_se(k, SE_SM_RABBIT_STAR_PIECE_HIT); w_se(k, SE_SV_RABBIT_STAR_PIECE_HIT);
        V3 side = vtangent(vsub(spine, p), k->grav);
        k->vel = vadd(vmul(vlen(side) > 1e-6f ? vnorm(side) : v3(0, 0, 0), 20.0f), vmul(k->grav, -20.0f)); /* setVelocitySeparateHV */
        w_nrv(k, SNR_BLOW); w_snrv(k, 0);
        printf("snow rabbit row %d: star piece, BlowDamage\n", k->row);
        return 1;
    }
    return 0;
}
#ifdef TRICK_TEST
static void snc_test(void);
#endif
static void snc_update(void)
{
#ifdef TRICK_TEST
    snc_test();
#endif
    for (int i = 0; i < snc.n; i++) {
        if (!w_zone_ground(&snc.r[i].r.k)) return; /* (its zone's collision not loaded yet) */
        trk = snc.r[i];
        snr_update(i);
        snc.r[i] = trk;
    }
    snc_nerve();
}
static void snc_init(void)
{
    memset(&snc, 0, sizeof snc);
    int t = scene_type(sc, "TrickRabbitFreeRunCollect"), c = scene_type(sc, "TrickRabbitFreeRunChild");
    snc.r = t < 0 ? NULL : row_table("TrickRabbitFreeRunChild", SNC_MAX, sizeof(Trk));
    if (!snc.r) return;
    for (u32 i = 0; i < sc->nobjs; i++) if ((int)sc->objs[i].type == t) { snc.row = (int)i; break; }
    for (u32 i = 0; i < sc->nobjs && snc.n < SNC_MAX; i++) { /* initRabbits: its children */
        if ((int)sc->objs[i].type != c) continue;
        Trk *q = &snc.r[snc.n++];
        memset(q, 0, sizeof *q);
        gw_body_init(&q->r, (int)i);
        q->r.talk = gw_talker((int)i, 120.0f);
        q->re.c158 = 1024;
        q->nostar = q->fix = 1; /* unusePowerStarModel; _1C, _1D false */
        q->fpos = q->r.k.pos; q->fup = q->r.k.qy; q->ffront = q->r.k.qz;
        w_nrv(&q->r.k, SNR_WAITSNOW);
        printf("snow rabbit row %u: talker %d, SW_A %d\n", i, q->r.talk, sc->objs[i].sw[SW_A]);
    }
    snc.talk = gw_talker(snc.row, 120.0f);
    snc.limit = sc->objs[snc.row].arg[0] != -1 ? sc->objs[snc.row].arg[0] : 180; /* getJMapInfoArg0NoInit */
    snc.on = 1;
    snc.nrv = SNC_WAIT; snc.step = -1;
    printf("snow rabbit collect row %d: %d rabbits, talker %d, %d s, camera set %d\n", snc.row, snc.n, snc.talk, snc.limit, row_camset[snc.row]);
}
static void trk_update(void)
{
    if (snc.on) { snc_update(); return; }
    if (!trk.on || !row_shown(trk.r.k.row)) return;
#ifdef TRICK_TEST
    trk_test();
#endif
    if (!w_zone_ground(&trk.r.k)) return; /* (its zone's collision not loaded yet: it would fall through its planet) */
    if (trk.kind) trm_update();
    else tf_update();
}
/* a star piece on its Body: TrickRabbitFreeRun in Runaway (isEnableBlowDamage) receiveMsgBlowDamage; TrickRabbit out
 * of WaitStart / GiveUp its two hit sounds and receiveMsgTrample */
static int trk_shot(V3 p)
{
    Kuribo *k = &trk.r.k;
    if (snc.on) return snc_shot(p);
    if (!trk.on || vlen(vsub(p, trk_spine())) > 70.0f + 30.0f) return 0;
    if (trk.kind) {
        if (k->nrv == TRM_WAITSTART || k->nrv == TRM_GIVEUP) return 0; /* (RabbitStateWaitStart's: npc_shot) */
        w_se(k, SE_SM_RABBIT_STAR_PIECE_HIT); w_se(k, SE_SV_RABBIT_STAR_PIECE_HIT);
        return trm_trample();
    }
    if (k->nrv != TF_RUNAWAY) return 0;
    V3 side = vtangent(vsub(trk_spine(), p), k->grav);
    k->vel = vadd(vmul(vlen(side) > 1e-6f ? vnorm(side) : v3(0, 0, 0), 20.0f), vmul(k->grav, -20.0f)); /* setVelocitySeparateHV */
    w_nrv(k, TF_BLOW); w_snrv(k, 0);
    printf("trick rabbit row %d: star piece, BlowDamage\n", k->row);
    return 1;
}
static int trk_star_out(int row) { return (trk.on && trk.r.k.row == row && trk.star) || (snc.on && snc.row == row && snc.star); }
static int trk_puppet(void) /* Mario's clip is the demo's own */
{
    if (snc.on && pd.name && (!strcmp(pd.name, "捕まり") || !strncmp(pd.name, "雪ウサギ", strlen("雪ウサギ")))) return 1;
    return trk.on && pd.name && (!strcmp(pd.name, "逃走") || !strcmp(pd.name, "捕まり"));
}
static void trk_init(void)
{
    memset(&trk, 0, sizeof trk);
    snc_init();
    if (snc.on) return;
    for (u32 i = 0; i < sc->nobjs && !trk.on; i++)
        if (!strcmp(sc->types + 32 * sc->objs[i].type, "TrickRabbitFreeRun")) {
            gw_body_init(&trk.r, (int)i);
            trk.r.talk = gw_talker((int)i, 120.0f);
            trk.re.c158 = 1024;
            trk.on = 1;
            w_nrv(&trk.r.k, TF_WAITSTART);
            printf("trick rabbit row %u: TrickRabbitFreeRun, talker %d\n", i, trk.r.talk);
        }
    const unsigned char *g = trk.on || !scene_blob_g ? NULL : blob_chunk(scene_blob_g, "RGPH", NULL);
    if (!g) return;
    u32 n = *(const u32 *)g;
    g += 4;
    for (u32 i = 0; i < n && !trk.on; i++) { /* slice.py rgph_chunk: {s32 row, u32 nnodes, u32 nedges, nodes, edges} */
        int row = *(const int *)g;
        u32 nn = *(const u32 *)(g + 4), ne = *(const u32 *)(g + 8);
        if (row >= 0 && row < (int)sc->nobjs && !strcmp(sc->types + 32 * sc->objs[row].type, "TrickRabbit") && nn && ne) {
            memset(&trg, 0, sizeof trg);
            trg.nodes = (const float *)(g + 12); trg.edges = (const TgEdge *)(g + 12 + 12 * nn); trg.nn = (int)nn; trg.ne = (int)ne;
            trg.cur = trg.prev = trg.sel = -1;
            gw_body_init(&trk.r, row);
            trk.r.talk = gw_talker(row, 120.0f);
            trk.re.c158 = 1024;
            trk.on = trk.kind = 1;
            w_nrv(&trk.r.k, trk.r.talk >= 0 ? TRM_WAITSTART : TRM_WAIT); /* init: no talk, Wait */
            printf("trick rabbit row %d: TrickRabbit, talker %d, graph %u nodes %u edges\n", row, trk.r.talk, nn, ne);
        }
        g += 12 + 12 * nn + 16 * ne;
    }
}
#ifdef TRICK_TEST /* XF=-DTRICK_TEST (plain, ReverseKingdomGalaxy/1): Mario put before the rabbit once he is free, A through
                   * its talk; 300 steps into its Runaway a star piece at it, at 500 Mario onto it (the catch), A through
                   * the caught talk */
static void trk_test(void)
{
    static int t, run;
    Kuribo *k = &trk.r.k;
    if (!trk.on || in_opening || mario->launch >= 0) return;
    t++;
    pad_force = 0;
    if (k->nrv == TF_WAITSTART) {
        if (t > 60 && t < 200) { mario->pos = vadd(k->pos, vadd(vmul(k->front, 170.0f), vmul(k->qy, 30.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        if (t > 260 && t % 45 < 5) pad_force = PAD_CROSS;
    }
    if (k->nrv == TF_RUNAWAY) {
        run++;
        if (run == 300) { shots[0].pos = vadd(trk_spine(), vmul(k->front, 200.0f)); shots[0].vel = vmul(k->front, -40.0f); shots[0].t = 1; shots[0].col = 0; }
        if (run == 500) { mario->pos = vadd(k->pos, vmul(k->qy, 10.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    }
    if ((k->nrv == TF_CAUGHT || k->nrv == TF_GIVEUP) && t % 45 < 5) pad_force = PAD_CROSS;
    if (trk.kind) { /* CosmosGardenGalaxy/3: its talk, the demo; Mario kept 500 behind it while it runs, dropped onto it
                     * at 600 steps of running (Tumble), onto its place at 1200 (the catch) */
        static int go;
        pad_force = 0;
        if (k->nrv == TRM_WAITSTART) {
            if (t > 60 && k->step < 60) { /* held until its zone's collision is loaded (its steps run); on its route's ground: toward the graph node 100 .. 400 off that is nearest (its front may point off its platform) */
                V3 d = k->front; float best = 3.4e38f;
                for (int n = 0; n < trg.nn; n++) { V3 v = vtangent(vsub(trg_node(n), k->pos), k->grav); float l = vlen(vsub(trg_node(n), k->pos)); if (l > 100.0f && l < best && vlen(v) > 50.0f) { best = l; d = vnorm(v); } }
                mario->pos = vadd(k->pos, vadd(vmul(d, 170.0f), vmul(k->qy, 30.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            }
            if (k->step > 150 && t % 45 < 5) pad_force = PAD_CROSS;
        }
        if (k->nrv == TRM_WAIT || k->nrv == TRM_RUNAWAY || k->nrv == TRM_JUMPSTART || k->nrv == TRM_JUMP || k->nrv == TRM_BRAKETURN) {
            go++;
            if (go % 90 == 0 && go != 600 && go < 1200) { mario->pos = trg.prev >= 0 ? vadd(trg_node(trg.prev), vmul(k->qy, 60.0f)) : vadd(k->pos, vadd(vmul(k->front, -300.0f), vmul(k->qy, 60.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
            if (go == 600) { mario->pos = vadd(k->pos, vmul(k->qy, 300.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
            if (go >= 1200 && go % 30 == 0) { mario->pos = vadd(k->pos, vmul(k->qy, 10.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        }
        if ((k->nrv == TRM_CAUGHT || k->nrv == TRM_GIVEUP) && t % 45 < 5) pad_force = PAD_CROSS;
        if (t % 60 == 0) printf("trick test t %d: go %d, node %d prev %d edge %d, level %d, d0 %d, pos %d %d %d\n", t, go, trg.cur, trg.prev, trg.sel, trg.level, (int)trg.d0, (int)k->pos.x, (int)k->pos.y, (int)k->pos.z);
    }
    if (t % 61 == 0)
        printf("trick test t %d: nerve %d step %d, wait %d, runaway %d, caught %d, star %d, Mario %d away, ground %d wall %d air %d ml %d, pos %d %d %d vel %d grav %d %d %d, demo %s\n", t, k->nrv, k->step,
               trk.ws, k->snrv, trk.cs, trk.star, (int)vlen(vsub(mario->pos, k->pos)), k->on_ground, k->on_wall, trk.r.air, k->ml, (int)k->pos.x, (int)k->pos.y, (int)k->pos.z,
               (int)vlen(k->vel), (int)(k->grav.x * 100), (int)(k->grav.y * 100), (int)(k->grav.z * 100), pd.name ? pd.name : "-");
    if (t % 61 == 0) printf("trick test: Mario %d %d %d up %d %d %d grounded %d, placed %d %d %d\n", (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z,
                            (int)(mario->up.x * 100), (int)(mario->up.y * 100), (int)(mario->up.z * 100), mario->grounded,
                            (int)sc->objs[k->row].pos[0], (int)sc->objs[k->row].pos[1], (int)sc->objs[k->row].pos[2]);
}
/* XF=-DTRICK_TEST (plain, SnowCapsuleGalaxy/1): Mario put at マリオ位置 (the start event), A through the talks; in the
 * hunt each rabbit in turn (700 steps apart): its SW_A on (without one Mario dropped onto its snow, at 300 forced out),
 * a star piece at it at 400, Mario onto it from 550 (the catch). XF=-DTRICK_TEST=2: nothing caught, the time up after
 * 10 s (the failed demo) */
static void snc_test(void)
{
    static int t, go;
    if (!snc.on || in_opening || mario->launch >= 0) return;
    t++;
    pad_force = 0;
    if (snc.nrv == SNC_WAIT && t > 60 && t % 120 == 61) {
        const float *g = scene_gpos("マリオ位置");
        if (g) { m_set_trans(vadd(v3(g[0], g[1], g[2]), vmul(snc.r[0].r.k.qy, 30.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    }
    if ((snc.nrv == SNC_STARTTALK || snc.nrv == SNC_FAILEDTALK || snc.nrv == SNC_SUCCESSTALK) && t % 45 < 5) pad_force = PAD_CROSS;
    for (int i = 0; i < snc.n; i++) if (snc.r[i].r.k.nrv == SNR_CAUGHT && t % 45 < 5) pad_force = PAD_CROSS;
    if (snc.nrv == SNC_START) {
        go++;
#if TRICK_TEST == 2
        if (go == 1) snc.rest = 600;
#else
        for (int i = 0; i < snc.n; i++) {
            Kuribo *k = &snc.r[i].r.k;
            const Row *row = &sc->objs[k->row];
            int at = go - 700 * i;
            if (at == 60 && row->sw[SW_A] >= 0) sw_set(row, SW_A, 1);
            if (row->sw[SW_A] < 0 && at >= 60 && at < 300 && at % 30 == 0 && k->nrv == SNR_HIDESNOW) { m_set_trans(vadd(k->pos, vmul(k->qy, 150.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
            if (at == 300 && k->nrv == SNR_HIDESNOW) printf("trick test: rabbit %d forced out (%d)\n", i, snc_snow_found(k->row, NULL));
            if (at == 400 && k->nrv == SNR_RUNAWAY) { V3 sp = vadd(k->pos, vmul(k->qy, 50.0f)); shots[0].pos = vadd(sp, vmul(k->front, 200.0f)); shots[0].vel = vmul(k->front, -40.0f); shots[0].t = 1; shots[0].col = 0; }
            if (at >= 550 && at < 700 && at % 30 == 0 && (k->nrv == SNR_RUNAWAY || k->nrv == SNR_BLOW)) { m_set_trans(vadd(k->pos, vmul(k->qy, 10.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        }
#endif
    }
    if (t % 61 == 0) {
        printf("trick test t %d go %d: collect %d step %d, rest %d, star %d, demo %s, Mario %d %d %d grounded %d;", t, go, snc.nrv, snc.step, snc.rest, snc.star, pd.name ? pd.name : "-",
               (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z, mario->grounded);
        for (int i = 0; i < snc.n; i++) {
            const Kuribo *k = &snc.r[i].r.k;
            printf(" [%d: nerve %d step %d wait %d caught %d shown %d ground %d pos %d %d %d]", i, k->nrv, k->step, snc.r[i].ws, snc.r[i].cs, row_shown(k->row), k->on_ground, (int)k->pos.x, (int)k->pos.y, (int)k->pos.z);
        }
        printf("\n");
    }
}
#endif
