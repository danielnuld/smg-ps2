/* trample_star.inc: MapObj/TrampleStar.cpp after Petari (#included by game.c after ocean_ring.inc; trs_*): the
 * rubber ball Mario sinks into and is shot back from (PhantomNestZone's; PoyoPoyoPlanetZone's two are in no galaxy).
 * Its mesh (shared by every one: TrampleStarShare): a cube of 24 vertices and 12 faces divided three times (486
 * vertices, 768 faces; the faces after the second divide are the LOD's 192), set on the sphere of the first one's
 * radius 500 x scale x; the vertices at one place are each other's overlap. Its body sensor (a prior binder, that
 * radius) binds the player (AutoRushBegin) unless he moves out of it: the deform rate his speed within 30 .. 50 (15
 * capture frames at 50, else 10), the jump the same, the capture vertex the nearest to his sensor. BindingCapture:
 * Wait, SE_OJ_TRAMPOLINE_LAND_L, each frame the rate on the vertices within 400 of the capture vertex (x its
 * distance's square), the rate x 0.85. BindingShoot: the spring 0.1, each vertex's coeff less its speed, x 0.8, its
 * speed + coeff x spring; the capture vertex through zero: the spring 0.2 and Mario jumps along it (A held: x 1.5,
 * SE_OJ_TRAMPOLINE_BOUND_L, SE_PV_JUMP_JOY; else BOUND_S, SE_PV_JUMP_M; Jump2), Wait; its speed through zero: the
 * spring 0.1; at step 120 the jump if he is still bound, all still. Bound, his matrix is at the capture vertex
 * pushed in by its coeff, up along it. Out of Wait the overlapping vertices share their mean (calcAnim). Past 5000
 * from the player the LOD's faces, undeformed; back from it every coeff is cleared.
 * ponytail: drawn unlit with its texture x its material colour CC (MR::loadActorLight's lights and the Mario shadow
 * stage, TEXMAP1, are not made); the divide finds an edge's midpoint in the list of the edges made (Petari's check
 * flag table is numVtxs squared) */
enum { TRS_WAIT, TRS_CAPTURE, TRS_SHOOT };
typedef struct { int row, nrv, step, lod, cap, capf; float rate, jv, spring, r; V3 pos; float *coeff, *speed; } Trs;
static struct {
    Trs *s;
    int n, host, nv, nf; /* host: the one holding Mario, -1 none */
    float (*vtx)[3], (*st)[2];
    u16 (*face)[3], (*lod)[3];
    s16 *over;
} trs;

static V3 trs_v(int i) { return v3(trs.vtx[i][0], trs.vtx[i][1], trs.vtx[i][2]); }
static void trs_divide(void) /* divide */
{
    u16 edge[486][2];
    int nf = trs.nf, nv = trs.nv, v0 = trs.nv;
    for (int f = 0; f < trs.nf; f++) {
        u16 mid[3];
        for (int k = 0; k < 3; k++) {
            u16 a = trs.face[f][k], b = trs.face[f][(k + 1) % 3];
            int m = -1;
            for (int e = v0; e < nv && m < 0; e++) /* readCheckFlag */
                if ((edge[e][0] == a && edge[e][1] == b) || (edge[e][0] == b && edge[e][1] == a)) m = e;
            if (m < 0) {
                m = nv++;
                for (int c = 0; c < 3; c++) trs.vtx[m][c] = (trs.vtx[a][c] + trs.vtx[b][c]) * 0.5f;
                for (int c = 0; c < 2; c++) trs.st[m][c] = (trs.st[a][c] + trs.st[b][c]) * 0.5f;
                edge[m][0] = a; edge[m][1] = b;
            }
            mid[k] = (u16)m;
        }
        u16 v1 = trs.face[f][1], v2 = trs.face[f][2];
        trs.face[nf][0] = mid[0]; trs.face[nf][1] = v1; trs.face[nf][2] = mid[1]; nf++;
        trs.face[nf][0] = mid[0]; trs.face[nf][1] = mid[1]; trs.face[nf][2] = mid[2]; nf++;
        trs.face[nf][0] = mid[1]; trs.face[nf][1] = v2; trs.face[nf][2] = mid[2]; nf++;
        trs.face[f][1] = mid[0]; trs.face[f][2] = mid[2];
    }
    trs.nf = nf; trs.nv = nv;
}
static void trs_init(void) /* from game_init (everything in MEM_OBJECTS) */
{
    static const signed char cube[8][3] = {{-1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}};
    static const u8 copy[16] = {0, 3, 7, 4, 3, 2, 6, 7, 2, 1, 5, 6, 1, 0, 4, 5}; /* mVtxs[8 .. 23] */
    static const u8 f0[12][3] = {{1, 0, 2}, {2, 0, 3}, {18, 17, 16}, {18, 16, 19}, {21, 23, 22}, {21, 20, 23}, {13, 12, 15}, {14, 13, 15},
                                 {10, 9, 11}, {11, 9, 8}, {4, 5, 6}, {4, 6, 7}};
    int t = scene_type(sc, "TrampleStar"), n = 0;
    trs.n = 0; trs.host = -1; trs.s = NULL;
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) n += (int)sc->objs[i].type == t;
    if (!n) return;
    trs.s = mem_alloc(MEM_OBJECTS, n * sizeof(Trs));
    trs.vtx = mem_alloc(MEM_OBJECTS, 486 * 12); trs.st = mem_alloc(MEM_OBJECTS, 486 * 8);
    trs.face = mem_alloc(MEM_OBJECTS, 768 * 6); trs.lod = mem_alloc(MEM_OBJECTS, 192 * 6);
    trs.over = mem_alloc(MEM_OBJECTS, 486 * 2);
    for (int i = 0; i < 24; i++) {
        const signed char *c = cube[i < 8 ? i : copy[i - 8]];
        for (int k = 0; k < 3; k++) trs.vtx[i][k] = 500.0f * c[k];
        trs.st[i][0] = (i & 3) == 1 || (i & 3) == 2 ? 1.0f : 0.0f; trs.st[i][1] = (i & 3) >= 2 ? 1.0f : 0.0f;
    }
    for (int i = 0; i < 12; i++) for (int k = 0; k < 3; k++) trs.face[i][k] = f0[i][k];
    trs.nv = 24; trs.nf = 12;
    for (int d = 0; d < 3; d++) {
        trs_divide();
        if (d == 1) memcpy(trs.lod, trs.face, 192 * 6);
    }
    for (int a = 0; a < trs.nv; a++) trs.over[a] = -1;
    for (int a = 0; a < trs.nv; a++)
        for (int b = a + 1; b < trs.nv; b++)
            if (vlen(vsub(trs_v(a), trs_v(b))) < 1.0f) {
                if (trs.over[a] != -1) { trs.over[b] = trs.over[a]; trs.over[a] = (s16)b; }
                else { trs.over[a] = (s16)b; trs.over[b] = (s16)a; }
                break;
            }
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type != t) continue;
        Trs *s = &trs.s[trs.n];
        memset(s, 0, sizeof *s);
        s->row = (int)i; s->pos = row_pos(r); s->r = 500.0f * r->scale[0];
        s->coeff = mem_alloc(MEM_OBJECTS, 486 * 4); s->speed = mem_alloc(MEM_OBJECTS, 486 * 4);
        memset(s->coeff, 0, 486 * 4); memset(s->speed, 0, 486 * 4);
        if (trs.n++ == 0) /* toSphere(scale): the first one's */
            for (int v = 0; v < trs.nv; v++) {
                V3 p = vmul(vnorm(trs_v(v)), s->r);
                trs.vtx[v][0] = p.x; trs.vtx[v][1] = p.y; trs.vtx[v][2] = p.z;
            }
    }
    printf("trample star: %d placed, %d vertices, %d faces%c", trs.n, trs.nv, trs.nf, 10);
}
static void trs_share(Trs *s, int speed) /* the overlapping vertices' mean */
{
    for (int i = 0; i < trs.nv; i++) {
        int j = trs.over[i];
        if (j < 0) continue;
        s->coeff[i] = s->coeff[j] = (s->coeff[i] + s->coeff[j]) * 0.5f;
        if (speed) s->speed[i] = s->speed[j] = (s->speed[i] + s->speed[j]) * 0.5f;
    }
}
static void trs_jump(Trs *s) /* endBindAndPlayerJump(up x mJumpVel) */
{
    if (pad_held(PAD_CROSS)) s->jv *= 1.5f; /* getPlayerLevelA */
    mario->up = up_at(mario->pos, mario->up);
    m_power_jump(mario, vmul(vnorm(trs_v(s->cap)), s->jv));
    trs.host = -1;
}
static void trs_update(void)
{
    for (int k = 0; k < trs.n; k++) {
        Trs *s = &trs.s[k];
        if (!row_shown(s->row)) continue;
        int lod = vlen(vsub(mario->pos, s->pos)) >= 5000.0f; /* control: onLod / offLod */
        if (s->lod && !lod) memset(s->coeff, 0, 486 * 4);
        s->lod = lod;
        if (s->nrv != TRS_WAIT || trs.host >= 0 || glr_host >= 0 || tz_ride >= 0 || swr_ride || wrd.ride || pole_ride >= 0 || wb_ride >= 0 || mario->launch >= 0) continue;
        /* receiveOtherMsg ACTMES_AUTORUSH_BEGIN from the player's sensor */
        if (vlen(vsub(mw.c, s->pos)) >= s->r + HIT_BODY_R) continue;
        V3 vel = vadd(mario->hvel, vmul(mario->up, mario->vup));
        float rate = vlen(vel);
        int best = 0;
        float bd = 5000.0f;
        for (int v = 0; v < trs.nv; v++) { /* calcNearestVtxIndex */
            float d = vlen(vsub(vadd(trs_v(v), s->pos), mw.c));
            if (d < bd) { bd = d; best = v; }
        }
        if (vdot(vel, trs_v(best)) > 0.0f || !mode_rush("TrampleStar")) continue;
        s->capf = 10;
        if (rate < 30.0f) rate = 30.0f;
        if (rate > 50.0f) { rate = 50.0f; s->capf = 15; }
        s->rate = s->jv = rate; s->cap = best;
        trs.host = k;
        mario->hip = 0; mario->spin_timer = 0; mario->jumping = 0; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        s->nrv = TRS_CAPTURE; s->step = -1;
#ifdef TRAMPLE_TEST
        printf("trample test f %u: star %d binds Mario, rate %.0f, vertex %d%c", game_frame, k, rate, best, 10);
#endif
    }
#ifdef TRAMPLE_TEST /* plain build XF=-DTRAMPLE_TEST (PhantomGalaxy/n): Mario dropped on the first star every 300 frames from 1000 */
    if (trs.n) {
        test_nolost = 1;
        if (game_frame >= 1000 && game_frame % 300 == 100 && trs.host < 0) {
            V3 up = up_at(trs.s[0].pos, v3(0, 1, 0));
            mario->pos = vadd(trs.s[0].pos, vmul(up, trs.s[0].r + 600.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
            camera_start_pos_end();
        }
        if (game_frame >= 1000 && game_frame % 20 == 0)
            printf("trample test f %u: nerve %d step %d host %d coeff %.0f spring %.1f jv %.0f Mario %.0f off, vup %.0f%c", game_frame, trs.s[0].nrv, trs.s[0].step,
                   trs.host, trs.s[0].coeff[trs.s[0].cap], trs.s[0].spring, trs.s[0].jv, vlen(vsub(mario->pos, trs.s[0].pos)), mario->vup, 10);
    }
#endif
}
/* its Binding nerves with Mario, calcAnim, then his base matrix (ACTMES_UPDATE_BASEMTX) */
static int trs_hold(Player *m)
{
    if (trs.host < 0) return 0;
    Trs *s = &trs.s[trs.host];
    int st = ++s->step;
    if (s->nrv == TRS_CAPTURE) {
        if (st == 0) { if (mario_anim) mario_play("wait", 1.0f, 1); se_start(SE_OJ_TRAMPOLINE_LAND_L, &m->pos.x); }
        V3 c = trs_v(s->cap);
        for (int v = 0; v < trs.nv; v++) { /* moveVtx */
            float d = vlen(vsub(trs_v(v), c));
            if (d < 400.0f) { float t = (400.0f - d) / 400.0f; s->coeff[v] += s->rate * t * t; }
        }
        s->rate *= 0.85f;
        if (st > s->capf) { s->nrv = TRS_SHOOT; s->step = -1; }
    } else {
        if (st == 0) {
            s->rate = 0.0f; s->spring = 0.1f;
            trs_share(s, 0);
            for (int v = 0; v < trs.nv; v++) s->speed[v] = s->spring * s->coeff[v];
        }
        for (int v = 0; v < trs.nv; v++) {
            float c = s->coeff[v], sp = s->speed[v];
            s->coeff[v] = (c - sp) * 0.8f;
            s->speed[v] += c * s->spring;
            if (v != s->cap) continue;
            if (c * s->coeff[v] <= 0.0f) {
                s->spring = 0.2f;
                if (trs.host >= 0) { /* isPlayerInBind */
                    int a = pad_held(PAD_CROSS);
                    se_start(a ? SE_OJ_TRAMPOLINE_BOUND_L : SE_OJ_TRAMPOLINE_BOUND_S, &m->pos.x);
                    se_start(a ? SE_PV_JUMP_JOY : SE_PV_JUMP_M, &m->pos.x);
                    trs_jump(s);
                    if (mario_anim) mario_play("jump2", 1.0f, 0);
                    s->nrv = TRS_WAIT;
                }
            }
            if (sp * s->speed[v] <= 0.0f) s->spring = 0.1f;
        }
        if (st == 120 && s->nrv == TRS_SHOOT) {
            trs_jump(s);
            s->nrv = TRS_WAIT;
            memset(s->speed, 0, 486 * 4); memset(s->coeff, 0, 486 * 4);
        }
    }
    if (s->nrv != TRS_WAIT) trs_share(s, 1); /* calcAnim */
    if (trs.host < 0) return 0;
    V3 up = vnorm(trs_v(s->cap));
    m->pos = vadd(vadd(trs_v(s->cap), s->pos), vmul(up, -s->coeff[s->cap]));
    m->up = up;
    V3 f = vtangent(m->face, up);
    if (vlen(f) > 1e-4f) m->face = vnorm(f);
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    return 1;
}
static void trs_draw(void)
{
    if (!trs.n) return;
    const Model *md = scene_model(sc, "TrampleStar"); /* TrampleStar.bti (slice.py TEX_ONLY), in its zone's pack: found once that is in */
    if (!md || !md->vu_ngroups) return;
    for (int k = 0; k < trs.n; k++) {
        const Trs *s = &trs.s[k];
        float c[3] = {s->pos.x, s->pos.y, s->pos.z};
        if (!row_shown(s->row) || !render_visible(c, s->r)) continue;
        for (int v = 0; v < trs.nv; v++) { /* calcSurface / calcLodSurface: the vertex pushed in along its normal */
            V3 p = trs_v(v);
            if (!s->lod) p = vsub(p, vmul(vnorm(p), s->coeff[v]));
            or_pos[v][0] = p.x + s->pos.x; or_pos[v][1] = p.y + s->pos.y; or_pos[v][2] = p.z + s->pos.z;
        }
        render_mesh_c((const float (*)[3])or_pos, (const float (*)[2])trs.st, trs.nv, s->lod ? trs.lod[0] : trs.face[0], s->lod ? 192 : trs.nf,
                      md->vu[0].tex, 0, 0x80666666u); /* the material colour CC CC CC */
    }
}
