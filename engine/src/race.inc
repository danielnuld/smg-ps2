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
 * layout's RaceStart / RaceGoal effects, the raster
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
    RmRacer r[RM_MAX]; int ord[RM_MAX], n, rank, par; /* mRacer (indices into r), mRacerNum, mRank */
    u32 best, time;
    V3 end_p, end_x, end_y, end_z; /* _FC (inverted: a point's local z is its offset along end_z) */
    RmRail rail;   /* the PlayerRacer's */
    int ctrl_off, cam; const char *bck, *bck_demo; /* bck: startBckPlayer's clip while the demo bck_demo holds Mario */
    int test_f;
} rm;
enum { TRC_WAIT, TRC_PRE, TRC_READY, TRC_MOVE, TRC_GOAL, TRC_POST, TRC_TALK, TRC_TAKEOUT };
enum { SK_REACTION = 20, SK_TALK, SK_WAIT, SK_DEMO, SK_AWAY, SK_TURN, SK_PROVOKE, SK_CAUGHT, SK_FADEOUT, SK_FADEIN, SK_GIVEUP,
       SK_SITDOWN, SK_SWITCH };
enum { PG_REACTION = 40, PG_WAIT, PG_PRE, PG_READY, PG_POST, PG_TALK, PG_TAKEOUT, PG_RACEWAIT, PG_WALK, PG_SWIM, PG_STOW, PG_WTOS,
       PG_FALL, PG_GOAL, PG_TURN };
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
    RmRail sr[8]; int nsr, cur, swr, ri, cross; /* PenguinSkater: its group's rails (sr[0] its own), mCurrentRail, mSwitchRail, mRail, mCrossPoint */
    NpcReact re; int react;      /* NPCActor's reaction flags; the Reaction nerve's start flags */
    V3 vel; int ground, pno; float boff, mspeed, pspeed; /* PenguinRacer: mVelocity, binded ground, mRailPointNo, its binder's offset, mMoveSpeed, mSpeed */
} RNpc;
static RNpc rn[RN_MAX];
static int nrn;
static int rm_star_appeared; /* StageStateKeeper::mIsPowerStarAppeared */
static void rm_reset(void) { rm.scene = NULL; rm.on = 0; rm.ctrl_off = 0; rm.bck = NULL; nrn = 0; rm_star_appeared = 0; }
static void rm_note_star(void) { rm_star_appeared = 1; }
static int star_appeared(void) { return rm_star_appeared; } /* MR::isStageStatePowerStarAppeared */
static int rm_ctrl_off(void) { return rm.scene == sc && rm.ctrl_off; }
static const char *rm_bck(void) { return rm.scene == sc && pd.name && rm.bck_demo && !strcmp(pd.name, rm.bck_demo) ? rm.bck : NULL; }
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
        char hi[64], lo[64]; /* (ペンギンレース[オーシャンリング]/hi is 50 bytes: 48 cut it, its best time read 0 and never kept) */
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
static void rm_player_bck(const char *b) { rm.bck = b; rm.bck_demo = "レース"; } /* startBckPlayer (played while the レース demo holds him) */
static V3 rm_player_center(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); } /* getPlayerCenterPos */
static void rn_reset_racer(RNpc *t);
static void sk_init(RNpc *t, const Row *r);
static void pg_init(RNpc *t, const Row *r, int leader), pg_prep_racer(RNpc *t), pg_reset_racer(RNpc *t);
static int pg_move_nerve(const RNpc *t), pgl_branch(int k, u32 v), pgl_event(int k, u32 v);
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
    if (rm.race == RACE_PENGUIN) { /* the water surface on the line from 500 over it, 1000 down
                                    * (getFirstPolyOnLineToWaterSurface), 15 up, SwimDrift (ponytail: no WaterSurface.kcl,
                                    * collision category 2: the water's own surface, OceanRing's or an area's) */
        float depth;
        V3 lo = vsub(p, vmul(up, 500.0f));
        if (in_water(p, up, &depth) && depth <= 500.0f) p = vadd(p, vmul(up, depth));
        else if (in_water(lo, up, &depth) && depth <= 500.0f) p = vadd(lo, vmul(up, depth));
        p = vadd(p, vmul(up, 15.0f));
        rm_player_bck("swimdrift");
    } else if (rm.race == RACE_TERESA_PHANTOM) { /* convertPosOnGround(grav x 1000), BattleWait */
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
    int tr = scene_type(sc, "RaceRail"), tt = scene_type(sc, "TeresaRacer"), tsk = scene_type(sc, "PenguinSkater"),
        tpr = scene_type(sc, "PenguinRacer"), tpl = scene_type(sc, "PenguinRacerLeader");
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type == tr && tr >= 0 && rm.row < 0) { /* RaceRail::init: entryRacerPlayer */
            if (!rr_init(&rm.rail, r)) { printf("race: RaceRail row %u, no rail" "\n", i); continue; }
            rm.row = (int)i;
            rm.race = rr_parg0(&rm.rail);
            if (rm.race < 0 || rm.race > 4) rm.race = 0; /* (getRailArg0WithInit -1: sRaceStruct[-1]) */
            char hi[64], lo[64]; /* (renewTime's) */
            snprintf(hi, sizeof hi, "%s/hi", rm_struct[rm.race].name); snprintf(lo, sizeof lo, "%s/lo", rm_struct[rm.race].name);
            rm.best = (u32)gd_value(hi) << 16 | (u32)gd_value(lo); /* getRaceBestTime */
            V3 d, e = nrail_at(rm.rail.pts, rm.rail.np, rm.rail.cl, rm.rail.len, &d); /* calcRailEndPointPosAndDirection */
            rm.end_p = e; rm.end_z = vnorm(d); /* makeMtxFrontUp(endDir, (0, 1, 0)), its inverse */
            rm.r[rm.n].npc = -1; rm.ord[rm.n] = rm.n; rm.par = rm.n; rm.n++;
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
            t->re.c158 = 1024; /* NPCActor _158 */
            if (!t->dead) { t->ar = rm.n; rm.r[rm.n].npc = nrn; rm.ord[rm.n] = rm.n; rm.n++; } /* entryRacerOthers */
            printf("race: TeresaRacer row %u id %d%s, rail %d points" "\n", i, t->id, t->dead ? " (dead)" : "", rr_npoints(&t->rail));
            nrn++;
        } else if ((int)r->type == tsk && tsk >= 0 && nrn < RN_MAX) { /* PenguinSkater::init */
            RNpc *t = &rn[nrn];
            memset(t, 0, sizeof *t);
            t->row = (int)i; t->pushed = -1; t->next = -1;
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, (float[3]){1, 1, 1});
            t->pose.m[12] = r->pos[0]; t->pose.m[13] = r->pos[1]; t->pose.m[14] = r->pos[2];
            t->pose.nu = t->pose.nu0 = vnorm(v3(m[4], m[5], m[6])); t->pose.nf = t->pose.nf0 = vnorm(v3(m[8], m[9], m[10]));
            t->pos0 = row_pos(r);
            sk_init(t, r);
            nrn++;
        } else if (((int)r->type == tpr && tpr >= 0) || ((int)r->type == tpl && tpl >= 0)) { /* PenguinRacer(Leader)::init */
            if (nrn >= RN_MAX || rm.n >= RM_MAX) continue;
            RNpc *t = &rn[nrn];
            memset(t, 0, sizeof *t);
            t->row = (int)i; t->pushed = -1; t->next = -1;
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, (float[3]){1, 1, 1});
            t->pose.m[12] = r->pos[0]; t->pose.m[13] = r->pos[1]; t->pose.m[14] = r->pos[2];
            t->pose.nu = t->pose.nu0 = vnorm(v3(m[4], m[5], m[6])); t->pose.nf = t->pose.nf0 = vnorm(v3(m[8], m[9], m[10]));
            t->pos0 = row_pos(r);
            pg_init(t, r, (int)r->type == tpl);
            if (t->id == 0 && t->rail.pts) { t->ar = rm.n; rm.r[rm.n].npc = nrn; rm.ord[rm.n] = rm.n; rm.n++; } /* entryRacerOthers */
            printf("race: %s row %u, race %s, rail %d points" "\n", sc->types + 32 * r->type, i, t->id == 0 ? "on" : "off", rr_npoints(&t->rail));
            nrn++;
        }
    }
    rm.on = rm.row >= 0;
}
static void rm_start_with_wipe(void) /* startWithWipe: requestStartDemoMarioPuppetableWithoutCinemaFrame(レース, PrepWipe) */
{
    if (!rm.on || rm.nrv != RM_WAIT || rm.req >= 0 || rm.next >= 0) return; /* (eventFunc 0 and exePre both ask) */
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
        if (rm.n > 1) menu_race_rank(rm_rank_of(rm.par)); /* getRank(mPlayerRacer) */
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
    if (on) t->cam = camera_actor_start(sc->types + 32 * sc->objs[t->row].type, "会話", (unsigned short)row_camset[t->row], t->pose.nu, t->pose.nf, rn_pos(t));
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
        t->k = talk_new(lab, t->row, t->row, t->kind >= 3 ? 130.0f : 150.0f, t->kind == 1 ? 500.0f : 240.0f); /* caps.mMessageOffset (0, 150, 0), PenguinRacer's
                                                                                                         * createTalkCtrl (0, 130, 0); TeresaRacer: setDistanceToTalk 500 */
        if (t->k < 0) return;
        Talker *tk = &talkers[t->k];
        if (t->kind == 1) { tk->root_auto = 1; tk->branch = tr_branch; tk->anime = tr_anime; tk->event = tr_event; } /* (PenguinSkater: offRootNodeAutomatic) */
        if (t->kind >= 3) tk->root_auto = 1; /* onRootNodeAutomatic */
        if (t->kind == 4) { tk->branch = pgl_branch; tk->event = pgl_event; }
        printf("racer npc row %d: talk %s" "\n", t->row, lab);
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
static void tr_control(RNpc *t) /* TeresaRacer::control: SE_SM_TERERACER_TRAMPLE on a trample (NPCActor's _D8: npc_react on
                                  * its Body, mSensorSize 220 at mSensorOffset (0, 25, 40)) */
{
    V3 body = vadd(vadd(rn_pos(t), vmul(t->pose.nu, 25.0f)), vmul(t->pose.nf, 40.0f));
    if (npc_react(&t->re, body, 220.0f, t->pose.nu, rn_pos(t), 450.0f, 0) & 1) {
        V3 p = rn_pos(t);
        se_start(SE_SM_TERERACER_TRAMPLE, &p.x);
        printf("teresa racer row %d: trampled" "\n", t->row);
    }
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
    if (t->kind >= 3) { pg_prep_racer(t); return; }
    t->rail.coord = 0.0f; t->rail.dir = 1; t->rail.speed = 0.0f; /* moveCoordToStartPos, setRailCoordSpeed 0 */
    rn_follow_rail_pose(t, 1.0f, 1.0f);
    rn_action(t, "chase", 1);
    t->p0 = t->p1 = 0;
}
static void rn_start_racer(RNpc *t) /* startRacer */
{
    if (t->kind >= 3) { rn_set(t, pg_move_nerve(t)); return; }
    t->goal = 0; t->bgm = 0; rn_set(t, TRC_MOVE);
}
static int rn_update_racer(RNpc *t, RmRacer *a) /* updateRacer: on the PlayerRacer's rail */
{
    V3 p = rn_pos(t);
    float coord = nrail_nearest(rm.rail.pts, rm.rail.np, rm.rail.cl, p);
    a->prev = a->cur; a->cur = p;
    if (fabsf(a->coord - coord) < 2000.0f) a->coord = coord;
    if (fabsf(a->coord - rm.rail.len) < 100.0f) { t->goal = rm_is_goal(a); return t->goal; }
    return 0;
}
static int rn_goal_racer(RNpc *t) { if (t->kind >= 3) rn_set(t, PG_GOAL); return 0; } /* goalRacer */
static void rn_lose_racer(RNpc *t) { if (t->kind >= 3) rn_set(t, PG_RACEWAIT); } /* loseRacer */
static void rn_reset_racer(RNpc *t) /* TeresaRacer::resetRacer */
{
    if (t->kind >= 3) { pg_reset_racer(t); return; }
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
    for (int i = nrn - 1; i >= 0; i--) if (rn[i].kind != 3) n = &rn[i]; /* the one Mario talks to (not a PenguinRacer) */
    V3 p = rn_pos(n);
    if (t - free_at < 120 && rm.nrv == RM_WAIT && n->nrv == (n->kind == 2 ? SK_WAIT : n->kind == 4 ? PG_WAIT : TRC_WAIT)) {
        mario->pos = vadd(vadd(p, vmul(n->pose.nf, n->kind != 1 ? 210.0f : 300.0f)), vmul(n->pose.nu, n->kind != 1 ? 0.0f : 30.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(n->pose.nf, -1.0f);
    }
#ifdef RACE_TRAMPLE /* (=1 XF -DRACE_TRAMPLE: Mario dropped on the Boo's head first: its trample SE) */
    if (n->kind == 1 && t - free_at >= 120 && t - free_at < 125) { mario->pos = vadd(p, vmul(n->pose.nu, 400.0f)); mario->hvel = v3(0, 0, 0); mario->vup = -5.0f; mario->grounded = 0; }
    if (n->kind == 1 && t - free_at < 240 && t - free_at >= 120) return;
#endif
    if (td.nrv != TD_SLCT) slct = 0;
    if (td.nrv == TD_SLCT) {
        if (++slct == 60) printf("race test: pointer on BoxRight %d" "\n", menu_test_point("BoxRight"));
        pad_force = slct == 80 ? PAD_CROSS : 0;
    } else if (t - free_at > 150 && (t % 45) < 5 && (rm.nrv != RM_RACE) && (td.nrv != TD_PREP || mario->grounded)) pad_force = PAD_CROSS;
    if (t % 60 == 0) printf("race test: Mario grounded %d, on %d, react %d" "\n", mario->grounded, mario->floor, n->react);
    if (n->kind == 2 && (n->nrv == SK_AWAY || n->nrv == SK_PROVOKE || n->nrv == SK_TURN || n->nrv == SK_SWITCH)) { /* the skater:
        * Mario slides after it at 20 a frame (sAwaySpeed 14) till he touches it */
        V3 d = vsub(p, mario->pos);
        float l = vlen(d);
        mario->pos = l > 20.0f ? vadd(mario->pos, vmul(d, 20.0f / l)) : p; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        if (l > 1e-3f) mario->face = vnorm(vtangent(d, mario->up));
    }
#if RACE_TEST == 1
    if (rm.nrv == RM_RACE) { /* along the judge rail, 400 ahead of the racer's coord, at 24 a frame at most */
        static float c;
        if (rm.step == 1) c = 0;
        RmRacer *a = &rm.r[n->ar];
        c = fminf(c + 24.0f, a->coord + 1200.0f);
        V3 d, q = nrail_at(rm.rail.pts, rm.rail.np, rm.rail.cl, fminf(c, rm.rail.len + 200.0f), &d);
        if (c > rm.rail.len) q = vadd(q, vmul(d, c - rm.rail.len));
        mario->pos = q; mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->face = d;
        life = max_life; player_mode = PM_INVINCIBLE; /* (carried through the course's enemies: no hits, no damage flight) */
    }
#endif
    if (n->nrv == (n->kind == 4 ? PG_TALK : TRC_TALK) && n->took && !pd.name && td.nrv != TD_TALK) /* the star out, its demo over: Mario on it */
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
        { float dep = 0; int wet = in_water(mario->pos, mario->up, &dep);
          printf("race test: Mario swimming %d, in water %d depth %d" "\n", swimming, wet, (int)dep); }
    }
}
#endif
/* --- PenguinSkater (NPC/PenguinSkater.cpp, rn kind 2) and NPCSupportRail (NPC/NPCSupportRail.cpp: a dead actor holding
 * a rail in the skater's group). With a rail: its group's loop rails are mCurrentRail (its own) and mSwitchRail, the
 * others the connectors (mSwitchRails); talked to at its end (Wait: offRootNodeAutomatic, forwardNode), the
 * ペンギンスケート開始 demo with its 開始 camera, Demo (IceSwimStart, sDemoSpeed 18) till half the loop ahead
 * (sProvokeRangeIn 1000), Away at sAwaySpeed 14 (accel 0.15): Turn (stopRail, brake 0.2, both loops reversed) when Mario
 * gets ahead, Switch onto a connector met at a passed point (sSamePosition 10, sSameDirection 0.55, within
 * sSwitchRange 3000) at sSwitchSpeed 10, its end: the other loop; Provoke (Congratulate1, a forced talk, till
 * sProvokeRangeOut 2000); caught by Mario's touch or spin (Away / Turn / Switch): the 捕まり demo (puppetable), its 終了
 * camera on Mario, Caught (TossStart, Mario's matrix blended to its over 5 frames), FadeOut (CaughtWait,
 * closeWipeCircle), FadeIn (ペンギン移動後 / マリオ移動後, Watch, SitDown / SitDownTalk), GiveUp (its talk to the end:
 * SW_A), SitDown. Without a rail: Talk. Reactions (npc_react: Body 50 at 50 up; Trampled / Spin / Pointing and their
 * voices, the Reaction nerve). ponytail: the circle wipes are fades, startTalkingSequence / startNPCTalkCamera (the
 * talk's own camera), the star piece reaction (_DB) */
static RmRail *sk_rail(RNpc *t) { return &t->sr[t->ri]; } /* mRail */
static int rr_next_point(const RmRail *rr) /* getNextRailPointNo */
{
    int n = rr_npoints(rr), cur = rr_point_no(rr);
    if (rr->dir > 0) return cur + 1 < n ? cur + 1 : rr->cl ? 0 : cur;
    if (rr->coord > rr_point_coord(rr, cur) + 0.01f || cur == 0) return rr->cl && cur == 0 && rr->coord <= 0.01f ? n - 1 : cur;
    return cur - 1;
}
static V3 rr_point_pos(const RmRail *rr, int k) { V3 d; return nrail_at(rr->pts, rr->np, rr->cl, rr_point_coord(rr, k), &d); } /* calcRailPointPos */
static float sk_lead(RNpc *t) /* calcLead */
{
    RmRail *rr = sk_rail(t);
    float pc = nrail_nearest(rr->pts, rr->np, rr->cl, mario->pos), c = rr->coord, half = rr->len / 2.0f; /* calcRailPosNearestPlayer */
    float diff = fabsf(c - pc);
    int ahead = c > pc;
    if (rr->dir > 0) { if ((ahead && diff > half) || (!ahead && diff < half)) diff = -diff; }
    else if ((ahead && diff < half) || (!ahead && diff > half)) diff = -diff;
    return diff;
}
static void sk_follow(RNpc *t, float blend) /* followRailPoseOnGround(this, mRail, blend) */
{
    RmRail *rr = sk_rail(t);
    V3 rp = rr_pos(rr), up = up_at(rp, t->pose.nu), hp, n; int inst;
    V3 p = rp;
    if (map_line(vadd(rp, vmul(up, 10.0f)), vmul(up, -1000.0f), &hp, &n, &inst)) p = hp; /* getFirstPolyOnLineToMap */
    npc_pose(&t->pose, up, rr_dir(rr), p, blend, 1.0f);
}
static void sk_move_rail(RNpc *t, float speed, float blend) /* moveRail: moveCoordAndFollowTransAndCheckPassPointNo */
{
    RmRail *rr = sk_rail(t);
    rr_adjust_speed(rr, speed, 0.15f); /* sAwayAccele */
    int next = rr_next_point(rr);
    rr_move(rr);
    t->cross = next == rr_next_point(rr) ? -1 : next;
    sk_follow(t, blend);
}
static void sk_stop_rail(RNpc *t, float blend) /* stopRail */
{
    RmRail *rr = sk_rail(t);
    rr_adjust_speed(rr, 0.0f, 0.2f); /* sAwayBrake */
    int next = rr_next_point(rr);
    rr_move(rr);
    t->cross = next == rr_next_point(rr) ? -1 : next;
    sk_follow(t, blend);
}
static void sk_reverse(RNpc *t) { t->sr[t->cur].dir = -t->sr[t->cur].dir; t->sr[t->swr].dir = -t->sr[t->swr].dir; } /* both loops */
static int sk_in_provoke(RNpc *t, float d, float range) { return fabsf(sk_rail(t)->len / 2.0f - fabsf(d)) < range; }
static int sk_switch(RNpc *t) /* trySwitchRail */
{
    RmRail *rr = sk_rail(t);
    float speed = rr->speed;
    V3 dir = rr_dir(rr), cross = rr_point_pos(rr, t->cross), d;
    for (int i = 0; i < t->nsr; i++) {
        RmRail *c = &t->sr[i];
        if (i == t->cur || i == t->swr) continue;
        V3 sp = nrail_at(c->pts, c->np, c->cl, 0.0f, &d);
        if (vlen(vsub(cross, sp)) < 10.0f && vdot(dir, d) > 0.55f) { c->dir = 1; c->coord = 0.0f; c->speed = speed; t->ri = i; return 1; }
        V3 ep = nrail_at(c->pts, c->np, c->cl, c->len, &d);
        if (vlen(vsub(cross, ep)) < 10.0f && vdot(dir, d) < -0.55f) { c->dir = -1; c->coord = c->len; c->speed = speed; t->ri = i; return 1; }
    }
    return 0;
}
static void sk_end_switch(RNpc *t) /* endSwitchRail */
{
    float speed = sk_rail(t)->speed;
    t->ri = t->swr;
    int c = t->cur; t->cur = t->swr; t->swr = c;
    t->sr[t->cur].speed = speed;
    RmRail *rr = sk_rail(t);
    rr->coord = nrail_nearest(rr->pts, rr->np, rr->cl, rn_pos(t)); /* moveCoordToNearestPos */
}
static int sk_attackable(const RNpc *t) { return t->nrv == SK_AWAY || t->nrv == SK_TURN || t->nrv == SK_SWITCH; }
static void sk_cam(RNpc *t, const char *ev, int on, int player) /* start / endMultiActorCamera 開始 / 終了 */
{
    if (row_camset[t->row] == -1) return;
    if (!on) { if (t->cam) { camera_event_stop(); t->cam = 0; } return; }
    t->cam = player ? camera_actor_start("PenguinSkater", ev, (unsigned short)row_camset[t->row], mario->up, mario->face, mario->pos)
                    : camera_actor_start("PenguinSkater", ev, (unsigned short)row_camset[t->row], t->pose.nu, t->pose.nf, rn_pos(t));
}
static int sk_react(RNpc *t) /* tryStartReaction: Trampled / Spin / Pointing (npc_react on its Body 50 at 50 up) */
{
    V3 body = vadd(rn_pos(t), vmul(t->pose.nu, 50.0f));
    int f = npc_react(&t->re, body, 50.0f, t->pose.nu, rn_pos(t), 450.0f, 0);
    if (!f) return 0;
    t->react = f;
    Inst *in = rn_inst(t);
    if (in) obj_play(in, f & 1 ? "trampled" : f & 2 ? "spin" : "pointing", 0, 0);
    printf("penguin skater row %d: reaction %d" "\n", t->row, f);
    return 1;
}
static void sk_exe(RNpc *t)
{
    int st = t->step;
    V3 p = rn_pos(t);
    switch (t->nrv) {
    case SK_REACTION: /* exeReaction */
        if (st == 0) {
            if (t->react & 1) { se_start(SE_SM_PENGUIN_TRAMPLED, &p.x); se_start(SE_SV_PENGUIN_S_TRAMPLED, &p.x); }
            if (t->react & 8) { se_start(SE_SV_PENGUIN_S_POINTED_2P, &p.x); dpd_hit_sound(); }
            if (t->react & 2) se_start(SE_SV_PENGUIN_S_SPIN_HIT, &p.x);
        }
        if (rn_action_done(t)) rn_pop(t); /* tryStartReactionAndPopNerve: isActionLoopedOrStopped */
        break;
    case SK_WAIT: /* exeWait */
        if (sk_react(t)) { rn_push(t, SK_REACTION); break; }
        rn_try_talk_action(t);
        if (!talk_near_at_end(t->k)) break;
        pd_try_start("ペンギンスケート開始", t->row); /* tryStartDemo */
        sk_cam(t, "開始", 1, 0);
        talk_forward_node(t->k);
        {
            V3 d, sp = nrail_at(sk_rail(t)->pts, sk_rail(t)->np, sk_rail(t)->cl, 0.0f, &d); /* calcRailStartPointDirection */
            (void)sp;
            if (vdot(vsub(mario->pos, p), d) > 0.0f) sk_reverse(t);
        }
        rn_set(t, SK_DEMO);
        printf("penguin skater row %d: the chase starts" "\n", t->row);
        break;
    case SK_DEMO: /* exeDemo */
        if (st == 0) rn_action(t, "iceswimstart", 0);
        if (st > 0 && rn_action_done(t)) rn_action(t, "iceswim", 1);
        if (st == 33) se_start(SE_SM_PENGUIN_SKATE_START, &p.x); /* sDemoSkateSeStartTiming */
        if (st >= 33) t->lv = se_level(SE_SM_LV_PENGUIN_SKATE, &p.x, t->lv);
        sk_move_rail(t, 18.0f, 0.05f); /* sDemoSpeed, sBlendRatio */
        if (sk_in_provoke(t, sk_lead(t), 1000.0f)) { sk_cam(t, "開始", 0, 0); pd_end("ペンギンスケート開始"); rn_set(t, SK_AWAY); }
        break;
    case SK_AWAY: { /* exeAway */
        if (st == 0) rn_action(t, "iceswim", 1);
        t->lv = se_level(SE_SM_LV_PENGUIN_SKATE, &p.x, t->lv);
        sk_move_rail(t, 14.0f, 0.05f); /* sAwaySpeed */
        float lead = sk_lead(t);
        if (lead < 0.0f) rn_set(t, SK_TURN);
        else if (fabsf(lead) <= 3000.0f && t->cross != -1 && sk_switch(t)) rn_set(t, SK_SWITCH); /* inSwitchRange */
        else if (sk_in_provoke(t, lead, 1000.0f)) rn_set(t, SK_PROVOKE);
        break;
    }
    case SK_TURN: /* exeTurn */
        if (st == 0) rn_action(t, "iceswimturn", 0);
        if (st > 0 && rn_action_done(t)) rn_action(t, "iceswim", 1);
        sk_stop_rail(t, 1.0f);
        if (fabsf(sk_rail(t)->speed) < 0.001f) { sk_reverse(t); rn_set(t, SK_AWAY); }
        break;
    case SK_SWITCH: /* exeSwitch */
        t->lv = se_level(SE_SM_LV_PENGUIN_SKATE, &p.x, t->lv);
        sk_move_rail(t, 10.0f, 0.05f); /* sSwitchSpeed */
        if (rr_reached_goal(sk_rail(t))) { sk_end_switch(t); rn_set(t, SK_AWAY); }
        break;
    case SK_PROVOKE: /* exeProvoke */
        if (fabsf(sk_rail(t)->speed) >= 0.001f) { sk_stop_rail(t, 1.0f); break; }
        rn_action(t, "congratulate1", 1); /* tryStartBck */
        if (vlen(vsub(mario->pos, p)) < 10000.0f) { talk_force(t->k, 1); rn_turn_to_player(t, t->p8); } /* tryTalkForce, turnToPlayer */
        {
            float lead = sk_lead(t);
            if (!sk_in_provoke(t, lead, 2000.0f)) {
                if (lead < 0.0f) sk_reverse(t);
                rn_set(t, SK_AWAY);
                se_start(SE_SM_PENGUIN_SKATE_START, &p.x);
            }
        }
        break;
    case SK_CAUGHT: /* exeCaught */
        if (st == 0) {
            pd_try_start("捕まり", t->row); /* tryStartDemoMarioPuppetable */
            rm.bck = "tossstart"; rm.bck_demo = "捕まり";
            sk_cam(t, "終了", 1, 1);
            rn_action(t, "caught", 0);
            se_start(SE_SM_PENGUIN_CAUGHT, &p.x);
            sk_rail(t)->speed = 0.0f; /* zeroVelocity */
        }
        if (st == 20) { se_start(SE_SV_PENGUIN_S_CAUGHT, &p.x); se_start(SE_SY_TOTAL_COMPLETE, NULL); }
        { /* blendBaseMatrixToMario(calcNerveRate(sMarioPoseBlendTime 5)) */
            float k = fminf((float)st / 5.0f, 1.0f);
            mario->pos = vadd(mario->pos, vmul(vsub(p, mario->pos), k));
            mario->up = vnorm(vadd(mario->up, vmul(vsub(t->pose.nu, mario->up), k)));
            V3 f = vtangent(vadd(mario->face, vmul(vsub(t->pose.nf, mario->face), k)), mario->up);
            if (vlen(f) > 1e-6f) mario->face = vnorm(f);
        }
        if (st > 0 && rn_action_done(t)) { rn_set(t, SK_FADEOUT); rm.bck = "tosswait"; }
        break;
    case SK_FADEOUT: /* exeFadeOut */
        if (st == 0) { rm_player_at(p, t->pose.nu, t->pose.nf); rn_action(t, "caughtwait", 1); menu_wipe(1, -1, 0); } /* closeWipeCircle */
        if (!menu_wipe_active()) rn_set(t, SK_FADEIN);
        break;
    case SK_FADEIN: /* exeFadeIn */
        if (st == 0) {
            const float *g = scene_gpos("ペンギン移動後"), *gm = scene_gpos("マリオ移動後");
            float m[16], one[3] = {1, 1, 1};
            if (g) { /* setNPCActorPos: placed there, its pose and _B0 / _C0 */
                float rot[3] = {g[3], g[4], g[5]};
                mat_translate_rot_scale(m, g, rot, one);
                t->pose.m[12] = g[0]; t->pose.m[13] = g[1]; t->pose.m[14] = g[2];
                t->pose.nu = t->pose.nu0 = vnorm(v3(m[4], m[5], m[6])); t->pose.nf = t->pose.nf0 = vnorm(v3(m[8], m[9], m[10]));
                t->pos0 = v3(g[0], g[1], g[2]);
            }
            if (gm) { /* setPlayerPosOnGroundAndWait */
                float rot[3] = {gm[3], gm[4], gm[5]};
                mat_translate_rot_scale(m, gm, rot, one);
                V3 at = v3(gm[0], gm[1], gm[2]), up = up_at(at, v3(m[4], m[5], m[6])), hp, n; int inst;
                if (map_line(vadd(at, vmul(up, 100.0f)), vmul(up, -1000.0f), &hp, &n, &inst)) at = hp;
                rm_player_at(at, up, v3(m[8], m[9], m[10]));
                mario->grounded = 1;
            }
            sk_cam(t, "終了", 0, 0);
            pd_end("捕まり");
            talk_forward_node(t->k);
            rm.bck = NULL;
            menu_wipe(0, -1, 0); /* openWipeCircle */
            t->mv[0] = t->mv[1] = "sitdown"; t->tk[0] = t->tk[1] = "sitdowntalk"; t->p0 = t->p1 = 0; /* setMoveTalkNoTurnAction */
            printf("penguin skater row %d: caught, moved" "\n", t->row);
        }
        rn_try_turn_action(t);
        if (!menu_wipe_active()) rn_set(t, SK_GIVEUP);
        break;
    case SK_GIVEUP: /* exeGiveUp */
        rn_try_talk_action(t);
        if (!talk_force_at_end(t->k, 1)) break;
        talk_forward_node(t->k);
        { const Row *r = &sc->objs[t->row]; if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1); } /* onSwitchA */
        printf("penguin skater row %d: SW_A on" "\n", t->row);
        rn_set(t, SK_SITDOWN);
        break;
    case SK_SITDOWN: /* exeSitDown */
        rn_try_talk_action(t);
        talk_near(t->k);
        break;
    case SK_TALK: /* exeTalk */
        if (sk_react(t)) { rn_push(t, SK_REACTION); break; }
        rn_try_talk_action(t);
        talk_near(t->k);
        break;
    }
}
static void sk_sensor(RNpc *t) /* attackSensor (Mario touches its Body) / receiveMsgPlayerAttack (his spin): Caught */
{
    if (!sk_attackable(t)) return;
    V3 body = vadd(rn_pos(t), vmul(t->pose.nu, 50.0f));
    if (mario_touch(body, 50.0f) || mario_offence(body, 50.0f, vmul(t->pose.nu, -1.0f)) == MSG_HITALL) {
        pad_rumble(RUMBLE_STRONG);
        rn_set(t, SK_CAUGHT);
        printf("penguin skater row %d: caught" "\n", t->row);
    }
}
static void sk_init(RNpc *t, const Row *r) /* PenguinSkater::init, initAfterPlacement */
{
    t->kind = 2; t->k = -1;
    t->mv[0] = "wait"; t->mv[1] = "turn"; t->tk[0] = "talk"; t->tk[1] = "talkturn";
    t->p0 = t->p1 = 1; t->p4 = 2000.0f; t->p8 = 4.0f;
    t->re.c158 = 1024;
    t->nrv = SK_TALK;
    if (!rr_init(&t->sr[0], r)) return; /* isConnectedWithRail */
    t->nsr = 1; t->cur = 0; t->swr = -1;
    int ty = scene_type(sc, "NPCSupportRail");
    for (u32 i = 0; ty >= 0 && i < sc->nobjs && t->nsr < 8; i++) { /* the group's rails (joinToGroupArray) */
        const Row *q = &sc->objs[i];
        if ((int)q->type != ty || q->group != r->group || q->zone != r->zone || !rr_init(&t->sr[t->nsr], q)) continue;
        if (t->sr[t->nsr].cl && t->swr < 0) t->swr = t->nsr;
        t->nsr++;
    }
    if (t->swr < 0) { t->nsr = 0; return; }
    t->ri = t->cur;
    t->nrv = SK_WAIT;
    sk_follow(t, 1.0f);
    printf("penguin skater row %d: %d rails (loop %d / %d), len %d" "\n", t->row, t->nsr, t->cur, t->swr, (int)t->sr[0].len);
}
/* --- PenguinRacer (NPC/PenguinRacer.cpp, rn kind 3) and PenguinRacerLeader (NPC/PenguinRacerLeader.cpp, kind 4).
 * A racer (arg0 mRaceDisabled 0) rides its rail in the race: each passed rail point sets its speed (point arg0) and
 * its section (point arg1 of this / the next point: both 0 Swim, both set Walk, StoW / WtoS between), its velocity
 * pulled toward the next point by mSpeed (1, sGoalBrake / sFallBrake down), clamped (walking: vertical 5x), gravity
 * off the ground out of Swim, its binder (80, offset 60 in the race, 80 out of it) on the map; Fall on a thrown shell
 * (sFallFrame 240, SE_SV_PENGUIN_S_DAMAGE_SHELL / HEAT_UP), Goal (braking till still), Turn (3 degrees a frame for
 * sTurnFrame 60). Talk (createTalkCtrl, the balloon 130 up), Wait (SE_SV_LV_PENGUIN_S_CHAT, reactions), RacerWait /
 * Turn, RacerTalk / TalkTurn. The leader: its 会話 camera, branchFunc 0 isStageStatePowerStarAppeared / 1 rank 1,
 * eventFunc 0 startRaceWithWipe / 1 TakeOutStar (then endMultiActorCamera), Pre (Yes: the race), Post (forced talk:
 * rank 1 Talk, else forceKillPlayerByGroundRace). ponytail: the star piece reflect message, its LodCtrl near clipping (sNearClipping 200), NPCActorItem (its goggles), the star pointer target */
static int pg_move_nerve(const RNpc *t) /* getMoveNerve */
{
    int cur = rr_point_arg(&t->rail, rr_point_no(&t->rail), 1), next = rr_point_arg(&t->rail, rr_next_point(&t->rail), 1);
    if (cur == -1) cur = 0; /* getCurrentRailPointArg1NoInit / getNextRailPointArg1NoInit (0) */
    if (next == -1) next = 0;
    return cur && next ? PG_WALK : !cur && !next ? PG_SWIM : cur ? PG_WTOS : PG_STOW;
}
static float pg_move_speed(const RNpc *t) /* getMoveSpeed: the current point's arg0, else as it was */
{
    int a = rr_point_arg(&t->rail, rr_point_no(&t->rail), 0);
    return a != -1 ? (float)a : t->mspeed;
}
static int pg_in_water(const RNpc *t) { return in_water(rn_pos(t), t->pose.nu, NULL); } /* MR::isInWater */
static int pg_in_race(const RNpc *t) { return t->nrv == PG_WALK || t->nrv == PG_SWIM || t->nrv == PG_STOW || t->nrv == PG_WTOS; }
static void pg_bind(RNpc *t) /* LiveActor::movement: mVelocity through its binder (radius 80 at its offset) */
{
    V3 up = up_at(rn_pos(t), t->pose.nu), gn, wn;
    V3 p = vadd(rn_pos(t), t->vel), c = vadd(p, vmul(up, t->boff));
    int wall = 0;
    t->ground = collide_n(&c, up, 80.0f, &gn, &wn, &wall);
    p = vsub(c, vmul(up, t->boff));
    if (t->ground && vdot(t->vel, up) < 0.0f) t->vel = vtangent(t->vel, up);
    t->pose.m[12] = p.x; t->pose.m[13] = p.y; t->pose.m[14] = p.z;
}
static void pg_velocity(RNpc *t) /* updateVelocity */
{
    RmRail *rr = &t->rail;
    V3 p = rn_pos(t), grav = vmul(up_at(p, t->pose.nu), -1.0f);
    rr->coord = nrail_nearest(rr->pts, rr->np, rr->cl, p); /* moveCoordToNearestPos */
    V3 to = vsub(rr_point_pos(rr, rr_next_point(rr)), p);
    if (vlen(to) > 1e-6f) to = vnorm(to);
    t->vel = vadd(t->vel, vmul(to, t->pspeed));
    int swim = pg_move_nerve(t) == PG_SWIM;
    V3 vv = swim ? v3(0, 0, 0) : vmul(grav, vdot(grav, t->vel)), vh = vsub(t->vel, vv);
    float mv = swim ? t->mspeed : t->mspeed * 5.0f, mh = t->mspeed;
    if (vlen(vv) > mv) vv = vmul(vnorm(vv), mv);
    if (vlen(vh) > mh) vh = vmul(vnorm(vh), mh);
    t->vel = vadd(vv, vh);
}
static void pg_update_move(RNpc *t) /* updateMove: velocity, gravity off the ground (not swimming), updateRail, decidePose */
{
    pg_velocity(t);
    V3 up = up_at(rn_pos(t), t->pose.nu);
    if (pg_move_nerve(t) != PG_SWIM && !t->ground) t->vel = vadd(t->vel, vmul(up, -1.0f)); /* addVelocityToGravity 1 */
    int no = rr_point_no(&t->rail); /* updateRail */
    if (no > t->pno) {
        t->pno = no; t->mspeed = pg_move_speed(t);
        if (t->nrv != PG_FALL && t->nrv != pg_move_nerve(t)) rn_set(t, pg_move_nerve(t));
    }
    npc_pose(&t->pose, up, rr_dir(&t->rail), rn_pos(t), 0.1f, 1.0f);
}
static int pgl_branch(int k, u32 v) { (void)k; return v == 0 ? star_appeared() : v == 1 ? rm.rank == 1 : 0; }
static int pgl_event(int k, u32 v) /* PenguinRacerLeader::eventFunc */
{
    RNpc *t = rn_of_talker(k);
    if (!t) return 1;
    if (v == 0) { rm_start_with_wipe(); return 1; }
    if (v == 1) {
        if (t->tos == 2) { rn_cam(t, 0); return 1; }
        if (t->tos == 0) { t->tos = 1; t->tos_step = 0; rn_push(t, PG_TAKEOUT); rn_action(t, "takeoutstar", 0); printf("penguin racer leader row %d: TakeOutStar" "\n", t->row); }
        return 0;
    }
    return 1;
}
static int pg_react(RNpc *t) /* tryReaction (tryStartReactionAndPushNerve): its push sensor 80 at 80 up */
{
    V3 body = vadd(rn_pos(t), vmul(t->pose.nu, 80.0f));
    int f = npc_react(&t->re, body, 80.0f, t->pose.nu, rn_pos(t), 450.0f, 0);
    if (!f) return 0;
    t->react = f;
    Inst *in = rn_inst(t);
    if (in) obj_play(in, f & 1 ? "trampled" : f & 2 ? "spin" : "pointing", 0, 0);
    rn_push(t, PG_REACTION);
    return 1;
}
static void pg_exe(RNpc *t)
{
    int st = t->step, leader = t->kind == 4;
    V3 p = rn_pos(t);
    switch (t->nrv) {
    case PG_REACTION: /* exeReaction */
        if (st == 0) {
            if (t->react & 1) { se_start(SE_SM_PENGUIN_TRAMPLED, &p.x); se_start(SE_SV_PENGUIN_S_TRAMPLED, &p.x); }
            if (t->react & 8) { se_start(SE_SV_PENGUIN_S_POINTED_2P, &p.x); dpd_hit_sound(); }
            if (t->react & 2) se_start(SE_SV_PENGUIN_S_SPIN_HIT, &p.x);
        }
        if (rn_action_done(t)) rn_pop(t);
        break;
    case PG_WAIT: /* exeWait */
        if (!leader) t->lv = se_level(SE_SV_LV_PENGUIN_S_CHAT, &p.x, t->lv);
        if (pg_react(t)) break;
        rn_try_talk_action(t);
        if (talk_near(t->k) && leader && t->id == 0) { rn_cam(t, 1); rn_set(t, PG_PRE); }
        break;
    case PG_PRE: /* PenguinRacerLeader::exePre */
        rn_try_talk_action(t);
        if (!talk_near_at_end(t->k)) break;
        if (tk_yesno == 1) { rm_start_with_wipe(); rn_set(t, PG_READY); printf("penguin racer leader row %d: the race (Yes)" "\n", t->row); }
        else { rn_cam(t, 0); rn_set(t, PG_WAIT); }
        break;
    case PG_READY: case PG_TAKEOUT: break;
    case PG_POST: /* exePost */
        rn_try_talk_action(t);
        if (!talk_force_at_end(t->k, 1)) break;
        rn_cam(t, 0);
        if (rm.rank == 1) rn_set(t, PG_TALK);
        else { miss_race = 1; miss(); rn_set(t, PG_READY); printf("penguin racer leader row %d: Mario lost: forceKillPlayerByGroundRace" "\n", t->row); }
        break;
    case PG_TALK: /* exeTalk */
        if (pg_react(t)) break;
        rn_try_talk_action(t);
        talk_near(t->k);
        break;
    case PG_RACEWAIT: /* exeRaceWait */
        if (st == 0) { rn_action(t, pg_in_water(t) ? "racerswimwaitsurface" : "racerwait", 1); } /* (setBckFrameAtRandom) */
        t->vel = vmul(t->vel, 0.9f); /* attenuateVelocity */
        break;
    case PG_WALK: /* exeRaceWalk */
        if (st == 0) rn_action(t, "racerlandswim", 1);
        t->lv = se_level(SE_SM_LV_PENGUIN_SLIDE, &p.x, t->lv);
        if (vlen(vsub(mario->pos, p)) < 500.0f) t->lv2 = se_level(SE_SV_LV_PENGUIN_S_RACE_SWIM, &p.x, t->lv2); /* sSwimSoundPlayDistance */
        pg_update_move(t);
        break;
    case PG_SWIM: /* exeRaceSwim */
        if (st == 0) rn_action(t, "racerswim", 1);
        if (vlen(vsub(mario->pos, p)) < 500.0f) t->lv2 = se_level(SE_SV_LV_PENGUIN_S_RACE_SWIM, &p.x, t->lv2);
        pg_update_move(t);
        break;
    case PG_STOW: case PG_WTOS: /* exeRaceStoW / exeRaceWtoS */
        if (st == 0) {
            rn_action(t, t->nrv == PG_STOW ? "racerlandswimslow" : "racerswimslow", 0);
            se_start(t->nrv == PG_STOW ? SE_SM_PENGUIN_WATER_OUT : SE_SM_PENGUIN_WATER_IN, &p.x);
        }
        pg_update_move(t);
        if (t->next >= 0 && t->next != t->nrv) { float m[16]; basis_matrix(m, rn_pos(t), t->pose.nu, t->pose.nf, 0); fx_named("Penguin", "WaterColumn", m, NULL, t); }
        break;
    case PG_FALL: /* exeRaceFall */
        if (st == 0) { rn_action(t, "racerswimdamage", 0); se_start(SE_SV_PENGUIN_S_DAMAGE_SHELL, &p.x); }
        t->lv = se_level(SE_SM_LV_PENGUIN_S_SWOON, &p.x, t->lv);
        t->pspeed = fmaxf(t->pspeed - 0.002f, 0.0f); /* sFallBrake */
        t->vel = vmul(t->vel, 0.9f);
        pg_update_move(t);
        if (st >= 240) { /* sFallFrame */
            t->pno = rr_point_no(&t->rail); t->mspeed = pg_move_speed(t);
            se_start(SE_SV_PENGUIN_S_HEAT_UP, &p.x);
            rn_set(t, pg_move_nerve(t));
        }
        break;
    case PG_GOAL: { /* exeRaceGoal */
        t->pspeed = fmaxf(t->pspeed - 0.005f, 0.0f); /* sGoalBrake */
        t->vel = vmul(t->vel, 0.9f);
        if (vlen(t->vel) > 5.0f) {
            pg_velocity(t);
            V3 up = up_at(rn_pos(t), t->pose.nu);
            if (pg_move_nerve(t) != PG_SWIM && !t->ground) t->vel = vadd(t->vel, vmul(up, -1.0f));
            npc_pose(&t->pose, up, rr_dir(&t->rail), rn_pos(t), 0.1f, 1.0f);
        } else { t->vel = v3(0, 0, 0); rn_set(t, PG_TURN); }
        break;
    }
    case PG_TURN: /* exeRaceTurn */
        if (st == 0) rn_action(t, pg_in_water(t) ? "racerswimwaitsurface" : "racerwait", 1);
        if (st < 60) t->pose.nf = rot_axis(t->pose.nf, t->pose.nu, 3.0f * 0.017453292f); /* sTurnFrame, mRotation.y + 3 */
        break;
    }
    if (pg_in_race(t) || t->nrv == PG_FALL || t->nrv == PG_GOAL || t->nrv == PG_RACEWAIT) pg_bind(t);
}
static int pg_jet_hit(V3 p) /* receiveMsgPlayerAttack: isMsgJetTurtleAttack in the race (its push sensor 80 at 80 up): RaceFall */
{
    for (int i = 0; i < nrn; i++) {
        RNpc *t = &rn[i];
        if ((t->kind != 3 && t->kind != 4) || !pg_in_race(t)) continue;
        if (vlen(vsub(p, vadd(rn_pos(t), vmul(t->pose.nu, 80.0f)))) >= 80.0f + 60.0f) continue;
        rn_set(t, PG_FALL);
        printf("penguin racer row %d: hit by the shell" "\n", t->row);
        return 1;
    }
    return 0;
}
static void pg_prep_racer(RNpc *t) /* PenguinRacer::prepRacer */
{
    t->boff = 60.0f; /* mBinder->mOffsetY */
    t->rail.coord = 0.0f; t->rail.dir = 1; /* moveCoordToStartPos, moveTransToCurrentRailPos */
    V3 p = rr_pos(&t->rail);
    t->pose.m[12] = p.x; t->pose.m[13] = p.y; t->pose.m[14] = p.z;
    t->pno = 0; t->mspeed = pg_move_speed(t); t->pspeed = 1.0f;
    npc_pose(&t->pose, up_at(p, t->pose.nu), rr_dir(&t->rail), p, 1.0f, 1.0f);
    if (pg_in_water(t)) rn_action(t, "racerswimsurfacesoon", 1);
    else {
        V3 hp, n; int inst;
        if (map_line(p, vmul(t->pose.nu, -1000.0f), &hp, &n, &inst)) { t->pose.m[12] = hp.x; t->pose.m[13] = hp.y; t->pose.m[14] = hp.z; }
        rn_action(t, "racerwalksoon", 1);
    }
    t->vel = v3(0, 0, 0); /* resetPosition */
    rn_set(t, PG_RACEWAIT);
}
static void pg_reset_racer(RNpc *t) /* PenguinRacer::resetRacer (+ the leader's) */
{
    t->boff = 80.0f;
    t->pose.nf = t->pose.nf0; t->pose.nu = t->pose.nu0; /* setDefaultPose */
    t->pose.m[12] = t->pos0.x; t->pose.m[13] = t->pos0.y; t->pose.m[14] = t->pos0.z;
    t->vel = v3(0, 0, 0);
    rn_action(t, pg_in_water(t) ? "racerswimwaitsoon" : "racerwaitsoon", 1);
    rn_set(t, PG_WAIT);
    if (t->kind == 4) { rn_turn_to_player(t, 180.0f); rn_cam(t, 1); rn_set(t, PG_POST); }
}
static void pg_init(RNpc *t, const Row *r, int leader) /* PenguinRacer::init / PenguinRacerLeader::init */
{
    t->kind = leader ? 4 : 3; t->k = -1;
    t->id = r->arg[0] == -1 ? 0 : r->arg[0]; /* mRaceDisabled (getJMapInfoArg0NoInit: 0) */
    t->mv[0] = "racerwait"; t->mv[1] = "turn"; t->tk[0] = "racertalk"; t->tk[1] = "talkturn";
    t->p0 = t->p1 = 1; t->p4 = 2000.0f; t->p8 = 4.0f;
    t->re.c158 = 1024; t->boff = 80.0f; t->pspeed = 1.0f;
    t->nrv = PG_WAIT;
    if (t->id == 0) rr_init(&t->rail, r);
}
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
        if (t->kind == 1) { tr_control(t); tr_exe(t); }
        else if (t->kind == 2) { sk_exe(t); sk_sensor(t); }
        else pg_exe(t);
        t->step++;
        if (t->next >= 0) { t->nrv = t->next; t->step = 0; t->next = -1; printf("racer npc row %d: nerve %d" "\n", t->row, t->nrv); }
        rn_place(t);
    }
}
