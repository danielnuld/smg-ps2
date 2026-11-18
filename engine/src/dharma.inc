/* dharma.inc: DharmaSambo.cpp after Petari (#included by game.c; ds_*). A stack of four DharmaSamboParts (its model's
 * Spine1-4 joints hidden, the parts at their FixedPositions: the joint turned (0, -90, -90)); the top one the head
 * (setHead: its Face / Eye / Flower materials, the others their Body one: slice.py DharmaSamboPartsHead / Body).
 * WaitUnderGround (HideWait, MOVE_UNDER level SE, turning to Mario 3 a frame) until Mario is within 800 past 120
 * frames: Appear (its parts too; SFSAMBO_APPEAR at 60) -> Pursue (Wait: towards Mario at 0.3 beyond its distance
 * (100 / 200 / 300 / 400 by the parts left) + 100, away at -0.25 nearer than it, else turning; there passing Wait's
 * frame 120: Attack (Attack, its head's Attack effect and SFSAMBO_ATTACK at frame 110, turning until frame 90 (75 with
 * two parts, at rate 1.2)); Hide (Hide, PRE_HIDE at 30, SFSAMBO_HIDE at 65; 300 frames, 360 with the head alone) past
 * 2000 from Mario, 1250 from home or its 600-frame timer; then HideAppear (every part back, SAMBOHEAD_APPEAR) and
 * WaitUnderGround again. A spin on a part (the nearest hit, in Pursue / Attack / the escape / SpinHitted past 50):
 * the head -> Killed (Hit, then dead); another -> it is blown off (Blow: SpinHitController 15 frames at 40 / 20,
 * BLOW SE), the stack jumps 10 / 30 away (SpinHitted: SpinReaction, the parts' Land at frame 27, back to Pursue once
 * landed and stopped; the parts above slide down half way a frame); with the head alone left it escapes (EscapeLand /
 * EscapeRun: the head's Land / Run at 1.2, hopping 7 / 30 away, or towards Mario jumping 30 beyond 800). Trampling
 * the head (hip drop too): TrampleDeath (Down, STOMPED_S), or out of the ground (AppearImmediately, AwayJump) from
 * WaitUnderGround / HideAppear; a body part's trample: DHARMASAMBO_TRAMPLE only. Its parts hurt Mario (EnemyAttack)
 * unless he is above them or hip dropping (a push then), the head only in Attack and the escape. kill: each part's
 * Death and EXPLODE_S, the head's six star pieces, its dead switch.
 * ponytail: the 2P pointer's bind (StarPointerBind / BindAfter), the parts' BTKs (Press, Run...), their shadows, the
 * anim scale controller; a part's place before its stack was first drawn: 160 x its index up the stack */
#define DS_MAX 4
enum { DH_WAITUNDER, DH_APPEAR, DH_APPEARIMM, DH_PURSUE, DH_ATTACK, DH_HIDE, DH_HIDEAPPEAR, DH_SPINHITTED, DH_KILLED,
       DH_ESCAPERUN, DH_ESCAPELAND, DH_ESCAPEWAITRUN, DH_ESCAPEWAITLAND, DH_TRAMPLEDEATH };
typedef struct { int idx, orig, head, alive, blow, bst; V3 pos, vel, ax[3]; char act[16]; float t, rate; int seq; } DsPart;
static struct { int k, count, hide_t; V3 init; DsPart p[4]; float fix[4][16]; int fix_ok; } ds[DS_MAX];
static int nds;
static const Scene *ds_sc;
static int ds_of(const Kuribo *k)
{
    int ki = (int)(k - kuribo);
    if (ds_sc != sc) { ds_sc = sc; nds = 0; }
    for (int i = 0; i < nds; i++) if (ds[i].k == ki) return i;
    if (nds == DS_MAX) return -1;
    memset(&ds[nds], 0, sizeof ds[0]); ds[nds].k = ki;
    return nds++;
}
static void ds_pact(DsPart *p, const char *a) { snprintf(p->act, sizeof p->act, "%s", a); p->t = 0; p->rate = 1.0f; p->seq++; } /* MR::startAction(part) */
static int ds_pend(const DsPart *p) /* isActionEnd(part) */
{
    char n[48]; int mode;
    snprintf(n, sizeof n, "dharmasambopartshead/%s", p->act);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    int fr = anim_clip_info(n, &mode);
    return fr > 0 && mode != 2 && mode != 4 && p->t >= fr;
}
static void ds_all(int d, const char *a) { for (int i = 0; i < 4; i++) ds_pact(&ds[d].p[i], a); } /* startActionAllParts */
static DsPart *ds_head(int d) { for (int i = 0; i < 4; i++) if (ds[d].p[i].head) return &ds[d].p[i]; return &ds[d].p[3]; }
static int ds_pass(const Kuribo *k, float f) { return k->act_t - k->act_rate < f && k->act_t >= f; } /* checkPassBckFrame */
static void ds_move(Kuribo *k, float a, float g, float d, float t) /* moveAndTurnToPlayer */
{
    w_move_turn(k, vsub(mario->pos, k->pos), a, g, d, t);
}
static void ds_part_kill(Kuribo *k, int d, DsPart *p)
{
    float m[16];
    p->alive = 0;
    basis_matrix(m, p->pos, p->ax[1], p->ax[2], 0);
    fx_named("DharmaSamboParts", "Death", m, NULL, NULL);
    se_start(SE_EM_EXPLODE_S, &p->pos.x);
    if (p->head) { launch_pieces(p->pos, vmul(k->grav, -1.0f), 6, 10.0f, 40.0f); se_start(SE_OJ_STAR_PIECE_BURST, &p->pos.x); } /* appearStarPiece(remnant 6, 10, 40) */
    (void)d;
}
static void ds_kill(Kuribo *k, int d) /* DharmaSambo::kill */
{
    const Row *r = &sc->objs[k->row];
    for (int i = 0; i < 4; i++) if (ds[d].p[i].alive) ds_part_kill(k, d, &ds[d].p[i]);
    if (r->sw[SW_DEAD] >= 0) sw_set((Row *)r, SW_DEAD, 1);
    k->state = KURIBO_DEAD; row_kill(k->row);
    printf("dharma sambo row %d dead%c", k->row, 10);
}
static int ds_hide_dec(Kuribo *k, int d, int hide) /* decCountAndTryToHide */
{
    if (--ds[d].hide_t <= 0 && hide) { w_nrv(k, DH_HIDE); return 1; }
    return 0;
}
/* a part's place: its FixedPosition (the host's Spine joint, turned (0, -90, -90)) from the last pose drawn */
static void ds_fixed(int d, int i, V3 *pos, V3 ax[3], const Kuribo *k)
{
    if (ds[d].fix_ok) {
        const float *f = ds[d].fix[i];
        *pos = v3(f[12], f[13], f[14]);
        for (int j = 0; j < 3; j++) ax[j] = vnorm(v3(f[4 * j], f[4 * j + 1], f[4 * j + 2]));
        return;
    }
    V3 up = vmul(k->grav, -1.0f);
    *pos = vadd(k->pos, vmul(up, 160.0f * i));
    ax[1] = up; ax[2] = vnorm(vtangent(k->front, up)); ax[0] = vcross(ax[1], ax[2]);
}
static void ds_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    int d = ds_of(k);
    if (d < 0) return;
    float s = w_scale(k);
    if (!k->front.x && !k->front.y && !k->front.z) { /* init */
        V3 x, z;
        row_axes(r, &x, &z);
        k->front = vnorm(z); ds[d].init = k->pos;
        k->grav = vmul(walker_up(k, k->up), -1.0f);
        k->qy = vmul(k->grav, -1.0f); k->qz = k->front;
        ds[d].count = 4; ds[d].hide_t = 600; /* hAppearTime */
        for (int i = 0; i < 4; i++) { DsPart *p = &ds[d].p[i]; p->orig = p->idx = i; p->head = i == 3; p->alive = 1; ds_fixed(d, i, &p->pos, p->ax, k); }
        w_nrv(k, r->sw[SW_APPEAR] >= 0 ? DH_HIDEAPPEAR : DH_WAITUNDER); /* (appear(): HideAppear) */
    }
    k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    if (ds[d].count == 1 && !ds_head(d)->alive) { ds_kill(k, d); return; }
    float near = vlen(vsub(mario->pos, k->pos));
    int st = ++k->step;
    int cnt = ds[d].count;
    switch (k->nrv) {
    case DH_WAITUNDER:
        if (st == 0) { w_act(k, "HideWait"); ds_all(d, "HideWait"); k->pos = ds[d].init; k->vel = v3(0, 0, 0); }
        w_se_lv(k, SE_EM_LV_DHARMASAMBO_MOVE_UNDER);
        ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f); /* hOnlyTurnParam */
        if (st > 120 && near < 800.0f) w_nrv(k, DH_APPEAR);
        break;
    case DH_APPEAR: case DH_APPEARIMM:
        if (st == 0) {
            ds[d].count = 4; w_act(k, "Appear"); k->vel = v3(0, 0, 0); ds[d].hide_t = 600; ds_all(d, "Appear");
            if (k->nrv == DH_APPEARIMM) k->act_t = 59.0f; /* setBckFrameAndStop 59, setBckRate 1 */
        }
        if (st == 60) w_se(k, SE_EM_SFSAMBO_APPEAR);
        ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f);
        if (w_bck_stopped(k)) w_nrv(k, DH_PURSUE);
        break;
    case DH_PURSUE: {
        if (st == 0 && strcmp(k->act, "Wait")) { w_act(k, "Wait"); ds_all(d, "Wait"); }
        w_se_lv(k, SE_EM_LV_DHARMASAMBO_MOVE);
        if (near > 2000.0f || vdot(vsub(k->pos, ds[d].init), vsub(k->pos, ds[d].init)) > 1250.0f * 1250.0f) { w_nrv(k, DH_HIDE); break; }
        static const float stop_d[4] = {100.0f, 200.0f, 300.0f, 400.0f}; /* hPursueEscapeDist */
        float sd = stop_d[cnt - 1];
        int in_range = 0;
        if (near > sd + 100.0f) ds_move(k, 0.3f, 1.0f, 0.97f, 3.0f); /* hPursueGoParam */
        else if (near < sd) ds_move(k, -0.25f, 1.0f, 0.97f, 3.0f); /* hPursueEscapeParam */
        else { ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f); in_range = 1; }
        if (in_range && ds_pass(k, 120.0f)) { w_nrv(k, DH_ATTACK); break; } /* hToAttackTime */
        ds_hide_dec(k, d, 1);
        break;
    }
    case DH_ATTACK: {
        if (st == 0) {
            if (strcmp(k->act, "Attack")) { w_act(k, "Attack"); k->vel = v3(0, 0, 0); ds_all(d, "Attack"); }
            if (cnt == 2) k->act_rate = 1.2f;
        }
        float turn = cnt == 2 ? 75.0f : 90.0f; /* hAttackTurnTime */
        if (k->act_t > turn) ds_move(k, 0.0f, 1.0f, 0.8f, 0.0f); /* hNoMoveNoTurnParam */
        else ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f);
        int end = w_bck_stopped(k);
        if (ds_pass(k, 110.0f)) {
            DsPart *h = ds_head(d);
            float m[16];
            basis_matrix(m, h->pos, h->ax[1], h->ax[2], 0);
            fx_named("DharmaSamboParts", "Attack", m, NULL, NULL);
            w_se(k, SE_EM_SFSAMBO_ATTACK);
        }
        if (!ds_hide_dec(k, d, end) && end) w_nrv(k, DH_PURSUE);
        break;
    }
    case DH_SPINHITTED:
        if (st == 0 && strcmp(k->act, "SpinReaction")) w_act(k, "SpinReaction");
        if (w_on_ground(k)) ds_move(k, 0.0f, 1.0f, 0.8f, 0.0f); /* hSpinHittedOnGroundParam */
        else ds_move(k, 0.0f, 2.0f, 0.97f, 0.0f);                /* hSpinHittedAirParam */
        if (cnt == 1) {
            if (w_on_ground(k)) { ds[d].hide_t = 300; w_nrv(k, DH_ESCAPELAND); } /* hEscapeTime */
        } else {
            if (ds_pass(k, 27.0f)) ds_all(d, "Land"); /* hSpinHittedLandTime */
            if (w_on_ground(k) && w_bck_stopped(k)) w_nrv(k, DH_PURSUE);
        }
        break;
    case DH_ESCAPERUN: case DH_ESCAPEWAITRUN: case DH_ESCAPELAND: case DH_ESCAPEWAITLAND: {
        DsPart *h = ds_head(d);
        int run = k->nrv == DH_ESCAPERUN || k->nrv == DH_ESCAPEWAITRUN, wait = k->nrv == DH_ESCAPEWAITRUN || k->nrv == DH_ESCAPEWAITLAND;
        if (st == 0) {
            if (strcmp(h->act, run ? "Run" : "Land")) { ds_pact(h, run ? "Run" : "Land"); if (run) w_se(k, SE_EM_SAMBOHEAD_HOP); }
            h->rate = 1.2f;
        }
        V3 away = vtangent(vsub(k->pos, mario->pos), k->grav);
        away = vlen(away) > 1e-3f ? vnorm(away) : v3(0, 0, 0);
        if (wait) { if (w_on_ground(k)) ds_move(k, 0.0f, 1.0f, 0.7f, 5.0f); else ds_move(k, 0.0f, 2.0f, 0.98f, 5.0f); } /* hEscapeWait*Param */
        else if (!run || w_on_ground(k)) w_move_turn(k, away, 0.0f, 1.0f, 0.7f, 5.0f); /* hEscapeOnGroundParam */
        else w_move_turn(k, away, 0.15f, 2.0f, 0.98f, 5.0f);                              /* hEscapeAirParam */
        ds_hide_dec(k, d, !run);
        if (k->nrv == DH_HIDE) break;
        if (run) { if (w_on_ground(k) && ds_pend(h)) w_nrv(k, wait ? DH_ESCAPEWAITLAND : DH_ESCAPELAND); }
        else if (ds_pend(h)) {
            if (near > 800.0f) { k->vel = vsub(k->vel, vmul(k->grav, 30.0f)); w_nrv(k, DH_ESCAPEWAITRUN); } /* hEscapeWaitDist, addVelocityJump 30 */
            else { k->vel = vadd(vmul(away, 7.0f), vmul(k->grav, -30.0f)); w_nrv(k, DH_ESCAPERUN); } /* addVelocitySeparateHV 7, 30 */
        }
        break;
    }
    case DH_TRAMPLEDEATH:
        if (st == 0) { k->act_rate = 0.0f; ds_pact(ds_head(d), "Down"); w_se(k, SE_EM_STOMPED_S); } /* stopBck */
        if (w_on_ground(k)) ds_move(k, 0.0f, 1.0f, 0.8f, 0.0f); else ds_move(k, 0.0f, 2.0f, 0.97f, 0.0f);
        if (ds_pend(ds_head(d))) { ds_kill(k, d); return; }
        break;
    case DH_HIDE:
        if (st == 0) { w_act(k, "Hide"); k->vel = v3(0, 0, 0); ds_all(d, "Hide"); if (cnt == 1) k->act_rate = 1.2f; }
        if (st == 30) w_se(k, SE_EM_DHARMASAMBO_PRE_HIDE);
        if (st == 65) w_se(k, SE_EM_SFSAMBO_HIDE);
        ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f);
        if (st > (cnt == 1 ? 360 : 300)) { k->pos = ds[d].init; k->vel = v3(0, 0, 0); w_nrv(k, DH_HIDEAPPEAR); } /* resetPosition */
        break;
    case DH_HIDEAPPEAR:
        if (st == 0) {
            w_act(k, "HideAppear"); w_se(k, SE_EM_SAMBOHEAD_APPEAR);
            for (int i = 0; i < 4; i++) {
                DsPart *p = &ds[d].p[i];
                p->alive = 1; p->idx = i; p->head = i == 3; p->blow = 0;
                ds_pact(p, "Wait"); p->t = 5.0f; p->rate = 0.0f; /* setBckFrameAndStop 5 */
                ds_fixed(d, i, &p->pos, p->ax, k);
            }
            ds_all(d, "HideAppear");
        }
        ds_move(k, 0.0f, 1.0f, 0.97f, 3.0f);
        if (w_bck_stopped(k)) w_nrv(k, DH_WAITUNDER);
        break;
    case DH_KILLED:
        if (st == 0) { k->act_rate = 0.0f; for (int i = 0; i < 4; i++) if (ds[d].p[i].idx != -1) ds[d].p[i].rate = 0.0f; w_act(k, "Hit"); k->vel = v3(0, 0, 0); }
        ds_move(k, 0.0f, 1.0f, 0.8f, 0.0f);
        if (w_bck_stopped(k)) { ds_kill(k, d); return; }
        break;
    }
    k->act_t += k->act_rate;
    w_bind(k, 80.0f * s); /* initBinder(80 x scale, 80 x scale) */
    k->qy = vmul(k->grav, -1.0f); k->qz = vnorm(vtangent(k->front, k->qy));
    if (vlen(k->qz) < 1e-3f) k->qz = k->front;
    k->up = k->qy;
    /* the parts: their actions, blown ones (SpinHitController 15 / 10 / 3, 0, 40 / 20), the others on their places */
    for (int i = 0; i < 4; i++) {
        DsPart *p = &ds[d].p[i];
        if (!p->alive) continue;
        p->t += p->rate;
        if (p->blow) {
            if (p->bst == 0) scene_stop = 3;
            p->pos = vadd(p->pos, p->vel);
            if (++p->bst > 15) { ds_part_kill(k, d, p); continue; }
            continue;
        }
        if (p->idx < 0) continue;
        V3 fp, fa[3];
        ds_fixed(d, p->idx, &fp, fa, k);
        if (k->nrv == DH_SPINHITTED) p->pos = vadd(p->pos, vmul(vsub(fp, p->pos), 0.5f)); /* hSpinHittedMoveBodyRate */
        else p->pos = fp;
        memcpy(p->ax, fa, sizeof fa);
    }
    /* the parts' sensors (80 x scale at 80 x scale up each): Mario's offence, then their attack */
    int hit_ok = k->nrv == DH_PURSUE || k->nrv == DH_ATTACK || k->nrv == DH_ESCAPERUN || k->nrv == DH_ESCAPELAND || (k->nrv == DH_SPINHITTED && k->step > 50);
    if (k->nrv == DH_KILLED || k->nrv == DH_TRAMPLEDEATH) return;
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    DsPart *hit = NULL;
    float hd = 1e9f;
    for (int i = 0; i < 4; i++) {
        DsPart *p = &ds[d].p[i];
        if (!p->alive || p->blow) continue;
        V3 c = vadd(p->pos, vmul(p->ax[1], 80.0f * s));
        int msg = mario_offence(&ds[d].p[i], c, 80.0f * s, k->grav);
        if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) {
            if (msg == MSG_TRAMPLE) mario_trample();
            if (!p->head) { se_start(SE_EM_DHARMASAMBO_TRAMPLE, &p->pos.x); return; }
            if (k->nrv == DH_WAITUNDER || k->nrv == DH_HIDEAPPEAR) { /* AppearImmediately, sendMsgAwayJump */
                w_nrv(k, DH_APPEARIMM);
                mario->vup = 30.0f; mario->grounded = 0;
                return;
            }
            if (k->nrv != DH_APPEAR && k->nrv != DH_APPEARIMM && k->nrv != DH_HIDE) {
                w_nrv(k, DH_TRAMPLEDEATH); se_start(SE_EM_DHARMASAMBO_TRAMPLE, &p->pos.x);
                printf("dharma sambo row %d: head trampled%c", k->row, 10);
            }
            return;
        }
        if (msg == MSG_HITALL && hit_ok) { /* isMsgPlayerSpinAttack: mHitDistance, the nearest */
            float l = vlen(vsub(mc, c));
            if (l < hd) { hd = l; hit = p; }
            continue;
        }
        /* attackSensor */
        if (!mario_touch(c, 80.0f * s)) continue;
        V3 tu = vsub(mc, c);
        int above = vlen(tu) > 1e-6f && vdot(vnorm(tu), p->ax[1]) > 0.25f; /* isPlayerExistUp(80, 0.25) */
        int hip = mario->hip == HIP_FALL || mario->hip == HIP_LAND;
        int push;
        if (p->head) {
            if (k->nrv == DH_ESCAPERUN || k->nrv == DH_ESCAPELAND) push = above || hip;
            else push = k->nrv == DH_WAITUNDER || k->nrv == DH_HIDE || k->nrv == DH_HIDEAPPEAR || k->nrv == DH_APPEARIMM || above || hip || k->nrv != DH_ATTACK;
        } else push = (k->nrv == DH_SPINHITTED && k->step < 30) || k->nrv == DH_WAITUNDER || k->nrv == DH_APPEARIMM ||
                      k->nrv == DH_HIDE || k->nrv == DH_HIDEAPPEAR || above || hip;
        if (!push && !mario->hurt_timer && mario_attack(ATK_ENEMY, c)) continue;
        V3 out = vtangent(vsub(mc, c), mario->up);
        float l = vlen(out), want = 80.0f * s + HIT_BODY_R * 0.6f;
        if (l > 1e-3f && l < want) mario->pos = vadd(mario->pos, vmul(out, (want - l) / l));
    }
    if (hit) { /* control: the nearest part hit */
        V3 c = vadd(hit->pos, vmul(hit->ax[1], 80.0f * s));
        float m[16];
        basis_matrix(m, vmul(vadd(c, mc), 0.5f), k->qy, k->qz, 0);
        fx_named("DharmaSambo", "HitMarkNormal", m, NULL, NULL);
        ds[d].count--;
        scene_stop = 5; /* stopSceneForDefaultHit(hHitStopTime) */
        hit->idx = -1; hit->blow = 1; hit->bst = 0; ds_pact(hit, "Blow");
        hit->vel = vsub(vmul(vnorm(vtangent(vsub(hit->pos, mario->pos), k->grav)), 40.0f), vmul(k->grav, 20.0f)); /* SpinHitController::start */
        se_start(SE_PM_SPIN_HIT, &hit->pos.x); se_start(SE_EM_DHARMASAMBO_BLOW, &hit->pos.x);
        if (hit->head) { w_nrv(k, DH_KILLED); printf("dharma sambo row %d: head blown%c", k->row, 10); }
        else {
            V3 away = vtangent(vsub(k->pos, vsub(mc, vmul(mario->up, 0.0f))), k->grav);
            k->vel = vadd(vlen(away) > 1e-3f ? vmul(vnorm(away), 10.0f) : v3(0, 0, 0), vmul(k->grav, -30.0f)); /* addVelocitySeparateHV 10, 30 */
            k->act_rate = 0.0f; /* stopBck */
            w_nrv(k, DH_SPINHITTED);
            int n = 0;
            for (int i = 0; i < 4; i++) if (ds[d].p[i].idx != -1) ds[d].p[i].idx = n++;
            printf("dharma sambo row %d: part blown, %d left%c", k->row, ds[d].count, 10);
        }
    }
}
/* drawn: the host (its Spine joints hidden: hideJoint) and its parts at its joints' FixedPositions */
static void ds_draw(Kuribo *k, const Model *md, Anim *a)
{
    int d = ds_of(k);
    float m[16];
    anim_pose(a);
    basis_matrix(m, k->pos, k->qy, k->qz, 0);
    const float *sc3 = sc->objs[k->row].scale;
    for (int j = 0; j < 3; j++) { m[j] *= sc3[0]; m[4 + j] *= sc3[1]; m[8 + j] *= sc3[2]; }
    if (d >= 0 && a->skin->njoints > 5) {
        static const float rx[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (int i = 0; i < 4; i++) { /* FixedPosition(Spine i+1, (0, -90, -90)): Rx(0) Ry(-90) Rz(-90) */
            float jm[16], rm[16], z0[3] = {0, 0, 0}, rot[3] = {0, -90.0f, -90.0f}, one[3] = {1, 1, 1};
            mat_mul(jm, m, a->world[2 + i]);
            mat_translate_rot_scale(rm, z0, rot, one);
            mat_mul(ds[d].fix[i], jm, rm);
            for (int c = 0; c < 3; c++) { V3 v = vnorm(v3(ds[d].fix[i][4 * c], ds[d].fix[i][4 * c + 1], ds[d].fix[i][4 * c + 2])); ds[d].fix[i][4 * c] = v.x; ds[d].fix[i][4 * c + 1] = v.y; ds[d].fix[i][4 * c + 2] = v.z; }
        }
        (void)rx;
        ds[d].fix_ok = 1;
        for (int i = 0; i < 4; i++) memset(a->world[2 + i], 0, 48); /* hideJoint: their shapes collapse */
    }
    anim_skin(a);
    draw_skin(md, m, a);
    if (d < 0) return;
    static Anim pa; /* the parts posed one at a time */
    const Model *mh = scene_model(sc, "DharmaSamboPartsHead"), *mb = scene_model(sc, "DharmaSamboPartsBody");
    for (int i = 0; i < 4; i++) {
        DsPart *p = &ds[d].p[i];
        const Model *pm = p->head ? mh : mb;
        if (!p->alive || !pm || !pm->skin) continue;
        if (pa.skin != pm->skin) anim_bind(&pa, pm->skin);
        char n[48];
        snprintf(n, sizeof n, "dharmasambopartshead/%s", p->act);
        for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
        anim_start(&pa, n, 0);
        int mode, fr = anim_clip_info(n, &mode);
        float t = p->t;
        if (fr > 0) t = mode == 2 || mode == 4 ? fmodf(t, (float)fr) : (t > fr ? (float)fr : t);
        pa.frame = t; pa.speed = 0;
        anim_update(&pa);
        float pmx[16];
        basis_matrix(pmx, p->pos, p->ax[1], p->ax[2], 0);
        draw_skin(pm, pmx, &pa);
    }
}
#ifdef DS_TEST /* XF=-DDS_TEST (SandClockGalaxy, its DharmaSambo's scenario): Mario 500 off the first; spins at its parts once it pursues */
int game_test_ds(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_DHARMA || k->state == KURIBO_DEAD || !row_shown(k->row)) continue;
        int d = ds_of(k);
        V3 up = vmul(k->grav, -1.0f);
        if (!k->front.x && !k->front.y && !k->front.z) up = up_at(k->pos, mario->up);
        static int t;
        t++;
        if (k->nrv == DH_WAITUNDER && vlen(vsub(mario->pos, k->pos)) > 900.0f) {
            V3 f = vnorm(vtangent(v3(1, 0, 0), up));
            mario->up = up; mario->pos = vadd(vadd(k->pos, vmul(f, 500.0f)), vmul(up, 50.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        }
        if ((k->nrv == DH_PURSUE || k->nrv == DH_ATTACK) && t % 90 == 0 && d >= 0) { /* beside the stack, a spin */
            V3 f = vnorm(vtangent(vsub(mario->pos, k->pos), up));
            mario->pos = vadd(k->pos, vmul(f, 150.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hurt_timer = 0;
            mario->spin_timer = SPIN_TIME;
        }
        if (t % 60 == 0) printf("ds test: row %d nrv %d step %d count %d%c", k->row, k->nrv, k->step, d >= 0 ? ds[d].count : -1, 10);
        return k->nrv;
    }
    return -1;
}
#endif
