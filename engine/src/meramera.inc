/* Meramera.cpp after Petari (Meramera, IceMeramera, ChildIceMeramera: IceMerameraKing's children; #included by game.c,
 * mm_*; walker kind WALK_MERAMERA). A flame floating over its shadow (binder 45 x scale about its place, body 55, break
 * 10, attack 70 at JointRoot + 50 along its X), its quaternion _128 (qy / qz) blended 0.25 / 0.25 to -gravity / _138
 * (front) or rolled as a ball (radius 50) while Runaway / StartDiving / Diving, its flame (_144, mm[0]) turned 1 degree
 * a frame to -gravity, toward Mario's spin pull (tryPullTrans) by a spin. Wait (130 over its shadow; after 120 Walk to
 * 200 about its home until 300 or 100 off) -> Mario within 900 (mChaseDistance), its home within 3000: ChaseStart (60)
 * -> ChaseDash (45, 0.175 along _15C, up a wall under 250) <-> ChaseTurn (50), back to Wait past 240, Mario 900 off or
 * 3000 from home. Its attack sensor burns / freezes Mario (sendMsgEnemyAttackFire / Freeze): AttackSuccess (180).
 * Mario's spin in its SpinStormRange: within 300 Damage (blown 35, its fire out at 3: ExtinguishSide / Smoke) -> 30
 * Runaway (rolling, jumping away from Mario 45-50 every 30-40 frames once slow; a fire floor 250 ahead (Meramera):
 * StartDiving (35) -> Diving (a 300 parabola, 40) -> Sink in it (Fall, hidden 50) -> Float (30) -> Ignition (20) ->
 * Chase / Wait); far from Mario and both places out of view: back home (resetAppear). Runaway / diving: a touch, a
 * spin (Down: one frame, stopScene 2, its explosion), a trample or hip drop (FlatDown) kills it (Break, one coin, its
 * dead switch; arg2: ReadyRestart, back after 180). A star piece: Runaway; a fireball: IgnitionForce (fire) / Runaway
 * or Down (ice); an explosion or Mario invincible: Down. arg0: 0 starts it in Runaway, 1 sunk (Float).
 * ponytail: no BindStarPointer (WalkerStateBindStarPointer, AnimScaleController: the 2P pointer), no BRK colours (one
 * baked colour a model; the BTPs OnFire / RedToBlack / OffFire are played), its shadow's projection the 地面用 sphere's
 * (1000 down, 50 over it), no element attack on other enemies, isMsgStartPowerStarGet left out (the scene ends);
 * tryPullTrans with Mario's feet as his ground point */
enum { MM_WAIT, MM_WALK, MM_CHASESTART, MM_CHASEDASH, MM_CHASETURN, MM_DAMAGE, MM_SHRINK, MM_RUNAWAY, MM_STARTDIVING,
       MM_DIVING, MM_SINK, MM_FLOAT, MM_IGNITION, MM_IGNITIONFORCE, MM_ATTACKSUCCESS, MM_DOWN, MM_FLATDOWN, MM_READYRESTART };
enum { MME_NULL, MME_WAIT, MME_CHASE, MME_ESCAPE };              /* mEffectType (lvt[0]) */
enum { MMB_HEAT = 1, MMB_COOLDOWN, MMB_COLD };                   /* mBodyEffectType (lvt[1]) */
#define MMB(n) (1 << MM_##n)
#define MM_FIRE_ 0  /* mm[0] _144: its flame's up */
#define MM_WALK_ 1  /* mm[1] _174: Walk's target */
#define MM_DIVE_ 2  /* mm[2] _180: the dive point */
#define MM_DUP_ 3   /* mm[3] _198: the dive point's up */
#define MM_DDIR_ 4  /* mm[4] _18C: toward the dive point */
#define MM_EXT_ 5   /* mm[5] mExtinguishMtx's place, mm[6] its up */
#define MM_FIX_ 7   /* mm[7] the binder's fix reaction */
static int mm_in(const Kuribo *k, int mask) { return (mask >> k->nrv) & 1; }
static int mm_ice(const Kuribo *k) { return k->item; } /* mElementType: 0 fire, 1 ice */
static const char *mm_arc(const Kuribo *k) { return mm_ice(k) ? "IceMeramera" : "Meramera"; } /* sElementArcName[mElementType] */
static const Model *mm_model(const Kuribo *k) { return mm_ice(k) ? m_imeramera : m_meramera; }
static int mm_disabled(const Kuribo *k) { return mm_in(k, MMB(RUNAWAY) | MMB(STARTDIVING) | MMB(DIVING)); } /* isActionDisabled */
static int mm_affected_spin(const Kuribo *k) /* isAffectedSpin */
{
    return mm_in(k, MMB(WAIT) | MMB(ATTACKSUCCESS) | MMB(WALK) | MMB(SHRINK) | MMB(CHASESTART) | MMB(CHASEDASH) | MMB(CHASETURN) |
                    MMB(IGNITION) | MMB(IGNITIONFORCE));
}
static int mm_enable_attack(const Kuribo *k) { return mm_in(k, MMB(WAIT) | MMB(WALK) | MMB(CHASEDASH) | MMB(CHASETURN)); }
static int mm_push_moved(const Kuribo *k) { return mm_in(k, MMB(WAIT) | MMB(WALK) | MMB(DAMAGE) | MMB(CHASEDASH) | MMB(CHASETURN) | MMB(ATTACKSUCCESS)); }
static int mm_enable_fireball(const Kuribo *k) { return !mm_in(k, MMB(FLOAT) | MMB(IGNITION) | MMB(IGNITIONFORCE) | MMB(DOWN) | MMB(FLATDOWN) | MMB(READYRESTART)); }
static int mm_near_player(V3 p, float d) { return vlen(vsub(mario->pos, p)) < d; } /* isNearPlayer */
static int mm_enable_chase(const Kuribo *k) { return mm_near_player(k->pos, 900.0f) && vlen(vsub(k->tcenter, k->pos)) <= 3000.0f; }
static void mm_grav_add(Kuribo *k, float v) { k->vel = vadd(k->vel, vmul(k->grav, v)); } /* addVelocityToGravity */
static float mm_rnd(void) { return frand(-1.0f, 1.0f); }
/* turnVecToVecDegree: v turned at most deg toward `to` (about `axis` when opposite) */
static void mm_turn_deg(V3 *v, V3 to, float deg, V3 axis)
{
    V3 a = vnorm(*v), b = vnorm(to), c = vcross(a, b);
    float d = vdot(a, b), ang = acosf(d > 1 ? 1 : d < -1 ? -1 : d), lim = deg * 0.0174533f;
    if (ang < 1e-6f) return;
    V3 ax = vlen(c) > 1e-6f ? vnorm(c) : vnorm(vtangent(axis, a));
    if (vlen(ax) < 1e-6f) return;
    *v = vnorm(rot_axis(a, ax, ang < lim ? ang : lim));
}
/* addVelocityKeepHeightUseShadow(h, a_over, a_under, range): getShadowNearProjectionLength (地面用: 50 over it, 1000 down) */
static float mm_shadow_len(const Kuribo *k)
{
    V3 hp;
    if (!kcl_line(vsub(k->pos, vmul(k->grav, 50.0f)), vadd(k->pos, vmul(k->grav, 1000.0f)), &hp, NULL, NULL)) return -1.0f;
    return vdot(k->grav, vsub(hp, k->pos));
}
static void mm_keep_height(Kuribo *k, float h, float a, float b, float range)
{
    float l = mm_shadow_len(k);
    if (l < 0.0f) return; /* isShadowProjectedAny */
    float f = l - h, n = f >= 0.0f ? mk_norm(f, -range, range) : -mk_norm(-f, -range, range); /* normalizeAbs */
    mm_grav_add(k, (n >= 0.0f ? a : b) * n);
}
/* reboundVelocityFromEachCollision(floor, wall, ceiling, min) on the binder's fix reaction */
static int mm_rebound_each(Kuribo *k, float fl, float wa, float ce, float mn)
{
    V3 r = k->mm[MM_FIX_];
    if (vlen(r) < 1e-4f) return 0;
    r = vnorm(r);
    float g = vdot(r, k->grav), rate = fabsf(g) < 0.34202015f ? wa : g < 0.0f ? fl : ce, v = vdot(r, k->vel);
    if (v < -mn) { k->vel = vsub(k->vel, vmul(r, v * (1.0f + rate))); return 1; }
    if (v < 0.0f) k->vel = vsub(k->vel, vmul(r, v));
    return 0;
}
static void mm_rebound(Kuribo *k) /* reboundVelocityFromCollision(0, 0, 1) */
{
    V3 r = k->mm[MM_FIX_];
    if (vlen(r) < 1e-4f) return;
    r = vnorm(r);
    float v = vdot(r, k->vel);
    if (v < 0.0f) k->vel = vsub(k->vel, vmul(r, v));
}
/* addMovingAccel: _138 turned (cos g) to rv off the gravity, then rv's length in _1A4 and a x _138 added */
static void mm_moving_accel(Kuribo *k, V3 rv, float a, float g)
{
    V3 h = vtangent(rv, vnorm(k->grav));
    k->from = vlen(h); /* separateScalarAndDirection(&_1A4) */
    if (k->from > 1e-6f) {
        if (-1.0f < g && g < 1.0f) turn_on_plane(&k->front, h, k->grav, g); /* turnVecToVecCos */
        else k->front = vnorm(h);
    }
    k->vel = vadd(k->vel, vmul(k->front, a));
}
static void mm_target_accel(Kuribo *k, V3 t, float a, float g) { mm_moving_accel(k, vsub(t, k->pos), a, g); }
static V3 mm_sin(Kuribo *k) { return w_joint(k, mm_model(k), 4, v3(0, 0, 0)); } /* its Sin joint */
/* MarioActor::tryPullTrans (calcPlayerSpinPullVelocity) at p */
static V3 mm_pull(V3 p)
{
    V3 c = mw.c, g = swimming ? vsub(c, vmul(mario->up, 600.0f)) : mario->pos, seg = vsub(g, c);
    float l2 = vdot(seg, seg);
    if (l2 < 1e-6f) return v3(0, 0, 0);
    float proj = -vdot(seg, vsub(c, p)) / l2; /* getFootPoint */
    V3 foot = vadd(c, vmul(seg, proj)), in = vsub(foot, p);
    float rad = fminf(vlen(in), 600.0f), sy = 400000.0f / (10000.0f + rad * rad), h = fabsf(proj * sqrtf(l2));
    if (h < sy) sy = h;
    if (proj < 0.0f) sy = -sy;
    if (!swimming) sy *= 0.4f;
    V3 ax = vlen(seg) > 1e-6f ? vnorm(seg) : v3(0, 0, 0);
    in = vlen(in) > 1e-6f ? vnorm(in) : v3(0, 0, 0);
    V3 tan = vcross(ax, in);
    float sz = (0.01f + 0.04f * (600.0f - rad) / 600.0f) * (2.0f * 3.14159265f * rad) * MC.mCoinPullAngleSpeedRatio;
    V3 pull = vsub(foot, vadd(p, vmul(tan, sz)));
    float nr = vlen(pull);
    pull = nr > 1e-6f ? vmul(pull, 1.0f / nr) : v3(0, 0, 0);
    float ps = rad / 25.0f * MC.mCoinPullDistSpeedRatio; /* (_934: a Bee / Boo flying, not here) */
    if (rad < ps) ps = rad;
    return vadd(vsub(vmul(pull, ps + (nr - rad)), vmul(ax, sy)), vmul(tan, sz));
}
static void mm_turn_fire_to_spin(Kuribo *k, float deg) /* turnFireDirectionToSpin */
{
    V3 pv = mm_pull(mm_sin(k));
    if (vlen(pv) < 1e-6f) return;
    mm_turn_deg(&k->mm[MM_FIRE_], vnorm(pv), deg, k->front);
}
static void mm_fx(Kuribo *k, const char *name, V3 p, V3 up, const void *key)
{
    float m[16];
    basis_matrix(m, p, up, k->qz, 0);
    for (int j = 0; j < 12; j++) m[j] *= w_scale(k);
    fx_named(mm_arc(k), name, m, NULL, key);
}
static void mm_fx_body(Kuribo *k, const char *name) { mm_fx(k, name, k->pos, k->qy, NULL); } /* MR::emitEffect (one-time) */
static void mm_head(Kuribo *k, int t) { if (k->lvt[0] != t) k->lvt[0] = t; } /* emitEffectHead (kept each frame: mm_effects) */
static void mm_head_off(Kuribo *k) { k->lvt[0] = MME_NULL; } /* deleteEffectHead (forceDelete: the same here) */
static void mm_body(Kuribo *k, int t) /* emitEffectHeatBody / CoolDownBody / ColdBody: startBtp (startBrk: ponytail) */
{
    if (k->lvt[1] == t) return;
    k->lvt[1] = t; k->lva[0] = 0.0f;
}
static void mm_effects(Kuribo *k) /* the kept effects on _98 (up _144 at Sin) / mExtinguishMtx */
{
    static const char *head[] = {NULL, "Wait", "Chase", "Escape"};
    if (k->phase) return;
    if (k->lvt[0]) mm_fx(k, head[k->lvt[0]], mm_sin(k), k->mm[MM_FIRE_], &k->lvt[0]);
    if (k->nrv == MM_SINK && mm_ice(k)) mm_fx(k, "Revival", k->pos, vmul(k->grav, -1.0f), &k->lvt[1]); /* deleted at Float */
}
static void mm_reset_appear(Kuribo *k);
static void mm_kill(Kuribo *k) /* kill */
{
    const Row *r = &sc->objs[k->row];
    mm_fx_body(k, "Break");
    pop_coins(k->pos, vmul(k->grav, -1.0f), 1); /* appearCoinPop */
    if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
    mm_head_off(k);
    if (r->arg[2] != -1 && r->arg[2] != 0) { k->lvt[2] = 1; k->phase = 1; w_nrv(k, MM_READYRESTART); printf("meramera row %d: ready restart\n", k->row); return; } /* mIsValidRestart */
    k->state = KURIBO_DEAD; row_kill(k->row);
    printf("meramera row %d killed\n", k->row);
}
static void mm_hide_sunk(Kuribo *k) /* Sink / resetAppear's Float: hideModel, offBind, its cold body */
{
    k->phase = 1; k->bind_off = 1;
    mm_head_off(k); mm_body(k, MMB_COLD);
}
static void mm_reset_appear(Kuribo *k) /* resetAppear */
{
    const Row *r = &sc->objs[k->row];
    k->pos = k->tcenter; k->vel = v3(0, 0, 0);
    k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f);
    switch (r->arg[0]) { /* mAppearStatus */
    case 0: w_nrv(k, MM_RUNAWAY); break;
    case 1:
        w_act(k, "Wait"); mm_hide_sunk(k);
        k->mm[MM_DUP_] = vmul(k->grav, -1.0f); k->mm[MM_DIVE_] = k->pos;
        w_nrv(k, MM_FLOAT);
        break;
    default: w_nrv(k, MM_WAIT); break;
    }
}
static int mm_hit_recover(Kuribo *k) /* isHitRecover: a fire floor (fire), water (ice) */
{
    float d;
    int code = k->on_ground ? k->jc : -1;
    if (!mm_ice(k)) return code == 10;                                           /* isBindedGroundDamageFire */
    return (code >= 20 && code <= 23) || in_water(k->pos, vmul(k->grav, -1.0f), &d); /* isBindedGroundWater, isInWater */
}
static int mm_find_dive(Kuribo *k, V3 p, V3 rv) /* findDivingPoint (fire only) */
{
    V3 hp;
    if (mm_ice(k) || line_hits(k->pos, p) || !kcl_line(p, vadd(p, rv), &hp, NULL, NULL) || (tri_attr(&kcl_line_tri) & 0x3F) != 10) return 0;
    k->mm[MM_DIVE_] = hp; k->mm[MM_DUP_] = vmul(k->grav, -1.0f);
    V3 d = vtangent(vsub(hp, k->pos), vnorm(k->mm[MM_DUP_]));
    k->mm[MM_DDIR_] = vlen(d) > 1e-6f ? vnorm(d) : v3(0, 0, 0);
    return 1;
}
static int mm_check_dive(Kuribo *k, float f, float deg) /* checkDivingPoint */
{
    k->dir = 0;
    if (mm_hit_recover(k)) { k->mm[MM_DIVE_] = k->pos; k->mm[MM_DUP_] = vmul(k->grav, -1.0f); k->dir = 1; return 1; } /* checkDirectDivingPoint */
    V3 a = rot_axis(vmul(k->front, f), vnorm(k->grav), deg * 0.0174533f), p = vsub(vadd(k->pos, a), vmul(k->grav, 100.0f));
    k->dir = mm_find_dive(k, p, vadd(vmul(k->grav, 600.0f), a));
    return k->dir;
}
static int mm_check_dive_more(Kuribo *k, float f) /* checkDivingPointMore */
{
    V3 a = vmul(k->mm[MM_DDIR_], f), p = vsub(vadd(k->mm[MM_DIVE_], a), vmul(k->grav, 100.0f));
    return mm_find_dive(k, p, vadd(vmul(k->grav, 600.0f), a));
}
static void mm_runaway_jump(Kuribo *k) /* addRunawayJumpPower */
{
    V3 v;
    float val = 1.0f;
    if (k->dir) { v = vsub(k->mm[MM_DIVE_], k->pos); val = bt_norm(vlen(vtangent(v, vnorm(k->grav))), 10.0f, 500.0f); }
    else if (mm_near_player(k->pos, 1000.0f)) v = vsub(k->pos, mario->pos);
    else v = vsub(k->tcenter, k->pos);
    v = vlen(v) > 1e-6f ? vnorm(v) : v3(0, 0, 0);
    float r = 0.5f * val;
    v = vadd(v, v3(mm_rnd() * r, mm_rnd() * r, mm_rnd() * r)); /* addRandomVector */
    float h = 20.0f * (0.1f + val * 0.9f), up = (0.2f + val * 0.8f) * frand(45.0f, 50.0f);
    mm_moving_accel(k, v, h, -1.0f);
    k->vel = vsub(k->vel, vmul(k->grav, up));
}
static int mm_try_force_sink(Kuribo *k) /* tryForceSink */
{
    if (!mm_hit_recover(k)) return 0;
    k->mm[MM_DIVE_] = k->pos; k->mm[MM_DUP_] = vmul(k->grav, -1.0f);
    k->vel = v3(0, 0, 0);
    w_nrv(k, MM_SINK);
    return 1;
}
static void mm_update_extinguish(Kuribo *k) /* updateExtinguishMtx */
{
    mm_turn_fire_to_spin(k, 10.0f);
    V3 t = k->mm[MM_EXT_], pv = mm_pull(t);
    t = vadd(t, vmul(pv, 0.9f));
    V3 d = vsub(mario->pos, t);
    float l = vlen(d);
    if (0.0f < l && l < 100.0f) t = vadd(mario->pos, vmul(d, 100.0f / l));
    k->mm[MM_EXT_] = t;
    k->mm[MM_EXT_ + 1] = vlen(pv) > 1e-6f ? vnorm(pv) : v3(0, 1, 0);
}
/* ---- the messages ---- */
static int mm_down_force(Kuribo *k, V3 from) /* requestDownForce */
{
    if (k->state == KURIBO_DEAD || k->nrv == MM_DOWN || k->nrv == MM_READYRESTART) return 0;
    mm_fx(k, "HitMark", vmul(vadd(k->pos, from), 0.5f), k->qy, NULL); /* emitEffectHitBetweenSensors */
    V3 h = vtangent(vsub(k->pos, from), vnorm(k->grav)); /* calcSensorHorizonNormalize */
    if (vlen(h) > 1e-6f) k->front = vnorm(h);
    k->vel = v3(0, 0, 0);
    w_nrv(k, MM_DOWN);
    printf("meramera row %d: down\n", k->row);
    return 1;
}
static int mm_force_runaway(Kuribo *k) /* requestForceRunaway */
{
    if (!mm_affected_spin(k)) return 0;
    w_act(k, "Damage");
    w_nrv(k, MM_RUNAWAY);
    return 1;
}
static int mm_star(Kuribo *k) { return k->state != KURIBO_DEAD && !k->phase && mm_force_runaway(k); } /* isMsgStarPieceAttack */
static int mm_fire(Kuribo *k, V3 from) /* isMsgFireBallAttack: requestFire */
{
    if (k->state == KURIBO_DEAD || k->phase) return 0;
    if (!mm_ice(k)) {
        if (!mm_enable_fireball(k)) return 0;
        w_nrv(k, MM_IGNITIONFORCE);
        return 1;
    }
    return mm_force_runaway(k) || mm_down_force(k, from);
}
static int mm_damage(Kuribo *k, V3 mc) /* isMsgSpinStormRange: requestDamage */
{
    if (!mm_affected_spin(k)) return 0;
    if (vlen(vsub(mc, k->pos)) >= 300.0f) { mm_head(k, MME_WAIT); mm_turn_fire_to_spin(k, 5.0f); return 0; }
    if (mario->spin_timer <= 0) return 0; /* isPlayerSwingAction */
    V3 h = vtangent(vsub(k->pos, mc), vnorm(k->grav)); /* setVelocitySeparateHV(35, 0) */
    k->vel = vlen(h) > 1e-6f ? vmul(vnorm(h), 35.0f) : v3(0, 0, 0);
    w_nrv(k, MM_DAMAGE);
    printf("meramera row %d: damage\n", k->row);
    return 1;
}
/* ---- the nerves ---- */
static void mm_init(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z;
    row_axes(r, &x, &z);
    k->item = strstr(walker_cls(k), "IceMeramera") != NULL; /* initElementType */
    k->grav = vmul(walker_up(k, k->up), -1.0f);             /* calcGravity */
    k->qy = vmul(k->grav, -1.0f); k->qz = vnorm(vtangent(z, k->qy));
    if (vlen(k->qz) < 1e-3f) k->qz = vnorm(vtangent(x, k->qy));
    k->front = v3(0, 0, 1); k->mm[MM_FIRE_] = v3(0, 1, 0); /* _138 (0, 0, 1), _144 (0, 1, 0) */
    k->mm[MM_DDIR_] = v3(0, 0, 1); k->mm[MM_DUP_] = v3(0, 1, 0);
    k->from = -1.0f; k->tcenter = k->pos;                   /* _1A4, mHomePosition */
    k->sstep = 0;
    w_act(k, "Wait"); /* (no clip started yet: the ported draw path) */
    mm_reset_appear(k);                                     /* initAppearState (ponytail: no appear switch on its rows) */
}
static void mm_update(Kuribo *k)
{
    float s = w_scale(k);
    if (!k->front.x && !k->front.y && !k->front.z) mm_init(k);
    if (k->sstep < 0 && k->nrv != MM_READYRESTART) { mm_head_off(k); w_nrv(k, MM_WAIT); } /* clipped (update_kuribo): startClipped deleteEffectAll, endClipped Wait */
    k->sstep = 0;
    if (!k->bind_off) k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    V3 up = vmul(k->grav, -1.0f);
    int st = ++k->step;
    switch (k->nrv) {
    case MM_WAIT:
        if (st == 0) { k->lvt[2] = 0; w_act(k, "Wait"); mm_head(k, MME_WAIT); mm_body(k, MMB_HEAT); }
        w_se_lv(k, mm_ice(k) ? SE_EM_LV_ICEMERA_WAIT : SE_EM_LV_MERAMERA_WAIT);
        mm_keep_height(k, 130.0f, 0.5f, 0.1f, 15.0f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (mm_enable_chase(k)) w_nrv(k, MM_CHASESTART); /* tryChase */
        else if (st > 120) { /* tryWalk */
            V3 v = v3(mm_rnd(), mm_rnd(), mm_rnd());
            v = vlen(v) > 1e-6f ? vnorm(v) : v;
            k->mm[MM_WALK_] = vadd(vmul(vtangent(v, vnorm(k->grav)), 200.0f), k->tcenter);
            w_nrv(k, MM_WALK);
        }
        break;
    case MM_WALK:
        if (st == 0) { k->lvt[2] = 0; mm_head(k, MME_WAIT); mm_body(k, MMB_HEAT); }
        w_se_lv(k, mm_ice(k) ? SE_EM_LV_ICEMERA_WAIT : SE_EM_LV_MERAMERA_WAIT);
        mm_keep_height(k, 130.0f, 0.5f, 0.1f, 15.0f);
        mm_target_accel(k, k->mm[MM_WALK_], 0.05f, 0.995f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (mm_enable_chase(k)) w_nrv(k, MM_CHASESTART);
        else if (st > 300 || (0.0f <= k->from && k->from <= 100.0f)) w_nrv(k, MM_WAIT); /* tryWalkEnd */
        break;
    case MM_SHRINK:
        if (st == 0) { k->lvt[2] = 1; w_act(k, "Wait"); mm_head(k, MME_WAIT); mm_body(k, MMB_HEAT); }
        mm_turn_fire_to_spin(k, 5.0f);
        mm_keep_height(k, 130.0f, 0.5f, 0.1f, 15.0f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (st > 20) w_nrv(k, mm_enable_chase(k) ? MM_CHASESTART : MM_WAIT); /* tryEndShrink */
        break;
    case MM_CHASESTART:
        if (st == 0) {
            k->lvt[2] = 0; w_act(k, "ChaseStart"); w_se(k, SE_EM_MERAMERA_FIND);
            w_se(k, mm_ice(k) ? SE_EM_ICEMERA_POWER_UP : SE_EM_MERAMERA_FIREPOWER_UP);
            mm_head_off(k); mm_body(k, MMB_HEAT);
        }
        w_se_lv(k, mm_ice(k) ? SE_EM_LV_ICEMERA_WAIT : SE_EM_LV_MERAMERA_WAIT);
        mm_keep_height(k, 300.0f, 0.5f, 0.2f, 15.0f);
        mm_target_accel(k, mario->pos, 0.05f, 0.99f);
        k->vel = vmul(k->vel, 0.97f);
        mm_rebound(k);
        if (!mm_near_player(k->pos, 900.0f) || st > 240 || vlen(vsub(k->tcenter, k->pos)) > 3000.0f) w_nrv(k, MM_WAIT); /* tryEndChase */
        else if (st > 60) w_nrv(k, MM_CHASEDASH);
        break;
    case MM_CHASEDASH: {
        if (st == 0) {
            k->lvt[2] = 1; w_act(k, "ChaseDash"); w_se(k, SE_EM_MERAMERA_DASH);
            k->ttarget = vsub(mario->pos, k->pos); /* _15C */
            mm_head(k, MME_CHASE); mm_body(k, MMB_HEAT);
        }
        w_se_lv(k, mm_ice(k) ? SE_EM_LV_ICEMERA_CHASE : SE_EM_LV_MERAMERA_CHASE);
        if (st < 30) mm_keep_height(k, 150.0f, 0.5f, 0.1f, 15.0f);
        else mm_keep_height(k, 190.0f, 0.5f, 0.2f, 20.0f);
        mm_moving_accel(k, k->ttarget, 0.175f, 0.98f);
        V3 h = vtangent(k->ttarget, vnorm(k->grav)); /* addOverWallAccel */
        h = vlen(h) > 1e-6f ? vnorm(h) : h;
        float sl = mm_shadow_len(k);
        if (line_hits(k->pos, vadd(k->pos, vmul(h, 200.0f))) && sl >= 0.0f && sl < 250.0f) k->vel = vsub(k->vel, vmul(k->grav, 0.9f));
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (st > 45) w_nrv(k, MM_CHASETURN);
        break;
    }
    case MM_CHASETURN:
        if (st == 0) { k->lvt[2] = 0; w_act(k, "ChaseTurn"); mm_head(k, MME_CHASE); mm_body(k, MMB_HEAT); }
        w_se_lv(k, mm_ice(k) ? SE_EM_LV_ICEMERA_CHASE : SE_EM_LV_MERAMERA_CHASE);
        mm_keep_height(k, 190.0f, 0.3f, 0.3f, 20.0f);
        mm_target_accel(k, mario->pos, 0.05f, 0.99f);
        k->vel = vmul(k->vel, 0.97f);
        mm_rebound(k);
        if (!mm_near_player(k->pos, 900.0f) || st > 240 || vlen(vsub(k->tcenter, k->pos)) > 3000.0f) w_nrv(k, MM_WAIT);
        else if (st > 50) w_nrv(k, MM_CHASEDASH);
        break;
    case MM_DAMAGE:
        if (st == 0) {
            k->lvt[2] = 1; w_act(k, "Damage");
            turn_on_plane(&k->front, vsub(mario->pos, k->pos), k->grav, -1.0f);
            w_se(k, mm_ice(k) ? SE_EM_ICEMERA_BLOW_COLD : SE_EM_MERAMERA_BLOW_FIRE);
            mm_head(k, MME_CHASE); mm_body(k, MMB_HEAT);
        }
        if (st == 3) {
            k->mm[MM_EXT_] = mm_sin(k); k->mm[MM_EXT_ + 1] = up;
            mm_head_off(k); mm_body(k, MMB_COOLDOWN);
            mm_fx(k, "ExtinguishSide", k->mm[MM_EXT_], up, NULL);
            mm_fx(k, "ExtinguishSmoke", mm_sin(k), k->mm[MM_FIRE_], NULL);
            w_se(k, SE_EM_MERAMERA_SMOKE);
        }
        if (st >= 3) mm_update_extinguish(k);
        mm_keep_height(k, 140.0f, 0.0f, 1.2f, 15.0f);
        k->vel = vmul(k->vel, 0.9f);
        mm_rebound_each(k, 0.0f, 0.0f, 0.0f, 0.0f);
        if (st > 30) w_nrv(k, MM_RUNAWAY); /* tryRunaway: getRandom(0, 0) + sRunawayTime */
        break;
    case MM_RUNAWAY: {
        if (st == 0) { k->lvt[2] = 1; k->dir = 0; mm_head_off(k); mm_body(k, MMB_COLD); k->timer = 0; } /* validateHitSensor break */
        if (mm_try_force_sink(k)) break;
        float dot = vdot(k->grav, k->vel);
        if (st == 10) mm_head(k, MME_ESCAPE);
        if (k->on_ground && dot > 12.0f) { mm_fx_body(k, "EscapeRock"); w_se(k, SE_EM_MERAMERA_BOUND); }
        if (k->dir && k->on_ground) { k->vel = v3(0, 0, 0); w_nrv(k, MM_STARTDIVING); break; } /* tryStartDiving */
        if (st > 70) {
            if (k->on_ground) {
                if (vlen(vtangent(k->vel, vnorm(k->grav))) < 3.0f) k->timer--;
                if (k->timer <= 0) { mm_runaway_jump(k); mm_fx_body(k, "EscapeRock"); w_se(k, SE_EM_MERAMERA_JUMP); k->timer = rand() % 10 + 30; }
            }
            if (!k->dir) {
                float v = (st % 5 * 80.0f) / 5.0f;
                if (st % 2 == 1) v = -v;
                mm_check_dive(k, 250.0f, v);
            }
        }
        if (!k->on_ground) mm_grav_add(k, 2.0f);
        k->vel = vmul(k->vel, k->on_ground ? 0.8f : 0.98f);
        mm_rebound_each(k, 0.5f, 0.3f, 0.0f, 10.0f);
        if (!mm_near_player(k->pos, 1500.0f) && !mm_near_player(k->tcenter, 1500.0f) && /* tryRecovery */
            render_clip_judge(&k->pos.x, s * 100.0f, 1e9f) && render_clip_judge(&k->tcenter.x, s * 100.0f, 1e9f)) {
            printf("meramera row %d: recovery\n", k->row);
            mm_reset_appear(k);
        }
        break;
    }
    case MM_STARTDIVING:
        if (st == 0) { k->lvt[2] = 1; mm_head_off(k); mm_body(k, MMB_COLD); k->act_rate = 0.0f; }
        if (st < 35) mm_check_dive_more(k, 1.0f / 0.35f);
        if (st > 35) w_nrv(k, MM_DIVING);
        break;
    case MM_DIVING: {
        if (st == 0) { /* mParabolicPath->initFromUpVector(mPosition, _180, _198, 300) */
            V3 d = vsub(k->mm[MM_DIVE_], k->pos), py = vnorm(k->mm[MM_DUP_]);
            float h = vdot(py, d), mx = 300.0f, disc = mx * (mx - h);
            V3 z = vsub(d, vmul(py, h));
            k->bpos = k->pos; k->len = vlen(z); k->bvel = k->len > 1e-6f ? vmul(z, 1.0f / k->len) : v3(0, 0, 0);
            if (disc <= 0.0f) { k->to = -mx; k->lva[1] = h + mx; } /* calcParabolicFunctionParam: _24 (to), _28 (lva[1]) */
            else if (fabsf(h) < 1e-4f) { k->to = -4.0f * mx; k->lva[1] = 4.0f * mx; }
            else {
                float rt = sqrtf(disc), t1 = (mx + rt) / h, t2 = (mx - rt) / h, t = t1 >= 0 && t1 <= 1 ? t1 : t2 >= 0 && t2 <= 1 ? t2 : 1.0f;
                k->to = -mx / (t * t); k->lva[1] = -2.0f * t * k->to;
            }
            mm_head_off(k); mm_body(k, MMB_COLD);
            turn_on_plane(&k->front, d, k->grav, -1.0f);
            k->lvt[2] = 1; k->act_rate = 1.4f;
            mm_fx_body(k, "EscapeRock"); w_se(k, SE_EM_MERAMERA_JUMP);
        }
        float t = st / 40.0f; /* calcPosition */
        V3 p = vadd(vadd(k->bpos, vmul(vnorm(k->mm[MM_DUP_]), t * (k->lva[1] + k->to * t))), vmul(k->bvel, k->len * t));
        k->vel = vsub(p, k->pos);
        mm_turn_deg(&k->mm[MM_FIRE_], up, 180.0f, k->front);
        if (!mm_try_force_sink(k) && st > 40) w_nrv(k, MM_RUNAWAY);
        break;
    }
    case MM_SINK:
        if (st == 0) {
            k->lvt[2] = 1;
            mm_fx(k, "Fall", k->pos, up, NULL);
            w_se(k, SE_EM_MERAMERA_LAVA_IN); w_act(k, "Wait");
            mm_hide_sunk(k);
        }
        turn_on_plane(&k->front, vsub(mario->pos, k->pos), k->grav, -1.0f);
        if (st > 50) w_nrv(k, MM_FLOAT); /* tryFloat (deleteEffect Revival: kept only in Sink) */
        break;
    case MM_FLOAT:
        if (st == 0) {
            k->lvt[2] = 1; k->phase = 0; k->bind_off = 0;
            w_act(k, "Float"); mm_fx(k, "Fall", k->pos, up, NULL); w_se(k, SE_EM_MERAMERA_LAVA_OUT);
            mm_head_off(k); mm_body(k, MMB_HEAT);
            k->vel = vmul(k->mm[MM_DUP_], 50.0f);
        }
        mm_keep_height(k, 100.0f, 0.9f, 0.1f, 50.0f);
        k->vel = vmul(k->vel, 0.94f);
        mm_rebound(k);
        turn_on_plane(&k->front, vsub(mario->pos, k->pos), k->grav, -1.0f);
        if (st > 30) w_nrv(k, MM_IGNITION);
        break;
    case MM_IGNITION:
        if (st == 15) {
            k->lvt[2] = 1; w_act(k, "FloatIgnition");
            w_se(k, mm_ice(k) ? SE_EM_ICEMERA_IGNITION : SE_EM_MERAMERA_IGNITION);
            k->vel = vadd(k->vel, vmul(k->mm[MM_DUP_], 25.0f));
        }
        mm_keep_height(k, 100.0f, 0.5f, 0.1f, 50.0f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (st > 20) w_nrv(k, mm_enable_chase(k) ? MM_CHASEDASH : MM_WAIT); /* tryEndIgnition */
        break;
    case MM_IGNITIONFORCE:
        if (st == 0) {
            k->lvt[2] = 1; w_act(k, "FloatIgnition");
            k->vel = vsub(vtangent(k->vel, vnorm(k->grav)), vmul(k->grav, 15.0f)); /* setVelocityJump */
            mm_head(k, MME_WAIT); mm_body(k, MMB_HEAT);
        }
        mm_keep_height(k, 130.0f, 0.5f, 0.1f, 15.0f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (w_bck_stopped(k)) w_nrv(k, MM_WAIT);
        break;
    case MM_ATTACKSUCCESS:
        if (st == 0) { k->lvt[2] = 1; w_act(k, "Attack"); k->vel = v3(0, 0, 0); mm_head_off(k); mm_body(k, MMB_HEAT); }
        mm_keep_height(k, 50.0f, 0.5f, 0.1f, 15.0f);
        k->vel = vmul(k->vel, 0.98f);
        mm_rebound(k);
        if (st > 180) w_nrv(k, MM_WAIT); /* tryEndAttackSuccess */
        break;
    case MM_DOWN:
        if (st == 0) {
            k->lvt[2] = 1; w_act(k, "Down"); scene_stop = 2; w_se(k, SE_EM_MERAMERA_STOMPED);
            mm_head_off(k); mm_body(k, MMB_COLD);
            k->vel = vsub(vmul(k->front, -0.0f), vmul(k->grav, 25.0f)); /* zeroVelocity, then step 0: -_138 x 0 - gravity x 25 */
        }
        if (!k->on_ground) mm_grav_add(k, 0.5f);
        k->vel = vmul(k->vel, 0.99f);
        mm_rebound_each(k, 0.9f, 0.9f, 0.9f, 0.0f);
        if (st > 0) { w_se(k, SE_EM_MERAMERA_EXPLODE); mm_kill(k); } /* (!isGreaterStep(0) || !isBinded) && !isGreaterStep(0): only step 0 waits */
        break;
    case MM_FLATDOWN:
        if (st == 0) { k->lvt[2] = 1; w_act(k, "FlatDown"); scene_stop = 2; w_se(k, SE_EM_MERAMERA_STOMPED); k->vel = v3(0, 0, 0); mm_head_off(k); mm_body(k, MMB_COLD); }
        if (st > 0) { w_se(k, SE_EM_EXPLODE_S); mm_kill(k); }
        break;
    case MM_READYRESTART:
        if (st > 180) { k->phase = 0; mm_reset_appear(k); } /* showModel, resetAppear */
        break;
    }
    if (k->state == KURIBO_DEAD) return;
    k->act_t += k->act_rate;
    k->lva[0] += 1.0f; /* its BTP's frame */
    /* control: updatePose, _144 turned 1 degree to -gravity (tryPointBind: ponytail) */
    if (mm_disabled(k)) { /* rotateQuatRollBall(mVelocity, -gravity, 50) */
        V3 mo = vmul(vcross(up, k->vel), 1.0f / 50.0f);
        float a = vlen(mo);
        if (a > 1e-6f) { mo = vmul(mo, 1.0f / a); k->qy = vnorm(rot_axis(k->qy, mo, a)); k->qz = vnorm(rot_axis(k->qz, mo, a)); }
    } else w_blend_quat(k, up, k->front, 0.25f, 0.25f);
    mm_turn_deg(&k->mm[MM_FIRE_], up, 1.0f, k->front);
    /* the Binder: 45 x scale about its place */
    k->mm[MM_FIX_] = v3(0, 0, 0);
    if (k->nrv == MM_READYRESTART) return;
    if (k->bind_off) { k->pos = vadd(k->pos, k->vel); k->on_ground = k->on_wall = 0; }
    else {
        V3 to = vadd(k->pos, k->vel), c = to, gn = up, wn = up;
        int wall = 0;
        k->on_ground = collide_n(&c, up, 45.0f * s, &gn, &wn, &wall);
        k->gn = k->on_ground ? gn : up; k->on_wall = wall; k->wn = wn;
        k->jc = k->on_ground ? ground_attr & 0x3F : -1; /* its ground's floor code (mm_hit_recover) */
        k->mm[MM_FIX_] = vsub(c, to);
        k->pos = c;
    }
    k->up = up;
    mm_effects(k);
    if (k->phase || k->nrv == MM_DOWN || k->nrv == MM_FLATDOWN) return;
    /* the sensors: Mario's attacks on its body (receiveMsgPlayerAttack / receiveMsgPush), his spin (SpinStormRange),
     * its attack sensor on him (attackSensor) */
    V3 body = k->pos, mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    float br = 55.0f * s;
    int off = mario_offence(k, body, br, k->grav);
    if (off == MSG_TRAMPLE || off == MSG_HIPDROP) { /* requestFlatDown */
        if (mm_disabled(k)) {
            mm_fx(k, "HitMark", vmul(vadd(body, mc), 0.5f), up, NULL);
            w_nrv(k, MM_FLATDOWN);
            if (off == MSG_TRAMPLE) mario_trample();
            printf("meramera row %d: flat down\n", k->row);
            return;
        }
    } else if (off == MSG_HITALL && mm_disabled(k) && mm_down_force(k, mc)) { m_punch(k, body, 0); return; } /* requestDown */
    if (mario_touch(body, br)) {
        if (mm_disabled(k)) { if (mm_down_force(k, mc)) { if (mario_kick(body, br)) mario_kicked(); return; } } /* requestDown, sendMsgKick */
        else if (mm_push_moved(k)) { V3 d = vsub(body, mc); if (vlen(d) > 1e-6f) k->vel = vadd(k->vel, vmul(vnorm(d), 0.5f)); }
        mario_push_out(body, br); /* attackSensor: sendMsgPush */
    }
    if (mario->spin_timer > 0 && mario->spin_t < 15 && vlen(vsub(mc, body)) < br + 600.0f && mm_damage(k, mc)) return;
    if (mm_enable_attack(k)) {
        V3 at = w_joint(k, mm_model(k), 2, v3(50.0f, 0, 0));
        if (!mario->hurt_timer && mario_touch(at, 70.0f * s) && mario_attack(mm_ice(k) ? ATK_FREEZE : ATK_FIRERUN, at)) { /* sendMsgElementAttack */
            w_nrv(k, MM_ATTACKSUCCESS);
            printf("meramera row %d: attack success\n", k->row);
            return;
        }
    }
    for (int i = 0; i < nkuribo; i++) { /* its body's push on the other Meramera (receiveMsgPush) */
        Kuribo *e = &kuribo[i];
        if (e == k || e->kind != WALK_MERAMERA || e->state == KURIBO_DEAD || e->phase || !mm_push_moved(e)) continue;
        V3 d = vsub(e->pos, body);
        if (vlen(d) < br + 55.0f * w_scale(e) && vlen(d) > 1e-6f) e->vel = vadd(e->vel, vmul(vnorm(d), 0.5f));
    }
}
static void mm_btp(Kuribo *k, const Model *md) /* its body's texture pattern (OnFire / RedToBlack / OffFire) */
{
    static const char *n[] = {"onfire", "onfire", "redtoblack", "offfire"};
    char key[48];
    snprintf(key, sizeof key, "%s/%s", walker_acls(k), n[k->lvt[1] & 3]);
    for (char *c = key; *c; c++) *c = (char)tolower((unsigned char)*c);
    btp_apply(md, btp_entry(key), k->lva[0]);
}
int game_test_mm(int frame) /* mm test (XF=-DMM_TEST, HellProminenceGalaxy/1): Mario 600 from the first Meramera (it
                             * chases and burns him), at 900 a spin by it (Damage, Runaway), then a trample on it */
{
    test_nolost = 1;
    static int row = -1, phase;
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_MERAMERA || (row >= 0 && k->row != row)) continue;
        if (k->state == KURIBO_DEAD) { if (frame % 20 == 0) printf("mm test f %d: row %d dead\n", frame, k->row); return -1; }
        if (row < 0 && (!row_shown(k->row) || (!k->front.x && !k->front.y && !k->front.z))) continue;
        row = k->row;
        V3 up = vmul(k->grav, -1.0f), f = vnorm(vtangent(k->qz, up)), g;
        if (frame % 20 == 0) printf("mm test f %d: row %d nerve %d step %d, %.0f from Mario, shadow %.0f, life %d\n", frame, k->row, k->nrv, k->step,
                                    vlen(vsub(mario->pos, k->pos)), mm_shadow_len(k), game_life());
        if (phase == 0 && (frame < 30 || vlen(vsub(mario->pos, k->pos)) > 2000.0f)) {
            if (drop_to_ground(vadd(vadd(k->tcenter, vmul(f, 600.0f)), vmul(up, 300.0f)), up, 2000.0f, &g)) {
                mario->up = up; mario->pos = vadd(g, vmul(up, 5.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            }
        }
        if (phase == 0 && frame >= 900 && mm_affected_spin(k)) { /* a spin by it */
            V3 d = vtangent(vsub(mario->pos, k->pos), up);
            d = vlen(d) > 1e-6f ? vnorm(d) : f;
            float sl = mm_shadow_len(k);
            mario->pos = vadd(vsub(k->pos, vmul(up, sl > 0 ? sl : 0)), vmul(d, 150.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME; mario->spin_t = 0;
            phase = 1;
            printf("mm test f %d: spin by it\n", frame);
        }
        if (phase == 1 && k->nrv == MM_RUNAWAY && k->step > 20) { /* a trample */
            mario->pos = vadd(k->pos, vmul(up, 80.0f)); mario->hvel = v3(0, 0, 0); mario->vup = -10.0f; mario->grounded = 0;
            phase = 2;
            printf("mm test f %d: trample\n", frame);
        }
        return k->nrv * 1000 + (k->step > 999 ? 999 : k->step);
    }
    return -2;
}
