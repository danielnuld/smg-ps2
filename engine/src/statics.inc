/* statics.inc: placed classes the port drew still, after Petari (#included by game.c; st_*). st_update from
 * game_update, st_draw from game_draw's world pass. */

/* ---- Flag.cpp after Petari (Flag, FlagKoopaA / B, FlagRaceA, FlagKoopaCastle, FlagPeachCastleA / B / C,
 * FlagTamakoro): a cloth of nv x nu SwingRopePoints hung from nv fix points up its pole (arg1 mStickLength along its
 * up, mPointIntervalV apart), each row a chain of nu points mPointIntervalU apart. Each frame (movement): SE_OJ_FLAG
 * every 36..65 frames (but FlagPeachCastleA / FlagRaceA), mWavePhase += 5, updateFlag: gravity (LiveActor's mGravity:
 * Flag never calculates its own, (0, -1, 0)) x mGravityScale and a side wind |sin(phase + 10 u + 10 v)| x
 * mWindAccelRate + mWindAccelMin along its Z on every point; random wind (a vertical flag: one random point every 0..1
 * frames, else every row's first point), the rows' columns restricted to mPointIntervalV (with the point above's
 * velocity), the rows to mPointIntervalU from the fix point, updatePos with the column's friction, the light colour
 * by the point's up on its front. Drawn (DrawType_Flag) as strips with <name>.bti (slice.py TEX_ONLY) x the vertex
 * colours, blended, and its green stick (5 x 2 off along world X / Z) when mStickLength > 0. Clipped by its sphere
 * (FlagKoopaCastle 800 at 500 up; with a stick stick / 2 + 300 at its middle; else LiveActor's 300 about it, far
 * 10000): no movement, not drawn. ponytail: the stick taken as the texture's (0, 0) texel x its green (the GS
 * modulates one texture a draw: a texel of the cloth's); no fog (GX_FOG_NONE: the port has none either);
 * FlagSurfing (OceanRingPipe's createMapFlag) is not made here */
#define ST_FLAG_MAX 64
#define ST_FLAG_PTS 2560
typedef struct { V3 pos, vel, up; } StFlagPt;
typedef struct {
    int row, nu, nv, vert, nosnd, wait, se, clip;
    float iu, iv, stick, phase, grav, fmin, fmax, wrate, wmin, rmin, rmax, lmin;
    V3 pos, up, side, front, ccen;
    float crad, cfar;
    u16 tex;
    StFlagPt *p;      /* nv x nu: p[v * nu + u] */
    V3 fix[10];
    u8 col[10 * 10]; /* mColors of the points (the fix points' stay white) */
} StFlag;
static StFlag st_flags[ST_FLAG_MAX];
static StFlagPt st_flag_pts[ST_FLAG_PTS];
static int st_nflags;
static const Scene *st_flag_sc;
static int st_flag_class(const char *n)
{
    static const char *const k[] = {"Flag", "FlagKoopaA", "FlagKoopaB", "FlagRaceA", "FlagKoopaCastle", "FlagPeachCastleA",
                                    "FlagPeachCastleB", "FlagPeachCastleC", "FlagTamakoro"};
    for (u32 i = 0; i < sizeof k / sizeof *k; i++) if (!strcmp(n, k[i])) return 1;
    return 0;
}
static void st_flag_init(void) /* Flag::init + appear, every placed Flag of the scene */
{
    st_flag_sc = sc; st_nflags = 0;
    int used = 0;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        const char *n = sc->types + 32 * r->type;
        if (!st_flag_class(n)) continue;
        const Model *md = scene_model(sc, n);
        if (!md || !md->vu_ngroups || st_nflags == ST_FLAG_MAX) continue;
        StFlag *f = &st_flags[st_nflags];
        memset(f, 0, sizeof *f);
        f->row = (int)i; f->tex = md->vu[0].tex;
        f->nv = 10; f->nu = 10; f->iu = 40.0f; f->iv = 40.0f; /* sDefaultPointIntervalU / V */
        f->grav = 0.1f; f->fmin = 0.6f; f->fmax = 1.0f; f->wrate = 10.0f; f->wmin = 0.1f; f->rmin = 1.0f; f->rmax = 4.0f;
        f->lmin = 120.0f; /* sLightColorMinFar */
        f->stick = r->arg[1] != -1 ? (float)r->arg[1] : 0.0f; /* getJMapInfoArg1NoInit */
        if (!strcmp(n, "FlagKoopaCastle")) { f->iu = 1000.0f / f->nu; f->iv = 500.0f / (f->nv - 1); }
        else if (!strcmp(n, "FlagKoopaA")) { f->iu = 450.0f / f->nu; f->iv = 275.0f / (f->nv - 1); }
        else if (!strcmp(n, "FlagKoopaB")) { f->iu = 450.0f / f->nu; f->iv = 112.5f / (f->nv - 1); }
        else if (!strcmp(n, "FlagPeachCastleA")) {
            f->nv = 5; f->nu = 6; f->iu = 170.0f / f->nu; f->iv = 145.0f / (f->nv - 1);
            f->vert = 1; f->grav = 0.5f; f->fmin = 0.85f; f->fmax = 1.0f; f->wrate = 0.0f; f->wmin = 0.01f;
            f->rmin = 0.5f; f->rmax = 1.5f; f->nosnd = 1; /* (mAlphaDistanceNear / Far: mUseAlpha is FlagSurfing's) */
        } else if (!strcmp(n, "FlagPeachCastleB") || !strcmp(n, "FlagPeachCastleC")) {
            f->nv = 5; f->nu = 5; f->iu = 500.0f / f->nu; f->iv = 400.0f / (f->nv - 1);
        } else if (!strcmp(n, "FlagRaceA")) f->nosnd = 1;
        if (used + f->nu * f->nv > ST_FLAG_PTS) { printf("flag %u: no room for its points%c", i, 10); continue; } /* ponytail: ST_FLAG_PTS a scene */
        f->p = &st_flag_pts[used]; used += f->nu * f->nv;
        float m[16], one[3] = {1, 1, 1};
        mat_translate_rot_scale(m, r->pos, r->rot, one);
        f->pos = row_pos(r); f->up = v3(m[4], m[5], m[6]); f->side = v3(m[8], m[9], m[10]); /* calcActorAxis(&front, &mUp, &mSide) */
        for (int v = 0; v < f->nv; v++) {
            f->fix[v] = vadd(f->pos, vmul(f->up, f->stick + f->iv * ((f->nv - 1) - v)));
            for (int u = 0; u < f->nu; u++) {
                StFlagPt *q = &f->p[v * f->nu + u];
                q->vel = v3(0, 0, 0); q->up = v3(0, 1, 0);
                q->pos = f->fix[v]; /* appear: setPosAndAxis(mFixPoints[v].mPos) (over the init's place, mGravity x
                                     * mPointIntervalU (u + 1) below it for a vertical flag) */
            }
        }
        memset(f->col, 0xFF, sizeof f->col);
        if (!strcmp(n, "FlagKoopaCastle")) { f->ccen = vadd(vmul(f->up, 500.0f), f->pos); f->crad = 500.0f + 300.0f; }
        else if (f->stick > 0.0f) { f->ccen = vadd(vmul(f->up, f->stick * 0.5f), f->pos); f->crad = f->stick / 2 + 300.0f; }
        else { f->ccen = f->pos; f->crad = 300.0f; } /* LiveActor's ClippingActorInfo */
        f->cfar = 10000.0f;
        se_start(SE_OJ_FLAG_APPEAR, &f->pos.x); /* appear */
        st_nflags++;
    }
    if (st_nflags) printf("flags: %d (%d points)%c", st_nflags, used, 10);
}
static float st_rnd(float a, float b) { return a + (b - a) * (float)(rand() % 10001) / 10000.0f; } /* MR::getRandom(f32, f32) */
static int st_rndi(int a, int b) { return a + rand() % (b - a); }                                  /* MR::getRandom(s32, s32) */
static int st_restrict(StFlagPt *q, V3 anchor, float len, const V3 *vel) /* SwingRopePoint::restrict */
{
    V3 d = vsub(vadd(q->pos, q->vel), anchor);
    if (vel) d = vsub(d, *vel);
    V3 n;
    if (vlen(d) < 1e-6f) n = vmul(q->up, -1.0f);
    else { n = vnorm(d); q->up = vmul(n, -1.0f); }
    if (vdot(d, d) >= len * len) { q->vel = vsub(q->vel, vsub(d, vmul(n, len))); return 1; }
    return 0;
}
static void st_flag_update(StFlag *f) /* Flag::movement, updateFlag */
{
    if (!f->nosnd) {
        if (f->se <= 0) { se_start(SE_OJ_FLAG, &f->pos.x); f->se = st_rndi(36, 66); }
        else f->se--;
    }
    f->phase += 5.0f; /* sWindTimeSpeed */
    f->front = vcross(f->side, f->up);
    f->front = vlen(f->front) < 1e-6f ? f->side : vnorm(f->front);
    V3 eye, watch, cup; float fovy;
    camera_pose(&eye, &watch, &cup, &fovy);
    float cd = vlen(vsub(f->pos, eye));
    if (cd < 500.0f) f->lmin = 200.0f; /* sLightDistanceNear: sLightColorMinNear */
    else if (cd >= 1000.0f) f->lmin = 120.0f;
    else { float d = (cd - 500.0f) / 500.0f; f->lmin = d * 120.0f + (1.0f - d) * 200.0f; }
    V3 g = vmul(v3(0, -1, 0), f->grav);
    for (int v = 0; v < f->nv; v++)
        for (int u = 0; u < f->nu; u++) {
            StFlagPt *q = &f->p[v * f->nu + u];
            q->vel = vadd(q->vel, g);
            float w = fabsf(sinf((f->phase + 10.0f * u + 10.0f * v) * 0.017453292f)); /* sWindPosRateU / V */
            q->vel = vadd(q->vel, vmul(f->side, f->wmin + w * f->wrate));
        }
    if (f->vert) {
        if (f->wait <= 0) {
            int pv = st_rndi(0, f->nv), pu = st_rndi(0, f->nu);
            V3 w = v3(st_rnd(-1.0f, 1.0f), 0.0f, st_rnd(-1.0f, 1.0f));
            w = vtangent(w, f->up); /* vecKillElement */
            w = vmul(w, st_rnd(f->rmin, f->rmax));
            f->p[pv * f->nu + pu].vel = vadd(f->p[pv * f->nu + pu].vel, w);
            f->wait = st_rndi(0, 2); /* sRandomWaitTimeMin / Max */
        } else f->wait--;
    } else
        for (int v = 0; v < f->nv; v++) {
            V3 w = vmul(v3(st_rnd(-1.0f, 1.0f), 0.0f, st_rnd(-1.0f, 1.0f)), st_rnd(f->rmin, f->rmax));
            f->p[v * f->nu].vel = vadd(f->p[v * f->nu].vel, w);
        }
    for (int u = 0; u < f->nu; u++) { /* restrict in the horizontal direction */
        V3 a = f->p[u].pos, av = f->p[u].vel;
        for (int v = 1; v < f->nv; v++) {
            StFlagPt *q = &f->p[v * f->nu + u];
            st_restrict(q, a, f->iv, &av);
            a = q->pos; av = q->vel;
        }
    }
    for (int v = 0; v < f->nv; v++) { /* and in the vertical one */
        V3 a = f->fix[v];
        for (int u = 0; u < f->nu; u++) { StFlagPt *q = &f->p[v * f->nu + u]; st_restrict(q, a, f->iu, NULL); a = q->pos; }
    }
    for (int u = 0; u < f->nu; u++) {
        float fr = f->fmin + (1.0f - (float)u / (float)(f->nu - 1)) * (f->fmax - f->fmin);
        for (int v = 0; v < f->nv; v++) {
            StFlagPt *q = &f->p[v * f->nu + u];
            q->pos = vadd(q->pos, q->vel); q->vel = vmul(q->vel, fr); /* updatePos */
            float lc = 255.0f * ((vdot(q->up, f->front) + 1.0f) / 1.5f); /* sLightBorderMin -1, Max 0.5 */
            lc = lc < f->lmin ? f->lmin : lc > 255.0f ? 255.0f : lc;
            f->col[v * f->nu + u] = (u8)lc;
        }
    }
}
static void st_flags_update(void)
{
    if (st_flag_sc != sc) st_flag_init();
    for (int k = 0; k < st_nflags; k++) {
        StFlag *f = &st_flags[k];
        f->clip = !row_shown(f->row) || render_clip_judge(&f->ccen.x, f->crad, f->cfar);
        if (!f->clip) st_flag_update(f);
    }
}
static u32 st_grey(int c) { u32 k = (u32)(c * 128 + 127) / 255; return 0x80000000u | k << 16 | k << 8 | k; } /* 0..255 -> GS 0..0x80 */
static void st_flags_draw(void)
{
    static float pos[10 * 11][3], uv[10 * 11][2];
    static u32 col[10 * 11];
    static u16 idx[9 * 10 * 6];
    for (int k = 0; k < st_nflags; k++) {
        StFlag *f = &st_flags[k];
        if (f->clip) continue;
        int w = f->nu + 1, nt = 0;
        for (int v = 0; v < f->nv; v++) {
            float tv = (float)v / (float)(f->nv - 1);
            for (int c = 0; c < w; c++) {
                int i = v * w + c;
                V3 p = c ? f->p[v * f->nu + c - 1].pos : f->fix[v];
                pos[i][0] = p.x; pos[i][1] = p.y; pos[i][2] = p.z;
                uv[i][0] = (float)c / (float)f->nu; uv[i][1] = tv; /* mTexST */
                col[i] = st_grey(c ? f->col[v * f->nu + c - 1] : 255);
            }
        }
        for (int v = 0; v + 1 < f->nv; v++)
            for (int c = 0; c + 1 < w; c++) {
                u16 a = (u16)(v * w + c), b = (u16)((v + 1) * w + c);
                idx[nt * 3] = b; idx[nt * 3 + 1] = a; idx[nt * 3 + 2] = (u16)(b + 1); nt++;
                idx[nt * 3] = a; idx[nt * 3 + 1] = (u16)(b + 1); idx[nt * 3 + 2] = (u16)(a + 1); nt++;
            }
        render_mesh_vc((const float (*)[3])pos, (const float (*)[2])uv, col, f->nv * w, idx, nt, f->tex, 3);
        if (f->stick > 0.0f) { /* the stick: sStickColors, 8 vertices up its length */
            static const float sc2[4][2] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}};
            static const u8 green[4] = {0xC8, 0xDC, 0xB4, 0xC8};
            float len = f->stick + f->iv * (f->nv - 1);
            int nv = 0; nt = 0;
            for (int j = 0; j < 4; j++) {
                V3 o = v3(sc2[j][0] * 5.0f * 2.0f, 0.0f, sc2[j][1] * 5.0f * 2.0f); /* v1 + v2, sStickWidth each */
                V3 a = vadd(vadd(vmul(f->up, len), o), f->pos), b = vadd(f->pos, o);
                u32 c = 0x80000000u | (u32)(green[j] * 128 / 255) << 8;
                pos[nv][0] = a.x; pos[nv][1] = a.y; pos[nv][2] = a.z; uv[nv][0] = uv[nv][1] = 0.0f; col[nv++] = c;
                pos[nv][0] = b.x; pos[nv][1] = b.y; pos[nv][2] = b.z; uv[nv][0] = uv[nv][1] = 0.0f; col[nv++] = c;
            }
            for (int s = 0; s + 2 < nv; s++) { idx[nt * 3] = (u16)s; idx[nt * 3 + 1] = (u16)(s + 1); idx[nt * 3 + 2] = (u16)(s + 2); nt++; }
            render_mesh_vc((const float (*)[3])pos, (const float (*)[2])uv, col, nv, idx, nt, f->tex, 3);
        }
    }
}

#ifdef ST_FLAG_TEST /* plain build XF=-DST_FLAG_TEST=k: from frame 60 the camera looks at flag k from its front */
static void st_flag_test(void)
{
    if (!st_nflags || game_frame < 60) return;
    StFlag *f = &st_flags[ST_FLAG_TEST % st_nflags];
    V3 front = vlen(f->front) > 0.5f ? f->front : vnorm(vcross(f->side, f->up));
    V3 c = vadd(f->fix[f->nv / 2], vmul(f->side, 0.0f));
    camera_set_override(1, vadd(vadd(c, vmul(front, f->iu * f->nu * 2.5f + 300.0f)), vmul(f->side, f->iu * f->nu * 0.5f)), c, f->up, 45.0f);
    if (game_frame % 120 == 60) {
        const StFlagPt *q = &f->p[f->nv * f->nu - 1];
        printf("flag test f %u: flag %d row %d clip %d, last point (%d %d %d), colour %d%c", game_frame, ST_FLAG_TEST % st_nflags, f->row,
               f->clip, (int)q->pos.x, (int)q->pos.y, (int)q->pos.z, f->col[f->nv * f->nu - 1], 10);
    }
}
#endif
/* ---- MapObj classes by placed type: their per-row state (StSt, kept across rebinds) ---- */
enum { ST_NONE = 1, ST_TWALL, ST_MIRRORMODEL, ST_UFOKC, ST_SHELLTOWER, ST_DOMEGALAXY, ST_STILL };
/* Placed classes whose Petari behaviour is the model drawn with its animations and collision (the engine's generic
 * instance does that): LavaGalaxyParts.cpp (LavaShellTowerStep, LavaStepA / B / C: startBtk / Brk / Bpk / Btp of its
 * name, collision, clipping; ponytail: the steps' DLMakerProjmapEffectMtxSetter lava glow is baked at slice time and
 * LodCtrlPlanet's low models are the zones' low packs), GlaringLight (TeresaRoomGlaringLight: SimpleMapObj in
 * DrawBufferType_GlaringLight; ponytail: drawn with the map objects, not in that buffer's order), SimpleMirrorObj
 * (MirrorReflectionTwinFallLake: the mirror's surface; ponytail: no mirror camera, its reflection texture is drawn as
 * the model's own) */
static const char *const st_still[] = {"LavaShellTowerStep", "LavaStepA", "LavaStepB", "LavaStepC", "TeresaRoomGlaringLight",
                                       "MirrorReflectionTwinFallLake"};
static signed char st_kinds[1024];
static const Scene *st_kinds_sc;
static int st_kind_name(const char *n)
{
    if (!strcmp(n, "InvisibleWaterfallTwinFallLake") || !strcmp(n, "StatusFloor")) return ST_TWALL; /* TransparentWall */
    if (!strcmp(n, "MirrorModelTwinFallLake")) return ST_MIRRORMODEL; /* SimpleMirrorReflectionObj */
    if (!strcmp(n, "UFOKinokoUnderConstruction")) return ST_UFOKC;
    if (!strcmp(n, "LavaShellTower")) return ST_SHELLTOWER;
    if (!strcmp(n, "AstroDomeDemoAstroGalaxy")) return ST_DOMEGALAXY;
    for (u32 k = 0; k < sizeof st_still / sizeof *st_still; k++) if (!strcmp(n, st_still[k])) return ST_STILL;
    return ST_NONE;
}
static int st_kind(int type)
{
    if (st_kinds_sc != sc) { memset(st_kinds, 0, sizeof st_kinds); st_kinds_sc = sc; }
    if (type >= 1024) return st_kind_name(sc->types + 32 * type);
    if (!st_kinds[type]) st_kinds[type] = (signed char)st_kind_name(sc->types + 32 * type);
    return st_kinds[type];
}
typedef struct { int row, n, st, on; const unsigned char *kcl; } StSt; /* n its nerve, st its step */
static StSt st_st[128];
static int nst_st;
static const Scene *st_st_sc;
static StSt *st_get(int row)
{
    if (st_st_sc != sc) { nst_st = 0; st_st_sc = sc; }
    for (int k = 0; k < nst_st; k++) if (st_st[k].row == row) return &st_st[k];
    if (nst_st == 128) return &st_st[127]; /* ponytail: 128 rows of these classes a scene */
    memset(&st_st[nst_st], 0, sizeof st_st[0]); st_st[nst_st].row = row;
    return &st_st[nst_st++];
}
static int st_grand(int n) { char f[24]; snprintf(f, sizeof f, "SpecialStarGrand%d", n); return gd_flag_is_on(f); } /* hasGrandStar */
/* the scene's start (game_init): the classes dead from their init */
static void st_init(const Scene *s)
{
    for (u32 i = 0; i < s->nobjs; i++) {
        const char *n = s->types + 32 * s->objs[i].type;
        /* UFOKinokoUnderConstruction::init: isUFOKinokoBeforeConstruction (EventKinopioExplorerRescued off): dead */
        if (!strcmp(n, "UFOKinokoUnderConstruction") && !gd_flag_is_on("EventKinopioExplorerRescued")) row_dead[i] = 1;
    }
}
/* TransparentWall.cpp after Petari (InvisibleWaterfallTwinFallLake, StatusFloor: InvisiblePolygonObj, never drawn):
 * its collision off from init, on while Mario is in a mode of arg4's bits 0..9 (isPlayerElementMode), skating (bit
 * 10) or, with bit 11, while not in Teresa Mario's disappearing (isPlayerTeresaDisappear); off again when none holds
 * (with bit 11: as soon as he disappears). ponytail: its body sensor's ATYPE_ICEJUMP_WALL (no wall jump off ice) */
static void st_twall(Inst *in, StSt *g)
{
    int bits = sc->objs[in->row].arg[4] == -1 ? 0 : sc->objs[in->row].arg[4];
    if (in->kcl) { g->kcl = in->kcl; if (!g->on) in->kcl = NULL; } /* (a rebind brings it back: kept and taken again) */
    int mode = 0;
    for (int b = 0; b < 10; b++) if ((bits >> b & 1) && player_mode == b) mode = 1;
    if ((bits & 1024) && skate_active()) mode = 1;
    if (!g->on) {
        if ((bits & 2048) && !teresa_through()) mode = 1;
        if (mode) { g->on = 1; in->kcl = g->kcl; printf("transparent wall %d: collision on%c", in->row, 10); }
    } else {
        int off = !mode;
        if (bits & 2048) off = teresa_through();
        if (off) { g->on = 0; in->kcl = NULL; printf("transparent wall %d: collision off%c", in->row, 10); }
    }
}
/* LavaShellTower.cpp after Petari: Wait until its 開始 demo's action (registerDemoActionNerve: st_cast_hook), Demo
 * (shakeCameraInfinity 0.1 / 2.2, overlayWithPreviousScreen(2), SE_DM_LV_SHELL_TOWER and a strong rumble each frame;
 * past step 1 at the demo's last step stopShakingCamera) -> Done */
enum { LST_WAIT, LST_DEMO, LST_DONE };
static void st_shelltower(Inst *in, StSt *g)
{
    static u32 lv;
    if (g->n != LST_DEMO) return;
    if (g->st == 0) { camera_shake_infinity(g, 0.1f, 2.2f); render_cover(2); }
    lv = se_level(SE_DM_LV_SHELL_TOWER, NULL, lv); /* startSystemLevelSE */
    pad_rumble(RUMBLE_STRONG);
    const Demo *d = dx.on ? &demos[dx.cur] : NULL;
    if (g->st > 1 && (!d || (dx.part == d->h->nparts - 1 && dx.step >= d->parts[dx.part].total - 1))) { /* isDemoLastStep */
        camera_shake_stop(g); g->n = LST_DONE; g->st = 0;
        printf("lava shell tower %d: done%c", in->row, 10);
    }
}
static int st_cast_hook(int row, int type, int part) /* dx_cast_hook: the demo actions of these classes */
{
    (void)part;
    if (type == 3 && st_kind(sc->objs[row].type) == ST_SHELLTOWER) {
        StSt *g = st_get(row);
        g->n = LST_DEMO; g->st = 0;
        printf("lava shell tower %d: demo%c", row, 10);
        return 1;
    }
    return 0;
}
static void st_objs_update(void)
{
    static int organize = -1;
    static const Scene *seen;
    if (seen != sc) { seen = sc; organize = gd_flag_is_on("EventKinopioExplorerOrganize"); }
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        int k = st_kind(sc->objs[in->row].type);
        if (k == ST_NONE || k == ST_STILL) continue;
        StSt *g = st_get(in->row);
#ifdef ST_TWALL_TEST
        if (!g->st) printf("st kind %d row %d kcl %p%c", k, in->row, (const void *)in->kcl, 10);
#endif
        switch (k) {
        case ST_TWALL: st_twall(in, g); break;
        case ST_MIRRORMODEL: in->invisible = 1; break; /* connectToSceneMirrorMapObjNoMovement: drawn in the mirror only
                                                         * (ponytail: no mirror pass, so never) */
        case ST_UFOKC: /* UFOKinokoUnderConstruction::init: UFOKinokoLandingAstro's model (and collision) once
                        * isUFOKinokoUnderConstruction is over (EventKinopioExplorerOrganize on) */
            if (organize) {
                const Model *md = scene_model(sc, "UFOKinokoLandingAstro");
                if (md && in->md != md) { in->md = md; in->kcl = scene_kcl(sc, "UFOKinokoLandingAstro"); in->reach = md->radius * in->s; }
            }
            break;
        case ST_SHELLTOWER: st_shelltower(in, g); break;
        case ST_DOMEGALAXY: { /* AstroDomeDemoAstroGalaxy::init: setBrkFrameAndStop 2 with grand star 5, 1 with 3, else 0
                               * (slice.py BRK_STEPS: "<class>~<frame>") */
            char n[40];
            snprintf(n, sizeof n, "AstroDomeDemoAstroGalaxy~%d", st_grand(5) ? 2 : st_grand(3) ? 1 : 0);
            const Model *md = scene_model(sc, n);
            if (md) in->md = md;
            break;
        }
        }
        if (g->st < 0x7FFF) g->st++;
    }
}

#ifdef ST_TWALL_TEST /* plain build XF=-DST_TWALL_TEST, HeavenlyBeachGalaxy/1: Mario Ice from frame 300 to 400 */
static void st_twall_test(void)
{
    if (game_frame == 100)
        for (u32 i = 0; i < sc->nobjs; i++)
            if (st_kind(sc->objs[i].type) == ST_TWALL) {
                mario->pos = vadd(row_pos(&sc->objs[i]), v3(0, 600.0f, 0)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
                printf("twall test: Mario over row %u (%d %d %d)%c", i, (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z, 10);
                break;
            }
    if (game_frame == 900) { player_mode = PM_ICE; printf("twall test: ice, Mario (%d %d %d)%c", (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z, 10); }
    if (game_frame == 1000) { player_mode = PM_NORMAL; printf("twall test: normal%c", 10); }
}
#endif
static void st_update(void)
{
#ifdef ST_TWALL_TEST
    st_twall_test();
#endif
    st_flags_update();
    st_objs_update();
#ifdef ST_FLAG_TEST
    st_flag_test();
#endif
}
static void st_draw(void)
{
    st_flags_draw();
}
