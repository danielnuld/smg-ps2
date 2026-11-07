/* WaterBazooka.cpp, WaterBazookaCapsule.cpp, MogucchiShooter.cpp and ElectricPressureBullet.cpp after Petari: the
 * Mogucchi's cannon (WaterBazooka: OceanPhantomCaveGalaxy 2, ReverseKingdomGalaxy 3; ElectricBazooka: FactoryGalaxy 3).
 * #included by game.c (wz_*). One a scene (every placement has one).
 * Its Cannon1 joint is mBaseMtx (createJointDelegatorWithNullChildFunc: the joint and its children Cannon2 / 3, Cockpit,
 * Top follow it), turned 1.2 degrees a frame to 100 over Mario (aimAtMario: at most 15 below the horizontal, never
 * above). Wait (WaitForBattle until its SW_A) -> Aim (ShotStart, 60 at most or aimed within 2) -> AimEnd (SwingStop)
 * -> Shot (Shot, the bullet at 30: a WaterPressureBullet at 40 from Cannon1 + 550 along its X, no gravity, sand on,
 * spin kill on, the placement's actor camera; ElectricBazooka: one of its 5 ElectricPressureBullets at 12) -> after
 * three Tire (120); at life 1 (not electric) ShotNoMotion (ShortShot, aiming, every 30, five then Tire). The Mogucchi
 * laughing (Mario in a bullet): WaitForLaugh. Mario on its cannon or capsule: Panic (aiming; at 180 the shooter's
 * SpinAttackStart and SE_EV_MOGUBAZ_PRE_STORM, at 300 Storm: the cannon spun about gravity, 25 degrees a frame eased
 * out to 0 over 150, its Spin effect to 90, flipping Mario off on a touch) back to Wait once he is on the ground off it.
 * A hip drop on the capsule (or a JetTurtle on the cannon / capsule) outside a demo: life 3 -> カプセル破壊 (1stDamage,
 * the capsule's Crack, DamageSmokeS1, at the demo's end 8 star pieces 600 up), 2 -> 乗組員怒り (2ndDamage, Angry,
 * DamageSmokeS2 / L1, 16 star pieces; ElectricBazooka: AngrySign / Angry, its electric leak cycle starts), 1 -> 完全破壊
 * (the capsule's Break, DamageSmokeL2: BreakWait 5, BreakSign (Down, SE_EM_LV_WATERBAZ_PRE_EXPLODE, from 120 the
 * flash), BreakExplosion (hidden, collisions off, the shooter's Down, the break model's Break, SE_EM_WATERBAZ_EXPLODE /
 * SE_EV_MOGUBAZ_DEAD; at 53 its power star from Cockpit (arg0) and startAfterBossBGM, else SE_SY_READ_RIDDLE_S),
 * BreakEnd: at 60 its dead switch, kill). Mario on it at a damage is flown (forceFlyPlayer: MarioWarp's point warp)
 * to the nearest 落下点1-4, else 800 from it. The demos are its sheet's last three parts (カプセル破壊, 乗組員怒り,
 * 完全破壊 end every sheet that casts it). ElectricBazooka at life 1: every 600 frames 300-420 AngrySign, 420-600
 * Angry (touching it shocks Mario; a hip drop on the capsule then only lands while he is hurt and not paralysed).
 * The demo camera's target (バズーカカメラターゲット, a CameraTargetDemoActor) is dx_tgt: at it, its up, the shooter's front.
 * The pinch BGM in OceanPhantomCaveGalaxy within 5000.
 * ponytail: no BRK colours (Damage /
 * PowerUp / Spin, the shooter's Normal / CoolDown, the break model's Break: baked), no center screen blur, the storm's
 * level SE without its volume parameter, no hit mark between sensors, no coconut hits (ATYPE_COCO_NUT), no frustum
 * hide (isJudgedToClipFrustum: the draw culls), the capsule's Crack effect validity check (it only matters inside
 * the demo, when every message is refused anyway), no shadows, no star pointer pop of electric bullets */
enum { WZ_WAITFORBATTLE, WZ_WAIT, WZ_AIM, WZ_AIMEND, WZ_SHOT, WZ_SHOTNOMOTION, WZ_TIRE, WZ_DEMOCRACK, WZ_DEMOANGER,
       WZ_DEMOBREAKWAIT, WZ_DEMOBREAKSIGN, WZ_DEMOBREAKEXPLOSION, WZ_DEMOBREAKEND, WZ_WAITFORLAUGH, WZ_PANIC, WZ_STORM,
       WZ_DEAD };
enum { MS_WAIT, MS_SHOT, MS_TIRE, MS_DEATHPANIC, MS_EXPLOSION, MS_LAUGH, MS_SHOCK, MS_PANIC, MS_HITSHOCK, MS_HITPANIC,
       MS_ANGER, MS_STORMSTART, MS_STORM, MS_DEAD };
enum { WC_NORMAL, WC_CRACK, WC_BREAK, WC_DEAD };
enum { WZJ_CANNON1 = 2, WZJ_COCKPIT = 5, WZJ_TOP = 6 }; /* AllRoot, JointRoot, Cannon1, Cannon2, Cannon3, Cockpit, Top */
#define WZ_EBULLETS 5
static struct {
    int row, done, nrv, st, elec, life, shots, star, pinch, demo, etime, kc, kp, hidden, ms_nrv, ms_st, wc_nrv, wc_st, break_on;
    int smoke[4], spin, angry, angry_sign;
    float base[16], cannon[16], shm[16], capm[16];
    V3 pos, grav, front; /* mPosition, mGravity, the shooter's mFront */
    Anim *anim, *sanim, *canim, *banim, *eanim[WZ_EBULLETS];
    const Model *md, *mshooter, *mcap, *mbreak, *mebullet;
    const unsigned char *kcl_body, *kcl_cannon, *kcl_cap;
    int cap_off, posed; /* the capsule's collision invalidated; mBaseMtx taken from its posed joint */
    char cls[24], scls[24], bcls[24];
    struct { int on, st; V3 pos, vel, dir; u32 lv; } eb[WZ_EBULLETS];
    u32 lv[10];
} wz = {.row = -1};

static void wz_set(int n) { wz.nrv = n; wz.st = -1; printf("water bazooka: nerve %d (life %d)\n", n, wz.life); }
static void ms_set(int n) { wz.ms_nrv = n; wz.ms_st = -1; }
static int wz_in(int a, int b) { return wz.nrv >= a && wz.nrv <= b; }
static int wz_nrv_demo(void) { return wz_in(WZ_DEMOCRACK, WZ_DEMOBREAKEND); } /* isNrvDemo */
static int wz_pinch(void) { return wz.life <= 1; }                            /* isBazookaPinch */
static int wz_panic(void) { return wz.nrv == WZ_PANIC || wz.nrv == WZ_STORM; } /* isPanic */
static int wz_leak_sign(void) { return wz.elec && wz.etime >= 300 && wz.etime < 420; } /* isElectricLeakSign */
static int wz_leak(void) { return wz.elec && wz.etime >= 420 && wz.etime < 600; }      /* isElectricLeak */
static int wz_demo_on(void) /* isDemoActiveRegistered: its demo running or asked for */
{
    if (wz.demo < 0) return 0;
    if ((dx.on && dx.cur == wz.demo) || dx.req == wz.demo) return 1;
    for (int i = 0; i < dx_nreq; i++) if (dx_reqq[i].k == wz.demo) return 1;
    return 0;
}
static int wz_any_demo(void) { return dx_busy() || pd.name != NULL; } /* MR::isDemoActive */
static void wz_se(u32 id) { se_start(id, &wz.pos.x); }
static void wz_lv(int k, u32 id) { wz.lv[k] = se_level(id, &wz.pos.x, wz.lv[k]); }
static V3 wz_col(const float *m, int k) { return v3(m[4 * k], m[4 * k + 1], m[4 * k + 2]); }
static void wz_jm(float o[16], int j) /* MR::getJointMtx (the posed joint in the world) */
{
    if (wz.anim && wz.anim->skin && (u32)j < wz.anim->skin->njoints) mat_mul(o, wz.base, wz.anim->world[j]); else memcpy(o, wz.base, 64);
}
static void wz_rigid_inv(float o[16], const float m[16]) /* a rotation + translation's inverse */
{
    mat_identity(o);
    for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) o[4 * c + r] = m[4 * r + c];
    for (int r = 0; r < 3; r++) o[12 + r] = -(o[r] * m[12] + o[4 + r] * m[13] + o[8 + r] * m[14]);
}
static void wz_side_up(float m[16], V3 side, V3 up, V3 pos) /* makeMtxSideUpPos */
{
    V3 x = vnorm(side), z = vnorm(vcross(x, up)), y = vcross(z, x);
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z; m[4] = y.x; m[5] = y.y; m[6] = y.z; m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
}
static float wz_angle(V3 a, V3 b) { float c = vdot(vnorm(a), vnorm(b)); return acosf(c > 1 ? 1 : c < -1 ? -1 : c); }
static void wz_clamp_deg(V3 *v, V3 base, float deg) /* clampVecAngleDeg */
{
    if (wz_angle(*v, base) * 57.29578f <= deg) return;
    V3 ax = vcross(base, *v);
    if (vlen(ax) < 1e-6f) return;
    *v = rot_axis(base, vnorm(ax), deg * 0.017453292f);
}
static void wz_bck(Anim *a, const char *cls, const char *clip, float rate) /* startBck / startAction: "<class>/<clip>" */
{
    char n[48];
    snprintf(n, sizeof n, "%s/%s", cls, clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (a && a->skin) { a->clip_id = -1; if (!anim_start(a, n, 0)) printf("water bazooka: no clip %s\n", n); a->speed = rate; }
}
static int wz_stopped(const Anim *a) { return !a || !a->skin || a->clip_id < 0 || a->done; } /* isBckStopped / isActionEnd */
static void wz_fx(const char *name, const void *key) { fx_named(wz.cls, name, wz.base, wz.anim ? (const float (*)[16])wz.anim->world : NULL, key); }
static int wz_on_player(void) /* isOnPlayer(cannon) || mCapsule->isPlayerOnCapsule() */
{
    return mario_ground >= 0 && (mario_ground == wz.kc || mario_ground == wz.kp);
}

/* --- MogucchiShooter (PartsModel at Cockpit, its own front) --- */
static void ms_reset_dir(void) { float m[16]; wz_jm(m, WZJ_COCKPIT); wz.front = vnorm(wz_col(m, 2)); } /* resetDirection */
static void ms_face(void) /* faceToMario: 1 degree a frame on its gravity's plane, within 30 of the cockpit's front */
{
    if (mario_hidden || in_opening || wz_any_demo()) return;
    float m[16];
    wz_jm(m, WZJ_COCKPIT);
    turn_on_plane(&wz.front, vsub(mario->pos, wz_col(wz.shm, 3)), wz.grav, cosf(1.0f * 0.017453292f));
    wz_clamp_deg(&wz.front, vnorm(wz_col(m, 2)), 30.0f);
}
static void ms_act(const char *clip) { wz_bck(wz.sanim, wz.scls, clip, 1.0f); }
static void ms_update(void)
{
    int s = ++wz.ms_st;
    switch (wz.ms_nrv) {
    case MS_WAIT:
        if (s == 0) { ms_act(wz_pinch() ? "WaitPinch" : "Wait"); break; }
        ms_face();
        if (wb_ride >= 0) { ms_set(MS_LAUGH); break; } /* isPlayerInBind */
        if (wz_panic()) { ms_set(MS_SHOCK); break; }
        if (wz.nrv == WZ_TIRE) { ms_set(MS_TIRE); break; }
        if (wz.nrv == WZ_SHOT && wz.st == 0) ms_set(MS_SHOT); /* isFirstShoot */
        break;
    case MS_SHOT:
        if (s == 0) { ms_act("Shot"); break; }
        if (wb_ride >= 0) { ms_set(MS_LAUGH); break; }
        if (wz_stopped(wz.sanim)) ms_set(MS_WAIT);
        break;
    case MS_TIRE:
        if (s == 0) ms_act("Tire");
        if (wz_panic()) ms_set(MS_SHOCK);
        else if (wz.nrv != WZ_TIRE) ms_set(MS_WAIT);
        break;
    case MS_DEATHPANIC: if (s == 0) ms_act("Panic"); break;
    case MS_EXPLOSION:
        if (s == 0) ms_act("Down");
        if (wz_stopped(wz.sanim)) ms_set(MS_DEAD); /* kill */
        break;
    case MS_LAUGH:
        if (s == 0) ms_act("Laugh");
        if (s == 120) ms_set(MS_WAIT); /* sStepForLaugh */
        break;
    case MS_SHOCK:
        if (s == 0) ms_act("Shock");
        if (wz_stopped(wz.sanim)) ms_set(MS_PANIC);
        break;
    case MS_PANIC:
        if (s == 0) { ms_act("Panic"); if (wz.life <= 0 && wz.sanim) wz.sanim->speed = 2.0f; } /* setBckRate 2 */
        wz_lv(0, SE_EM_LV_WATERBAZ_PANIC);
        ms_face();
        if (!wz_panic() && mario->grounded && wz.life > 0) ms_set(MS_WAIT);
        break;
    case MS_HITSHOCK:
        if (s == 0) ms_act("Shock");
        ms_face();
        if (wz_stopped(wz.sanim)) ms_set(MS_HITPANIC);
        break;
    case MS_HITPANIC:
        if (s == 0) ms_act("Panic");
        wz_lv(0, SE_EM_LV_WATERBAZ_PANIC);
        if (!wz_any_demo()) ms_set(MS_WAIT);
        break;
    case MS_ANGER:
        if (s == 0) ms_act("Angry");
        ms_face(); /* (step 40: startCenterScreenBlur) */
        if (!wz_any_demo()) ms_set(MS_WAIT);
        break;
    case MS_STORMSTART: if (s == 0) ms_act("SpinAttackStart"); ms_face(); break;
    case MS_STORM:
        if (s == 0) ms_act("SpinAttack");
        ms_face();
        if (wz_stopped(wz.sanim)) ms_set(MS_WAIT);
        break;
    }
}

/* --- ElectricPressureBullet --- */
static void eb_kill(int i) /* kill: Break, SE_EM_ELECBUBLLET_BREAK */
{
    float m[16];
    basis_matrix(m, wz.eb[i].pos, up_at(wz.eb[i].pos, v3(0, 1, 0)), wz.eb[i].dir, 0);
    fx_named("ElectricBullet", "Break", m, NULL, NULL);
    se_start(SE_EM_ELECBUBLLET_BREAK, &wz.eb[i].pos.x);
    wz.eb[i].on = 0;
}
static void eb_update(void)
{
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < WZ_EBULLETS; i++) {
        typeof(&wz.eb[0]) o = &wz.eb[i];
        if (!o->on) continue;
        int s = ++o->st;
        if (s == 0 && wz.eanim[i] && wz.eanim[i]->skin) wz_bck(wz.eanim[i], "ElectricBullet", "Shot", 1.0f);
        if (s > 0 && wz.eanim[i] && wz.eanim[i]->skin && wz.eanim[i]->clip_id >= 0 && wz.eanim[i]->done) wz_bck(wz.eanim[i], "ElectricBullet", "Move", 1.0f);
        o->lv = se_level(SE_EM_LV_ELECBUBLLET_FLY, &o->pos.x, o->lv);
        if (s >= 10 && s < 150 && s % 5 == 0) { /* 4 degrees to Mario's centre every 5 steps */
            V3 to = vsub(mc, o->pos);
            if (vlen(to) > 1e-6f) { o->dir = turn_to(o->dir, vnorm(to), cosf(4.0f * 0.017453292f)); o->vel = vmul(o->dir, 12.0f); }
        }
        o->pos = vadd(o->pos, o->vel);
        Hit h[1];
        if (kcl_query(o->pos, 100.0f, h, 1) || s == 360) { eb_kill(i); continue; } /* isBinded (binder 100) / step 360 */
        if (vlen(vsub(mc, o->pos)) < 100.0f + HIT_BODY_R && mario_attack(ATK_ELECTRIC, o->pos)) eb_kill(i); /* attackSensor */
    }
}

/* --- WaterBazooka --- */
static void wz_shot_bullet(void) /* tryShotBullet: calcGunPointFromCannon (Cannon1 + 550 along X, its X the front) */
{
    V3 x = vnorm(wz_col(wz.cannon, 0)), up = vnorm(wz_col(wz.cannon, 1)), p = vadd(wz_col(wz.cannon, 3), vmul(x, 550.0f));
    if (wz.elec) { /* selectBulletElectric, shotElectricBullet(12) */
        for (int i = 0; i < WZ_EBULLETS; i++) {
            if (wz.eb[i].on) continue;
            wz.eb[i].on = 1; wz.eb[i].st = -1; wz.eb[i].pos = p; wz.eb[i].dir = x; wz.eb[i].vel = vmul(x, 12.0f);
            return;
        }
        return;
    }
    for (int b = 0; b < MAX_WBULLETS; b++) { /* WaterPressureBulletHolder::callEmptyBullet, shotWaterBullet(40, true, true, false) */
        if (wbs[b].alive) continue;
        wbs[b] = (typeof(wbs[0])){.alive = 1, .host = wz.row, .grav = 0, .nospin = 0, .sand = 1,
                                  .pos = p, .front = x, .side = vnorm(vcross(up, x)), .vel = vmul(x, 40.0f)};
        wbs[b].step = -1;
        return;
    }
}
static int wz_aim(void) /* aimAtMario */
{
    if (in_opening) return 1;
    V3 side = vnorm(wz_col(wz.cannon, 0)), cp = wz_col(wz.cannon, 3), ug = vmul(wz.grav, -1.0f);
    V3 aim = vnorm(vsub(vadd(mario->pos, vmul(ug, 100.0f)), cp));
    side = turn_to(side, aim, cosf(1.2f * 0.017453292f)); /* turnVecToVecCos */
    V3 v1 = vtangent(side, ug); /* turnVecToPlane */
    v1 = vlen(v1) > 1e-6f ? vmul(vnorm(v1), vlen(side)) : side;
    wz_clamp_deg(&side, v1, 15.0f);
    if (vdot(side, ug) > 0.0f) { V3 t = vtangent(side, ug); if (vlen(t) > 1e-6f) side = vmul(vnorm(t), vlen(side)); }
    wz_side_up(wz.cannon, side, ug, cp);
    return wz_angle(wz_col(wz.cannon, 0), aim) * 57.29578f <= 2.0f;
}
static void wz_near_drop(V3 *out) /* calcNearDropPoint: the nearest of 落下点1-4, else 800 from it toward Mario */
{
    static const char *const pts[] = {"落下点1", "落下点2", "落下点3", "落下点4"};
    float best = 3.4e38f;
    for (int i = 0; i < 4; i++) {
        const float *g = scene_gpos(pts[i]);
        if (g && vlen(vsub(v3(g[0], g[1], g[2]), mario->pos)) < best) { *out = v3(g[0], g[1], g[2]); best = vlen(vsub(*out, mario->pos)); }
    }
    if (best < 3.4e38f) return;
    V3 to = vtangent(vsub(mario->pos, wz.pos), wz.grav);
    to = vlen(to) > 1e-6f ? vnorm(to) : vmul(mario->face, -1.0f);
    *out = vadd(wz.pos, vmul(to, 800.0f));
}
static void wz_jump_back(void) /* tryJumpBackPlayerFromBazooka: forceFlyPlayer */
{
    if (!wz_on_player()) return;
    V3 dp;
    wz_near_drop(&dp);
    V3 rej = vtangent(vsub(dp, mario->pos), wz.grav);
    float d = vlen(vsub(dp, mario->pos));
    m_point_warp(dp, vmul(rej, -1.0f), (int)(d / 20.0f));
}
static void wz_demo_part(int back) /* requestStartDemoRegisteredMarioPuppetable: its sheet's part, `back` from the end */
{
    if (wz.demo >= 0) dx_request_at(wz.demo, demos[wz.demo].h->nparts - back);
}
static void wz_damage(void) /* damageBazooka */
{
    if (wz.life == 3) { /* startDemoCrackCapsule */
        wz_demo_part(3); wz.wc_nrv = WC_CRACK; wz.wc_st = -1; wz.cap_off = 1; wz_set(WZ_DEMOCRACK);
        wz.smoke[0] = 1;
    } else if (wz.life == 2) { /* startDemoAnger */
        wz_demo_part(2); wz.wc_nrv = WC_CRACK; wz.wc_st = -1; wz.cap_off = 1; wz_set(WZ_DEMOANGER);
        wz.smoke[1] = wz.smoke[2] = 1;
    } else if (wz.life == 1) { /* startDemoBreakCapsule */
        wz_demo_part(1); ms_set(MS_DEATHPANIC); wz.wc_nrv = WC_BREAK; wz.wc_st = -1; wz.cap_off = 1; wz_set(WZ_DEMOBREAKWAIT);
        wz.smoke[3] = 1;
    }
    wz.life--;
}
static int wz_hit_ok(void) { return wz.row >= 0 && wz.nrv != WZ_DEAD && !wz_any_demo() && !wz_nrv_demo(); }
static int wz_jet_hit(V3 p) /* receiveMsgPlayerAttack isMsgJetTurtleAttack on its cannon / capsule */
{
    if (!wz_hit_ok()) return 0;
    Hit h[8];
    int n = kcl_query(p, 60.0f, h, 8), hit = 0;
    for (int i = 0; i < n; i++) if (h[i].inst >= 0 && (h[i].inst == wz.kc || h[i].inst == wz.kp)) hit = 1;
    if (!hit) return 0;
    wz_damage();
    return 1;
}
static void wz_leak_update(void) /* updateElectricLeak */
{
    if (!wz.elec || wz.etime < 0 || wz_nrv_demo() || !wz_pinch()) return;
    if (wz_leak_sign() && !wz.angry_sign) { wz.angry_sign = 1; wz.angry = 0; }
    else if (wz_leak() && !wz.angry) { wz.angry = 1; wz.angry_sign = 0; }
    if (wz_leak_sign()) wz_lv(1, SE_EM_LV_ELECBAZ_PRE_ELEC_LEAK);
    else if (wz_leak()) wz_lv(1, SE_EM_LV_ELECBAZ_ELEC_LEAK);
    if (wz.etime < 600) wz.etime++;
    else { wz.etime = 0; wz.angry = 0; }
}
static void wz_touch(void) /* receiveOtherMsg: Mario's floor / wall touch on the cannon or capsule */
{
    if (wz_any_demo() || wz_nrv_demo()) return;
    int on = (mario_ground >= 0 && (mario_ground == wz.kc || mario_ground == wz.kp)) || (mario_wall_inst >= 0 && (mario_wall_inst == wz.kc || mario_wall_inst == wz.kp));
    if (!on) return;
    if (wz_leak() && mario->hip != HIP_FALL && mario->hip != HIP_LAND) { mario_attack(ATK_ELECTRIC, wz.pos); return; }
    if (wz.nrv == WZ_STORM) mario_attack(ATK_FLIP, wz.pos);
}
static void wz_kill(void) /* kill: its dead switch, LiveActor::kill */
{
    const Row *r = &sc->objs[wz.row];
    if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
    row_dead[wz.row] = 1; sw_changed = 1;
    wz_set(WZ_DEAD);
    seq_done_update(wz.done, 1); /* updateAlreadyDoneFlag */
}
static void wz_nerve(void)
{
    int s = ++wz.st;
    switch (wz.nrv) {
    case WZ_WAITFORBATTLE:
        if (s == 0) wz_bck(wz.anim, wz.cls, "Wait", 1.0f);
        { const Row *r = &sc->objs[wz.row]; if (r->sw[SW_A] < 0 || sw_on(r, SW_A) || in_opening) wz_set(WZ_WAIT); }
        break;
    case WZ_WAIT: {
        if (s == 0 && wz_pinch()) { /* startBrk PowerUp (baked) */ }
        if (wz.ms_nrv == MS_LAUGH) { wz_set(WZ_WAITFORLAUGH); break; }
        if (s > 0 && !mario_hidden) { wz_set(WZ_AIM); break; }
        const Row *r = &sc->objs[wz.row]; /* tryWaitForBattle */
        if (!in_opening && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) wz_set(WZ_WAITFORBATTLE);
        break;
    }
    case WZ_AIM:
        if (s == 0) wz_bck(wz.anim, wz.cls, "ShotStart", 1.0f);
        wz_lv(2, SE_EM_LV_WATERBAZ_TURN);
        if (wz.elec && s >= 10) wz_lv(3, SE_EM_LV_ELECBAZ_PRE_SHOOT);
        if (wz_aim() || s >= 60) wz_set(WZ_AIMEND);
        break;
    case WZ_AIMEND:
        if (s == 0) { wz_bck(wz.anim, wz.cls, "SwingStop", 1.0f); wz_se(SE_EM_WATERBAZ_TURN_END); }
        if (wz.elec) wz_lv(3, SE_EM_LV_ELECBAZ_PRE_SHOOT);
        if (wz_stopped(wz.anim)) wz_set(wz_pinch() && !wz.elec ? WZ_SHOTNOMOTION : WZ_SHOT);
        break;
    case WZ_SHOT:
        if (s == 0) { wz_bck(wz.anim, wz.cls, "Shot", 1.0f); wz.shots++; }
        if (wz.elec) wz_lv(3, SE_EM_LV_ELECBAZ_PRE_SHOOT);
        if (s == 30) { wz_se(SE_EM_WATERBAZ_SHOT); wz_shot_bullet(); }
        if (s > 0 && wz_stopped(wz.anim)) {
            if (wz.shots >= 3) { wz.shots = 0; wz_set(WZ_TIRE); break; }
            wz_set(wz_pinch() && !wz.elec ? WZ_SHOTNOMOTION : WZ_WAITFORLAUGH);
        }
        break;
    case WZ_SHOTNOMOTION:
        if (s == 0) { wz.shots++; wz_bck(wz.anim, wz.cls, "ShortShot", 1.0f); wz_se(SE_EM_WATERBAZ_SHOT); wz_shot_bullet(); }
        wz_aim();
        if (wz.ms_nrv == MS_LAUGH) { wz_set(WZ_WAITFORLAUGH); break; }
        if (s == 30) {
            if (wz.shots >= 5) { wz.shots = 0; wz_set(WZ_TIRE); break; }
            wz_set(WZ_SHOTNOMOTION);
        }
        break;
    case WZ_TIRE:
        if (s == 0) wz_bck(wz.anim, wz.cls, "Tire", 1.0f);
        if (wz.ms_nrv == MS_LAUGH) { wz_set(WZ_WAITFORLAUGH); break; }
        if (s == 120) wz_set(WZ_WAIT);
        break;
    case WZ_DEMOCRACK: case WZ_DEMOANGER: {
        int anger = wz.nrv == WZ_DEMOANGER;
        if (s == 0) {
            wz_jump_back();
            wz_bck(wz.anim, wz.cls, anger ? "2ndDamage" : "1stDamage", 1.0f);
            ms_reset_dir(); ms_set(anger ? MS_ANGER : MS_HITSHOCK);
        }
        if (s == (anger ? 20 : 27)) wz_se(anger ? SE_EV_MOGUBAZ_ANGRY2 : SE_EV_MOGUBAZ_ANGRY1);
        if (anger && wz.elec) {
            if (s == 0) wz.angry_sign = 1;
            else if (s == 60) wz.angry_sign = 0;
            else if (s == 90) wz.angry = 1;
            if (s < 60) wz_lv(1, SE_EM_LV_ELECBAZ_PRE_ELEC_LEAK);
            else if (s >= 90) wz_lv(1, SE_EM_LV_ELECBAZ_ELEC_LEAK);
        }
        if (!wz_demo_on()) {
            if (anger && wz.elec) { wz.angry = 0; wz.etime = 0; }
            V3 up = vnorm(wz_col(wz.base, 1)); /* calcUpVec */
            launch_pieces(vadd(wz.pos, vmul(up, 600.0f)), up, anger ? 16 : 8, 25.0f, 40.0f); /* appearStarPiece */
            wz_se(SE_OJ_STAR_PIECE_BURST);
            wz.cap_off = 0; /* validateCollisionParts(mCapsule) */
            wz_set(WZ_WAIT);
        }
        break;
    }
    case WZ_DEMOBREAKWAIT:
        if (s == 0 && wb_ride >= 0) wb_release(wb_ride, vadd(mario->hvel, vmul(mario->up, mario->vup)), 1); /* endBindAndPlayerJumpWithRollLanding */
        if (s == 5) wz_set(WZ_DEMOBREAKSIGN);
        break;
    case WZ_DEMOBREAKSIGN:
        if (s == 0) { wz_bck(wz.anim, wz.cls, "Down", 1.0f); wz_jump_back(); if (wz.elec) wz.angry = wz.angry_sign = 0; }
        pad_rumble(RUMBLE_WEAK);
        wz_lv(4, SE_EM_LV_WATERBAZ_PRE_EXPLODE);
        if (s >= 120) wz_lv(5, SE_EM_LV_WATERBAZ_BREAK_FLASH);
        if (s > 0 && wz_stopped(wz.anim)) wz_set(WZ_DEMOBREAKEXPLOSION);
        break;
    case WZ_DEMOBREAKEXPLOSION:
        if (s == 0) {
            wz.hidden = 1; /* hideModel, invalidateCollisionParts (both), sensors off */
            for (int k = 0; k < 4; k++) wz.smoke[k] = 0;
            ms_reset_dir(); ms_set(MS_EXPLOSION);
            wz.wc_nrv = WC_DEAD; /* mCapsule->kill */
            wz.break_on = 1; wz_bck(wz.banim, wz.bcls, "Break", 1.0f);
            pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL);
            wz_se(SE_EM_WATERBAZ_EXPLODE); wz_se(SE_EV_MOGUBAZ_DEAD);
        }
        if (s == 53) {
            if (wz.star) { /* requestAppearPowerStar at Cockpit */
                float m[16];
                wz_jm(m, WZJ_COCKPIT);
                bgm_after_boss();
                note_star_row = wz.row; note_star_at = wz_col(m, 3); row_dead[wz.row] = 1; sw_changed = 1;
                printf("water bazooka: its power star\n");
            } else se_start(SE_SY_READ_RIDDLE_S, NULL);
        }
        if (s > 0 && wz_stopped(wz.banim)) { wz.break_on = 0; wz_set(WZ_DEMOBREAKEND); }
        break;
    case WZ_DEMOBREAKEND: if (s == 60) wz_kill(); break;
    case WZ_WAITFORLAUGH:
        if (wz.ms_nrv == MS_LAUGH) wz.shots = 0; else wz_set(WZ_WAIT);
        break;
    case WZ_PANIC:
        if (s == 0) { wz_bck(wz.anim, wz.cls, "Wait", 1.0f); wz.shots = 0; }
        wz_aim();
        if (!wz_on_player() && mario->grounded) { wz_set(WZ_WAIT); break; }
        if (s == 180) { ms_set(MS_STORMSTART); wz_se(SE_EV_MOGUBAZ_PRE_STORM); }
        if (s >= 180) wz_lv(6, SE_EM_LV_WATERBAZ_PRE_STORM);
        if (s == 300) wz_set(WZ_STORM);
        break;
    case WZ_STORM: {
        if (s == 0) { ms_set(MS_STORM); wz.spin = 1; }
        V3 pos = wz_col(wz.cannon, 3), side = wz_col(wz.cannon, 0);
        float t = s >= 150 ? 1.0f : s / 150.0f, deg = 25.0f + (0.0f - 25.0f) * sinf(t * 1.5707964f); /* calcNerveEaseOutValue(150, 25, 0) */
        side = rot_axis(side, wz.grav, deg * 0.017453292f);
        wz_side_up(wz.cannon, side, vmul(wz.grav, -1.0f), pos);
        wz_lv(7, SE_EM_LV_WATERBAZ_STORM); /* ponytail: its level parameter deg x 100 / 25 */
        if (s == 90) wz.spin = 0;
        if (s == 150) { wz.spin = 0; wz_set(WZ_WAIT); } /* endStorm */
        break;
    }
    }
}
static void wz_pose(void) /* the pose, then calcJointCannon: Cannon1 = mBaseMtx, its children carried with it */
{
    if (!wz.anim || !wz.anim->skin || wz.anim->skin->njoints <= WZJ_TOP) return;
    anim_update(wz.anim);
    float ib[16], want[16], iold[16], d[16], t[16];
    wz_rigid_inv(ib, wz.base);
    mat_mul(want, ib, wz.cannon);
    wz_rigid_inv(iold, wz.anim->world[WZJ_CANNON1]);
    mat_mul(d, want, iold);
    memcpy(wz.anim->world[WZJ_CANNON1], want, 64);
    for (int j = WZJ_CANNON1 + 1; j <= WZJ_TOP; j++) { mat_mul(t, d, wz.anim->world[j]); memcpy(wz.anim->world[j], t, 64); }
    wz.anim->skin_stale = 1;
}
static void wz_parts(void) /* the parts' matrices: the shooter (Cockpit, its front), the capsule (Top), the collisions */
{
    float m[16];
    wz_jm(m, WZJ_COCKPIT);
    basis_matrix(wz.shm, wz_col(m, 3), vnorm(wz_col(m, 1)), wz.front, 0); /* makeMtxUpFrontPos */
    wz_jm(wz.capm, WZJ_TOP);
    if (wz.kc >= 0 && wz.kc < ninsts) { memcpy(insts[wz.kc].m, wz.cannon, 64); insts[wz.kc].kcl = wz.hidden ? NULL : wz.kcl_cannon; }
    if (wz.kp >= 0 && wz.kp < ninsts) { memcpy(insts[wz.kp].m, wz.capm, 64); insts[wz.kp].kcl = wz.cap_off ? NULL : wz.kcl_cap; }
}
static void update_wz(void)
{
    if (wz.row < 0) return;
    if (wz.demo == -2) { /* tryRegisterDemoCast */
        wz.demo = -1;
        for (int d = 0; d < ndemos; d++) for (int c = 0; c < demos[d].h->ngroup; c++) if (demos[d].group[c] == wz.row) wz.demo = d;
        printf("water bazooka: demo %d\n", wz.demo);
    }
    if (wz.nrv == WZ_DEAD) return;
    eb_update();
    if (scene_stop > 0) return;
    /* receiveMsgPlayerAttack: a hip drop on the capsule (its body sensor ignored) */
    if (hip_hit && hip_inst >= 0 && hip_inst == wz.kp && wz_hit_ok() && wz.wc_nrv != WC_BREAK &&
        !(wz_leak() && (!dm_damaging() || dmg.st == DMS_PARALYZE))) wz_damage();
    wz_touch();
    wz_nerve();
    if (wz.nrv == WZ_DEAD) return;
    /* control */
    { /* setCameraTargetMtx: at mPosition, its up, the shooter's front */
        V3 up = vnorm(wz_col(wz.base, 1)), f = vtangent(vnorm(wz_col(wz.shm, 2)), up);
        dx_tgt.on = 1; dx_tgt.pos = wz.pos; dx_tgt.up = up; dx_tgt.front = vlen(f) > 1e-6f ? vnorm(f) : vnorm(wz_col(wz.base, 2));
    }
    if (!in_opening && !strncmp(game_stage_name, "OceanPhantomCaveGalaxy", 22) && vlen(vsub(mario->pos, wz.pos)) < 5000.0f) { bgm_pinch(wz.pinch); wz.pinch = 1; }
    wz_leak_update();
    if (wz.life >= 1 && wz.life <= 4) wz_lv(8, SE_EM_LV_WATERBAZ_SMOKE); /* getSmokeLevel != -1 */
    if (!wz_nrv_demo() && !wz_panic() && wz_on_player()) wz_set(WZ_PANIC); /* tryPanic */
    wz_pose();
    if (wz.ms_nrv != MS_DEAD) ms_update();
    if (wz.wc_nrv == WC_CRACK || wz.wc_nrv == WC_BREAK) { /* the capsule's Crack / Break */
        if (++wz.wc_st == 0) {
            float cm[16];
            memcpy(cm, wz.capm, 64);
            scene_stop = 12; /* stopScene(12) */
            fx_named("WaterBazookaCapsule", wz.wc_nrv == WC_CRACK ? "Crack" : "Break", cm, NULL, NULL);
            se_start(wz.wc_nrv == WC_CRACK ? SE_EM_WATERBAZ_CAPSULE_DAMAGE : SE_EM_WATERBAZ_CAPSULE_BREAK, &cm[12]);
        }
    }
    wz_parts();
    if (wz.sanim && wz.sanim->skin && wz.ms_nrv != MS_DEAD) anim_update(wz.sanim);
    if (wz.canim && wz.canim->skin) anim_update(wz.canim);
    if (wz.break_on && wz.banim && wz.banim->skin) anim_update(wz.banim);
    if (!wz.hidden) {
        static const char *const smk[4] = {"DamageSmokeS1", "DamageSmokeS2", "DamageSmokeL1", "DamageSmokeL2"};
        for (int k = 0; k < 4; k++) if (wz.smoke[k]) wz_fx(smk[k], &wz.smoke[k]);
        if (wz.spin) wz_fx("Spin", &wz.spin);
        if (wz.angry) wz_fx("Angry", &wz.angry);
        if (wz.angry_sign) wz_fx("AngrySign", &wz.angry_sign);
    }
}
static void wz_draw(void)
{
    if (wz.row < 0 || wz.nrv == WZ_DEAD) return;
    if (!wz.hidden && wz.md && wz.anim && wz.anim->skin) draw_skin(wz.md, wz.base, wz.anim);
    if (wz.ms_nrv != MS_DEAD && wz.mshooter && wz.sanim && wz.sanim->skin) draw_skin(wz.mshooter, wz.shm, wz.sanim);
    if (wz.wc_nrv != WC_DEAD && wz.mcap && wz.canim && wz.canim->skin) { /* its Normal / Crack / Break BVA */
        static const char *const bva[3] = {"waterbazookacapsule/normal", "waterbazookacapsule/crack", "waterbazookacapsule/break"};
        bva_key = bva[wz.wc_nrv]; bva_frame = wz.wc_nrv == WC_NORMAL ? 0.0f : (float)(wz.wc_st < 0 ? 0 : wz.wc_st);
        draw_skin(wz.mcap, wz.capm, wz.canim);
    }
    if (wz.break_on && wz.mbreak && wz.banim && wz.banim->skin) draw_skin(wz.mbreak, wz.base, wz.banim);
    for (int i = 0; wz.mebullet && i < WZ_EBULLETS; i++) {
        if (!wz.eb[i].on || !wz.eanim[i] || !wz.eanim[i]->skin) continue;
        float m[16];
        basis_matrix(m, wz.eb[i].pos, up_at(wz.eb[i].pos, v3(0, 1, 0)), wz.eb[i].dir, 0);
        anim_update(wz.eanim[i]);
        draw_skin(wz.mebullet, m, wz.eanim[i]);
    }
}
static Anim *wz_anim_for(const Model *md)
{
    Anim *a = mem_alloc(MEM_PLAYER, sizeof(Anim));
    memset(a, 0, sizeof(Anim));
    if (md && md->skin) anim_bind(a, md->skin);
    return a;
}
static void wz_models(const Scene *s) /* (game_rebind_: the models looked up again, the anims bound to their skins) */
{
    if (wz.row < 0) return;
    const Model *mm[5] = {scene_model(s, wz.cls), scene_model(s, "MogucchiShooter"), scene_model(s, "WaterBazookaCapsule"), scene_model(s, wz.bcls), scene_model(s, "ElectricBullet")};
    Anim *an[4] = {wz.anim, wz.sanim, wz.canim, wz.banim};
    for (int k = 0; k < 4; k++) if (an[k] && mm[k] && mm[k]->skin && an[k]->skin != mm[k]->skin) anim_bind(an[k], mm[k]->skin);
    for (int i = 0; i < WZ_EBULLETS; i++) if (wz.eanim[i] && mm[4] && mm[4]->skin && wz.eanim[i]->skin != mm[4]->skin) anim_bind(wz.eanim[i], mm[4]->skin);
    wz.md = mm[0]; wz.mshooter = mm[1]; wz.mcap = mm[2]; wz.mbreak = mm[3]; wz.mebullet = wz.elec ? mm[4] : NULL;
    wz.kcl_cannon = scene_kcl(s, wz.elec ? "ElectricBazooka#Cannon1" : "WaterBazooka#Cannon1");
    wz.kcl_cap = scene_kcl(s, "WaterBazookaCapsule");
    if (!wz.posed && wz.anim->skin) { /* initJointTransform: mBaseMtx = Cannon1 as bound (once its zone brings its model) */
        wz.posed = 1;
        anim_update(wz.anim);
        wz_jm(wz.cannon, WZJ_CANNON1);
        ms_reset_dir(); /* calcFrontVec */
        wz_parts();
        printf("water bazooka: models %d %d %d %d, kcl cannon %d capsule %d\n", !!wz.md, !!wz.mshooter, !!wz.mcap, !!wz.mbreak, !!wz.kcl_cannon, !!wz.kcl_cap);
    }
}
static void wz_init(const Scene *s) /* WaterBazooka::init */
{
    memset(&wz, 0, sizeof wz); wz.row = wz.kc = wz.kp = -1;
    for (u32 i = 0; i < s->nobjs; i++) {
        const char *c = s->types + 32 * s->objs[i].type;
        if (strcmp(c, "WaterBazooka") && strcmp(c, "ElectricBazooka")) continue;
        const Row *r = &s->objs[i];
        float one[3] = {1, 1, 1};
        wz.row = (int)i; wz.elec = !strcmp(c, "ElectricBazooka"); wz.life = 3; wz.etime = -1; wz.demo = -2;
        snprintf(wz.cls, sizeof wz.cls, "%s", c); snprintf(wz.scls, sizeof wz.scls, "MogucchiShooter");
        snprintf(wz.bcls, sizeof wz.bcls, "%s", wz.elec ? "ElectricBazookaBreak" : "WaterBazookaBreak");
        wz.star = r->arg[0] != -1; /* getJMapInfoArg0NoInit(bool): declarePowerStar */
        wz.pos = row_pos(r); wz.grav = v3(0, -1, 0); /* (no calcGravity: the default) */
        mat_translate_rot_scale(wz.base, r->pos, r->rot, one);
        wz.anim = wz_anim_for(NULL); wz.sanim = wz_anim_for(NULL); wz.canim = wz_anim_for(NULL); wz.banim = wz_anim_for(NULL);
        for (int k = 0; k < WZ_EBULLETS; k++) wz.eanim[k] = wz_anim_for(NULL);
        wz_jm(wz.cannon, WZJ_CANNON1);
        wz_models(s);
        wz.ms_nrv = MS_WAIT; wz.ms_st = -1; wz.wc_nrv = WC_NORMAL;
        wz_set(WZ_WAIT);
        wz.done = -1;
        if (!strncmp(game_stage_name, "OceanPhantomCaveGalaxy/", 23)) { /* setupAlreadyDoneFlag ウォータバズーカ撃破: beaten
                                                                       * before a miss, dead from the start */
            int done;
            wz.done = seq_done_setup("ウォータバズーカ撃破", r->zone, r->l_id, &done);
            if (done) { if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1); row_dead[wz.row] = 1; wz.nrv = WZ_DEAD; }
        }
        printf("water bazooka: row %d %s, star %d, done %d\n", wz.row, c, wz.star, row_dead[wz.row]);
        break;
    }
}
static void wz_rebind_place(Inst *in) /* game_rebind_: its own instance (AllRoot's collision at its base) not drawn */
{
    int i = (int)(in - insts);
    if (wz.row < 0 || in->row != wz.row || in->kparent >= 0 || i == wz.kc || i == wz.kp) return;
    if (in->kcl) wz.kcl_body = in->kcl;
    in->invisible = 1;
    in->kcl = wz.hidden ? NULL : wz.kcl_body;
}
static void wz_rebind_insts(const Scene *s) /* its Cannon1 and capsule collisions: two invisible instances it moves */
{
    wz.kc = wz.kp = -1;
    if (wz.row < 0 || wz.nrv == WZ_DEAD || row_dead[wz.row]) return;
    const Row *r = &s->objs[wz.row];
    for (int k = 0; k < 2 && ninsts < insts_cap; k++) {
        const Model *md = k ? wz.mcap : wz.md;
        if (!md) continue;
        Inst *in = &insts[ninsts];
        memset(in, 0, sizeof *in);
        in->md = md; in->row = wz.row; in->zone = r->zone; in->invisible = 1; in->kmove = in->kparent = -1;
        memcpy(in->m, k ? wz.capm : wz.cannon, 64);
        in->s = 1.0f; in->reach = md->radius * 2.0f;
        in->kcl = k ? (wz.cap_off ? NULL : wz.kcl_cap) : (wz.hidden ? NULL : wz.kcl_cannon);
        zone_grow(in);
        if (k) wz.kp = ninsts; else wz.kc = ninsts;
        ninsts++;
    }
}
int game_test_wz(int frame) /* XF=-DWZ_TEST (ReverseKingdomGalaxy/3, OceanPhantomCaveGalaxy/2, FactoryGalaxy/3): Mario
                             * by it (its SW_A on after 300 if nothing wrote it), A through talks; after 1500 a hip
                             * drop on the capsule whenever it can take one; at its death, its star */
{
    if (wz.row < 0) return -1;
    const Row *r = &sc->objs[wz.row];
    if (frame % 60 == 0)
        printf("wz test f %d: nrv %d st %d life %d ms %d wc %d, Mario %.0f %.0f %.0f ground %d (kc %d kp %d), ride %d, dx %d part %d, hurt %d life %d\n", frame, wz.nrv, wz.st,
               wz.life, wz.ms_nrv, wz.wc_nrv, mario->pos.x, mario->pos.y, mario->pos.z, mario_ground, wz.kc, wz.kp, wb_ride, dx.on, dx.part, mario->hurt_timer, game_life());
    if (life < 2) life = 3;
    static int hip_at = -1000, done_at = -1, phase;
    if (frame > 300 && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) { sw_set(r, SW_A, 1); printf("wz test: SW_A on\n"); }
    if (talk_active()) { pad_force = frame % 24 < 6 ? PAD_CROSS : 0; return 0; }
    if (wz.nrv == WZ_DEAD) {
        if (done_at < 0) done_at = frame;
#ifdef WZ_MISS /* -DWZ_MISS: a miss 60 after its death; the galaxy entered again (a retry), it stays dead (AlreadyDoneFlag) */
        if (frame - done_at == 60) game_test_miss(0);
        return 0;
#endif
        for (int i = 0; frame - done_at >= 120 && frame % 20 == 0 && i < ninsts; i++)
            if (insts[i].item == ITEM_STAR && star_wait[insts[i].row] == wz.row + 1) { int st; V3 p = star_pos(&insts[i], &st); if (st) { mario->pos = p; mario->hvel = v3(0, 0, 0); mario->vup = 0; } }
        return 100;
    }
    static V3 held;
    if (frame == 700) { /* held 1500 before its cannon at its height till a bullet takes him */
        V3 f = vtangent(vnorm(wz_col(wz.base, 0)), vmul(wz.grav, -1.0f));
        held = vadd(wz_col(wz.cannon, 3), vmul(vnorm(f), 1500.0f));
        printf("wz test: Mario held before the cannon\n");
    }
    if (frame >= 700 && frame < 1300 && wb_ride < 0 && !pw.on) { mario->pos = held; mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    if (frame >= 700 && frame < 1300 && wb_ride >= 0 && frame % 10 == 0) printf("wz test f %d: in a bullet %d, shooter %d\n", frame, wb_ride, wz.ms_nrv);
    if (frame < 1500 || dx.on || wz_nrv_demo()) { pad_force = 0; phase = 0; return 0; }
    if (phase == 0 && frame - hip_at > 400 && wz.wc_nrv != WC_BREAK && !wz.cap_off && wz.kcl_cap) { /* over the capsule, then a hip drop */
        V3 top = wz_col(wz.capm, 3), up = vnorm(wz_col(wz.capm, 1));
        mario->pos = vadd(top, vmul(up, 500.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        hip_at = frame; phase = 1;
        printf("wz test f %d: over the capsule, life %d\n", frame, wz.life);
    }
    pad_force = phase == 1 && frame - hip_at >= 6 && frame - hip_at < 9 ? PAD_R1 : 0;
    if (phase == 1 && frame - hip_at > 120) phase = 0;
    return 0;
}
