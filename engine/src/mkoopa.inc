/* MechanicKoopaMini.cpp after Petari (game.c includes it after teresa.inc; kr_move is Karon's moveAndTurnToDirection).
 * Kuribo fields: nrv / step its nerve, qy / qz its rotation's up / front (calcRotate: its up eased 0.1 to the ground's
 * normal), front the turned front, dir / lva[1] its rail's direction / coord, lvt[0] invalidateHitSensors, lvt[1] its
 * fire sensor valid, lvt[2] invalidateClipping, lv its level sound.
 * Binder 120 at (0, 110, 20) of its base; body sensor 130 x scale at JointRoot (48, 13, 0), fire sensor 29 x scale
 * 130 x scale up and up to 400 (eased in from step 10 to 30) ahead towards Mario's centre. On its SW_APPEAR: JumpStart
 * (arg0 set) or Wait; else WalkOnRail (hWalk 0.4 / 2 / 0.8 / 2, 600 ahead; in the air 0.2 / 2 / 0.98 / 2): at a map
 * wall straight ahead (front . normal < -0.99) JumpStart (45 up 3 frames before its clip's end, JumpEnd), at the rail's
 * end Turn (to the rail's way, then on within 5 degrees), 120 frames Wait (unless arg1 is set), Mario within 900 and
 * 120 degrees: Find (26 up), Pursue (10 up, hPursue 1.3; a wall ahead: JumpStart; past 1100 back to its rail; within 300
 * and 0.1 rad on the ground PreFire, Fire 120 (its flame: Fire effect, the fire sensor burns Mario), FireEnd). Its body
 * pushes Mario (not hip-dropping, not over it by 130 less than the two radii); a trample under 80 less than the radii:
 * Trample (TRAMPLE SE), a spin or a star piece SpinHit (120), both then Fire; a hip drop: HipDropped (Break, BREAK SE,
 * then dead and a coin). An explosion: dead and a coin; a death area, water, a black hole: dead. kill: Death,
 * EXPLODE_S, SW_DEAD. ponytail: no star pointer bind (WalkerStateBindStarPointer: the 2P pointer; its
 * AnimScaleController only moves under it), no move limit collision (trySetMoveLimitCollision), pushes only between
 * MechanicKoopaMinis; its Fire effect kept while it is drawn (its joint) */
enum { MKM_WAIT, MKM_WALKONRAIL, MKM_TURN, MKM_JUMPSTART, MKM_JUMPEND, MKM_FIND, MKM_PURSUE, MKM_PREFIRE, MKM_FIRE,
       MKM_FIREEND, MKM_ATTACKHIT, MKM_SPINHIT, MKM_TRAMPLE, MKM_HIPDROPPED };
static void mkm_nrv(Kuribo *k, int n) /* setNerve: FireAttack's onEnd (deleteEffect Fire, the fire sensor off) */
{
    if (k->nrv == MKM_FIRE) k->lvt[1] = 0;
    w_nrv(k, n);
}
/* moveAndTurnToDirection with calcRotate: from its rotation's front, its up eased to the ground */
static void mkm_move(Kuribo *k, V3 dir, float a, float g, float f, float deg)
{
    k->front = k->qz; /* calcFrontVec */
    kr_move(k, dir, a, g, f, deg);
    V3 n = k->on_ground ? k->gn : vmul(vnorm(k->grav), -1.0f), u = vadd(vmul(k->qy, 0.9f), vmul(n, 0.1f)); /* vecBlend 0.1 */
    if (vlen(u) > 1e-6f) k->qy = vnorm(u);
    V3 z = vtangent(k->front, k->qy); /* makeMtxUpFront */
    if (vlen(z) > 1e-6f) k->qz = vnorm(z);
}
static void mkm_to_player(Kuribo *k, float a, float g, float f, float deg) { mkm_move(k, vsub(mario->pos, k->pos), a, g, f, deg); }
static void mkm_hold(Kuribo *k) { if (w_on_ground(k)) mkm_to_player(k, 0.0f, 2.0f, 0.8f, 0.0f); else mkm_to_player(k, 0.0f, 2.0f, 0.98f, 0.0f); } /* hNoMoveNoTurn / hOnAir */
static int mkm_near_angle(V3 a, V3 b, float deg) /* isNearAngleDegree */
{
    if (vlen(a) < 1e-6f || vlen(b) < 1e-6f) return 0;
    return vdot(vnorm(a), vnorm(b)) >= cosf(deg * 0.0174533f);
}
static int mkm_find(const Kuribo *k) /* calcDistanceToPlayer < 900 && isFaceToPlayerDegree 120 */
{
    V3 to = vsub(mario->pos, k->pos);
    return vlen(to) < 900.0f && mkm_near_angle(to, k->qz, 120.0f);
}
static int mkm_wall_ahead(const Kuribo *k) { return k->on_wall && vdot(k->qz, k->wn) < -0.99f; } /* isBindedWall(OfMap): its normal against its front */
static void mkm_kill(Kuribo *e, int coin) /* kill (appearCoinPop) */
{
    float m[16];
    basis_matrix(m, e->pos, e->qy, e->qz, 0);
    fx_named(walker_cls(e), "Death", m, NULL, NULL);
    w_se(e, SE_EM_EXPLODE_S);
    if (coin) pop_coins(e->pos, e->qy, 1);
    e->state = KURIBO_DEAD; row_kill(e->row); /* onSwitchDead */
    printf("mechanic koopa mini row %d dead\n", e->row);
}
static int mkm_explosion(Kuribo *e) { if (e->lvt[0]) return 0; mkm_kill(e, 1); return 1; } /* receiveMsgEnemyAttack: isMsgExplosionAttack */
static int mkm_star(Kuribo *e) /* isMsgStarPieceAttack */
{
    if (e->lvt[0] || e->nrv == MKM_HIPDROPPED) return 0;
    mkm_nrv(e, MKM_SPINHIT);
    return 1;
}
static V3 mkm_body(Kuribo *k) { return w_joint(k, walker_new_model(k->row) ? walker_new_model(k->row) : m_mkoopa, 0, v3(48.0f, 13.0f, 0.0f)); } /* JointRoot (48, 13, 0) */
static void mkoopa_update(Kuribo *k, V3 to, float d)
{
    (void)to; (void)d;
    const Row *r = &sc->objs[k->row];
    float sy = r->scale[1];
    u32 rn = 0; int rcl = 0;
    const float *rail = w_rail(k, &rn, &rcl);
    if (!k->front.x && !k->front.y && !k->front.z) { /* init */
        V3 x, z;
        row_axes(r, &x, &z);
        k->grav = vmul(walker_up(k, k->up), -1.0f);
        k->qz = k->front = vnorm(z); k->qy = vnorm(vcross(z, x));
        k->dir = 1;
        if (rail) k->lva[1] = w_rail_nearest(rail, rn, k->pos);
        if (r->sw[SW_APPEAR] >= 0) mkm_nrv(k, r->arg[0] != -1 ? MKM_JUMPSTART : MKM_WAIT); /* onAppearSwitchOn */
        else mkm_nrv(k, MKM_WALKONRAIL);
    }
    k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    float depth;
    if (in_death(k->pos) || in_water(k->pos, vmul(k->grav, -1.0f), &depth)) { mkm_kill(k, 0); return; } /* control */
    int st = ++k->step;
    V3 mp = mario->pos;
    switch (k->nrv) {
    case MKM_WAIT:
        if (st == 0) w_act(k, "Wait");
        mkm_to_player(k, 0.0f, 2.0f, 0.8f, 0.0f);
        if (mkm_find(k)) mkm_nrv(k, MKM_FIND);
        else if (st > 120) mkm_nrv(k, MKM_WALKONRAIL);
        break;
    case MKM_WALKONRAIL: {
        if (st == 0) w_act(k, "Walk");
        int turn = 0, on = w_on_ground(k);
        if (rail) { /* moveAndTurnAlongRail(600): calcMovingDirectionAlongRailH */
            float len = w_rail_len(rail, rn), c = w_rail_nearest(rail, rn, k->pos);
            k->lva[1] = c;
            if (!rcl && (k->dir > 0 ? c >= len - 0.01f : c <= 0.01f)) { k->dir = -k->dir; turn = 1; }
            V3 p = rail_at(rail, rn, c), dv = vmul(rail_tan(rail, (int)rn, c), (float)k->dir);
            V3 mv = vadd(vmul(vtangent(vsub(p, k->pos), vnorm(k->grav)), 1.0f / 600.0f), vtangent(dv, vnorm(k->grav)));
            if (on) mkm_move(k, mv, 0.4f, 2.0f, 0.8f, 2.0f); else mkm_move(k, mv, 0.2f, 2.0f, 0.98f, 2.0f);
        } else if (on) mkm_move(k, k->qz, 0.4f, 2.0f, 0.8f, 2.0f); else mkm_move(k, k->qz, 0.2f, 2.0f, 0.98f, 2.0f);
        if (on) {
            if (mkm_wall_ahead(k)) { mkm_nrv(k, MKM_JUMPSTART); break; }
            if (mkm_find(k)) { mkm_nrv(k, MKM_FIND); break; }
        }
        if (turn) mkm_nrv(k, MKM_TURN);
        else if (r->arg[1] == -1 && st > 120) mkm_nrv(k, MKM_WAIT); /* !mIsForbidPause */
        break;
    }
    case MKM_TURN: {
        if (st == 0) w_act(k, "Turn");
        V3 rd = rail ? vmul(rail_tan(rail, (int)rn, k->lva[1]), (float)k->dir) : k->qz; /* getRailDirection */
        if (w_on_ground(k)) mkm_move(k, rd, 0.0f, 2.0f, 0.8f, 2.0f); else mkm_move(k, rd, 0.0f, 2.0f, 0.98f, 0.0f);
        if (mkm_near_angle(rd, k->qz, 5.0f)) mkm_nrv(k, MKM_WALKONRAIL); /* isFaceToTargetDegree(pos + rd, 5) */
        break;
    }
    case MKM_JUMPSTART: case MKM_JUMPEND:
        if (st == 0) w_act(k, k->nrv == MKM_JUMPSTART ? "JumpStart" : "JumpEnd");
        if (w_on_ground(k)) mkm_to_player(k, 0.0f, 1.2f, 0.8f, 0.0f); else mkm_to_player(k, 0.5f, 2.0f, 0.95f, 0.0f); /* hJumpOnGround / hJumpAir */
        if (k->nrv == MKM_JUMPSTART && st == k->act_frames - 3) { k->vel = vsub(k->vel, vmul(k->grav, 45.0f)); w_se(k, SE_EM_MKOOPAMINI_JUMP); } /* addVelocityJump 45 */
        if (st > 0 && w_bck_stopped(k)) mkm_nrv(k, k->nrv == MKM_JUMPSTART ? MKM_JUMPEND : MKM_WALKONRAIL);
        break;
    case MKM_FIND:
        if (st == 0) { w_act(k, "Find"); k->vel = vsub(k->vel, vmul(k->grav, 26.0f)); w_se(k, SE_EM_MKOOPAMINI_JUMP); }
        if (w_on_ground(k)) mkm_to_player(k, 0.0f, 2.0f, 0.8f, 2.0f); else mkm_to_player(k, 0.0f, 2.0f, 0.98f, 2.0f); /* hTurn / hOnAirTurn */
        if (st > 0 && w_bck_stopped(k)) mkm_nrv(k, MKM_PURSUE);
        break;
    case MKM_PURSUE: {
        if (st == 0) { w_act(k, "Pursue"); k->lvt[2] = 1; k->vel = vmul(k->grav, -10.0f); }
        if (w_on_ground(k)) {
            mkm_to_player(k, 1.3f, 2.0f, 0.8f, 2.0f);
            if (mkm_wall_ahead(k)) mkm_nrv(k, MKM_JUMPSTART);
        } else mkm_to_player(k, 0.2f, 2.0f, 0.98f, 2.0f);
        if (k->nrv != MKM_PURSUE) break;
        float dist = vlen(vsub(mp, k->pos));
        V3 g = vnorm(k->grav);
        if (dist > 1100.0f) mkm_nrv(k, MKM_WALKONRAIL);
        else if (dist < 300.0f && mkm_near_angle(vtangent(vsub(mp, k->pos), g), vtangent(k->qz, g), 5.729578f) && w_on_ground(k))
            mkm_nrv(k, MKM_PREFIRE); /* isFaceToPlayerHorizontalDegree */
        break;
    }
    case MKM_PREFIRE:
        if (st == 0) { w_act(k, "PreFire"); k->lvt[2] = 1; k->vel = vmul(k->grav, -10.0f); }
        if (st <= 60) w_se_lv(k, SE_EM_LV_MKOOPAMINI_PRE_FIRE);
        if (w_on_ground(k)) mkm_to_player(k, 0.0f, 2.0f, 0.8f, 0.0f); else mkm_to_player(k, 0.0f, 2.0f, 0.98f, 0.0f);
        if (st > 0 && w_bck_stopped(k)) mkm_nrv(k, MKM_FIRE);
        break;
    case MKM_FIRE:
        if (st == 0) { k->lvt[1] = 1; w_act(k, "Fire"); } /* the fire sensor on, emitEffect Fire (the draw) */
        if (w_on_ground(k)) mkm_to_player(k, 0.0f, 2.0f, 0.8f, 1.0f); else mkm_to_player(k, 0.0f, 2.0f, 0.98f, 0.0f);
        if (st > 120) mkm_nrv(k, MKM_FIREEND);
        else if (st <= 90) w_se_lv(k, SE_EM_LV_MKOOPAMINI_FIRE);
        break;
    case MKM_FIREEND:
        if (st == 0) w_act(k, "FireEnd");
        mkm_hold(k);
        if (st > 0 && w_bck_stopped(k)) mkm_nrv(k, MKM_WALKONRAIL);
        break;
    case MKM_ATTACKHIT:
        if (st == 0) w_act(k, "AttackHit");
        if (w_on_ground(k)) mkm_to_player(k, 0.0f, 1.2f, 0.8f, 5.0f); else mkm_to_player(k, 0.0f, 2.0f, 0.95f, 5.0f);
        if (w_on_ground(k) && st > 0 && w_bck_stopped(k)) mkm_nrv(k, MKM_WAIT);
        break;
    case MKM_SPINHIT: case MKM_TRAMPLE:
        if (st == 0) {
            w_act(k, k->nrv == MKM_SPINHIT ? "SpinHit" : "Trample");
            if (k->nrv == MKM_SPINHIT) { w_se(k, SE_PM_SPIN_HIT); w_se(k, SE_EM_MKOOPAMINI_SPIN_HIT); } /* startBlowHitSound */
        }
        if (st >= 60) w_se_lv(k, SE_EM_LV_MKOOPAMINI_PRE_FIRE);
        mkm_hold(k);
        if (st > 0 && w_bck_stopped(k)) mkm_nrv(k, MKM_FIRE);
        break;
    case MKM_HIPDROPPED:
        if (st == 0) { w_act(k, "Break"); k->lvt[0] = 1; w_se(k, SE_EM_MKOOPAMINI_BREAK); }
        if (st > 0 && w_bck_stopped(k)) { mkm_kill(k, 1); return; }
        break;
    }
    k->act_t += k->act_rate;
    /* the Binder: 120 at (0, 110, 20) of its base */
    V3 up = vmul(vnorm(k->grav), -1.0f), off = vadd(vmul(k->qy, 110.0f), vmul(k->qz, 20.0f));
    V3 c = vadd(vadd(k->pos, off), k->vel), gn = up, wn = up;
    int wall = 0;
    k->on_ground = collide_n(&c, up, 120.0f, &gn, &wn, &wall);
    k->gn = k->on_ground ? gn : up; k->on_wall = wall ? 2 : 0; k->wn = wn;
    k->pos = vsub(c, off);
    k->up = k->qy;
    if (k->lvt[0] || k->step < 0) return; /* invalidateHitSensors; a nerve just set */
    /* Mario's attacks on its body (receiveMsgPlayerAttack) */
    V3 body = mkm_body(k), mc = mw.c;
    float br = 130.0f * sy;
    int msg = mario_offence(body, br, k->grav);
    if (msg == MSG_TRAMPLE && vdot(vsub(mc, body), up) <= HIT_BODY_R + br - 80.0f) { /* hTrampleUpRangeSub */
        mario_trample(); w_se(k, SE_EM_MKOOPAMINI_TRAMPLE);
        if (k->nrv != MKM_SPINHIT && k->nrv != MKM_TRAMPLE) mkm_nrv(k, MKM_TRAMPLE);
        printf("mechanic koopa mini row %d trampled\n", k->row);
        return;
    }
    if (msg == MSG_HIPDROP) { mkm_nrv(k, MKM_HIPDROPPED); printf("mechanic koopa mini row %d hip dropped\n", k->row); return; }
    if (msg == MSG_HITALL) {
        if (!(k->nrv == MKM_SPINHIT && st < 120)) { mkm_nrv(k, MKM_SPINHIT); printf("mechanic koopa mini row %d spun\n", k->row); }
        return;
    }
    /* attackSensor: the fire sensor on Mario, the body pushing Mario / the other MechanicKoopaMinis */
    if (k->lvt[1]) {
        float fd = fminf(fmaxf(vdot(k->qz, vsub(mc, k->pos)), 0.0f), 400.0f * fminf(fmaxf((st - 10) / 20.0f, 0.0f), 1.0f)); /* calcNerveRate(10, 30) */
        V3 fp = vadd(vadd(k->pos, vmul(k->qy, br)), vmul(k->qz, fd));
        if (vlen(vsub(mc, fp)) < 29.0f * sy + HIT_BODY_R && !mario->hurt_timer) mario_attack(ATK_FIRERUN, fp); /* sendMsgEnemyAttackFire */
    }
    if (mario->hip != HIP_FALL && vlen(vsub(mc, body)) < br + HIT_BODY_R && !(HIT_BODY_R + br - 130.0f < vdot(vsub(mc, body), up)))
        mario_push_out(body, br); /* sendMsgPush (hPushUpRangeSub) */
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (e == k || e->kind != WALK_MKOOPA || e->state == KURIBO_DEAD || e->lvt[0] || !row_shown(e->row)) continue;
        V3 dv = vsub(e->pos, k->pos); /* receiveMsgPush: addVelocityFromPush(3) */
        if (vlen(dv) >= br + 130.0f * sc->objs[e->row].scale[1] || vlen(dv) < 1e-6f) continue;
        if (3.0f < vdot(e->vel, vmul(vnorm(e->grav), -1.0f))) dv = vtangent(dv, vnorm(e->grav));
        if (vlen(dv) < 1e-6f) continue;
        V3 n = vnorm(dv);
        float f1 = vdot(n, e->vel);
        if (f1 < 3.0f) e->vel = vadd(e->vel, vmul(n, 3.0f - f1)); /* addVelocityLimit */
    }
}
#ifdef MKOOPA_TEST
void game_mkm_state(char *out, int n) /* the first live MechanicKoopaMini: its nerve, step */
{
    for (int i = 0; i < nkuribo; i++) {
        const Kuribo *e = &kuribo[i];
        if (e->kind != WALK_MKOOPA || e->state == KURIBO_DEAD) continue;
        snprintf(out, n, "row %d nrv %d step %d dist %d fire %d ground %d", e->row, e->nrv, e->step, (int)vlen(vsub(mario->pos, e->pos)), e->lvt[1], e->on_ground);
        return;
    }
    snprintf(out, n, "none");
}
#endif
