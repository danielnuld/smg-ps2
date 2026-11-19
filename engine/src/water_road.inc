/* water_road.inc: WaterRoad.cpp after Petari (#included by game.c after ocean_ring.inc; wrd_*): the tube of water
 * that grows along its rail in a demo and carries Mario to its end (OceanRingGalaxy: the sheet WaterRoadAppear, its
 * parts 0 ウォーターロード出現, 1 ウォーターロード成長, 2 終了待ち).
 * init: its radius arg1 (sDefaultRadius 500), its up its Y (its X when the rail starts within 0.99 of it), a demo cast:
 * dead until the demo's appear. appear: at the rail's start, SW_A on, DemoStart (DemoGrowUp when that part runs).
 * DemoStart: SE_OJ_LV_WATER_ROAD_APPEAR, SE_AT_LV_EARTHQUAKE until 出現's last step. DemoGrowUp: the Top effect,
 * SE_OJ_WATER_ROAD_GROW_START, a strong shake and rumble; the rail coord eased out over 成長 (updateDemo: its matrix
 * up the rail there), SE_OJ_LV_WATER_ROAD_GROW, SE_AT_LV_EARTHQUAKE; at its last step Top off, End on, DemoWaitEnd
 * (the demo over: showPlayer, Wait). Wait: its Binder sensor (the radius, at the rail's nearest place to Mario, not
 * under 300 above the rail's start) binds him (AutoRushBegin): the coord nearest him, its バインド中 camera on the
 * player, RideStart (WaterRoadIn, SE_OJ_WATER_ROAD_BIND_IN), RideWait (SwimFlutter), RideSpin on a swing (SwimSpin,
 * SE_PM_TORNADE_IN_WATER_ST, SE_PM_LV_TORNADE_IN_WATER, the speed + 50, 30 frames). updateRide: the speed x 0.99
 * within 100 .. 150 along the rail, SE_OJ_LV_WATER_ROAD_BIND, his matrix up the rail there; at the goal
 * SE_OJ_WATER_ROAD_BIND_END, SE_PV_JUMP_M, endBindAndPlayerJump(0), the camera off, Disappear (its alpha to 0 over
 * 30, kill). movement: the texture scroll, SE_AT_LV_WATER_ROAD at the rail's nearest place to the camera.
 * WaterRoadModelInfo: a ring of 12 points at each line point (one where the rail's direction left the last by the
 * model's dot: high 0.998, low 0.99, tested every sPointIntervalLine 100), u 0.05 a loop point, v the coord x 0.0001;
 * the high one within sDistanceToLow 10000 of its box, drawn up to the rail coord while it grows (drawDirect).
 * (WaterRoadNrvWaitInvalid is set by nothing in Petari.)
 * ponytail: the rail is the pack's polyline (the Bezier sampled by tools/slice.py: its direction turns only at its 15
 * points, so both models get those 15 rings, where Petari's rail turns all along it); the line points keep their
 * rail place and axes (40 B), the 12 loop points made at the draw; Water.bti's
 * two stages are one sample (tools/jpa.py fx_ocean_ring_tex, scrolled as stage 0: 0.002, -0.005), the screen term
 * C1 55 96 BE as the frame less 1 - C1 (ocean_ring.inc or_mesh), no indirect warp (WaterIndirect.bti), no EnvMap.bti
 * stage, no R8_EQ stage (C0 28 28 28 where the texture's red equals A0 14); its clipping sphere is the chunks'
 * render_visible; its box is the rail's polyline padded, not MR::calcBoundingBox's rail points */
enum { WRD_DEAD, WRD_WAIT, WRD_RIDESTART, WRD_RIDEWAIT, WRD_RIDESPIN, WRD_DEMOSTART, WRD_DEMOGROW, WRD_DEMOWAITEND, WRD_DISAPPEAR };
typedef struct { V3 pos, side, up; float coord; } WrdLine; /* a line point: the rail's place, the ring's first side and its up */
static struct {
    const Scene *sc;
    int row, nrv, step, demo, ride, n[2]; /* n: the high / low models' line points (mNumLinePoints) */
    WrdLine *line[2];
    RmRail rail;
    float radius, tu, tv;
    int alpha;
    V3 base_up, bfront, pos, rfront, near, bmin, bmax; /* mBaseUp, mBaseMtx's Z, mPosition, mRiderMtx's Z, mCamRailNearestPos, mBoundingBox */
    float bm[16]; /* mBaseMtx: its effects' host */
    u32 lv[6];
} wrd;

static void wrd_set(int n) { wrd.nrv = n; wrd.step = -1; }
static V3 wrd_at(float c, V3 *dir) { return nrail_at(wrd.rail.pts, wrd.rail.np, wrd.rail.cl, c, dir); }
static int wrd_count(float thr) /* calcNumDrawLinePoints */
{
    float len = wrd.rail.len, c = 0.0f;
    int nt = (int)(len / 100.0f) + 1, n = 1; /* sPointIntervalLine */
    float delta = len / (nt - 1);
    V3 d0, d;
    wrd_at(0.0f, &d0);
    for (int i = 1; i < nt; i++) {
        c = fminf(c + delta, len);
        wrd_at(c, &d);
        if (vdot(d, d0) < thr) { n++; d0 = d; }
    }
    return n < 2 ? 2 : n;
}
static void wrd_fill(float thr, WrdLine *l, int n) /* WaterRoadModelInfo::initPoints */
{
    float len = wrd.rail.len, delta = len / (int)(len / 100.0f), c = 0.0f;
    V3 up = wrd.base_up, front, d;
    wrd_at(0.0f, &front);
    for (int i = 0; i < n; i++) {
        if (i == n - 1) c = len; /* moveCoordToEndPos */
        V3 p = wrd_at(c, &d), side = vnorm(vcross(d, up));
        up = vcross(side, d);
        l[i] = (WrdLine){p, side, up, c};
        while (c < len && vdot(d, front) > thr) { c = fminf(c + delta, len); wrd_at(c, &d); }
        front = d;
    }
}
static void wrd_base(float rate) /* updateDemo: the rail coord eased out, its matrix up the rail there */
{
    V3 d;
    wrd.rail.coord = sinf(rate * 3.14159265f / 2.0f) * wrd.rail.len; /* getEaseOutValue */
    wrd.pos = wrd_at(wrd.rail.coord, &d);
    V3 s = vcross(d, wrd.bfront); /* makeMtxUpFront */
    if (vlen(s) > 1e-6f) wrd.bfront = vcross(vnorm(s), d);
    basis_matrix(wrd.bm, wrd.pos, d, wrd.bfront, 0);
}
static void wrd_init(void) /* from game_init (its line points: MEM_OBJECTS) */
{
    wrd.sc = sc; wrd.row = -1; wrd.ride = 0;
    int t = scene_type(sc, "WaterRoad");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type != t || !rr_init(&wrd.rail, r) || wrd.rail.len < 100.0f) continue;
        float m[16], one[3] = {1, 1, 1};
        V3 d;
        mat_translate_rot_scale(m, r->pos, r->rot, one);
        wrd.row = (int)i; wrd.demo = -2; wrd.alpha = 255; wrd.tu = wrd.tv = 0.0f;
        wrd.radius = r->arg[1] != -1 ? (float)r->arg[1] : 500.0f; /* getJMapInfoArg1NoInit, sDefaultRadius */
        wrd.base_up = vnorm(v3(m[4], m[5], m[6])); wrd.bfront = vnorm(v3(m[8], m[9], m[10])); wrd.rfront = v3(0, 0, 1);
        wrd_at(0.0f, &d);
        if (vdot(d, wrd.base_up) >= 0.99f) wrd.base_up = vnorm(v3(m[0], m[1], m[2])); /* sDotUpdateAxis: calcActorAxisX */
        wrd_base(0.0f);
        for (int k = 0; k < 2; k++) {
            float thr = k ? 0.99f : 0.998f; /* sDotPointCreateLow / High */
            wrd.n[k] = wrd_count(thr);
            wrd.line[k] = mem_alloc(MEM_OBJECTS, wrd.n[k] * sizeof(WrdLine));
            wrd_fill(thr, wrd.line[k], wrd.n[k]);
        }
        wrd.bmin = wrd.bmax = v3(wrd.rail.pts[0], wrd.rail.pts[1], wrd.rail.pts[2]);
        for (u32 k = 1; k < wrd.rail.np; k++) {
            const float *p = wrd.rail.pts + 3 * k;
            wrd.bmin = v3(fminf(wrd.bmin.x, p[0]), fminf(wrd.bmin.y, p[1]), fminf(wrd.bmin.z, p[2]));
            wrd.bmax = v3(fmaxf(wrd.bmax.x, p[0]), fmaxf(wrd.bmax.y, p[1]), fmaxf(wrd.bmax.z, p[2]));
        }
        float pad = 100.0f + wrd.radius; /* sClippingRadiusOffset, pad(mRadius) */
        wrd.bmin = vsub(wrd.bmin, v3(pad, pad, pad)); wrd.bmax = vadd(wrd.bmax, v3(pad, pad, pad));
        memset(wrd.lv, 0, sizeof wrd.lv);
        wrd_set(WRD_DEAD); /* tryRegisterDemoCast: makeActorDead */
        printf("water road: row %d, rail %.0f, %d / %d line points%c", wrd.row, wrd.rail.len, wrd.n[0], wrd.n[1], 10);
        break;
    }
}
static int wrd_part(int p) { return dx.on && dx.cur == wrd.demo && dx_active(p); } /* isDemoPartActive */
static void wrd_cast_hook(int type) /* the sheet's appear on it: WaterRoad::appear */
{
    if (type != 0 || wrd.row < 0 || wrd.nrv != WRD_DEAD) return;
    const Row *r = &sc->objs[wrd.row];
    V3 d;
    wrd.pos = wrd_at(0.0f, &d); /* getRailPointPosStart */
    if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
    wrd_set(wrd_part(1) ? WRD_DEMOGROW : WRD_DEMOSTART);
}
static int wrd_ride_update(Player *m) /* updateRide */
{
    V3 d;
    wrd.rail.speed *= 0.99f; /* sRiderFrictionRate */
    wrd.rail.speed = wrd.rail.speed < 100.0f ? 100.0f : wrd.rail.speed > 150.0f ? 150.0f : wrd.rail.speed; /* sRiderSpeedMin / Max */
    wrd.rail.coord = fminf(wrd.rail.coord + wrd.rail.speed, wrd.rail.len);
    wrd.pos = wrd_at(wrd.rail.coord, &d); /* moveCoordAndFollowTrans */
    wrd.lv[0] = se_level(SE_OJ_LV_WATER_ROAD_BIND, &m->pos.x, wrd.lv[0]);
    if (wrd.rail.coord >= wrd.rail.len) { /* isRailReachedGoal */
        se_start(SE_OJ_WATER_ROAD_BIND_END, &m->pos.x); se_start(SE_PV_JUMP_M, &m->pos.x);
        m->up = up_at(m->pos, m->up);
        m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; /* endBindAndPlayerJump(0) */
        camera_event_end(); /* endMultiActorCamera バインド中 */
        wrd.ride = 0;
        wrd_set(WRD_DISAPPEAR);
        return 1;
    }
    return 0;
}
/* while Mario rides: its Ride nerves, then his base matrix (ACTMES_UPDATE_BASEMTX: makeMtxUpFrontPos, up the rail's
 * direction, the front his last one's, at the rail's place) */
static int wrd_hold(Player *m)
{
    if (!wrd.ride) return 0;
    int st = ++wrd.step, swing = pad_pressed(PAD_SQUARE); /* isCorePadSwing */
    switch (wrd.nrv) {
    case WRD_RIDESTART:
        if (st == 0) { if (mario_anim) mario_play("waterroadin", 1.0f, 0); se_start(SE_OJ_WATER_ROAD_BIND_IN, &m->pos.x); }
        if (wrd_ride_update(m)) return 0;
        if (swing) wrd_set(WRD_RIDESPIN);
        else if (!mario_anim || mario_anim->done) wrd_set(WRD_RIDEWAIT);
        break;
    case WRD_RIDEWAIT:
        if (st == 0 && mario_anim) mario_play("swimflutter", 1.0f, 1);
        if (wrd_ride_update(m)) return 0;
        if (swing) wrd_set(WRD_RIDESPIN);
        break;
    case WRD_RIDESPIN:
        if (st == 0) {
            if (mario_anim) mario_play("swimspin", 1.0f, 0);
            se_start(SE_PM_TORNADE_IN_WATER_ST, &m->pos.x);
            wrd.rail.speed += 50.0f; /* sRiderSpinAccel */
        }
        wrd.lv[1] = se_level(SE_PM_LV_TORNADE_IN_WATER, &m->pos.x, wrd.lv[1]);
        if (wrd_ride_update(m)) return 0;
        if (st == 30) wrd_set(WRD_RIDEWAIT); /* sStepRideSpin */
        break;
    }
    V3 d, s;
    m->pos = wrd_at(wrd.rail.coord, &d);
    s = vcross(d, wrd.rfront);
    if (vlen(s) > 1e-6f) wrd.rfront = vcross(vnorm(s), d);
    m->up = d; m->face = wrd.rfront;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    camera_event_target(m->up, m->pos); /* startMultiActorCameraTargetPlayer */
    return 1;
}
#ifdef WATERROAD_TEST
/* plain build XF=-DWATERROAD_TEST (OceanRingGalaxy/1): its demo's switch on at frame 1000; once it waits the camera shows the whole road for 300 frames, then Mario is
 * put in its foot (a swing 60 frames into the ride) */
static void wrd_test(void)
{
    static const char *const nn[] = {"Dead", "Wait", "RideStart", "RideWait", "RideSpin", "DemoStart", "DemoGrowUp", "DemoWaitEnd", "Disappear"};
    static int wait_at = -1, ride_at = -1, was = -1;
    int f = (int)game_frame;
    if (f < 1000 || wrd.row < 0) return;
    test_nolost = 1;
    if (f == 1000 && wrd.demo >= 0) {
        Row r = dx_row(&demos[wrd.demo]);
        sw_set(&r, SW_APPEAR, 1);
        printf("water road test f %d: demo %d's switch on%c", f, wrd.demo, 10);
    }
    if (wrd.nrv != was) { printf("water road test f %d: %s -> %s%c", f, was < 0 ? "-" : nn[was], nn[wrd.nrv], 10); was = wrd.nrv; }
    if (wrd.nrv == WRD_WAIT && wait_at < 0) wait_at = f;
    if (wrd.nrv == WRD_WAIT && wait_at >= 0 && f < wait_at + 300) { /* the whole road from Mario's side */
        V3 mid = wrd.line[0][wrd.n[0] / 2].pos;
        camera_set_override(1, vadd(mario->pos, v3(0.0f, 2500.0f, 0.0f)), mid, v3(0, 1, 0), 60.0f);
    }
    if (wrd.nrv == WRD_WAIT && wait_at >= 0 && f == wait_at + 300) {
        camera_override_end(0);
        V3 d;
        mario->pos = vadd(wrd_at(400.0f, &d), v3(100.0f, 0.0f, 0.0f));
        mario->up = up_at(mario->pos, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        camera_start_pos_end();
        printf("water road test f %d: Mario put in its foot%c", f, 10);
    }
    if (wrd.ride && ride_at < 0) ride_at = f;
    pad_force = wrd.ride && f == ride_at + 60 ? PAD_SQUARE : 0;
    if (f % 30 == 0)
        printf("water road test f %d: %s step %d ride %d coord %.0f / %.0f speed %.0f alpha %d pos (%d %d %d) Mario (%d %d %d) clip %s demo on %d part %d%c", f,
               nn[wrd.nrv], wrd.step, wrd.ride, wrd.rail.coord, wrd.rail.len, wrd.rail.speed, wrd.alpha, (int)wrd.pos.x, (int)wrd.pos.y, (int)wrd.pos.z,
               (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z,
               mario_anim && mario_anim->clip_id >= 0 ? sc->clips[mario_anim->clip_id].name : "-", dx.on, dx.on ? dx.part : -1, 10);
}
#endif
static void wrd_update(void)
{
    if (wrd.sc != sc || wrd.row < 0) return;
    if (wrd.demo == -2) { /* tryRegisterDemoCast (dx_init comes after wrd_init) */
        wrd.demo = -1;
        for (int d = 0; d < ndemos; d++) for (int c = 0; c < demos[d].h->ngroup; c++) if (demos[d].group[c] == wrd.row) wrd.demo = d;
        printf("water road: demo %d%c", wrd.demo, 10);
    }
#ifdef WATERROAD_TEST
    wrd_test();
#endif
    if (wrd.nrv == WRD_DEAD) return;
    int st = wrd.ride ? wrd.step : ++wrd.step; /* (the Ride nerves step with Mario: wrd_hold) */
    switch (wrd.nrv) {
    case WRD_DEMOSTART:
        wrd.lv[2] = se_level(SE_OJ_LV_WATER_ROAD_APPEAR, &wrd.pos.x, wrd.lv[2]);
        wrd.lv[3] = se_level(SE_AT_LV_EARTHQUAKE, NULL, wrd.lv[3]); /* startAtmosphereLevelSE */
        if (dx.on && dx.cur == wrd.demo && dx_last(0)) wrd_set(WRD_DEMOGROW); /* isDemoPartLastStep ウォーターロード出現 */
        break;
    case WRD_DEMOGROW:
        if (st == 0) {
            se_start(SE_OJ_WATER_ROAD_GROW_START, &wrd.pos.x);
            camera_shake(SHAKE_STRONG); /* shakeCameraStrong */
            pad_rumble(RUMBLE_VERY_STRONG); /* tryRumblePadVeryStrong */
        }
        if (wrd_part(1)) wrd_base(dx_total(1) > 0 ? (float)dx_step(1) / (float)dx_total(1) : 0.0f); /* calcDemoPartStepRate ウォーターロード成長 */
        else wrd_base(0.0f);
        {
            int ok = fx_named("WaterRoad", "Top", wrd.bm, NULL, &wrd); /* emitEffect Top (setEffectHostMtx mBaseMtx) */
            (void)ok;
#ifdef WATERROAD_TEST
            if (st == 0) printf("water road test: Top effect %d%c", ok, 10);
#endif
        }
        wrd.lv[4] = se_level(SE_OJ_LV_WATER_ROAD_GROW, &wrd.pos.x, wrd.lv[4]);
        wrd.lv[3] = se_level(SE_AT_LV_EARTHQUAKE, NULL, wrd.lv[3]);
        if (dx.on && dx.cur == wrd.demo) camera_event_target(v3(0, 1, 0), wrd.pos); /* the part's camera: CameraTargetArg(the cast) */
        if (dx.on && dx.cur == wrd.demo && dx_last(1)) wrd_set(WRD_DEMOWAITEND); /* (deleteEffect Top: no longer kept; End below) */
        break;
    case WRD_DEMOWAITEND:
        if (!dx.on) { mario_hidden = 0; wrd_set(WRD_WAIT); } /* isDemoActive; showPlayer */
        break;
    case WRD_DISAPPEAR:
        wrd.alpha = (int)(255.0f * (1.0f - (float)st / 30.0f)); /* calcNerveValue(sDisappearStep, 255, 0) */
        if (st >= 30) { wrd.alpha = 0; wrd_set(WRD_DEAD); row_dead[wrd.row] = 1; return; } /* kill */
        break;
    default: break;
    }
    if (wrd.nrv != WRD_DEMOSTART && wrd.nrv != WRD_DEMOGROW) {
        int ok = fx_named("WaterRoad", "End", wrd.bm, NULL, (const char *)&wrd + 1); /* emitEffect End: until its kill */
        (void)ok;
#ifdef WATERROAD_TEST
        if (wrd.nrv == WRD_DEMOWAITEND && st == 0) printf("water road test: End effect %d%c", ok, 10);
#endif
    }
    /* movement */
    wrd.tu = fmodf(wrd.tu + 0.002f, 1.0f); wrd.tv = fmodf(wrd.tv - 0.005f + 1.0f, 1.0f); /* sTexSpeedU0 / V0 */
    {
        V3 d;
        wrd.near = wrd_at(nrail_nearest(wrd.rail.pts, wrd.rail.np, wrd.rail.cl, cam_pos), &d); /* calcNearestRailPos */
        wrd.lv[5] = se_level(SE_AT_LV_WATER_ROAD, &wrd.near.x, wrd.lv[5]);
    }
    /* receiveOtherMsg AutoRushBegin: its Binder sensor at the rail's nearest place to Mario (updateHitSensor) */
    if (wrd.nrv != WRD_WAIT || tz_ride >= 0 || swr_ride || mario->launch >= 0 || pole_ride >= 0 || wb_ride >= 0) return;
    V3 d, start = wrd_at(0.0f, &d);
    float c = nrail_nearest(wrd.rail.pts, wrd.rail.np, wrd.rail.cl, mario->pos);
    V3 sp = wrd_at(c, &d);
    if (sp.y < start.y + 300.0f) sp.y = start.y + 300.0f; /* sSensorOffsetY */
    if (vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), sp)) >= wrd.radius + HIT_BODY_R || !mode_rush("WaterRoad")) return;
    wrd.rail.coord = c; /* moveCoordToNearestPos */
    wrd.ride = 1;
    mario->hip = 0; mario->spin_timer = 0; mario->jumping = 0; mario->hvel = v3(0, 0, 0); mario->vup = 0;
    if (row_camset[wrd.row] != -1) /* startMultiActorCameraTargetPlayer バインド中 */
        camera_actor_start("WaterRoad", "バインド中", (unsigned short)row_camset[wrd.row], mario->up, mario->face, mario->pos);
    wrd_set(WRD_RIDESTART);
}
static void wrd_ring(int nv, V3 c, V3 side, V3 up, float coord) /* a line point's 12 loop points, the first again for the seam */
{
    static const float cs[13][2] = {{1.0f, 0.0f}, {0.8660254f, 0.5f}, {0.5f, 0.8660254f}, {0.0f, 1.0f}, {-0.5f, 0.8660254f},
                                    {-0.8660254f, 0.5f}, {-1.0f, 0.0f}, {-0.8660254f, -0.5f}, {-0.5f, -0.8660254f}, {0.0f, -1.0f},
                                    {0.5f, -0.8660254f}, {0.8660254f, -0.5f}, {1.0f, 0.0f}};
    for (int j = 0; j < 13; j++) {
        V3 p = vadd(c, vmul(vadd(vmul(side, cs[j][0]), vmul(up, cs[j][1])), wrd.radius));
        or_pos[nv + j][0] = p.x; or_pos[nv + j][1] = p.y; or_pos[nv + j][2] = p.z;
        or_uv[nv + j][0] = 0.05f * j + wrd.tu; or_uv[nv + j][1] = coord * 0.0001f + wrd.tv; /* sTexRateU0, V0 */
        or_al[nv + j] = (u8)wrd.alpha;
    }
}
static void draw_water_road(void)
{
    int tex = fx_ocean_ring_tex();
    if (wrd.sc != sc || wrd.row < 0 || tex < 0 || wrd.nrv == WRD_DEAD || wrd.nrv == WRD_DEMOSTART) return;
    int grow = wrd.nrv == WRD_DEMOGROW || wrd.nrv == WRD_DEMOWAITEND, k = 0;
    if (!grow) { /* the low model past sDistanceToLow of its box */
        V3 q = v3(fminf(fmaxf(mario->pos.x, wrd.bmin.x), wrd.bmax.x), fminf(fmaxf(mario->pos.y, wrd.bmin.y), wrd.bmax.y),
                  fminf(fmaxf(mario->pos.z, wrd.bmin.z), wrd.bmax.z));
        k = vlen(vsub(mario->pos, q)) > 10000.0f;
    }
    const WrdLine *l = wrd.line[k];
    int n = wrd.n[k], tip = 0;
    if (grow) { /* drawDirect: calcDemoDrawPointNum, then a ring at the rail coord between the two line points */
        int pn = n;
        for (int i = 1; i < n; i++) if (wrd.rail.coord < l[i].coord) { pn = i; break; }
        tip = pn < n; n = pn;
    }
    for (int a = 0; a < n + tip - 1; a += 63) { /* 64 rings a mesh (or_tri) */
        int b = a + 64 < n + tip ? a + 64 : n + tip, nv = 0, nt = 0;
        V3 lo = l[a].pos, hi = lo;
        for (int i = a; i < b; i++) {
            V3 p = l[i < n ? i : n].pos;
            if (i < n) wrd_ring(nv, p, l[i].side, l[i].up, l[i].coord);
            else { /* the tip: the points between the rings n - 1 and n */
                float t = (wrd.rail.coord - l[n - 1].coord) / (l[n].coord - l[n - 1].coord);
                p = vadd(vmul(l[n - 1].pos, 1.0f - t), vmul(p, t));
                wrd_ring(nv, p, vadd(vmul(l[n - 1].side, 1.0f - t), vmul(l[n].side, t)),
                         vadd(vmul(l[n - 1].up, 1.0f - t), vmul(l[n].up, t)), wrd.rail.coord);
            }
            lo = v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z)); hi = v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
            if (i > a)
                for (int j = 0; j < 12; j++) {
                    u16 *x = or_tri + 3 * nt, a0 = (u16)(nv - 13 + j), b0 = (u16)(nv + j);
                    x[0] = a0; x[1] = (u16)(a0 + 1); x[2] = b0; x[3] = (u16)(a0 + 1); x[4] = (u16)(b0 + 1); x[5] = b0; nt += 2;
                }
            nv += 13;
        }
        V3 mid = vmul(vadd(lo, hi), 0.5f);
        float cc[3] = {mid.x, mid.y, mid.z};
        if (!render_visible(cc, vlen(vsub(hi, lo)) * 0.5f + 2.0f * wrd.radius)) continue;
        or_mesh(nv, nt, tex, 0x213555u); /* 1 - C1 (55 96 BE) in GS units */
    }
}
