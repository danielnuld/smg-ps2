/* KoopaJrShip.cpp, KoopaJrShipCannonShell.cpp, KoopaJrShipCannonMainShell.cpp and KoopaJr.cpp (its ShipBattle states)
 * after Petari: Bowser Jr.'s airship (KoopaJrShipLv1Galaxy 1, KoopaJrShipBattleLv1Tmp1Zone). #included by game.c (kj_*).
 * The ship (cast of クッパＪｒシップバトル) waits dead until the sheet's 出現 (Appear: SE_BM_KOOPAJR_SHIP_ENTER, Arrival,
 * Koopa Jr. out with AppearDemo; at クッパJr会話's last step MBGM_BOSS_06_A, Move). It rides its rail (2 / 4 a frame,
 * eased in over _188 60 and towards a point), its propellers turning 20 (40 when hurt), shooting: every 120 steps of
 * Move (no killer out) Shoot1 (one shell, _D0 5) or Shoot2 (five, 45 apart) from FirePoint1 at 15; at a rail end
 * StopAtEnd (150 / 90), then back. Five JetTurtle hits (its collision's body message sensor): Damage (Damage clip,
 * Stumble, DamageSmoke1 at 3 / 2 at 1, DamageFire, stopScene 12; at 90: killers out, Kameck 0 at 3 / 1 at 1, MoveFrontAttack
 * once _D0 <= 2); at the third hit the 旋廻 demo (TurnFront: 0.6 a step for 150, EyeLight, AngryDemo, MBGM_BOSS_06_B,
 * Koopa Jr. in front); MoveFrontAttack (HeadShootSign, 120 steps or the goal) / ShootMain (HeadShoot, three main shells
 * at 23 from FirePoint3, 0 / +15 / -15 degrees, all killers at _D0 1). The fifth: BreakStart (60), the 破壊 demo, Break
 * (KoopaJrShipBreak's Break, KoopaJrShipPod's Escape carrying Koopa Jr. RunAwayDemo), at the demo's end its grand star
 * (requestAppearPowerStar at its place) and startAfterBossBGM. Its shells (18, KoopaJrShipCannonShell: 360 steps, base
 * scale 1) and main shells (9, scale 1.2, 300 steps, FireStrong on Mario, explode on a wall after 60), six HomingKillers
 * (chase 5500 / 6500) from its cannons, two FireBallBeamKamecks (its ChildKameck rows, dead until it calls them).
 * ponytail: the joint transforms (Propeller00 about Y, Screw00 / 01 about Z) put on the posed joints' world matrices,
 * no KoopaJr reaction to star pieces / spins (NPCActor tryStartReaction: Damage / Recovery, 20 star pieces), its turn
 * to Mario at 5 degrees a step (NPCActor::turnToPlayer's own speed), the event cameras on the ship's placement (not the
 * moving ship), no shadows, no star pointer freeze of shells / killers, killers not exploding other actors */
enum { KJ_DEAD, KJ_APPEAR, KJ_MOVE, KJ_FRONT, KJ_SHOOT1, KJ_SHOOT2, KJ_SHOOTMAIN, KJ_STOPEND, KJ_POWERUP, KJ_DAMAGE,
       KJ_BREAKSTART, KJ_BREAK, KJ_BREAKEND, KJ_TURNFRONT, KJ_DONE };
enum { JR_WAIT, JR_APPEAR, JR_POWERUP, JR_ESCAPE, JR_SHIPDAMAGE, JR_TALKSTART, JR_TALKWAIT };
#define KJ_SHELLS 18
#define KJ_MAINS 9
#define KJ_KILLERS 6
typedef struct { V3 pos, vel, dir; int t, state, smoke; u32 lv; } KjShell; /* state 0 dead, 1 Fly, 2 Down, 3 HipDropDown */
typedef struct { Killer k; int nrv, t, inv, move; V3 bpos, bfront; u32 lv; } KjKiller; /* nrv 0 dead, 1 Appear, 2 MoveStart/Move, 3 Chase */
static struct {
    int row, nrv, st, d0, r188, demo, inst, dir;
    float coord, len, rspeed, f184, ec, prop, roty;
    const float *rail; u32 nrail;
    V3 pos, d4, e0, p1f8, jroff;
    Anim *anim, *banim, *panim, *jranim;
    const Model *md, *mbreak, *mpod, *mjr, *mshell, *mmain, *mkiller;
    const unsigned char *kcl;
    int jr_on, jr_nrv, jr_st, jr_hidden, jr_angry, jr_talked, talk, jr_row; /* jr_row: its owner (KoopaJrShip / DemoKoopaJrShip) */
    int hidden, break_on, pod_on, pod_hidden;
    int smoke1, smoke2, eye, sign;
    float base[16], shipm[16], podm[16], jrm[16];
    KjShell sh[KJ_SHELLS];
    struct { V3 pos, vel, dir; int t, on; u32 lv; } ms[KJ_MAINS];
    KjKiller kl[KJ_KILLERS];
    int kam[2], ac;
    u32 lv[8];
} kj = {.row = -1};
static const int kj_cannon[6] = {1, 2, 3, 5, 6, 7};       /* FirePoint0, 1, 2, 4, 5, 6 (cJointNameCannon) */
static const char *const kj_shootfx[6] = {"ShootJ0", "ShootJ1", "ShootJ2", "ShootJ4", "ShootJ5", "ShootJ6"};
enum { KJJ_ROOT = 0, KJJ_MAIN = 4, KJJ_PROP = 8, KJJ_SCREW0 = 9, KJJ_SCREW1 = 10 }; /* KoopaJrShip, FirePoint3, Propeller00, Screw00 / 01 */

static void kj_set(int n) { kj.nrv = n; kj.st = -1; printf("koopajr ship: nerve %d (d0 %d)\n", n, kj.d0); }
static void jr_set(int n) { kj.jr_nrv = n; kj.jr_st = -1; }
static int kj_break(void) { return kj.nrv == KJ_BREAKSTART || kj.nrv == KJ_BREAK || kj.nrv == KJ_BREAKEND; } /* isStateBreak */
static int kj_demo_k(void) { for (int i = 0; i < ndemos; i++) if (!strcmp(demos[i].h->name, "クッパＪｒシップバトル")) return i; return -1; }
static int kj_part_last(int p) { return dx.on && dx.cur == kj.demo && dx_last(p); } /* isDemoPartLastStep: 1 クッパJr会話, 2 旋廻 */
static int kj_demo_last(void) { return dx.on && dx.cur == kj.demo && dx_last(demos[dx.cur].h->nparts - 1); } /* isDemoLastStep */
static void kj_se(u32 id) { se_start(id, &kj.pos.x); }
static void kj_lv(int k, u32 id) { kj.lv[k] = se_level(id, &kj.pos.x, kj.lv[k]); }
static void kj_jm(float o[16], int j) /* MR::getJointMtx (the posed joint in the world) */
{
    if (kj.anim && kj.anim->skin && (u32)j < kj.anim->skin->njoints) mat_mul(o, kj.base, kj.anim->world[j]); else memcpy(o, kj.base, 64);
}
static V3 kj_col(const float *m, int k) { return v3(m[4 * k], m[4 * k + 1], m[4 * k + 2]); }
static void kj_fx(const char *name, const void *key) { fx_named("KoopaJrShip", name, kj.base, kj.anim ? (const float (*)[16])kj.anim->world : NULL, key); }
static void kj_base(void) /* calcAndSetBaseMtx: mPosition, mRotation (degrees) */
{
    float rot[3] = {0, kj.roty, 0}, one[3] = {1, 1, 1}, p[3] = {kj.pos.x, kj.pos.y, kj.pos.z};
    mat_translate_rot_scale(kj.base, p, rot, one);
    if (kj.inst >= 0 && kj.inst < ninsts && insts[kj.inst].row == kj.row) memcpy(insts[kj.inst].m, kj.base, 64); /* its collision */
}
static void kj_rail_move(void) /* moveCoordAndFollowTrans */
{
    kj.coord += kj.dir * kj.rspeed;
    kj.coord = kj.coord < 0 ? 0 : kj.coord > kj.len ? kj.len : kj.coord;
    if (kj.rail) kj.pos = km_rail_at(kj.rail, kj.nrail, kj.coord);
}
static int kj_edge(void) { return kj.coord <= 0 || kj.coord >= kj.len; }                 /* isRailReachedEdge */
static int kj_goal(void) { return kj.dir > 0 ? kj.coord >= kj.len : kj.coord <= 0; }      /* isRailReachedGoal */
static float kj_ease(float x, float a, float b, float max) { return a + (b - a) * (1.0f - cosf(x / max * 1.5707964f)); } /* getEaseInValue */
static float kj_prop_speed(void) /* getPropellerRotSpeed */
{
    if (kj.nrv == KJ_BREAKSTART) return 40.0f * (1.0f - fminf(kj.st / 60.0f, 1.0f));
    if (kj.nrv == KJ_BREAK) return 0.0f;
    if (kj.nrv == KJ_POWERUP || (kj.d0 <= 2 && kj.nrv != KJ_DAMAGE)) return 40.0f;
    return 20.0f;
}
static void kj_coord_speed(void) /* updateCoordSpeed (a two-point rail: the rate along its one segment) */
{
    float rate = kj.len > 0 ? (kj.dir > 0 ? kj.coord / kj.len : (kj.len - kj.coord) / kj.len) : 0.0f; /* calcRailRateToNextPoint */
    float v = kj.d0 <= 2 ? 2.0f : 4.0f;
    if (0.95f < rate) { kj.f184 = kj_ease(rate, v, 1.0f, 1.0f); return; }
    if (kj.r188 >= 0) { kj.f184 = kj_ease((float)kj.r188, v, 0.0f, 60.0f); kj.r188--; kj_lv(0, SE_BM_LV_KOOPAJR_SHIP_ACCEL); }
    kj.prop = kj_prop_speed();
}

/* --- KoopaJrShipCannonShell / KoopaJrShipCannonMainShell (CannonShellHolder: the first dead one) --- */
static void kj_shell_fx(const KjShell *o, const char *name, const void *key)
{
    float m[16];
    basis_matrix(m, o->pos, up_at(o->pos, mario->up), vlen(o->dir) > 1e-6f ? o->dir : v3(0, 0, 1), 0);
    fx_named("KoopaJrShipCannonShell", name, m, NULL, key);
}
static void kj_shell_explode(KjShell *o) /* explosion() */
{
    rumble_shake_weak(o->pos, RUMBLE_STRONG, RUMBLE_MIDDLE, 1000.0f, 2000.0f);
    kj_shell_fx(o, "Explosion", NULL);
    se_start(SE_EM_KILLER_EXPLOSION, &o->pos.x);
    o->state = 0;
}
static void kj_shoot_shell(int idx) /* shootShell: calcLauncherInfo (the cannon's X), launch at 15, ShootJ<n> */
{
    for (int i = 0; i < KJ_SHELLS; i++) {
        KjShell *o = &kj.sh[i];
        if (o->state) continue;
        float m[16];
        kj_jm(m, kj_cannon[idx]);
        o->pos = kj_col(m, 3); o->dir = vnorm(kj_col(m, 0)); o->vel = vmul(o->dir, 15.0f);
        o->t = -1; o->state = 1; o->smoke = 1;
        se_start(SE_BM_KOOPAJR_SHIP_SHOOT_NORMAL, &o->pos.x);
        break;
    }
    kj_fx(kj_shootfx[idx], NULL);
}
static void kj_shoot_mains(void) /* shootMainShells: FirePoint3's Z, then turned +15 / -15 about its Y, at 23 */
{
    float m[16];
    kj_jm(m, KJJ_MAIN);
    V3 p = kj_col(m, 3), y = vnorm(kj_col(m, 1)), z = vnorm(kj_col(m, 2));
    float ang[3] = {0.0f, 15.0f, -15.0f};
    for (int a = 0; a < 3; a++)
        for (int i = 0; i < KJ_MAINS; i++) {
            if (kj.ms[i].on) continue;
            kj.ms[i].on = 1; kj.ms[i].t = -1; kj.ms[i].pos = p;
            kj.ms[i].dir = vnorm(rot_axis(z, y, ang[a] * 0.017453292f)); kj.ms[i].vel = vmul(kj.ms[i].dir, 23.0f);
            break;
        }
}
static void kj_main_explode(int i) /* explosion(): its effect group is KoopaJrShipCannonShell's (initEffectKeeper) */
{
    KjShell o = {.pos = kj.ms[i].pos, .dir = kj.ms[i].dir};
    rumble_shake_weak(o.pos, RUMBLE_STRONG, RUMBLE_MIDDLE, 1000.0f, 2000.0f);
    kj_shell_fx(&o, "Explosion", NULL);
    se_start(SE_BM_KOOPAJR_SHIP_BREAK_FIRE, &o.pos.x);
    kj.ms[i].on = 0;
}
static void kj_kill_shells(void) { for (int i = 0; i < KJ_SHELLS; i++) kj.sh[i].state = 0; for (int i = 0; i < KJ_MAINS; i++) kj.ms[i].on = 0; } /* killActiveShells */
static void kj_update_shells(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < KJ_SHELLS; i++) {
        KjShell *o = &kj.sh[i];
        if (!o->state) continue;
        int t = ++o->t;
        V3 up = up_at(o->pos, mario->up), gn = up, wn;
        int wall = 0;
        if (o->state >= 2) {
            if (t == 0) { se_start(SE_EM_STOMPED_S, &o->pos.x); if (o->state == 3) { mario->hip = 0; mario->grounded = 0; mario->hvel = v3(0, 0, 0); mario->vup = 1.0f; } } /* jumpPlayer(-mGravity) */
            o->vel = vsub(o->vel, up); /* onCalcGravity, sGravity 1 */
        }
        V3 center = vadd(o->pos, o->vel);
        int ground = collide_n(&center, up, 75.0f, &gn, &wn, &wall); /* sBinderRadius x getBaseScale 1 */
        if (o->smoke) kj_shell_fx(o, "LocusSmoke", o);
        if (o->state == 1) { /* exeFly */
            o->lv = se_level(SE_EM_LV_KILLER_FLY, &o->pos.x, o->lv);
            if (t > 10 && (ground || wall)) { kj_shell_explode(o); continue; } /* sWallHitInvalidTime */
            o->pos = vadd(o->pos, o->vel);
            if (t == 360) { o->state = 0; continue; } /* sLifeTime */
        } else { /* exeDown */
            o->pos = center;
            float into = -vdot(o->vel, gn);
            if (ground && into > 2.0f) { o->vel = vadd(o->vel, vmul(gn, 1.55f * into)); o->smoke = 0; } /* reboundVelocityFromCollision(0.55, 2) */
            o->vel = vmul(o->vel, ground ? 0.95f : 0.99f);
            if (t == 45) { camera_shake(SHAKE_WEAK); pad_rumble(RUMBLE_WEAK); kj_shell_fx(o, "MisFire", NULL); se_start(SE_EM_KILLER_MISS_FIRE, &o->pos.x); o->state = 0; continue; } /* misfire */
        }
        if (vlen(vsub(c, o->pos)) > 75.0f + HIT_BODY_R) continue; /* its body (75) / attack (60) sensors on Mario's */
        if (o->state == 1 && mario->hip == HIP_FALL && vdot(vsub(mario->pos, o->pos), up) > 30.0f) { o->state = 3; o->t = -1; pad_rumble(RUMBLE_MIDDLE); } /* isMsgPlayerHipDrop */
        else if (o->state == 1 && mario->vup < 0 && vdot(vsub(mario->pos, o->pos), up) > 30.0f) { o->state = 2; o->t = -1; pad_rumble(RUMBLE_MIDDLE); mario_trample(); } /* isMsgPlayerTrample */
        else if (vlen(vsub(c, o->pos)) <= 60.0f + HIT_BODY_R) { mario_attack(ATK_EXPLOSION, o->pos); kj_shell_explode(o); } /* attackSensor: sendMsgEnemyAttackExplosion */
    }
    for (int i = 0; i < KJ_MAINS; i++) {
        if (!kj.ms[i].on) continue;
        int t = ++kj.ms[i].t;
        V3 p = vadd(kj.ms[i].pos, kj.ms[i].vel), up = up_at(p, mario->up), gn, wn;
        int wall = 0;
        V3 q = p;
        collide_n(&q, up, 75.0f * 1.2f, &gn, &wn, &wall);
        KjShell o = {.pos = p, .dir = kj.ms[i].dir};
        kj_shell_fx(&o, "LocusSmoke", &kj.ms[i]); kj_shell_fx(&o, "LocusFire", &kj.ms[i].lv);
        kj.ms[i].lv = se_level(SE_BM_LV_KOOPAJR_SHIP_FLY_FIRE, &p.x, kj.ms[i].lv);
        kj.ms[i].pos = p;
        if (t > 60 && wall) { kj_main_explode(i); continue; } /* sWallHitInvalidTime, isBindedWall */
        if (t == 300) { kj.ms[i].on = 0; continue; } /* sLifeTime */
        if (vlen(vsub(c, p)) <= 75.0f * 1.2f + HIT_BODY_R) { mario_attack(ATK_FIRERUN_STRONG, p); kj_main_explode(i); } /* sendMsgEnemyAttackFireStrong */
    }
}

/* --- the HomingKillers (initWithoutIter, setChaseStartEndDistance 5500 / 6500) --- */
static void kj_killer_info(int idx, V3 *p, V3 *f) /* calcLauncherInfoKiller: sKillerLauncherAngle all 0, 100 behind */
{
    float m[16];
    kj_jm(m, kj_cannon[idx]);
    *f = vnorm(kj_col(m, 0));
    *p = vadd(kj_col(m, 3), vmul(*f, -100.0f));
}
static void kj_killer_appear(int i) /* HomingKiller::appear(pos, front) */
{
    KjKiller *q = &kj.kl[i];
    kj_killer_info(i, &q->bpos, &q->bfront);
    q->k.pos = q->bpos; q->k.front = q->bfront; q->k.up = up_at(q->bpos, mario->up); q->k.rotz = 0;
    q->nrv = 1; q->t = -1; q->inv = 0; q->move = 0;
    kj_fx(kj_shootfx[i], NULL);
}
static int kj_killer_alive(void) { for (int i = 0; i < KJ_KILLERS; i++) if (kj.kl[i].nrv) return 1; return 0; } /* isExistActiveKiller */
static Kuribo *kj_kameck(int i) { return kj.kam[i] >= 0 ? &kuribo[kj.kam[i]] : NULL; }
static int kj_kameck_alive(void) { for (int i = 0; i < 2; i++) if (kj_kameck(i) && kj_kameck(i)->state != KURIBO_DEAD) return 1; return 0; }
static int kj_all_killers(void) /* tryShootAllKillers */
{
    if (kj_killer_alive() || kj_kameck_alive()) return 0;
    for (int i = 0; i < KJ_KILLERS; i++) kj_killer_appear(i);
    return 1;
}
static void kj_killer_break(KjKiller *q)
{
    float m[16];
    basis_matrix(m, q->k.pos, q->k.up, q->k.front, 0);
    fx_emit(FX_HOMINGKILLER_EXPLOSION, m);
    se_start(SE_EM_KILLER_EXPLOSION, &q->k.pos.x);
    rumble_shake_weak(q->k.pos, RUMBLE_STRONG, RUMBLE_MIDDLE, 1000.0f, 2000.0f);
    q->nrv = 0;
}
static void kj_update_killers(void)
{
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < KJ_KILLERS; i++) {
        KjKiller *q = &kj.kl[i];
        Killer *k = &q->k;
        if (!q->nrv) continue;
        int t = ++q->t;
        V3 g = vmul(up_at(k->pos, k->up), -1.0f);
        if (q->nrv == 1) { /* exeAppear (the base following its cannon: setBasePosAndFront) */
            kj_killer_info(i, &q->bpos, &q->bfront);
            int s = t < 20 ? t : 20;
            k->pos = vadd(q->bpos, vmul(q->bfront, s * 200.0f / 20.0f));
            if (t < 20) q->lv = se_level(SE_EM_LV_KILLER_STANDBY, &k->pos.x, q->lv);
            if (t == 20) se_start(SE_EM_KILLER_STANDBY_END, &k->pos.x);
            if (t > 20 && t < 42) { int u = t - 20; k->pos = vadd(k->pos, vmul(q->bfront, (22 - u) * sinf(fmodf(u * 100.0f, 360.0f) * 0.017453292f) * 18.0f / 22.0f)); }
            k->front = q->bfront; k->up = vmul(g, -1.0f);
            if (t == 72) { float m[16]; basis_matrix(m, k->pos, k->up, k->front, 0); fx_emit(FX_HOMINGKILLER_SHOOT, m); se_start(SE_EM_KILLER_FIRING, &k->pos.x); q->nrv = 2; q->t = -1; }
            continue;
        }
        int brk = 0;
        float d = vlen(vsub(mario->pos, k->pos));
        if (q->nrv == 2) { /* processMove, tryChaseStart (isChaseStart: 35 steps, within 5500, any angle) */
            if (++q->move > 600) brk = 1;
            V3 lvl = vnorm(vtangent(k->front, g));
            if (vlen(lvl) > 0.5f) k->front = turn_to(k->front, lvl, 0.9995f);
            k->up = vmul(g, -1.0f);
            if (++q->inv >= 35 && d < 5500.0f && d > 0) { q->nrv = 3; q->t = -1; se_start(SE_EM_KILLER_JET, &k->pos.x); }
        } else { /* processChase: out past 6500 breaks it */
            if (d > 6500.0f) brk = 1;
            V3 tg = killer_target(k, g, 200.0f);
            k->front = turn_to(k->front, tg, 0.9997f);
            killer_rotz(k, tg, g);
            k->up = turn_to(k->up, vmul(g, -1.0f), cosf(2.0f * 0.017453292f));
        }
        q->lv = se_level(SE_EM_LV_KILLER_FLY, &k->pos.x, q->lv);
        if (!brk) k->pos = vadd(k->pos, vmul(k->front, 12.0f)); /* cChaseSpeed */
        Hit h[1];
        if (!brk && vlen(vsub(mc, k->pos)) < 70.0f + HIT_BODY_R) { mario_attack(ATK_EXPLOSION, k->pos); brk = 1; }
        if (!brk && q->inv >= 35 && kcl_query(k->pos, 70.0f, h, 1)) brk = 1; /* onBind at cChaseInvalidFrame, tryBindedBreak */
        if (brk) kj_killer_break(q);
    }
}

/* --- KoopaJr (NPCActor "KoopaJr": its talk KoopaJrShipBattleLv1Tmp1Zone_KoopaJr000 on the ship's row) --- */
static void jr_play(const char *clip, int loop) { char n[40]; snprintf(n, sizeof n, "koopajr/%s", clip); if (kj.jranim && kj.jranim->skin) { if (loop) anim_play(kj.jranim, n, 1.0f, 1, 0); else { kj.jranim->clip_id = -1; anim_start(kj.jranim, n, 0); } } }
static int jr_done(void) { return !kj.jranim || !kj.jranim->skin || kj.jranim->done; } /* isBckStopped */
static void jr_talk_start(void) /* startShipBattleTalk: tryTalkTimeKeepDemo(mMsgCtrl), ShipBattleDemoTalkStart */
{
    if (kj.jr_talked) return;
    kj.jr_talked = 1;
    char lab[48];
    if (talk_row_label(kj.jr_row, lab)) { kj.talk = talk_new(lab, kj.jr_row, kj.jr_row, 150.0f, 0.0f); talk_place(kj.talk, kj.jrm); talk_force(kj.talk, 1); }
    else printf("koopajr: no talk\n");
    jr_set(JR_TALKSTART);
}
static void jr_update(void)
{
    if (!kj.jr_on) return;
    int s = ++kj.jr_st;
    switch (kj.jr_nrv) {
    case JR_WAIT: jr_play(kj.jr_angry ? "waitangry" : "wait", 1); break; /* mParam.setSingleAction(Wait / WaitAngry) */
    case JR_APPEAR: if (s == 0) jr_play(!strncmp(game_stage_name, "TriLegLv1Galaxy/", 16) ? "roboappeardemo" : "appeardemo", 0); /* isEqualStageName */ else if (jr_done()) jr_set(JR_WAIT); break;
    case JR_POWERUP: if (s == 0) jr_play("angrydemo", 0); else if (jr_done()) { kj.jr_angry = 1; jr_set(JR_WAIT); } break;
    case JR_ESCAPE: if (s == 0) jr_play("runawaydemo", 0); else if (jr_done()) jr_set(JR_WAIT); break;
    case JR_SHIPDAMAGE: if (s == 0) jr_play("stumble", 0); else if (jr_done()) jr_set(JR_WAIT); break;
    case JR_TALKSTART: if (s == 0) jr_play("talk", 0); else if (jr_done()) jr_set(JR_TALKWAIT); break;
    case JR_TALKWAIT: if (s == 0) jr_play("talkwait", 1); break;
    }
    if (kj.jranim && kj.jranim->skin) anim_update(kj.jranim);
}
static void kj_jr_pos(void) /* updateKoopaJrPos (Break: the pod's KoopaJrShipPod joint), Koopa Jr. turned to Mario (control: turnToPlayer) */
{
    float m[16];
    if (kj.nrv == KJ_BREAK && kj.panim && kj.panim->skin) mat_mul(m, kj.podm, kj.panim->world[0]);
    else {
        kj_jm(m, KJJ_ROOT);
        V3 y = vnorm(kj_col(m, 1)), z = vnorm(kj_col(m, 2)), to = vnorm(vtangent(vsub(mario->pos, kj_col(m, 3)), y));
        if (vlen(to) > 0.5f) { z = turn_to(z, to, cosf(5.0f * 0.017453292f)); basis_matrix(m, kj_col(m, 3), y, z, 0); } /* faceToPoint(.., 5) */
    }
    V3 o = kj.jroff, x = kj_col(m, 0), y = kj_col(m, 1), z = kj_col(m, 2);
    V3 p = vadd(kj_col(m, 3), vadd(vmul(x, o.x), vadd(vmul(y, o.y), vmul(z, o.z))));
    V3 up = vnorm(y), f = vnorm(vtangent(vsub(mario->pos, p), up)), f0 = vnorm(vtangent(kj_col(kj.jrm, 2), up));
    if (vlen(f0) < 0.5f) f0 = vnorm(z);
    if (vlen(f) > 0.5f) f0 = turn_to(f0, f, cosf(5.0f * 0.017453292f));
    basis_matrix(kj.jrm, p, up, f0, 0);
    if (kj.talk >= 0) talk_place(kj.talk, kj.jrm);
}

/* --- the ship --- */
static void kj_kill_subs(void) { kj.jr_on = 0; kj.pod_on = 0; kj.break_on = 0; } /* killAllSubModels */
static void kj_kameck_appear(int i) /* Kameck::appear: its rail's start, Appear */
{
    Kuribo *k = kj_kameck(i);
    if (!k) return;
    u32 n = 0; int cl = 0;
    const float *r = w_rail(k, &n, &cl);
    k->state = KURIBO_WALK; k->lva[0] = 0;
    if (r) k->pos = km_rail_at(r, n > 16 ? 16 : n, 0);
    w_nrv(k, KM_APPEAR);
    printf("koopajr ship: kameck %d out\n", i);
}
static void kj_turn_front(void) /* setStateTurnFront (the 旋廻 functor) */
{
    kj.pos = kj.p1f8; /* moveCoordAndTransToNearestRailPos */
    float best = 1e30f;
    for (float c = 0; c <= kj.len; c += 5.0f) { float d = vlen(vsub(km_rail_at(kj.rail, kj.nrail, c), kj.pos)); if (d < best) { best = d; kj.coord = c; } }
    if (kj.rail) kj.pos = km_rail_at(kj.rail, kj.nrail, kj.coord);
    jr_set(JR_POWERUP);
    kj.jroff = v3(0.0f, 188.0f, 135.0f); /* sKoopaJrPosFront */
    kj_set(KJ_TURNFRONT);
}
static void kj_cast_hook(int row, int type, int part) /* the sheet's actions on it: 出現 appear (0), 旋廻 / クッパJr会話 functors */
{
    if (row != kj.row || kj.row < 0) return;
    if (type == 0) { kj_set(KJ_APPEAR); return; } /* LiveActor::appear (its nerve Appear from init) */
    if (type == 2 && part == 2) kj_turn_front();
    if (type == 2 && part == 1) jr_talk_start(); /* (Koopa Jr. is its cast クッパJr: slice.py DEMO_CAST_ALIAS) */
}
static void kj_damage(V3 p, V3 v) /* receiveMsgJetTurtleAttack */
{
    if (kj.nrv == KJ_DAMAGE || kj.nrv == KJ_POWERUP || kj_break()) return; /* (the shell taken, no damage) */
    kj.d4 = p; kj.e0 = vlen(v) > 1e-6f ? vnorm(v) : v3(0, 0, 1);
    if (--kj.d0 <= 0) { kj_se(SE_BM_KOOPAJR_SHIP_DAMAGE_L); se_start(SE_BV_KOOPAJR_DAMAGE_L, &kj.jrm[12]); se_start(SE_SY_VS_BOSS_LAST_HIT, NULL); kj_set(KJ_BREAKSTART); }
    else { kj_se(SE_BM_KOOPAJR_SHIP_DAMAGE); se_start(SE_BV_KOOPAJR_DAMAGE_S, &kj.jrm[12]); se_start(SE_SY_VS_BOSS_DAMAGE_1, NULL); kj_set(KJ_DAMAGE); }
}
static int kj_jet_hit(V3 p) /* receiveMsgPlayerAttack isMsgJetTurtleAttack on its collision's message sensor */
{
    if (kj.row < 0 || kj.nrv == KJ_DEAD || kj.nrv == KJ_DONE || kj.hidden || kj.inst < 0) return 0;
    Hit h[8];
    int n = kcl_query(p, 60.0f, h, 8), hit = 0;
    for (int i = 0; i < n; i++) if (h[i].inst == kj.inst) hit = 1;
    if (!hit) return 0;
    kj_damage(p, jet.vel);
    return 1;
}
static void kj_hit_fx(void) /* emitDamageHitEffect: makeMtxSideUpPos(-_E0, up, _D4), DamageFire */
{
    float m[16];
    V3 up = vnorm(kj_col(kj.base, 1)), x = vnorm(vmul(kj.e0, -1.0f)), z = vnorm(vcross(x, up)), y = vcross(z, x);
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z; m[4] = y.x; m[5] = y.y; m[6] = y.z; m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = kj.d4.x; m[13] = kj.d4.y; m[14] = kj.d4.z;
    fx_named("KoopaJrShip", "DamageFire", m, NULL, NULL);
    kj_se(SE_BM_KOOPAJR_SHIP_IGNIT);
}
static void kj_play(const char *clip, float rate) { char n[40]; snprintf(n, sizeof n, "koopajrship/%s", clip); if (kj.anim && kj.anim->skin) { kj.anim->clip_id = -1; anim_start(kj.anim, n, 0); kj.anim->speed = rate; } }
static void kj_kill(void) /* kill: requestAppearPowerStar(this, mPosition) */
{
    note_star_row = kj.row; note_star_at = kj.pos; row_kill(kj.row);
    if (kj.inst >= 0 && kj.inst < ninsts) insts[kj.inst].kcl = NULL;
    kj_set(KJ_DONE);
    printf("koopajr ship: down, its star\n");
}
static void kj_nerve(void)
{
    int s = ++kj.st;
    switch (kj.nrv) {
    case KJ_APPEAR:
        if (s == 0) { kj_se(SE_BM_KOOPAJR_SHIP_ENTER); kj_play("arrival", 1.0f); kj.hidden = 1; }
        if (s == 1) { kj.hidden = 0; kj.jr_on = 1; jr_set(JR_APPEAR); if (kj.inst >= 0) insts[kj.inst].kcl = kj.kcl; }
        if (kj_part_last(1)) { jr_set(JR_WAIT); kj.r188 = 60; bgm_start("MBGM_BOSS_06_A"); kj_set(KJ_MOVE); } /* endShipBattleTalk */
        break;
    case KJ_MOVE:
        if (s == 0) kj.rspeed = kj.d0 <= 2 ? 2.0f : 4.0f;
        kj_coord_speed();
        kj_rail_move();
        if (s > 0 && s % 120 == 0 && !kj_killer_alive() && kj.d0 >= 2 && kj.d0 <= 5) kj_set(kj.d0 == 5 ? KJ_SHOOT1 : KJ_SHOOT2);
        if (kj_edge()) kj_set(KJ_STOPEND);
        break;
    case KJ_FRONT: /* MoveFrontAttack */
        if (s == 0) { kj.rspeed = kj.d0 <= 2 ? 2.0f : 4.0f; kj.sign = 1; } /* HeadShootSign */
        kj_lv(1, SE_BM_LV_KOOPAJR_SHIP_MAIN_PREP);
        kj_coord_speed();
        kj_rail_move();
        if (kj_goal()) { if (s > 120) { kj.dir = -kj.dir; kj_set(KJ_SHOOTMAIN); } }
        else if (s == 120) kj_set(KJ_SHOOTMAIN);
        break;
    case KJ_SHOOT1: case KJ_SHOOT2: {
        kj_rail_move();
        kj_coord_speed();
        int n = kj.nrv == KJ_SHOOT1 ? 1 : 5;
        for (int i = 0; i < n; i++)
            if (s == 45 * i) { kj_shoot_shell(1); if (i == n - 1) { kj_set(KJ_MOVE); return; } }
        if (kj_edge()) kj_set(KJ_STOPEND);
        break;
    }
    case KJ_SHOOTMAIN:
        if (s == 0) { kj.sign = 0; kj_fx("HeadShoot", NULL); kj_se(SE_BM_KOOPAJR_SHIP_SHOOT_MAIN); camera_shake(SHAKE_NORMAL); if (kj.d0 <= 1) kj_all_killers(); }
        if (s == 15) kj_shoot_mains();
        kj_rail_move();
        kj_coord_speed();
        if (s == 150) kj_set(KJ_FRONT);
        break;
    case KJ_STOPEND:
        if (s == (kj.d0 <= 2 ? 90 : 150)) { kj.dir = -kj.dir; kj.r188 = 60; kj_set(kj.d0 <= 2 ? KJ_FRONT : KJ_MOVE); }
        break;
    case KJ_POWERUP: if (s == 90) { kj.r188 = 60; kj_set(KJ_MOVE); } break;
    case KJ_DAMAGE:
        if (s == 0) {
            kj.f184 = 0.0f; kj.sign = 0;
            kj_play("damage", 1.0f); jr_set(JR_SHIPDAMAGE);
            if (kj.d0 == 3) kj.smoke1 = 1; else if (kj.d0 == 1) kj.smoke2 = 1; /* emitDamageSmokeEffect */
            kj_hit_fx(); camera_shake(SHAKE_NORMAL); pad_rumble(RUMBLE_STRONG); scene_stop = 12;
        }
        if (s == 90) {
            if (kj.d0 == 2) { int k = kj_demo_k(); if (k >= 0) dx_request_at(k, 2); printf("koopajr ship: 旋廻 demo\n"); return; } /* requestStartDemoRegistered 旋廻 */
            if (kj.d0 <= 2) kj_all_killers(); else { for (int i = 0; i < 3; i++) if (!kj.kl[i].nrv) kj_killer_appear(i); } /* shootKillersAfterDamage */
            if (kj.d0 == 3) kj_kameck_appear(0);
            if (kj.d0 == 1) kj_kameck_appear(1);
            kj.r188 = 60;
            kj_set(kj.d0 <= 2 ? KJ_FRONT : KJ_MOVE);
        }
        break;
    case KJ_BREAKSTART:
        if (s == 0) { kj.sign = 0; kj_play("damage", 1.0f); jr_set(JR_SHIPDAMAGE); kj_hit_fx(); camera_shake(SHAKE_NORMAL); pad_rumble(RUMBLE_STRONG); scene_stop = 12; }
        if (s == 60) {
            kj_kill_shells();
            for (int i = 0; i < 2; i++) if (kj_kameck(i) && kj_kameck(i)->state != KURIBO_DEAD) km_dead_force(kj_kameck(i)); /* makeActorDeadForce */
            for (int i = 0; i < KJ_KILLERS; i++) if (kj.kl[i].nrv) kj_killer_break(&kj.kl[i]); /* kill */
            int k = kj_demo_k(); if (k >= 0) dx_request_at(k, 3); /* requestStartDemoRegistered 破壊 */
            memcpy(kj.shipm, kj.base, 64); kj.break_on = 1; /* mShipBreakModel appear, Break */
            if (kj.banim && kj.banim->skin) { kj.banim->clip_id = -1; anim_start(kj.banim, "koopajrshipbreak/break", 0); }
            memcpy(kj.podm, kj.base, 64); kj.pod_on = 1; kj.pod_hidden = 0; /* mPodModel appear, Escape */
            if (kj.panim && kj.panim->skin) { kj.panim->clip_id = -1; anim_start(kj.panim, "koopajrshippod/escape", 0); }
            kj_set(KJ_BREAK);
        }
        break;
    case KJ_BREAK:
        if (s == 0) {
            kj.eye = 0; pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL); kj_se(SE_BM_KOOPAJR_SHIP_BREAK_S);
            jr_set(JR_ESCAPE); kj.hidden = 1; kj.smoke1 = kj.smoke2 = 0;
            if (kj.inst >= 0) insts[kj.inst].kcl = NULL; /* (hideModel: its collision with it) */
        }
        if (kj.pod_on && !kj.pod_hidden && (!kj.panim || !kj.panim->skin || kj.panim->done)) { kj.pod_hidden = 1; kj.jr_hidden = 1; }
        if (s == 287) kj_se(SE_BM_KOOPAJR_SHIP_BREAK_S);
        if (s < 300) { kj_lv(2, SE_BM_LV_KOOPAJR_SHIP_BURN1); kj_lv(3, SE_BM_LV_KOOPAJR_SHIP_BURN2); }
        if (s == 300) { camera_shake(SHAKE_NORMAL); pad_rumble(RUMBLE_STRONG); kj_se(SE_BM_KOOPAJR_SHIP_BREAK_L); }
        if (kj.break_on && ((kj.banim && kj.banim->done) || s == 320)) kj.break_on = 0;
        if (kj_demo_last() || (s > 600 && !dx.on)) { kj_kill_subs(); kj_set(KJ_BREAKEND); }
        break;
    case KJ_BREAKEND: if (s == 0) { bgm_after_boss(); kj_kill(); } break;
    case KJ_TURNFRONT:
        if (s == 0) {
            bgm_stop(30); kj_kill_shells();
            kj.ac = kj_kameck(0) && kj_kameck(0)->state != KURIBO_DEAD; /* _AC.push_back(mKamecks[0]) */
            if (kj_kameck(0) && kj_kameck(0)->state != KURIBO_DEAD) km_dead_force(kj_kameck(0));
            for (int i = 0; i < KJ_KILLERS; i++) kj.kl[i].nrv = 0; /* makeActorDead */
        }
        kj_lv(4, SE_BM_LV_KOOPAJR_SHIP_3RD_DEMO);
        if (s <= 150) kj.roty += 0.6f;
        if (s == 150) { kj.eye = 1; kj_se(SE_BM_KOOPAJR_SHIP_EYE_BLINK); }
        if (kj_part_last(2)) { kj.r188 = 60; bgm_start("MBGM_BOSS_06_B"); if (kj.ac) kj_kameck_appear(0); kj_set(KJ_FRONT); } /* (_AC[2]: its count) */
        break;
    }
}
static void kj_find_kamecks(void) /* initKameck: its two child Kamecks (createFireBallBeamKameck, initChildObj 0 / 1: its zone's ChildKameck rows) */
{
    int nk = 0;
    for (int i = 0; i < nkuribo && nk < 2; i++)
        if (kuribo[i].kind == WALK_KAMECK && sc->objs[kuribo[i].row].zone == sc->objs[kj.row].zone && km_child(&kuribo[i])) kj.kam[nk++] = i;
}
static void update_kj(void)
{
    if (kj.row >= 0 && kj.kam[0] < 0) kj_find_kamecks();
    if (kj.row >= 0 && kj.demo == -2) { /* tryRegisterDemoCast: Appear, makeActorDead (no demo: Move, makeActorAppeared) */
        kj.demo = -1;
        for (int d = 0; d < ndemos; d++) for (int c = 0; c < demos[d].h->ngroup; c++) if (demos[d].group[c] == kj.row) kj.demo = d;
        if (kj.demo < 0) { kj_set(KJ_MOVE); kj.jr_on = 1; jr_set(JR_WAIT); }
        printf("koopajr ship: demo %d\n", kj.demo);
    }
    if (kj.row < 0 || kj.nrv == KJ_DEAD || kj.nrv == KJ_DONE) { if (kj.row >= 0) kj_update_shells(), kj_update_killers(); return; }
    if (kj.inst < 0 || kj.inst >= ninsts || insts[kj.inst].row != kj.row) { kj.inst = -1; for (int i = 0; i < ninsts; i++) if (insts[i].row == kj.row && insts[i].kcl) { kj.inst = i; kj.kcl = insts[i].kcl; } }
    if (scene_stop > 0) return;
    kj_nerve();
    /* control */
    kj.ec = fmodf(kj.ec + kj.prop, 360.0f);
    kj.rspeed = kj.f184; /* setRailCoordSpeed(_184) */
    if (kj.d0 > 0) {
        if (kj.d0 <= 3) kj_lv(5, SE_BM_LV_KOOPAJR_SHIP_BURN1);
        if (kj.d0 <= 1) kj_lv(6, SE_BM_LV_KOOPAJR_SHIP_BURN2);
        kj_lv(7, SE_BM_LV_KOOPAJR_SHIP_MOVE);
    }
    kj_base();
    if (kj.anim && kj.anim->skin) { /* the pose, the joint transforms (setJointTransformLocalMtx) on top */
        anim_update(kj.anim);
        float a = kj.ec * 0.017453292f, r[16], t[16];
        int js[3] = {KJJ_PROP, KJJ_SCREW0, KJJ_SCREW1};
        for (int k = 0; k < 3; k++) {
            if ((u32)js[k] >= kj.anim->skin->njoints) continue;
            mat_identity(r);
            if (k == 0) { r[0] = cosf(a); r[2] = -sinf(a); r[8] = sinf(a); r[10] = cosf(a); } /* setRotateY */
            else { r[0] = cosf(a); r[1] = sinf(a); r[4] = -sinf(a); r[5] = cosf(a); }        /* setEulerZ */
            mat_mul(t, kj.anim->world[js[k]], r);
            memcpy(kj.anim->world[js[k]], t, 64);
        }
        kj.anim->skin_stale = 1;
    }
    if (kj.break_on && kj.banim && kj.banim->skin) anim_update(kj.banim);
    if (kj.pod_on && kj.panim && kj.panim->skin) anim_update(kj.panim);
    jr_update();
    kj_jr_pos();
    if (!kj.hidden) { /* its looping effects while on */
        if (kj.smoke1) kj_fx("DamageSmoke1", &kj.smoke1);
        if (kj.smoke2) kj_fx("DamageSmoke2", &kj.smoke2);
        if (kj.eye) kj_fx("EyeLight", &kj.eye);
        if (kj.sign) kj_fx("HeadShootSign", &kj.sign);
    }
    kj_update_shells();
    kj_update_killers();
}
static void kj_draw(void)
{
    if (kj.row < 0) return;
    float m[16];
    if (kj.nrv != KJ_DEAD && kj.nrv != KJ_DONE && !kj.hidden && kj.md) { if (kj.anim && kj.anim->skin) draw_skin(kj.md, kj.base, kj.anim); else render_model(kj.md, kj.base, 0x80, 0x80, 0x80); }
    if (kj.break_on && kj.mbreak) { if (kj.banim && kj.banim->skin) draw_skin(kj.mbreak, kj.shipm, kj.banim); else render_model(kj.mbreak, kj.shipm, 0x80, 0x80, 0x80); }
    if (kj.pod_on && !kj.pod_hidden && kj.mpod) { if (kj.panim && kj.panim->skin) draw_skin(kj.mpod, kj.podm, kj.panim); else render_model(kj.mpod, kj.podm, 0x80, 0x80, 0x80); }
    if (kj.jr_on && !kj.jr_hidden && kj.mjr) { if (kj.jranim && kj.jranim->skin) draw_skin(kj.mjr, kj.jrm, kj.jranim); else render_model(kj.mjr, kj.jrm, 0x80, 0x80, 0x80); }
    for (int i = 0; kj.mshell && i < KJ_SHELLS; i++) if (kj.sh[i].state) { basis_matrix(m, kj.sh[i].pos, up_at(kj.sh[i].pos, v3(0, 1, 0)), kj.sh[i].dir, 0); render_model(kj.mshell, m, 0x80, 0x80, 0x80); }
    for (int i = 0; kj.mmain && i < KJ_MAINS; i++) if (kj.ms[i].on) {
        basis_matrix(m, kj.ms[i].pos, up_at(kj.ms[i].pos, v3(0, 1, 0)), kj.ms[i].dir, 0);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) m[j] *= 1.2f; /* sTmpScale */
        render_model(kj.mmain, m, 0x80, 0x80, 0x80);
    }
    for (int i = 0; kj.mkiller && i < KJ_KILLERS; i++) if (kj.kl[i].nrv) { const Killer *k = &kj.kl[i].k; basis_matrix(m, k->pos, k->up, k->front, k->rotz * 0.017453292f); render_model(kj.mkiller, m, 0x80, 0x80, 0x80); }
}
static void kj_init(const Scene *s, const unsigned char *blob) /* KoopaJrShip::init, initAfterPlacement */
{
    memset(&kj, 0, sizeof kj); kj.row = -1; kj.inst = -1; kj.talk = -1; kj.kam[0] = kj.kam[1] = -1;
#ifdef KJ_OFF
    return;
#endif
    int t = scene_type(s, "KoopaJrShip");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        int cl = 0;
        kj.row = kj.jr_row = (int)i; kj.d0 = 5; kj.prop = 20.0f; kj.dir = 1; kj.e0 = v3(0, 0, 1); kj.jroff = v3(135.0f, 188.0f, 0.0f); /* sKoopaJrPos */
        kj.roty = r->rot[1]; kj.pos = row_pos(r);
        kj.rail = find_rail_of(blob, r->zone, r->path, &kj.nrail, &cl, 0);
        if (kj.rail && kj.nrail > 16) kj.nrail = 16;
        float cum[16];
        if (kj.rail) { km_rail_cum(kj.rail, kj.nrail, cum); kj.len = cum[kj.nrail - 1]; }
        float best = 1e30f; /* moveCoordAndTransToNearestRailPos */
        for (float c = 0; kj.rail && c <= kj.len; c += 5.0f) { float d = vlen(vsub(km_rail_at(kj.rail, kj.nrail, c), kj.pos)); if (d < best) { best = d; kj.coord = c; } }
        if (kj.rail) kj.pos = km_rail_at(kj.rail, kj.nrail, kj.coord);
        kj.p1f8 = kj.pos;
        kj.md = scene_model(s, "KoopaJrShip"); kj.mbreak = scene_model(s, "KoopaJrShipBreak"); kj.mpod = scene_model(s, "KoopaJrShipPod");
        kj.mjr = scene_model(s, "KoopaJr"); kj.mshell = scene_model(s, "KoopaJrShipCannonShell"); kj.mmain = scene_model(s, "KoopaJrShipCannonMainShell");
        kj.mkiller = scene_model(s, "HomingKiller");
        Anim **an[4] = {&kj.anim, &kj.banim, &kj.panim, &kj.jranim};
        const Model *mm[4] = {kj.md, kj.mbreak, kj.mpod, kj.mjr};
        for (int k = 0; k < 4; k++) { *an[k] = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(*an[k], 0, sizeof(Anim)); if (mm[k] && mm[k]->skin) anim_bind(*an[k], mm[k]->skin); }
        if (kj.anim->skin) { anim_play(kj.anim, "koopajrship/koopajrship", 0.0f, 0, 0); } /* startBck KoopaJrShip, setBckRate 0 */
        kj.demo = -2; kj.nrv = KJ_DEAD; /* (its demo looked up at the first update: dx_init comes after) */
        printf("koopajr ship: row %d, rail %u points (%.0f)\n", kj.row, kj.nrail, kj.len);
        break;
    }
    if (kj.row < 0) return;
}
static void kj_rebind_place(Inst *in) /* game_rebind_: its collision instance at the ship (dead: none), not drawn */
{
    if (kj.row < 0 || in->row != kj.row) return;
    if (in->kcl) kj.kcl = in->kcl;
    in->invisible = 1;
    if (kj.nrv == KJ_DEAD || kj.nrv == KJ_DONE || kj.hidden) in->kcl = NULL;
    if (kj.nrv != KJ_DEAD) memcpy(in->m, kj.base, 64);
}
int game_test_kj(int frame) /* XF=-DKJ_TEST, KoopaJrShipLv1Galaxy/1: Mario put at the deck's SwitchCube (SW 15: the
                             * demo), then the ship hit by a JetTurtle every 300 frames it can take one; till its star */
{
    if (kj.row < 0) return -1;
    if (frame % 60 == 0)
        printf("kj test f %d: nrv %d st %d d0 %d coord %.0f/%.0f pos %.0f %.0f %.0f, Mario %.0f %.0f %.0f, life %d, dx %d part %d\n", frame, kj.nrv, kj.st, kj.d0,
               kj.coord, kj.len, kj.pos.x, kj.pos.y, kj.pos.z, mario->pos.x, mario->pos.y, mario->pos.z, game_life(), dx.on, dx.part);
    if (life < 2) life = 3;
    if (talk_active() && (frame & 3) == 0) pad_force |= PAD_CROSS; /* (A through Koopa Jr.'s talk) */
    static int hit_at = -1000, done_at = -1;
    if (kj.nrv == KJ_DEAD) {
        if (frame < 60) return 0;
        int ty = scene_type(sc, "SwitchCube");
        for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].sw[SW_A] == 15 && frame % 30 == 0) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 0;
    }
    if (kj.nrv == KJ_DONE) {
        if (done_at < 0) done_at = frame;
        for (int i = 0; frame - done_at >= 300 && frame % 20 == 0 && i < ninsts; i++)
            if (insts[i].item == ITEM_STAR && star_wait[insts[i].row] == kj.row + 1) { int st; V3 p = star_pos(&insts[i], &st); if (st) { mario->pos = p; mario->hvel = v3(0, 0, 0); mario->vup = 0; } }
        return 100;
    }
    if (!dx.on && frame - hit_at > 300 && (kj.nrv == KJ_MOVE || kj.nrv == KJ_FRONT || kj.nrv == KJ_SHOOT1 || kj.nrv == KJ_SHOOT2 || kj.nrv == KJ_STOPEND || kj.nrv == KJ_SHOOTMAIN)) {
        hit_at = frame; /* (a JetTurtle thrown at it from Mario: its hit as the turtle's message, test only) */
        float m[16];
        kj_jm(m, KJJ_ROOT);
        int d0 = kj.d0;
        kj_damage(kj_col(m, 3), vsub(kj_col(m, 3), mario->pos));
        printf("kj test f %d: hit, d0 %d -> %d\n", frame, d0, kj.d0);
    }
    return 0;
}

/* DemoKoopaJrShip.cpp after Petari: Bowser Jr.'s ship in the boss galaxies' openings (FloaterOtaKingGalaxy arg0 1,
 * TriLegLv1Galaxy arg0 0: KoopaJrEntry / LeaveOtaKingDemo, ..TriPodDemo, their anim cameras on it), a cast of
 * クッパＪｒ．デモ (SW 67). Appear (the sheet's appear: Koopa Jr. AppearDemo / RoboAppearDemo, the entry clip and camera,
 * stopStageBGM(60), SE_BM_KOOPAJR_SHIP_ENTER, BGM_KOOPA_JR_APPEAR at 309), at the clip's end Talk (startShipBattleTalk,
 * Wait) until the time keeper resumes, FlyAway (SE_BV_KOOPAJR_LAUGH_LEAVE, stopSubBGM(120), the leave clip and
 * camera) to the part's last step: both killed. Koopa Jr. at (135, 188, 0) on its obj joint turned 5 degrees to Mario.
 * ponytail: arg0 -1's reversed Arrival (no placement uses it) */
enum { DKJ_DEAD, DKJ_APPEAR, DKJ_TALK, DKJ_FLYAWAY };
static struct { int row, nrv, st, demo, cam, hidden; float base[16]; Anim *anim; const Model *md; u32 lv; char clip[32]; } dkj = {.row = -1};
static const char *const dkj_anims[2][2] = {{"KoopaJrEntryTriPodDemo", "KoopaJrLeaveTriPodDemo"}, {"KoopaJrEntryOtaKingDemo", "KoopaJrLeaveOtaKingDemo"}};
static void dkj_set(int n) { dkj.nrv = n; dkj.st = -1; printf("demo koopajr ship: nerve %d\n", n); }
static void dkj_bck(const char *name) /* startBck (and startAnimCameraTargetSelf with the same name) */
{
    snprintf(dkj.clip, sizeof dkj.clip, "demokoopajrship/%s", name); /* (cut at 31 as the packs' names) */
    for (char *c = dkj.clip; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (dkj.anim && dkj.anim->skin) { dkj.anim->clip_id = -1; anim_start(dkj.anim, dkj.clip, 0); }
}
static void dkj_cam(int on) /* the clip's anim camera, held on its last frame; endAnimCamera */
{
    if (!on) { camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45); return; }
    int cf = canm_frames(dkj.clip);
    if (cf && dkj.anim) demo_camera(dkj.clip, (int)dkj.anim->frame < cf ? (int)dkj.anim->frame : cf - 1, dkj.base);
}
static void dkj_cast_hook(int row, int type, int part)
{
    (void)part;
    if (row != dkj.row || type != 0) return;
    dkj_set(DKJ_APPEAR); /* appear: Appear, Koopa Jr. at sKoopaJrPos */
}
static int dkj_last(void) { return dx.on && dx.cur == dkj.demo && dx_last(0); } /* tryDemoEnd: クッパＪｒ．デモ's last step */
static void update_dkj(void)
{
    if (dkj.row < 0) return;
    if (dkj.demo == -2) { dkj.demo = -1; for (int d = 0; d < ndemos; d++) for (int c = 0; c < demos[d].h->ngroup; c++) if (demos[d].group[c] == dkj.row) dkj.demo = d; }
    if (dkj.nrv == DKJ_DEAD) return;
    int s = ++dkj.st;
    switch (dkj.nrv) {
    case DKJ_APPEAR:
        if (s == 0) {
            kj.jr_on = 1; kj.jr_hidden = 0; kj.jr_talked = 0; jr_set(JR_APPEAR);
            if (dkj.cam >= 0) dkj_bck(dkj_anims[dkj.cam][0]); else dkj_bck("Arrival"); /* startEntryAnim */
            bgm_stop(60); se_start(SE_BM_KOOPAJR_SHIP_ENTER, &dkj.base[12]); dkj.hidden = 1;
        }
        if (s >= 1 && dkj.hidden) dkj.hidden = 0;
        if (s == 309) bgm_sub("BGM_KOOPA_JR_APPEAR"); /* sBgmStartStep */
        dkj.lv = se_level(SE_BM_LV_KOOPAJR_SHIP_MOVE, &dkj.base[12], dkj.lv);
        if (s > 0 && (!dkj.anim || !dkj.anim->skin || dkj.anim->done)) dkj_set(DKJ_TALK);
        break;
    case DKJ_TALK:
        if (s == 0) { jr_talk_start(); dkj_bck("Wait"); }
        dkj.lv = se_level(SE_BM_LV_KOOPAJR_SHIP_MOVE, &dkj.base[12], dkj.lv);
        if (s > 0 && !dx.paused) dkj_set(DKJ_FLYAWAY); /* !isPauseTimeKeepDemo */
        break;
    case DKJ_FLYAWAY:
        if (s == 0) { se_start(SE_BV_KOOPAJR_LAUGH_LEAVE, &kj.jrm[12]); bgm_sub_stop(120); if (dkj.cam >= 0) dkj_bck(dkj_anims[dkj.cam][1]); }
        dkj.lv = se_level(SE_BM_LV_KOOPAJR_SHIP_MOVE, &dkj.base[12], dkj.lv);
        if (dkj_last()) { if (dkj.cam >= 0) dkj_cam(0); kj.jr_on = 0; dkj_set(DKJ_DEAD); printf("demo koopajr ship: gone\n"); return; }
        break;
    }
    if (dkj.anim && dkj.anim->skin) anim_update(dkj.anim);
    if (dkj.cam >= 0 && dkj.nrv != DKJ_TALK) dkj_cam(1);
    /* control: Koopa Jr. on its obj joint (11) faced to Mario */
    float m[16];
    if (dkj.anim && dkj.anim->skin && dkj.anim->skin->njoints > 11) mat_mul(m, dkj.base, dkj.anim->world[11]); else memcpy(m, dkj.base, 64);
    V3 y = vnorm(kj_col(m, 1)), z = vnorm(kj_col(m, 2)), to = vnorm(vtangent(vsub(mario->pos, kj_col(m, 3)), y));
    if (vlen(to) > 0.5f) { z = turn_to(z, to, cosf(5.0f * 0.017453292f)); basis_matrix(m, kj_col(m, 3), y, z, 0); }
    V3 x = kj_col(m, 0), p = vadd(kj_col(m, 3), vadd(vmul(x, 135.0f), vmul(y, 188.0f)));
    V3 f = vnorm(vtangent(vsub(mario->pos, p), y)), f0 = vnorm(vtangent(kj_col(kj.jrm, 2), y));
    if (vlen(f0) < 0.5f) f0 = z;
    if (vlen(f) > 0.5f) f0 = turn_to(f0, f, cosf(5.0f * 0.017453292f));
    basis_matrix(kj.jrm, p, y, f0, 0);
    if (kj.talk >= 0) talk_place(kj.talk, kj.jrm);
    jr_update();
}
static void dkj_draw(void)
{
    if (dkj.row < 0 || dkj.nrv == DKJ_DEAD) return;
    if (!dkj.hidden && dkj.md) { if (dkj.anim && dkj.anim->skin) draw_skin(dkj.md, dkj.base, dkj.anim); else render_model(dkj.md, dkj.base, 0x80, 0x80, 0x80); }
    if (kj.jr_on && kj.mjr) { if (kj.jranim && kj.jranim->skin) draw_skin(kj.mjr, kj.jrm, kj.jranim); else render_model(kj.mjr, kj.jrm, 0x80, 0x80, 0x80); }
}
static void dkj_init(const Scene *s) /* DemoKoopaJrShip::init (after kj_init) */
{
    memset(&dkj, 0, sizeof dkj); dkj.row = -1;
    int t = scene_type(s, "DemoKoopaJrShip");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        float one[3] = {1, 1, 1};
        dkj.row = kj.jr_row = (int)i; dkj.demo = -2; dkj.nrv = DKJ_DEAD; /* makeActorDead */
        dkj.cam = r->arg[0] >= 0 && r->arg[0] < 2 ? r->arg[0] : -1; /* initAnimID */
        mat_translate_rot_scale(dkj.base, r->pos, r->rot, one); /* initDefaultPos */
        dkj.md = scene_model(s, "DemoKoopaJrShip"); kj.mjr = scene_model(s, "KoopaJr");
        dkj.anim = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(dkj.anim, 0, sizeof(Anim)); if (dkj.md && dkj.md->skin) anim_bind(dkj.anim, dkj.md->skin);
        kj.jranim = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(kj.jranim, 0, sizeof(Anim)); if (kj.mjr && kj.mjr->skin) anim_bind(kj.jranim, kj.mjr->skin);
        kj.talk = -1;
        printf("demo koopajr ship: row %d, camera %d\n", dkj.row, dkj.cam);
        break;
    }
}
