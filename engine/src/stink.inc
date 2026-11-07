/* StinkBugSmall.cpp / StinkBugBase.cpp after Petari (#included by game.c, sb_*; walker kind WALK_STINKBUG). A beetle
 * at its home (_98: the ground under it) facing _8C (_A4 its first front): its head (EnemyAttack 100 x s at Face
 * (0, -30 x s, 0)) and its body (arg7 set: an enemy sensor 110 x s at 10 x s up; else a message sensor and its Switch
 * collision on its base matrix, Mario may stand on it). Wait (arg0 _B0, 180: Search, sweeping 1 degree a frame within
 * _B0 of _A4) -> Mario in its territory (isPlayerInTerritory 400 / 600 / 200 / 200 about arg1 mRadius 1000):
 * DashSign (RushStart, turning 3 to him within _B0) -> DashSignEnd (10) -> Dash (20 a frame along _8C until mRadius
 * from home or a wall) -> DashEnd (RushStop) -> Back (5 to home, Wait within 10). Its horn hurts Mario
 * (EnemyAttackStrong; Attack unless dashing). A spin or a star piece: SpinReaction (GUARD_S). A hip drop (on it or its
 * collision) or Mario invincible: HipDropDown (Death) then kill: three star pieces (or its dead switch), Death,
 * EXPLODE_S. Mario standing on its back: Panic (Loss) for 90, then ShakeStart (Repel, 40) -> Shake (him on it: flipped
 * off); off it in Panic: Recover (60). Its SW_A: ForceFall (2 a frame along gravity).
 * ponytail: no WalkerStateBindStarPointer / DPDSwoon (the 2P pointer), no Death BRK (its colour), the collision's
 * own sensor messages (FloorTouch, HipDropFloor) as Mario's ground instance */
enum { SBS_WAIT, SBS_SEARCH, SBS_DASHSIGN, SBS_DASHSIGNEND, SBS_DASH, SBS_DASHEND, SBS_BACK, SBS_HIPDROPDOWN, SBS_ATTACK, SBS_SPINREACTION,
       SBS_SHAKESTART, SBS_SHAKE, SBS_PANIC, SBS_RECOVER, SBS_FORCEFALL };
#define SB_B0 lva[0]     /* _B0: its turn range (degrees) */
#define SB_RADIUS lva[1] /* mRadius */
static float sb_s(const Kuribo *k) { return sc->objs[k->row].scale[0]; } /* mScale.x */
static Inst *sb_coll(const Kuribo *k) /* its Switch collision (initCollisionParts on the body sensor), NULL with arg7 */
{
    if (k->item) return NULL;
    Inst *in = row_inst(k->row);
    return in && in->invisible && in->kcl ? in : NULL;
}
static int sb_on_player(const Kuribo *k) { Inst *in = sb_coll(k); return in && mario_ground == (int)(in - insts); } /* isOnPlayer(body) */
static float sb_deg(V3 a, V3 b) /* the angle between a and b (degrees) */
{
    float la = vlen(a), lb = vlen(b);
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    float c = vdot(a, b) / (la * lb);
    return acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
}
static void sb_clamp(V3 *v, V3 ref, float deg) /* clampVecAngleDeg: v within deg of ref (turned towards it) */
{
    if (sb_deg(*v, ref) <= deg) return;
    V3 ax = vcross(ref, *v);
    if (vlen(ax) < 1e-6f) return;
    *v = vmul(rot_axis(vnorm(ref), vnorm(ax), deg * 0.0174533f), vlen(*v));
}
static void sb_fix_init_pos(Kuribo *k) { k->vel = v3(0, 0, 0); k->pos = k->tcenter; } /* fixInitPos */
static int sb_territory(const Kuribo *k, float a1, float a2, float a3, float a4) /* isPlayerInTerritory */
{
    if (vlen(vsub(mario->pos, k->pos)) >= k->SB_RADIUS + a1) return 0;
    V3 v1 = vsub(mario->pos, k->pos), side = vcross(k->qy, k->qz);
    if (fabsf(vdot(v1, k->qy)) > a3 || fabsf(vdot(v1, side)) > a2) return 0;
    if (k->SB_B0 == 0.0f) return vdot(v1, k->front) > 0.0f;
    if (k->SB_B0 == 180.0f) return 1;
    float f1 = fabsf(sinf(k->SB_B0 * 0.0174533f));
    V3 p = vadd(k->pos, vmul(k->ttarget, -a4 / f1)), v3_ = vsub(mario->pos, p);
    V3 g = vnorm(k->grav), v5 = vsub(v3_, vmul(g, vdot(v3_, g))); /* killElement(mGravity) */
    return sb_deg(v5, k->ttarget) <= k->SB_B0; /* isNearAngleDegree */
}
static int sb_turn_search(Kuribo *k, float a) /* tryTurnSearch */
{
    k->front = rot_axis(k->front, vnorm(k->grav), (k->dir ? a : -a) * 0.0174533f); /* rotateVecDegree */
    if (sb_deg(k->front, k->ttarget) > k->SB_B0) {
        sb_clamp(&k->front, k->ttarget, k->SB_B0);
        k->dir = !k->dir;
        return 1;
    }
    return 0;
}
static void sb_turn_dash_sign(Kuribo *k, float a) /* tryTurnDashSign */
{
    w_turn_to(k, mario->pos, a);
    sb_clamp(&k->front, k->ttarget, k->SB_B0);
}
static void sb_dash_velocity(Kuribo *k, float v) /* setDashVelocity */
{
    float d = vlen(vsub(vadd(k->tcenter, vmul(k->front, k->SB_RADIUS)), k->pos));
    k->vel = vmul(k->front, d < v ? d : v);
}
static void sb_kill(Kuribo *k) /* kill */
{
    const Row *r = &sc->objs[k->row];
    float m[16];
    if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
    else { drop_pieces(k->pos, k->qy, 3); w_se(k, SE_OJ_STAR_PIECE_BURST); } /* appearStarPiece 3, 10, 40 */
    basis_matrix(m, k->pos, k->qy, k->qz, 0);
    fx_named("StinkBugSmall", "Death", m, NULL, NULL);
    w_se(k, SE_EM_EXPLODE_S);
    Inst *in = sb_coll(k);
    if (in) in->kcl = NULL;
    k->state = KURIBO_DEAD; row_kill(k->row);
    printf("stink bug row %d: dead\n", k->row);
}
static int sb_enable_star(const Kuribo *k) /* isNrvEnableStarPieceAttack */
{
    return k->nrv == SBS_WAIT || k->nrv == SBS_SEARCH || k->nrv == SBS_DASHSIGN || k->nrv == SBS_DASHSIGNEND || k->nrv == SBS_BACK;
}
static int sb_star(Kuribo *k) /* isMsgStarPieceReflect on its body (taken) */
{
    if (k->nrv == SBS_HIPDROPDOWN) return 0;
    if (sb_enable_star(k)) w_nrv(k, SBS_SPINREACTION);
    return 1;
}
static void sb_init(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z;
    row_axes(r, &x, &z);
    k->qy = vnorm(vcross(z, x)); /* calcUpVec */
    k->qz = vnorm(z);
    k->grav = vmul(k->qy, -1.0f);
    k->front = k->ttarget = k->qz; /* _8C, _A4: calcFrontVec */
    k->SB_B0 = r->arg[0] != -1 ? (float)r->arg[0] : 180.0f;
    k->SB_RADIUS = r->arg[1] != -1 ? (float)r->arg[1] : 1000.0f;
    k->item = r->arg[7] != -1; /* _C4 */
    k->dir = 0; k->lvt[1] = 1; k->lvt[2] = 1; /* head / all sensors valid */
    k->tcenter = k->pos; /* initAfterPlacement: the ground 50 over it to 150 under it (its own collision left out) */
    int off = kcl_off_row;
    kcl_off_row = k->row;
    V3 hp, a = vadd(k->pos, vmul(k->qy, 50.0f));
    if (kcl_line(a, vadd(a, vmul(k->qy, -200.0f)), &hp, NULL, NULL)) k->tcenter = hp;
    kcl_off_row = off;
    k->pos = k->tcenter;
    k->bind_off = 1;
    w_nrv(k, SBS_WAIT);
}
static void stink_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    float s = sb_s(k);
    if (!k->front.x && !k->front.y && !k->front.z) sb_init(k);
    int st = k->step;
    switch (k->nrv) {
    case SBS_WAIT:
        if (st == 0 && k->SB_B0 == 0.0f && strcmp(k->act, "Search")) w_act(k, "Search"); /* tryStartBck */
        sb_fix_init_pos(k);
        if (sb_territory(k, 400.0f, 600.0f, 200.0f, 200.0f)) w_nrv(k, SBS_DASHSIGN);
        else if (k->SB_B0 != 0.0f && st > 1) w_nrv(k, SBS_SEARCH);
        break;
    case SBS_SEARCH:
        if (st == 0 && strcmp(k->act, "Search")) w_act(k, "Search");
        sb_fix_init_pos(k);
        if (sb_turn_search(k, 1.0f)) w_nrv(k, SBS_WAIT);
        else if (sb_territory(k, 400.0f, 600.0f, 200.0f, 200.0f)) w_nrv(k, SBS_DASHSIGN);
        break;
    case SBS_DASHSIGN:
        if (st == 0) { w_act(k, "RushStart"); w_se(k, SE_EV_STINKBUG_S_FIND); }
        w_se_lv(k, SE_EM_LV_STINKBUG_S_DASH_SIGN);
        sb_fix_init_pos(k);
        sb_turn_dash_sign(k, 3.0f);
        if (w_bck_stopped(k)) w_nrv(k, SBS_DASHSIGNEND);
        break;
    case SBS_DASHSIGNEND:
        if (st == 10) w_nrv(k, SBS_DASH);
        break;
    case SBS_DASH:
        if (st == 0) { w_act(k, "Rush"); k->lvt[1] = 1; } /* validateHitSensors */
        w_se_lv(k, SE_EM_LV_STINKBUG_S_DASH);
        if (vlen(vsub(k->tcenter, k->pos)) >= k->SB_RADIUS || k->on_wall) { /* !isNear(_98, mRadius) || isBindedWall */
            k->vel = v3(0, 0, 0);
            w_nrv(k, SBS_DASHEND);
            w_se(k, SE_EM_STINKBUG_S_DASH_END);
            printf("stink bug row %d: dash ends%s\n", k->row, k->on_wall ? " at a wall" : "");
            break;
        }
        sb_dash_velocity(k, 20.0f);
        break;
    case SBS_DASHEND:
        if (st == 0) w_act(k, "RushStop");
        k->vel = v3(0, 0, 0);
        if (w_bck_stopped(k)) w_nrv(k, SBS_BACK);
        break;
    case SBS_BACK: {
        if (st == 0) { w_act(k, "Back"); k->lvt[1] = 0; } /* invalidateHitSensor head */
        w_se_lv(k, SE_EM_LV_STINKBUG_S_BACK);
        if (vlen(vsub(k->tcenter, k->pos)) < 10.0f) { w_nrv(k, SBS_WAIT); break; }
        V3 t = vnorm(vsub(k->tcenter, k->pos)), g = vnorm(k->grav);
        t = vtangent(t, g); /* turnVecToPlane */
        k->vel = vmul(vlen(t) > 1e-6f ? vnorm(t) : t, 5.0f);
        break;
    }
    case SBS_HIPDROPDOWN:
        if (st == 0) {
            Inst *in = sb_coll(k);
            w_act(k, "Death");
            if (in) in->kcl = NULL; /* invalidateCollisionParts */
            k->lvt[2] = 0; k->dir = 1; /* invalidateHitSensors, invalidateClipping */
            w_se(k, SE_EM_STOMPED_S); w_se(k, SE_EV_STINKBUG_S_STOMPED);
        }
        k->vel = v3(0, 0, 0);
        if (w_bck_stopped(k)) { sb_kill(k); return; }
        break;
    case SBS_ATTACK:
        if (st == 0) w_act(k, "Attack");
        k->vel = v3(0, 0, 0);
        if (w_bck_stopped(k)) w_nrv(k, SBS_BACK);
        break;
    case SBS_SPINREACTION:
        if (st == 0) { w_act(k, "SpinAction"); w_se(k, SE_EM_GUARD_S); }
        k->vel = v3(0, 0, 0);
        if (w_bck_stopped(k)) w_nrv(k, SBS_BACK);
        break;
    case SBS_SHAKESTART:
        if (st == 0) w_act(k, "Repel");
        if (st == 40) w_nrv(k, SBS_SHAKE);
        break;
    case SBS_SHAKE:
        if (w_bck_stopped(k)) w_nrv(k, SBS_BACK);
        break;
    case SBS_PANIC:
        if (st == 0) { w_act(k, "Loss"); k->lvt[1] = 0; }
        w_se_lv(k, SE_EM_LV_STINKBUG_S_PANIC); /* (and SE_EV_LV_STINKBUG_S_PANIC: one level handle a walker) */
        k->vel = v3(0, 0, 0);
        if (!sb_on_player(k)) { w_nrv(k, SBS_RECOVER); break; }
        if (st == 90) w_nrv(k, SBS_SHAKESTART);
        break;
    case SBS_RECOVER:
        k->vel = v3(0, 0, 0);
        w_se_lv(k, SE_EM_LV_STINKBUG_S_PANIC);
        if (st == 60) w_nrv(k, SBS_BACK);
        break;
    case SBS_FORCEFALL:
        if (st == 0) { k->vel = v3(0, 0, 0); k->grav = vmul(up_at(k->pos, k->qy), -1.0f); k->bind_off = 0; } /* calcGravity, onBind */
        k->vel = vadd(k->vel, vmul(k->grav, 2.0f));
        break;
    }
#ifdef STINK_TEST
    if (st == 0) printf("stink bug row %d: nerve %d %s, Mario on it %d\n", k->row, k->nrv, k->act, sb_on_player(k));
#endif
    /* control */
    if (in_death(k->pos)) { sb_kill(k); return; }
    if (k->nrv != SBS_FORCEFALL && r->sw[SW_A] >= 0 && sw_on(r, SW_A)) w_nrv(k, SBS_FORCEFALL); /* tryForceFall */
    else if (k->nrv != SBS_FORCEFALL) {
        if (k->nrv != SBS_DASH && k->nrv != SBS_BACK) k->bind_off = 1; /* offBind */
        else {
            k->bind_off = 0;
            if (k->on_ground) k->grav = vmul(k->gn, -1.0f);
            V3 g = vnorm(k->grav);
            k->vel = vadd(vsub(k->vel, vmul(g, vdot(k->vel, g))), vmul(k->grav, 2.0f)); /* killElement, + 2 x gravity */
            V3 f = vtangent(k->front, g);
            if (vlen(f) > 1e-6f) k->front = vnorm(f); /* turnVecToPlane */
        }
    }
    k->step++;
    k->act_t += k->act_rate;
    {
        int off = kcl_off_row; /* setBinderExceptActor: its own collision left out */
        kcl_off_row = k->row;
        w_bind(k, 200.0f * s);
        kcl_off_row = off;
    }
    w_blend_quat(k, vmul(k->grav, -1.0f), k->front, 0.3f, 0.3f); /* calcMtxFromGravityAndZAxis, blendMtx 0.3 */
    k->up = k->qy;
    Inst *in = sb_coll(k);
    if (in) { /* its collision on its base matrix (Mario carried) */
        float old[16];
        memcpy(old, in->m, sizeof old);
        basis_matrix(in->m, k->pos, k->qy, k->qz, 0);
        for (int j = 0; j < 12; j++) in->m[j] *= s;
        in->s = s;
        if (mario_ground == (int)(in - insts)) carry(old, in);
    }
    if (!k->lvt[2]) return;
    /* Mario's messages */
    V3 body = k->item ? vadd(k->pos, vmul(k->qy, 10.0f * s)) : k->pos, ms = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    float br = k->item ? 110.0f * s : 0.0f;
    int invinc = player_mode == PM_INVINCIBLE;
    if (in && sb_on_player(k) && (hip_hit || invinc)) { w_nrv(k, SBS_HIPDROPDOWN); return; } /* isMsgPlayerHipDropFloor; FloorTouch invincible */
    int msg = mario_offence(k, body, br, k->grav);
    if (msg == MSG_HIPDROP || (invinc && vlen(vsub(ms, body)) < br + HIT_BODY_R)) { w_nrv(k, SBS_HIPDROPDOWN); return; } /* isMsgPlayerHipDrop, isMsgInvincibleAttack */
    if (msg == MSG_HITALL && k->nrv != SBS_SPINREACTION && k->nrv != SBS_DASH) { m_punch(k, body, 0); w_nrv(k, SBS_SPINREACTION); } /* isMsgPlayerSpinAttack */
    if (in && sb_on_player(k) && k->nrv != SBS_PANIC && k->nrv != SBS_SHAKESTART) { /* isMsgFloorTouch, isOnPlayer */
        if (k->nrv == SBS_SHAKE && mario->hip != HIP_FALL && mario->hip != HIP_LAND) { /* isShakeChance: sendMsgEnemyAttackFlip */
            int ok = mario_attack(ATK_FLIP, k->pos);
#ifdef STINK_TEST
            printf("stink bug row %d: shake flips Mario %d\n", k->row, ok);
#endif
            (void)ok;
        }
        else if (k->nrv != SBS_SHAKE) w_nrv(k, SBS_PANIC);
    }
    /* attackSensor */
    if (k->item && vlen(vsub(ms, body)) < br + HIT_BODY_R) mario_push_out(body, br); /* its body: sendMsgPush */
    if (k->lvt[1] && k->nrv != SBS_ATTACK) { /* its head (EnemyAttack) on Mario */
        V3 head = w_joint(k, m_stink, 6, v3(0, -30.0f * s, 0));
        if (vlen(vsub(ms, head)) < 100.0f * s + HIT_BODY_R) {
            V3 to = vsub(ms, head);
            if (dm_damaging()) mario_push_out(head, 100.0f * s); /* isPlayerDamaging */
            else if (vdot(to, k->front) >= 0.0f && vlen(vmul(k->qy, vdot(k->qy, to))) <= 20.0f * s + HIT_BODY_R && mario_attack(ATK_STRONG, head)) { /* isHitHorn */
                hit_mark_between("StinkBugSmall", head, 100.0f * s, ms, HIT_BODY_R); /* emitEffectHitBetweenSensors */
                if (k->nrv != SBS_DASH) w_nrv(k, SBS_ATTACK);
                printf("stink bug row %d: horn hits Mario\n", k->row);
            }
        }
    }
}
static int sb_body_push(const Kuribo *k) { return k->item && k->nrv != SBS_HIPDROPDOWN; } /* attackSensor: _C4's body (not in HipDropDown) */
