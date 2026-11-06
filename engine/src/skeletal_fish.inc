/* Kingfin and Bonefin after Petari: src/Game/Boss/SkeletalFishRailControl.cpp, SkeletalFishJointCalc.cpp,
 * SkeletalFishBaby.cpp and ExterminationChecker.cpp's ExterminationSkeletalFishBaby (createExterminationPowerStar:
 * ChildSkeletalFishBaby children). #included by game.c after tripod_boss.inc (its tb_* matrix helpers), sf_*.
 * SkeletalFishBaby (SkeletalFishBaby / ChildSkeletalFishBaby rows, model SnakeFish): on its own rail at arg0 (20),
 * Swim (accel 0.1 up to it), its 4 joints Joint00..03 bent along the rail (SkeletalFishJointCalc), sensors head 190 /
 * bust 120 / waist 120 / tail 100 hurting Mario strongly (shakeCameraStrong) or pushing him, a thrown shell
 * (isMsgJetTurtleAttack) breaks it: shakeCameraNormal, an air bubble where the shell was, Break (Death BCK, SEs), Dead
 * (10 star pieces at Center, kill: SE, SW_DEAD).
 * ponytail: no Bind (isStarPointerPointing2POnPressButton: no 2P pointer), no AnimScaleController reaction to a star
 * piece (asc_ is the walkers' only), no shadow (initShadowFromCSV), no SkeletalFishBaby effects (its AutoEffectList
 * rows are not in jpa.py NAMED), the base scale left at 1 (no row scales them), no clipping (sphere 500) */
#define SF_BABIES 12
static int sfk_jet_hit(V3 p); /* skeletal_fish_boss.inc */
static void update_sfk(void), sfk_draw(void), sfk_init(const Scene *s, const unsigned char *blob);
#define SF_SNAKE_J0 2 /* SnakeFish's Joint00..03 (JNT1 order: All_Root, Center, Joint00, LFin, LowerJaw, Antenna, RFin,
                       * UpperJaw, Eye, Joint01, Hire, Joint02, Joint03) */
static const int sf_snake_j[4] = {2, 9, 11, 12};
enum { SFB_SWIM, SFB_BIND, SFB_BREAK, SFB_DEAD };
typedef struct {            /* SkeletalFishRailControl, mode 2 only (no second rail: the boss / baby rails start on theirs) */
    const float *p; u32 n; int cl;
    float len, coord, speed; /* RailRider coord, _14 */
    int end;                 /* _18: the open rail's end reached this frame */
} SfRail;
typedef struct {
    int row, nrv, st, dead, child;
    SfRail rc;
    float speed, smax;       /* mSpeed, mSpeedMax (arg0) */
    float base[16], inv[16]; /* calcAndSetBaseMtx's rail matrix, _AC */
    float wj[4][16];         /* its joints' world matrices (sensors) */
    Anim *anim;
    u32 lv;
} SfBaby;
static SfBaby sfb[SF_BABIES];
static int nsfb;
static struct { int row, nrv, st; } sfx = {.row = -1}; /* ExterminationSkeletalFishBaby: 0 Watching, 1 TryStartDemoAppear, 2 AppearStar, 3 dead */

/* --- SkeletalFishRailControl --- */
static float sf_wrap(const SfRail *r, float c) { if (!r->cl || r->len <= 0) return c; c = fmodf(c, r->len); return c < 0 ? c + r->len : c; }
static V3 sf_rail_pos(const SfRail *r, float c, V3 *dir) /* calcRailPosAtCoord / calcRailDirectionAtCoord */
{
    V3 d;
    V3 p = nrail_at(r->p, r->n, r->cl, sf_wrap(r, c), &d);
    if (dir) *dir = d;
    return p;
}
static void sf_rail_init(SfRail *r, const unsigned char *blob, const Row *row, V3 pos, float speed) /* setRailActor(this, nullptr, true), moveCoordToNearestPos */
{
    memset(r, 0, sizeof *r);
    int cl = 0;
    r->p = row->path >= 0 && blob ? find_rail_of(blob, row->zone, row->path, &r->n, &cl, 0) : NULL;
    r->cl = cl; r->speed = speed;
    if (!r->p || r->n < 2) { r->p = NULL; return; }
    r->len = nrail_len(r->p, r->n, r->cl);
    r->coord = nrail_nearest(r->p, r->n, r->cl, pos);
}
static void sf_rail_update(SfRail *r) /* update, mode 2: moveRailRider (an open rail clamps at its end) */
{
    r->end = 0;
    if (!r->p) return;
    if (r->coord + r->speed > r->len) r->end = 1;
    r->coord += r->speed;
    if (r->cl) r->coord = sf_wrap(r, r->coord);
    else r->coord = fminf(fmaxf(r->coord, 0.0f), r->len);
}
static V3 sf_rail_at(const SfRail *r, float a2, V3 *dir) /* getPos / getMtx's position: before an open rail's start, along its first direction */
{
    float c = r->coord - a2;
    V3 d;
    if (c < 0.0f && !r->cl) {
        V3 p = sf_rail_pos(r, 0.0f, &d);
        if (dir) *dir = d;
        return vadd(p, vmul(d, c));
    }
    return sf_rail_pos(r, c, dir);
}
static void sf_rail_mtx(const SfRail *r, float a2, float *m) /* getMtx: x = -gravity x dir, y = dir x x, z = dir */
{
    V3 d, p = sf_rail_at(r, a2, &d);
    d = vlen(d) > 0 ? vnorm(d) : v3(0, 0, 1);
    V3 g = vmul(up_at(p, v3(0, 1, 0)), -1.0f); /* calcGravityVector */
    V3 x = vcross(vmul(g, -1.0f), d), y = vcross(d, x);
    x = vlen(x) > 0 ? vnorm(x) : x; y = vlen(y) > 0 ? vnorm(y) : y;
    mat_identity(m); tb_setcol(m, 0, x); tb_setcol(m, 1, y); tb_setcol(m, 2, d); tb_setcol(m, 3, p);
}
/* SkeletalFishJointCalc::calcJoint: the joint's matrix (model space w) moved back to z 0, then onto the rail z further
 * along (its own z: behind the head is negative); out the world matrix */
static void sf_joint_calc(const SfRail *r, const float *w, float *out)
{
    float t[16], rm[16];
    memcpy(t, w, 64);
    float z = t[14];
    t[14] = 0.0f; /* makeTrans(0, 0, -trans.z) concat */
    sf_rail_mtx(r, -z, rm);
    tb_mul(out, rm, t);
}

/* --- SkeletalFishBaby --- */
static const struct { int j; float r; V3 off; } sfb_sensor[4] = {{0, 190.0f, {0, 20, 100}}, {1, 120.0f, {0, 0, 30}}, {2, 120.0f, {0, 0, 20}}, {3, 100.0f, {0, 0, 0}}}; /* initSensor */
static const Model *sfb_model(void) { return scene_model(sc, "SnakeFish"); }
static void sfb_se(const SfBaby *b, u32 id) { se_start(id, &b->base[12]); }
static void sfb_nrv(SfBaby *b, int n) { b->nrv = n; b->st = -1; }
static int sfb_attackable(const SfBaby *b) { return b->nrv != SFB_DEAD && b->nrv != SFB_BREAK && b->nrv != SFB_BIND; }
static void sfb_kill(SfBaby *b) /* kill */
{
    b->dead = 1;
    sfb_se(b, SE_EM_EXPLODE_UNDER_WATER);
    row_kill(b->row); /* onSwitchDead */
    printf("skeletal fish baby row %d: killed\n", b->row);
}
static void sf_air_bubble(V3 p) /* MR::appearAirBubble(pos, -1): AirBubble::appearMove (cDefaultLife) */
{
    for (int k = 0; k < MAX_BUBBLES; k++) {
        if (bubbles[k].life) continue;
        bubbles[k].pos = p; bubbles[k].t = 0; bubbles[k].nrv = 0; bubbles[k].a4 = v3(0, 0, 1);
        bubbles[k].vel = vmul(up_at(p, v3(0, 1, 0)), 7.0f);
        bubbles[k].life = 360;
        return;
    }
}
static int sfb_damage(SfBaby *b, V3 at, int normal) /* damage */
{
    if (b->nrv == SFB_DEAD || b->nrv == SFB_BREAK) return 0;
    camera_shake(normal ? SHAKE_NORMAL : SHAKE_WEAK);
    sf_air_bubble(at);
    sfb_nrv(b, SFB_BREAK);
    printf("skeletal fish baby row %d: break\n", b->row);
    return 1;
}
static void sfb_pose(SfBaby *b) /* calcAndSetBaseMtx, calcAnim with the joint delegators */
{
    const Model *md = sfb_model();
    if (!b->rc.p) return;
    sf_rail_mtx(&b->rc, 0.0f, b->base);
    tb_inv(b->inv, b->base);
    if (!md || !md->skin || md->skin->njoints <= 12) return;
    Anim *a = b->anim;
    if (a->skin != md->skin) { anim_bind(a, md->skin); anim_play(a, "snakefish/swim", 1.0f, 1, 0); }
    anim_pose(a);
    for (int k = 0; k < 4; k++) { /* createJointDelegatorWithNullMtxFunc: after its children (they keep the clip's) */
        float w[16];
        sf_joint_calc(&b->rc, a->world[sf_snake_j[k]], w);
        memcpy(b->wj[k], w, 64);
        tb_mul(a->world[sf_snake_j[k]], b->inv, w);
    }
    anim_skin(a);
}
static V3 sfb_sensor_pos(const SfBaby *b, int k) { return tb_mulp(b->wj[sfb_sensor[k].j], sfb_sensor[k].off); }
static void sfb_update(SfBaby *b)
{
    const Row *r = &sc->objs[b->row];
    if (b->dead || !row_shown(b->row) || !b->rc.p) return;
    if (!dx_row_moves(b->row)) return;
    int st = ++b->st;
    switch (b->nrv) {
    case SFB_SWIM:
        if (st == 0 && b->anim->skin) anim_start(b->anim, "snakefish/swim", 0);
        b->speed += 0.1f; /* sAccel */
        if (b->speed > b->smax) b->speed = b->smax;
        break;
    case SFB_BREAK:
        if (st == 0) { anim_start(b->anim, "snakefish/death", 0); sfb_se(b, SE_EM_EXPLODE_UNDER_WATER); sfb_se(b, SE_EV_SNAKEHEAD_DAMAGE); }
        if (st > 0 && b->anim->done) sfb_nrv(b, SFB_DEAD);
        break;
    case SFB_DEAD: { /* appearStarPiece at Center, kill */
        V3 c = tb_mulp(b->base, v3(b->anim->world[1][12], b->anim->world[1][13], b->anim->world[1][14]));
        launch_pieces(c, up_at(c, v3(0, 1, 0)), 10, 10.0f, 40.0f);
        sfb_se(b, SE_OJ_STAR_PIECE_BURST_W_F);
        sfb_kill(b);
        return;
    }
    }
    b->rc.speed = b->speed; /* control */
    sf_rail_update(&b->rc);
    if (b->nrv == SFB_SWIM) b->lv = se_level(SE_EM_LV_SNAKEFISH_SWIM, &b->base[12], b->lv);
    sfb_pose(b);
    (void)r;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int k = 0; k < 4; k++) { /* attackSensor */
        V3 sp = sfb_sensor_pos(b, k);
        if (vlen(vsub(c, sp)) >= sfb_sensor[k].r + HIT_BODY_R) continue;
        if (sfb_attackable(b) && !mario->hurt_timer && mario_attack(ATK_STRONG, sp)) camera_shake(SHAKE_STRONG); /* sendMsgEnemyAttackStrong */
        else mario_push_out(sp, sfb_sensor[k].r); /* sendMsgPush */
        break;
    }
}
static int sf_jet_hit(V3 p) /* a thrown JetTurtle at p (isMsgJetTurtleAttack): 1 taken */
{
    for (int i = 0; i < nsfb; i++) {
        SfBaby *b = &sfb[i];
        if (b->dead || !row_shown(b->row) || !b->rc.p) continue;
        for (int k = 0; k < 4; k++)
            if (vlen(vsub(p, sfb_sensor_pos(b, k))) < sfb_sensor[k].r + 60.0f) return sfb_damage(b, p, 1);
    }
    return sfk_jet_hit(p);
}
static void sfx_update(void) /* ExterminationChecker (power star kind) over its zone's ChildSkeletalFishBaby rows */
{
    if (sfx.row < 0 || sfx.nrv == 3 || !row_shown(sfx.row)) return;
    int st = ++sfx.st;
    if (sfx.nrv == 0) { /* exeWatching */
        for (int i = 0; i < nsfb; i++)
            if (sfb[i].child && sc->objs[sfb[i].row].zone == sc->objs[sfx.row].zone && !sfb[i].dead) return;
        sfx.nrv = 1; sfx.st = -1;
        printf("extermination skeletal fish row %d: all down\n", sfx.row);
    } else if (sfx.nrv == 1) { /* exeTryStartDemoAppear */
        if (st == 0) bgm_stop(10);
        if (st > 5 && pd_try_start("全滅スター出現", sfx.row)) { sfx.nrv = 2; sfx.st = -1; }
    } else if (sfx.nrv == 2) { /* exeAppearStar */
        if (st == 0) se_start(SE_SY_TOTAL_COMPLETE, NULL);
        if (st == 120) { pd_end("全滅スター出現"); bgm_after_boss(); row_kill(sfx.row); sfx.nrv = 3; } /* requestAppearPowerStar, kill */
    }
}
static void update_sf(void)
{
    for (int i = 0; i < nsfb; i++) sfb_update(&sfb[i]);
    sfx_update();
    update_sfk();
}
static void sf_draw(void)
{
    const Model *md = sfb_model();
    for (int i = 0; md && md->skin && i < nsfb; i++) {
        SfBaby *b = &sfb[i];
        if (b->dead || !row_shown(b->row) || !b->rc.p || b->anim->skin != md->skin) continue;
        float c[3] = {b->base[12], b->base[13], b->base[14]};
        if (!render_visible(c, md->radius + 700.0f)) continue;
        use_row_light(b->row, v3(c[0], c[1], c[2]), LIGHT_STRONG);
        draw_skin(md, b->base, b->anim);
    }
    sfk_draw();
}
static void sf_init(const Scene *s, const unsigned char *blob)
{
    nsfb = 0; sfx.row = -1;
    sfk_init(s, blob);
    int t1 = scene_type(s, "SkeletalFishBaby"), t2 = scene_type(s, "ChildSkeletalFishBaby"), tx = scene_type(s, "ExterminationSkeletalFishBaby");
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type == tx && tx >= 0) { sfx.row = (int)i; sfx.nrv = 0; sfx.st = -1; continue; }
        if (((int)r->type != t1 || t1 < 0) && ((int)r->type != t2 || t2 < 0)) continue;
        if (nsfb >= SF_BABIES) break;
        SfBaby *b = &sfb[nsfb];
        Anim *a = mem_alloc(MEM_PLAYER, sizeof(Anim));
        memset(b, 0, sizeof *b); memset(a, 0, sizeof(Anim));
        b->anim = a; b->row = (int)i; b->child = (int)r->type == t2;
        b->smax = r->arg[0] >= 0 ? (float)r->arg[0] : 20.0f; /* getJMapInfoArg0NoInit (sSpeed); arg1 mObjArg1 is never read */
        b->speed = b->smax;
        sf_rail_init(&b->rc, blob, r, row_pos(r), b->speed);
        sfb_nrv(b, SFB_SWIM); /* makeActorAppeared */
        printf("skeletal fish baby row %d: rail %u points (%s, %.0f), speed %.0f\n", (int)i, b->rc.n, b->rc.cl ? "loop" : "open", b->rc.len, b->smax);
        nsfb++;
    }
}
/* Test builds (XF=-DSF_TEST): TearDropGalaxy/1 (ChildSkeletalFishBaby + its ExterminationSkeletalFishBaby): every 240
 * frames a JetTurtle thrown at the next live baby from 500 behind its head (test only: no swim to it), till the
 * checker's star; then Mario to the star */
int game_test_sf(int frame)
{
    static int last = -1000;
    if (!nsfb) return -1;
    if (frame % 60 == 0)
        for (int i = 0; i < nsfb; i++)
            printf("sf test f %d: baby row %d nrv %d dead %d coord %.0f / %.0f at %.0f %.0f %.0f; checker %d\n", frame, sfb[i].row, sfb[i].nrv, sfb[i].dead,
                   sfb[i].rc.coord, sfb[i].rc.len, sfb[i].base[12], sfb[i].base[13], sfb[i].base[14], sfx.nrv);
    if (sfx.nrv == 3) { /* to its star (arg7: the declarer) */
        int ty = scene_type(sc, "PowerStar");
        for (u32 i = 0; frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].arg[7] == sfx.row && !demo_on) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 100;
    }
    if (frame < 120 || frame - last < 240 || jet.inst >= 0 || demo_on) return 0;
    for (int i = 0; i < nsfb; i++) {
        SfBaby *b = &sfb[i];
        if (b->dead || b->nrv != SFB_SWIM || !row_shown(b->row)) continue;
        for (int k = 0; k < ninsts; k++) {
            Inst *in = &insts[k];
            if (in->jet != 1 || row_step[in->row]) continue;
            V3 h = sfb_sensor_pos(b, 0), d = tb_col(b->base, 2);
            in->head = vsub(h, vmul(d, -500.0f)); in->hvel = vmul(d, -25.0f); /* from before it, against its way */
            jet.inst = k; jet.phase = 2; jet.t = 0; jet.row = in->row;
            last = frame;
            printf("sf test f %d: shell row %d thrown at baby row %d\n", frame, in->row, b->row);
            return 1;
        }
    }
    return 0;
}
