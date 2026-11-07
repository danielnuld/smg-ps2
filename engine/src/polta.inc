/* Polta.cpp (Bouldergeist, PhantomGalaxy 3 / 4's PhantomNestZone) with PoltaActionBase / Sequencer, PoltaArm,
 * PoltaBattleLv1 / Lv2, PoltaDemo, PoltaFunction, PoltaGroundRock(Holder), PoltaRock(Holder), PoltaSensorCtrl,
 * PoltaStateAttackGround / GenerateRock / GroundRockAttack / Punch / Stagger and PoltaWaitStart after Petari.
 * #included by game.c (pol_*). Dead to the eye until its SW_A (start): ポルタ開始デモ (OpeningDemo anim camera,
 * PoltaFormationRock, stopStageBGM(60), MBGM_BOSS_09_A at 60), BattleLv1 (3 body hits, no arms; rocks: white,
 * black = a BombTeresa, yellow = a coin; ground rocks), its body broken: Stagger (StaggerLv1, its BombTeresas from
 * its root every 210), a bomb at the core: ポルタパワーアップデモ (its arms formed, MBGM_BOSS_09_B), BattleLv2 (arms:
 * punches, ground attacks, 2 hits break an arm, repaired after 1200), the body broken again, the core: ポルタダウンデモ,
 * kill (startAfterBossBGM, its power star, its dead switch). Hit only by BombTeresa explosions (bt_update ->
 * pol_explosion); star pieces bounce off it (pol_shot). Its BombTeresas are walker slots after their row (host -1).
 * Its Shadow / HeadShadow effect rows (PoltaShadow) are never emitted: PoltaFunction::emitEffectShadow is empty.
 * ponytail: its arms' Shadow.bcsv poles drawn as discs (no volume lines), no BTK Inner, no center screen blur
 * (startCenterScreenBlur), overlayWithPreviousScreen as render_cover, no declareCameraRegisterVec (arg7), the rocks'
 * Froat / Sign clips not played (static models per Color BVA frame), the shadow projection as a 3000 line along
 * gravity (addVelocityKeepHeightUseShadow), a spin on a ground rock's collision taken at its body sensor, four
 * PoltaGroundRockBreak poses for its 16 ground rocks, no resetPlayerEffect */
#define POL_ROCKS 16 /* PoltaRockHolder sMaxPoltaRock */
#define POL_GBREAKS 4
enum { POQ_WAITSTART, POQ_OPENING, POQ_LV1, POQ_POWERUP, POQ_LV2, POQ_DOWN };
enum { POD_TRYSTART, POD_OPENING, POD_POWERUP, POD_DOWN };
enum { POB_WAIT, POB_PUNCH, POB_ATTACKGROUND, POB_GENGROUNDROCK, POB_GENROCK, POB_DAMAGEBODY, POB_BREAKBODY, POB_STAGGER, POB_DAMAGECORE };
enum { POA_CONTROLLED, POA_DAMAGE, POA_BREAK, POA_BREAKBODY, POA_BROKEN, POA_REPAIR, POA_WAITDAMAGEEND, POA_WAITREPAIREND };
enum { POR_FLOAT, POR_SIGN, POR_MOVE };
enum { POG_SIGN, POG_JUT, POG_WAIT, POG_BREAK };
enum { POS_CORE, POS_BODY, POS_LARM = POS_BODY + 3, POS_RARM = POS_LARM + 11, POS_SCREAM = POS_RARM + 11, POS_HEAD, POS_N }; /* PoltaSensorCtrl: 28 */
typedef struct { int nrv, st, dead; } PolSt; /* an ActorStateBase / NerveExecutor: its nerve, step, mIsDead */
typedef struct { Anim *a; char key[32]; char bva[32]; int bva_clip; float bva_f; } PolMd; /* a model's action and BVA */
typedef struct {                       /* PoltaArm */
    int left, alive, hidden, nrv, st, c8, cc, ctr;   /* mIsLeftArm, !isDead, hideModel, _C8 / _CC, mBrokenCounter */
    int noshadow;                        /* invalidateShadowAll (Repair, the power-up demo) */
    PolMd md; const Model *m;
    int br_on; PolMd br; float brm[16];  /* mBreakModel (PoltaArmBreak) on _98 */
    int fo_on; PolMd fo;                 /* mFormationModel (PoltaFormationRock) */
} PolArm;
typedef struct { int on, nrv, st, type; V3 pos, vel, ac, a0, x, y, z; int bind; u32 lv; } PolRock;
typedef struct { int on, nrv, st, inst, br, shown; V3 a4, pos, g, x, y, z; u32 lv; } PolGRock;
static struct {
    int row, seq, sst, hidden, coins;           /* coins: getDeclareRemnantCoinCount (declareCoin 20) */
    V3 pos, vel, g, U, F, c4, e0, U0, F0;        /* _B4 as its up / front, _C4, _E0, _D0 */
    float ec, scream;                            /* _EC, the scream sensor's radius */
    PolMd md; const Model *m;
    int smoke1, smoke2;
    int br_on; PolMd br;                         /* mBreakModel (PoltaBodyBreak) */
    int fo_on; PolMd fo;                         /* mFormationModel */
    PolArm arm[2];                               /* mLeftArm, mRightArm */
    PolSt dm; int dpend; const char *dname;      /* PoltaDemo: its nerve (TryStartDemo: mCurDemoNerve) */
    int lv2; PolSt bat; int health, c24;         /* PoltaBattleLv1 / Lv2: mPoltaHealth, _24 / _2C */
    PolSt stg; const char *stg_name; int stg_cw; /* PoltaStateStagger: mActionName, _20 */
    PolSt gra; int gra_n, gra_cw;                /* PoltaStateGroundRockAttack: _10, _14 */
    PolSt grk; int grk_pat, grk_idx, grk_max, grk_delay; /* PoltaStateGenerateRock */
    PolSt atk; int atk_len[3];                   /* PoltaStateAttackGround: AttackFrontStart / AttackFront / ToWait frames */
    PolSt pun; int pun_left;                     /* PoltaStatePunch: mIsLeftArmActor */
    PolRock rock[POL_ROCKS];
    PolGRock grock[POL_GROCKS]; PolMd gbr[POL_GBREAKS]; int gbr_of[POL_GBREAKS];
    int bombs;                                   /* its BombTeresas' first walker slot */
    V3 sc[POS_N]; float sr[POS_N];                 /* its sensors this frame */
    u32 lv[2];
    const char *cam; int cam_f;                  /* its anim camera running */
    int inst0;                                   /* the ground rocks' instances (game_rebind_) */
    const Model *rockm[3], *grm, *grbm, *brm, *abrm, *fom;
} pol = {.row = -1, .inst0 = -1};
static const struct { int j; float r, x, y, z; } pol_body_s[4] = {{18, 150, 0, 0, 0}, {3, 320, 100, -20, 0}, {4, 260, 180, 0, 0}, {8, 290, 320, 0, 0}};
static const struct { int j; float r, x; } pol_arm_s[11] = {{3, 140, 90}, {5, 140, 120}, {7, 240, 220}, {8, 140, 130}, {9, 120, 140}, {12, 140, 130},
                                                            {13, 120, 140}, {16, 140, 130}, {17, 120, 140}, {20, 140, 130}, {21, 120, 140}}; /* s*ArmSensorList */
static const int pol_rock_pat[10][7] = {{1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 0, 0, 1}, {0, 0, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0},
                                        {1, 0, 0, 1, 0, 0, 0}, {0, 1, 0, 0, 1, 0, 0}, {0, 0, 1, 0, 0, 1, 0}, {0, 0, 0, 1, 0, 0, 1}, {0, 0, 0, 0, 1, 0, 0}}; /* sRockPattern */
static float pol_rate(int st, int n) { return n <= 0 ? 1.0f : fminf(fmaxf((float)st / n, 0.0f), 1.0f); } /* calcNerveRate */
static float pol_frand(void) { return (float)(rand() % 10000) / 10000.0f; }
static void pol_set(PolSt *s, int n) { s->nrv = n; s->st = -1; }
#define POL_NRV(o, n) ((o)->nrv = (n), (o)->st = -1) /* a rock's setNerve */
static void pol_se(V3 p, u32 id) { se_start(id, &p.x); }
/* startAction on one of its models: "<model>/<clip>" (slice.py clip_key's shortening over 31); a BVA of that name
 * becomes the model's (frames with the clip), else the one it had stays (BreakLevel) */
static void pol_key(char *k, const char *mdl, const char *clip)
{
    char n[64];
    snprintf(n, sizeof n, "%s/%s", mdl, clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    static const char *const from[3] = {"poltaleftarm", "poltarightarm", "poltaformationrock"}, *const to[3] = {"pla", "pra", "pfr"};
    for (int i = 0; i < 3 && strlen(n) > 31; i++) {
        char *d = strstr(n, from[i]);
        if (d) memmove(d + strlen(to[i]), d + strlen(from[i]), strlen(d + strlen(from[i])) + 1), memcpy(d, to[i], strlen(to[i]));
    }
    snprintf(k, 32, "%s", n);
}
static int pol_has_bva(const Model *md, const char *key)
{
    const u8 *q = md ? md->bvas : NULL;
    for (u32 i = 0; q && i < md->nbvas; i++) {
        if (!strcmp((const char *)q, key)) return 1;
        q += 36 + 4u * *(const u16 *)(q + 32) * q[35];
    }
    return 0;
}
static void pol_act(PolMd *p, const Model *md, const char *mdl, const char *clip)
{
    if (!p->a) return;
    pol_key(p->key, mdl, clip);
    if (md && md->skin && p->a->skin != md->skin) anim_bind(p->a, md->skin);
    if (p->a->skin) { p->a->clip_id = -1; if (!anim_start(p->a, p->key, 0)) printf("polta: no clip %s\n", p->key); }
    if (pol_has_bva(md, p->key)) { snprintf(p->bva, sizeof p->bva, "%s", p->key); p->bva_clip = 1; }
}
static void pol_bva(PolMd *p, const char *mdl, const char *bva, float f) /* startBva + setBvaFrameAndStop */
{
    pol_key(p->bva, mdl, bva); p->bva_clip = 0; p->bva_f = f;
}
static int pol_end(const PolMd *p) { return !p->a || !p->a->skin || p->a->done; } /* isActionEnd */
static void pol_draw_md(const Model *md, const float m[16], PolMd *p, V3 at)
{
    if (!md || !p->a) return;
    if (md->skin && p->a->skin != md->skin) { anim_bind(p->a, md->skin); if (p->key[0]) anim_start(p->a, p->key, 0); }
    use_row_light(pol.row, at, LIGHT_STRONG);
    if (!md->skin || !p->a->skin) { render_model(md, m, 0x80, 0x80, 0x80); return; }
    if (p->bva[0]) { bva_key = p->bva; bva_frame = p->bva_clip ? p->a->frame : p->bva_f; }
    draw_skin(md, m, p->a);
}
static void pol_base(float m[16]) { basis_matrix(m, pol.pos, pol.U, pol.F, 0); } /* setBaseTRMtx(_B4) */
static V3 pol_joint(const PolMd *p, int j, V3 off) /* a joint's point (its offset in joint space) */
{
    float b[16], o[16];
    pol_base(b);
    if (!p->a || !p->a->skin || (u32)j >= p->a->skin->njoints) return pol.pos;
    mat_mul(o, b, p->a->world[j]);
    return v3(o[12] + o[0] * off.x + o[4] * off.y + o[8] * off.z, o[13] + o[1] * off.x + o[5] * off.y + o[9] * off.z,
              o[14] + o[2] * off.x + o[6] * off.y + o[10] * off.z);
}
static V3 pol_mc(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); } /* getPlayerCenterPos */
static int pol_bomb_slot(int k) { return pol.bombs + k; }
static Kuribo *pol_bomb(int k) { return &kuribo[pol_bomb_slot(k)]; }

/* ---- Polta: its pose, its turn to the player ---- */
static void pol_rotate_g(V3 *v, float deg) { *v = rot_axis(*v, vnorm(pol.g), deg * 0.0174533f); } /* rotateDirectionGravityDegree */
static int pol_face_player(V3 dir, float deg) /* isFaceToPlayerHorizontalDegree */
{
    V3 g = vnorm(pol.g), a = vtangent(vsub(mario->pos, pol.pos), g), b = vtangent(dir, g);
    if (vlen(a) < 1e-4f || vlen(b) < 1e-4f) return 0;
    return vdot(vnorm(a), vnorm(b)) >= cosf(deg * 0.0174533f);
}
static V3 pol_side(void) { return vcross(pol.U, vnorm(vtangent(pol.F, pol.U))); } /* calcSideVec: the base's X */
static void pol_rotate_to_player(void) /* rotateToPlayer / calcToPlayerRotateSpeed */
{
    float sp = pol.ec;
    if (pol_face_player(pol.c4, 10.0f)) sp *= 0.99f; /* sTurnBreakFric */
    else {
        sp += vdot(pol_side(), vsub(mario->pos, pol.pos)) < 0.0f ? 0.1f : -0.1f; /* sTurnAccel */
        sp *= 0.998f;                                                        /* sTurnFric */
    }
    pol.ec = fminf(fmaxf(sp, -1.1f), 1.1f);                                 /* sTurnMaxSpeed */
    pol_rotate_g(&pol.c4, pol.ec);
}
static void pol_blend(V3 *U, V3 *F, V3 up, V3 front, float ru, float rf) /* blendQuatUpFront */
{
    float c = vdot(*U, up);
    V3 ax = vcross(*U, up);
    if (vlen(ax) > 1e-6f) {
        float ang = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * ru;
        ax = vnorm(ax); *U = vnorm(rot_axis(*U, ax, ang)); *F = vnorm(rot_axis(*F, ax, ang));
    }
    V3 tf = vtangent(front, *U);
    if (vlen(tf) < 1e-6f) return;
    tf = vnorm(tf);
    c = vdot(*F, tf);
    float ang = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * rf;
    if (vdot(vcross(*F, tf), *U) < 0) ang = -ang;
    *F = vnorm(vtangent(rot_axis(*F, *U, ang), *U));
}
static void pol_update_pose(float ru, float rf) /* updatePose */
{
    pol.c4 = vtangent(pol.c4, vnorm(pol.g));
    pol.c4 = vlen(pol.c4) < 1e-4f ? pol.F : vnorm(pol.c4);
    pol_blend(&pol.U, &pol.F, vmul(vnorm(pol.g), -1.0f), pol.c4, ru, rf);
}
static void pol_start_pose(void) { pol.U = pol.U0; pol.F = pol.F0; pol.pos = pol.e0; pol.vel = v3(0, 0, 0); pol.c4 = pol.F; } /* setStartPose */
static void pol_star_pieces(int n) /* appearStarPiece: Head3, -gravity, 50 / 60 */
{
    launch_pieces(pol_joint(&pol.md, 8, v3(0, 0, 0)), vmul(vnorm(pol.g), -1.0f), n, 50.0f, 60.0f);
    pol_se(pol.pos, SE_OJ_STAR_PIECE_BURST);
}
static void pol_body_act(const char *clip) { pol_act(&pol.md, pol.m, "Polta", clip); }
static void pol_body_hp(int hp) /* PoltaFunction::setBodyHP: BreakLevel 3 - hp, the smoke */
{
    pol_bva(&pol.md, "Polta", "BreakLevel", 3.0f - hp);
    pol.smoke1 = hp == 2 || hp == 1; pol.smoke2 = hp == 1;
}
static void pol_break_model(const char *clip) /* appearBreakModel: PoltaBodyBreak on its base */
{
    pol.br_on = 1; pol_act(&pol.br, pol.brm, "PoltaBodyBreak", clip);
}

/* ---- PoltaArm ---- */
static const char *pol_arm_name(const PolArm *a) { return a->left ? "PoltaLeftArm" : "PoltaRightArm"; }
static void pol_arm_act(PolArm *a, const char *clip) { pol_act(&a->md, a->m, pol_arm_name(a), clip); }
static void pol_arm_kill(PolArm *a) { a->alive = 0; a->br_on = 0; } /* kill (makeActorDead: its break model too) */
static void pol_arm_nrv(PolArm *a, int n) { if (a->nrv == POA_REPAIR && n != POA_REPAIR) a->fo_on = 0; a->nrv = n; a->st = -1; } /* (Repair's onEnd: endRepair) */
static void pol_arm_break_model(PolArm *a) /* appearBreakModel: _98 at HandL / HandR, up -gravity */
{
    V3 h = pol_joint(&a->md, 7, v3(0, 0, 0)), up = vmul(vnorm(pol.g), -1.0f);
    basis_matrix(a->brm, h, up, pol.F, 0); /* makeMtxUpNoSupportPos */
    a->br_on = 1; pol_act(&a->br, pol.abrm, "PoltaArmBreak", "Break");
}
static int pol_arm_sensor_ok(const PolArm *a) { return a->alive && (a->nrv == POA_CONTROLLED || a->nrv == POA_WAITDAMAGEEND || a->nrv == POA_WAITREPAIREND); } /* isEnableHitSensor */
static int pol_arm_attack_ok(const PolArm *a) { return a->alive && a->nrv == POA_CONTROLLED; } /* isEnableAttack */
static int pol_arm_broken(const PolArm *a) { return !a->alive || a->nrv == POA_BREAK || a->nrv == POA_BREAKBODY || a->nrv == POA_BROKEN; }
static void pol_arm_start(PolArm *a) /* start */
{
    a->alive = 1; a->hidden = 0;
    pol_arm_act(a, "Wait");
    pol_bva(&a->md, pol_arm_name(a), "BreakLevel", 0.0f);
    a->ctr = 0; a->c8 = a->cc;
    pol_arm_nrv(a, POA_CONTROLLED);
}
static void pol_arm_break_body(PolArm *a) /* requestBreakBody */
{
    if (!a->alive) return;
    if (a->nrv == POA_BROKEN) { pol_arm_kill(a); return; }
    pol_arm_nrv(a, POA_BREAKBODY);
}
static void pol_arm_damage(PolArm *a) /* requestDamage */
{
    if (!pol_arm_sensor_ok(a)) return;
    if (--a->c8 <= 0) { a->c8 = 0; a->ctr = 0; pol_arm_nrv(a, POA_BREAK); }
    else pol_arm_nrv(a, POA_DAMAGE);
}
static void pol_arm_start_control(PolArm *a) { if (a->alive && (a->nrv == POA_WAITREPAIREND || a->nrv == POA_WAITDAMAGEEND)) pol_arm_nrv(a, POA_CONTROLLED); } /* requestStartControll */
static void pol_arm_controlled(PolArm *a, const char *clip) { if (a->alive && a->nrv == POA_CONTROLLED) pol_arm_act(a, clip); } /* requestControlled */
static void pol_arm_update(PolArm *a)
{
    if (!a->alive) return;
    if (a->br_on && pol_end(&a->br)) a->br_on = 0; /* control */
    int st = ++a->st;
    V3 at = pol_joint(&a->md, 7, v3(0, 0, 0));
    switch (a->nrv) {
    case POA_DAMAGE:
        if (st == 0) {
            pol_arm_act(a, "Damage");
            pol_bva(&a->md, pol_arm_name(a), "BreakLevel", (1.0f - (float)a->c8 / (float)a->cc) * 2.0f);
            pol_se(at, SE_BM_POLTA_ROCK_DAMAGE); pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); scene_stop = 5;
            pol_arm_break_model(a);
            printf("polta: %s arm damaged (%d left)\n", a->left ? "left" : "right", a->c8);
        }
        if (pol_end(&a->md)) { pol_arm_act(a, "DamageWait"); pol_arm_nrv(a, POA_WAITDAMAGEEND); }
        break;
    case POA_BREAK:
        if (st == 0) {
            pol_arm_act(a, "Break");
            pol_se(at, SE_BM_POLTA_ROCK_BREAK); pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); scene_stop = 5;
            pol_arm_break_model(a);
            printf("polta: %s arm broken\n", a->left ? "left" : "right");
        }
        if (pol_end(&a->md)) { a->hidden = 1; pol_arm_nrv(a, POA_BROKEN); }
        break;
    case POA_BREAKBODY:
        if (st == 0) { pol_arm_act(a, "BreakBody"); pol_arm_break_model(a); }
        if (pol_end(&a->md)) pol_arm_kill(a);
        break;
    case POA_REPAIR:
        if (st == 0) {
            a->hidden = 0; a->noshadow = 1; /* invalidateShadowAll, showModel */
            pol_arm_act(a, "Repair");
            a->fo_on = 1; pol_act(&a->fo, pol.fom, "PoltaFormationRock", a->left ? "RepairArmL" : "RepairArmR");
            a->c8 = a->cc;
            printf("polta: %s arm repairs\n", a->left ? "left" : "right");
        }
        if (pol_end(&a->md)) {
            pol_arm_act(a, "RepairWait"); a->noshadow = 0; /* validateShadowAll */
            pol_bva(&a->md, pol_arm_name(a), "BreakLevel", 0.0f);
            pol_arm_nrv(a, POA_WAITREPAIREND);
        }
        break;
    case POA_BROKEN:
        a->ctr++; /* (_D4 never set) */
        if (a->ctr > 1200) pol_arm_nrv(a, POA_REPAIR);
        break;
    }
}

/* ---- PoltaRock / PoltaGroundRock ---- */
static int pol_living_bombs(void) { int n = 0; for (int k = 0; k < POL_BOMBS; k++) n += pol_bomb(k)->state != KURIBO_DEAD; return n; }
static int pol_bomb_appear(V3 p, V3 vel, int shadow) /* BombTeresa::appearNormal / appearShadow */
{
    for (int k = 0; k < POL_BOMBS; k++) {
        Kuribo *b = pol_bomb(k);
        if (b->state != KURIBO_DEAD) continue;
        V3 up = up_at(p, vmul(vnorm(pol.g), -1.0f));
        b->state = KURIBO_WALK; b->pos = p; b->vel = vel; b->grav = vmul(up, -1.0f); b->up = up;
        b->qy = up; b->qz = vnorm(vtangent(pol.F, up)); b->front = b->qz; b->tcenter = p; b->bpos = p; b->ttarget = p;
        b->lvt[0] = shadow; b->lvt[1] = 0; b->lvt[2] = 0; b->from = 0.0f; b->on_ground = 0; b->on_wall = 0;
        w_nrv(b, shadow ? BT_SHADOWAPPEAR : BT_APPEAR);
        printf("polta: bomb teresa %d out (%s)\n", k, shadow ? "shadow" : "normal");
        return 1;
    }
    return 0;
}
static void pol_rock_kill(PolRock *r) /* PoltaRock::kill */
{
    float m[16];
    pol_se(r->pos, SE_OJ_POLTA_ROCK_BREAK);
    r->on = 0;
    basis_matrix(m, r->pos, r->y, r->z, 0);
    if (r->type == 2) fx_named("PoltaRock", "BreakYellow", m, NULL, NULL);
    if (r->type == 0) fx_named("PoltaRock", "BreakWhite", m, NULL, NULL);
}
static void pol_rock_generate_kill(PolRock *r) /* generateKill */
{
    if (r->type == 1) pol_bomb_appear(r->pos, v3(0, 0, 0), 0);
    if (r->type == 2) { pop_coins(r->pos, vmul(vnorm(pol.g), -1.0f), 1); if (pol.coins > 0) pol.coins--; }
    pol_rock_kill(r);
}
static int pol_rock_attack_ok(const PolRock *r) { return r->nrv == POR_MOVE; }
static int pol_rock_explosion_ok(const PolRock *r) { return !(r->nrv == POR_FLOAT && r->st < 200); }
static int pol_rock_circle(V3 c, float radius, int idx, int type) /* PoltaFunction::appearRockCircle */
{
    PolRock *r = NULL;
    for (int i = 0; i < POL_ROCKS && !r; i++) if (!pol.rock[i].on) r = &pol.rock[i];
    if (!r) return 0;
    float ang = (idx % 2 ? 1.0f : -1.0f) * (15.0f + 7.0f * (idx + 1));
    V3 d = pol.c4;
    pol_rotate_g(&d, ang);
    d = vtangent(d, vnorm(pol.g));
    if (vlen(d) > 1e-6f) d = vnorm(d);
    memset(r, 0, sizeof *r);
    r->type = type; r->on = 1;
    r->pos = vadd(c, vmul(d, radius));
    r->ac = v3(radius * -sinf(ang * 0.0174533f), 0.0f, radius * cosf(ang * 0.0174533f)); /* _AC: in its base */
    r->y = vmul(vnorm(pol.g), -1.0f); r->z = vnorm(vtangent(pol.F, r->y)); r->x = vcross(r->y, r->z); /* _90 */
    r->bind = 0; POL_NRV(r, POR_FLOAT);
    return 1;
}
static float pol_shadow_len(V3 p, V3 g) /* getShadowNearProjectionLength (ponytail: a 3000 line along gravity) */
{
    V3 hp;
    return kcl_line(p, vadd(p, vmul(vnorm(g), 3000.0f)), &hp, NULL, NULL) ? vlen(vsub(hp, p)) : -1.0f;
}
static void pol_rock_float(PolRock *r, V3 g) /* exeFloat / exeSign's motion */
{
    float b[16];
    pol_base(b);
    V3 t = v3(b[0] * r->ac.x + b[4] * r->ac.y + b[8] * r->ac.z + b[12], b[1] * r->ac.x + b[5] * r->ac.y + b[9] * r->ac.z + b[13],
              b[2] * r->ac.x + b[6] * r->ac.y + b[10] * r->ac.z + b[14]); /* PSMTXMultVec(owner's base, _AC) */
    V3 h = vtangent(vsub(t, r->pos), vnorm(g)); /* addVelocityMoveToTarget(t, 0, 1.2, 0, 200) */
    float d = vlen(h);
    if (d > 1e-6f) r->vel = vadd(r->vel, vmul(vnorm(h), 1.2f * bt_norm(d, 0.0f, 200.0f)));
    float sl = pol_shadow_len(r->pos, g); /* addVelocityKeepHeightUseShadow(800, 1.5, 300) */
    if (sl >= 0.0f) r->vel = vadd(r->vel, vmul(vnorm(g), 1.5f * bt_nabs(sl - 800.0f, -300.0f, 300.0f)));
    r->vel = vmul(r->vel, 0.92f);
}
static void pol_grock_break(PolGRock *gr);
static int pol_grock_of_inst(int i) { return pol.inst0 >= 0 && i >= pol.inst0 && i < pol.inst0 + POL_GROCKS ? i - pol.inst0 : -1; }
static void pol_rock_update(PolRock *r)
{
    V3 g = km_g(r->pos);
    int st = ++r->st;
    switch (r->nrv) {
    case POR_FLOAT:
        if (st == 0) {
            float m[16];
            pol_se(r->pos, SE_OJ_POLTA_ROCK_FLOAT_ST);
            basis_matrix(m, r->pos, r->y, r->z, 0); fx_named("PoltaRock", "Appear", m, NULL, NULL);
        }
        r->lv = se_level(SE_OJ_LV_POLTA_ROCK_FLOAT, &r->pos.x, r->lv);
        pol_rock_float(r, g);
        if (st > 90) { POL_NRV(r, POR_SIGN); r->bind = 1; }
        break;
    case POR_SIGN:
        pol_rock_float(r, g);
        if (st > 60) { POL_NRV(r, POR_MOVE); r->bind = 1; }
        break;
    case POR_MOVE:
        if (st == 0) {
            r->a0 = vsub(pol_mc(), r->pos); r->a0 = vlen(r->a0) > 1e-6f ? vnorm(r->a0) : r->a0;
            pol_se(r->pos, SE_OJ_POLTA_ROCK_FLY); r->vel = v3(0, 0, 0);
        }
        if (st < 30) r->vel = vadd(r->vel, vmul(r->a0, 3.0f * pol_rate(st, 30)));
        r->vel = vadd(r->vel, vmul(vnorm(g), -0.02f)); /* addVelocityToGravity(-0.02) */
        r->vel = vmul(r->vel, 0.999f);
        { /* rotateQuatRollBall(_90, velocity, -gravity, 120) */
            V3 ax = vcross(vmul(vnorm(g), -1.0f), r->vel);
            float a = vlen(r->vel) / 120.0f;
            if (vlen(ax) > 1e-6f) { ax = vnorm(ax); r->x = rot_axis(r->x, ax, a); r->y = rot_axis(r->y, ax, a); r->z = rot_axis(r->z, ax, a); }
        }
        break;
    }
    /* its binder (120), off until Sign */
    V3 c = vadd(r->pos, r->vel), up = vmul(vnorm(g), -1.0f), gn = up, wn = up;
    int wall = 0, ground = 0;
    if (r->bind) { wall_inst = -1; ground = collide_n(&c, up, 120.0f, &gn, &wn, &wall); }
    int hit = ground ? ground_inst : wall ? wall_inst : -1;
    r->pos = c;
    if (r->nrv == POR_MOVE && r->st >= 0 && (ground || wall)) { /* isBinded: sendMsgEnemyAttackToBindedSensor, tryRumbleDefaultHit */
        int gk = hit >= 0 ? pol_grock_of_inst(hit) : -1;
        if (gk >= 0 && pol.grock[gk].nrv != POG_BREAK) pol_grock_break(&pol.grock[gk]);
        pad_rumble(RUMBLE_MIDDLE);
        pol_rock_generate_kill(r);
        return;
    }
    if (r->nrv == POR_MOVE && st > 300) { pol_rock_kill(r); return; }
    /* attackSensor: Mario */
    if (vlen(vsub(pol_mc(), r->pos)) < 120.0f + HIT_BODY_R) {
        if (pol_rock_attack_ok(r)) { if (!mario->hurt_timer && mario_attack(ATK_ENEMY, r->pos)) { pol_rock_generate_kill(r); return; } }
        else mario_push_out(r->pos, 120.0f);
    }
    /* receiveMsgPlayerAttack: his spin while it flies */
    if (pol_rock_attack_ok(r) && mario_offence(r, r->pos, 120.0f, g) == MSG_HITALL) { m_punch(r, r->pos, 0); r->vel = v3(0, 0, 0); pol_rock_generate_kill(r); }
}
static void pol_grock_start(PolGRock *gr, V3 p) /* PoltaGroundRock::start */
{
    POL_NRV(gr, POG_SIGN);
    gr->a4 = p; gr->g = km_g(p);
    gr->pos = vadd(p, vmul(vnorm(gr->g), 500.0f));
    gr->y = vmul(vnorm(gr->g), -1.0f);
    V3 f = vnorm(vtangent(fabsf(gr->y.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0), gr->y)); /* makeMtxUpNoSupportPos */
    gr->z = rot_axis(f, gr->y, pol_frand() * 6.2831853f); gr->x = vcross(gr->y, gr->z); /* rotateMtxLocalY(random 2 pi) */
    gr->on = 1; gr->shown = 1; gr->br = -1;
}
static int pol_grock_attack_ok(const PolGRock *gr) { return gr->on && gr->nrv == POG_JUT; }
static int pol_grock_break_ok(const PolGRock *gr) { return gr->nrv != POG_BREAK; }
static void pol_grock_break(PolGRock *gr) { if (pol_grock_break_ok(gr)) POL_NRV(gr, POG_BREAK); } /* requestBreak */
static V3 pol_grock_body(const PolGRock *gr) { return vadd(gr->pos, vmul(gr->y, 260.0f)); } /* its "body" sensor (170) */
static void pol_rumble(V3 p, int strong) /* tryRumblePadAndCameraDistanceStrong / Middle(800, 1200, 2000) */
{
    float d = vlen(vsub(mario->pos, p));
    if (strong) {
        if (d < 800.0f) { pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL); }
        else if (d < 1200.0f) { pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); }
        else if (d < 2000.0f) { pad_rumble(RUMBLE_WEAK); camera_shake(SHAKE_WEAK); }
    } else {
        if (d < 800.0f) { pad_rumble(RUMBLE_MIDDLE); camera_shake(SHAKE_NORMAL_WEAK); }
        else if (d < 1200.0f) { pad_rumble(RUMBLE_WEAK); camera_shake(SHAKE_WEAK); }
        else if (d < 2000.0f) { pad_rumble(RUMBLE_VERY_WEAK); camera_shake(SHAKE_VERY_WEAK); }
    }
}
static void pol_grock_mtx(const PolGRock *gr, float m[16])
{
    mat_identity(m);
    m[0] = gr->x.x; m[1] = gr->x.y; m[2] = gr->x.z; m[4] = gr->y.x; m[5] = gr->y.y; m[6] = gr->y.z;
    m[8] = gr->z.x; m[9] = gr->z.y; m[10] = gr->z.z; m[12] = gr->pos.x; m[13] = gr->pos.y; m[14] = gr->pos.z;
}
static void pol_grock_update(PolGRock *gr, int k)
{
    int st = ++gr->st;
    float m[16];
    switch (gr->nrv) {
    case POG_SIGN:
        basis_matrix(m, gr->a4, gr->y, gr->z, 0); fx_named("PoltaGroundRock", "Shadow", m, NULL, &gr->a4); /* setEffectHostSRT _A4 */
        gr->lv = se_level(SE_OJ_LV_POLTA_G_ROCK_SIGN, &gr->a4.x, gr->lv);
        if (st > 60) POL_NRV(gr, POG_JUT);
        break;
    case POG_JUT:
        if (st == 0) pol_se(gr->a4, SE_OJ_POLTA_G_ROCK_JUT);
        gr->pos = vadd(gr->a4, vmul(vnorm(gr->g), 500.0f * (1.0f - sinf(pol_rate(st, 20) * 1.5707963f)))); /* calcNerveEaseOutValue(20, 500, 0) */
        if (st > 20) { pol_rumble(gr->pos, 0); POL_NRV(gr, POG_WAIT); }
        break;
    case POG_WAIT:
        if (st == 0) gr->pos = gr->a4;
        pol_grock_mtx(gr, m); fx_named("PoltaGroundRock", "Rock", m, NULL, &gr->pos);
        break;
    case POG_BREAK:
        if (st == 0) {
            gr->br = -1;
            for (int i = 0; i < POL_GBREAKS && gr->br < 0; i++) if (pol.gbr_of[i] < 0) gr->br = i;
            if (gr->br >= 0) { pol.gbr_of[gr->br] = k; pol_act(&pol.gbr[gr->br], pol.grbm, "PoltaGroundRockBreak", "Break"); }
            pol_se(gr->pos, SE_OJ_POLTA_G_ROCK_BREAK);
            gr->shown = 0; /* hideModel, invalidateCollisionParts */
            pol_rumble(gr->pos, 1);
            printf("polta: ground rock %d breaks\n", k);
        }
        if (gr->br < 0 || pol_end(&pol.gbr[gr->br])) { if (gr->br >= 0) pol.gbr_of[gr->br] = -1; gr->br = -1; gr->on = 0; }
        break;
    }
    if (!gr->on) return;
    /* attackSensor: in Jut its body hurts Mario (strong), breaks the other ground rocks it touches */
    if (pol_grock_attack_ok(gr)) {
        V3 b = pol_grock_body(gr);
        if (vlen(vsub(pol_mc(), b)) < 170.0f + HIT_BODY_R && !mario->hurt_timer) mario_attack(ATK_STRONG, b);
        for (int j = 0; j < POL_GROCKS; j++)
            if (j != k && pol.grock[j].on && vlen(vsub(pol_grock_body(&pol.grock[j]), b)) < 340.0f) pol_grock_break(&pol.grock[j]);
    }
    /* receiveMsgPlayerAttack: a spin on its collision (ponytail: at its body sensor) */
    if (gr->shown && pol_grock_break_ok(gr) && mario_offence(gr, pol_grock_body(gr), 170.0f, gr->g) == MSG_HITALL) { m_punch(gr, pol_grock_body(gr), 0); pol_grock_break(gr); }
}
static int pol_inst_explosion(int i) /* sendMsgEnemyAttackExplosionToBindedSensor on a ground rock's collision: Break */
{
    int k = pol_grock_of_inst(i);
    if (k < 0 || !pol.grock[k].on || !pol_grock_break_ok(&pol.grock[k])) return 0;
    pol_grock_break(&pol.grock[k]);
    return 1;
}
static int pol_grocks_dead(void) { int n = 0; for (int i = 0; i < POL_GROCKS; i++) n += !pol.grock[i].on; return n; } /* getCountDeadGroundRock */
static int pol_grock_appear(float dist, float ang) /* PoltaFunction::appearGroundRock */
{
    for (int i = 0; i < POL_GROCKS; i++) {
        if (pol.grock[i].on) continue;
        V3 d = vtangent(vsub(mario->pos, pol.pos), vnorm(pol.g));
        d = vlen(d) < 1e-6f ? pol.c4 : vnorm(d);
        pol_rotate_g(&d, ang);
        pol_grock_start(&pol.grock[i], vadd(pol.pos, vmul(d, dist)));
        return 1;
    }
    return 0;
}

/* ---- PoltaFunction ---- */
static void pol_on_movement(void) { pol.smoke1 = pol.smoke2 = 0; } /* forceDeleteEffectAll; requestMovementOn (its demo moves it) */
static void pol_start_action(const char *clip, int body) /* PoltaFunction::startAction */
{
    if (body) pol_body_act(clip);
    pol_arm_controlled(&pol.arm[0], clip); pol_arm_controlled(&pol.arm[1], clip);
}
static void pol_start_control(void) { pol_arm_start_control(&pol.arm[0]); pol_arm_start_control(&pol.arm[1]); }
static void pol_rock_kill(PolRock *r);
static void pol_kill_rocks(void) { for (int i = 0; i < POL_ROCKS; i++) if (pol.rock[i].on) pol_rock_kill(&pol.rock[i]); } /* killAll: PoltaRock::kill */
static void pol_kill_grocks(void) { for (int i = 0; i < POL_GROCKS; i++) pol.grock[i].on = 0; for (int i = 0; i < POL_GBREAKS; i++) pol.gbr_of[i] = -1; }
static void pol_break_grocks(void) { for (int i = 0; i < POL_GROCKS; i++) if (pol.grock[i].on) pol_grock_break(&pol.grock[i]); } /* breakAll */
static void pol_disperse_bombs(void) /* BombTeresaHolder::disperseAll: requestDisperse */
{
    for (int k = 0; k < POL_BOMBS; k++) {
        Kuribo *b = pol_bomb(k);
        if (b->state == KURIBO_DEAD || b->nrv == BT_EXPLOSION || b->nrv == BT_SHOCK || b->nrv == BT_DISPERSE) continue;
        w_nrv(b, BT_DISPERSE);
    }
}
static void pol_kill_bombs(void) { for (int k = 0; k < POL_BOMBS; k++) pol_bomb(k)->state = KURIBO_DEAD; }
static int pol_is_body(int s) { return s >= POS_BODY && s < POS_LARM; }
static int pol_is_larm(int s) { return s >= POS_LARM && s < POS_RARM; }
static int pol_is_rarm(int s) { return s >= POS_RARM && s < POS_SCREAM; }
static int pol_is_arm(int s) { return pol_is_larm(s) || pol_is_rarm(s); }

/* ---- PoltaActionBase ---- */
static void pol_update_wait(void) /* updateWait */
{
    if (pol.bat.st == 0) pol_start_action("Wait", 1);
    pol.lv[0] = se_level(SE_BM_LV_POLTA_IN_BATTLE_ROCK, &pol.pos.x, pol.lv[0]);
    pol_rotate_to_player();
    pol_start_control();
}
static int pol_update_damage_body(int first) /* updateDamageBody */
{
    if (pol.bat.st == 0) {
        pol_start_action("Damage", 1);
        se_start(SE_SY_VS_BOSS_DAMAGE_1, NULL); pol_se(pol.pos, SE_BM_POLTA_ROCK_DAMAGE); pol_se(pol.pos, SE_BV_POLTA_DAMAGE_BODY);
        pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); scene_stop = 3;
        pol.ec = 0.0f;
        pol_break_model(first ? "BreakFirst" : "BreakSecond");
    }
    pol.lv[0] = se_level(SE_BM_LV_POLTA_IN_BATTLE_ROCK, &pol.pos.x, pol.lv[0]);
    if (pol.br_on && pol_end(&pol.br)) pol.br_on = 0;
    if (pol_end(&pol.md)) { pol.br_on = 0; pol.scream = 0.0f; return 1; }
    return 0;
}
static void pol_update_scream(void) { pol.scream = pol.bat.st < 30 ? 3200.0f * pol_rate(pol.bat.st, 30) : 0.0f; } /* updateScreamSensor */
static int pol_update_break_body(void) /* updateBreakBody */
{
    if (pol.bat.st == 0) {
        pol_start_action("BreakBody", 1);
        se_start(SE_SY_VS_BOSS_DAMAGE_1, NULL); pol_se(pol.pos, SE_BM_POLTA_ROCK_BREAK); pol_se(pol.pos, SE_BV_POLTA_LOSE_SHELL);
        pol_arm_break_body(&pol.arm[0]); pol_arm_break_body(&pol.arm[1]);
        pol_kill_rocks(); pol_break_grocks();
        pad_rumble(RUMBLE_STRONG); pol.vel = v3(0, 0, 0); camera_shake(SHAKE_NORMAL_STRONG); scene_stop = 3;
        pol.ec = 0.0f;
        pol_break_model("BreakThird");
    }
    pol_update_scream();
    if (pol.br_on && pol_end(&pol.br)) pol.br_on = 0;
    if (pol_end(&pol.md)) { pol.br_on = 0; pol.scream = 0.0f; return 1; }
    return 0;
}
static int pol_update_damage_core(void) /* updateDamageCore */
{
    if (pol.bat.st == 0) {
        pol_start_action("DamageCore", 1);
        pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); scene_stop = 3;
        pol.ec = 0.0f; pol.vel = v3(0, 0, 0);
        pol_disperse_bombs();
    }
    if (pol_end(&pol.md)) { pol_se(pol.pos, SE_BM_POLTA_CORE_DOWN); pol_kill_bombs(); pol.vel = v3(0, 0, 0); return 1; }
    return 0;
}

/* ---- the states ---- */
static void pol_stagger(void) /* PoltaStateStagger::exeWait */
{
    int st = ++pol.stg.st;
    if (st == 0) { pol.vel = v3(0, 0, 0); pol_body_act(pol.stg_name); }
    V3 d = vsub(pol.e0, pol.pos), g = vnorm(pol.g);
    float l = vlen(d);
    d = l > 1e-6f ? vmul(d, 1.0f / l) : v3(0, 0, 0); /* separateScalarAndDirection */
    if (l >= 1800.0f) {
        pol.vel = vadd(pol.vel, vmul(d, l - 1800.0f));
        V3 cw = vcross(pol.g, vsub(mario->pos, pol.pos)); /* isClockwiseToPlayer */
        pol.stg_cw = vdot(vlen(cw) > 1e-6f ? vnorm(cw) : cw, d) >= 0.0f;
    }
    if (vlen(vtangent(vsub(pol.pos, mario->pos), g)) < 1500.0f) { /* isNearPlayerHorizontal: addVelocityAwayFromTarget(0.7) */
        V3 a = vtangent(vsub(pol.pos, mario->pos), g);
        if (vlen(a) > 1e-6f) pol.vel = vadd(pol.vel, vmul(vnorm(a), 0.7f));
    }
    { /* addVelocityClockwiseToPlayer(+-0.8) */
        V3 a = vsub(mario->pos, pol.pos);
        if (vlen(a) > 1e-6f) {
            V3 c = vtangent(vcross(pol.g, vnorm(a)), g);
            if (vlen(c) > 1e-6f) pol.vel = vadd(pol.vel, vmul(vnorm(c), pol.stg_cw ? 0.8f : -0.8f));
        }
    }
    pol.vel = vmul(pol.vel, 0.9f);
    pol.vel = vadd(pol.vel, vmul(g, 0.5f * bt_nabs(vdot(g, vsub(pol.e0, pol.pos)), -50.0f, 50.0f))); /* addVelocityKeepHeight(_E0, 0.5, 50) */
    pol_rotate_to_player();
    if (st % 210 == 0 && pol_bomb_appear(vadd(pol.pos, vmul(g, -120.0f)), v3(0, 0, 0), 1)) pol_se(pol.pos, SE_BM_POLTA_GEN_BOMB_TERESA); /* appearBombTeresaFromRoot */
}
static int pol_grock_attack(void) /* PoltaStateGroundRockAttack: 1 dead */
{
    PolSt *s = &pol.gra;
    int st = ++s->st;
    switch (s->nrv) {
    case 0: /* Sign */
        if (st == 0) { pol_start_control(); pol_start_action("GenerateGroundRockSign", 1); pol_se(pol.pos, SE_BV_POLTA_PREP_GND_ROCK); }
        pol_rotate_to_player();
        if (pol_end(&pol.md)) pol_set(s, 1);
        break;
    case 1: /* Generate */
        if (st == 0) {
            pol_start_action("GenerateGroundRockStart", 1); pol.vel = v3(0, 0, 0);
            V3 v2 = vsub(mario->pos, pol.pos), c = vcross(pol.g, v2); /* isClockwiseToDir(getPlayerLastMove, player - pos) */
            pol.gra_cw = vdot(vlen(c) > 1e-6f ? vnorm(c) : c, mario->hvel) >= 0.0f;
        }
        if (pol.md.a && pol.md.a->skin && !pol.md.a->loop && pol.md.a->done) { /* isBckOneTimeAndStopped */
            pol_start_action("GenerateGroundRock", 1);
            pol_se(pol.pos, SE_BV_POLTA_GEN_GND_ROCK); pol_se(pol.pos, SE_BM_POLTA_GEN_GND_ROCK);
        }
        if (st <= 15 * (pol.gra_n - 1) && st % 15 == 0) {
            int a = st / 15, b = a % 4;
            if (a > 0 && !b) pol.gra_cw = !pol.gra_cw;
            pol_grock_appear(850.0f + 400.0f * b, pol.gra_cw ? 20.0f : -20.0f);
        }
        if (st > 15 * pol.gra_n + 90) pol_set(s, 2);
        break;
    case 2: /* End */
        if (st == 0) pol_start_action("GenerateGroundRockEnd", 1);
        if (pol_end(&pol.md)) s->dead = 1;
        break;
    }
    return s->dead;
}
static int pol_generate_rock(void) /* PoltaStateGenerateRock: 1 dead */
{
    PolSt *s = &pol.grk;
    int st = ++s->st;
    switch (s->nrv) {
    case 0: /* Sign */
        if (st == 0) { pol_start_control(); pol_start_action("GenerateRockStart", 1); pol_se(pol.pos, SE_BV_POLTA_GEN_ROCK); }
        pol_rotate_to_player();
        if (pol_end(&pol.md)) pol_set(s, 1);
        break;
    case 1: /* Generate */
        if (st == 0) { pol_start_action("GenerateRock", 1); pol.vel = v3(0, 0, 0); pol.grk_pat = rand() % 9; } /* getRandom(0, 9) */
        pol_rotate_to_player();
        if (st % 30 == 0) {
            int pi = pol.grk_idx % 7;
            if (pol_rock_pat[pol.grk_pat][pi] && pol_living_bombs() < POL_BOMBS) pol_rock_circle(pol.pos, 600.0f, pi, 1);
            else if (pol_frand() < 0.08f && pol.coins > 0) pol_rock_circle(pol.pos, 600.0f, pi, 2);
            else pol_rock_circle(pol.pos, 600.0f, pi, 0);
            pol.grk_idx++;
        }
        if (pol.grk_idx >= pol.grk_max) pol_set(s, 2);
        break;
    case 2: /* End */
        if (st == pol.grk_delay) pol_start_action("GenerateRockToWait", 1);
        pol_rotate_to_player();
        if (st > pol.grk_delay && pol_end(&pol.md)) s->dead = 1;
        break;
    }
    return s->dead;
}
static int pol_attack_ground(void) /* PoltaStateAttackGround: 1 dead */
{
    PolSt *s = &pol.atk;
    int st = ++s->st, can = pol_arm_attack_ok(&pol.arm[0]) || pol_arm_attack_ok(&pol.arm[1]);
    switch (s->nrv) {
    case 0: /* Start */
        if (st == 0) { pol_start_control(); pol_start_action("AttackFrontStart", 1); pol_se(pol.pos, SE_BV_POLTA_PREP_ARM_DOWN); pol_se(pol.pos, SE_BM_POLTA_ARM_LIFT_UP); }
        if (st < 60) pol_rotate_to_player();
        if (!can) s->dead = 1;
        else if (st > pol.atk_len[0]) pol_set(s, 1);
        break;
    case 1: /* Attack */
        if (st == 0) { pol_start_action("AttackFront", 1); pol_se(pol.pos, SE_BV_POLTA_ARM_DOWN); pol_se(pol.pos, SE_BM_POLTA_ARM_SWING_DOWN); }
        if (!can) s->dead = 1;
        else if (st > pol.atk_len[1]) { pol_set(s, 2); pad_rumble(RUMBLE_STRONG); pol_se(pol.pos, SE_BM_POLTA_HIT_GROUND); camera_shake(SHAKE_NORMAL_STRONG); }
        break;
    case 2: /* ToWait */
        if (st == 0) pol_start_action("AttackFrontToWait", 1);
        if (st > pol.atk_len[2]) s->dead = 1;
        break;
    }
    return s->dead;
}
static int pol_punch(void) /* PoltaStatePunch: 1 dead */
{
    PolSt *s = &pol.pun;
    PolArm *a = &pol.arm[pol.pun_left ? 0 : 1];
    int st = ++s->st, ok = pol_arm_attack_ok(a); /* isEnablePunchArm */
    switch (s->nrv) {
    case 0: /* Start */
        if (st == 0) { pol_start_control(); pol_start_action(pol.pun_left ? "PunchLeftStart" : "PunchRightStart", 1); pol_se(pol.pos, SE_BV_POLTA_PREP_PUNCH); }
        if (st < 60) pol_rotate_to_player();
        if (!ok) s->dead = 1;
        else if (pol_end(&a->md)) pol_set(s, 1);
        break;
    case 1: /* Attack */
        if (st == 0) { pol_start_action(pol.pun_left ? "PunchLeft" : "PunchRight", 1); pol_se(pol.pos, SE_BV_POLTA_PUNCH); pol_se(pol.pos, SE_BM_POLTA_PUNCH); }
        if (!ok) s->dead = 1;
        else if (pol_end(&a->md)) pol_set(s, 2);
        break;
    case 2: /* ToWait */
        if (st == 0) pol_start_action(pol.pun_left ? "PunchLeftToWait" : "PunchRightToWait", 1);
        if (!ok) { s->dead = 1; break; }
        if (st > 70) pol_rotate_to_player();
        if (pol_end(&a->md)) s->dead = 1;
        break;
    }
    return s->dead;
}
static void pol_state_appear(PolSt *s) { s->dead = 0; pol_set(s, 0); } /* appear */
static int pol_state(PolSt *s, int (*fn)(void)) /* MR::updateActorState: appear at the battle's first step */
{
    if (pol.bat.st == 0) {
        pol_state_appear(s);
        if (s == &pol.grk) pol.grk_idx = 0;
    }
    return fn();
}

/* ---- PoltaBattleLv1 / Lv2 ---- */
static void pol_bat_set(int n) /* setNerve (Stagger's onEnd: endStagger) */
{
    if (pol.bat.nrv == POB_STAGGER && n != POB_STAGGER) bgm_state(1, 90);
    pol_set(&pol.bat, n);
}
static int pol_enable_damage(void) { return pol.bat.nrv != POB_BREAKBODY && pol.bat.nrv != POB_DAMAGECORE; }
static int pol_enable_scream(void) { return pol.bat.nrv == POB_BREAKBODY; }
static int pol_sensor_ok(int s) /* isEnableSensor */
{
    if (pol.bat.nrv == POB_STAGGER && pol.stg.nrv != 0) return 0;
    if (pol_is_body(s)) return pol.health > 0;
    if (s == POS_CORE || s == POS_HEAD) return pol.health <= 0;
    if (pol.lv2 && pol_is_larm(s)) return pol_arm_sensor_ok(&pol.arm[0]);
    if (pol.lv2 && pol_is_rarm(s)) return pol_arm_sensor_ok(&pol.arm[1]);
    return 0;
}
static int pol_attack_ok(int s) /* PoltaBattleLv2::isEnableAttack */
{
    if (pol_is_body(s)) return 1;
    if (pol.bat.nrv == POB_ATTACKGROUND && !pol_is_body(s) && pol.atk.nrv == 1) return 1;
    if (pol.bat.nrv == POB_PUNCH && pol.pun.nrv == 1 && (pol.pun_left ? pol_is_larm(s) : pol_is_rarm(s))) return 1;
    if (pol.bat.nrv == POB_STAGGER && (s == POS_CORE || s == POS_HEAD)) return 1;
    return 0;
}
static int pol_in_battle(void) { return (pol.seq == POQ_LV1 || pol.seq == POQ_LV2) && !pol.bat.dead; }
static void pol_battle_appear(int lv2) /* PoltaBattleLv1 / Lv2 ::appear */
{
    pol.hidden = 0; pol.lv2 = lv2;
    pol.bat.dead = 0; pol.health = 3; pol_body_hp(3);
    if (lv2) { pol_arm_start(&pol.arm[0]); pol_arm_start(&pol.arm[1]); }
    else { pol_arm_kill(&pol.arm[0]); pol_arm_kill(&pol.arm[1]); }
    pol.bat.nrv = -1; pol_bat_set(POB_WAIT);
    if (!lv2) pol.c24 = 0;
    pol.gra_n = lv2 ? 8 : 4; pol.grk_max = lv2 ? 7 : 5; pol.grk_delay = lv2 ? 180 : 120;
    printf("polta: battle lv%d\n", lv2 + 1);
}
static void pol_set_generate_rock(void) /* setNerveGenerateRock (Lv1's exeWait the same) */
{
    pol_bat_set(pol.c24 == 2 && pol_grocks_dead() >= (pol.lv2 ? 8 : 4) ? POB_GENGROUNDROCK : POB_GENROCK);
    pol.c24 = (pol.c24 + 4) % 3;
}
static int pol_try_attack_ground(void) /* tryAttackGround */
{
    int l = pol_arm_attack_ok(&pol.arm[0]), r = pol_arm_attack_ok(&pol.arm[1]);
    if (!l && !r) return 0;
    if (!pol_face_player(pol.c4, 45.0f)) return 0;
    float d = vlen(vsub(mario->pos, pol.pos));
    if (d < 1500.0f) { pol_bat_set(POB_ATTACKGROUND); return 1; }
    if (d < 3000.0f) {
        pol.pun_left = !r ? 1 : !l ? 0 : vdot(vsub(mario->pos, pol.pos), pol_side()) >= 0.0f; /* isPlayerLeftSide */
        pol_bat_set(POB_PUNCH);
        return 1;
    }
    return 0;
}
static int pol_battle(void) /* PoltaBattleLv1 / Lv2 ::update: 1 dead */
{
    int st = ++pol.bat.st, lv2 = pol.lv2;
    switch (pol.bat.nrv) {
    case POB_WAIT:
        pol_update_wait();
        if (!lv2) { if (st > 90) pol_set_generate_rock(); }
        else if (!(pol_arm_broken(&pol.arm[0]) && pol_arm_broken(&pol.arm[1]) && st > 120 && (pol_set_generate_rock(), 1))) pol_try_attack_ground(); /* tryGenerateRock */
        break;
    case POB_PUNCH: if (pol_state(&pol.pun, pol_punch)) pol_set_generate_rock(); break;
    case POB_ATTACKGROUND: if (pol_state(&pol.atk, pol_attack_ground)) pol_set_generate_rock(); break;
    case POB_GENGROUNDROCK: if (pol_state(&pol.gra, pol_grock_attack)) pol_bat_set(POB_WAIT); break;
    case POB_GENROCK: if (pol_state(&pol.grk, pol_generate_rock)) pol_bat_set(POB_WAIT); break;
    case POB_DAMAGEBODY: if (pol_update_damage_body(pol.health == 2)) pol_bat_set(POB_WAIT); break;
    case POB_BREAKBODY:
        if (st == 0) pol_star_pieces(10);
        if (pol_update_break_body()) pol_bat_set(POB_STAGGER);
        break;
    case POB_STAGGER:
        if (st == 0) { pol.stg_name = lv2 ? "StaggerLv2" : "StaggerLv1"; bgm_state(2, 60); pol_se(pol.pos, SE_BV_POLTA_RUN_AWAY); pol_state_appear(&pol.stg); }
        pol_stagger();
        break;
    case POB_DAMAGECORE:
        if (st == 0) {
            if (!lv2) { se_start(SE_SY_VS_BOSS_DAMAGE_3, NULL); pol_se(pol.pos, SE_BV_POLTA_CORE_DAMAGE); pol_se(pol.pos, SE_BM_POLTA_CORE_DAMAGE); }
            else { se_start(SE_SY_VS_BOSS_LAST_HIT, NULL); pol_se(pol.pos, SE_BV_POLTA_DAMAGE_LAST); pol_se(pol.pos, SE_BM_POLTA_LAST_DAMAGE); bgm_stop(60); }
            printf("polta: core hit (lv%d)\n", lv2 + 1);
        }
        if (pol_update_damage_core()) pol.bat.dead = 1;
        break;
    }
    if (pol.bat.dead) return 1;
    /* control */
    pol_update_pose(0.2f, 0.4f);
    int n = pol.bat.nrv;
    if (n == POB_WAIT || n == POB_GENGROUNDROCK || n == POB_GENROCK || n == POB_DAMAGEBODY || n == POB_BREAKBODY) pol.lv[0] = se_level(SE_BM_LV_POLTA_IN_BATTLE_ROCK, &pol.pos.x, pol.lv[0]);
    else if (n == POB_STAGGER || n == POB_DAMAGECORE) pol.lv[1] = se_level(SE_BM_LV_POLTA_CORE_ESCAPE, &pol.pos.x, pol.lv[1]);
    return 0;
}
static int pol_enemy_attack(int s) /* receiveMsgEnemyAttack (isMsgExplosionAttack) on sensor s */
{
    if (!pol_in_battle() || !pol_enable_damage() || !pol_sensor_ok(s)) return 0;
    if (pol_is_body(s)) {
        if (pol.health > 0) { pol.health--; pol_body_hp(pol.health); } /* addDamageBody */
        printf("polta: body hit (%d left)\n", pol.health);
        pol_bat_set(pol.health <= 0 ? POB_BREAKBODY : POB_DAMAGEBODY);
        return 1;
    }
    if (pol.lv2 && pol_is_larm(s)) { pol_arm_damage(&pol.arm[0]); return 1; }
    if (pol.lv2 && pol_is_rarm(s)) { pol_arm_damage(&pol.arm[1]); return 1; }
    if (s == POS_CORE) { pol_bat_set(POB_DAMAGECORE); return 1; }
    return 0;
}
static int pol_explosion(V3 c, float r) /* a BombTeresa's sendMsgEnemyAttackExplosion: Polta's sensors, its rocks */
{
    if (pol.row < 0) return 0;
    for (int i = 0; i < POL_ROCKS; i++) {
        PolRock *k = &pol.rock[i];
        if (k->on && vlen(vsub(k->pos, c)) < r + 120.0f && pol_rock_explosion_ok(k)) { k->vel = v3(0, 0, 0); pol_rock_generate_kill(k); return 1; }
    }
    for (int s = 0; s < POS_N; s++)
        if (s != POS_SCREAM && pol.sr[s] > 0.0f && vlen(vsub(pol.sc[s], c)) < r + pol.sr[s] && pol_enemy_attack(s)) return 1;
    return 0;
}
static int pol_shot(V3 p) /* a star piece: 1 taken (a rock: isMsgStarPieceAttack), 2 reflected (Polta: isMsgStarPieceReflect) */
{
    if (pol.row < 0) return 0;
    for (int i = 0; i < POL_ROCKS; i++)
        if (pol.rock[i].on && vlen(vsub(pol.rock[i].pos, p)) < 120.0f) { pol.rock[i].vel = v3(0, 0, 0); pol_rock_generate_kill(&pol.rock[i]); return 1; }
    if (!pol_in_battle()) return 0;
    for (int s = 0; s < POS_N; s++)
        if (s != POS_SCREAM && pol.sr[s] > 0.0f && pol_sensor_ok(s) && vlen(vsub(pol.sc[s], p)) < pol.sr[s]) return 2;
    return 0;
}
static void pol_sensors(void) /* its sensors' places this frame (PoltaSensorCtrl) */
{
    for (int i = 0; i < 4; i++) { pol.sc[i] = pol_joint(&pol.md, pol_body_s[i].j, v3(pol_body_s[i].x, pol_body_s[i].y, pol_body_s[i].z)); pol.sr[i] = pol_body_s[i].r; }
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 11; i++) {
            int s = (k ? POS_RARM : POS_LARM) + i;
            pol.sc[s] = pol_joint(&pol.arm[k].md, pol_arm_s[i].j, v3(pol_arm_s[i].x, 0, 0)); pol.sr[s] = pol_arm_s[i].r;
        }
    pol.sc[POS_SCREAM] = pol_joint(&pol.md, 18, v3(0, 0, 0)); pol.sr[POS_SCREAM] = pol.scream;
    pol.sc[POS_HEAD] = pol_joint(&pol.md, 15, v3(-50, 0, 0)); pol.sr[POS_HEAD] = 200.0f;
}
static void pol_attack_sensors(void) /* PoltaBattleLv1 / Lv2 ::attackSensor, its sensors against Mario and the others */
{
    if (!pol_in_battle()) return;
    V3 mc = pol_mc();
    int mario_done = 0;
    if (pol_enable_scream() && pol.scream > 0.0f) /* attackScreamSensor: sendMsgToEnemyAttackShockWave to every non-player in it */
        for (int k = 0; k < POL_BOMBS; k++) {
            Kuribo *b = pol_bomb(k);
            V3 d = vsub(b->pos, pol.sc[POS_SCREAM]);
            if (b->state == KURIBO_DEAD || b->nrv == BT_SHADOWAPPEAR || !bt_shock_ok(b) || vlen(d) >= pol.scream + 85.0f) continue;
            b->vel = vmul(vlen(d) > 1e-6f ? vnorm(d) : v3(0, 1, 0), 20.0f); w_nrv(b, BT_SHOCK);
        }
    for (int s = 0; s < POS_N; s++) {
        if (s == POS_SCREAM || !pol_sensor_ok(s)) continue;
        V3 c = pol.sc[s];
        float r = pol.sr[s];
        if (!mario_done && vlen(vsub(mc, c)) < r + HIT_BODY_R) {
            int took = 0;
            if (!pol.lv2) took = !mario->hurt_timer && mario_attack(ATK_STRONG, c);
            else if (pol_attack_ok(s)) took = pol.bat.nrv == POB_ATTACKGROUND && pol_is_arm(s) ? mario_attack(ATK_CRUSH, c) : !mario->hurt_timer && mario_attack(ATK_STRONG, c);
            if (took) mario_done = 1; else mario_push_out(c, r);
        }
        for (int k = 0; k < POL_BOMBS; k++) { /* the BombTeresas: pushed 3 (Lv2's attacking arm: disperse) */
            Kuribo *b = pol_bomb(k);
            if (b->state == KURIBO_DEAD || b->nrv == BT_SHADOWAPPEAR || b->nrv == BT_EXPLOSION || b->nrv == BT_DISPERSE || b->nrv == BT_READYRESTART) continue;
            V3 d = vsub(b->pos, c);
            if (vlen(d) >= r + 85.0f) continue;
            if (pol.lv2 && pol_attack_ok(s) && pol_is_arm(s)) { if (b->nrv != BT_DRIFT && bt_drift_ok(b)) { b->vel = v3(0, 0, 0); w_nrv(b, BT_DISPERSE); } } /* isEnableDisperse */
            else b->vel = vadd(b->vel, vmul(vlen(d) > 1e-6f ? vnorm(d) : v3(0, 1, 0), 3.0f));
        }
        if (pol.lv2) /* ACTMES_BREAK_POLTA_GROUND_ROCK to the ground rocks' body sensors */
            for (int i = 0; i < POL_GROCKS; i++)
                if (pol.grock[i].on && vlen(vsub(pol_grock_body(&pol.grock[i]), c)) < r + 170.0f) pol_grock_break(&pol.grock[i]);
    }
}

/* ---- PoltaDemo / the sequencer ---- */
static int pol_demo(void) { return pol.row >= 0 && (pol.seq == POQ_OPENING || pol.seq == POQ_POWERUP || pol.seq == POQ_DOWN) && !pol.dm.dead && pol.dm.nrv != POD_TRYSTART; }
static void pol_player_at(const char *name) /* setPlayerPos(GeneralPos) */
{
    const float *gp = scene_gpos(name);
    if (!gp) { printf("polta: no %s\n", name); return; }
    float m[16], one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, gp, gp + 3, one);
    m_set_trans(v3(gp[0], gp[1], gp[2])); mario->up = vnorm(v3(m[4], m[5], m[6]));
    mario->face = vnorm(vtangent(v3(m[8], m[9], m[10]), mario->up)); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 1;
}
static void pol_cam(const char *clip) /* startAnimCameraTargetSelf (its base matrix) / NULL endAnimCamera */
{
    pol.cam = clip; pol.cam_f = 0;
    if (!clip) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45);
}
static void pol_cam_update(void)
{
    if (!pol.cam) return;
    char n[40];
    float m[16];
    snprintf(n, sizeof n, "polta/%s", pol.cam);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    int cf = canm_frames(n);
    pol_base(m);
    if (cf) demo_camera(n, pol.cam_f < cf ? pol.cam_f : cf - 1, m); /* (held on its last frame until endAnimCamera) */
    pol.cam_f++;
}
static void pol_try_demo(const char *name, int nrv) /* startTryDemo */
{
    pol.dm.dead = 0; pol.dname = name; pol.dpend = nrv;
    if (pd_try_start(name, pol.row)) { render_cover(2); pol_set(&pol.dm, nrv); } /* startDemoSetting: overlayWithPreviousScreen(2) */
    else pol_set(&pol.dm, POD_TRYSTART);
    printf("polta: %s\n", name);
}
static void pol_formation(int on, const char *body, const char *l, const char *r) /* the formation rocks (appear / kill) */
{
    pol.fo_on = on && body; pol.arm[0].fo_on = on && l; pol.arm[1].fo_on = on && r;
    if (on && body) pol_act(&pol.fo, pol.fom, "PoltaFormationRock", body);
    if (on && l) pol_act(&pol.arm[0].fo, pol.fom, "PoltaFormationRock", l);
    if (on && r) pol_act(&pol.arm[1].fo, pol.fom, "PoltaFormationRock", r);
}
static void pol_demo_update(void) /* PoltaDemo's nerves */
{
    int st = ++pol.dm.st;
    switch (pol.dm.nrv) {
    case POD_TRYSTART:
        if (pd_try_start(pol.dname, pol.row)) { render_cover(2); pol_set(&pol.dm, pol.dpend); }
        break;
    case POD_OPENING:
        if (st == 0) {
            pol.hidden = 0; pol_kill_grocks(); pol_start_pose(); bgm_stop(60);
            pol_cam("OpeningDemo"); pol_body_act("OpeningDemo");
            pol_on_movement(); pol_player_at("ポルタ開始デモプレイヤー位置");
            pol_formation(1, "OpeningDemo", NULL, NULL);
        }
        if (st == 60) bgm_start("MBGM_BOSS_09_A");
        if (st > 0 && pol_end(&pol.md)) { pol_cam(NULL); pd_end("ポルタ開始デモ"); pol.fo_on = 0; pol.dm.dead = 1; }
        break;
    case POD_POWERUP:
        if (st == 0) {
            pol_kill_grocks(); pol_start_pose(); bgm_stop(60); pol_on_movement();
            pol_arm_start(&pol.arm[0]); pol_arm_start(&pol.arm[1]);
            pol_cam("PowerUpDemo"); pol_body_act("PowerUpDemo");
            pol_arm_act(&pol.arm[0], "PowerUpDemo"); pol_arm_act(&pol.arm[1], "PowerUpDemo");
            pol.arm[0].noshadow = pol.arm[1].noshadow = 1; /* invalidateShadowAll */
            pol_player_at("ポルタデモプレイヤー位置");
            pol_formation(1, "PowerUpDemo", "PowerUpDemoArmL", "PowerUpDemoArmR");
        }
        if (st > 0 && pol_end(&pol.md)) {
            pd_end("ポルタパワーアップデモ"); pol_cam(NULL); bgm_start("MBGM_BOSS_09_B");
            pol.arm[0].noshadow = pol.arm[1].noshadow = 0; /* validateShadowAll */
            pol_formation(0, NULL, NULL, NULL); pol.dm.dead = 1;
        }
        break;
    case POD_DOWN:
        if (st == 0) {
            pol_kill_grocks(); pol_start_pose(); bgm_stop(30);
            pol_cam("DownDemo"); pol_body_act("DownDemo"); pol_on_movement();
            pol_player_at("ポルタデモプレイヤー位置");
        }
        if (st > 0 && pol_end(&pol.md)) {
            pd_end("ポルタダウンデモ"); pol_cam(NULL);
            const Row *r = &sc->objs[pol.row]; /* Polta::kill */
            pol_arm_kill(&pol.arm[0]); pol_arm_kill(&pol.arm[1]);
            bgm_after_boss();
            note_star_row = pol.row; note_star_at = pol.pos; row_kill(pol.row); /* requestAppearPowerStar(this, mPosition) */
            if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
            pol.dm.dead = 1; pol.seq = -1;
            printf("polta: down, its star\n");
        }
        break;
    }
    pol_cam_update();
}
static void pol_update_models(void) /* every model's clip a frame (calcAnim) */
{
    PolMd *ms[] = {&pol.md, &pol.br, &pol.fo, &pol.arm[0].md, &pol.arm[1].md, &pol.arm[0].br, &pol.arm[1].br, &pol.arm[0].fo, &pol.arm[1].fo,
                   &pol.gbr[0], &pol.gbr[1], &pol.gbr[2], &pol.gbr[3]};
    const Model *mds[] = {pol.m, pol.brm, pol.fom, pol.arm[0].m, pol.arm[1].m, pol.abrm, pol.abrm, pol.fom, pol.fom, pol.grbm, pol.grbm, pol.grbm, pol.grbm};
    for (u32 i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        Anim *a = ms[i]->a;
        if (!a || !mds[i] || !mds[i]->skin) continue;
        if (a->skin != mds[i]->skin) { anim_bind(a, mds[i]->skin); if (ms[i]->key[0]) anim_start(a, ms[i]->key, 0); }
        anim_update(a);
    }
}
static void update_polta(void)
{
    if (pol.row < 0 || pol.seq < 0 || !dx_cls_moves("Polta")) return;
    const Row *r = &sc->objs[pol.row];
    if (r->zone < MAX_ZONES && zone_low[r->zone]) return; /* (its zone not in yet) */
    pol.g = km_g(pol.pos);
    if (pol.seq == POQ_WAITSTART) {
        if (r->sw[SW_A] < 0 || !sw_on(r, SW_A)) return; /* listenStageSwitchOnA: start */
        pol.seq = POQ_OPENING; pol.sst = -1;
        printf("polta: start\n");
    }
    int s0 = ++pol.sst;
    switch (pol.seq) { /* PoltaActionSequencer */
    case POQ_OPENING:
        if (s0 == 0) pol_try_demo("ポルタ開始デモ", POD_OPENING);
        pol_demo_update();
        if (pol.dm.dead) { pol.seq = POQ_LV1; pol.sst = -1; }
        break;
    case POQ_LV1:
        if (s0 == 0) pol_battle_appear(0);
        if (pol_battle()) { pol.seq = POQ_POWERUP; pol.sst = -1; }
        break;
    case POQ_POWERUP:
        if (s0 == 0) pol_try_demo("ポルタパワーアップデモ", POD_POWERUP);
        pol_demo_update();
        if (pol.dm.dead) { pol.seq = POQ_LV2; pol.sst = -1; }
        break;
    case POQ_LV2:
        if (s0 == 0) { pol_battle_appear(1); pol_star_pieces(10); }
        if (pol_battle()) { pol.seq = POQ_DOWN; pol.sst = -1; }
        break;
    case POQ_DOWN:
        if (s0 == 0) pol_try_demo("ポルタダウンデモ", POD_DOWN);
        pol_demo_update();
        break;
    }
    if (pol.seq < 0) return;
    pol.pos = vadd(pol.pos, pol.vel);
    pol_arm_update(&pol.arm[0]); pol_arm_update(&pol.arm[1]);
    for (int i = 0; i < POL_ROCKS; i++) if (pol.rock[i].on) pol_rock_update(&pol.rock[i]);
    for (int i = 0; i < POL_GROCKS; i++) {
        PolGRock *gr = &pol.grock[i];
        if (gr->on) pol_grock_update(gr, i);
        if (pol.inst0 >= 0) { /* its collision instance follows it (validate / invalidateCollisionParts) */
            Inst *in = &insts[pol.inst0 + i];
            pol_grock_mtx(gr, in->m);
            in->nokcl = in->invisible = !(gr->on && gr->shown);
        }
    }
    pol_update_models();
    pol_sensors();
    pol_attack_sensors();
}
static void pol_draw(void)
{
    if (pol.row < 0 || pol.seq < 0 || pol.seq == POQ_WAITSTART) return;
    float b[16];
    pol_base(b);
    if (!pol.hidden) pol_draw_md(pol.m, b, &pol.md, pol.pos);
    if (pol.md.a && pol.md.a->skin) { /* DamageSmoke1 / 2 (Head3) */
        static char key[2][4096]; /* (fx_named keeps a row's effect at key + its row: two far apart) */
        if (pol.smoke1) fx_named("Polta", "DamageSmoke1", b, (const float (*)[16])pol.md.a->world, key[0]);
        if (pol.smoke2) fx_named("Polta", "DamageSmoke2", b, (const float (*)[16])pol.md.a->world, key[1]);
    }
    if (pol.br_on) pol_draw_md(pol.brm, b, &pol.br, pol.pos);
    if (pol.fo_on) pol_draw_md(pol.fom, b, &pol.fo, pol.pos);
    for (int k = 0; k < 2; k++) {
        PolArm *a = &pol.arm[k];
        if (a->alive && !a->hidden) pol_draw_md(a->m, b, &a->md, pol.pos);
        if (a->alive && !a->hidden && !a->noshadow && a->md.a && a->md.a->skin) { /* initShadowFromCSV: its VolumeOvalPoles (shoulder, elbow, hand
            * 283.69 out, the fingers' joints, A3..D3 200 / 150 out), dropped 2000. ponytail: the poles as their ovals, its VolumeLines not drawn */
            static const struct { int j; float x, z, ox; } sh[] = {{3, 100, 100, 0}, {5, 100, 100, 0}, {7, 250, 250, 283.69f}, {8, 30, 120, 0}, {9, 50, 80, 0},
                {12, 30, 120, 0}, {13, 50, 80, 0}, {16, 30, 120, 0}, {17, 50, 80, 0}, {20, 30, 120, 0}, {21, 50, 80, 0}, {9, 50, 80, 200}, {13, 50, 80, 150},
                {17, 50, 80, 200}, {21, 50, 80, 200}};
            for (unsigned i = 0; i < sizeof sh / sizeof sh[0]; i++) {
                if (sh[i].j >= (int)a->md.a->skin->njoints) continue;
                float w[16];
                mat_mul(w, b, a->md.a->world[sh[i].j]);
                V3 p = v3(w[12] + w[0] * sh[i].ox, w[13] + w[1] * sh[i].ox, w[14] + w[2] * sh[i].ox);
                draw_shadow(p, pol.U, v3(w[8], w[9], w[10]), sh[i].x, sh[i].z, 0.0f, 50.0f, 2000.0f);
            }
        }
        if (a->br_on) pol_draw_md(pol.abrm, a->brm, &a->br, pol.pos);
        if (a->fo_on) pol_draw_md(pol.fom, b, &a->fo, pol.pos);
    }
    for (int i = 0; i < POL_ROCKS; i++) {
        const PolRock *r = &pol.rock[i];
        if (!r->on || !pol.rockm[r->type]) continue;
        float m[16];
        mat_identity(m);
        m[0] = r->x.x; m[1] = r->x.y; m[2] = r->x.z; m[4] = r->y.x; m[5] = r->y.y; m[6] = r->y.z;
        m[8] = r->z.x; m[9] = r->z.y; m[10] = r->z.z; m[12] = r->pos.x; m[13] = r->pos.y; m[14] = r->pos.z;
        use_row_light(pol.row, r->pos, LIGHT_STRONG);
        render_model(pol.rockm[r->type], m, 0x80, 0x80, 0x80);
        draw_shadow(r->pos, up_at(r->pos, pol.U), r->z, 120.0f, 100.0f, 0.0f, 50.0f, 2000.0f); /* initShadowFromCSV: 本体 VolumeOval (120, 80, 100) dropped 2000 */
    }
    for (int i = 0; i < POL_GBREAKS; i++) {
        if (pol.gbr_of[i] < 0) continue;
        float m[16];
        pol_grock_mtx(&pol.grock[pol.gbr_of[i]], m);
        pol_draw_md(pol.grbm, m, &pol.gbr[i], pol.grock[pol.gbr_of[i]].pos);
    }
}
static void pol_models(const Scene *s) /* its models (the zone's pack brings them: found again at each rebind) */
{
    pol.m = scene_model(s, "Polta"); pol.brm = scene_model(s, "PoltaBodyBreak"); pol.fom = scene_model(s, "PoltaFormationRock");
    pol.abrm = scene_model(s, "PoltaArmBreak"); pol.grm = scene_model(s, "PoltaGroundRock"); pol.grbm = scene_model(s, "PoltaGroundRockBreak");
    pol.rockm[0] = scene_model(s, "PoltaRock#Color:0"); pol.rockm[1] = scene_model(s, "PoltaRock#Color:1"); pol.rockm[2] = scene_model(s, "PoltaRock#Color:2");
    pol.arm[0].m = scene_model(s, "PoltaLeftArm"); pol.arm[1].m = scene_model(s, "PoltaRightArm");
}
static void pol_rebind_inst(const Scene *s) /* game_rebind_: PoltaGroundRockHolder's collision parts */
{
    pol.inst0 = -1;
    if (pol.row < 0) return;
    pol_models(s);
    static int said;
    if (pol.m && !said) { said = 1; printf("polta: models %s %s %s %s %s\n", pol.m ? "body" : "-", pol.arm[0].m ? "arms" : "-", pol.rockm[0] ? "rocks" : "-", pol.grm ? "ground" : "-", pol.fom ? "formation" : "-"); }
    if (pol.row < 0 || !scene_model(s, "PoltaGroundRock") || ninsts + POL_GROCKS > insts_cap) return;
    pol.inst0 = ninsts;
    for (int i = 0; i < POL_GROCKS; i++) {
        Inst *in = &insts[ninsts++];
        memset(in, 0, sizeof *in);
        in->md = scene_model(s, "PoltaGroundRock"); in->row = pol.row; in->zone = s->objs[pol.row].zone; in->kmove = in->kparent = -1;
        pol_grock_mtx(&pol.grock[i], in->m);
        in->s = 1.0f; in->reach = in->md->radius;
        in->kcl = scene_kcl(s, "PoltaGroundRock"); in->nokcl = in->invisible = 1; /* (listed by kcl_list_build, off until it juts) */
    }
}
static void pol_init(const Scene *s) /* Polta::init (makeActorAppeared; WaitStart: hideModel, its arms dead) */
{
    memset(&pol, 0, sizeof pol); pol.row = -1; pol.inst0 = -1;
    int t = scene_type(s, "Polta");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        V3 x, z;
        row_axes(r, &x, &z);
        pol.row = (int)i; pol.pos = pol.e0 = row_pos(r); pol.g = km_g(pol.pos);
        pol.U = pol.U0 = vmul(vnorm(pol.g), -1.0f); pol.F = pol.F0 = vnorm(vtangent(z, pol.U)); pol.c4 = pol.F; /* makeQuatAndFrontFromRotate */
        pol.coins = 20; pol.hidden = 1; pol.seq = POQ_WAITSTART;
        pol.m = scene_model(s, "Polta"); pol.brm = scene_model(s, "PoltaBodyBreak"); pol.fom = scene_model(s, "PoltaFormationRock");
        pol.abrm = scene_model(s, "PoltaArmBreak"); pol.grm = scene_model(s, "PoltaGroundRock"); pol.grbm = scene_model(s, "PoltaGroundRockBreak");
        pol.rockm[0] = scene_model(s, "PoltaRock#Color:0"); pol.rockm[1] = scene_model(s, "PoltaRock#Color:1"); pol.rockm[2] = scene_model(s, "PoltaRock#Color:2");
        pol.arm[0].left = 1; pol.arm[0].m = scene_model(s, "PoltaLeftArm"); pol.arm[1].m = scene_model(s, "PoltaRightArm");
        pol.arm[0].cc = pol.arm[1].cc = 2; pol.arm[0].c8 = pol.arm[1].c8 = 2; /* _C8 / _CC */
        PolMd *ms[] = {&pol.md, &pol.br, &pol.fo, &pol.arm[0].md, &pol.arm[1].md, &pol.arm[0].br, &pol.arm[1].br, &pol.arm[0].fo, &pol.arm[1].fo,
                       &pol.gbr[0], &pol.gbr[1], &pol.gbr[2], &pol.gbr[3]};
        for (u32 k = 0; k < sizeof ms / sizeof ms[0]; k++) { ms[k]->a = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(ms[k]->a, 0, sizeof(Anim)); }
        for (int k = 0; k < POL_GBREAKS; k++) pol.gbr_of[k] = -1;
        pol_bva(&pol.md, "Polta", "BreakLevel", 0.0f);
        pol_body_act("Wait");
        int mode; /* PoltaStateAttackGround: getBckFrameMax */
        pol.atk_len[0] = anim_clip_info("polta/attackfrontstart", &mode); pol.atk_len[1] = anim_clip_info("polta/attackfront", &mode);
        pol.atk_len[2] = anim_clip_info("polta/attackfronttowait", &mode);
        printf("polta: row %d at %.0f %.0f %.0f, models %s %s %s %s, attack %d %d %d\n", (int)i, pol.pos.x, pol.pos.y, pol.pos.z,
               pol.m ? "body" : "-", pol.arm[0].m ? "arms" : "-", pol.rockm[0] ? "rocks" : "-", pol.grm ? "ground" : "-", pol.atk_len[0], pol.atk_len[1], pol.atk_len[2]);
        break;
    }
}
static void pol_find_bombs(void) /* its BombTeresaHolder: the walker slots made after its row (host -1) */
{
    pol.bombs = -1;
    for (int k = 0; pol.row >= 0 && k < nkuribo && pol.bombs < 0; k++) if (kuribo[k].host == -1 && kuribo[k].row == pol.row) pol.bombs = k;
    if (pol.row >= 0 && pol.bombs < 0) { printf("polta: no bomb teresa slots\n"); pol.row = -1; }
}
/* Test builds (XF=-DPOL_TEST, PhantomGalaxy/3): Mario to the opening demo's place, its SW_A on; in the battle a
 * black rock floating is broken (as by a star piece: its BombTeresa), a BombTeresa is taken by Mario's spin and,
 * once it holds him, set on the body / an arm / the core sensor that takes a hit (test only), till its star */
int game_test_pol(int frame)
{
    if (pol.row < 0) return -1;
    const Row *r = &sc->objs[pol.row];
    test_nolost = 1;
    if (frame % 60 == 0)
        printf("pol test f %d: seq %d, battle %d (%d), health %d, arms %d/%d %d/%d, bombs %d, rocks %d, ground %d, demo %d, %.0f from Mario, life %d\n", frame, pol.seq, pol.bat.nrv,
               pol.bat.st, pol.health, pol.arm[0].alive, pol.arm[0].nrv, pol.arm[1].alive, pol.arm[1].nrv, pol_living_bombs(),
               (int)(pol.rock[0].on + pol.rock[1].on + pol.rock[2].on + pol.rock[3].on + pol.rock[4].on + pol.rock[5].on + pol.rock[6].on),
               POL_GROCKS - pol_grocks_dead(), pol.dm.nrv, vlen(vsub(mario->pos, pol.pos)), game_life());
    if (life < 2) life = 3; /* (test: Mario is not dodging) */
    if (pol.seq < 0) { /* to its star */
        static int dead_at = -1;
        int ty = scene_type(sc, "PowerStar");
        if (dead_at < 0) dead_at = frame;
        for (u32 i = 0; frame - dead_at >= 200 && frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].zone == r->zone) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 100;
    }
    if (pol.seq == POQ_WAITSTART) { /* held at the opening demo's place till its zone is in */
        static int full_at = -1;
        if (full_at < 0) pol_player_at("ポルタ開始デモプレイヤー位置");
        if (full_at < 0 && r->zone < MAX_ZONES && !zone_low[r->zone]) full_at = frame;
        if (full_at >= 0 && frame - full_at > 120 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
        return 0;
    }
    if (!pol_in_battle() || demo_on) return pol.seq;
    if (vlen(vsub(mario->pos, pol.e0)) > 2600.0f) pol_player_at("ポルタデモプレイヤー位置"); /* (off its planet: the black hole under it) */
    for (int i = 0; i < POL_ROCKS; i++) /* a black rock in its Sign: broken (its BombTeresa) */
        if (pol.rock[i].on && pol.rock[i].type == 1 && pol.rock[i].nrv == POR_SIGN) { printf("pol test f %d: black rock broken\n", frame); pol_rock_generate_kill(&pol.rock[i]); }
    static int last = -1000;
    for (int k = 0; k < POL_BOMBS; k++) {
        Kuribo *b = pol_bomb(k);
        if (b->state == KURIBO_DEAD) continue;
        if ((b->nrv == BT_DRIFT && b->step > 30) || b->nrv == BT_DRIFTRELEASE) { /* thrown at a sensor that takes it */
            int want = pol.health > 0 ? POS_BODY + (frame / 7) % 3 : POS_CORE;
            if (pol_enable_damage() && pol_sensor_ok(want)) { /* its explosion's message to that sensor (bt_update's pol_explosion) */
                b->pos = b->bpos = pol.sc[want]; b->vel = v3(0, 0, 0);
                if (pol_explosion(pol.sc[want], 85.0f)) bt_explode(b);
                printf("pol test f %d: bomb %d thrown at sensor %d\n", frame, k, want);
            }
            continue;
        }
        if ((b->nrv == BT_WAIT || b->nrv == BT_WANDER || b->nrv == BT_CHASE || b->nrv == BT_TACKLESIGN) && frame - last > 60) { /* Mario spins by it */
            V3 up = vmul(b->grav, -1.0f), h = vtangent(vsub(b->pos, pol.pos), up);
            h = vlen(h) > 1e-3f ? vnorm(h) : pol.F;
            mario->pos = vsub(vadd(b->pos, vmul(h, 250.0f)), vmul(up, 150.0f)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->face = vmul(h, -1.0f); mario->spin_timer = SPIN_TIME; mario->spin_t = 0; mario->hurt_timer = 0;
            last = frame;
            printf("pol test f %d: spin by bomb %d\n", frame, k);
            break;
        }
    }
    return pol.seq;
}
