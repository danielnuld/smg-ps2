/* IceMerameraKing.cpp after Petari (Baron Brr, IceVolcanoGalaxy 1's IceMountainZone; #included by game.c, mk_*):
 * walker kind WALK_MERAKING, scale 1.5, its body (130 x its Center joint's scale X) at JointRoot, binder the same
 * over its feet, its up (_BC) and gravity those of its placement. Its 17 nerves in Petari's order: Search (to Mario
 * within 4500: under 1000 PreAttack, else after 200 (100 on its last life) with every ice down Throw / Throw2nd), the
 * throws (AttackThrow; at 22 a ThrowingIce from its Top joint at Mario's centre + his velocity x 35; 3 a volley, 6 on
 * its last life), PreAttack (120 keeping 800 up, then Attack: falls at 6, AttackAfter: the IceMerameraKingShock ring,
 * Land, HIP_DROP, rumble / shake by distance), Extinguish (a spin storm 600 from it, not within its body: GoOut,
 * BodyIceOff, 20 away, 40 frames rising to 200 over its shadow, then falling), Escape (rolling on the ground away
 * from Mario at -0.7, its JointRoot rolled by its velocity (rotateQuatRollBall), EscapeJump at a wall ahead, 600
 * frames then PreRecover / Recover: BodyIce back, Search), a spin (or the Ice Mario freeze spin) while it rolls:
 * Damage (Damage, 80 up, stopScene 4) and the 怒りデモ (AngryDemo1st: Recover, 8 star pieces; 2nd: Summon, its
 * ChildIceMeramera out at 90, 16 pieces), at 0 life the 死亡デモ (DeathDemoWait: stopStageBGM 30; DeathDemo: Death, the
 * IceMerameraKingBreak model's Break, hidden 37 before its end, endDemo, kill: startAfterBossBGM, its power star from
 * its place). The opening (メラキンオープニング, the DemoGroup's switch: the arena's SwitchCubes) moves it as its cast
 * (BeginDemo: a throw); in it SE_DM_LV_MUTE_BGM and from カメラ寄る's last step, and near it (5000) afterwards, the
 * pinch BGM (playLevelMarioPinchBGM). Its body freezes Mario (EnemyAttackFreeze) but pushes him in the hurt / rolling /
 * demo nerves or as Ice Mario. ThrowingIce (MoguStone.cpp): IceManIce x 1.3 on a ParabolicPath (200 over the higher
 * end) for 101 frames turning 0.25 a frame, then falling (2 a frame); it breaks on the map, on Mario (Ice Mario: an
 * EnemyAttackStrong, else Freeze) or a star piece. Its sheet's parts by index (DemoSheet MeraKingOpening's time
 * sheet): 0 カメラ俯瞰, 1 カメラ寄る, 2 怒りデモ, 3 カメラ寄る２, 4 死亡デモ; its camera target (メラキン注目ターゲット,
 * a CameraTargetDemoActor at its placement, at its place in the death demo) through dx_tgt.
 * ponytail: the SE_DM_LV_MUTE_BGM's BGM mute (JAudio's flag, no such service: the SE alone); BodyIceOff's
 * MerameraParticleCallBack (SpinPullParticleCallBack: no particle callbacks); the BRKs (normal / goout / recover / down:
 * one baked colour a model); its children are the port's Meramera walker (Meramera.cpp not ported: issue left open) */
enum { MK_SEARCH, MK_BEGINDEMO, MK_THROW, MK_THROW2ND, MK_EXTINGUISH, MK_ESCAPE, MK_ESCAPEJUMP, MK_DAMAGE, MK_PRERECOVER,
       MK_RECOVER, MK_PREATTACK, MK_ATTACK, MK_ATTACKAFTER, MK_ANGRY1, MK_ANGRY2, MK_DEATHWAIT, MK_DEATH };
#define MK_ICES 6
#define MK_SCALE 1.5f
typedef struct { int nrv, step, bound, smoke; V3 pos, vel, b0, ax, ay, az, rax; V3 p0, py, pz; float pa, pv, pl; } MkIce;
static struct {
    int row, demo;                 /* its row, its demo (-1 none) */
    V3 c8, up, d4f;                /* _C8 its place, _BC its up, the front of _D4 (its placement's rotation) */
    Quat q;                        /* _100: JointRoot's turn */
    int e0, e4, e8, ec, b120, b121; /* throws this volley, the next ice, escape frames, life, the pinch flags */
    float r, h11c;                 /* the body's radius, _11C */
    int hidden, bodyice, rolling;  /* hideModel, BodyIce / Rolling on */
    MkIce ice[MK_ICES];
    int shock, shock_t; V3 shock_p; float shock_r[2]; /* IceMerameraKingShock: alive, step, place, circle / circle_end */
    int brk, brk_t, brk_hidden; V3 brk_p;              /* IceMerameraKingBreak */
    float m[16]; const float (*w)[16]; int nw;          /* its last base matrix and pose */
    u32 lv, lvroll, lvmute;
} mk = {.row = -1};
static int mk_is(const Kuribo *k, int n) { return k->nrv == n; }
static int mk_escaping(const Kuribo *k) { return k->nrv == MK_ESCAPE || k->nrv == MK_ESCAPEJUMP; }
static Kuribo *mk_find(void)
{
    for (int i = 0; i < nkuribo; i++)
        if (kuribo[i].kind == WALK_MERAKING && kuribo[i].state != KURIBO_DEAD) return &kuribo[i];
    return NULL;
}
static V3 mk_center(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); } /* getPlayerCenterPos */
static V3 mk_joint(const Kuribo *k, int j) /* a joint's world place from its last pose (AllRoot 0, Center 1, JointRoot 2, Top 7) */
{
    if (!mk.w || j >= mk.nw) return vadd(k->pos, vmul(mk.up, mk.r));
    const float *w = mk.w[j], *m = mk.m;
    return v3(m[0] * w[12] + m[4] * w[13] + m[8] * w[14] + m[12], m[1] * w[12] + m[5] * w[13] + m[9] * w[14] + m[13],
              m[2] * w[12] + m[6] * w[13] + m[10] * w[14] + m[14]);
}
static void mk_move(Kuribo *k, float a, float g, float f, float t) { w_move_turn(k, vsub(mario->pos, k->pos), a, g, f, t); } /* moveAndTurnToPlayer */
static float mk_norm(float x, float lo, float hi) { float r = hi - lo; if (fabsf(r) < 1e-6f) return x < lo ? 0.0f : 1.0f; return (fminf(fmaxf(x, lo), hi) - lo) / r; }
static void mk_keep_height(Kuribo *k, float h, float a, float range) /* addVelocityKeepHeightUseShadow(h, a, range): the shadow 1500 down */
{
    V3 hp;
    if (!kcl_line(k->pos, vadd(k->pos, vmul(k->grav, 1500.0f)), &hp, NULL, NULL)) return; /* isShadowProjectedAny */
    float f = vlen(vsub(hp, k->pos)) - h;
    float n = f >= 0.0f ? mk_norm(f, -range, range) : -mk_norm(-f, -range, range); /* normalizeAbs */
    k->vel = vadd(k->vel, vmul(k->grav, a * n));
}
static float mk_shadow_len(const Kuribo *k) /* getShadowNearProjectionLength (FLOAT_MAX when none) */
{
    V3 hp;
    return kcl_line(k->pos, vadd(k->pos, vmul(k->grav, 1500.0f)), &hp, NULL, NULL) ? vlen(vsub(hp, k->pos)) : 3.4e38f;
}
static void mk_to_init(Kuribo *k) /* addVelocityToInitPos */
{
    V3 v = vsub(mk.c8, k->pos);
    if (vdot(k->grav, v) > 0.0f) v = vtangent(v, k->grav);
    if (vlen(v) > 1e-6f) k->vel = vadd(k->vel, vmul(vnorm(v), 0.5f));
}
static void mk_hv(Kuribo *k, V3 d, float h, float v, int add) /* set / addVelocitySeparateHV */
{
    V3 s = vtangent(d, k->grav);
    s = vlen(s) > 1e-6f ? vnorm(s) : v3(0, 0, 0);
    V3 nv = vsub(vmul(s, h), vmul(k->grav, v));
    k->vel = add ? vadd(k->vel, nv) : nv;
}
static void mk_roll(V3 vel, V3 n, float radius) /* rotateQuatRollBall: calcMomentRollBall, rotateQuatMoment (q x rot) */
{
    if (vlen(n) < 1e-6f || radius < 1e-6f) return;
    V3 mo = vmul(vcross(vnorm(n), vel), 1.0f / radius);
    float a = vlen(mo);
    if (a > 1e-9f) mk.q = q_norm(q_mul(mk.q, q_axis(mo, a)));
}
static int mk_dx_on(void) { return dx.on && mk.demo >= 0 && dx.cur == mk.demo; } /* isDemoActive("メラキンオープニング") */
static int mk_try_demo(int part) /* tryStartDemoRegisteredMarioPuppetable / requestStartTimeKeepDemo at a part */
{
    if (mk.demo < 0) return 1; /* (no sheet: on as if it ran) */
    if (dx_busy()) return 0;
    dx_request_at(mk.demo, part);
    return 1;
}
static void mk_fx(const char *name) /* emitEffect (a one-time one) */
{
    fx_named("IceMerameraKing", name, mk.m, mk.w, NULL);
}
static Kuribo *mk_child(int i) /* its ChildObj: the ChildIceMeramera of its zone */
{
    int n = 0;
    for (int j = 0; j < nkuribo; j++)
        if (kuribo[j].kind == WALK_MERAMERA && !strcmp(walker_cls(&kuribo[j]), "ChildIceMeramera") && sc->objs[kuribo[j].row].zone == sc->objs[mk.row].zone && n++ == i)
            return &kuribo[j];
    return NULL;
}
static void mk_child_kill(Kuribo *e) /* Meramera::kill: Break, appearCoinPop 1 */
{
    if (e->state == KURIBO_DEAD) return;
    e->state = KURIBO_DEAD; row_kill(e->row);
    fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up); pop_coins(e->pos, e->up, 1);
}
static void mk_ice_break(MkIce *c, int sound)
{
    float m[16];
    basis_matrix(m, c->pos, c->ay, c->az, 0);
    fx_named("IceManIce", "Break", m, NULL, NULL);
    if (sound) se_start(SE_BM_ICEMERAKING_STONE_BREAK, &c->pos.x); /* startBreakSound */
    c->smoke = 0; c->nrv = 0; /* deleteEffect Smoke, kill */
}
static int mk_dead_all_ice(void) { for (int i = 0; i < MK_ICES; i++) if (mk.ice[i].nrv) return 0; return 1; } /* isDeadAllIce */
static int mk_enable_throw(const Kuribo *k) /* isEnableThrow */
{
    const Row *r = &sc->objs[k->row];
    if (r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) return 0;
    if (swimming) return 0; /* isPlayerInWaterMode / isPlayerOnWaterSurface */
    V3 d = vsub(mk_center(), k->pos);
    if (vlen(d) > 1e-6f) d = vnorm(d);
    return vdot(mk.up, d) > -0.6f;
}
static MkIce *mk_dead_weapon(const Kuribo *k) /* getDeadWeaponAndAppear */
{
    int t = mk.e4;
    for (int i = 0; i < MK_ICES; i++) {
        if (!mk.ice[t].nrv) {
            MkIce *c = &mk.ice[t];
            memset(c, 0, sizeof *c);
            c->pos = k->pos; c->nrv = 1; c->step = -1; /* resetPosition, appear: Taken */
            c->ax = v3(1, 0, 0); c->ay = v3(0, 1, 0); c->az = v3(0, 0, 1);
            mk.e4 = (mk.e4 + MK_ICES + 1) % MK_ICES;
            return c;
        }
        t = (t + MK_ICES + 1) % MK_ICES;
    }
    return NULL;
}
static void mk_emit_ice(MkIce *c, V3 start, V3 end, V3 grav) /* emitIce: initFromUpVectorAddHeight(start, end, -gravity, 200) */
{
    V3 up = vmul(grav, -1.0f), d = vsub(end, start);
    float h = vdot(up, d), mx = 200.0f + (h < 0.0f ? 0.0f : h), disc = mx * (mx - h);
    V3 z = vsub(d, vmul(up, h));
    c->p0 = start; c->py = up; c->pl = vlen(z); c->pz = c->pl > 1e-6f ? vmul(z, 1.0f / c->pl) : v3(0, 0, 0);
    if (disc <= 0.0f) { c->pa = -mx; c->pv = h + mx; } /* calcParabolicFunctionParam */
    else if (fabsf(h) < 1e-4f) { c->pa = -4.0f * mx; c->pv = 4.0f * mx; }
    else {
        float rt = sqrtf(disc), t1 = (mx + rt) / h, t2 = (mx - rt) / h;
        float t = t1 >= 0 && t1 <= 1 ? t1 : t2 >= 0 && t2 <= 1 ? t2 : 1.0f;
        c->pa = -mx / (t * t); c->pv = -2.0f * t * c->pa;
    }
    V3 b = vtangent(d, grav);
    c->b0 = vlen(b) > 1e-6f ? vnorm(b) : v3(0, 0, 0);
    c->nrv = 2; c->step = -1; c->smoke = 1; /* Throw, emitEffect Smoke */
}
static void mk_ices_update(const Kuribo *k)
{
    for (int i = 0; i < MK_ICES; i++) {
        MkIce *c = &mk.ice[i];
        if (!c->nrv) continue;
        int st = ++c->step;
        if (c->nrv == 2) { /* exeThrow: doBehavior */
            if (st == 0) { V3 a = vcross(c->b0, k->grav); c->rax = vlen(a) > 1e-6f ? vnorm(a) : v3(1, 0, 0); } /* _A0.setRotate(_B0 x gravity, 0.25) */
            float t = fminf((float)st / 101.0f, 1.0f); /* calcNerveRate(101) */
            V3 p = vadd(c->p0, vadd(vmul(c->py, t * (c->pv + c->pa * t)), vmul(c->pz, c->pl * t)));
            c->vel = vsub(p, c->pos);
            if (c->bound) { mk_ice_break(c, 1); continue; } /* isBinded */
            if (st > 100) { c->nrv = 3; c->step = -1; }
        } else if (c->nrv == 3) { /* exeFall: applyVelocityDampAndGravity(2, 0.8, 0.98, 0.98) */
            c->vel = vadd(vmul(c->vel, 0.98f), vmul(k->grav, 2.0f));
            if (c->bound || st > 60) { mk_ice_break(c, 1); continue; }
        }
        if (c->nrv >= 2) { /* _90 = _A0 _90 */
            c->ax = rot_axis(c->ax, c->rax, 0.25f); c->ay = rot_axis(c->ay, c->rax, 0.25f); c->az = rot_axis(c->az, c->rax, 0.25f);
        }
        c->pos = vadd(c->pos, c->vel);
        Hit h[1];
        c->bound = c->nrv >= 2 && kcl_query(c->pos, 60.0f * 1.3f, h, 1) > 0; /* initBinder 60 x 1.3 */
        /* attackSensor (body 80 x 1.3) */
        if (vlen(vsub(mk_center(), c->pos)) < 80.0f * 1.3f + HIT_BODY_R && !mario->hurt_timer &&
            mario_attack(player_mode == PM_ICE ? ATK_STRONG : ATK_FREEZE, c->pos))
            mk_ice_break(c, 1);
    }
}
int mk_shot(V3 p) /* a star piece on its ices (isMsgStarPieceAttack in Throw: Break) or its body (isMsgStarPieceReflect) */
{
    Kuribo *k = mk_find();
    if (!k) return 0;
    for (int i = 0; i < MK_ICES; i++)
        if (mk.ice[i].nrv == 2 && vlen(vsub(p, mk.ice[i].pos)) < 80.0f * 1.3f) { mk_ice_break(&mk.ice[i], 1); return 1; }
    return row_shown(k->row) && vlen(vsub(p, mk_joint(k, 2))) < mk.r ? 2 : 0;
}
static void mk_kill(Kuribo *k) /* kill(): onSwitchDead, startAfterBossBGM, its ices */
{
    k->state = KURIBO_DEAD;
    note_star_row = k->row; note_star_at = k->pos; /* requestAppearPowerStar(this, mPosition) */
    row_kill(k->row);
    bgm_after_boss();
    for (int i = 0; i < MK_ICES; i++) mk.ice[i].nrv = 0;
    printf("ice merameraking: dead, its star\n");
}
/* AudBgmConductor::playPinchEventBgmLevel / stopPinchEventBgmSoon / movementPinchEventBgm (game.c pinch) */
static void mk_init(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z;
    row_axes(r, &x, &z);
    memset(&mk.e0, 0, sizeof mk - offsetof(typeof(mk), e0));
    mk.row = k->row; mk.c8 = k->pos; mk.up = vnorm(vcross(z, x)); mk.d4f = vnorm(z);
    mk.q = q_ident();
    mk.ec = 3; mk.r = 130.0f * (MK_SCALE / 1.8f); mk.bodyice = 1; /* emitEffect BodyIce */
    mk.demo = -1;
    dx_tgt.on = 1; dx_tgt.pos = mk.c8; dx_tgt.up = mk.up; dx_tgt.front = mk.d4f; /* mCameraTarget at _C8 / _D4 */
    for (int d = 0; d < ndemos; d++) if (!strcmp(demos[d].h->name, "メラキンオープニング")) mk.demo = d;
    k->front = mk.d4f; k->grav = vmul(up_at(k->pos, mk.up), -1.0f); /* calcGravity */
    k->qy = k->up = mk.up; k->qz = k->front;
    w_nrv(k, MK_SEARCH);
    w_act(k, "Wait");
    for (int i = 0; ; i++) { Kuribo *e = mk_child(i); if (!e) break; e->state = KURIBO_DEAD; } /* makeActorDead */
    printf("ice merameraking: init, demo %d, life %d\n", mk.demo, mk.ec);
}
static void mk_update(Kuribo *k)
{
    if (!k->front.x && !k->front.y && !k->front.z) mk_init(k);
    if (mk.row != k->row) return;
    if (mk_dx_on() && dx.part == 4 && k->nrv != MK_DEATH) w_nrv(k, MK_DEATH); /* the 死亡デモ's start: its nerve DeathDemo */
    int st = ++k->step;
    V3 body = mk_joint(k, 2), mc = mk_center();
    switch (k->nrv) {
    case MK_SEARCH: {
        if (st == 0) w_act(k, "Wait");
        mk_to_init(k);
        mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f); /* hFlyParam */
        mk_keep_height(k, 400.0f, 1.5f, 300.0f);
        float d = vlen(vsub(mario->pos, k->pos));
        if (!(4500.0f < d)) {
            if (d < 1000.0f) w_nrv(k, MK_PREATTACK);
            else if (mk_enable_throw(k)) {
                if (!(mk.ec > 2)) { if (st > 100 && mk_dead_all_ice()) { mk.e0 = 0; w_nrv(k, MK_THROW2ND); } }
                else if (st > 200 && mk_dead_all_ice()) { mk.e0 = 0; w_nrv(k, MK_THROW); }
            }
        }
        break;
    }
    case MK_BEGINDEMO: case MK_THROW: case MK_THROW2ND: {
        if (st == 0) { w_act(k, "AttackThrow"); if (k->nrv == MK_BEGINDEMO) k->front = mk.d4f; } /* calcFrontVec(mCameraTarget) */
        mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f);
        mk_keep_height(k, 400.0f, 1.5f, 300.0f);
        mk_to_init(k);
        if (st == 22) {
            MkIce *c = mk_dead_weapon(k);
            if (!c) { w_nrv(k, MK_SEARCH); break; }
            c->pos = mk_joint(k, 7); /* mFixedPos (Top) */
            mk.e0++;
            V3 mv = vadd(mario->hvel, vmul(mario->up, mario->vup));
            mk_emit_ice(c, c->pos, vadd(mc, vmul(mv, 35.0f)), k->grav);
            w_se(k, SE_BM_ICEMERAKING_THROW);
        }
        if (w_bck_stopped(k)) {
            if (vlen(vsub(mario->pos, k->pos)) < 1000.0f) w_nrv(k, MK_PREATTACK);
            else {
                int n = k->nrv == MK_THROW2ND ? 6 : 3;
                if (n <= mk.e0 || !mk_enable_throw(k)) w_nrv(k, MK_SEARCH);
                else w_nrv(k, k->nrv == MK_THROW2ND ? MK_THROW2ND : MK_THROW);
            }
        }
        break;
    }
    case MK_EXTINGUISH:
        if (st == 0) {
            w_act(k, "GoOut");
            w_se(k, SE_BM_ICEMERAKING_BLOW); w_se(k, SE_BM_ICEMERAKING_SMOKE);
            mk.bodyice = 0; mk_fx("BodyIceOff"); /* deleteEffect BodyIce, emitEffectWithParticleCallBack BodyIceOff */
            V3 v = vtangent(vsub(k->pos, mc), k->grav);
            mk_hv(k, vlen(v) > 1e-6f ? vnorm(v) : v, 20.0f, 0.0f, 0);
            mk.h11c = 200.0f + mk_shadow_len(k);
            printf("ice merameraking: extinguished (life %d)\n", mk.ec);
        }
        if (st < 40) { mk_keep_height(k, mk.h11c, 1.3f, 700.0f); mk_move(k, 0.0f, 0.125f, 0.96f, 0.0f); }
        else if (w_on_ground(k) && w_bck_stopped(k)) {
            mk_move(k, 0.0f, 1.0f, 0.8f, 0.0f); w_se(k, SE_BM_ICEMERAKING_LAND);
            mk.e8 = 0; w_nrv(k, MK_ESCAPE);
        } else mk_move(k, 0.0f, 4.0f, 0.96f, 0.0f);
        break;
    case MK_ESCAPE:
        mk.e8++;
        if (mk.e8 > 600 && w_on_ground(k)) { mk.e8 = 0; w_nrv(k, MK_PRERECOVER); break; }
        mk.rolling = 1; /* emitEffect Rolling */
        if (w_on_ground(k)) {
            float v = vlen(k->vel), vol = (fminf(fmaxf(v, 2.0f), 6.0f) - 2.0f) / 4.0f * 100.0f; /* getLinerValueFromMinMax */
            (void)vol; mk.lvroll = se_level(SE_BM_LV_ICEMERAKING_ROLL, &k->pos.x, mk.lvroll); /* ponytail: its volume (levelA) */
            mk_roll(k->vel, k->gn, 0.5f * mk.r);
            mk_move(k, -0.7f, 1.0f, 0.96f, 180.0f); /* hEscapeOnGroundParam */
            if (k->on_wall == 2 && vdot(vmul(k->front, -1.0f), k->wn) < -0.25f) w_nrv(k, MK_ESCAPEJUMP); /* isBindedWallFront(-_B0, 0.25) */
        } else {
            mk_roll(k->vel, vmul(k->grav, -1.0f), 0.5f * mk.r);
            mk_move(k, 0.0f, 3.0f, 0.96f, 0.0f); /* hEscapeOnAirParam */
        }
        break;
    case MK_ESCAPEJUMP:
        if (st == 20) { mk_hv(k, k->front, 20.0f, 90.0f, 1); w_se(k, SE_BM_ICEMERAKING_JUMP); }
        mk.e8++;
        if (w_on_ground(k)) {
            mk_roll(k->vel, k->gn, 0.5f * mk.r);
            mk_move(k, -0.7f, 1.0f, 0.96f, 180.0f);
            if (st > 20) { mk_fx("Land"); w_se(k, SE_BM_ICEMERAKING_LAND); w_nrv(k, MK_ESCAPE); }
        } else {
            mk_roll(k->vel, vmul(k->grav, -1.0f), 0.5f * mk.r);
            mk_move(k, 0.0f, 3.0f, 0.96f, 0.0f);
        }
        break;
    case MK_DAMAGE:
        if (st == 0) {
            w_act(k, "Damage"); mk_fx("Damage");
            se_start(SE_PM_SPIN_HIT, &k->pos.x); w_se(k, SE_BM_ICEMERAKING_DAMAGE); /* startBlowHitSound */
            k->vel = vmul(k->grav, -80.0f); scene_stop = 4; /* setVelocityJump 80, stopSceneForDefaultHit(4) */
            printf("ice merameraking: damage, life %d\n", mk.ec);
        }
        if (st > 20) mk_keep_height(k, 900.0f, 3.0f, 300.0f);
        if (w_on_ground(k)) mk_move(k, 0.0f, 1.0f, 0.8f, 0.0f);
        else mk_move(k, 0.0f, 1.9f, 0.98f, 0.0f); /* hDamageJumpParam */
        if (st > 60 && mk_try_demo(2)) w_nrv(k, mk.ec == 1 ? MK_ANGRY2 : MK_ANGRY1); /* 怒りデモ */
        break;
    case MK_PRERECOVER:
        if (st == 0) { k->vel = vmul(k->grav, -70.0f); w_se(k, SE_BM_ICEMERAKING_JUMP); }
        mk_to_init(k);
        if (st < 50) mk_move(k, 0.0f, 1.0f, 0.96f, 0.0f);
        else if (st == 50) { w_act(k, "PreRecover"); mk.r = 130.0f; mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f); }
        else if (st < 120) { mk_keep_height(k, 750.0f, 1.5f, 300.0f); mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f); }
        else w_nrv(k, MK_RECOVER);
        break;
    case MK_RECOVER:
        if (st == 0) { w_act(k, "Recover"); mk.bodyice = 1; w_se(k, SE_BM_ICEMERAKING_PRE_RECOVER); }
        if (st == 80) w_se(k, SE_BM_ICEMERAKING_RECOVER);
        mk_to_init(k);
        mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f);
        mk_keep_height(k, 750.0f, 1.5f, 300.0f);
        if (st > 160) w_nrv(k, MK_SEARCH);
        break;
    case MK_PREATTACK:
        if (st < 120) mk_keep_height(k, 800.0f, 1.5f, 300.0f);
        else if (!mk.shock) { w_nrv(k, MK_ATTACK); break; }
        mk_to_init(k);
        mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f);
        break;
    case MK_ATTACK:
        if (st == 0) { w_act(k, "Attack"); w_se(k, SE_BM_ICEMERAKING_PRE_ATTACK); }
        if (!w_bck_stopped(k)) { mk_keep_height(k, 800.0f, 1.5f, 300.0f); mk_move(k, 0.0f, 0.5f, 0.9f, 1.0f); }
        else mk_move(k, 0.0f, 6.0f, 0.98f, 0.0f); /* hAttackParam */
        if (w_on_ground(k)) w_nrv(k, MK_ATTACKAFTER);
        break;
    case MK_ATTACKAFTER:
        if (st == 0) {
            mk.shock = 1; mk.shock_t = -1; mk.shock_p = k->pos; mk.shock_r[0] = mk.shock_r[1] = 0.0f; /* appear: Shock, circle 0 */
            mk_fx("Land"); w_act(k, "AttackEnd"); w_se(k, SE_BM_ICEMERAKING_HIP_DROP);
            float d = vlen(vsub(mario->pos, k->pos)); /* tryRumblePadAndCameraDistanceStrong(800, 1200, 2000) */
            if (d < 800.0f) { pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL); }
            else if (d < 1200.0f) { pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); }
            else if (d < 2000.0f) { pad_rumble(RUMBLE_WEAK); camera_shake(SHAKE_WEAK); }
        }
        mk_move(k, 0.0f, 3.0f, 0.96f, 0.0f); /* hOnAirParam */
        if (st > 90) w_nrv(k, MK_SEARCH);
        break;
    case MK_ANGRY1: case MK_ANGRY2:
        if (st == 0) {
            mk.bodyice = 1;
            if (k->nrv == MK_ANGRY2) { w_act(k, "Summon"); w_se(k, SE_BM_ICEMERAKING_PRE_ANGRY2); }
            else { w_act(k, "Recover"); w_se(k, SE_BM_ICEMERAKING_PRE_ANGRY1); }
        }
        if (st == 1) { k->pos = mk.c8; k->vel = v3(0, 0, 0); } /* resetPosition(_C8) */
        mk_move(k, 0.0f, 0.5f, 0.9f, 1.5f); /* hAngryDemoParam */
        mk_keep_height(k, 400.0f, 1.5f, 300.0f);
        if (k->nrv == MK_ANGRY2 && st == 90)
            for (int i = 0; ; i++) { Kuribo *e = mk_child(i); if (!e) break; if (!row_dead[e->row]) { e->state = KURIBO_WALK; e->pos = row_pos(&sc->objs[e->row]); e->vel = v3(0, 0, 0); } } /* makeActorAppeared */
        if (k->nrv == MK_ANGRY2 && st == 85) w_se(k, SE_BM_ICEMERAKING_ANGRY2);
        else if (st == 85) w_se(k, SE_BM_ICEMERAKING_ANGRY1);
        bgm_pinch(1);
        if (mk_dx_on() && dx_last(2)) {
            launch_pieces(vsub(k->pos, vmul(k->grav, 200.0f)), mk.up, !(mk.ec > 2) ? 8 : 16, 15.0f, 70.0f); /* appearStarPiece */
            w_se(k, SE_OJ_STAR_PIECE_BURST);
            w_nrv(k, MK_SEARCH);
        } else if (mk.demo < 0 && st > 162) w_nrv(k, MK_SEARCH);
        break;
    case MK_DEATHWAIT:
        if (st == 0) bgm_stop(30);
        k->vel = v3(0, 0, 0);
        if (mk.demo < 0) w_nrv(k, MK_DEATH);
        break;
    case MK_DEATH: {
        if (st == 0) {
            w_act(k, "Death"); w_se(k, SE_BM_ICEMERAKING_DEAD);
            mk.brk = 1; mk.brk_t = -1; mk.brk_hidden = 0; mk.brk_p = k->pos; /* appear, resetPosition, Break */
            dx_tgt.pos = k->pos; /* mCameraTarget->mPosition */
            for (int i = 0; ; i++) { Kuribo *e = mk_child(i); if (!e) break; mk_child_kill(e); }
            mk.bodyice = 0;
        }
        if (w_on_ground(k)) k->vel = v3(0, 0, 0);
        else mk_move(k, 0.0f, 3.0f, 0.96f, 0.0f);
        if (!mk.hidden && st > k->act_frames - 37) mk.hidden = 1; /* hideModelAndOnCalcAnim */
        int bf = anim_clip_info("icemeramerakingbreak/break", &(int){0});
        if (!mk.brk_hidden && mk.brk_t >= bf) mk.brk_hidden = 1;
        if (w_bck_stopped(k) && mk.hidden && mk.brk_hidden && mk_dx_on()) dx_end(); /* endDemo(メラキンオープニング) */
        if (!mk_dx_on()) { mk_kill(k); mk.brk = 0; return; }
        break;
    }
    }
    k->act_t += k->act_rate;
    if (mk.brk) mk.brk_t++;
    if (k->nrv != MK_ESCAPE && mk.rolling) mk.rolling = 0; /* endEscape: deleteEffect Rolling */
    /* control() */
    if (mk.w && mk.nw > 1) { const float *c = mk.w[1]; mk.r = 130.0f * sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]); } /* copyJointScale Center */
    if (!mk_escaping(k)) mk.q = q_slerp(mk.q, q_ident(), 0.25f); /* _100.slerp((0, 0, 0, 1), 0.25) */
    if (!mk_escaping(k)) mk.lv = se_level(SE_BM_LV_ICEMERAKING_WAIT, &k->pos.x, mk.lv);
    if (mk_dx_on()) {
        mk.lvmute = se_level(SE_DM_LV_MUTE_BGM, NULL, mk.lvmute); /* startSystemLevelSE */
        if (dx_last(1)) { bgm_pinch(mk.b120); mk.b120 = mk.b121 = 1; } /* isDemoPartLastStep カメラ寄る */
    } else if (k->nrv != MK_DEATHWAIT && k->nrv != MK_DEATH && vlen(vsub(mario->pos, k->pos)) < 5000.0f && mk.b121) {
        bgm_pinch(mk.b120); mk.b120 = 1;
    }
    /* receiveOtherMsg: a spin storm (Mario's spin's first 15 frames, his sensor within 600) */
    int hurt = k->nrv == MK_DEATH || k->nrv == MK_EXTINGUISH || k->nrv == MK_DAMAGE || k->nrv == MK_ANGRY1 || k->nrv == MK_ANGRY2 ||
               mk_escaping(k) || k->nrv == MK_PRERECOVER || k->nrv == MK_RECOVER;
    if (mario->spin_timer > 0 && mario->spin_t < 15 && vlen(vsub(mc, body)) < 600.0f && !hurt) {
        if (vlen(vtangent(vsub(mc, k->pos), k->grav)) >= mk.r) w_nrv(k, MK_EXTINGUISH);
    }
    /* receiveMsgPlayerAttack: a spin (or the freeze spin) while it rolls */
    if (mk_escaping(k) && mario_offence(body, mk.r, k->grav) == MSG_HITALL) {
        int freeze = player_mode == PM_ICE;
        if (--mk.ec == 0) {
            if (freeze) bgm_pinch_stop_soon(); /* stopMarioPinchBGMSoon */
            if (mk.demo >= 0) dx_request_at(mk.demo, 4); /* (queued: DemoStartRequestHolder) */
            w_nrv(k, MK_DEATHWAIT); /* requestStartTimeKeepDemo(死亡デモ, DeathDemo, DeathDemoWait) */
            printf("ice merameraking: down, its death demo\n");
        } else w_nrv(k, MK_DAMAGE);
    }
    /* attackSensor: the body on Mario */
    else if (vlen(vsub(mc, body)) < mk.r + HIT_BODY_R) {
        int push = k->nrv == MK_DAMAGE || k->nrv == MK_EXTINGUISH || mk_escaping(k) || k->nrv == MK_PRERECOVER || k->nrv == MK_RECOVER ||
                   k->nrv == MK_DEATHWAIT || k->nrv == MK_ANGRY1 || k->nrv == MK_ANGRY2 || k->nrv == MK_DEATH || player_mode == PM_ICE;
        if (push || mario->hurt_timer || !mario_attack(ATK_FREEZE, body)) mario_push_out(body, mk.r); /* sendMsgPush / EnemyAttackFreeze */
    }
    w_bind(k, mk.r);
    k->up = k->qy = mk.up; k->qz = k->front;
    mk_ices_update(k);
    if (mk.shock) { /* IceMerameraKingShockWave::control, attackSensor */
        int fr = anim_clip_info("icemeramerakingshock/shock", &(int){0});
        if (++mk.shock_t >= fr) mk.shock = 0;
        else {
            V3 sp = vsub(mk.shock_p, vmul(mk.up, 100.0f)); /* both sensors (0, -100, 0) */
            float pr = HIT_BODY_R;
            if (vlen(vsub(mc, sp)) < mk.shock_r[0] + pr && !(vlen(vsub(sp, mc)) < mk.shock_r[1] + pr)) {
                V3 v = vsub(mc, sp);
                if (fabsf(vdot(v, mk.up)) < 200.0f && !mario->hurt_timer) {
                    v = vtangent(v, mk.up);
                    mario_attack(ATK_FLIP_MAXIMUM, vsub(mario->pos, v)); /* sendMsgEnemyAttackFlipMaximumToDir */
                }
            }
        }
    }
}
static int mk_noclip(const Kuribo *k) /* control: invalidateClipping while its SW_A is on */
{
    const Row *r = &sc->objs[k->row];
    return r->sw[SW_A] >= 0 && sw_on(r, SW_A);
}
static void mk_demo_update(void) /* a demo moves its casts: the king, its children */
{
    Kuribo *k = mk_find();
    if (!k || !mk_dx_on()) return;
    mk_update(k);
    for (int i = 0; ; i++) { Kuribo *e = mk_child(i); if (!e) break; if (e->state != KURIBO_DEAD) update_kuribo_one(e); }
}
static void mk_begin_demo(int row) /* registerDemoActionNerve: BeginDemo (the opening's カメラ寄る) */
{
    Kuribo *k = mk_find();
    if (!k || k->row != row) return;
    if (!k->front.x && !k->front.y && !k->front.z) mk_init(k);
    w_nrv(k, MK_BEGINDEMO);
    printf("ice merameraking: opening demo\n");
}
static void mk_draw(Kuribo *k, const Model *md, Anim *a) /* calcAndSetBaseMtx: makeMtxUpFrontPos(_BC, _B0), x 1.5; JointRoot turned _100 */
{
    float m[16];
    anim_update(a);
    basis_matrix(m, k->pos, mk.up, k->front, 0);
    for (int j = 0; j < 12; j++) m[j] *= MK_SCALE;
    if (a->skin && a->skin->njoints > 7) { /* calcJoint: JointRoot's matrix x makeQuat(_100) in Center's frame, its place kept */
        V3 qx, qy, qz;
        q_axes(mk.q, &qx, &qy, &qz);
        float R[9] = {qx.x, qx.y, qx.z, qy.x, qy.y, qy.z, qz.x, qz.y, qz.z}; /* columns */
        const float *c = a->world[1];
        V3 b[3];
        for (int i = 0; i < 3; i++) { b[i] = v3(c[4 * i], c[4 * i + 1], c[4 * i + 2]); b[i] = vlen(b[i]) > 1e-6f ? vnorm(b[i]) : v3(i == 0, i == 1, i == 2); }
        float W[9]; /* B R B^T, by the image of each world axis */
        for (int i = 0; i < 3; i++) { /* the image of world axis i */
            V3 e = v3(i == 0, i == 1, i == 2), l = v3(vdot(b[0], e), vdot(b[1], e), vdot(b[2], e));
            V3 rl = v3(R[0] * l.x + R[3] * l.y + R[6] * l.z, R[1] * l.x + R[4] * l.y + R[7] * l.z, R[2] * l.x + R[5] * l.y + R[8] * l.z);
            V3 o = vadd(vadd(vmul(b[0], rl.x), vmul(b[1], rl.y)), vmul(b[2], rl.z));
            W[3 * i] = o.x; W[3 * i + 1] = o.y; W[3 * i + 2] = o.z;
        }
        V3 p = v3(a->world[2][12], a->world[2][13], a->world[2][14]);
        for (int j = 2; j < a->skin->njoints; j++) { /* JointRoot and below */
            float *q = a->world[j];
            for (int col = 0; col < 4; col++) {
                V3 v = col < 3 ? v3(q[4 * col], q[4 * col + 1], q[4 * col + 2]) : vsub(v3(q[12], q[13], q[14]), p);
                V3 o = vadd(vadd(vmul(v3(W[0], W[1], W[2]), v.x), vmul(v3(W[3], W[4], W[5]), v.y)), vmul(v3(W[6], W[7], W[8]), v.z));
                if (col == 3) o = vadd(o, p);
                q[4 * col] = o.x; q[4 * col + 1] = o.y; q[4 * col + 2] = o.z;
            }
        }
    }
    memcpy(mk.m, m, sizeof m);
    mk.w = (const float (*)[16])a->world; mk.nw = a->skin ? a->skin->njoints : 0;
    if (mk.bodyice) fx_named("IceMerameraKing", "BodyIce", m, mk.w, &mk.bodyice);
    if (mk.rolling) fx_named("IceMerameraKing", "Rolling", m, mk.w, &mk.rolling);
    if (mk.hidden) return; /* hideModelAndOnCalcAnim */
    draw_shadow(k->pos, mk.up, k->front, mk.r, mk.r, 0.0f, 0.0f, 1500.0f); /* initShadowVolumeSphere, setShadowDropLength 1500 */
    draw_skin(md, m, a);
}
static void mk_draw_extras(void) /* its ices (IceManIce x 1.3), the shock ring, the break model */
{
    if (mk.row < 0 || mk.row >= (int)sc->nobjs) return;
    float m[16];
    const Model *im = scene_model(sc, "IceManIce");
    for (int i = 0; im && i < MK_ICES; i++) {
        MkIce *c = &mk.ice[i];
        if (!c->nrv) continue;
        basis_matrix(m, c->pos, c->ay, c->az, 0);
        for (int j = 0; j < 12; j++) m[j] *= 1.3f;
        if (c->smoke) fx_named("IceManIce", "Smoke", m, NULL, &c->smoke);
        draw_shadow(c->pos, mk.up, c->az, 90.0f * 1.3f, 90.0f * 1.3f, 0.0f, 0.0f, 1500.0f);
        render_model(im, m, 0x80, 0x80, 0x80);
    }
    const Model *sm = scene_model(sc, "IceMerameraKingShock");
    if (mk.shock && sm && sm->skin) {
        Anim *a = obj_anim((int)sc->nobjs * 5 + mk.row, sm, "IceMerameraKingShock");
        if (a) {
            if (mk.shock_t <= 0) anim_start(a, "icemeramerakingshock/shock", 0);
            anim_update(a);
            if (a->skin->njoints > 2) { /* copyJointScale Hirgh1 / WideInSide: their own Y scales */
                float s0 = vlen(v3(a->world[0][4], a->world[0][5], a->world[0][6])), s1 = vlen(v3(a->world[1][4], a->world[1][5], a->world[1][6])),
                      s2 = vlen(v3(a->world[2][4], a->world[2][5], a->world[2][6]));
                mk.shock_r[0] = 15.0f * (s1 > 1e-6f ? s2 / s1 : s2); mk.shock_r[1] = 24.0f * s0;
            }
            basis_matrix(m, mk.shock_p, mk.up, mk.d4f, 0);
            draw_skin(sm, m, a);
        }
    }
    const Model *bm = scene_model(sc, "IceMerameraKingBreak");
    if (mk.brk && !mk.brk_hidden && bm && bm->skin) {
        Anim *a = obj_anim((int)sc->nobjs * 6 + mk.row, bm, "IceMerameraKingBreak");
        if (a) {
            if (mk.brk_t <= 0) anim_start(a, "icemeramerakingbreak/break", 0);
            anim_update(a);
            basis_matrix(m, mk.brk_p, mk.up, mk.d4f, 0);
            for (int j = 0; j < 12; j++) m[j] *= MK_SCALE;
            draw_skin(bm, m, a);
        }
    }
}
int game_test_mk(int frame) /* mk test (XF=-DMK_TEST, IceVolcanoGalaxy/1): into the arena's SwitchCubes (the opening),
                             * then a spin storm 300 from it, a spin by it while it rolls, three times, then its star */
{
    Kuribo *k = mk_find();
    static int last = -1000, dead_at = -1, row = -1;
    if (k) row = k->row;
    if (!k) {
        if (row < 0) return -2;
        if (dead_at < 0) dead_at = frame;
        int ty = scene_type(sc, "PowerStar");
        for (u32 i = 0; frame - dead_at >= 350 && frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++) /* to its star (arg7: the declarer) */
            if ((int)sc->objs[i].type == ty && sc->objs[i].arg[7] == row) {
                V3 p = row_pos(&sc->objs[i]);
                mario->pos = vsub(p, vmul(mario->up, 60.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
                printf("mk test f %d: to the star\n", frame);
            }
        if (frame % 20 == 0) printf("mk test f %d: king dead, life Mario %d\n", frame, game_life());
        return -1;
    }
    if (frame % 20 == 0) printf("mk test f %d: nerve %d step %d, life %d, %.0f from Mario, r %.0f, life Mario %d, demo %d part %d\n", frame, k->nrv, k->step, mk.ec,
                                vlen(vsub(mario->pos, k->pos)), mk.r, game_life(), dx.on ? dx.cur : -1, dx.part);
    if (demo_on || dx_busy()) return k->nrv;
    if (!mk.b121) { /* into the SwitchCubes (SW_A 50: the demo) and the arena's (SW_A 19) */
        if (frame % 30 == 0) {
            int ty = scene_type(sc, "SwitchCube");
            for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++)
                if ((int)sc->objs[i].type == ty && sc->objs[i].zone == sc->objs[k->row].zone && sc->objs[i].sw[SW_A] == 50) {
                    mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0;
                    printf("mk test f %d: into a switch cube\n", frame);
                    break;
                }
        }
        return k->nrv;
    }
    if (frame - last < 90) return k->nrv;
    V3 up = mk.up, d = vtangent(vsub(mario->pos, k->pos), up);
    d = vlen(d) > 1e-6f ? vnorm(d) : mk.d4f;
    if (mk_escaping(k) && k->step > 20) { /* by it, a spin */
        mario->pos = vadd(k->pos, vmul(d, mk.r + 40.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->spin_timer = SPIN_TIME; mario->spin_t = 0;
        last = frame;
        printf("mk test f %d: spin by it, rolling\n", frame);
    } else if (k->nrv == MK_SEARCH || k->nrv == MK_THROW || k->nrv == MK_THROW2ND) { /* 300 off under it, a spin storm */
        V3 g = vsub(k->pos, vmul(up, mk_shadow_len(k) < 1e30f ? mk_shadow_len(k) : 0.0f));
        mario->pos = vadd(vadd(g, vmul(d, 300.0f)), vmul(up, 30.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->spin_timer = SPIN_TIME; mario->spin_t = 0;
        last = frame;
        printf("mk test f %d: spin storm 300 off\n", frame);
    }
    return k->nrv;
}
