/* SkeletalFishBoss (Kingfin, SkullSharkGalaxy 1) after Petari: SkeletalFishBoss.cpp, SkeletalFishBossInfo (Param.bcsv:
 * sf_param), SkeletalFishBossRail / RailHolder, SkeletalFishBossFunc, SkeletalFishBossBattleDirector, its head
 * (SkeletalFishBossHead: sensors body 400 and 16 wounds 270 x 0.9, the Head / Jow collisions), ScarFlash, the break
 * model, the eye lights; SkeletalFishGuard / GuardHolder (sfg_*). #included by game.c after skeletal_fish.inc.
 * On its level's rail (RailIDLv) at SpeedLv, its joints Joint00..12 bent along it (createJointDelegatorWithNullChildFunc:
 * their children follow), 14 collisions on its joints; its mouth opens with Mario within 5000 of its Head 5000 ahead,
 * closes beyond 7000, bites at a touch in OpenWait. A thrown shell at the head's sensors: EnergyLv hits a level (Damage:
 * 10 / 15 star pieces, 6 at its end), then Down and スカルシャークパワーアップ (the next level, its demo guards), the
 * last one DeadDamage, スカルシャーク死亡 (Down, then the break model), its power star; MBGM_BOSS_09_A / B and the
 * near / far BGM states. Its guards come out of the ガード出現ポイント nearest the rail's fill-up points, swim beside the
 * rail, and attack Mario on sight.
 * ponytail: the BRKs / BVAs (Base, DamageNColor, Crack, Down, the guards' Attack ones: colours at their first frame), no
 * shadows, no bloom (turnOnNormalBloom, the LightBloom models), no centre screen blur, no image effect, no bird louse
 * (BirdLouseS / L not ported: appear / killBirdLouse), no
 * デモ終了後カメラ (programmable camera: the game camera at once), the power star from the engine's appear point (not
 * _150 * (0, 1573, 0)), no camera register vector, no head enemy attack on other enemies, no guard Numb (2P pointer),
 * the guards' Appear base matrix as the others' (front _D0, up against gravity) */
static int gh_name_pos(const char *name, float m[16]); /* MR::findNamePos (game.c) */
enum { SK_SWIM, SK_OPEN, SK_OPENWAIT, SK_CLOSE, SK_BITE, SK_DAMAGE, SK_DOWN, SK_DEADDAMAGE, SK_DEAD, SK_APPEARWAIT,
       SK_APPEARDEMO, SK_POWERUPDEMO, SK_DEADDEMO, SK_BREAKDEMO, SK_DEMOWAIT };
enum { SKF_APPEAR = 1, SKF_POWERUP, SKF_DEAD }; /* stopScene's SceneFunc */
/* SkeletalFishBoss.arc Param.bcsv (SkeletalFishBossInfo::loadLevelStatus; LevelNum 3) */
static const struct { int energy; float speed; int rail; float goff; int gnum; struct { float x, y, z; int wait; } g[4]; } sf_param[3] = {
    {1, 12.0f, 0, 2000.0f, 0, {{0, 0, 0, 0}}},
    {2, 16.0f, 1, 1000.0f, 2, {{1000, -600, -300, 0}, {-1000, -300, -1400, 60}}},
    {2, 20.0f, 2, 1500.0f, 4, {{1000, 0, -2000, 0}, {-1000, 0, -1500, 40}, {-200, -700, -2800, 80}, {200, -600, 0, 120}}}};
static const int sfk_joint[13] = {1, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}; /* SkeletalFishBoss's Joint00..12 (JNT1) */
static const int sfk_col_joint[14] = {3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 4, 5}; /* sColInfo: Joint01..12, LeftFinJoint, RightFinJoint */
static const char *const sfk_col_name[16] = {"BackBone01", "BackBone02", "BackBone03", "BackBone04", "BackBone05", "BackBone06", "BackBone07",
                                             "BackBone08", "BackBone09", "BackBone10", "BackBone11", "BackFin", "LeftFinJoint", "RightFinJoint", "Head", "Jow"};
static const struct { int jow; float x, y, z; } sfk_wound[16] = { /* sHitPosData: on Head / Jow */
    {0, 26.144243f, 414.56268f, 552.1146f}, {0, -618.77576f, 55.65544f, 163.1091f}, {0, 597.2239f, 92.83909f, 188.59679f},
    {0, 4.24828f, -134.68741f, 869.9156f}, {0, 88.67339f, 10.598214f, -206.06902f}, {0, 331.39523f, 37.908966f, 794.4394f},
    {0, -314.09827f, -16.389574f, 782.438f}, {0, 6.604942f, 104.82322f, 1221.328f}, {0, 365.95203f, 371.1259f, 93.8099f},
    {0, -262.9857f, 410.83163f, 159.02629f}, {1, 35.98041f, -300.88702f, 76.61445f}, {1, -181.04329f, -124.71387f, 593.6195f},
    {1, 475.85388f, -146.99239f, 119.52184f}, {1, -473.8122f, -146.99239f, 119.52184f}, {1, 233.05017f, -124.71387f, 575.92737f},
    {1, -4.140523f, -318.03336f, 394.53833f}};
typedef struct { int id; const unsigned char *e; SfRail r; } SfkRail;
enum { SG_WAIT, SG_APPEAR, SG_NORMAL, SG_APART, SG_FOLLOW, SG_STRAIGHT, SG_DEFENCE, SG_KILL, SG_NUMB };
typedef struct {
    int alive, nrv, st, delay, wait, hidden, bind;     /* mAttackDelay, _A0, hideModel, the binder */
    V3 off, pos, grav, d0, a4, e8, f4, v100, dc, c10;  /* _94, mPosition, mGravity, _D0, _A4, _E8, _F4, _100, _DC, _10C */
    float b0, c4;
    Anim *a; u32 lv[3];
} SfGuard;
static struct {
    int row, nrv, st, dead, alive, hidden, lvl, energy, dmgc, levels, gnum, bgm_t, bgm_st;
    int scene_nrv, scene_fn; const char *scene;
    SfRail rc; SfkRail rail[4]; int nrail;
    float pm[16], base[16], inv[16], head[16];
    Anim *body, *hd, *brk, *scar, *dg[4];
    int head_dead, lights, brk_on, scar_on, dg_on[4];
    const char *cam; int cam_f;
    int ki[16];
    u32 lv[4];
    SfGuard g[4]; int g_on; float g_c1c; const float *g_rail; /* SkeletalFishGuardHolder: _20, _1C, mBossRail */
} sfk = {.row = -1};
static void sf_clip(char *o, size_t n, const char *model, const char *clip) /* slice.py clip_key */
{
    snprintf(o, n, "%s/%s", model, clip);
    for (char *c = o; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (strlen(o) > 31 && !strncmp(o, "skeletalfish", 12)) { memmove(o + 2, o + 12, strlen(o + 12) + 1); o[1] = 'f'; } /* "sf" */
}
static void sfk_bck(Anim *a, const char *model, const char *clip, int interp) { char n[48]; sf_clip(n, sizeof n, model, clip); if (a && a->skin && !anim_start(a, n, interp)) printf("skeletal fish: no clip %s\n", n); } /* startBck */
static int sfk_bck_playing(const Anim *a, const char *model, const char *clip) { char n[48]; sf_clip(n, sizeof n, model, clip); return a && a->clip_id >= 0 && !strcmp(sc->clips[a->clip_id].name, n); }
static void sfk_body(const char *c, int interp) { sfk_bck(sfk.body, "SkeletalFishBoss", c, interp); }
static void sfk_head(const char *c, int interp) { sfk_bck(sfk.hd, "SkeletalFishBossHeadA", c, interp); }
static int sfk_stopped(const Anim *a) { return !a || !a->skin || a->clip_id < 0 || a->done; } /* isBckStopped */
static void sfk_set(int n) { sfk.nrv = n; sfk.st = -1; }
static void sfk_se(u32 id) { se_start(id, &sfk.head[12]); } /* startSound(mBossHead) */
static void sfk_lv(int k, u32 id) { sfk.lv[k] = se_level(id, &sfk.head[12], sfk.lv[k]); }
static void sfk_swim_lv(void) { sfk_lv(0, SE_BM_LV_SKL_BOSS_SWIM_FAR); sfk_lv(1, SE_BM_LV_SKL_BOSS_SWIM_NEAR); }
static int sfk_in_demo(void) { return sfk.nrv == SK_APPEARDEMO || sfk.nrv == SK_POWERUPDEMO || sfk.nrv == SK_DEADDEMO; }
static V3 sfk_pos(void) { return v3(sfk.base[12], sfk.base[13], sfk.base[14]); }
static SfkRail *sfk_rail_by_id(int id) /* SkeletalFishBossRailHolder::getByID (else the first) */
{
    for (int i = 0; i < sfk.nrail; i++) if (sfk.rail[i].id == id) return &sfk.rail[i];
    return sfk.nrail ? &sfk.rail[0] : NULL;
}
static SfkRail *sfk_cur(void) { for (int i = 0; i < sfk.nrail; i++) if (sfk.rail[i].r.p == sfk.rc.p) return &sfk.rail[i]; return NULL; }
static void sfk_reset_rail(void) /* resetRail: setRailActor(rail, nullptr, true) at coord 0, SpeedLv */
{
    SfkRail *r = sfk_rail_by_id(sf_param[sfk.lvl].rail);
    if (!r) return;
    sfk.rc = r->r; sfk.rc.coord = 0.0f; sfk.rc.speed = sf_param[sfk.lvl].speed;
}
static void sfk_power_up(void) { if (sfk.lvl < 2) sfk.lvl++; sfk.energy = sf_param[sfk.lvl].energy; sfk_reset_rail(); }
static void sfk_joint_w(const Anim *a, const float *base, int j, float *o) { tb_mul(o, base, a->world[j]); } /* getJointMtx */
static V3 sfk_mouth(float d) { float m[16]; sfk_joint_w(sfk.body, sfk.base, 2, m); return tb_mulp(m, v3(0, 0, d)); } /* getMouthSensorCenterPos */
static void sfk_kcl_name(char *o, size_t n, int k) { snprintf(o, n, k < 14 ? "SkeletalFishBoss#%s" : "SkeletalFishBossHeadA#%s", sfk_col_name[k]); }
static void sfk_coll(int on) /* validateCollision / invalidateCollision */
{
    for (int k = 0; k < 16; k++) {
        if (sfk.ki[k] < 0 || sfk.ki[k] >= ninsts) continue;
        char kn[48];
        sfk_kcl_name(kn, sizeof kn, k);
        insts[sfk.ki[k]].kcl = on ? scene_kcl(sc, kn) : NULL;
    }
}
static void sfk_coll_mtx(void) /* updateCollision: each part on its joint (setMtx) */
{
    for (int k = 0; k < 16; k++) {
        if (sfk.ki[k] < 0 || sfk.ki[k] >= ninsts) continue;
        if (k < 14) { if (sfk.body->skin) sfk_joint_w(sfk.body, sfk.base, sfk_col_joint[k], insts[sfk.ki[k]].m); }
        else if (sfk.hd->skin) sfk_joint_w(sfk.hd, sfk.head, k == 14 ? 0 : 3, insts[sfk.ki[k]].m);
    }
}
static void sf_rebind_insts(const Scene *s) /* game_rebind_: the boss's collision parts as invisible instances */
{
    for (int k = 0; k < 16; k++) sfk.ki[k] = -1;
    if (sfk.row < 0 || sfk.dead) return;
    const Row *r = &s->objs[sfk.row];
    const Model *md = scene_model(s, "SkeletalFishBoss");
    for (int k = 0; md && k < 16 && ninsts < (int)s->nobjs; k++) {
        char kn[48];
        sfk_kcl_name(kn, sizeof kn, k);
        if (!scene_kcl(s, kn)) continue;
        Inst *in = &insts[ninsts];
        sfk.ki[k] = ninsts++;
        memset(in, 0, sizeof *in);
        in->md = md; in->row = sfk.row; in->zone = r->zone; in->invisible = 1; in->kmove = in->kparent = -1;
        memcpy(in->m, sfk.base, 64);
        in->s = 1.0f; in->reach = 1500.0f;
        zone_grow(in);
    }
    sfk_coll(sfk.alive);
    if (sfk.alive) sfk_coll_mtx();
}
static void sfk_pose(void) /* calcAndSetBaseMtx + calcAnim (the joint delegators), the PartsModels on Head */
{
    const Model *md = scene_model(sc, "SkeletalFishBoss"), *hm = scene_model(sc, "SkeletalFishBossHeadA");
    if (!md || !md->skin || md->skin->njoints < 18) return;
    if (sfk.body->skin != md->skin) { anim_bind(sfk.body, md->skin); sfk_body("Swim", 0); }
    if (sfk_in_demo()) memcpy(sfk.base, sfk.pm, 64); /* setBaseTRMtx(_150) */
    else sf_rail_mtx(&sfk.rc, 0.0f, sfk.base);
    tb_inv(sfk.inv, sfk.base);
    Anim *a = sfk.body;
    anim_pose(a);
    if (!sfk_in_demo())
        for (int k = 0; k < 13; k++) { /* calcJoint before its children: they follow it */
            int j = sfk_joint[k];
            float w[16], nw[16], oi[16], d[16];
            sf_joint_calc(&sfk.rc, a->world[j], w);
            tb_mul(nw, sfk.inv, w);
            tb_inv(oi, a->world[j]); tb_mul(d, nw, oi);
            for (u32 c = (u32)j + 1; c < md->skin->njoints; c++) {
                int under = 0;
                for (int p = md->skin->joints[c].parent; p >= 0; p = md->skin->joints[p].parent) if (p == j) { under = 1; break; }
                if (under) tb_mul(a->world[c], d, a->world[c]);
            }
            memcpy(a->world[j], nw, 64);
        }
    anim_skin(a);
    sfk_joint_w(a, sfk.base, 2, sfk.head); /* initFixedPosition("Head") */
    if (hm && hm->skin) {
        if (sfk.hd->skin != hm->skin) { anim_bind(sfk.hd, hm->skin); sfk_head("Wait", 0); }
        anim_update(sfk.hd);
    }
    const Model *sm = scene_model(sc, "SkeletalFishBossScarFlash");
    if (sfk.scar_on && sm && sm->skin) {
        anim_update(sfk.scar);
        if (sfk_stopped(sfk.scar)) sfk.scar_on = 0; /* SkeletalFishBossScarFlash::control: kill */
    }
}
static int sfk_damageable(void) /* isEnableToBeDamaged (RMGE01) */
{
    return sfk.nrv != SK_DAMAGE && sfk.nrv != SK_DOWN && sfk.nrv != SK_DEADDAMAGE && sfk.nrv != SK_DEAD && !sfk_in_demo();
}
static void sfk_scar(void) /* SkeletalFishBossScarFlash::appear: Damage BCK */
{
    const Model *sm = scene_model(sc, "SkeletalFishBossScarFlash");
    if (!sm || !sm->skin) return;
    if (sfk.scar->skin != sm->skin) anim_bind(sfk.scar, sm->skin);
    sfk_bck(sfk.scar, "SkeletalFishBossScarFlash", "Damage", 0); sfk.scar_on = 1;
}
static void sfk_damage_anim(void) { sfk_body("Damage", 0); sfk_head("Damage", 0); sfk_scar(); } /* startDamageAnim */
static void sfk_damage(V3 from) /* damage(sensor, sender position) */
{
    if (!sfk_damageable()) return;
    camera_shake(SHAKE_STRONG);
    sfk.dmgc++; /* playDamageBrk (its colour: ponytail) */
    sfk.energy--;
    printf("skeletal fish boss: hit, level %d, energy %d\n", sfk.lvl, sfk.energy);
    if (sfk.energy == 1 && sfk.lvl + 1 == sfk.levels) fx_named("SkeletalFishBoss", "SkeltalDamageLast", sfk.base, NULL, NULL);
    if (sfk.energy <= 0) sfk_set(sfk.lvl + 1 == sfk.levels ? SK_DEADDAMAGE : SK_DOWN); /* (Down: isDemoExist("スカルシャークデモ")) */
    else {
        launch_pieces(from, up_at(from, v3(0, 1, 0)), sfk.lvl + 1 == sfk.levels ? 15 : 10, 10.0f, 40.0f);
        se_start(SE_OJ_STAR_PIECE_BURST_W_F, &from.x);
        sfk_set(SK_DAMAGE);
    }
}
static void sfk_stop_scene(const char *name, int nrv, int fn) { sfk_set(SK_DEMOWAIT); sfk.scene = name; sfk.scene_nrv = nrv; sfk.scene_fn = fn; }
static void sfk_cam(const char *name) { sfk.cam = name; sfk.cam_f = 0; } /* startCamera: the event camera anim on its CameraTargetMtx (_150) */
static void sfk_cam_end(void) { if (sfk.cam) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45); sfk.cam = NULL; } /* endEventCamera, resetCamera */
static void sfk_mario_to(const char *pos) /* tryFindNamePos + setPlayerBaseMtx */
{
    float m[16];
    if (!gh_name_pos(pos, m)) return;
    mario->pos = v3(m[12], m[13], m[14]); mario->up = vnorm(v3(m[4], m[5], m[6])); mario->face = vnorm(vtangent(v3(m[8], m[9], m[10]), mario->up));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
}

/* --- SkeletalFishGuard / SkeletalFishGuardHolder --- */
static V3 sfg_g(V3 p) { return vmul(up_at(p, v3(0, 1, 0)), -1.0f); } /* calcGravityVector */
static V3 sfg_kill_el(V3 v, V3 n) { return vsub(v, vmul(n, vdot(v, n))); } /* killElement / orthogonalize */
static int sfg_near0(V3 v) { return vlen(v) < 1e-3f; }
static float sfg_angle(V3 a, V3 b) { float la = vlen(a), lb = vlen(b); if (la <= 0 || lb <= 0) return 0; float c = vdot(a, b) / (la * lb); return acosf(c < -1 ? -1 : c > 1 ? 1 : c); }
static V3 sfg_rot(V3 v, V3 from, V3 to, float ratio) /* TQuat4f::setRotate(from, to[, ratio]).transform */
{
    V3 ax = vcross(from, to);
    float ang = sfg_angle(from, to) * ratio;
    return vlen(ax) < 1e-6f ? v : rot_axis(v, vnorm(ax), ang);
}
static V3 sfg_cubic(V3 p0, V3 t0, V3 t1, V3 p1, float t) /* TVec3f::cubic (Hermite) */
{
    float t2 = t * t, t3 = t2 * t;
    return vadd(vadd(vmul(p0, 2 * t3 - 3 * t2 + 1), vmul(t0, t3 - 2 * t2 + t)), vadd(vmul(p1, -2 * t3 + 3 * t2), vmul(t1, t3 - t2)));
}
static void sfg_set(SfGuard *g, int n) { g->nrv = n; g->st = -1; }
static void sfg_se(SfGuard *g, u32 id) { se_start(id, &g->pos.x); }
static void sfg_lv(SfGuard *g, int k, u32 id) { g->lv[k] = se_level(id, &g->pos.x, g->lv[k]); }
static void sfg_bck(SfGuard *g, const char *c) { sfk_bck(g->a, "SkeletalFishGuard", c, 0); }
static V3 sfg_player(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); } /* getPlayerCenterPos */
static void sfg_target(SfGuard *g, V3 *a1, V3 *a2, V3 *a3, int step) /* calcTarget */
{
    float c = g->off.z + sfk.rc.coord + sfk.rc.speed * step;
    *a1 = sf_rail_pos(&sfk.rc, c, a3);
    V3 v22 = sfg_g(*a1), v21 = vcross(*a3, v22);
    v21 = sfg_near0(v21) ? v21 : vnorm(v21);
    *a2 = vadd(*a1, vadd(vmul(v21, g->off.x), vmul(v22, -g->off.y)));
}
static void sfg_trans_front(SfGuard *g) { sfg_target(g, &g->e8, &g->f4, &g->v100, 0); g->d0 = g->v100; g->a4 = vsub(g->f4, g->pos); g->pos = g->f4; } /* calcTransAndFront */
static void sfg_look(SfGuard *g, float h, float vrt) /* lookToPlayer: rotateHorizontal, rotateVertical */
{
    V3 p = vsub(sfg_player(), g->pos);
    if (sfg_near0(p)) return;
    p = vnorm(p);
    V3 v10 = sfg_kill_el(p, g->grav);
    if (!sfg_near0(v10)) {
        v10 = vnorm(v10);
        V3 v9 = sfg_kill_el(g->d0, g->grav);
        if (!sfg_near0(v9)) { v9 = vnorm(v9); float a = sfg_angle(v9, v10); g->d0 = sfg_rot(g->d0, v9, v10, a > h ? h / a : 1.0f); } /* turn */
    }
    V3 v12 = vcross(g->grav, g->d0);
    if (!sfg_near0(v12)) {
        float a = sfg_angle(g->grav, p) - sfg_angle(g->grav, g->d0);
        a = a > vrt ? vrt : a < -vrt ? -vrt : a;
        g->d0 = rot_axis(g->d0, vnorm(v12), a);
    }
}
static int sfg_try_apart(SfGuard *g) /* tryShiftApart */
{
    if (g->delay > 0) return 0;
    float pp[3] = {g->pos.x, g->pos.y, g->pos.z};
    if (!render_visible(pp, 0.0f)) return 0; /* isInScreen */
    V3 v7 = vsub(sfg_player(), g->pos), hp;
    if (vlen(v7) > 5000.0f || sfg_near0(v7)) return 0; /* isPlayerInAttackRange (cosDegree(180): any way) */
    if (map_poly(g->pos, v7, &hp, NULL) || map_poly(g->pos, vsub(cam_pos, g->pos), &hp, NULL)) return 0; /* isLineOfSightClear */
    V3 v11 = sfg_kill_el(vsub(g->e8, g->pos), g->v100), v12 = sfg_kill_el(v7, g->v100);
    if (!sfg_near0(v11) && !sfg_near0(v12) && vlen(v11) > 400.0f && sfg_angle(v11, v12) < asinf(400.0f / vlen(v11))) return 0; /* getBodyThickness */
    for (int i = 0; i < 4; i++) if (&sfk.g[i] != g) sfk.g[i].delay = 120; /* notifyAttack: waitAttack(120) */
    sfg_set(g, SG_APART);
    return 1;
}
static void sfg_kill(SfGuard *g) { g->alive = 0; }
static void sfg_update(SfGuard *g)
{
    if (!g->alive) return;
    const Model *gm = scene_model(sc, "SkeletalFishGuard");
    if (gm && gm->skin && g->a->skin != gm->skin) { anim_bind(g->a, gm->skin); sfg_bck(g, "Swim"); }
    if (g->delay > 0) g->delay--; /* control */
    if (g->nrv != SG_KILL && g->nrv != SG_APART && g->bind) {
        Hit h[2];
        if (kcl_query(g->pos, 120.0f, h, 2)) sfg_set(g, SG_KILL); /* isBinded (initBinder 120) */
    }
    int st = ++g->st;
    switch (g->nrv) {
    case SG_WAIT: if (st == g->wait) sfg_set(g, SG_APPEAR); return;
    case SG_APPEAR: {
        if (st == 0) {
            g->hidden = 0; sfg_bck(g, "Swim"); sfg_se(g, SE_BM_SKL_GUARD_APPEAR); g->bind = 0;
            g->d0 = vmul(g->grav, -1.0f); g->c10 = g->pos;
            g->dc = v3((rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f);
        }
        V3 v32 = sfg_g(g->c10), v31 = vsub(g->c10, vmul(v32, 1000.0f));
        sfg_target(g, &g->e8, &g->f4, &g->v100, 300 - st);
        V3 v30 = sfg_kill_el(g->dc, v32);
        v30 = sfg_near0(v30) ? v30 : vnorm(v30);
        v31 = vadd(v31, vmul(v30, 500.0f));
        v30 = vnorm(vadd(v30, v3(0, 1, 0)));
        V3 v29 = g->pos;
        if (st < 99) g->pos = sfg_cubic(g->c10, v3(0, 0, 0), vmul(v30, 10.0f * 99), v31, st / 99.0f);
        else g->pos = sfg_cubic(v31, vmul(v30, 10.0f * 201), vmul(g->v100, sfk.rc.speed * 201), g->f4, (st - 99) / 201.0f);
        V3 v28 = vsub(g->pos, v29);
        if (!sfg_near0(v28)) g->d0 = vnorm(v28);
        g->grav = sfg_g(g->pos);
        sfg_lv(g, 0, SE_BM_LV_SKL_GUARD_SWIM_NORMAL);
        if (st == 300) sfg_set(g, SG_NORMAL);
        break;
    }
    case SG_NORMAL:
        if (st == 0) g->bind = 1;
        sfg_trans_front(g);
        g->grav = sfg_g(g->pos);
        sfg_lv(g, 0, SE_BM_LV_SKL_GUARD_SWIM_NORMAL);
        sfg_try_apart(g);
        break;
    case SG_APART: {
        if (st == 0) {
            V3 v15 = sfg_kill_el(g->a4, g->grav);
            g->b0 = vdot(g->grav, vsub(g->a4, v15)); g->a4 = v15;
            sfg_bck(g, "Turn"); g->delay = 0;
        }
        float t = st / 80.0f;
        V3 v14 = g->a4;
        if (!sfg_near0(v14)) {
            v14 = vmul(vnorm(v14), 0.3f);
            if (vdot(v14, v14) >= vdot(g->a4, g->a4)) g->a4 = v3(0, 0, 0); else g->a4 = vsub(g->a4, vmul(v14, 0.3f));
        }
        float v7 = g->b0 * t;
        V3 og = g->grav;
        g->grav = sfg_g(g->pos);
        g->d0 = sfg_rot(g->d0, og, g->grav, 1.0f); g->a4 = sfg_rot(g->a4, og, g->grav, 1.0f);
        float r = t * (t * (6 * (float)M_PI)) / 180.0f;
        sfg_look(g, r, r);
        g->pos = vadd(g->pos, vadd(vmul(g->grav, v7), g->a4));
        g->grav = sfg_g(g->pos);
        sfg_lv(g, 0, SE_BM_LV_SKL_GUARD_SWIM_NORMAL); sfg_lv(g, 1, SE_BM_LV_SKL_GUARD_ALARM); /* (its distance parameter: ponytail) */
        if (st == 80) sfg_set(g, SG_FOLLOW);
        break;
    }
    case SG_FOLLOW: case SG_STRAIGHT: {
        if (st == 0 && g->nrv == SG_FOLLOW) { g->c4 = 0.0f; sfg_bck(g, "Attack"); sfg_se(g, SE_BM_SKL_GUARD_ATTACK_START); }
        sfg_lv(g, 2, SE_BM_LV_SKL_GUARD_SWIM_ATTACK); sfg_lv(g, 0, SE_BM_LV_SKL_GUARD_SWIM_NORMAL); sfg_lv(g, 1, SE_BM_LV_SKL_GUARD_ALARM);
        g->pos = vadd(g->pos, vmul(g->d0, g->c4));
        g->c4 += 0.8f;
        if (g->c4 >= 22.0f) g->c4 = 22.0f;
        if (g->nrv == SG_FOLLOW) {
            float d = vlen(vsub(sfg_player(), g->pos)), rt = (d - 1000.0f) / 500.0f;
            rt = rt < 0 ? 0 : rt > 1 ? 1 : rt;
            float e = 1.0f - (1.0f - rt) * (1.0f - rt);
            sfg_look(g, 1.5f * e * (float)M_PI / 180.0f, e * (float)M_PI / 180.0f);
        }
        g->grav = sfg_g(g->pos);
        if (g->nrv == SG_FOLLOW) { if (vdot(g->d0, vsub(sfg_player(), g->pos)) < 0.0f) sfg_set(g, SG_STRAIGHT); } /* tryShiftStraight */
        else { float dep; if (!in_water(g->pos, vmul(g->grav, -1.0f), &dep)) sfg_set(g, SG_KILL); } /* tryShiftKill: getWaterAreaObj */
        break;
    }
    case SG_KILL:
        if (st == 0) { sfg_bck(g, "Break"); sfg_se(g, SE_BM_SKL_GUARD_EXPLODE); }
        if (st > 0 && sfk_stopped(g->a)) { g->hidden = 1; sfg_kill(g); return; } /* isBckOneTimeAndStopped: hideModel, kill */
        break;
    }
    if (g->nrv == SG_KILL || (g->nrv == SG_NORMAL && g->st < 2) || g->hidden) return;
    V3 c = sfg_player();
    for (int k = 0; k < 2; k++) { /* attackSensor: head 120 at Joint00, body 120 at Joint02 (here along its front) */
        V3 sp = vsub(g->pos, vmul(g->d0, k ? 98.0f : 0.0f));
        if (vlen(vsub(c, sp)) >= 120.0f + HIT_BODY_R) continue;
        if (!mario->hurt_timer && mario_attack(ATK_STRONG, sp)) sfg_set(g, SG_KILL); /* sendMsgEnemyAttackStrong */
        else mario_push_out(sp, 120.0f);
        break;
    }
}
static int sfg_jet_hit(V3 p) /* receiveMsgPlayerAttack: isMsgJetTurtleAttack */
{
    for (int i = 0; i < 4; i++) {
        SfGuard *g = &sfk.g[i];
        if (!g->alive || g->hidden || g->nrv == SG_KILL || g->nrv == SG_WAIT || (g->nrv == SG_NORMAL && g->st < 2)) continue;
        if (vlen(vsub(p, g->pos)) >= 120.0f + 60.0f && vlen(vsub(p, vsub(g->pos, vmul(g->d0, 98.0f)))) >= 120.0f + 60.0f) continue;
        launch_pieces(g->pos, vmul(g->grav, -1.0f), 3, 10.0f, 40.0f); sfg_se(g, SE_OJ_STAR_PIECE_BURST_W_F);
        sfg_set(g, SG_KILL);
        return 1;
    }
    return 0;
}
static int sfg_alive(void) { int n = 0; for (int i = 0; i < sfk.gnum; i++) n += sfk.g[i].alive; return n; } /* countGuardAlive */
static int sfg_appearable(void) { int n = sf_param[sfk.lvl].gnum - sfg_alive(); return n < 0 ? 0 : n; } /* countAppearableGuard */
static void sfg_place(SfGuard *g, int i) /* _94 GuardPosLv, _A0 GuardWaitLv */
{
    g->off = v3(sf_param[sfk.lvl].g[i].x, sf_param[sfk.lvl].g[i].y, sf_param[sfk.lvl].g[i].z); g->wait = sf_param[sfk.lvl].g[i].wait;
    g->grav = sfg_g(g->pos);
}
static void sfg_appear_guard(int n, int pt) /* appearGuard: at ガード出現ポイント<getFillUpNamePos> */
{
    SfkRail *r = sfk_cur();
    if (!r) return;
    V3 rp = sf_rail_pos(&r->r, pk_point_coord(r->e, pt), NULL), gp = v3(0, 0, 0);
    float best = 100000.0f; int id = -1; /* SkeletalFishBossFunc::getNearestGuardPosID (linkFillUpNamePosID) */
    for (int k = 0; k < 16; k++) {
        char nm[48]; float m[16];
        snprintf(nm, sizeof nm, "ガード出現ポイント%d", k);
        if (!scene_gpos(nm)) continue;
        gh_name_pos(nm, m);
        float d = vlen(vsub(rp, v3(m[12], m[13], m[14])));
        if (d < best) { best = d; id = k; gp = v3(m[12], m[13], m[14]); }
    }
    if (id < 0) return;
    for (int i = 0; i < sfk.gnum && n > 0; i++) {
        SfGuard *g = &sfk.g[i];
        if (g->alive) continue;
        g->pos = gp; sfg_place(g, i);
        g->alive = 1; g->hidden = 1; g->bind = 0; g->delay = 0; sfg_set(g, SG_WAIT); /* appearNaturally */
        n--;
        printf("skeletal fish guard %d: appears at point %d (wait %d)\n", i, id, g->wait);
    }
}
static void sfg_movement(void) /* SkeletalFishGuardHolder::movement, then the guards (connectToSceneEnemy) */
{
    if (sfk.g_on) {
        if (sfk.g_rail != sfk.rc.p) { sfk.g_c1c = sfk.rc.coord; sfk.g_rail = sfk.rc.p; } /* checkRailChangedAndReset */
        int n = sfg_appearable();
        SfkRail *r = sfk_cur();
        if (n > 0 && r && r->e) { /* checkDistanceAndAppear */
            float coord = sfk.rc.coord, len = sfk.rc.len;
            if (coord < sfk.g_c1c) coord += len;
            u32 nr = *(const u32 *)(r->e + 12);
            for (u32 i = 0; i < nr; i++) {
                if (pk_point_arg0(r->e, (int)i) == -1) continue; /* isFillUpPoint */
                float v9 = fmodf(pk_point_coord(r->e, (int)i) - sf_param[sfk.lvl].goff, len);
                if (v9 < 0) v9 += len;
                if (v9 < sfk.g_c1c) v9 += len;
                if (v9 < coord) { sfg_appear_guard(n, (int)i); break; }
            }
            sfk.g_c1c = sfk.rc.coord;
        }
    }
    for (int i = 0; i < sfk.gnum; i++) sfg_update(&sfk.g[i]);
}
static void sfg_validate(void) { if (!sfk.g_on) { sfk.g_on = 1; sfk.g_c1c = sfk.rc.coord; } }
static void sfg_invalidate(void) { sfk.g_on = 0; }
static void sfg_force_kill_all(void) { for (int i = 0; i < 4; i++) sfk.g[i].alive = 0; } /* makeActorDead */
static void sfg_kill_all(void) /* killNaturally */
{
    for (int i = 0; i < 4; i++) if (sfk.g[i].alive) { if (sfk.g[i].nrv == SG_WAIT) sfk.g[i].alive = 0; else if (sfk.g[i].nrv != SG_KILL) sfg_set(&sfk.g[i], SG_KILL); }
}
static void sfg_force_appear_all(void) /* forceAppearAll: appearForce (Normal, shown) */
{
    int n = sfg_appearable();
    for (int i = 0; i < sfk.gnum && n > 0; i++) {
        SfGuard *g = &sfk.g[i];
        if (g->alive) continue;
        sfg_place(g, i);
        g->alive = 1; g->hidden = 0; g->bind = 1; g->delay = 0; sfg_se(g, SE_BM_SKL_GUARD_APPEAR); sfg_set(g, SG_NORMAL);
        sfg_trans_front(g);
        n--;
    }
}
static void sfg_draw(void)
{
    const Model *gm = scene_model(sc, "SkeletalFishGuard");
    for (int i = 0; gm && gm->skin && i < 4; i++) {
        SfGuard *g = &sfk.g[i];
        if (!g->alive || g->hidden || g->a->skin != gm->skin) continue;
        float m[16];
        V3 up = vmul(g->grav, -1.0f), x = vcross(up, g->d0); /* calcAndSetBaseMtx */
        if (sfg_near0(x)) continue;
        x = vnorm(x);
        mat_identity(m); tb_setcol(m, 0, x); tb_setcol(m, 1, vcross(g->d0, x)); tb_setcol(m, 2, g->d0); tb_setcol(m, 3, g->pos);
        anim_update(g->a);
        draw_skin(gm, m, g->a);
    }
}

/* --- SkeletalFishBoss's nerves --- */
static void sfk_demo_guards(const char *clip, int n) /* SkeletalFishBossBattleDirector::playGuardAnim */
{
    const Model *gm = scene_model(sc, "SkeletalFishGuard");
    for (int i = 0; i < n && gm && gm->skin; i++) {
        char c[24];
        snprintf(c, sizeof c, "%s%c", clip, 'A' + i);
        if (sfk.dg[i]->skin != gm->skin) anim_bind(sfk.dg[i], gm->skin);
        sfk_bck(sfk.dg[i], "SkeletalFishGuard", c, 0); sfk.dg_on[i] = 1;
        se_start(SE_BM_SKL_GUARD_APPEAR, NULL);
    }
}
static void sfk_scene_fn(int fn)
{
    if (fn == SKF_APPEAR) { sfk_mario_to("マリオ再セット位置1"); mario_hidden = 1; sfk_cam("skeletalfishbossbattlestart"); } /* startAppearDemo */
    else if (fn == SKF_POWERUP) { render_cover(2); mario_hidden = 1; sfk_cam(!sfk.lvl ? "skeletalfishbosspowerup1" : "skeletalfishbosspowerup2"); } /* startPowerUpDemo */
    else if (fn == SKF_DEAD) { render_cover(2); mario_hidden = 1; sfk_cam("skeletalfishbossdown"); } /* startDeadDemo */
}
static void sfk_appear(void) /* appear: hideModel, AppearWait (makeActorAppeared: validateCollision, level 0) */
{
    sfk.alive = 1; sfk.hidden = 1; sfk.lvl = -1; sfk_power_up(); sfk_coll(1);
    sfk_set(SK_APPEARWAIT);
    printf("skeletal fish boss: appear\n");
}
static void sfk_update_bgm(void) /* updateBgm */
{
    int cur = sfk.bgm_t;
    if (cur < 0 || (sfk.bgm_t = cur - 1) < 0) {
        sfk.bgm_t = -1;
        int near = vlen(vsub(mario->pos, sfk_pos())) <= 4200.0f, t = near ? 2 : 1;
        if (t != sfk.bgm_st) bgm_state(t, near ? 60 : 120);
        sfk.bgm_st = t;
    }
}
static void sfk_head_touch(void) /* SkeletalFishBossHead::attackSensor: body 400 at Head (0, -120, 320) */
{
    if (sfk.nrv == SK_DEADDAMAGE || sfk.nrv == SK_DEAD || sfk_in_demo() || sfk.head_dead || sfk.hidden) return;
    V3 c = sfg_player(), s = tb_mulp(sfk.head, v3(0, -120.0f, 320.0f));
    if (vlen(vsub(c, s)) >= 400.0f + HIT_BODY_R) return;
    mario_push_out(s, 400.0f); /* sendMsgPush */
    if (sfk.nrv == SK_OPENWAIT) sfk_set(SK_BITE);
}
static int sfk_jet_hit(V3 p) /* SkeletalFishBossHead::receiveMsgPlayerAttack (isMsgJetTurtleAttack on any of its sensors), the guards' */
{
    if (sfk.row < 0 || !sfk.alive || sfk.dead) return 0;
    if (sfg_jet_hit(p)) return 1;
    if (sfk.hidden || sfk.head_dead || !sfk.hd->skin) return 0;
    if (vlen(vsub(p, tb_mulp(sfk.head, v3(0, -120.0f, 320.0f)))) < 400.0f + 60.0f) { sfk_damage(p); return 1; }
    for (int k = 0; k < 16; k++) {
        float jw[16];
        sfk_joint_w(sfk.hd, sfk.head, sfk_wound[k].jow ? 3 : 0, jw);
        if (vlen(vsub(p, tb_mulp(jw, vmul(v3(sfk_wound[k].x, sfk_wound[k].y, sfk_wound[k].z), 0.9f)))) < 270.0f + 60.0f) { sfk_damage(p); return 1; }
    }
    return 0;
}
static void update_sfk(void)
{
    if (sfk.row < 0 || sfk.dead) return;
    const Row *r = &sc->objs[sfk.row];
    if (!sfk.alive) { /* initSwitch: SW_APPEAR (syncStageSwitchAppear) or SW_A's listener (appear) */
        if ((r->sw[SW_APPEAR] >= 0 && sw_on(r, SW_APPEAR)) || (r->sw[SW_APPEAR] < 0 && r->sw[SW_A] >= 0 && sw_on(r, SW_A))) sfk_appear();
        else return;
    }
    if (sfk.cam) { /* its event camera anim, on _150 */
        char n[40];
        snprintf(n, sizeof n, "sfb/%s", sfk.cam);
        int cf = canm_frames(n);
        if (cf) demo_camera(n, sfk.cam_f < cf ? sfk.cam_f : cf - 1, sfk.pm);
        sfk.cam_f++;
    }
    const Model *bmd = scene_model(sc, "SkeletalFishBoss"), *hmd = scene_model(sc, "SkeletalFishBossHeadA"); /* initModelManagerWithAnm: bound before any startBck */
    if (bmd && bmd->skin && sfk.body->skin != bmd->skin) { anim_bind(sfk.body, bmd->skin); sfk_body("Swim", 0); }
    if (hmd && hmd->skin && sfk.hd->skin != hmd->skin) { anim_bind(sfk.hd, hmd->skin); sfk_head("Wait", 0); }
    int st = ++sfk.st;
    switch (sfk.nrv) {
    case SK_SWIM:
        if (st == 0) {
            if (!sfk_bck_playing(sfk.body, "SkeletalFishBoss", "Swim")) { sfk_body("Swim", 60); sfk_head("Wait", 60); } /* startBckWithInterpole */
            sfg_validate();
        }
        sfk_swim_lv();
        if (vlen(vsub(sfk_mouth(5000.0f), mario->pos)) < 5000.0f) sfk_set(SK_OPEN);
        break;
    case SK_OPEN:
        if (st == 0) { sfk_head("Open", 0); sfk_se(SE_BM_SKL_BOSS_MOUTH_OPEN); }
        sfk_swim_lv();
        if (st > 0 && sfk_stopped(sfk.hd)) { sfk_head("OpenWait", 0); sfk_set(SK_OPENWAIT); }
        break;
    case SK_OPENWAIT:
        if (vlen(vsub(sfk_mouth(7000.0f), mario->pos)) >= 7000.0f) sfk_set(SK_CLOSE); else sfk_swim_lv();
        break;
    case SK_CLOSE: case SK_BITE:
        if (st == 0) { sfk_head(sfk.nrv == SK_CLOSE ? "Close" : "Bite", 0); if (sfk.nrv == SK_CLOSE) sfk_se(SE_BM_SKL_BOSS_MOUTH_CLOSE); }
        sfk_swim_lv();
        if (st > 0 && sfk_stopped(sfk.hd)) { sfk_head("Wait", 0); sfk_set(SK_SWIM); }
        break;
    case SK_DAMAGE:
        if (st == 0) { sfk_damage_anim(); sfk_se(SE_BM_SKL_BOSS_DAMAGE_S); se_start(SE_SY_VS_BOSS_DAMAGE_1, NULL); sfg_invalidate(); }
        sfk_swim_lv();
        if (st > 0 && sfk_stopped(sfk.body)) { /* (Damage1Color / Damage2Color) */
            sfk_set(SK_SWIM);
            V3 p = sfk_pos();
            launch_pieces(p, up_at(p, v3(0, 1, 0)), 6, 10.0f, 40.0f); se_start(SE_OJ_STAR_PIECE_BURST_W_F, &p.x);
        }
        break;
    case SK_DOWN:
        if (st == 0) { sfk_damage_anim(); sfk_se(SE_BM_SKL_BOSS_DAMAGE_L); se_start(SE_SY_VS_BOSS_DAMAGE_3, NULL); sfg_invalidate(); } /* (Crack BVA) */
        sfk_swim_lv();
        if (st == 120) sfk_stop_scene("スカルシャークパワーアップ", SK_POWERUPDEMO, SKF_POWERUP);
        break;
    case SK_DEADDAMAGE:
        if (st == 0) { sfk_damage_anim(); sfk_se(SE_BM_SKL_BOSS_DAMAGE_LAST); se_start(SE_SY_VS_BOSS_LAST_HIT, NULL); sfg_invalidate(); sfg_kill_all(); sfk.lights = 0; }
        sfk_swim_lv();
        if (st == 120) { bgm_stop(30); sfk_stop_scene("スカルシャーク死亡", SK_DEADDEMO, SKF_DEAD); }
        break;
    case SK_DEAD:
        if (st == 0) { sfk_mario_to("マリオ再セット位置2"); sfk_cam_end(); return; } /* resetCamera */
        sfk.dead = 1; sfk.alive = 0; sfk_coll(0); sfk.scar_on = 0; row_kill(sfk.row); /* kill: its power star out (declarePowerStar) */
        printf("skeletal fish boss: dead\n");
        return;
    case SK_APPEARWAIT:
        if (!demo_on && !dx.on && !pd.name) sfk_stop_scene("スカルシャーク出現", SK_APPEARDEMO, SKF_APPEAR); /* !isPlayerInBind && !isEventCameraActive */
        return;
    case SK_APPEARDEMO:
        if (st == 0) { sfk.hidden = 0; sfk_body("BattleStart", 0); sfk_head("BattleStart", 0); bgm_stop(60); sfg_invalidate(); }
        if (st == 280) bgm_start("MBGM_BOSS_09_A");
        sfk_lv(2, SE_BM_LV_SKL_BOSS_SWIM_APPEAR);
        if (st > 0 && sfk_stopped(sfk.body)) {
            if (!bgm_stage_playing()) bgm_start("MBGM_BOSS_09_A");
            sfk.bgm_t = 120;
            sfk_mario_to("マリオ再セット位置1"); mario_hidden = 0; sfk_cam_end(); pd_end("スカルシャーク出現"); /* endAppearDemo */
            sfk_body("Swim", 0); sfk_head("Wait", 0); sfk_reset_rail(); sfk_set(SK_SWIM);
        }
        break;
    case SK_POWERUPDEMO:
        if (st == 0) {
            sfk.hidden = 0; sfk_power_up(); sfg_invalidate(); sfg_force_kill_all(); sfk.rc.speed = 60.0f;
            if (sfk.lvl == 1) { sfk_body("PowerUp1", 0); sfk_head("PowerUp1", 0); sfk_demo_guards("PowerUp1", 2); }
            else { sfk_body("PowerUp2", 0); sfk_head("PowerUp2", 0); sfk_demo_guards("PowerUp2", 4); }
            if (sfk.lvl > 1) bgm_stop(30);
        }
        if (sfk.lvl == 2 && st == 240) se_start(SE_BM_SKL_GUARD_COME_FRONT, NULL); /* startAtmosphereSE */
        sfk_lv(2, SE_BM_LV_SKL_BOSS_SWIM_POW_UP);
        if (st > 0 && sfk_stopped(sfk.body)) {
            if (sfk.lvl > 1) bgm_start("MBGM_BOSS_09_B");
            sfk.bgm_t = 120;
            sfk_mario_to("マリオ再セット位置2"); mario_hidden = 0; sfk_cam_end(); /* endPowerUpDemo */
            for (int i = 0; i < 4; i++) sfk.dg_on[i] = 0; /* endPowerUpDemo1 / 2: killGuard */
            pd_end("スカルシャークパワーアップ");
            sfk_reset_rail(); sfk_body("Swim", 0); sfk_head("Wait", 0); sfg_force_appear_all(); sfk_set(SK_SWIM);
        }
        break;
    case SK_DEADDEMO:
        if (st == 0) { sfk_body("Down", 0); sfk_head("Down", 0); sfk_se(SE_BM_SKL_BOSS_MOUTH_OPEN); sfg_invalidate(); }
        if (st == 30) sfk_se(SE_BM_SKL_BOSS_BODY_SOLID);
        if (st < 218) sfk_lv(2, SE_BM_LV_SKL_BOSS_SWIM_DIE);
        if (st > 0 && sfk_stopped(sfk.body)) { render_cover(2); sfk_set(SK_BREAKDEMO); }
        break;
    case SK_BREAKDEMO: {
        const Model *bm = scene_model(sc, "SkeletalFishBossBreak");
        if (st == 0) {
            sfk.hidden = 1; sfk.head_dead = 1; /* hideModel, mBossHead->kill */
            if (bm && bm->skin) { if (sfk.brk->skin != bm->skin) anim_bind(sfk.brk, bm->skin); sfk_bck(sfk.brk, "SkeletalFishBossBreak", "Break", 0); sfk.brk_on = 1; }
            sfk_se(SE_BM_SKL_BOSS_EXPLODE); sfg_invalidate(); pad_rumble(RUMBLE_MIDDLE);
        }
        if (st == 204) pad_rumble(RUMBLE_STRONG);
        if (st == 206) sfk_se(SE_BM_SKL_BOSS_EXPLODE_2);
        if (sfk.brk_on) anim_update(sfk.brk);
        if (st > 0 && (!sfk.brk_on || sfk_stopped(sfk.brk))) {
            bgm_after_boss();
            sfk_cam_end(); pd_end("スカルシャーク死亡"); mario_hidden = 0; sfk.brk_on = 0; /* endBreakDemo */
            sfk_set(SK_DEAD);
        }
        break;
    }
    case SK_DEMOWAIT:
        if (pd_try_start(sfk.scene, sfk.row)) { sfk_scene_fn(sfk.scene_fn); sfk_set(sfk.scene_nrv); }
        return;
    }
    for (int i = 0; i < 4; i++) if (sfk.dg_on[i]) anim_update(sfk.dg[i]);
    if (!sfk_in_demo()) { /* control */
        sf_rail_update(&sfk.rc);
        sfk_pose();
        sfk_coll_mtx();
        sfg_movement();
        sfk_update_bgm();
    } else sfk_pose();
    if (sfk.nrv != SK_DEAD && sfk.nrv != SK_APPEARWAIT) st_column_strike(tb_mulp(sfk.head, v3(0, -120.0f, 320.0f)), 400.0f); /* tryColumnCollision (statics.inc) */
    sfk_head_touch();
}
static void sfk_draw(void)
{
    if (sfk.row < 0 || sfk.dead || !sfk.alive) return;
    const Model *md = scene_model(sc, "SkeletalFishBoss"), *hm = scene_model(sc, "SkeletalFishBossHeadA"), *lm = scene_model(sc, "SkeletalFishBossLight");
    use_row_light(sfk.row, sfk_pos(), LIGHT_STRONG);
    if (!sfk.hidden && md && md->skin && sfk.body->skin == md->skin) draw_skin(md, sfk.base, sfk.body);
    if (!sfk.hidden && !sfk.head_dead && hm && hm->skin && sfk.hd->skin == hm->skin) {
        draw_skin(hm, sfk.head, sfk.hd);
        for (int k = 0; lm && sfk.lights && k < 2; k++) { float m[16]; sfk_joint_w(sfk.hd, sfk.head, 4 + k, m); render_model(lm, m, 0x80, 0x80, 0x80); } /* 眼光 on LightConeLeft / Right */
    }
    const Model *sm = scene_model(sc, "SkeletalFishBossScarFlash");
    if (sfk.scar_on && !sfk.hidden && sm && sm->skin && sfk.scar->skin == sm->skin) draw_skin(sm, sfk.head, sfk.scar);
    const Model *bm = scene_model(sc, "SkeletalFishBossBreak");
    if (sfk.brk_on && bm && bm->skin && sfk.brk->skin == bm->skin) draw_skin(bm, sfk.pm, sfk.brk);
    const Model *gm = scene_model(sc, "SkeletalFishGuard");
    float id[16];
    mat_identity(id);
    for (int i = 0; i < 4 && gm && gm->skin; i++) if (sfk.dg_on[i] && sfk.dg[i]->skin == gm->skin) draw_skin(gm, id, sfk.dg[i]); /* デモ用スカルシャークガード (no matrix) */
    sfg_draw();
}
static void sfk_init(const Scene *s, const unsigned char *blob)
{
    memset(&sfk, 0, sizeof sfk); sfk.row = -1;
    for (int k = 0; k < 16; k++) sfk.ki[k] = -1;
    int t = scene_type(s, "SkeletalFishBoss"), tr = scene_type(s, "SkeletalFishBossRail");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        sfk.row = (int)i;
        sfk.levels = r->arg[0] >= 0 ? r->arg[0] : 3; sfk.gnum = r->arg[1] >= 0 ? r->arg[1] : 4; /* _1A0, _1A4 */
        if (sfk.levels > 3) sfk.levels = 3;
        if (sfk.gnum > 4) sfk.gnum = 4;
        float one[3] = {1, 1, 1}, pp[3] = {r->pos[0], r->pos[1], r->pos[2]}, rr[3] = {r->rot[0], r->rot[1], r->rot[2]};
        mat_translate_rot_scale(sfk.pm, pp, rr, one); /* getZonePlacementMtx */
        memcpy(sfk.base, sfk.pm, 64);
        for (u32 j = 0; tr >= 0 && j < s->nobjs && sfk.nrail < 4; j++) /* SkeletalFishBossRail::init: arg0 its id */
            if ((int)s->objs[j].type == tr && s->objs[j].path >= 0) {
                SfkRail *k = &sfk.rail[sfk.nrail];
                k->id = s->objs[j].arg[0];
                sf_rail_init(&k->r, blob, &s->objs[j], v3(0, 0, 0), 0.0f);
                k->e = find_path(blob, s->objs[j].zone, s->objs[j].path);
                if (k->r.p && k->e) sfk.nrail++;
            }
        Anim **an[12] = {&sfk.body, &sfk.hd, &sfk.brk, &sfk.scar, &sfk.dg[0], &sfk.dg[1], &sfk.dg[2], &sfk.dg[3], &sfk.g[0].a, &sfk.g[1].a, &sfk.g[2].a, &sfk.g[3].a};
        for (int k = 0; k < 12; k++) { *an[k] = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(*an[k], 0, sizeof(Anim)); }
        sfk.lvl = -1; sfk_power_up(); /* initAfterPlacement: powerUp, makeActorDead */
        sfk.lights = 1; sfk.bgm_t = -1; sfk.g_on = 1;
        printf("skeletal fish boss: row %d, %d levels, %d guards, %d rails, sw appear %d a %d\n", (int)i, sfk.levels, sfk.gnum, sfk.nrail, r->sw[SW_APPEAR], r->sw[SW_A]);
        break;
    }
}
/* Test builds (XF=-DSFK_TEST, SkullSharkGalaxy/1): the boss's switch on (test only, if its SwitchSphere was not
 * reached), then a JetTurtle thrown at its head every 300 frames outside demos, till its star; then Mario to it */
int game_test_sfk(int frame)
{
    static int last = -1000;
    if (sfk.row < 0) return -1;
    const Row *r = &sc->objs[sfk.row];
    if (frame % 60 == 0)
        printf("sfk test f %d: nrv %d lvl %d energy %d alive %d dead %d coord %.0f / %.0f at %.0f %.0f %.0f, Mario %.0f %.0f %.0f, guards %d %d %d %d, life %d\n",
               frame, sfk.nrv, sfk.lvl, sfk.energy, sfk.alive, sfk.dead, sfk.rc.coord, sfk.rc.len, sfk.base[12], sfk.base[13], sfk.base[14],
               mario->pos.x, mario->pos.y, mario->pos.z, sfk.g[0].alive ? sfk.g[0].nrv : -1, sfk.g[1].alive ? sfk.g[1].nrv : -1,
               sfk.g[2].alive ? sfk.g[2].nrv : -1, sfk.g[3].alive ? sfk.g[3].nrv : -1, game_life());
    if (life < 2) life = 3; /* (test: the guards are not dodged) */
    if (sfk.dead) {
        int ty = scene_type(sc, "PowerStar");
        for (u32 i = 0; frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].arg[7] == sfk.row && !demo_on && !pd.name) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 100;
    }
    if (!sfk.alive) {
        if (frame > 200 && r->sw[SW_APPEAR] >= 0) sw_set(r, SW_APPEAR, 1);
        else if (frame > 200 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
        return 0;
    }
    if (demo_on || pd.name || sfk.nrv == SK_DEMOWAIT || !sfk_damageable() || frame - last < 300 || jet.inst >= 0) return sfk.nrv;
    for (int k = 0; k < ninsts; k++) {
        Inst *in = &insts[k];
        if (in->jet != 1 || row_step[in->row]) continue;
        V3 h = tb_mulp(sfk.head, v3(0, -120.0f, 320.0f)), d = vnorm(v3(sfk.head[8], sfk.head[9], sfk.head[10]));
        in->head = vadd(h, vmul(d, 900.0f)); in->hvel = vmul(d, -30.0f); /* before its mouth, at it */
        jet.inst = k; jet.phase = 2; jet.t = 0; jet.row = in->row;
        last = frame;
        printf("sfk test f %d: shell row %d thrown at the head\n", frame, in->row);
        return 1;
    }
    return sfk.nrv;
}
