/* race.inc (#included by game.c): Map/RaceManager.cpp, Map/RaceRail.cpp (PlayerRacer) and NPC/TeresaRacer.cpp after
 * Petari.
 * RaceManager (rm): made by the first racer's entry (RaceManagerFunction::entryRacer*): RaceRail's PlayerRacer (the
 * judge rail: its rail's path_arg0 the race id, slice.py PATH; its best time from the game data; _FC the inverse of
 * the rail end's frame: a racer is at the goal when it crosses that plane forwards) and the others in placement
 * order. Nerves as Petari: Wait, Prep (initRacer), the wipes (stopStageBGM 90, closeWipeFade, prepRace, 30 frames,
 * BGM_MINIGAME_START, openWipeFade), Intro 110, Count (the Go count, SE_SY_RACE_COUNT_DOWN every 60, the race's event
 * BGM at sBgmStartStep 166, then playGo and startRace), Race (the time, its max, playRank, updateRacer from mRank on,
 * the rest sorted by rail coord), Demo (canStartDemo: goalRace, offPlayerControl), Goal (stopStageBGM 30,
 * BGM_RACE_WIN / LOSE, Goal / Lose / TimeUp), Rank (60 frames, the レース demo), the wipes back (resetRace,
 * startCurrentStageBGM, endDemo, exitRacer). PlayerRacer: updateRacer by Mario's centre on its rail (sRaceJudgeLength
 * 10000, its point arg0 gates), prepRacer / resetRacer per race (the Phantom's: on the ground, BattleWait; after:
 * レース終了後位置, Watch), its actor camera (RaceRail固有) from prepRacer to startRacer.
 * TeresaRacer (rn kind 1): its talk (setDistanceToTalk 500, the balloon 150 up; branchFunc rank 1, animeFunc 0 Close
 * / 1 Defeat / 2 3 Open, eventFunc its TakeOutStar), mParam (WaitOpen / WaitOpenTalk, _8 1.5, _4 3000, Chase its move
 * action, _10C 20, _110 0.1, _114 0.05), its nerves Wait / Pre / Ready / Move / Goal / Post / Talk / TakeOutStar, its
 * 会話 camera, Move by its rail points' arg0 (adjustmentRailCoordSpeed, decidePose 1 / 0.05 / 0.1), Goal near the
 * rail's end (sNearGoalDist 500), changeBgmState (after 480 frames: state 2 within 3000 of Mario, else 1).
 * ponytail: the star pointer demo modes (startStarPointerModeDemo(WithStarPointer) / endStarPointerMode), the
 * layout's RaceStart / RaceGoal effects, TeresaRacer's trample reaction SE (no NPC reaction sensor here), the raster
 * scroll of TeresaMario2D (setRasterScroll: its level SE only), the PowerStarDemoModel in its hands (the star comes
 * out at the action's end: star_pos), Luigi's TeresaRacer (arg0 1: slice.py declares Mario's), audiences (only
 * PenguinCoach's surfing modes enter one), the rush actor messages (ACTMES_RACE_*: SurfRay's) */
enum { RM_WAIT, RM_PREPWIPE, RM_PREPIMME, RM_PREWIPEOUT, RM_PREWIPEWAIT, RM_PREWIPEIN, RM_INTRO, RM_COUNT, RM_RACE,
       RM_DEMO, RM_GOAL, RM_RANK, RM_PSTWIPEOUT, RM_PSTWIPEWAIT, RM_PSTWIPEIN };
enum { RACE_PENGUIN, RACE_TERESA_PHANTOM, RACE_TERESA_DEATH_PROMENADE, RACE_SURFING_TRIAL, RACE_SURFING_CHALLENGE };
static const struct { const char *name, *bgm; int max; } rm_struct[5] = { /* sRaceStruct: mName, mEventBgmId, mMaxTime */
    {"ペンギンレース[オーシャンリング]", "MBGM_RACE_01", 120}, {"テレサレース[ファントム]", "MBGM_RACE_02", 120},
    {"テレサレース[デスプロムナード]", "MBGM_RACE_02", 120}, {"サーフィン[トライアル]", "MBGM_RACE_01", 0},
    {"サーフィン[チャレンジ]", "MBGM_RACE_01", 180}};
#define RM_MAX 10
typedef struct { int npc; float coord; V3 cur, prev; } RmRacer; /* AbstractRacer (npc -1: the PlayerRacer) */
typedef struct { /* a rail rider: the rail, its coord, speed, direction (1 to its end) */
    const unsigned char *path; const float *pts; u32 np; int cl; float len, coord, speed; int dir;
} RmRail;
static struct {
    const Scene *scene; int on, row, race, nrv, step, next, req;
    RmRacer r[RM_MAX]; int ord[RM_MAX], n, rank; /* mRacer (indices into r), mRacerNum, mRank */
    u32 best, time;
    V3 end_p, end_x, end_y, end_z; /* _FC (inverted: a point's local z is its offset along end_z) */
    RmRail rail;   /* the PlayerRacer's */
    int ctrl_off, cam; const char *bck;
    int test_f;
} rm;
enum { TRC_WAIT, TRC_PRE, TRC_READY, TRC_MOVE, TRC_GOAL, TRC_POST, TRC_TALK, TRC_TAKEOUT };
#define RN_MAX 8
typedef struct {
    int row, kind, id, ar, dead; /* kind 1 TeresaRacer; id: mRacerId */
    int nrv, step, next, pushed; /* pushed: pushNerve's (-1 none) */
    RmRail rail;
    float speed, accel, blend;   /* _10C, _110, _114 */
    Inst pose;                   /* its _A0 (nf / nu), _B0 (nf0 / nu0), position (m[12..14]); _C0 pos0 */
    V3 pos0;
    const char *mv[2], *tk[2], *act; /* mParam _14 / _18, _1C / _20, _11C */
    u8 p0, p1; float p4, p8;         /* mParam _0 / _1, _4, _8 */
    int k, cam, goal, bgm, took, tos, tos_step; /* talker; 会話 camera on; mIsGoal; mBgmState; star out; TakeOutStar (0 Anim, 1 Term) */
    u32 lv, lv2;
} RNpc;
static RNpc rn[RN_MAX];
static int nrn;
static void rm_reset(void) { rm.scene = NULL; rm.on = 0; rm.ctrl_off = 0; rm.bck = NULL; nrn = 0; }
static int rm_ctrl_off(void) { return rm.scene == sc && rm.ctrl_off; }
static const char *rm_bck(void) { return rm.scene == sc && pd.name && !strcmp(pd.name, "レース") ? rm.bck : NULL; }
static int race_took(int row) { for (int i = 0; i < nrn; i++) if (rn[i].row == row) return rn[i].took; return 0; }

/* --- the rail rider (RailRider.cpp / RailUtil) */
static int rr_init(RmRail *rr, const Row *r)
{
    memset(rr, 0, sizeof *rr);
    if (r->path < 0 || !scene_blob_g) return 0;
    rr->path = find_path(scene_blob_g, r->zone, r->path);
    rr->pts = find_rail(scene_blob_g, r->zone, r->path, &rr->np, &rr->cl);
    if (!rr->path || !rr->pts || rr->np < 2) return 0;
    rr->len = nrail_len(rr->pts, rr->np, rr->cl);
    rr->dir = 1;
    return 1;
}
static int rr_parg0(const RmRail *rr) { return rr->path ? *(const int *)(rr->path + 20) : -1; } /* getRailArg0WithInit */
static int rr_npoints(const RmRail *rr) { return rr->path ? (int)*(const u32 *)(rr->path + 12) : 0; }
static float rr_point_coord(const RmRail *rr, int k)
{
    u32 np = *(const u32 *)(rr->path + 8), nr = *(const u32 *)(rr->path + 12);
    return *(const float *)(rr->path + 24 + 12 * np + 12 * nr + 12 * k);
}
static int rr_point_arg(const RmRail *rr, int k, int which) /* point_arg0 / 1 of raw point k */
{
    u32 np = *(const u32 *)(rr->path + 8), nr = *(const u32 *)(rr->path + 12);
    if (k < 0 || (u32)k >= nr) return -1;
    const unsigned char *rec = rr->path + 24 + 12 * np + 12 * nr;
    return which == 0 ? *(const int *)(rec + 12 * k + 4) : *(const int *)(rec + 12 * nr + 8 * k + 4 * (which - 1));
}
static int rr_point_no(const RmRail *rr) /* getCurrentRailPointNo: the last point passed going its way */
{
    int n = rr_npoints(rr), cur = rr->dir > 0 ? 0 : n - 1;
    for (int k = 0; k < n; k++) {
        float c = rr_point_coord(rr, k);
        if (rr->dir > 0 ? c <= rr->coord + 0.01f : c >= rr->coord - 0.01f) { if (rr->dir > 0) cur = k; else { cur = k; break; } }
    }
    return cur;
}
static V3 rr_pos(const RmRail *rr) { V3 d; return nrail_at(rr->pts, rr->np, rr->cl, rr->coord, &d); } /* getRailPos */
static V3 rr_dir(const RmRail *rr) { V3 d; nrail_at(rr->pts, rr->np, rr->cl, rr->coord, &d); return vmul(d, (float)rr->dir); } /* getRailDirection */
static void rr_adjust_speed(RmRail *rr, float target, float accel) /* adjustmentRailCoordSpeed */
{
    if (rr->speed < target) rr->speed = fminf(rr->speed + accel, target);
    else if (rr->speed > target) rr->speed = fmaxf(rr->speed - accel, target);
}
static void rr_move(RmRail *rr) /* moveRailRider */
{
    rr->coord += rr->speed * (float)rr->dir;
    if (rr->cl) rr->coord = fmodf(rr->coord + 4.0f * rr->len, rr->len);
    else rr->coord = fminf(fmaxf(rr->coord, 0.0f), rr->len);
}
static int rr_reached_goal(const RmRail *rr) { return !rr->cl && (rr->dir > 0 ? rr->coord >= rr->len : rr->coord <= 0.0f); } /* isRailReachedGoal */
static int rr_near_goal(const RmRail *rr, float d) { return !rr->cl && (rr->dir > 0 ? rr->coord >= rr->len - d : rr->coord <= d); } /* isRailReachedNearGoal */

/* --- RaceManager */
static int rm_can_demo(void) { return !pd.name && !dx.on && dx.req < 0 && !in_opening; } /* MR::canStartDemo */
static void rm_set(int n) { rm.next = n; }
static int rm_nerve_is_pre(void) { return rm.nrv == RM_PREWIPEOUT || rm.nrv == RM_PREWIPEWAIT || rm.nrv == RM_PREWIPEIN; }
static int rm_is_goal(const RmRacer *a) /* isGoal: _FC's z of its last place below 0, of its place over 0 */
{
    return vdot(vsub(a->prev, rm.end_p), rm.end_z) < 0.0f && vdot(vsub(a->cur, rm.end_p), rm.end_z) > 0.0f;
}
static u32 rm_rank_of(int ar) /* getRank */
{
    u32 rank = (u32)rm.n;
    for (int i = 0; i < rm.n; i++) if (rm.ord[i] == ar) rank = (u32)i;
    return rank + 1;
}
static void rm_renew_time(void) /* renewTime */
{
    rm.time = (u32)rm.step;
    if (rm.time < rm.best) {
        char hi[48], lo[48];
        snprintf(hi, sizeof hi, "%s/hi", rm_struct[rm.race].name); snprintf(lo, sizeof lo, "%s/lo", rm_struct[rm.race].name);
        gd_set_value(hi, (int)(rm.time >> 16)); gd_set_value(lo, (int)(rm.time & 0xFFFF)); /* setRaceBestTime */
    }
    printf("race: time %u (best %u), rank %d" "\n", rm.time, rm.best, rm.rank);
}
static void rm_player_at(V3 pos, V3 up, V3 front) /* setPlayerBaseMtx: Mario at the matrix, still */
{
    mario->pos = pos; mario->up = vnorm(up);
    V3 f = vtangent(front, mario->up);
    if (vlen(f) > 1e-6f) mario->face = vnorm(f);
    mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->launch = -1;
}
static void rm_player_bck(const char *b) { rm.bck = b; } /* startBckPlayer (played while the レース demo holds him) */
static V3 rm_player_center(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); } /* getPlayerCenterPos */
static void rn_reset_racer(RNpc *t);
static void rn_prep_racer(RNpc *t);
static void rn_start_racer(RNpc *t);
static int rn_update_racer(RNpc *t, RmRacer *a);
static int rn_goal_racer(RNpc *t);
static void rn_lose_racer(RNpc *t);
static void rn_exit_racer(RNpc *t);
/* PlayerRacer */
static void pr_prep(void) /* PlayerRacer::prepRacer */
{
    RmRail *rr = &rm.rail;
    rr->coord = 0.0f; rr->dir = 1; /* moveCoordToStartPos */
    V3 p = rr_pos(rr), up = up_at(p, mario->up); /* moveTransToCurrentRailPos, calcGravityVector */
    if (rm.race == RACE_TERESA_PHANTOM) { /* convertPosOnGround(grav x 1000), BattleWait */
        V3 hp, n; int inst;
        if (map_line(p, vmul(up, -1000.0f), &hp, &n, &inst)) p = hp;
        rm_player_bck("battlewait");
    } else if (rm.race == RACE_TERESA_DEATH_PROMENADE) rm_player_bck("wait"); /* (onFollowDemoEffect) */
    rm_player_at(p, up, rr_dir(rr)); /* makeMtxFrontUpPos(getRailDirection, -grav), setPlayerBaseMtx */
    if (row_camset[rm.row] != -1) /* startActorCameraTargetPlayer */
        rm.cam = camera_actor_start("RaceRail", "", (unsigned short)row_camset[rm.row], mario->up, mario->face, mario->pos);
    printf("race: Mario at the rail's start (%d %d %d), camera %d" "\n", (int)p.x, (int)p.y, (int)p.z, rm.cam);
}
static void pr_reset(void) /* PlayerRacer::resetRacer: setPlayerPosOnGround(レース終了後位置) */
{
    const float *g = scene_gpos("レース終了後位置");
    if (g) {
        float m[16], one[3] = {1, 1, 1}, rot[3] = {g[3], g[4], g[5]};
        mat_translate_rot_scale(m, g, rot, one);
        V3 p = v3(g[0], g[1], g[2]), up = up_at(p, v3(m[4], m[5], m[6])), hp, n; int inst;
        if (map_line(vadd(p, vmul(up, 100.0f)), vmul(up, -1000.0f), &hp, &n, &inst)) p = hp;
        rm_player_at(p, up, v3(m[8], m[9], m[10]));
        mario->grounded = 1;
    }
    rm_player_bck(rm.race == RACE_TERESA_DEATH_PROMENADE ? (rm.rank != 1 ? "sad" : "wait") : "watch");
}
static int pr_update(RmRacer *a) /* PlayerRacer::updateRacer */
{
    RmRail *rr = &rm.rail;
    V3 c = rm_player_center(), dd;
    float coord = nrail_nearest(rr->pts, rr->np, rr->cl, c);
    a->prev = a->cur; a->cur = c;
    if (vlen(vsub(nrail_at(rr->pts, rr->np, rr->cl, coord, &dd), c)) > 10000.0f) return 0; /* sRaceJudgeLength */
    if (fabsf(a->coord - coord) < 10000.0f) a->coord = coord;
    int a1 = rr_point_arg(rr, rr_point_no(rr), 0);
    rr->coord = a->coord; /* setRailCoord */
    int a2 = rr_point_arg(rr, rr_point_no(rr), 0);
    if (a1 != -1 && a1 == a2) a->coord = coord;
    if (fabsf(a->coord - rr->len) < 100.0f && rm_is_goal(a)) { pad_rumble(RUMBLE_VERY_STRONG); return 1; }
    return 0;
}
static void rm_init(void)
{
    memset(&rm, 0, sizeof rm);
    rm.scene = sc; rm.row = -1; rm.req = -1; rm.next = -1;
    nrn = 0;
    int tr = scene_type(sc, "RaceRail"), tt = scene_type(sc, "TeresaRacer");
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type == tr && tr >= 0 && rm.row < 0) { /* RaceRail::init: entryRacerPlayer */
            if (!rr_init(&rm.rail, r)) { printf("race: RaceRail row %u, no rail" "\n", i); continue; }
            rm.row = (int)i;
            rm.race = rr_parg0(&rm.rail);
            if (rm.race < 0 || rm.race > 4) rm.race = 0; /* (getRailArg0WithInit -1: sRaceStruct[-1]) */
            char hi[48], lo[48];
            snprintf(hi, sizeof hi, "%s/hi", rm_struct[rm.race].name); snprintf(lo, sizeof lo, "%s/lo", rm_struct[rm.race].name);
            rm.best = (u32)gd_value(hi) << 16 | (u32)gd_value(lo); /* getRaceBestTime */
            V3 d, e = nrail_at(rm.rail.pts, rm.rail.np, rm.rail.cl, rm.rail.len, &d); /* calcRailEndPointPosAndDirection */
            rm.end_p = e; rm.end_z = vnorm(d); /* makeMtxFrontUp(endDir, (0, 1, 0)), its inverse */
            rm.r[rm.n].npc = -1; rm.ord[rm.n] = rm.n; rm.n++;
            printf("race: RaceRail row %u, race %d %s, rail %d points len %d, best %u" "\n", i, rm.race, rm_struct[rm.race].name,
                   rr_npoints(&rm.rail), (int)rm.rail.len, rm.best);
        } else if ((int)r->type == tt && tt >= 0 && nrn < RN_MAX && rm.n < RM_MAX) { /* TeresaRacer::init */
            RNpc *t = &rn[nrn];
            memset(t, 0, sizeof *t);
            t->row = (int)i; t->kind = 1; t->id = r->arg[0]; t->k = -1; t->pushed = -1; t->next = -1;
            t->dead = (t->id == 0 && gd_player_luigi()) || (t->id == 1 && !gd_player_luigi());
            rr_init(&t->rail, r);
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, (float[3]){1, 1, 1});
            t->pose.m[12] = r->pos[0]; t->pose.m[13] = r->pos[1]; t->pose.m[14] = r->pos[2];
            t->pose.nu = t->pose.nu0 = vnorm(v3(m[4], m[5], m[6])); t->pose.nf = t->pose.nf0 = vnorm(v3(m[8], m[9], m[10]));
            t->pos0 = row_pos(r);
            t->mv[0] = t->mv[1] = "waitopen"; t->tk[0] = t->tk[1] = "waitopentalk"; t->act = "chase";
            t->p0 = t->p1 = 1; t->p4 = 3000.0f; t->p8 = 1.5f;
            t->speed = 20.0f; t->accel = 0.1f; t->blend = 0.05f; /* sSpeed, sAccele, _114 */
            if (!t->dead) { t->ar = rm.n; rm.r[rm.n].npc = nrn; rm.ord[rm.n] = rm.n; rm.n++; } /* entryRacerOthers */
            printf("race: TeresaRacer row %u id %d%s, rail %d points" "\n", i, t->id, t->dead ? " (dead)" : "", rr_npoints(&t->rail));
            nrn++;
        }
    }
    rm.on = rm.row >= 0;
}
static void rm_start_with_wipe(void) /* startWithWipe: requestStartDemoMarioPuppetableWithoutCinemaFrame(レース, PrepWipe) */
{
    rm.rank = 0;
    menu_race_hide_all();
    rm.req = RM_PREPWIPE;
}
static void rm_prep_race(void) /* prepRace: the player's first */
{
    pr_prep();
    for (int i = 0; i < rm.n; i++) if (rm.r[rm.ord[i]].npc >= 0) rn_prep_racer(&rn[rm.r[rm.ord[i]].npc]);
}
static void rm_start_race(void) /* startRace */
{
    for (int i = 0; i < rm.n; i++) {
        RmRacer *a = &rm.r[rm.ord[i]];
        if (a->npc >= 0) rn_start_racer(&rn[a->npc]);
        else if (rm.cam) { camera_event_stop(); rm.cam = 0; } /* PlayerRacer::startRacer: endActorCamera */
    }
    pd_end("レース");
    rm.bck = NULL; /* onPlayerControl */
    printf("race: start" "\n");
}
static void rm_goal_race(void) /* goalRace: loseRacer for those not in */
{
    for (int i = rm.rank; i < rm.n; i++) if (rm.r[rm.ord[i]].npc >= 0) rn_lose_racer(&rn[rm.r[rm.ord[i]].npc]);
}
static void rm_reset_race(void) /* resetRace: the player's first; the layout killed */
{
    pr_reset();
    for (int i = 0; i < rm.n; i++) if (rm.r[rm.ord[i]].npc >= 0) rn_reset_racer(&rn[rm.r[rm.ord[i]].npc]);
    menu_race_kill();
}
static void rm_exe(void)
{
    int st = rm.step;
    switch (rm.nrv) {
    case RM_WAIT: break;
    case RM_PREPWIPE: case RM_PREPIMME: /* exePrep: initRacer */
        for (int i = 0; i < rm.n; i++) { RmRacer *a = &rm.r[i]; a->coord = 0; a->cur = a->prev = v3(0, 0, 0); }
        rm_player_bck("watch"); /* PlayerRacer::initRacer */
        rm_set(rm.nrv == RM_PREPWIPE ? RM_PREWIPEOUT : RM_INTRO);
        break;
    case RM_PREWIPEOUT: case RM_PSTWIPEOUT: /* exeWipeOut */
        if (st == 0) {
            if (rm.nrv == RM_PREWIPEOUT) bgm_stop(90);
            menu_wipe(1, -1, 0); /* closeWipeFade */
        }
        if (menu_wipe_active()) break;
        if (rm.nrv == RM_PREWIPEOUT) { rm_prep_race(); rm_set(RM_PREWIPEWAIT); }
        else { rm_reset_race(); rm_set(RM_PSTWIPEWAIT); }
        break;
    case RM_PREWIPEWAIT: case RM_PSTWIPEWAIT: /* exeWipeWait */
        if (st == 30) rm_set(rm.nrv == RM_PREWIPEWAIT ? RM_PREWIPEIN : RM_PSTWIPEIN);
        break;
    case RM_PREWIPEIN: case RM_PSTWIPEIN: /* exeWipeIn */
        if (st == 0) {
            if (rm.nrv == RM_PREWIPEIN) bgm_sub("BGM_MINIGAME_START");
            else if (!miss_running()) game_bgm_stage(); /* startCurrentStageBGM */
            menu_wipe(0, -1, 0); /* openWipeFade */
        }
        if (menu_wipe_active()) break;
        if (rm.nrv == RM_PREWIPEIN) rm_set(RM_INTRO);
        else {
            rm.ctrl_off = 0; /* onPlayerControl */
            pd_end("レース"); rm.bck = NULL;
            for (int i = 0; i < rm.n; i++) if (rm.r[rm.ord[i]].npc >= 0) rn_exit_racer(&rn[rm.r[rm.ord[i]].npc]);
            rm_set(RM_WAIT);
            printf("race: over" "\n");
        }
        break;
    case RM_INTRO: /* exeIntro (ponytail: the star pointer demo mode) */
        if (st == 110) rm_set(RM_COUNT);
        break;
    case RM_COUNT: /* exeCount */
        if (st == 0) {
            menu_race_set_time(0);
            menu_race_set_best_time(rm.best);
            menu_race_appear();
            menu_race_count_and_go();
            menu_race_show("Record");
        }
        if (st % 60 == 0) se_start(SE_SY_RACE_COUNT_DOWN, NULL);
        if (st == 166) bgm_start(rm_struct[rm.race].bgm); /* sBgmStartStep: startEventBGM */
        if (menu_race_counting()) break;
        menu_race_go();
        rm_start_race();
        rm_set(RM_RACE);
        break;
    case RM_RACE: { /* exeRace */
        if (st == 0) se_start(SE_SY_RACE_START, NULL);
        int maxt = rm_struct[rm.race].max;
        menu_race_set_time((u32)st);
        if (maxt > 0 && st >= maxt * 60) { rm.rank = 0; rm_renew_time(); rm_set(RM_DEMO); break; }
        if (rm.n > 1) menu_race_rank(rm_rank_of(0)); /* getRank(mPlayerRacer): r[0] the player's */
        for (int i = rm.rank; i < rm.n; i++) {
            int ar = rm.ord[i];
            RmRacer *a = &rm.r[ar];
            int in = a->npc < 0 ? pr_update(a) : rn_update_racer(&rn[a->npc], a);
            if (in) {
                int sw = rm.ord[i]; rm.ord[i] = rm.ord[rm.rank]; rm.ord[rm.rank++] = sw;
                printf("race: racer %d (%s) in at f %d, place %d" "\n", ar, a->npc < 0 ? "Mario" : "npc", st, rm.rank);
                if (a->npc < 0 || rn_goal_racer(&rn[a->npc])) { rm_renew_time(); rm_set(RM_DEMO); break; } /* goalRacer */
            }
        }
        for (int i = rm.rank + 1; i < rm.n; i++) /* std::sort by compRacer (coord, highest first) */
            for (int j = i; j > rm.rank && rm.r[rm.ord[j]].coord > rm.r[rm.ord[j - 1]].coord; j--) {
                int sw = rm.ord[j]; rm.ord[j] = rm.ord[j - 1]; rm.ord[j - 1] = sw;
            }
        break;
    }
    case RM_DEMO: /* exeDemo */
        if (!rm_can_demo()) break;
        rm_goal_race();
        rm.ctrl_off = 1; /* offPlayerControl */
        rm_set(RM_GOAL);
        break;
    case RM_GOAL: /* exeGoal */
        if (st == 0) {
            bgm_stop(30);
            if (rm.rank == 0) { bgm_sub("BGM_RACE_LOSE"); menu_race_time_up(); }
            else if (rm.rank == 1) { bgm_sub("BGM_RACE_WIN"); menu_race_goal(); }
            else { bgm_sub("BGM_RACE_LOSE"); menu_race_lose(); }
        }
        if (st > 0 && menu_race_all_stopped()) rm_set(RM_RANK);
        break;
    case RM_RANK: /* exeRank */
        if (st < 60 || !rm_can_demo()) break;
        pd_try_start("レース", rm.row); /* tryStartDemoWithoutCinemaFrame */
        rm_set(RM_PSTWIPEOUT);
        break;
    }
}
static void rm_update(void)
{
    if (!rm.on) return;
    if (rm.req >= 0 && pd_try_start("レース", rm.row)) { rm.next = rm.req; rm.req = -1; } /* the requested demo starts: its nerve */
    if (rm.next >= 0) { rm.nrv = rm.next; rm.step = 0; rm.next = -1; }
    rm_exe();
    rm.step++;
    if (rm.next >= 0) { rm.nrv = rm.next; rm.step = 0; rm.next = -1; printf("race: nerve %d" "\n", rm.nrv); }
}

/* --- TeresaRacer (and the NPC racers' shared parts) */
static void rn_set(RNpc *t, int n) { t->next = n; }
static void rn_push(RNpc *t, int n) { t->pushed = t->nrv; t->nrv = n; t->step = 0; t->next = -1; } /* pushNerve */
static void rn_pop(RNpc *t) { if (t->pushed >= 0) { t->next = t->pushed; t->pushed = -1; } } /* popNerve */
static Inst *rn_inst(const RNpc *t) { return row_inst(t->row); }
static int rn_talking(const RNpc *t) /* isTalkTalking */
{
    return t->k >= 0 && td.st == t->k && (td.nrv == TD_TALK || td.nrv == TD_SLCT || td.nrv == TD_NEXT);
}
static int rn_action(RNpc *t, const char *act, int loop) /* tryStartAction (no-op while it plays) */
{
    Inst *in = rn_inst(t);
    if (!in || !in->anim || !act) return 0;
    if (in->anim->clip_id >= 0 && !strcmp(sc->clips[in->anim->clip_id].name + strlen(sc->types + 32 * sc->objs[t->row].type) + 1, act)) return 0;
    int mode = 0;
    char n[48];
    snprintf(n, sizeof n, "%s/%s", sc->types + 32 * sc->objs[t->row].type, act);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (loop < 0) { anim_clip_info(n, &mode); loop = mode == 2 || mode == 4; }
    return obj_play(in, act, loop, 0);
}
static int rn_action_done(const RNpc *t) /* isBckOneTimeAndStopped */
{
    const Inst *in = rn_inst(t);
    return !in || !in->anim || (!in->anim->loop && in->anim->done);
}
static int rn_action_continuous(const RNpc *t) /* isActionContinuous: a one-time action still playing */
{
    const Inst *in = rn_inst(t);
    return in && in->anim && !in->anim->loop && !in->anim->done;
}
static NpcTurn rn_turn(const RNpc *t, float speed) { NpcTurn nt = {"TeresaRacer", t->p0, t->p1, t->p4, speed, 0.0f, 0.0f, {NULL}}; return nt; }
static V3 rn_pos(const RNpc *t) { return v3(t->pose.m[12], t->pose.m[13], t->pose.m[14]); }
static int rn_turn_to_player(RNpc *t, float speed) /* turnToPlayer(speed, 0, 0) */
{
    NpcTurn nt = rn_turn(t, speed);
    V3 to = vsub(mario->pos, rn_pos(t));
    float d = vlen(to);
    return npc_turn_to_player(&t->pose, &nt, d > 1e-6f ? vmul(to, 1.0f / d) : t->pose.nf);
}
static int rn_try_turn_action(RNpc *t) /* tryStartTurnAction */
{
    const char *a;
    NpcTurn nt = rn_turn(t, t->p8);
    if (vlen(vsub(mario->pos, rn_pos(t))) < t->p4) a = t->p0 ? (rn_turn_to_player(t, t->p8) ? t->mv[0] : t->mv[1]) : t->mv[0];
    else a = (t->p0 || t->p1) ? (npc_turn_default(&t->pose, &nt) ? t->mv[0] : t->mv[1]) : t->mv[0];
    return rn_action(t, a, -1);
}
static int rn_try_talk_action(RNpc *t) /* tryStartTalkAction */
{
    if (!rn_talking(t)) return rn_try_turn_action(t);
    return rn_action(t, t->p1 && !rn_turn_to_player(t, t->p8) ? t->tk[1] : t->tk[0], -1);
}
static void rn_follow_rail_pose(RNpc *t, float fb, float pb) /* followRailPose(fb, pb): its front along the rail, at its place */
{
    npc_pose(&t->pose, t->pose.nu, rr_dir(&t->rail), rr_pos(&t->rail), fb, pb);
}
static void rn_move_action(RNpc *t) /* startMoveAction */
{
    rr_adjust_speed(&t->rail, t->speed, t->accel);
    rr_move(&t->rail);
    rn_follow_rail_pose(t, t->blend, t->blend);
    if (rr_reached_goal(&t->rail)) t->rail.dir = -t->rail.dir; /* reverseRailDirection */
}
static int rn_try_move_turn_action(RNpc *t) /* tryStartMoveTurnAction */
{
    if (!t->rail.pts) return rn_try_turn_action(t);
    rn_move_action(t);
    return rn_action(t, t->act, -1);
}
static void rn_cam(RNpc *t, int on) /* startMultiActorCameraTargetSelf / endMultiActorCamera 会話 */
{
    if (row_camset[t->row] == -1) return;
    if (on) t->cam = camera_actor_start("TeresaRacer", "会話", (unsigned short)row_camset[t->row], t->pose.nu, t->pose.nf, rn_pos(t));
    else if (t->cam) { camera_event_stop(); t->cam = 0; }
}
static RNpc *rn_of_talker(int k) { for (int i = 0; i < nrn; i++) if (rn[i].k == k) return &rn[i]; return NULL; }
static int tr_branch(int k, u32 v) { (void)k; (void)v; return rm.rank == 1; } /* branchFunc: getRaceRank() == 1 */
static int tr_anime(int k, u32 v) /* animeFunc */
{
    RNpc *t = rn_of_talker(k);
    if (!t) return 1;
    if (t->nrv != TRC_READY) rn_push(t, TRC_READY);
    if (rn_action_done(t)) { rn_pop(t); return 1; } /* isBckOneTimeAndStopped */
    switch (v & 0xFFFF) {
    case 0: rn_action(t, "close", 0); t->tk[0] = t->tk[1] = "closetalk"; t->mv[0] = t->mv[1] = "closetalk"; break;
    case 1: t->mv[0] = t->mv[1] = "defeat"; t->tk[0] = t->tk[1] = "defeat"; rn_pop(t); return 1;
    case 2: case 3: rn_action(t, "open", 0); t->tk[0] = t->tk[1] = "waitopentalk"; t->mv[0] = t->mv[1] = "waitopen"; break;
    }
    return 0;
}
static int tr_event(int k, u32 v) /* eventFunc: mTakeOutStar->takeOut() */
{
    (void)v;
    RNpc *t = rn_of_talker(k);
    if (!t || t->tos == 2) return 1;
    if (t->tos == 0) { t->tos = 1; t->tos_step = 0; rn_push(t, TRC_TAKEOUT); rn_action(t, "takeoutstarteresaracer", 0); printf("teresa racer row %d: TakeOutStar" "\n", t->row); }
    return 0;
}
static void rn_takeout(RNpc *t) /* TakeOutStar::exeAnim (sStarAppearSeStepTeresaRacer 89), exeDemo: the star out */
{
    if (t->tos != 1) return;
    Inst *in = rn_inst(t);
    V3 c = vadd(rn_pos(t), vmul(t->pose.nu, 200.0f));
    if (t->tos_step++ > 89) t->lv2 = se_level(in_water(c, t->pose.nu, NULL) ? SE_OJ_LV_POW_STAR_EXIST_W : SE_OJ_LV_POW_STAR_EXIST, &c.x, t->lv2);
    if (t->tos_step > 1 && (!in || rn_action_done(t))) {
        t->took = 1; t->tos = 2; sw_changed = 1; rn_pop(t);
        printf("teresa racer row %d: its star out" "\n", t->row);
    }
}
static void rn_talker(RNpc *t) /* its mMsgCtrl at its base matrix */
{
    if (t->k < 0) {
        char lab[32];
        if (!talk_row_label(t->row, lab)) return;
        t->k = talk_new(lab, t->row, t->row, 150.0f, 500.0f); /* caps.mMessageOffset (0, 150, 0), setDistanceToTalk 500 */
        if (t->k < 0) return;
        Talker *tk = &talkers[t->k];
        tk->root_auto = 1; tk->branch = tr_branch; tk->anime = tr_anime; tk->event = tr_event;
        printf("teresa racer row %d: talk %s" "\n", t->row, lab);
    }
    float m[16];
    basis_matrix(m, rn_pos(t), t->pose.nu, t->pose.nf, 0);
    talk_place(t->k, m);
}
static void tr_bgm(RNpc *t) /* changeBgmState */
{
    if (t->step < 480 || t->goal) return; /* sBgmChangeStep */
    if (vlen(vsub(mario->pos, rn_pos(t))) < 3000.0f) { if (t->bgm != 2) bgm_state(2, 90); t->bgm = 2; }
    else { if (t->bgm != 1) bgm_state(1, 150); t->bgm = 1; }
}
static void tr_raster(RNpc *t) /* setRasterScroll (ponytail), its level SE */
{
    if (t->id != -1 && vlen(vsub(mario->pos, rn_pos(t))) < 2500.0f) t->lv = se_level(SE_AT_LV_DEATH_PROM_RASTER, NULL, t->lv);
}
static void tr_exe(RNpc *t)
{
    int st = t->step;
    switch (t->nrv) {
    case TRC_WAIT: /* exeWait */
        rn_try_talk_action(t);
        if (talk_near(t->k)) { if (t->id == -1) rn_cam(t, 1); rn_set(t, TRC_PRE); }
        break;
    case TRC_PRE: /* exePre (resetGCapture) */
        if (rn_action_continuous(t)) break;
        rn_try_talk_action(t);
        if (!talk_near_at_end(t->k)) break;
        if (tk_yesno == 1) { rm_start_with_wipe(); rn_set(t, TRC_READY); printf("teresa racer row %d: the race (Yes)" "\n", t->row); }
        else { if (t->id == -1) rn_cam(t, 0); rn_set(t, TRC_WAIT); }
        break;
    case TRC_READY: case TRC_TAKEOUT: break;
    case TRC_POST: /* exePost */
        if (rn_action_continuous(t)) break;
        rn_try_talk_action(t);
        if (!talk_force_at_end(t->k, 1)) break;
        rn_cam(t, 0);
        if (rm.rank == 1) rn_set(t, TRC_TALK);
        else { miss_race = 1; miss(); rn_set(t, TRC_READY); printf("teresa racer row %d: Mario lost: forceKillPlayerByGroundRace" "\n", t->row); }
        break;
    case TRC_MOVE: /* exeMove */
        if (rr_near_goal(&t->rail, 500.0f) || t->rail.dir < 0) rn_set(t, TRC_GOAL); /* sNearGoalDist */
        else {
            int a = rr_point_arg(&t->rail, rr_point_no(&t->rail), 0);
            if (a != -1) t->speed = (float)a; /* getCurrentRailPointArg0NoInit */
            V3 up = t->pose.nu;
            rr_adjust_speed(&t->rail, t->speed, t->accel);
            rr_move(&t->rail);
            V3 f = rr_dir(&t->rail);
            if (t->id != -1) { /* its front turned to the camera: the rail's + -camera z x sCameraDirScale 0.4 */
                V3 ce, cw, cu; float fy;
                camera_pose(&ce, &cw, &cu, &fy);
                f = vadd(f, vmul(vnorm(vsub(cw, ce)), -0.4f));
                if (vlen(f) < 1e-6f) break;
                f = vnorm(f);
            }
            npc_pose(&t->pose, up, f, rr_pos(&t->rail), 0.05f, 0.1f); /* decidePose(up, front, railPos, 1, 0.05, 0.1) */
        }
        tr_raster(t);
        tr_bgm(t);
        break;
    case TRC_GOAL: /* exeGoal */
        if (st == 0) { t->p0 = 1; t->speed = 0.0f; t->p1 = 1; }
        if (t->rail.speed < 1.0f) rn_try_move_turn_action(t);
        else rn_try_turn_action(t);
        tr_raster(t);
        tr_bgm(t);
        break;
    case TRC_TALK: /* exeTalk */
        rn_try_talk_action(t);
        talk_near(t->k);
        break;
    }
}
static void rn_prep_racer(RNpc *t) /* TeresaRacer::prepRacer */
{
    t->rail.coord = 0.0f; t->rail.dir = 1; t->rail.speed = 0.0f; /* moveCoordToStartPos, setRailCoordSpeed 0 */
    rn_follow_rail_pose(t, 1.0f, 1.0f);
    rn_action(t, "chase", 1);
    t->p0 = t->p1 = 0;
}
static void rn_start_racer(RNpc *t) { t->goal = 0; t->bgm = 0; rn_set(t, TRC_MOVE); } /* startRacer */
static int rn_update_racer(RNpc *t, RmRacer *a) /* updateRacer: on the PlayerRacer's rail */
{
    V3 p = rn_pos(t);
    float coord = nrail_nearest(rm.rail.pts, rm.rail.np, rm.rail.cl, p);
    a->prev = a->cur; a->cur = p;
    if (fabsf(a->coord - coord) < 2000.0f) a->coord = coord;
    if (fabsf(a->coord - rm.rail.len) < 100.0f) { t->goal = rm_is_goal(a); return t->goal; }
    return 0;
}
static int rn_goal_racer(RNpc *t) { (void)t; return 0; } /* TeresaRacer::goalRacer */
static void rn_lose_racer(RNpc *t) { (void)t; }
static void rn_reset_racer(RNpc *t) /* TeresaRacer::resetRacer */
{
    if (rm.race == RACE_TERESA_PHANTOM) { /* setDefaultPose: _A0 = _B0, at _C0 */
        t->pose.nf = t->pose.nf0; t->pose.nu = t->pose.nu0;
        t->pose.m[12] = t->pos0.x; t->pose.m[13] = t->pos0.y; t->pose.m[14] = t->pos0.z;
    } else {
        const float *g = scene_gpos("レース終了後位置テレサ"); /* setNPCActorPos, setInitPose */
        if (g) {
            float m[16], one[3] = {1, 1, 1}, rot[3] = {g[3], g[4], g[5]};
            mat_translate_rot_scale(m, g, rot, one);
            t->pose.m[12] = g[0]; t->pose.m[13] = g[1]; t->pose.m[14] = g[2];
            t->pose.nu = t->pose.nu0 = vnorm(v3(m[4], m[5], m[6])); t->pose.nf = t->pose.nf0 = vnorm(v3(m[8], m[9], m[10]));
            t->pos0 = v3(g[0], g[1], g[2]);
        }
    }
    rn_turn_to_player(t, 180.0f);
    rn_set(t, TRC_POST);
    rn_cam(t, 1);
    t->p0 = t->p1 = 1;
}
static void rn_exit_racer(RNpc *t) { (void)t; } /* validateClipping */
static void rn_place(RNpc *t) /* its Inst at its pose (kept across rebinds) */
{
    Inst *in = rn_inst(t);
    if (!in) return;
    const Row *r = &sc->objs[t->row];
    basis_matrix(in->m, rn_pos(t), t->pose.nu, t->pose.nf, 0);
    for (int j = 0; j < 12; j++) in->m[j] *= r->scale[j / 4];
    in->nu = t->pose.nu; in->nf = t->pose.nf;
}
#ifdef RACE_TEST
/* XF=-DRACE_TEST=1 (PhantomGalaxy/2): once Mario is free, put 300 before the first racer NPC; A for its talk, Yes;
 * in the race Mario carried along the judge rail ahead of it (=2: left at the start: it wins), A through the talks */
static void game_test_race(void)
{
    static int free_at = -1, slct, t;
    pad_force = 0;
    if (free_at < 0) { if (!game_test_free() || !nrn) return; free_at = t; printf("race test: free" "\n"); }
    t++;
    RNpc *n = &rn[0];
    V3 p = rn_pos(n);
    if (t - free_at < 120 && rm.nrv == RM_WAIT && n->nrv == TRC_WAIT) {
        mario->pos = vadd(vadd(p, vmul(n->pose.nf, 300.0f)), vmul(n->pose.nu, 30.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(n->pose.nf, -1.0f);
    }
    if (td.nrv != TD_SLCT) slct = 0;
    if (td.nrv == TD_SLCT) {
        if (++slct == 60) printf("race test: pointer on BoxRight %d" "\n", menu_test_point("BoxRight"));
        pad_force = slct == 80 ? PAD_CROSS : 0;
    } else if (t - free_at > 150 && (t % 45) < 5 && (rm.nrv != RM_RACE)) pad_force = PAD_CROSS;
#if RACE_TEST == 1
    if (rm.nrv == RM_RACE) { /* along the judge rail, 400 ahead of the racer's coord, at 24 a frame at most */
        static float c;
        if (rm.step == 1) c = 0;
        RmRacer *a = &rm.r[n->ar];
        c = fminf(c + 24.0f, a->coord + 1200.0f);
        V3 d, q = nrail_at(rm.rail.pts, rm.rail.np, rm.rail.cl, fminf(c, rm.rail.len + 200.0f), &d);
        if (c > rm.rail.len) q = vadd(q, vmul(d, c - rm.rail.len));
        mario->pos = q; mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->face = d;
    }
#endif
    if (n->nrv == TRC_TALK && n->took && !pd.name && td.nrv != TD_TALK) /* the star out, its demo over: Mario on it */
        for (int i = 0; i < ninsts; i++)
            if (insts[i].item == ITEM_STAR && row_shown(insts[i].row) && !row_dead[insts[i].row]) {
                mario->pos = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]); mario->hvel = v3(0, 0, 0); mario->vup = 0;
                break;
            }
    if (t % 60 == 0) {
        printf("race test t %d: rm nerve %d step %d rank %d | npc nerve %d coord %d speed %.1f pos %d %d %d clip %s | Mario %d %d %d talk %d" "\n",
               t, rm.nrv, rm.step, rm.rank, n->nrv, (int)n->rail.coord, n->rail.speed, (int)p.x, (int)p.y, (int)p.z,
               rn_inst(n) && rn_inst(n)->anim && rn_inst(n)->anim->clip_id >= 0 ? sc->clips[rn_inst(n)->anim->clip_id].name : "-",
               (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z, td.nrv);
        for (int i = 0; i < rm.n; i++) printf("race test: racer %d coord %d" "\n", rm.ord[i], (int)rm.r[rm.ord[i]].coord);
    }
}
#endif
static void update_race(void)
{
    if (rm.scene != sc) rm_init();
    if (!rm.on && !nrn) return;
    menu_race_update();
#ifdef RACE_TEST
    game_test_race();
#endif
    rm_update();
    for (int i = 0; i < nrn; i++) {
        RNpc *t = &rn[i];
        if (t->dead) { Inst *in = rn_inst(t); if (in) in->invisible = 1; continue; } /* makeActorDead */
        rn_talker(t);
        if (t->next >= 0) { t->nrv = t->next; t->step = 0; t->next = -1; }
        rn_takeout(t);
        tr_exe(t);
        t->step++;
        if (t->next >= 0) { t->nrv = t->next; t->step = 0; t->next = -1; printf("teresa racer row %d: nerve %d" "\n", t->row, t->nrv); }
        rn_place(t);
    }
}
