/* gravity_light_road.inc: MapObj/GravityLightRoad.cpp after Petari (#included by game.c; glr_*): the beam of light of
 * KoopaBattleVs3Galaxy (five placed) that draws Mario up its axis. Its length 2500 x scale y under its place along
 * -up. Wait; its body sensor (a prior binder, 250, at the foot of Mario's centre on the beam's line) binds the player
 * inside its cylinder (the model's box: 390 round the axis, its height along it): Fall, Draw. Draw: SE_PM_GRAV_LIGHT_
 * DRAWN_ST, the speed + sAccel 2 a frame along its up (sSpeedMax 20), 2 a frame towards the axis, his matrix's up
 * turned to -up by sBaseInterpoleRate 0.2 a frame; after sForceEndBindTime 1800, or its SW_A on (startDisappear):
 * endBindAndPlayerForceJump(up x 2), Disappear (Disappear.brk's 59 frames, kill).
 * ponytail: the pack carries no joint box (GravityLightRoad.bdl's read offline: x 390, y -2500 .. 0) and one BRK
 * track a model (BRKC): Wait.brk / Disappear.brk are not played, it goes after Disappear.brk's length; the matrix
 * blend is of his up vector, not blendMtxRotate's quaternions */
enum { GLR_WAIT, GLR_DRAW, GLR_DISAPPEAR, GLR_DEAD };
typedef struct { int row, nrv, step; float speed, len; V3 pos, up; } Glr;
static Glr *glrs;
static int nglr, glr_host = -1; /* mHost: the one drawing Mario, -1 none */

static void glr_init(void) /* from game_init (its state: MEM_OBJECTS) */
{
    int t = scene_type(sc, "GravityLightRoad");
    nglr = 0; glr_host = -1; glrs = NULL;
    for (int pass = 0; t >= 0 && pass < 2; pass++) {
        int n = 0;
        for (u32 i = 0; i < sc->nobjs; i++) {
            const Row *r = &sc->objs[i];
            if ((int)r->type != t) continue;
            if (pass) {
                float m[16], one[3] = {1, 1, 1};
                mat_translate_rot_scale(m, r->pos, r->rot, one);
                glrs[n] = (Glr){(int)i, GLR_WAIT, -1, 0.0f, 2500.0f * r->scale[1], row_pos(r), vnorm(v3(m[4], m[5], m[6]))};
            }
            n++;
        }
        if (!pass && n) glrs = mem_alloc(MEM_OBJECTS, n * sizeof(Glr));
        nglr = n;
    }
}
static void glr_disappear(Glr *g) /* startDisappear, endBind */
{
    if (glr_host == g - glrs) {
        mario->up = up_at(mario->pos, mario->up);
        m_force_free_jump(mario, vmul(g->up, 2.0f)); /* endBindAndPlayerForceJump(up x sAccel) */
        glr_host = -1;
    }
    g->nrv = GLR_DISAPPEAR; g->step = -1;
}
static void glr_update(void)
{
    for (int k = 0; k < nglr; k++) {
        Glr *g = &glrs[k];
        const Row *r = &sc->objs[g->row];
        if (g->nrv == GLR_DEAD || !row_shown(g->row)) continue;
        if (g->nrv != GLR_DISAPPEAR && r->sw[SW_A] >= 0 && sw_on(r, SW_A)) glr_disappear(g); /* listenStageSwitchOnA */
        int st = glr_host == k ? g->step : ++g->step; /* (Draw steps with Mario: glr_hold) */
        if (g->nrv == GLR_DISAPPEAR && st >= 59) { g->nrv = GLR_DEAD; row_dead[g->row] = 1; continue; } /* isBrkStopped: kill */
        if (g->nrv != GLR_WAIT || glr_host >= 0 || tz_ride >= 0 || swr_ride || wrd.ride || pole_ride >= 0 || wb_ride >= 0 || mario->launch >= 0) continue;
        /* receiveOtherMsg AutoRushBegin from the player's sensor: tryStartBind */
        float t = -vdot(vsub(mw.c, g->pos), g->up) / g->len;
        V3 sp = vsub(g->pos, vmul(g->up, g->len * (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t))); /* updateHitSensor */
        if (vlen(vsub(mw.c, sp)) >= 250.0f + HIT_BODY_R) continue;
        V3 dir = vsub(mario->pos, g->pos); /* isPlayerInCylinder */
        if (vlen(vtangent(dir, g->up)) > 390.0f || fabsf(vdot(g->up, dir)) > g->len) continue;
        if (!mode_rush("GravityLightRoad")) continue;
        glr_host = k;
        mario->hip = 0; mario->spin_timer = 0; mario->jumping = 0; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        if (mario_anim) mario_play("fall", 1.0f, 1);
        g->nrv = GLR_DRAW; g->step = -1;
#ifdef GLR_TEST
        printf("glr test f %u: road %d (row %d) binds Mario%c", game_frame, k, g->row, 10);
#endif
    }
#ifdef GLR_TEST /* plain build XF=-DGLR_TEST (KoopaBattleVs3Galaxy/1): Mario put in the first road's foot at frame 1000 */
    if (nglr) {
        test_nolost = 1;
        if (game_frame == 1000) {
            mario->pos = vadd(vsub(glrs[0].pos, vmul(glrs[0].up, glrs[0].len - 300.0f)), v3(150.0f, 0.0f, 0.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
            camera_start_pos_end();
        }
        if (game_frame >= 1000 && game_frame % 30 == 0)
            printf("glr test f %u: nerve %d step %d host %d speed %.0f along %.0f of %.0f off axis %.0f sw %d grounded %d%c", game_frame, glrs[0].nrv,
                   glrs[0].step, glr_host, glrs[0].speed, -vdot(vsub(mario->pos, glrs[0].pos), glrs[0].up), glrs[0].len,
                   vlen(vtangent(vsub(mario->pos, glrs[0].pos), glrs[0].up)), sw_on(&sc->objs[glrs[0].row], SW_A), mario->grounded, 10);
    }
#endif
}
/* exeDraw with Mario, then his base matrix (updateBindActorMtx) */
static int glr_hold(Player *m)
{
    if (glr_host < 0) return 0;
    Glr *g = &glrs[glr_host];
    int st = ++g->step;
    if (st == 0) { g->speed = 0.0f; se_start(SE_PM_GRAV_LIGHT_DRAWN_ST, &m->pos.x); } /* startSoundPlayer */
    g->speed += 2.0f; /* sAccel */
    m->pos = vadd(m->pos, vmul(g->up, g->speed > 20.0f ? 20.0f : g->speed)); /* restrictVelocity(sSpeedMax) */
    V3 f = vtangent(vsub(g->pos, m->pos), g->up);
    if (fabsf(f.x) >= 2.0f || fabsf(f.y) >= 2.0f || fabsf(f.z) >= 2.0f) m->pos = vadd(m->pos, vmul(vnorm(f), 2.0f)); /* isNearZero(fwd, sAccel) */
    if (st == 1800) { glr_disappear(g); return 0; } /* sForceEndBindTime */
    V3 u = vadd(vmul(m->up, 0.8f), vmul(g->up, -0.2f)); /* sBaseInterpoleRate towards makeMtxUpNoSupport(-up) */
    if (vlen(u) > 1e-4f) m->up = vnorm(u);
    V3 ff = vtangent(m->face, m->up);
    if (vlen(ff) > 1e-4f) m->face = vnorm(ff);
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    return 1;
}
