/* Poihana.cpp after Petari (#included by game.c, ph_*; walker kind WALK_POIHANA). An ape at its home: binder
 * 150 x 150 (arg7 set: 100), its "binder" sensor (PriorBinder, 125 at (0, 130, 120); trampled 200 at (0, 150, 0)) and
 * its body (70 at Body, the enemy sensor). Wait (Mario within 800: Search, 180 steps: WalkAround; Mario 3500 off on
 * the ground: NonActive) -> Search (turning 4 to him till its clip ends) -> ChasePlayer (0.5 a frame along its front,
 * 10 at most, turning 4; GoBack 2000 from home or Mario 1100 off). Mario touching the binder (AutoRushBegin): in the
 * air he is bound at once (ShootUp), on the ground within 100 ShootUpCharge (ThrowStart) then ShootUp (Throw): bound
 * in its first steps, thrown at step 2 at sqrt(2 x arg0 (1000)) along -gravity (endBindAndPlayerJump, HELI_JUMP),
 * its actor camera; then GoBack (arg3 2: a new home, WalkAround). A trample or hip drop: on a shocked / sleeping one
 * Bound (40 frames of scale) and AwayJump on a hip drop; else its binder grows to the trample one (he is then thrown).
 * A spin (stopScene 3), an explosion or a hip drop landing within 500: Shock (30 up, PunchDamage) -> SwoonLand ->
 * Swoon (110) -> Recover. From behind (walk / wait / shooting) its body flips Mario (FlipWeak): Shake (20 frames of
 * scale). arg3 1: asleep (Sleep: Mario within 500 at speed 10 or spinning, or touching it: GetUp). arg4 set: water
 * drowns it (Drown, Hide 150, Appear at its first place). Pushes: its body pushes enemies / map objects and cuts its
 * velocity into them, takes 1.5 from them.
 * ponytail: no WalkerStateBindStarPointer / DPDSwoon (the 2P pointer), no WaterColumn model in Drown (its effect
 * only), no mirror actor (tryCreateMirrorActor), no move limit collision, no rebind timer table (MarioActor
 * selectRebindTimer: ActionFlag data); blendMtx 0.3 as a 0.3 blend of its axes */
enum { PH_NONACTIVE, PH_WAIT, PH_WALKAROUND, PH_SLEEPSTART, PH_SLEEP, PH_GETUP, PH_SEARCH, PH_CHASE, PH_CHARGE, PH_SHOOTUP,
       PH_GOBACK, PH_SHOCK, PH_SWOON, PH_SWOONLAND, PH_RECOVER, PH_SHAKE, PH_DROWN, PH_HIDE, PH_APPEAR };
#define PH_SCALE_ mm[0] /* mScale */
static int ph_last = -1;  /* MarioActor _928: the last rush target (its row) */
static int ph_in(const Kuribo *k, int a, int b, int c, int d) { return k->nrv == a || k->nrv == b || k->nrv == c || k->nrv == d; }
static int ph_walk_or_wait(const Kuribo *k) { return ph_in(k, PH_WAIT, PH_SEARCH, PH_WALKAROUND, PH_CHASE) || k->nrv == PH_GOBACK; } /* isNerveTypeWalkOrWait */
static int ph_shocked(const Kuribo *k) { return ph_in(k, PH_SHOCK, PH_SWOONLAND, PH_SWOON, PH_RECOVER); }
static int ph_near_player(const Kuribo *k, float r) { return vlen(vsub(mario->pos, k->pos)) < r; } /* isNearPlayer(AnyTime) */
static int ph_near(const Kuribo *k, V3 p, float r) { return vlen(vsub(p, k->pos)) < r; }
static void ph_binder(Kuribo *k, int how) { k->lvt[2] = how; } /* setSensorOffset / Radius + validateHitSensor (1 normal, 2 trample), 0 invalidate */
static V3 ph_binder_pos(const Kuribo *k) /* its binder sensor's place: (0, 130, 120) or (0, 150, 0) on its base matrix */
{
    return k->lvt[2] == 2 ? vadd(k->pos, vmul(k->qy, 150.0f)) : vadd(k->pos, vadd(vmul(k->qy, 130.0f), vmul(k->qz, 120.0f)));
}
static int ph_sensors_on(const Kuribo *k) { return k->item && k->nrv != PH_DROWN && k->nrv != PH_HIDE && k->nrv != PH_APPEAR; } /* invalidateHitSensors */
static void ph_end_bind(Kuribo *k) /* endBind */
{
    k->dir = 0; /* validateClipping */
    if (k->bshow) { /* isPlayerInRush: endBindAndPlayerJump(-gravity x mLaunchIntensity) */
        const Row *r = &sc->objs[k->row];
        float h = r->arg[0] != -1 ? (float)r->arg[0] : 1000.0f;
        m_power_jump(mario, vmul(k->grav, -sqrtf(2.0f * h)));
        w_se(k, SE_PM_HELI_JUMP);
        printf("poihana row %d: throws Mario up at %d\n", k->row, (int)sqrtf(2.0f * h));
    }
    k->bshow = 0;
}
static void ph_set(Kuribo *k, int n) /* setNerve: the NEW_NERVE_ONEND ends of a nerve that ran (Spine::setNerve) */
{
    if (k->step < 0) { w_nrv(k, n); return; }
    if (k->nrv == PH_NONACTIVE) { /* endNonActive */
        k->bind_off = 0; k->item = 1;
        ph_binder(k, 1);
    }
    if (k->nrv == PH_SHOOTUP) { /* endShootUp */
        ph_end_bind(k);
        ph_binder(k, 1);
        camera_event_end(); /* endActorCamera */
    }
    w_nrv(k, n);
}
static int ph_shock(Kuribo *k) /* tryShock */
{
    if (ph_in(k, PH_DROWN, PH_HIDE, PH_APPEAR, PH_SHOCK) || k->nrv == PH_SWOONLAND) return 0;
    k->vel = vmul(k->grav, -30.0f); /* sShockReactionY */
    ph_set(k, PH_SHOCK);
    printf("poihana row %d: shock\n", k->row);
    return 1;
}
static int poihana_shock(Kuribo *k) { return ph_shock(k); }
static int ph_try_bind(Kuribo *k) /* tryToStartBind */
{
    float depth;
    if (k->bshow || in_water(mario->pos, mario->up, &depth)) return 0;
    pad_rumble(RUMBLE_MIDDLE);
    k->bshow = 1; k->bpos = mario->pos;
    mario_play("rise", 1.0f, 0); /* startBckPlayer Rise */
    k->dir = 1; /* invalidateClipping */
    ph_last = k->row;
    printf("poihana row %d: catches Mario\n", k->row);
    return 1;
}
static int ph_rush(Kuribo *k, V3 ms) /* receiveOtherMsg isMsgAutoRushBegin (Mario's body sensor at ms) */
{
    if (k->nrv == PH_CHARGE) return 0;
    if (!mario->grounded) {
        if (!ph_try_bind(k)) return 0;
        ph_set(k, PH_SHOOTUP);
        return 1;
    }
    if (k->nrv == PH_SHOOTUP) return k->step + 1 < 30 && ph_try_bind(k); /* sBindOnTiming (getNerveStep: after Spine's step++) */
    if (vlen(vsub(ms, ph_binder_pos(k))) < 100.0f) ph_set(k, PH_CHARGE); /* sChargeStartDistance */
    return 0;
}
static void ph_start_bound(Kuribo *k) { k->lvt[0] = 0; k->PH_SCALE_ = v3(1.0f, 1.0f, 1.0f); } /* startBound */
static void ph_calc_bound(Kuribo *k) /* calcBound */
{
    if (k->lvt[0] == -1) return;
    if (k->lvt[0] >= 40) { k->PH_SCALE_ = v3(1.0f, 1.0f, 1.0f); k->lvt[0] = -1; return; } /* sBoundTime */
    k->lvt[0]++;
    float t = (float)k->lvt[0], a = 0.4f - t * 0.01f, d = 0.0174533f; /* sBoundAmpMax */
    k->PH_SCALE_ = v3(a * 0.9f * sinf(t * 9.0f * d) + 1.0f, a * sinf(t * 27.0f * d) + 1.0f, a * 0.9f * sinf(t * 18.0f * d) + 1.0f);
}
static void ph_control_velocity(Kuribo *k) /* controlVelocity */
{
    if (k->nrv == PH_NONACTIVE) return;
    V3 g = k->on_ground ? vmul(k->gn, -1.0f) : k->grav;
    V3 f = vtangent(k->front, vnorm(g)); /* turnVecToPlane */
    if (vlen(f) > 1e-6f) k->front = vnorm(f);
    if (k->on_ground) {
        V3 side = vmul(vsub(k->vel, vmul(k->front, vdot(k->front, k->vel))), 0.8f); /* sVelocityDownRateSide */
        k->vel = vadd(vmul(k->front, vdot(k->front, k->vel)), side);
        if (vdot(k->vel, g) > 0.0f) k->vel = vsub(k->vel, vmul(g, vdot(g, k->vel)));
        k->vel = vmul(k->vel, 0.95f); /* sVelocityDownRate */
    }
    k->vel = vadd(k->vel, vmul(g, 2.0f)); /* sGravity */
    if (k->nrv != PH_SHOCK) {
        float mx = k->nrv == PH_CHASE ? 10.0f : 5.0f; /* sChaseSpeedMax, sWalkSpeedMax */
        if (vlen(k->vel) > mx) k->vel = vmul(vnorm(k->vel), mx);
        if (vlen(k->vel) < 0.001f) k->vel = v3(0, 0, 0); /* isNearZero */
    }
}
static int ph_try_non_active(Kuribo *k) /* tryNonActive */
{
    if (!ph_near_player(k, 3500.0f) && k->on_ground) { ph_set(k, PH_NONACTIVE); return 1; } /* sActiveRadius */
    return 0;
}
static int ph_need_back_home(const Kuribo *k) /* isNeedForBackHome */
{
    if (k->nrv == PH_WALKAROUND) return !ph_near(k, k->tcenter, 350.0f); /* sWalkAroundRadius */
    if (k->nrv == PH_CHASE) {
        if (sc->objs[k->row].arg[3] == 2) return !ph_near_player(k, 1100.0f);
        return !ph_near(k, k->tcenter, 2000.0f) || !ph_near_player(k, 1100.0f); /* sDistanceForNeedBack, sChaseRadius */
    }
    return 0;
}
static int ph_need_get_up(const Kuribo *k) /* isNeedForGetUp */
{
    if (!ph_near_player(k, 500.0f)) return 0; /* sGetUpRadius */
    return vlen(vadd(mario->hvel, vmul(mario->up, mario->vup))) >= 10.0f || mario->spin_timer > 0; /* sJudgeForGetUpSpeed */
}
static void ph_init(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z;
    row_axes(r, &x, &z);
    k->grav = vmul(up_at(k->pos, k->up), -1.0f);
    k->qy = vmul(k->grav, -1.0f); /* initAfterPlacement: makeMtxUpNoSupportPos(-gravity), calcFrontVec */
    k->qz = vlen(vtangent(z, k->qy)) > 1e-3f ? vnorm(vtangent(z, k->qy)) : vnorm(vtangent(x, k->qy));
    k->front = k->qz;
    k->ttarget = k->tcenter = k->pos; /* mRespawnPos, mHomePos */
    k->PH_SCALE_ = v3(r->scale[0], r->scale[1], r->scale[2]);
    k->lvt[0] = -1; k->bshow = 0; k->dir = 0; k->phase = 0; k->jc = 0;
    k->item = 1; /* mIsActive */
    ph_binder(k, 1);
    w_nrv(k, r->arg[3] == 1 ? PH_SLEEP : PH_WAIT);
}
static void poihana_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    if (!k->front.x && !k->front.y && !k->front.z) ph_init(k);
    int st = ++k->step; /* Spine::update: a nerve set since runs its step 0 */
    switch (k->nrv) {
    case PH_NONACTIVE:
        if (st == 0) { k->vel = v3(0, 0, 0); k->bind_off = 1; k->item = 0; ph_binder(k, 0); } /* offBind, offCalcShadow / Anim, invalidateHitSensors */
        if (ph_near_player(k, 3500.0f)) ph_set(k, r->arg[3] == 1 ? PH_SLEEP : PH_WAIT);
        break;
    case PH_WAIT:
        if (st == 0) { w_act(k, "Wait"); ph_binder(k, 1); }
        if (ph_near_player(k, 800.0f)) ph_set(k, PH_SEARCH); /* sSearchRadius */
        else if (st > 180) ph_set(k, PH_WALKAROUND); /* sWalkAroundBreakTime */
        else ph_try_non_active(k);
        break;
    case PH_WALKAROUND:
        if (st == 0) {
            if (strcmp(k->act, "Walk") || w_bck_stopped(k)) w_act(k, "Walk");
            k->lvt[1] = (int)frand(-2.0f, 2.0f); /* getRandom(-sWalkAroundTurn, sWalkAroundTurn) */
        }
        k->front = rot_axis(k->front, vnorm(k->grav), (float)k->lvt[1] * 0.0174533f); /* rotateVecDegree */
        k->vel = vadd(k->vel, vmul(k->front, 0.5f)); /* sWalkAccel */
        if (ph_need_back_home(k)) ph_set(k, PH_GOBACK);
        else if (ph_near_player(k, 800.0f)) ph_set(k, PH_SEARCH);
        else if (st > 120) ph_set(k, r->arg[3] == 1 ? PH_SLEEPSTART : PH_WAIT); /* sWalkAroundKeepTime */
        break;
    case PH_SLEEPSTART:
        if (st == 0) { w_act(k, "SleepStart"); w_se(k, SE_EV_POIHANA_SLEEP_START); ph_binder(k, 0); }
        if (w_bck_stopped(k)) ph_set(k, PH_SLEEP);
        break;
    case PH_SLEEP:
        if (st == 0) { w_act(k, "Sleep"); ph_binder(k, 0); }
        if (ph_need_get_up(k)) ph_set(k, PH_GETUP);
        else ph_try_non_active(k);
        break;
    case PH_GETUP:
        if (st == 0) { w_act(k, "GetUp"); w_se(k, SE_EV_POIHANA_WAKEUP); w_se(k, SE_EM_POIHANA_WAKEUP); }
        if (w_bck_stopped(k)) { ph_binder(k, 1); ph_set(k, PH_WAIT); }
        break;
    case PH_SEARCH:
        if (st == 0) { w_act(k, "Search"); w_se(k, SE_EV_POIHANA_FIND); }
        w_turn_to(k, mario->pos, 4.0f); /* sChaseTurnRate */
        if (w_bck_stopped(k)) ph_set(k, PH_CHASE);
        break;
    case PH_CHASE:
        if (st == 0) { w_act(k, "Run"); ph_binder(k, 1); }
        w_turn_to(k, mario->pos, 4.0f);
        k->vel = vadd(k->vel, vmul(k->front, 0.5f)); /* sChaseAccel */
        if (ph_need_back_home(k)) ph_set(k, PH_GOBACK);
        break;
    case PH_CHARGE:
        if (st == 0) w_act(k, "ThrowStart");
        k->vel = vmul(k->grav, vdot(k->grav, k->vel));
        if (w_bck_stopped(k)) ph_set(k, PH_SHOOTUP);
        break;
    case PH_SHOOTUP:
        if (st == 0) {
            w_act(k, "Throw"); w_se(k, SE_EV_POIHANA_SHOOT_UP);
            if (row_camset[k->row] != -1) camera_actor_start(walker_cls(k), "", (unsigned short)row_camset[k->row], k->qy, k->qz, k->pos); /* startActorCameraNoTarget */
        }
        k->vel = vmul(k->grav, vdot(k->grav, k->vel));
        if (st == 2) { ph_end_bind(k); ph_binder(k, 0); } /* sBindOffTiming */
        if (st == 30) ph_binder(k, 1); /* sBindOnTiming */
        if (w_bck_stopped(k)) {
            if (r->arg[3] == 2) { k->tcenter = k->pos; ph_set(k, PH_WALKAROUND); }
            else ph_set(k, PH_GOBACK);
        }
        break;
    case PH_GOBACK:
        if (st == 0 && (strcmp(k->act, "Walk") || w_bck_stopped(k))) w_act(k, "Walk");
        w_turn_to(k, k->tcenter, 2.0f); /* sWalkTurnRate */
        k->vel = vadd(k->vel, vmul(k->front, 0.5f));
        if (ph_near_player(k, 800.0f) && st > 120) ph_set(k, PH_SEARCH); /* sStepForNonSearch */
        else if (ph_near(k, k->tcenter, 100.0f)) ph_set(k, PH_WAIT); /* sJudgeForGoBackRadius */
        break;
    case PH_SHOCK:
        if (st == 0) { w_act(k, "PunchDamage"); w_se(k, SE_PM_SPIN_HIT); ph_binder(k, 0); } /* startBlowHitSound */
        if (w_on_ground(k) && st > 12) ph_set(k, PH_SWOONLAND); /* sStepToShockLand */
        break;
    case PH_SWOONLAND:
        if (st == 0) { w_act(k, "SwoonLand"); w_se(k, SE_EV_POIHANA_SWOON); }
        if (w_bck_stopped(k)) ph_set(k, PH_SWOON);
        break;
    case PH_SWOON:
        if (st == 0) w_act(k, "Swoon");
        w_se_lv(k, SE_EM_LV_SWOON_S);
        if (st == 110) ph_set(k, PH_RECOVER); /* sStepForSwoon */
        break;
    case PH_RECOVER:
        if (st == 0) { w_act(k, "Recover"); w_se(k, SE_EM_POIHANA_RECOVER); w_se(k, SE_EV_POIHANA_RECOVER); k->PH_SCALE_ = v3(1.0f, 1.0f, 1.0f); }
        if (w_bck_stopped(k)) { ph_binder(k, 1); ph_set(k, PH_WAIT); }
        break;
    case PH_SHAKE: {
        float a = 0.2f - 0.01f * (float)st, s = sinf((float)st * 36.0f * 0.0174533f) * a + 1.0f; /* sShakeAmpMax */
        k->PH_SCALE_ = v3(s, s, s);
        if (st == 20) { k->PH_SCALE_ = v3(1.0f, 1.0f, 1.0f); ph_set(k, PH_SEARCH); } /* sStepForShake */
        break;
    }
    case PH_DROWN:
        if (st == 0) { w_act(k, "Drown"); k->dir = 1; w_se(k, SE_EM_FALL_INTO_WATER_S); } /* invalidateClipping / HitSensors */
        if (w_bck_stopped(k)) ph_set(k, PH_HIDE);
        break;
    case PH_HIDE:
        if (st == 0) {
            float m[16];
            w_se(k, SE_EM_EXPLODE_S_WATER);
            basis_matrix(m, k->pos, k->qy, k->qz, 0);
            fx_named("Poihana", "DeathWater", m, NULL, NULL);
            k->phase = 1; /* hideModel */
        }
        if (st == 150) ph_set(k, PH_APPEAR); /* sStepForHide */
        break;
    case PH_APPEAR:
        if (st == 0) {
            float m[16];
            k->pos = k->ttarget; k->vel = v3(0, 0, 0); /* resetPosition */
            basis_matrix(m, k->pos, k->qy, k->qz, 0);
            fx_named("Poihana", "Appear", m, NULL, NULL);
        }
        if (st < 60) w_se_lv(k, SE_EM_LV_POIHANA_REVIVE_EFFECT); /* sStepToAppearEffectEnd */
        if (st == 60) { k->phase = 0; w_act(k, "Appear"); w_se(k, SE_EM_POIHANA_REVIVE_APPEAR); }
        else if (w_bck_stopped(k) && st > 60) { k->dir = 0; ph_binder(k, 1); ph_set(k, PH_WAIT); }
        break;
    }
#ifdef POIHANA_TEST
    if (st == 0) printf("poihana row %d: nerve %d %s at %d %d %d, clipped %d\n", k->row, k->nrv, k->act, (int)k->pos.x, (int)k->pos.y, (int)k->pos.z, k->clipped);
#endif
    /* control */
    if (k->nrv != PH_HIDE) {
        ph_calc_bound(k);
        ph_control_velocity(k);
        if (k->item) { /* calcMyGravity: at 20 up */
            V3 up = vmul(k->grav, -1.0f);
            k->grav = vmul(up_at(vadd(k->pos, vmul(up, 20.0f)), up), -1.0f);
        }
        float depth;
        int drown = r->arg[4] != -1 && k->nrv != PH_APPEAR && k->nrv != PH_DROWN && in_water(k->pos, vmul(k->grav, -1.0f), &depth); /* tryDrown */
        if (drown) ph_set(k, PH_DROWN);
        else if (ph_near_player(k, 500.0f) && mario->hip == HIP_LAND) ph_shock(k); /* tryHipDropShock: sDistanceForHipDropShock */
    }
    k->act_t += k->act_rate;
    w_bind(k, r->arg[7] != -1 ? 100.0f : 150.0f);
    { /* calcAndSetBaseMtx: calcMtxFromGravityAndZAxis, blended 0.3 while walking / waiting */
        V3 up = vmul(k->grav, -1.0f), f = vtangent(k->front, up);
        if (ph_walk_or_wait(k)) w_blend_quat(k, up, f, 0.3f, 0.3f);
        else if (vlen(f) > 1e-6f) { k->qy = up; k->qz = vnorm(f); }
    }
    k->up = k->qy;
    if (k->bshow) { mario->pos = k->bpos; mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* ACTMES_UPDATE_BASEMTX: his own matrix kept */
    if (!ph_sensors_on(k)) return;
    /* Mario's messages to its body (70 at Body) */
    V3 body = w_joint(k, m_poihana, 1, v3(0, 0, 0)), ms = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    int msg = k->bshow ? MSG_NONE : mario_offence(k, body, 70.0f, k->grav);
    if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) { /* isMsgPlayerTrample / HipDrop */
        if (ph_shocked(k) || ph_in(k, PH_SLEEPSTART, PH_SLEEP, PH_GETUP, -1)) {
            ph_start_bound(k);
            w_se(k, SE_EV_POIHANA_TRAMPLE);
            if (msg == MSG_HIPDROP) { V3 a = vtangent(vsub(mario->pos, k->pos), k->grav); mario->hip = 0; m_power_jump(mario, vadd(vmul(vlen(a) > 1e-6f ? vnorm(a) : k->qz, 10.0f), vmul(mario->up, 25.0f))); } /* sendMsgAwayJump */
            else mario_trample();
            return;
        }
        ph_binder(k, 2); /* sTrampleBinderPos / Radius: not taken, he lands in its binder */
    } else if (msg == MSG_HITALL && ph_shock(k)) { m_punch(k, body, 0); scene_stop = 3; return; } /* stopSceneForDefaultHit(3) */
    /* Mario's body sensor on its binder: AutoRushBegin (not rising again into the one that threw him: _928) */
    if (k->lvt[2] && !k->bshow && vlen(vsub(ms, ph_binder_pos(k))) < (k->lvt[2] == 2 ? 200.0f : 125.0f) + HIT_BODY_R &&
        !(!mario->grounded && mario->vup > 0.0f && ph_last == k->row))
        ph_rush(k, ms);
    /* attackSensor: its body on Mario */
    if (vlen(vsub(ms, body)) >= 70.0f + HIT_BODY_R) return;
    if (ph_walk_or_wait(k) || k->nrv == PH_CHARGE || k->nrv == PH_SHOOTUP) { /* contactMario */
        if (vdot(vsub(ms, k->pos), vmul(k->qz, -1.0f)) > 0.0f && /* isBackAttack (his sensor behind its front) */
            mario_attack(ATK_FLIP_WEAK, body))
            ph_set(k, PH_SHAKE);
    } else if (k->nrv == PH_SLEEP) ph_set(k, PH_GETUP);
    mario_push_out(body, 70.0f); /* sendMsgPush */
    if (ph_shocked(k)) return;
    V3 d = vsub(k->pos, mario->pos);
    if (vlen(d) > 1e-6f) { d = vnorm(d); float f = vdot(k->vel, d); if (f < 0.0f) k->vel = vsub(k->vel, vmul(d, f)); }
}
