/* Teresa.cpp after Petari (Boo; TeresaChief the same class with its power star). Included by game.c after
 * meramera.inc (mm_pull: calcPlayerSpinPullVelocity). Kuribo fields: nrv / step its nerve, front mWallNormal (_A4),
 * qy / qz its quaternion _94 (up, front), tcenter _C8 (its place), ttarget _E0 (Walk's target), mm[0] mWallHitPos,
 * mm[1] the binder's last fix vector, from _F4 (its transparency), to _D4.x (_D4.z its inverse), len its shadow
 * sphere's radius, phase hideModel, lvt[0] invalidateHitSensors, bind_off offBind, lvt[1] invalidateClipping,
 * lvt[2] its SpinBlur kept, dir / lva[1] its rail's direction / coord, lv its level sound.
 * Init: binder 60 x scale at its place (GhostThroughCode walls bind only while drifting: filterBind), its attack
 * sensor 70 x scale and body 85 x scale at its place, shadow sphere 80 (TeresaChief: dropped 3000); arg0 its
 * appearance (0 from the ground, 1 from a wall: on its SW_APPEAR; else the start nerve), arg7 its DummyDisplayModel
 * (shown inside it while it is not wholly opaque; 6 a KeySwitch given at its death, no coin; TeresaChief: a power
 * star, no coin; else one coin at its death). Start nerve: on a rail RailWalk (to the rail's place 0.35, 10 ahead;
 * at its end RailTurn 150), else Wait (192, then Walk to a place 600 off its home: 300 or within 200). Mario facing it
 * (his front to it, not squatting) within 1500: Shay (stop; its dummy shown at half), else within 1800 Chase (0.75 x
 * rate over 20, at most 240, to 2000), Shay ending into Aggressive (75). Its attack sensor on Mario: AttackSuccess
 * (Laugh). Mario's spin (SpinStormRange 600): Drift (SpinBlur, pulled by the spin 35 frames, -1.25 to him, 50
 * frames; squashed by its speed, its transparency dipping) and into a wall HideWall (60), back by AppearFromWall; in
 * water HideWater, back from the ground. A star piece: Stop (transparent, 60). A fire ball, invincible Mario or a
 * GlaringLightArea: Ascension (dead: Death, its key switch / power star / coin). Boo Mario: LoveFind / LoveChase /
 * LoveHit / LoveEnd. Its height: 150 + 85 x (scale - 1) over its shadow (0.4, 40), rebound -0.2 / 4 / 1, x 0.9.
 * ponytail: no point light (requestPointLight: the port's lights are the area ones); no star pointer shy (the 2P
 * pointer); isInShadeFromTheSun taken as true (no SunLightArea where Teresas are placed, no Sunshade collision);
 * no MarioSearchLight (not in the port); its transparency a constant alpha on its groups without texture alpha
 * (render: one alpha a pass); the actions' BTPs (Normal / Love) are the clips' same-name ones; the dummy model's
 * InTeresa / InRotation clips not played; pushes only between Teresas */
enum { TS_APPEARWALL, TS_APPEARGROUND, TS_WAIT, TS_WALK, TS_RAILWALK, TS_RAILTURN, TS_LOVEFIND, TS_LOVECHASE, TS_LOVEHIT,
       TS_LOVEEND, TS_CHASE, TS_SHAY, TS_AGGRESSIVE, TS_ATTACKSUCCESS, TS_DRIFT, TS_HIDEWALL, TS_HIDEWATER, TS_ASCENSION,
       TS_STOP };
static int ts_chief(const Kuribo *k) { return !strcmp(walker_cls(k), "TeresaChief"); } /* _FD */
static int ts_key(const Kuribo *k) { return sc->objs[k->row].arg[7] == 6; } /* getDummyDisplayModelId 6: mKeySwitch */
static int ts_coin(const Kuribo *k) { return !ts_chief(k) && !ts_key(k); } /* _FC */
static int ts_n(const Kuribo *k, int a) { return k->nrv == a; }
static void ts_fx(Kuribo *k, const char *name, const float *m, const void *key)
{
    float b[16];
    if (!m) { basis_matrix(b, k->pos, k->qy, k->qz, 0); m = b; }
    if (!fx_named(walker_cls(k), name, m, NULL, key)) fx_named("Teresa", name, m, NULL, key);
}
static void ts_fx_front(Kuribo *k, const char *name, V3 front, V3 at) /* emitEffectHit(makeMtxFrontNoSupportPos) */
{
    float m[16];
    V3 up = fabsf(front.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0);
    basis_matrix(m, at, vnorm(vtangent(up, vnorm(front))), front, 0);
    ts_fx(k, name, m, NULL);
}
static void ts_set_alpha(Kuribo *k, float v) { k->from = v; } /* setTransparency: setBrkFrameAndStop(9 x (1 - v)), its dummy */
static void ts_normal_alpha(Kuribo *k) /* updateNormalTransparency */
{
    if (k->from < 1.0f) { k->from += 0.02f; if (k->from > 1.0f) k->from = 1.0f; }
    ts_set_alpha(k, k->from);
}
static void ts_start(Kuribo *k) /* setStartNerve */
{
    u32 rn; int rcl;
    const float *rail = w_rail(k, &rn, &rcl);
    if (rail) {
        w_nrv(k, TS_RAILWALK); ts_set_alpha(k, 1.0f); w_act(k, "Wait");
        k->lva[1] = w_rail_nearest(rail, rn, k->pos); /* moveCoordToNearestPos */
    } else w_nrv(k, TS_WAIT);
}
static void ts_appear(Kuribo *k) /* appear */
{
    ts_set_alpha(k, 0.0f);
    int t = sc->objs[k->row].arg[0];
    if (t == 0 || t == 1) {
        k->lvt[1] = 1; k->bind_off = 0; k->mm[0] = k->pos; /* invalidateClipping, onBind */
        w_nrv(k, t == 0 ? TS_APPEARGROUND : TS_APPEARWALL);
    } else ts_start(k);
}
static void ts_hide(Kuribo *k) /* invalidateClipping, invalidateHitSensors, offBind, hideModel, zeroVelocity */
{
    k->lvt[1] = 1; k->lvt[0] = 1; k->bind_off = 1; k->phase = 1; k->vel = v3(0, 0, 0);
}
static void ts_show(Kuribo *k) { k->lvt[0] = 0; k->bind_off = 0; k->phase = 0; } /* validateHitSensors, onBind, showModel */
static void ts_end_appear(Kuribo *k) /* endAppear: validateClipping (not TeresaChief), startBrk Erase, transparency 1 */
{
    if (!ts_chief(k)) k->lvt[1] = 0;
    ts_set_alpha(k, 1.0f);
}
static void ts_end_drift(Kuribo *k) { k->lvt[2] = 0; ts_set_alpha(k, 1.0f); k->len = 80.0f; } /* endDrift */
static void ts_set_nrv(Kuribo *k, int n) /* setNerve: the old nerve's onEnd */
{
    if (k->nrv == TS_APPEARWALL || k->nrv == TS_APPEARGROUND) ts_end_appear(k);
    else if (k->nrv == TS_DRIFT) ts_end_drift(k);
    w_nrv(k, n);
}
static int ts_can_attack(const Kuribo *k)
{
    return !ts_n(k, TS_APPEARWALL) && !ts_n(k, TS_APPEARGROUND) && !ts_n(k, TS_ATTACKSUCCESS) && !ts_n(k, TS_DRIFT) && !ts_n(k, TS_ASCENSION);
}
static int ts_can_drift(const Kuribo *k) { return !ts_n(k, TS_DRIFT) && !ts_n(k, TS_ASCENSION); }
static int ts_can_die(const Kuribo *k) /* canSearchLightDead (isCheckWater the same nerves) */
{
    return !ts_n(k, TS_APPEARWALL) && !ts_n(k, TS_APPEARGROUND) && !ts_n(k, TS_HIDEWATER) && !ts_n(k, TS_HIDEWALL) && !ts_n(k, TS_ASCENSION);
}
static int ts_can_stop(const Kuribo *k) { return ts_can_die(k) && !ts_n(k, TS_DRIFT) && !ts_n(k, TS_STOP); } /* isEnableStarPieceStop */
static int ts_shay(const Kuribo *k) /* isShay */
{
    return !mario->squat && vdot(mario->face, vsub(k->pos, mario->pos)) > 0.0f && vlen(vsub(mario->pos, k->pos)) < 1500.0f;
}
/* requestSearchLightDead (isMsgSearchlightAttack / FireBallAttack / InvincibleAttack) */
static int ts_dead_req(Kuribo *k)
{
    if (!ts_can_die(k)) return 0;
    ts_set_nrv(k, TS_ASCENSION);
    return 1;
}
static int ts_star(Kuribo *k) /* isMsgStarPieceAttack */
{
    if (!ts_can_stop(k)) return 0;
    ts_set_nrv(k, TS_STOP);
    return 1;
}
static void ts_turn(Kuribo *k, V3 target, float c) { turn_on_plane(&k->front, vsub(target, k->pos), k->grav, c); } /* turnDirectionToTarget */
static void ts_move_to(Kuribo *k, V3 target, float a) { bt_move_dir(k, vsub(target, k->pos), a); } /* addVelocityMoveToTarget */
static void ts_normal_vel(Kuribo *k) /* updateNormalVelocity */
{
    float sy = sc->objs[k->row].scale[1], drop = ts_chief(k) ? 3000.0f : 1000.0f; /* setShadowDropLength */
    V3 g = vnorm(k->grav), hp;
    /* addVelocityKeepHeightUseShadow(150 + 85 (scale - 1), 0.4, 40): the shadow's projection */
    if (kcl_line(k->pos, vadd(k->pos, vmul(g, drop)), &hp, NULL, NULL))
        k->vel = vadd(k->vel, vmul(g, 0.4f * bt_nabs(vlen(vsub(hp, k->pos)) - (150.0f + 85.0f * (sy - 1.0f)), -40.0f, 40.0f)));
    V3 f = k->mm[1]; /* reboundVelocityFromCollision(-0.2, 4, 1): getBindedFixReactionVector */
    if (vlen(f) > 1e-4f) {
        f = vnorm(f);
        float d = vdot(f, k->vel);
        if (d < -4.0f) { k->vel = vsub(k->vel, vmul(f, d)); k->vel = vsub(k->vel, vmul(f, d * -0.2f)); }
        else if (d < 0.0f) k->vel = vsub(k->vel, vmul(f, d));
    }
    k->vel = vmul(k->vel, 0.9f); /* attenuateVelocity */
}
static int ts_ascension(Kuribo *k) /* tryAscension: GlaringLightArea (or out of the sun's shade: ponytail, never) */
{
    int a = area_obj("GlaringLightAreaCylinder", k->pos);
    if (a < 0 || (sc->objs[a].sw[SW_A] >= 0 && !sw_on(&sc->objs[a], SW_A))) return 0; /* GlaringLightArea::movement: valid on its SW_A */
    ts_set_nrv(k, TS_ASCENSION);
    return 1;
}
static int ts_try_shay(Kuribo *k)
{
    if (player_mode == PM_TERESA || !ts_shay(k)) return 0;
    ts_set_nrv(k, TS_SHAY);
    return 1;
}
static int ts_try_love_find(Kuribo *k)
{
    if (player_mode != PM_TERESA || vlen(vsub(mario->pos, k->pos)) >= 1500.0f) return 0;
    ts_set_nrv(k, TS_LOVEFIND);
    return 1;
}
static int ts_try_love_end(Kuribo *k)
{
    if (player_mode == PM_TERESA || vlen(vsub(mario->pos, k->pos)) >= 1500.0f) return 0;
    ts_set_nrv(k, TS_LOVEEND);
    return 1;
}
static void ts_kill(Kuribo *k) /* kill */
{
    V3 up = vmul(k->grav, -1.0f);
    ts_fx(k, "Death", NULL, NULL);
    if (ts_key(k)) okey_appear(k->row, k->pos); /* appearKeySwitch */
    if (ts_chief(k)) { bgm_after_boss(); note_star_row = k->row; note_star_at = k->pos; } /* startAfterBossBGM, requestAppearPowerStar */
    if (ts_coin(k)) pop_coins(k->pos, up, 1); /* appearCoinPop */
    k->lvt[2] = 0;
    k->state = KURIBO_DEAD; row_kill(k->row); /* onSwitchDead */
    printf("teresa row %d ascends\n", k->row);
}
static void ts_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    float sx = r->scale[0], sy = r->scale[1];
    u32 rn = 0; int rcl = 0;
    const float *rail = w_rail(k, &rn, &rcl);
    (void)rcl;
    if (!k->front.x && !k->front.y && !k->front.z) { /* init */
        V3 x, z;
        row_axes(r, &x, &z);
        k->grav = vmul(walker_up(k, k->up), -1.0f);
        k->front = k->qz = vnorm(z); k->qy = vnorm(vcross(z, x)); /* makeQuatAndFrontFromRotate */
        k->tcenter = k->pos; k->mm[0] = k->pos; k->mm[1] = v3(0, 0, 0);
        k->dir = 1;
        if (rail) { k->lva[1] = w_rail_nearest(rail, rn, k->pos); k->pos = rail_at(rail, rn, k->lva[1]); } /* moveCoordAndTransToNearestRailPos */
        k->to = 1.0f; k->len = 80.0f;
        if (ts_chief(k)) k->lvt[1] = 1; /* invalidateClipping */
        ts_set_alpha(k, 1.0f);
        if (r->sw[SW_APPEAR] >= 0) ts_appear(k); /* syncStageSwitchAppear: appear */
        else ts_start(k);
    }
    k->grav = vmul(walker_up(k, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    if (k->lvt[2]) ts_fx(k, "SpinBlur", NULL, k);
    int st = ++k->step;
    V3 mp = mario->pos;
    switch (k->nrv) {
    case TS_APPEARWALL: case TS_APPEARGROUND: {
        V3 d = k->nrv == TS_APPEARWALL ? k->qz : k->qy; /* _94's Z / Y */
        if (st == 0) {
            k->phase = 0; w_act(k, k->nrv == TS_APPEARWALL ? "AppearWall" : "AppearGround"); k->vel = v3(0, 0, 0);
            ts_fx_front(k, "Appear", d, k->mm[0]); w_se(k, SE_EM_TERESA_APPEAR);
        }
        k->vel = vadd(k->vel, vmul(d, 0.2f));
        k->vel = vmul(k->vel, 0.9f);
        float t = bt_rate(k, 60);
        k->len = 80.0f * (1.0f - (1.0f - t) * (1.0f - t));
        if (st > 0 && w_bck_stopped(k)) ts_start(k);
        break;
    }
    case TS_WAIT:
        if (st == 0) w_act(k, "Wait");
        w_se_lv(k, SE_EM_LV_TERESA_MOVE);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (player_mode != PM_TERESA && !ts_shay(k) && vlen(vsub(mp, k->pos)) < 1800.0f) { ts_set_nrv(k, TS_CHASE); break; } /* tryChase */
        if (st > 192) { /* tryWalk: a random way 600 off its place */
            V3 v = v3(frand(-1.0f, 1.0f), frand(-1.0f, 1.0f), frand(-1.0f, 1.0f));
            if (vlen(v) > 1e-6f) v = vnorm(v);
            v = vtangent(v, vnorm(k->grav));
            k->ttarget = vadd(vmul(v, 600.0f), k->tcenter);
            ts_set_nrv(k, TS_WALK);
            break;
        }
        ts_try_love_find(k);
        break;
    case TS_WALK:
        if (st == 0) { /* startBtp Normal */ }
        ts_turn(k, k->ttarget, 0.9995f);
        bt_move_dir(k, k->front, 0.2f);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 300 || vlen(vsub(k->ttarget, k->pos)) <= 200.0f) { ts_set_nrv(k, TS_WAIT); break; } /* tryWalkEnd */
        ts_try_love_find(k);
        break;
    case TS_RAILWALK: {
        if (!rail) { ts_set_nrv(k, TS_WAIT); break; }
        V3 rp = rail_at(rail, rn, k->lva[1]);
        if (vlen(vtangent(vsub(rp, k->pos), vnorm(k->grav))) < 10.0f) { k->lva[1] += 10.0f * k->dir; rp = rail_at(rail, rn, k->lva[1]); } /* moveCoord 10 */
        ts_turn(k, rp, 0.9995f);
        ts_move_to(k, rp, 0.35f);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        float len = w_rail_len(rail, rn);
        if (k->dir > 0 ? k->lva[1] >= len : k->lva[1] <= 0.0f) { /* tryRailTurn: isRailReachedGoal, reverseRailDirection */
            k->lva[1] = k->lva[1] < 0.0f ? 0.0f : k->lva[1] > len ? len : k->lva[1];
            k->dir = -k->dir; ts_set_nrv(k, TS_RAILTURN); break;
        }
        ts_try_love_find(k);
        break;
    }
    case TS_RAILTURN: {
        if (!rail) { ts_set_nrv(k, TS_WAIT); break; }
        V3 rp = rail_at(rail, rn, k->lva[1]);
        if (st > 60) { /* turnDirection(getRailDirection, 0.9997) */
            V3 dv = vsub(rail_at(rail, rn, k->lva[1] + k->dir), rp);
            if (vlen(dv) > 1e-6f) turn_on_plane(&k->front, dv, k->grav, 0.9997f);
        }
        ts_move_to(k, rp, 0.1f);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 150) { ts_set_nrv(k, TS_RAILWALK); break; }
        ts_try_love_find(k);
        break;
    }
    case TS_LOVEFIND:
        if (st == 0) { w_act(k, "LoveFind"); w_se(k, SE_EM_TERESA_LOVE_FIND); w_se(k, SE_EV_TERESA_LAUGH); }
        ts_turn(k, mp, 0.98f);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 0 && w_bck_stopped(k)) { ts_set_nrv(k, TS_LOVECHASE); break; }
        ts_try_love_end(k);
        break;
    case TS_LOVECHASE:
        if (st == 0) w_act(k, "LoveChase");
        ts_turn(k, mp, 0.999f);
        bt_move_dir(k, k->front, 0.75f * bt_rate(k, 20));
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k) || ts_try_love_end(k)) break;
        if (st > 240) ts_start(k);
        break;
    case TS_LOVEHIT:
        if (st == 0) { w_act(k, "LoveHit"); w_se(k, SE_EM_TERESA_LOVE_HIT); w_se(k, SE_EV_TERESA_LOVE_HIT); }
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 0 && w_bck_stopped(k)) ts_set_nrv(k, TS_LOVECHASE);
        break;
    case TS_LOVEEND: case TS_AGGRESSIVE:
        if (st == 0) w_act(k, k->nrv == TS_LOVEEND ? "LoveEnd" : "Aggressive");
        if (k->nrv == TS_AGGRESSIVE) w_se_lv(k, SE_EM_LV_TERESA_MOVE);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 75) ts_set_nrv(k, TS_WAIT); /* tryAggressiveEnd */
        break;
    case TS_CHASE:
        if (st == 0) { w_act(k, "Chase"); w_se(k, SE_EV_TERESA_LAUGH); }
        w_se_lv(k, SE_EM_LV_TERESA_MOVE);
        ts_turn(k, mp, 0.999f);
        bt_move_dir(k, k->front, 0.75f * bt_rate(k, 20));
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k) || ts_try_shay(k)) break;
        if (st > 240 || player_mode == PM_TERESA || vlen(vsub(mp, k->pos)) >= 2000.0f) ts_start(k); /* tryCheseEnd */
        break;
    case TS_SHAY: {
        int dummy = r->arg[7] >= 0 && r->arg[7] < 15; /* mDisplayModel */
        if (st == 0) { w_act(k, "Shay"); w_se(k, SE_EM_TERESA_STOP); k->vel = v3(0, 0, 0); }
        if (ts_chief(k)) w_se_lv(k, SE_OJ_LV_POW_STAR_EXIST);
        if (dummy) ts_set_alpha(k, bt_ease_io(bt_rate(k, 60), k->from, 0.5f)); /* calcNerveEaseInOutValue(60, _F4, 0.5) */
        else ts_normal_alpha(k);
        ts_normal_vel(k);
        if (ts_ascension(k)) break;
        if (player_mode == PM_TERESA) ts_start(k); /* tryShayEnd */
        else if (!ts_shay(k)) { if (rail) ts_start(k); else ts_set_nrv(k, TS_AGGRESSIVE); }
        break;
    }
    case TS_ATTACKSUCCESS:
        if (st == 0) { w_act(k, "Laugh"); k->len = 80.0f; w_se(k, SE_EV_TERESA_ATTACK_SUCCESS); }
        ts_turn(k, mp, 0.99f);
        ts_normal_alpha(k); ts_normal_vel(k);
        if (ts_ascension(k)) break;
        if (st > 0 && w_bck_stopped(k)) ts_set_nrv(k, TS_WAIT);
        break;
    case TS_DRIFT: {
        if (st == 0) { w_act(k, "Drift"); k->lvt[2] = 1; k->to = 1.0f; w_se(k, SE_EM_TERESA_TORNADE); } /* forceDeleteEffectAll, SpinBlur, _D4 1 */
        ts_turn(k, mp, 0.96f);
        if (st <= 35) k->vel = vadd(k->vel, vmul(mm_pull(k->pos), 0.12f)); /* addDriftVelocity */
        bt_move_dir(k, vsub(k->pos, mp), -1.25f); /* addVelocityAwayFromTarget(-1.25) */
        ts_normal_vel(k);
        { V3 x = vcross(k->qy, k->qz); /* updateDriftAnimScale: _94's X */
          k->to = 1.0f + 0.3f * bt_norm(fabsf(vdot(x, k->vel)), 3.0f, 53.0f); }
        { float t = bt_rate(k, 50), u = 4.0f * t * (t - 1.0f), v = 1.0f - u * u; /* updateDriftTransparency */
          ts_set_alpha(k, k->from + (v - k->from) * bt_rate(k, 25)); k->len = 80.0f * v; } /* calcNerveValue(25, _F4, v) */
        if (ts_ascension(k)) break;
        if (k->on_wall) { /* tryHideWall: isBindedWall */
            k->front = k->wn; k->mm[0] = vsub(k->pos, vmul(k->wn, 60.0f * sx));
            ts_fx_front(k, "HideWall", k->front, k->mm[0]);
            ts_hide(k); ts_set_nrv(k, TS_HIDEWALL);
            printf("teresa row %d hides in a wall\n", k->row);
            break;
        }
        if (st > 50) ts_set_nrv(k, TS_WAIT); /* tryDriftEnd */
        break;
    }
    case TS_HIDEWALL:
        if (st == 0) { w_act(k, "Hide"); k->len = 0.0f; }
        if (st > 60) { ts_show(k); ts_set_nrv(k, TS_APPEARWALL); } /* tryHideWallEnd */
        break;
    case TS_HIDEWATER:
        if (st == 0) { w_act(k, "Hide"); k->len = 0.0f; k->mm[0] = k->tcenter; }
        if (st > 60) { ts_show(k); ts_set_nrv(k, TS_APPEARGROUND); }
        break;
    case TS_ASCENSION:
        if (st == 0) { k->to = 1.0f; w_se(k, SE_EM_TERESA_DEAD); w_se(k, SE_EV_TERESA_DEAD); ts_kill(k); return; }
        break;
    case TS_STOP:
        if (st == 0) { k->vel = v3(0, 0, 0); w_act(k, "Wait"); ts_set_alpha(k, 0.0f); }
        if (ts_ascension(k)) break;
        if (st > 60) ts_start(k);
        break;
    }
    if (k->state == KURIBO_DEAD) return;
    k->act_t += k->act_rate;
    /* control */
    if (!ts_n(k, TS_DRIFT) && !ts_n(k, TS_ASCENSION)) k->to = 1.0f + 0.95f * (k->to - 1.0f);
    if (ts_can_die(k)) { /* tryHideWater (isCheckWater) */
        float depth;
        V3 w = vadd(k->pos, vmul(k->grav, 70.0f * sy));
        if (in_water(w, vmul(k->grav, -1.0f), &depth)) {
            ts_fx_front(k, "HideWall", vmul(k->grav, -1.0f), w);
            ts_hide(k); ts_set_nrv(k, TS_HIDEWATER);
        }
    }
    w_blend_quat(k, vmul(vnorm(k->grav), -1.0f), k->front, 0.1f, 0.2f); /* blendQuatUpFront(-gravity, mWallNormal, 0.1, 0.2) */
    /* the Binder: 60 x scale at its place; GhostThroughCode walls pass but while drifting (filterBind) */
    k->mm[1] = v3(0, 0, 0);
    if (!k->bind_off) {
        V3 up = vmul(vnorm(k->grav), -1.0f), want = vadd(k->pos, k->vel), c = want, gn = up, wn = up;
        int wall = 0, gt = ghost_through;
        ghost_through = k->nrv != TS_DRIFT;
        k->on_ground = collide_n(&c, up, 60.0f * sx, &gn, &wn, &wall);
        ghost_through = gt;
        k->gn = k->on_ground ? gn : up; k->on_wall = wall; k->wn = wn;
        k->mm[1] = vsub(c, want);
        k->pos = c;
    } else { k->pos = vadd(k->pos, k->vel); k->on_ground = k->on_wall = 0; }
    k->up = k->qy;
    if (k->lvt[0] || k->step < 0) return; /* invalidateHitSensors; a nerve just set */
    /* Mario's messages (MarioActor::attackSensor on its sensors: body 85 x scale the larger) */
    V3 mc = mw.c;
    float dm = vlen(vsub(mc, k->pos));
    if (player_mode == PM_INVINCIBLE && mario_touch(k->pos, 85.0f * sx) && ts_dead_req(k)) return; /* isMsgInvincibleAttack */
    if (mario->spin_timer > 0 && mario->spin_t < 15 && dm < 85.0f * sx + 600.0f && mode_action("スピンアタック") < 4 && ts_can_drift(k)) {
        ts_set_nrv(k, TS_DRIFT); printf("teresa row %d drifts\n", k->row); return; /* SpinStormRange: requestDrift */
    }
    if (player_mode == PM_TERESA && dm < 85.0f * sx + MC.mTeresaBodyRadius && dm > 1e-4f) { /* as Teresa: reflection, ACTMES_TERESA_PLAYER_TOUCH */
        V3 off = vsub(mc, k->pos);
        teresa_reflect(mario, vmul(vnorm(off), 85.0f * sx + MC.mTeresaBodyRadius - dm), 0);
        if (ts_n(k, TS_LOVECHASE)) { /* requestLoveHit: addVelocitySeparateHV(20, 0) */
            V3 h = vtangent(vsub(k->pos, mc), vnorm(k->grav));
            if (vlen(h) > 1e-6f) k->vel = vadd(k->vel, vmul(vnorm(h), 20.0f));
            ts_set_nrv(k, TS_LOVEHIT);
        }
        set_player_mode(PM_NORMAL, 1); /* an enemy sensor */
        return;
    }
    /* attackSensor: its attack sensor on Mario (requestAttack), on the other Teresas a push (receiveMsgPush 0.1) */
    if (ts_can_attack(k) && !mario->hurt_timer && dm < 70.0f * sx + HIT_BODY_R && mario_attack(ATK_ENEMY, k->pos)) { ts_set_nrv(k, TS_ATTACKSUCCESS); return; }
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (e == k || e->kind != WALK_TERESA || e->state == KURIBO_DEAD || e->lvt[0] || !row_shown(e->row)) continue;
        V3 d = vsub(e->pos, k->pos);
        if (vlen(d) >= 85.0f * sx + 85.0f * sc->objs[e->row].scale[0] || vlen(d) < 1e-6f) continue;
        e->vel = vadd(e->vel, vmul(vnorm(d), 0.1f));
    }
}
/* the Erase BRK's C1 alpha (body_m_v) at frames 0..9 (setBrkFrameAndStop 9 x (1 - _F4)), Appear's every 4 frames */
static const float ts_erase[10] = {1.0f, 0.9277f, 0.8264f, 0.7041f, 0.5694f, 0.4306f, 0.2959f, 0.1736f, 0.0723f, 0.0f};
static const float ts_appear_brk[13] = {0.0f, 0.047f, 0.12f, 0.214f, 0.322f, 0.44f, 0.56f, 0.678f, 0.786f, 0.88f, 0.953f, 1.0f, 1.0f};
static float ts_alpha(const Kuribo *k)
{
    if (ts_n(k, TS_APPEARWALL) || ts_n(k, TS_APPEARGROUND)) { /* AppearWall / AppearGround start the Appear BRK */
        float f = fminf(fmaxf((float)k->step, 0.0f), 48.0f) / 4.0f;
        int i = (int)f;
        return i >= 12 ? 1.0f : ts_appear_brk[i] + (ts_appear_brk[i + 1] - ts_appear_brk[i]) * (f - i);
    }
    float f = 9.0f * (1.0f - fminf(fmaxf(k->from, 0.0f), 1.0f));
    int i = (int)f;
    return i >= 9 ? 0.0f : ts_erase[i] + (ts_erase[i + 1] - ts_erase[i]) * (f - i);
}
/* calcAndSetBaseMtx (_D4 x scale: m holds the scale), its transparency, its DummyDisplayModel at SklRoot */
static void ts_draw(Kuribo *k, const Model *md, float *m, Anim *a)
{
    for (int j = 0; j < 3; j++) { m[j] *= k->to; m[8 + j] *= 1.0f / k->to; }
    float al = ts_alpha(k);
    if (al < 0.999f) {
        const Row *r = &sc->objs[k->row];
        static const char *const dn[15] = {"Coin", NULL, NULL, NULL, NULL, NULL, "KeySwitchOwned", "PowerStar"};
        static const float dy[15] = {70.0f};
        const Model *dm = r->arg[7] >= 0 && r->arg[7] < 15 && dn[r->arg[7]] ? scene_model(sc, dn[r->arg[7]]) : NULL;
        if (dm && a->skin && a->skin->njoints) { /* mDisplayModel->appear: at SklRoot (its info offset) */
            float dmm[16];
            mat_mul(dmm, m, a->world[0]);
            for (int j = 0; j < 3; j++) dmm[12 + j] += dmm[4 + j] * dy[r->arg[7]];
            if (dm->skin) {
                Anim *da = obj_anim((int)sc->nobjs * 4 + k->row, dm, dn[r->arg[7]]);
                if (da) { anim_update(da); draw_skin(dm, dmm, da); }
            } else render_model(dm, dmm, 0x80, 0x80, 0x80);
        }
        if (al <= 0.001f) { anim_update(a); return; }
        u8 sb[48], sa[48];
        u32 n = md->vu_ngroups < 48 ? md->vu_ngroups : 48;
        for (u32 g = 0; g < n; g++) { /* a constant alpha (FIX) on the groups whose texture has none */
            VuGroup *vg = &((Model *)md)->vu[g];
            sb[g] = vg->blend; sa[g] = vg->alpha;
            if (vg->blend > 1) continue;
            vg->alpha = (u8)((vg->blend ? vg->alpha : 255) * al);
            vg->blend = 1;
        }
        draw_skin(md, m, a);
        for (u32 g = 0; g < n; g++) { VuGroup *vg = &((Model *)md)->vu[g]; vg->blend = sb[g]; vg->alpha = sa[g]; }
        return;
    }
    draw_skin(md, m, a);
}
#ifdef TERESA_TEST
static int ts_test_i = -1; /* the Teresa the test watches: the first one out over ground (its zone has pits) */
static int ts_test_ground(const Kuribo *e, V3 *g)
{
    return kcl_line(e->pos, vadd(e->pos, vmul(vnorm(e->grav), 1500.0f)), g, NULL, NULL);
}
/* Mario on the ground under the watched Teresa's side: how 0 200 off facing it, 1 200 off his back to it, 2 140 off
 * spinning; its row, -1 none */
int game_teresa_test_place(int how)
{
    V3 g;
    if (ts_test_i < 0)
        for (int i = 0; i < nkuribo && ts_test_i < 0; i++) {
            const Kuribo *e = &kuribo[i];
            if (e->kind == WALK_TERESA && e->state != KURIBO_DEAD && row_shown(e->row) && e->front.x + e->front.y + e->front.z != 0.0f &&
                ts_test_ground(e, &g)) ts_test_i = i;
        }
    if (ts_test_i < 0) return -1;
    Kuribo *e = &kuribo[ts_test_i];
    if (!ts_test_ground(e, &g)) return -1;
    V3 up = vmul(vnorm(e->grav), -1.0f), side = vnorm(vtangent(v3(1, 0, 0), up));
    if (vlen(side) < 0.5f) side = vnorm(vtangent(v3(0, 0, 1), up));
    mario->up = up; mario->pos = vadd(g, vmul(side, how == 2 ? 140.0f : 200.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
    mario->face = how == 0 ? vmul(side, -1.0f) : side;
    if (how == 2) { mario->spin_timer = SPIN_TIME; mario->spin_t = 0; }
    return e->row;
}
void game_teresa_state(char *out, int n) /* the watched Teresa: its nerve, step, transparency */
{
    if (ts_test_i < 0) { snprintf(out, n, "none"); return; }
    const Kuribo *e = &kuribo[ts_test_i];
    snprintf(out, n, "row %d nrv %d step %d alpha %.2f dist %d hidden %d dead %d shay %d", e->row, e->nrv, e->step, e->from,
             (int)vlen(vsub(mario->pos, e->pos)), e->phase, e->state == KURIBO_DEAD, ts_shay(e));
}
#endif
