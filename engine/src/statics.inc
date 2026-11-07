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
enum { ST_NONE = 1, ST_TWALL, ST_MIRRORMODEL, ST_MIRROR, ST_UFOKC, ST_SHELLTOWER, ST_DOMEGALAXY, ST_STILL, ST_SURP, ST_LAVABALL, ST_TMWALL, ST_MANHOLE, ST_CHAIN, ST_BIGCOL, ST_FLEX, ST_TEXSW, ST_TRIPROP };
/* Placed classes whose Petari behaviour is the model drawn with its animations and collision (the engine's generic
 * instance does that): LavaGalaxyParts.cpp (LavaShellTowerStep, LavaStepA / B / C: startBtk / Brk / Bpk / Btp of its
 * name, collision, clipping; ponytail: the steps' DLMakerProjmapEffectMtxSetter lava glow is baked at slice time and
 * LodCtrlPlanet's low models are the zones' low packs), GlaringLight (TeresaRoomGlaringLight: SimpleMapObj in
 * DrawBufferType_GlaringLight; ponytail: drawn with the map objects, not in that buffer's order), CoconutTree.cpp (its
 * model, collision, a clipping sphere of 2000) */
static struct { const Scene *at; int inst, ok; V3 n, p; } st_mir = {NULL, -1, 0, {0, 0, 0}, {0, 0, 0}}; /* SimpleMirrorObj (st_mirror_*) */
static const char *const st_still[] = {"LavaShellTowerStep", "LavaStepA", "LavaStepB", "LavaStepC", "TeresaRoomGlaringLight",
                                       "CoconutTree"};
static signed char st_kinds[1024];
static const Scene *st_kinds_sc;
static int st_kind_name(const char *n)
{
    if (!strcmp(n, "InvisibleWaterfallTwinFallLake") || !strcmp(n, "StatusFloor")) return ST_TWALL; /* TransparentWall */
    if (!strcmp(n, "MirrorModelTwinFallLake")) return ST_MIRRORMODEL; /* SimpleMirrorReflectionObj */
    if (!strcmp(n, "MirrorReflectionTwinFallLake")) return ST_MIRROR; /* SimpleMirrorObj */
    if (!strcmp(n, "UFOKinokoUnderConstruction")) return ST_UFOKC;
    if (!strcmp(n, "LavaShellTower")) return ST_SHELLTOWER;
    if (!strcmp(n, "AstroDomeDemoAstroGalaxy")) return ST_DOMEGALAXY;
    if (!strcmp(n, "LavaBallRisingPlanetLava")) return ST_LAVABALL;
    if (!strcmp(n, "KoopaShipManholeCover")) return ST_MANHOLE; /* ManholeCover */
    if (!strcmp(n, "LargeChain")) return ST_CHAIN;
    if (!strcmp(n, "SubmarineVolcanoBigColumn")) return ST_BIGCOL;
    if (!strcmp(n, "FlexibleSandPlanetPartsA")) return ST_FLEX; /* FlexibleSphere */
    if (!strcmp(n, "TrialBubbleTimerMoveWallA")) return ST_TMWALL; /* TimerMoveWall */
    if (!strcmp(n, "TeresaPicture") || !strcmp(n, "KinokoOneUpPicture")) return ST_TEXSW; /* SimpleTextureSwitchChangeObj */
    if (!strcmp(n, "SeaBottomTriplePropeller")) return ST_TRIPROP;
    if (!strcmp(n, "SurprisedGalaxy") || (!strncmp(n, "Surp", 4) && strlen(n) > 10 && !strcmp(n + strlen(n) - 6, "Galaxy"))) return ST_SURP;
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
typedef struct { int row, n, st, on, t2, t3; const unsigned char *kcl; u32 lv[3]; V3 off; float k; } StSt; /* n its nerve, st its step */
static StSt st_st[128];
static void st_column(Inst *in, StSt *g); /* SubmarineVolcanoBigColumn (below) */
static void st_flex(Inst *in, StSt *g, int i); /* FlexibleSphere (below) */
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
/* SurprisedGalaxy.cpp after Petari (SurprisedGalaxy, Surp<galaxy>: the observatory's surprise galaxies): mGalaxyName
 * SurfingLv1Galaxy for "SurprisedGalaxy", else its name after "Surp"; the MiniSurprisedGalaxy model (slice.py
 * MODEL_ARC) playing MiniSurprisedGalaxy (its BTK); far clipping 200 m; its appear switch (useStageSwitchReadAppear),
 * else out only once SpecialStarGreenAll is on for a galaxy whose Appear<galaxy> flag depends on it
 * (isGalaxyAppearGreenDriver), for the rest once the complete ending was seen with both (ViewCompleteMarioAndLuigi).
 * Its body (a map object sensor of its model's bounding radius) touched by Mario (attackSensor): Exit: submitTrigSE,
 * closeSystemWipeWhiteFade, stopStageBGM / stopSubBGM(90), ギャラクシー移動 demo; once the wipe is closed its
 * galaxy's flag on (onGameEventFlagGalaxyOpen), endDemo, permitTrigSE, requestStartScenarioSelect(galaxy).
 * ponytail: ACTMES_IS_RUSH_TAKEOVER (a ride carrying Mario in) is not sent by the port's rides */
static const char *st_surp_galaxy(const char *n) { return !strcmp(n, "SurprisedGalaxy") ? "SurfingLv1Galaxy" : n + 4; }
static void st_surp(Inst *in, StSt *g)
{
    const char *gal = st_surp_galaxy(sc->types + 32 * sc->objs[in->row].type);
    if (g->n == 0) { /* Wait */
        float c[3] = {in->m[12], in->m[13], in->m[14]};
        if (!dx_busy() && mario_touch(v3(c[0], c[1], c[2]), in->md->radius * in->s)) {
            g->n = 1; g->st = 0;
            printf("surprised galaxy %d (%s): exit%c", in->row, gal, 10);
        }
        return;
    }
    if (g->n == 1) {
        if (g->st == 0) {
            menu_wipe(1, -1, 0xFFFFFF); /* closeSystemWipeWhiteFade */
            bgm_stop(90);               /* stopStageBGM(90), stopSubBGM(90) */
            dx_request("ギャラクシー移動", 0); /* tryStartDemo */
        }
        if (menu_wipe_active()) return; /* isSystemWipeActive */
        gd_flag_try_on(gal);            /* onGameEventFlagGalaxyOpen */
        if (dx.on && !strcmp(demos[dx.cur].h->name, "ギャラクシー移動")) dx_end(); /* endDemo */
        seq_galaxy_move(SEQ_SCENARIO_SELECT, gal, -1, -1, 0); /* requestStartScenarioSelect */
        menu_wipe(0, -1, 0xFFFFFF);
        g->n = 2;
        printf("surprised galaxy %d: to %s's scenario select%c", in->row, gal, 10);
    }
}
/* the scene's start (game_init): the classes dead from their init */
static void st_init(const Scene *s)
{
    st_mapboard = 0;
    for (u32 i = 0; i < s->nobjs; i++) {
        const char *n = s->types + 32 * s->objs[i].type;
        if (st_kind_name(n) == ST_SURP && s->objs[i].sw[SW_APPEAR] < 0) { /* SurprisedGalaxy::init */
            char f[48];
            snprintf(f, sizeof f, "Appear%s", st_surp_galaxy(n));
            if (gd_flag_depends(f, "SpecialStarGreenAll")) row_dead[i] = !gd_flag_is_on("SpecialStarGreenAll"); /* isGalaxyAppearGreenDriver */
            else row_dead[i] = !gd_flag_is_on("ViewCompleteMarioAndLuigi"); /* isOnGameEventFlagViewCompleteEndingMarioAndLuigi */
#ifdef ST_SURP_TEST
            row_dead[i] = 0;
#endif
        }
        /* AstroMapBoard.cpp: makeActorDead unless isButlerMapAppear (hasGrandStar(2)); initAfterPlacement's
         * changeModelDataTexAll(MapDummy, getGalaxyMapResTIMG): the galaxy map captured at the first frame (game_draw) */
        if (!strcmp(n, "AstroMapBoard")) { row_dead[i] = !st_grand(2); if (!row_dead[i]) st_mapboard = 1; }
#ifdef ST_MAPBOARD_TEST /* XF=-DST_MAPBOARD_TEST (AstroGalaxy): the board shown without the grand star */
        if (!strcmp(n, "AstroMapBoard")) { row_dead[i] = 0; st_mapboard = 1; }
#endif
        /* AstroChildRoom (AstroMapObj.cpp: moreInlines' AstroDomeEntrance / AstroLibrary / AstroChildRoom / AstroParking):
         * drawn as the generic instance; ponytail: its Dead / Alive / Revival / Open looks are update_astro_objs' ponytail
         * (one model per object fits the Observatory's pack) */
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
/* LavaBallRisingPlanetLava.cpp after Petari: WaitSmall 120 -> ScaleUp (1 -> 1.1 over 240) -> WaitBig 120 -> ScaleDown
 * (1.1 -> 1 over 240) -> WaitSmall; SE_AT_LV_MAGMA_WIND_RISING always, SE_OJ_LV_LAVA_MOVE_RISING while it scales.
 * n: 0 WaitSmall, 1 ScaleUp, 2 WaitBig, 3 ScaleDown */
static void st_lavaball(Inst *in, StSt *g)
{
    const Row *r = &sc->objs[in->row];
    g->lv[0] = se_level(SE_AT_LV_MAGMA_WIND_RISING, &in->m[12], g->lv[0]);
    float k = g->n == 0 ? 1.0f : g->n == 2 ? 1.1f : 0.0f;
    if (g->n == 1 || g->n == 3) {
        g->lv[1] = se_level(SE_OJ_LV_LAVA_MOVE_RISING, &in->m[12], g->lv[1]);
        float t = (float)g->st / 240.0f; /* calcNerveValue(240, a, b) */
        k = g->n == 1 ? 1.0f + 0.1f * t : 1.1f - 0.1f * t;
        if (g->st >= 240) { g->n = (g->n + 1) & 3; g->st = -1; }
    } else if (g->st >= 120) { g->n++; g->st = -1; } /* !isLessStep(120) */
    float s[3] = {r->scale[0] * k, r->scale[1] * k, r->scale[2] * k};
    mat_translate_rot_scale(in->m, r->pos, r->rot, s);
    in->s = s[0];
}
/* TimerMoveWall.cpp after Petari (TrialBubbleTimerMoveWallA): Wait; its SW_B coming on (listenStageSwitchOnB) ->
 * MoveFront: arg0 frames (300) at arg1 / arg0 (1000 / 300) a frame along its front (TRIBBL_T_MVWALL_ST, LV_..MV) ->
 * MoveBack the same way back -> TRIBBL_T_MVWALL_ED, Wait. n: 0 Wait, 1 front, 2 back; off its place's offset */
static void st_tmwall(Inst *in, StSt *g)
{
    const Row *r = &sc->objs[in->row];
    int timer = r->arg[0] != -1 ? r->arg[0] : 300;
    float len = r->arg[1] != -1 ? (float)r->arg[1] : 1000.0f;
    int swb = r->sw[SW_B] >= 0 && sw_on(r, SW_B);
    if (g->n == 0 && swb && !g->on) { g->n = 1; g->st = 0; printf("timer move wall %d: move%c", in->row, 10); }
    g->on = swb;
    if (g->n) {
        V3 x, z;
        row_axes(r, &x, &z);
        if (g->st == 0) se_start(SE_OJ_TRIBBL_T_MVWALL_ST, &in->m[12]);
        g->lv[0] = se_level(SE_OJ_LV_TRIBBL_T_MVWALL_MV, &in->m[12], g->lv[0]);
        g->off = vadd(g->off, vmul(vnorm(z), (g->n == 1 ? 1.0f : -1.0f) * len / timer));
        if (g->st + 1 >= timer) { /* isStep(mTimer): the velocity zeroed */
            if (g->n == 1) { g->n = 2; g->st = -1; }
            else { se_start(SE_OJ_TRIBBL_T_MVWALL_ED, &in->m[12]); g->n = 0; g->off = v3(0, 0, 0); }
        }
    }
    mat_translate_rot_scale(in->m, r->pos, r->rot, r->scale);
    in->m[12] += g->off.x; in->m[13] += g->off.y; in->m[14] += g->off.z;
}
/* RailMoveObj.cpp after Petari, its objinfo placements (DeathPromenadeMovePartsSpuareA; RailDemoMoveObj:
 * SeaBottomBigDoorA / B): MapPartsRailMover on its rail (game.c update_railers: the rail points' speeds and stops,
 * StageEffect's start / moving / stop SEs), MoveConditionType unconditional (no mappartsinfo columns), its SW_B the
 * start (initCaseUseSwitchB: Wait until listenStageSwitchOnB; mp_running). RailDemoMoveObj::tryStartMove: its
 * DemoCtrlBase first (tryStartDemo(its name), startActorCameraNoTarget, Demo's camera shaking: DemoParamCommonDataTable
 * 0.1 / 2.2, pad rumble 中 each frame; at its camera's last frame end: endDemo, endActorCamera, stopShakingCamera).
 * ponytail: tryStartDemo's scene pause for the actors not cast (no demo named after an object in the port's demo
 * executor: Mario keeps control while its camera runs) */
static const char *const st_rail_objs[] = {"DeathPromenadeMovePartsSpuareA", "SeaBottomBigDoorA", "SeaBottomBigDoorB"};
static int st_rail_obj(const char *n)
{
    for (u32 k = 0; k < sizeof st_rail_objs / sizeof *st_rail_objs; k++) if (!strcmp(n, st_rail_objs[k])) return 1 + (k > 0);
    return 0;
}
static int st_rail_go(const Inst *in) /* update_railers: may its rail mover run (RailDemoMoveObj: its demo's camera) */
{
    const char *n = sc->types + 32 * sc->objs[in->row].type;
    if (st_rail_obj(n) != 2) return 1;
    StSt *g = st_get(in->row);
    if (g->n == 0) { /* tryStartMove: DemoCtrlBase::tryStart */
        g->n = 1; g->st = 0; g->on = 0;
        if (row_camset[in->row] != -1) {
            V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), front = vnorm(v3(in->m[8], in->m[9], in->m[10]));
            g->on = camera_actor_frames(n, "", (unsigned short)row_camset[in->row]);
            camera_actor_start(n, "", (unsigned short)row_camset[in->row], up, front, v3(in->m[12], in->m[13], in->m[14]));
        }
        camera_shake_infinity(g, 0.1f, 2.2f); /* MR::Demo::isExistCameraShaking */
        printf("rail demo move obj %d: demo (%d frames)%c", in->row, g->on, 10);
    }
    if (g->n == 1) { /* DemoCtrlBase::update: the rumble, its end at the camera's frames */
        pad_rumble(RUMBLE_MIDDLE);
        if (g->st >= g->on) { camera_event_stop(); camera_shake_stop(g); g->n = 2; printf("rail demo move obj %d: demo end%c", in->row, 10); }
        g->st++;
    }
    return 1;
}
/* ManholeCover.cpp after Petari (KoopaShipManholeCover): Wait; a hip drop on it (isMsgPlayerHipDropFloor) in Wait or
 * Rattle: Open (Rotation clip, its collision off until the clip stops, then Wait); Mario's spin storm range (his spin
 * within its sensor 150 + 600, the spin's first 15 frames: isMsgSpinStormRange) in Wait: Rattle (Vibration clip,
 * SE_OJ_MANHOLE_RATTLE; past sRattleTime 45 with the clip stopped, Wait). n: 0 Wait, 1 Open, 2 Rattle.
 * Mario's upper punch (isMsgPlayerUpperPunch) in Wait or Rattle opens it too (st_manhole_upper) */
static int st_manhole_upper(int i)
{
    Inst *in = &insts[i];
    StSt *g = st_get(in->row);
    if (g->n != 0 && g->n != 2) return 0;
    if (in->kcl) g->kcl = in->kcl;
    g->n = 1; g->st = 0; obj_play(in, "rotation", 0, 0); in->kcl = NULL; /* invalidateCollisionParts */
    printf("manhole cover %d: open (upper punch)%c", in->row, 10);
    return 1;
}
static void st_manhole(Inst *in, StSt *g, int i)
{
    if (in->kcl) g->kcl = in->kcl;
    V3 p = v3(in->m[12], in->m[13], in->m[14]), mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    if ((g->n == 0 || g->n == 2) && hip_hit && hip_inst == i) {
        g->n = 1; g->st = 0; obj_play(in, "rotation", 0, 0); in->kcl = NULL; /* invalidateCollisionParts */
        printf("manhole cover %d: open%c", in->row, 10);
    } else if (g->n == 0 && mario->spin_timer > 0 && mario->spin_t < 15 && vlen(vsub(mc, p)) < 150.0f + 600.0f) {
        g->n = 2; g->st = 0; obj_play(in, "vibration", 0, 0); se_start(SE_OJ_MANHOLE_RATTLE, &in->m[12]);
        printf("manhole cover %d: rattle%c", in->row, 10);
    }
    int stopped = !in->anim || in->anim->done;
    if (g->n == 1) {
        in->kcl = NULL;
        if (g->st > 0 && stopped) { in->kcl = g->kcl; g->n = 0; } /* validateCollisionParts */
    } else if (g->n == 2 && g->st > 45 && stopped) g->n = 0;
}
/* LargeChain.cpp / LargeChainParts.cpp after Petari: arg0 / sPartsLength 200 parts (LargeChain, its collision), part i
 * at its place + (0, 200 i, 0) with its rotation (the row's own instance is part 0), a LargeChainFixPoint (slice.py
 * EXTRA) on part 0 (固定鎖 下) and one 200 over the last with its rotation + (180, 90, 0) (固定鎖 上); useStageSwitchSleep.
 * Its demo's action (registerDemoActionNerve: st_cast_hook): Break: every sBreakInterval 5 frames the next part
 * breaks (the lower fixed point with the first, the upper with the last: LargeChainParts::kill: its Break effect,
 * SE_OJ_LARGE_CHAIN_BREAK), at the last the chain is killed. StSt: n 1 Break, on the parts broken */
static void st_chain_rebind(const Scene *s) /* game_rebind_: its parts as instances (the unbroken ones) */
{
    for (u32 q = 0; q < s->nobjs && ninsts + 32 < insts_cap; q++) {
        const Row *r = &s->objs[q];
        if (strcmp(s->types + 32 * r->type, "LargeChain") || !row_shown((int)q)) continue;
        const Model *ml = scene_model(s, "LargeChain"), *mf = scene_model(s, "LargeChainFixPoint");
        int n = r->arg[0] > 0 ? (int)(r->arg[0] / 200.0f) : 0, broken = st_get((int)q)->on;
        for (int k = 0; k <= n; k++) { /* k 0: the lower fixed point, 1 .. n - 1 parts, n: the upper fixed point */
            const Model *md = k == 0 || k == n ? mf : ml;
            if (!md || (k < n ? k : n - 1) < broken || (k == 0 && broken > 0)) continue;
            float pos[3] = {r->pos[0], r->pos[1] + 200.0f * (k == n ? n - 1 : k), r->pos[2]}, rot[3] = {r->rot[0], r->rot[1], r->rot[2]};
            if (k == n) { pos[1] += 200.0f; rot[1] += 90.0f; rot[0] += 180.0f; }
            Inst *in = &insts[ninsts++];
            memset(in, 0, sizeof *in);
            in->md = md; in->row = (int)q; in->zone = r->zone; in->kmove = in->kparent = -1;
            in->kcl = scene_kcl(s, md == mf ? "LargeChainFixPoint" : "LargeChain");
            mat_translate_rot_scale(in->m, pos, rot, r->scale);
            in->s = r->scale[0]; in->reach = md->radius * in->s;
        }
    }
}
static int st_chain(Inst *in, StSt *g) /* 1: rebound */
{
    if (in->md != scene_model(sc, "LargeChain") || in->m[13] != sc->objs[in->row].pos[1]) return 0; /* part 0 (the row's own) runs it */
    if (g->on > 0) { in->invisible = 1; in->kcl = NULL; }
    if (g->n != 1 || g->st % 5) return 0;
    int n = sc->objs[in->row].arg[0] > 0 ? (int)(sc->objs[in->row].arg[0] / 200.0f) : 0, step = g->st / 5;
    float m[16];
    memcpy(m, in->m, 64);
    m[13] += 200.0f * step;
    fx_named("LargeChain", "Break", m, NULL, g); /* LargeChainParts::kill */
    se_start(SE_OJ_LARGE_CHAIN_BREAK, &m[12]);
    g->on = step + 1;
    printf("large chain %d: part %d broken%c", in->row, step, 10);
    if (step >= n - 1) { g->n = 2; row_kill(in->row); }
    g->st++;
    game_rebind();
    return 1;
}
static int st_cast_hook(int row, int type, int part) /* dx_cast_hook: the demo actions of these classes */
{
    (void)part;
    if (type == 2 && !strcmp(sc->types + 32 * sc->objs[row].type, "SunkenShip")) { /* SimpleBreakableObj: isDemoCast:
        * tryRegisterDemoActionFunctor(setStateBreak) */
        const Inst *in = row_inst(row);
        if (in && !row_dead[row]) { /* startBreak: the riddle SE (arg4 1, a riddle type stop), tryStartBreak: its StageEffect stop SE */
            printf("sunken ship %d: break (demo)%c", row, 10);
            if (sc->objs[row].arg[4] == 1) se_start(SE_SY_READ_RIDDLE_S, NULL);
            se_start(SE_OJ_SUNKEN_SHIP_BREAK, &in->m[12]);
            break_inst(in, 1); game_rebind();
        }
        return 1;
    }
    if (type == 3 && st_kind(sc->objs[row].type) == ST_CHAIN) { st_get(row)->n = 1; st_get(row)->st = 0; printf("large chain %d: break%c", row, 10); return 1; }
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
                                                         * (st_mirror_pass) */
        case ST_MIRROR: /* SimpleMirrorObj: its MirrorCamera's plane (setMirrorMapInfo); drawn by st_mirror_draw */
            in->invisible = 1;
            if (st_mir.at != sc || st_mir.inst != i) {
                float n[3], p[3];
                st_mir.at = sc; st_mir.inst = i;
                st_mir.ok = render_mirror_plane(in->md, in->m, n, p);
                st_mir.n = v3(n[0], n[1], n[2]); st_mir.p = v3(p[0], p[1], p[2]);
                printf("mirror: row %d plane %d (%.2f %.2f %.2f) at %.0f %.0f %.0f" "\n", in->row, st_mir.ok, n[0], n[1], n[2], p[0], p[1], p[2]);
            }
            break;
        case ST_UFOKC: /* UFOKinokoUnderConstruction::init: UFOKinokoLandingAstro's model (and collision) once
                        * isUFOKinokoUnderConstruction is over (EventKinopioExplorerOrganize on) */
            if (organize) {
                const Model *md = scene_model(sc, "UFOKinokoLandingAstro");
                if (md && in->md != md) { in->md = md; in->kcl = scene_kcl(sc, "UFOKinokoLandingAstro"); in->reach = md->radius * in->s; }
            }
            break;
        case ST_SHELLTOWER: st_shelltower(in, g); break;
        case ST_SURP: st_surp(in, g); break;
        case ST_LAVABALL: st_lavaball(in, g); break;
        case ST_TMWALL: st_tmwall(in, g); break;
        case ST_MANHOLE: st_manhole(in, g, i); break;
        case ST_CHAIN: if (st_chain(in, g)) return; break;
        case ST_BIGCOL: st_column(in, g); break;
        case ST_FLEX: st_flex(in, g, i); break;
        case ST_TEXSW: { /* SimpleMapObj::init: TexChange (BTK) at arg1 (setupInitInfoTextureChangeArg1; -1 none),
                          * SimpleTextureSwitchChangeObj::changeTexture on its SW_A's rise: frame 1 for good (slice.py
                          * BRK_STEPS: "<class>~<frame>") */
            const Row *r = &sc->objs[in->row];
#ifdef ST_TEXSW_TEST /* (with ST_VISIT): every picture's SW_A on at frame 300 */
            if (game_frame == 300 && r->sw[SW_A] >= 0) { sw_set(r, SW_A, 1); printf("texsw: row %d SW_A on%c", in->row, 10); }
#endif
            if (r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) g->on = 0;
            if (r->sw[SW_A] >= 0 && sw_on(r, SW_A) && !g->on) { g->on = 1; g->t2 = 1; }
            int fr = g->t2 ? 1 : r->arg[1];
            if (fr >= 0) {
                char n[40];
                snprintf(n, sizeof n, "%s~%d", sc->types + 32 * r->type, fr);
                const Model *md = scene_model(sc, n);
                if (md) in->md = md;
            }
            break;
        }
        case ST_TRIPROP: /* SeaBottomTriplePropeller::exeWait: its BCK (the class's clip), the level SE at each propeller
                          * joint (AudSeKeeper); its PropellerCap / Propeller collisions on the root and Propeller1-3
                          * (slice.py KCL_PARTS, JOINT_KCL_ONLY). ponytail: its demo's Break (kill) not registered */
            if (in->anim && in->anim->skin == in->md->skin && !in->invisible)
                for (int j = 1; j < 4 && j < (int)in->md->skin->njoints; j++) {
                    float jm[16];
                    mat_mul(jm, in->m, in->anim->world[j]);
                    g->lv[j - 1] = se_level(SE_OJ_LV_O_TRIPLE_PROP_MOVE, &jm[12], g->lv[j - 1]);
                }
            break;
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

/* BallBeamer.cpp after Petari (with RingBeam.cpp's rings, mFollowGravity _9c on): placed 50 under its row along world
 * Y; Wait (Wait clip) until Mario is within 3000 (isNearPlayer), then setupAttack (no SW_A, or SW_A on): Attack 360
 * frames: each 120 the charge level SE from 75 to 118, the Charge effect at 75, at 119 Sign, SE_EM_BALLBEAMER_ROT_HEAD
 * and the first of its 12 rings not out appears; Inter 120, then Attack again while SW_A is on (syncSwitchOnA's _90),
 * else Wait. Its SW_B: Vanish, killed with its rings. A ring (JumpBeamerBeam, its Spread clip at radius / 2000) starts
 * 75 up its Y along its Z; each frame its point _c0 goes arg0 (12) along the front kept square to the gravity ahead
 * (makeMtxUpFront), the ring's centre the foot of _c0 on the beamer's Y axis, its radius their distance (2000 at
 * most), arg1 (530) frames; Mario's sensor 5 off its circle (square to its Y) is shocked (EnemyAttackElectric); its
 * move SE at the circle's point nearest the camera. The beamer's body (120, 20 down its Y) pushes Mario, star pieces
 * bounce off it (isMsgStarPieceReflect); its collision the Head KCL at the Head joint's init pose (slice.py KCL_JOINT).
 * ponytail: no Erase BRK fade, no bloom model, no ring shadows (JumpBeamerBeamShadow / RingBeamShadowDrawer), no
 * shadow volume, no light control (the engine's shared lighting) */
#define ST_BB_MAX 8
#define ST_BB_RINGS 12
enum { BBM_WAIT, BBM_ATTACK, BBM_INTER };
typedef struct { int on, st; float rad; V3 a8, c0, ec; u32 lv; } StBbRing;
typedef struct { int row, nrv, st, on90, swa, swb; u32 lv; StBbRing b[ST_BB_RINGS]; } StBb;
static StBb st_bbs[ST_BB_MAX];
static int st_nbbs;
static const Scene *st_bb_sc;
static StBb *st_bb(int row)
{
    if (st_bb_sc != sc) { st_nbbs = 0; st_bb_sc = sc; }
    for (int k = 0; k < st_nbbs; k++) if (st_bbs[k].row == row) return &st_bbs[k];
    if (st_nbbs == ST_BB_MAX) return NULL;
    StBb *o = &st_bbs[st_nbbs++];
    memset(o, 0, sizeof *o);
    o->row = row; o->nrv = BBM_WAIT; o->st = -1;
    return o;
}
static void st_bb_nrv(StBb *o, int n) { o->nrv = n; o->st = -1; }
static void st_bb_setup_attack(StBb *o, const Row *r) /* setupAttack */
{
    if (r->sw[SW_A] < 0 || sw_on(r, SW_A)) { if (o->nrv != BBM_ATTACK) st_bb_nrv(o, BBM_ATTACK); }
}
static void st_bb_place(Inst *in) /* its matrix: the row's, 50 down world Y */
{
    const Row *r = &sc->objs[in->row];
    float p[3] = {r->pos[0], r->pos[1] - 50.0f, r->pos[2]};
    mat_translate_rot_scale(in->m, p, r->rot, r->scale);
}
static void st_bb_ring(Inst *in, StBb *o, StBbRing *b) /* RingBeam::exeSpread, attackSensor */
{
    const Row *r = &sc->objs[in->row];
    V3 bp = v3(in->m[12], in->m[13], in->m[14]), y = vnorm(v3(in->m[4], in->m[5], in->m[6]));
    float speed = r->arg[0] != -1 ? (float)r->arg[0] : 12.0f;
    int life = r->arg[1] != -1 ? r->arg[1] : 530;
    if (b->st == 0) {
        if (!star_demo) se_start(SE_EM_JUMPRING_APPEAR, &b->c0.x);
        b->a8 = vnorm(v3(in->m[8], in->m[9], in->m[10])); /* extractMtxZDir */
        b->c0 = vadd(bp, vmul(y, 75.0f));
    }
    V3 up = up_at(vadd(b->c0, vmul(b->a8, speed)), y); /* -_b4 */
    V3 f = vtangent(b->a8, up);
    if (vlen(f) > 1e-6f) b->a8 = vnorm(f); /* makeMtxUpFront(-_b4, _a8): its Z */
    b->ec = vadd(bp, vmul(y, vdot(vsub(b->c0, bp), y))); /* calcPerpendicFootToLine */
    b->c0 = vadd(b->c0, vmul(b->a8, speed));
    b->rad = vlen(vsub(b->c0, b->ec));
    if (b->rad > 2000.0f) b->rad = 2000.0f; /* setRadius */
    if (!star_demo) {
        V3 ce = vsub(cam_pos, b->ec), sp = b->ec;
        if (vlen(ce) > 1e-3f) {
            V3 h = vtangent(ce, y);
            float k = 1.0f - fabsf(vdot(y, vnorm(ce)));
            if (vlen(h) > 1e-6f) sp = vadd(b->ec, vmul(vnorm(h), b->rad * k));
        }
        b->lv = se_level(SE_EM_LV_RINGBEAM_MOVE, &sp.x, b->lv);
    }
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), d = vsub(mc, b->ec);
    if (vlen(d) < b->rad + HIT_BODY_R && !mario->hurt_timer && !mario_hidden) { /* the sensors meet */
        V3 h = vtangent(d, y);
        if (vlen(h) > 1e-6f && vlen(vsub(vmul(vnorm(h), b->rad), d)) <= 5.0f + HIT_BODY_R) {
            if (mario_attack(ATK_ELECTRIC, b->ec)) printf("ball beamer %d: ring shocks Mario%c", o->row, 10);
        }
    }
    if (++b->st > life) b->on = 0; /* isGreaterEqualStep(mLife): kill */
}
static void st_bbeamers_update(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        const Row *r = &sc->objs[in->row];
        if (row_dead[in->row] || strcmp(sc->types + 32 * r->type, "BallBeamer")) continue;
        StBb *o = st_bb(in->row);
        if (!o) continue;
        st_bb_place(in);
        int swa = r->sw[SW_A] >= 0 && sw_on(r, SW_A), swb = r->sw[SW_B] >= 0 && sw_on(r, SW_B);
        if (swa && !o->swa) { o->on90 = 1; st_bb_setup_attack(o, r); } /* syncSwitchOnA */
        if (!swa && o->swa) o->on90 = 0;                                /* syncSwitchOffA */
        o->swa = swa;
        if (swb && !o->swb) { /* syncSwitchOnB: Vanish, kill (and its rings) */
            fx_named("BallBeamer", "Vanish", in->m, in->anim ? (const float (*)[16])in->anim->world : NULL, NULL);
            for (int b = 0; b < ST_BB_RINGS; b++) o->b[b].on = 0;
            o->swb = 1;
            printf("ball beamer %d: vanish%c", in->row, 10);
            row_kill(in->row);
            continue;
        }
        o->swb = swb;
        int st = ++o->st;
        switch (o->nrv) {
        case BBM_WAIT:
            if (st == 0) obj_play(in, "Wait", 1, 0);
            if (vlen(vsub(mario->pos, v3(in->m[12], in->m[13], in->m[14]))) < 3000.0f) st_bb_setup_attack(o, r);
            break;
        case BBM_ATTACK:
            if (st >= 360) { st_bb_nrv(o, BBM_INTER); break; }
            if (st % 120 >= 75 && st % 120 < 119) o->lv = se_level(SE_EM_LV_RINGBEAM_CHARGE, &in->m[12], o->lv);
            if (st % 120 == 75) fx_named("BallBeamer", "Charge", in->m, in->anim ? (const float (*)[16])in->anim->world : NULL, NULL);
            if (st % 120 == 119) {
                obj_play(in, "Sign", 0, 0);
                se_start(SE_EM_BALLBEAMER_ROT_HEAD, &in->m[12]);
                for (int b = 0; b < ST_BB_RINGS; b++) /* tryAttack */
                    if (!o->b[b].on) { memset(&o->b[b], 0, sizeof o->b[b]); o->b[b].on = 1; printf("ball beamer %d: ring %d%c", in->row, b, 10); break; }
            }
            break;
        case BBM_INTER:
            if (st >= 120) st_bb_nrv(o, o->on90 ? BBM_ATTACK : BBM_WAIT);
            break;
        }
        for (int b = 0; b < ST_BB_RINGS; b++) if (o->b[b].on) st_bb_ring(in, o, &o->b[b]);
        /* attackSensor: its body pushes Mario (sendMsgPush) */
        V3 y = vnorm(v3(in->m[4], in->m[5], in->m[6])), body = vsub(v3(in->m[12], in->m[13], in->m[14]), vmul(y, 20.0f));
        V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), out = vtangent(vsub(mc, body), mario->up);
        float l = vlen(out), want = 120.0f + HIT_BODY_R * 0.6f;
        if (vlen(vsub(mc, body)) < 120.0f + HIT_BODY_R && l > 1e-3f && l < want) mario->pos = vadd(mario->pos, vmul(out, (want - l) / l));
    }
}
static int st_shot(V3 p) /* a star piece: BallBeamer's body reflects it (receiveMsgPlayerAttack) */
{
    for (int k = 0; k < st_nbbs && st_bb_sc == sc; k++) {
        const Inst *in = row_inst(st_bbs[k].row);
        if (!in || row_dead[in->row]) continue;
        V3 body = vsub(v3(in->m[12], in->m[13], in->m[14]), vmul(vnorm(v3(in->m[4], in->m[5], in->m[6])), 20.0f));
        if (vlen(vsub(p, body)) < 120.0f + 30.0f) return 1;
    }
    return 0;
}
static void st_bbeamers_draw(void)
{
    if (!m_rbeam || !m_rbeam->skin || st_bb_sc != sc) return;
    for (int k = 0; k < st_nbbs; k++) {
        StBb *o = &st_bbs[k];
        const Row *r = &sc->objs[o->row];
        if (!row_shown(o->row)) continue;
        Anim *a = NULL;
        for (int b = 0; b < ST_BB_RINGS; b++) {
            const StBbRing *q = &o->b[b];
            if (!q->on) continue;
            if (!a && !(a = obj_anim(16 * (int)sc->nobjs + o->row, m_rbeam, "JumpBeamerBeam"))) return; /* (one pose drawn for each ring in turn) */
            if (a->clip_id < 0 || strcmp(sc->clips[a->clip_id].name, "jumpbeamerbeam/spread")) anim_start(a, "jumpbeamerbeam/spread", 0);
            a->speed = 0.0f; a->frame = q->rad / 2000.0f * (float)anim_length(a); /* setBckFrameAndStop */
            anim_update(a);
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
            m[12] = q->ec.x; m[13] = q->ec.y; m[14] = q->ec.z;
            draw_skin(m_rbeam, m, a);
        }
    }
}

/* SubmarineVolcanoBigColumn.cpp after Petari (SubmarineVolcanoBigColumn; SubmarineVolcanoSmallColumn is placed
 * nowhere): Wait; SkeletalFishBossBattleDirector::tryColumnCollision: the boss head's body sensor (400) striking its
 * collision (checkStrikeBall) sends it an enemy attack: Break: startRumbleWithShakeCameraWeak 強 / 弱 within 800 / any,
 * its model hidden, its collision off, its break model (<name>Break, slice.py EXTRA) out playing Break,
 * SE_OJ_SMN_VOLC_COL_L_BREAK; killed once that clip stops. StSt n: 0 Wait, 1 Break */
static const Model *st_col_break(void) { return scene_model(sc, "SubmarineVolcanoBigColumnBreak"); }
static void st_column_strike(V3 c, float r) /* tryColumnCollision */
{
    Hit h[8];
    int n = kcl_query(c, r, h, 8);
    for (int j = 0; j < n; j++) {
        if (h[j].inst < 0) continue;
        Inst *in = &insts[h[j].inst];
        if (strcmp(sc->types + 32 * sc->objs[in->row].type, "SubmarineVolcanoBigColumn")) continue;
        StSt *g = st_get(in->row);
        if (g->n) continue; /* receiveMsgEnemyAttack: Wait only */
        g->n = 1; g->st = 0;
        rumble_shake_weak(v3(in->m[12], in->m[13], in->m[14]), RUMBLE_STRONG, RUMBLE_WEAK, 800.0f, 3.4e38f);
        in->invisible = 1; in->kcl = NULL; /* hideModel, invalidateHitSensors, invalidateCollisionParts */
        se_start(SE_OJ_SMN_VOLC_COL_L_BREAK, &in->m[12]);
        printf("submarine volcano big column %d: break%c", in->row, 10);
        return; /* (one a frame) */
    }
}
static void st_column(Inst *in, StSt *g)
{
    if (g->n != 1) return;
    in->invisible = 1; in->kcl = NULL; /* (kept over a rebind) */
    const Model *md = st_col_break();
    Anim *a = md && md->skin ? obj_anim(16 * (int)sc->nobjs + in->row, md, "SubmarineVolcanoBigColumnBreak") : NULL;
    if (a && g->st == 0) anim_play(a, "submarinevolcanobigcolumnbreak/break", 1.0f, 0, 0); /* startBck(mBreakModel, Break) */
    else if (a) anim_update(a);
    if (!a || (g->st > 0 && a->done)) { g->n = 2; row_kill(in->row); printf("submarine volcano big column %d: gone%c", in->row, 10); }
}
static void st_columns_draw(void)
{
    const Model *md = st_col_break();
    if (!md || !md->skin) return;
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (st_kind(sc->objs[in->row].type) != ST_BIGCOL || st_get(in->row)->n != 1) continue;
        Anim *a = obj_anim(16 * (int)sc->nobjs + in->row, md, "SubmarineVolcanoBigColumnBreak");
        if (a) draw_skin(md, in->m, a); /* (its base matrix, the column's) */
    }
}

/* FlexibleSphere.cpp after Petari (FlexibleSandPlanetPartsA): idle until its SW_A is on (Wind effect), or without
 * one until Mario stands on it; then it grows by arg2 / 10000 (0.002) a frame up to arg0 / 10000 (1) and shrinks to
 * arg1 / 10000 (0.1), its collision scaled with it, its move level SE; at either end a quake: 60 frames of
 * shakeCameraInfinity(0.2, 3) and the earthquake level SE within 4000, a numb rumble with Mario on it. arg3 1: it
 * starts shrinking with a countdown (the timer SEs every 60 frames by what is left: 0..120 QUASI_0, ..600 _1, else
 * _2) and at its end vanishes (TIMER_A_0, VANISH, its dead switch). StSt: k its scale, n 0 grow / 1 shrink, on
 * enabled, t2 the shake timer, t3 mTimer. ponytail: its texture scaled 0.05 / scale + 0.95 (the shape callback's
 * texture matrix), the move SE's level parameter */
static void st_flex(Inst *in, StSt *g, int i)
{
    const Row *r = &sc->objs[in->row];
    float mx = r->arg[0] != -1 ? r->arg[0] / 10000.0f : 1.0f, mn = r->arg[1] != -1 ? r->arg[1] / 10000.0f : 0.1f;
    float rate = r->arg[2] != -1 ? r->arg[2] / 10000.0f : 0.002f;
    int vanish = r->arg[3] == 1;
    if (g->k == 0.0f) { /* init: mScale (the row's) */
        g->k = r->scale[0];
        if (vanish) { g->n = 1; g->t3 = (int)((g->k - mn) / rate); }
    }
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    float d = vlen(vsub(mario->pos, p));
    if (g->t2) {
        if (d < 4000.0f) g->lv[1] = se_level(SE_AT_LV_EARTHQUAKE, NULL, g->lv[1]);
        if (--g->t2 == 0) camera_shake_stop(g);
        goto place;
    }
    if (!g->on) {
        if (r->sw[SW_A] >= 0) {
            if (!sw_on(r, SW_A)) goto place;
            g->on = 1;
            fx_named("FlexibleSandPlanetPartsA", "Wind", in->m, NULL, NULL);
        } else if (mario_ground == i) g->on = 1; /* isActorOnPlayer */
        else goto place;
        printf("flexible sphere %d: on%c", in->row, 10);
    }
    g->lv[0] = se_level(SE_OJ_LV_FLEX_SPHERE_MOVE, &in->m[12], g->lv[0]);
    int quake = 0;
    if (g->n == 0) {
        if (g->k < mx) g->k += rate;
        else { g->n = 1; quake = 1; }
    } else if (g->k > mn) {
        g->k -= rate;
        if (vanish && g->t3 > 0) {
            if (!miss_running() && !star_demo && g->t3 % 60 == 0)
                se_start(g->t3 <= 120 ? SE_SY_TIMER_A_QUASI_0 : g->t3 <= 600 ? SE_SY_TIMER_A_1 : SE_SY_TIMER_A_2, NULL);
            g->t3--;
        }
    } else if (vanish) { /* kill: its dead switch, Vanish */
        se_start(SE_SY_TIMER_A_0, NULL);
        se_start(SE_OJ_FLEX_SPHERE_VANISH, &in->m[12]);
        fx_named("FlexibleSandPlanetPartsA", "Vanish", in->m, NULL, NULL);
        if (r->sw[SW_DEAD] >= 0) sw_set((Row *)r, SW_DEAD, 1);
        printf("flexible sphere %d: vanish%c", in->row, 10);
        row_kill(in->row);
        return;
    } else { g->n = 0; quake = 1; }
    if (quake && !g->t2) {
        g->t2 = 60;
        if (d < 4000.0f) camera_shake_infinity(g, 0.2f, 3.0f);
        if (mario_ground == i) pad_rumble(RUMBLE_NUMB); /* マリオ[しびれ] */
        printf("flexible sphere %d: quake at scale %d / 1000%c", in->row, (int)(g->k * 1000.0f), 10);
    }
place: {
        float old[16], s[3] = {g->k, g->k, g->k};
        memcpy(old, in->m, sizeof old);
        mat_translate_rot_scale(in->m, r->pos, r->rot, s);
        in->s = g->k; in->reach = in->md ? in->md->radius * g->k : in->reach;
        if (mario_ground == i) carry(old, in); /* (Mario on it moves with its surface) */
    }
}

/* SpinningBox.cpp after Petari (SpinningBox; IceBox is Mario's freeze's, not placed): its own body (binder 140 at 80
 * up, gravity at its place, its collision the instance's KCL moved with it and left out of its own binding), the
 * quaternion mRotate as its axes (its up turned 0.125 a frame to the ground's normal, then about it by -mRotateSpeed).
 * Fall (step > 30 on the ground: Wait), Wait (on the ground its gravity the ground's for the damping), Sliding (a kick
 * from Mario's side: Mario's front at 17, -0.08 a frame, rotate 0.02; walls / slopes over cos 0.4 bounce it at half
 * speed, BOUND SE), Spinning (a spin: Mario's front at 25, -0.01 a frame, rotate 0.5 -> 0.1, the SPIN SE each 2 of
 * turn, Blur; a wall or slope breaks it); both bind to the ground below within 65 (forceBindOnGround) and Wait under
 * 0.1. Spinning into Mario on the ground from its side: strong (over 10) / flip weak attack and its reflection
 * (mSlideSpeed = 0.5: Petari's bug kept), else a push; Sliding pushes him; both blow the walkers they touch
 * (sendMsgToEnemyAttackBlow) and break on one. Mario standing on it: Wait. A hip drop, a death area, water: kill:
 * BREAK, STAR_PIECE_BURST, its 3 star pieces 50 up (10 / 40), Break.
 * ponytail: no 2P pointer (Pointed), no box-against-box reflection (AttributeGroupReflectSpinningBox), no electric
 * floor (isBindedDamageElectric), no shadow volume, no black hole, the slide SE's level parameter, no map object
 * sensors (only walkers) hit */
#define ST_SBOX_MAX 8
enum { SB_FALL, SB_WAIT, SB_SLIDING, SB_SPINNING };
typedef struct { int row, nrv, st, ground, onground, wall; float rot, slide, spin; V3 pos, vel, grav, gn, wn, slidedir, qy, qz; u32 lv; } StSb;
static StSb st_sbs[ST_SBOX_MAX];
static int st_nsbs;
static const Scene *st_sb_sc;
static StSb *st_sb(const Inst *in)
{
    if (st_sb_sc != sc) { st_nsbs = 0; st_sb_sc = sc; }
    for (int k = 0; k < st_nsbs; k++) if (st_sbs[k].row == in->row) return &st_sbs[k];
    if (st_nsbs == ST_SBOX_MAX) return NULL;
    StSb *b = &st_sbs[st_nsbs++];
    memset(b, 0, sizeof *b);
    const Row *r = &sc->objs[in->row];
    b->row = in->row; b->pos = row_pos(r); b->nrv = SB_FALL; b->st = -1;
    b->qy = vnorm(v3(in->m[4], in->m[5], in->m[6])); b->qz = vnorm(v3(in->m[8], in->m[9], in->m[10]));
    b->slidedir = v3(1, 0, 0);
    b->grav = vmul(up_at(b->pos, b->qy), -1.0f);
    return b;
}
static void st_sb_nrv(StSb *b, int n) { b->nrv = n; b->st = -1; }
static void st_sb_rebound(StSb *b) /* reboundVelocityFromCollision(0, 0, 1) */
{
    if (b->ground && vdot(b->vel, b->gn) < 0) b->vel = vsub(b->vel, vmul(b->gn, vdot(b->vel, b->gn)));
    if (b->wall && vdot(b->vel, b->wn) < 0) b->vel = vsub(b->vel, vmul(b->wn, vdot(b->vel, b->wn)));
}
static void st_sb_damp(StSb *b, V3 grav, float g, float gd, float ad, float vd, float hmin) /* applyVelocityDampAndGravity (as js_damp) */
{
    if (!b->ground) b->vel = vadd(b->vel, vmul(grav, g));
    V3 v = vmul(grav, vdot(grav, b->vel)), h = vsub(b->vel, v);
    h = vmul(h, b->ground ? gd : ad);
    if (vdot(v, grav) < 0) v = vmul(v, vd);
    b->vel = vadd(h, v);
    if (b->ground && vlen(vtangent(b->vel, b->gn)) < hmin) b->vel = vmul(b->gn, vdot(b->gn, b->vel));
}
static void st_sb_force_bind(StSb *b) /* forceBindOnGround(65, 0.5): the ground below within 65, half the way */
{
    V3 hp, n;
    int inst;
    if (kcl_line(b->pos, vadd(b->pos, vmul(b->grav, 65.0f)), &hp, &n, &inst) && insts[inst].row != b->row)
        b->pos = vadd(b->pos, vmul(vsub(hp, b->pos), 0.5f));
}
static int st_sb_wall_or_slope(const StSb *b, V3 *n) /* isBindedWallOrSlopeGround(cos 0.4) */
{
    if (b->wall) { *n = b->wn; return 1; }
    if (b->ground && vdot(b->gn, vmul(b->grav, -1.0f)) < 0.4f) { *n = b->gn; return 1; }
    return 0;
}
static void st_sb_kill(Inst *in, StSb *b) /* SpinningBox::kill */
{
    se_start(SE_OJ_SPIN_BOX_BREAK, &b->pos.x);
    se_start(SE_OJ_STAR_PIECE_BURST, &b->pos.x);
    launch_pieces(vsub(b->pos, vmul(b->grav, 50.0f)), vmul(b->grav, -1.0f), 3, 10.0f, 40.0f);
    fx_named("SpinningBox", "Break", in->m, NULL, NULL);
    printf("spinning box %d: break%c", in->row, 10);
    row_kill(in->row);
}
static int st_sb_side(const StSb *b, V3 *up) /* isPlayerExistSide(0, 0.25) */
{
    *up = vmul(b->grav, -1.0f);
    V3 d = vsub(mario->pos, b->pos);
    return vlen(d) < 1e-3f || fabsf(vdot(*up, vnorm(d))) < 0.25f;
}
static void st_sboxes_update(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        const Row *r = &sc->objs[in->row];
        if (row_dead[in->row] || strcmp(sc->types + 32 * r->type, "SpinningBox")) continue;
        StSb *b = st_sb(in);
        if (!b) continue;
        int st = ++b->st, onp = mario_ground == i;
        float s = r->scale[0], dep;
        V3 up = vmul(b->grav, -1.0f), body = vadd(b->pos, vmul(b->qy, -40.0f * s)), mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), side;
        /* control */
        if (in_death(b->pos) || in_water(b->pos, up, &dep)) { st_sb_kill(in, b); continue; }
        V3 sd = b->slidedir, norm;
        if (b->ground) { norm = b->gn; float k = vdot(norm, b->grav); if (fabsf(k) > 1e-6f) sd = vadd(sd, vmul(b->grav, -vdot(norm, sd) / k)); } /* calcPlanarProjectedVec */
        else { norm = up; sd = vtangent(sd, norm); }
        if (vlen(sd) > 1e-6f) b->slidedir = vnorm(sd);
        V3 ax = vcross(b->qy, norm); /* rotY.setRotate(up, norm, 0.125) */
        if (vlen(ax) > 1e-6f) {
            float c = vdot(b->qy, norm), a = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 0.125f;
            ax = vnorm(ax); b->qy = vnorm(rot_axis(b->qy, ax, a)); b->qz = vnorm(rot_axis(b->qz, ax, a));
        }
        b->qz = vnorm(vtangent(rot_axis(b->qz, b->qy, -b->rot), b->qy)); /* setRotate(up, -mRotateSpeed) */
        if (onp && b->nrv != SB_WAIT) st_sb_nrv(b, SB_WAIT), st = ++b->st;
        /* Mario's messages: receiveMsgPlayerAttack, receiveOtherMsg (kick) */
        if (hip_hit && hip_inst == i) { st_sb_kill(in, b); continue; } /* isMsgPlayerHipDrop */
        int msg = mario_offence(body, 160.0f * s, b->grav);
        if (msg == MSG_HITALL && b->nrv != SB_FALL && !onp) { /* isMsgPlayerHitAll: Spinning */
            b->slidedir = vtangent(mario->face, b->grav);
            if (vlen(b->slidedir) > 1e-6f) b->slidedir = vnorm(b->slidedir);
            b->rot = 0.5f; b->slide = 25.0f;
            se_start(SE_PM_SPIN_HIT, &b->pos.x); /* startSpinHitSound (CS), startBlowHitSound */
            m_punch(b, body, 0);
            st_sb_nrv(b, SB_SPINNING); st = ++b->st;
            printf("spinning box %d: spinning%c", in->row, 10);
        } else if (b->nrv != SB_SPINNING && !(b->nrv == SB_SLIDING && st < 30) && !onp && st_sb_side(b, &side) && mario_kick(body, 160.0f * s)) {
            b->slidedir = mario->face; b->slide = 17.0f; /* isMsgPlayerKick: Sliding */
            mario_kicked();
            st_sb_nrv(b, SB_SLIDING); st = ++b->st;
            printf("spinning box %d: kicked%c", in->row, 10);
        }
        /* its nerve */
        V3 n;
        switch (b->nrv) {
        case SB_FALL:
            st_sb_rebound(b);
            st_sb_damp(b, b->grav, 3.0f, 0.6f, 0.6f, 0.6f, 10.0f);
            if (b->ground && st > 30) st_sb_nrv(b, SB_WAIT);
            break;
        case SB_WAIT: {
            if (st == 0) { b->onground = 0; b->rot = 0.0f; }
            V3 g = b->grav;
            if (b->ground) { b->onground = 1; g = vmul(b->gn, -1.0f); }
            st_sb_rebound(b);
            if (b->onground) st_sb_damp(b, g, 3.0f, 0.0f, 0.0f, 0.6f, 10.0f);
            else st_sb_damp(b, g, 3.0f, 0.6f, 0.6f, 0.6f, 10.0f);
            break;
        }
        case SB_SLIDING: case SB_SPINNING: {
            int spin = b->nrv == SB_SPINNING;
            if (spin) {
                fx_named("SpinningBox", "Blur", in->m, NULL, b); /* emitEffect Blur, deleted at its end */
                if (st == 0) b->spin = 0.0f;
                b->rot = fminf(fmaxf(b->rot - 0.001f, 0.1f), 0.5f);
                b->slide = fminf(fmaxf(b->slide - 0.01f, 0.0f), 25.0f);
                b->spin += b->rot;
                if (b->spin > 2.0f) { while (b->spin > 2.0f) b->spin -= 2.0f; se_start(SE_OJ_SPIN_BOX_SPIN, &b->pos.x); }
            } else {
                if (st == 0) b->rot = 0.02f;
                b->rot = 0.02f; /* clamp(rot - 0.01, 0.02, 0.02) */
                b->slide = fminf(fmaxf(b->slide - 0.08f, 0.0f), 17.0f);
            }
            b->lv = se_level(SE_OJ_LV_SPIN_BOX_SLIDE, &b->pos.x, b->lv);
            if (b->slide <= 0.1f) { st_sb_nrv(b, SB_WAIT); break; }
            st_sb_rebound(b);
            b->vel = vmul(b->slidedir, b->slide);
            if (!b->ground) st_sb_force_bind(b);
            if (st_sb_wall_or_slope(b, &n)) {
                if (spin) { st_sb_kill(in, b); goto next; }
                se_start(SE_OJ_SPIN_BOX_BOUND, &b->pos.x);
                b->slidedir = vsub(b->slidedir, vmul(n, 2.0f * vdot(b->slidedir, n))); /* calcReflectionVector(1, 0) */
                b->slide *= 0.5f;
                if (vlen(b->slidedir) > 1e-6f) b->slidedir = vnorm(b->slidedir);
            }
            /* attackSensor: Mario from its side, walkers */
            if (!onp && st_sb_side(b, &side) && vlen(vsub(mc, body)) < 160.0f * s + HIT_BODY_R) {
                if (spin) {
                    if (mario->spin_timer <= 0 && mario->grounded) {
                        int hit = !mario->hurt_timer && mario_attack(b->slide > 10.0f ? ATK_STRONG : ATK_FLIP_WEAK, body);
                        if (hit) { /* hitReflection */
                            V3 d = vsub(body, mc);
                            if (vlen(d) > 1e-6f) { d = vnorm(d); b->slidedir = vsub(b->slidedir, vmul(d, 2.0f * vdot(b->slidedir, d))); b->slide = 0.5f; if (vlen(b->slidedir) > 1e-6f) b->slidedir = vnorm(b->slidedir); }
                        } else mario_push_out(body, 160.0f * s);
                    }
                } else mario_push_out(body, 160.0f * s);
            }
            for (int w = 0; w < nkuribo; w++) { /* sendMsgToEnemyAttackBlow */
                Kuribo *e = &kuribo[w];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || vlen(vsub(vadd(e->pos, vmul(e->up, 50.0f)), body)) > 160.0f * s + 60.0f) continue;
                if (walker_blow(e, body) == 1) { st_sb_kill(in, b); goto next; }
            }
            break;
        }
        }
        { /* the binder (its own collision left out), its base matrix, Mario carried */
            float old[16];
            memcpy(old, in->m, sizeof old);
            V3 c = vadd(b->pos, vadd(b->vel, vmul(up, 80.0f)));
            int keep = kcl_off_row;
            kcl_off_row = in->row;
            b->ground = collide_n(&c, up, 140.0f, &b->gn, &b->wn, &b->wall);
            kcl_off_row = keep;
            if (!b->ground) b->gn = up;
            b->pos = vsub(c, vmul(up, 80.0f));
            b->grav = vmul(up_at(b->pos, up), -1.0f);
            V3 x = vcross(b->qy, b->qz);
            float m[16] = {x.x * s, x.y * s, x.z * s, 0, b->qy.x * s, b->qy.y * s, b->qy.z * s, 0, b->qz.x * s, b->qz.y * s, b->qz.z * s, 0, b->pos.x, b->pos.y, b->pos.z, 1};
            memcpy(in->m, m, sizeof m);
            if (onp) carry(old, in);
        }
    next:;
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
#ifdef ST_VISIT /* plain build XF='-DST_VISIT=Class [-DST_VISIT_K=k] [-DST_VISIT_D=d]': from frame 60 Mario held 300
                * (and from 400 to 430) over its k-th row, the camera looking at it from d (1500) along its Z and 600 up */
#ifndef ST_VISIT_K
#define ST_VISIT_K 0
#endif
#ifndef ST_VISIT_D
#define ST_VISIT_D 1500.0f
#endif
#define ST_STR_(x) #x
#define ST_STR(x) ST_STR_(x)
static void st_visit(void)
{
    const char *cls = ST_STR(ST_VISIT);
    if (game_frame < 60) return;
    int k = 0;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (strcmp(sc->types + 32 * r->type, cls) || k++ != ST_VISIT_K) continue;
        V3 x, z, p = row_pos(r);
        row_axes(r, &x, &z);
        V3 up = vnorm(vcross(z, x));
        if (game_frame < 90 || (game_frame >= 400 && game_frame < 430)) { mario->pos = vadd(p, vadd(vmul(up, 300.0f), vmul(z, 400.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* (again after a launch star's start) */
        camera_set_override(1, vadd(p, vadd(vmul(vnorm(z), ST_VISIT_D), vmul(up, 600.0f))), p, up, 45.0f);
        if (game_frame % 120 == 60) printf("visit f %u: %s row %u shown %d dead %d%c", game_frame, cls, i, row_shown((int)i), row_dead[i], 10);
        return;
    }
}
#endif
#ifdef ST_SB_TEST /* plain build XF=-DST_SB_TEST, ReverseKingdomGalaxy/1: from 400 Mario 190 off the first SpinningBox
                    * facing it, at 460 a spin (Spinning), the camera on it; its nerve and place logged */
static void st_sb_test(void)
{
    if (game_frame < 400) return;
    for (int k = 0; k < st_nsbs && st_sb_sc == sc; k++) {
        StSb *b = &st_sbs[k];
        V3 up = vmul(b->grav, -1.0f), f = vnorm(vtangent(v3(1, 0, 0), up));
        static int t0;
        if (!t0) t0 = (int)game_frame;
        if ((int)game_frame < t0 + 40) { mario->pos = vadd(vadd(b->pos, vmul(f, 190.0f)), vmul(up, 50.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->face = vmul(f, -1.0f); }
        if ((int)game_frame == t0 + 60) mario->spin_timer = SPIN_TIME;
        camera_set_override(1, vadd(b->pos, vadd(vmul(up, 1200.0f), vmul(f, 2500.0f))), b->pos, up, 45.0f);
        if (game_frame % 30 == 0) printf("sb test f %u: row %d nrv %d slide %d at (%d %d %d) ground %d%c", game_frame, b->row, b->nrv, (int)b->slide, (int)b->pos.x, (int)b->pos.y, (int)b->pos.z, b->ground, 10);
        return;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* none bound yet: Mario to the first one's row (its zone streams in) */
        if (!strcmp(sc->types + 32 * sc->objs[i].type, "SpinningBox")) {
            if (game_frame % 60 == 0) printf("sb test f %u: to row %u shown %d%c", game_frame, i, row_shown((int)i), 10);
            mario->pos = vadd(row_pos(&sc->objs[i]), v3(0, 400.0f, 0)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            break;
        }
}
#endif
#ifdef ST_BB_TEST /* plain build XF=-DST_BB_TEST, OceanFloaterLandGalaxy/1: from frame 60 Mario dropped 800 along the
                    * first BallBeamer's Z and 300 up (then left there), the camera behind him on the beamer */
static void st_bb_test(void)
{
    static int put;
    if (game_frame < 60) return;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (strcmp(sc->types + 32 * r->type, "BallBeamer")) continue;
        V3 x, z, p = row_pos(r);
        row_axes(r, &x, &z);
        z = vnorm(z);
        V3 y = vnorm(vcross(z, x));
        if (game_frame >= 400 && vlen(vsub(mario->pos, p)) > 1500.0f && put++ < 20) { mario->pos = vadd(p, vadd(vmul(z, 800.0f), vmul(y, 300.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        camera_set_override(1, vadd(p, vadd(vmul(z, 2200.0f), vmul(y, 900.0f))), p, y, 45.0f);
        if (game_frame % 60 == 0) {
            StBb *o = st_bb((int)i);
            printf("bb test f %u: Mario %d off, nrv %d st %d, rings%c", game_frame, (int)vlen(vsub(mario->pos, p)), o ? o->nrv : -1, o ? o->st : -1, 10);
            for (int b = 0; o && b < ST_BB_RINGS; b++) if (o->b[b].on) printf("  ring %d st %d rad %d%c", b, o->b[b].st, (int)o->b[b].rad, 10);
        }
        return;
    }
}
#endif
#ifdef ST_COL_TEST /* plain build XF=-DST_COL_TEST, SkullSharkGalaxy/1: from frame 300 the camera on the first
                     * SubmarineVolcanoBigColumn, at 400 a 400 sphere struck at its place (tryColumnCollision's) */
static void st_col_test(void)
{
    if (game_frame < 300) return;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (strcmp(sc->types + 32 * r->type, "SubmarineVolcanoBigColumn")) continue;
        V3 p = row_pos(r);
        camera_set_override(1, vadd(p, v3(3000.0f, 1500.0f, 0.0f)), vadd(p, v3(0, 1000.0f, 0)), v3(0, 1, 0), 45.0f);
        if (game_frame == 400) { printf("col test: strike row %u (%d %d %d)%c", i, (int)p.x, (int)p.y, (int)p.z, 10); for (int h = 0; h <= 3000; h += 300) for (int x = 0; x <= 2000; x += 200) st_column_strike(vadd(p, v3((float)x, (float)h, 0)), 400.0f); }
        if (game_frame % 30 == 0) printf("col test f %u: row %u dead %d n %d%c", game_frame, i, row_dead[i], st_get((int)i)->n, 10);
        return;
    }
}
#endif
static void st_update(void)
{
#ifdef ST_MAPBOARD_TEST /* the camera on the board, 2500 in front of it */
    {
        int t = scene_type(sc, "AstroMapBoard");
        for (u32 i = 0; t >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == t) {
                V3 x, z, p = row_pos(&sc->objs[i]);
                row_axes(&sc->objs[i], &x, &z);
                camera_set_override(1, vadd(p, vmul(z, 2500.0f)), p, vcross(z, x), 45.0f);
                break;
            }
    }
#endif
#ifdef ST_COL_TEST
    st_col_test();
#endif
#ifdef ST_BB_TEST
    st_bb_test();
#endif
#ifdef ST_SB_TEST
    st_sb_test();
#endif
#ifdef ST_VISIT
    st_visit();
#endif
#ifdef ST_TWALL_TEST
    st_twall_test();
#endif
    st_flags_update();
    st_objs_update();
    st_bbeamers_update();
    st_sboxes_update();
#ifdef ST_FLAG_TEST
    st_flag_test();
#endif
}
/* SimpleMirrorObj (MirrorReflectionTwinFallLake) and MirrorCamera / GameScene::drawMirror after Petari: while the
 * player is in a MirrorArea, the frame first drawn by the mirror camera (the camera's eye, target and up reflected in
 * the mirror's plane, re-squared as MirrorCamera::updateViewMtx) with the MirrorMapObj models (SimpleMirrorReflectionObj:
 * MirrorModelTwinFallLake; a Sky with arg0 0: its MirrorReflectionModel, at the reflected eye), captured
 * (CaptureScreenIndirect), the depth cleared by the frame's own render_begin; the mirror's surface then textured with
 * it through the mirror camera (MirrorReflectionMtxSetter), its material's alpha. Outside the area: its own model.
 * ponytail: the picture at 320 x 224 (16-bit); fillScreen's blue left to the frame's clear */
static int st_mirror_on(void)
{
#ifdef MIRROR_OFF /* test builds: the surface's own model, for a comparison */
    return 0;
#endif
    return st_mir.at == sc && st_mir.ok && st_mir.inst < ninsts && area_obj("MirrorAreaCube", mario->pos) >= 0; /* isPlayerInAreaObj("MirrorArea") */
}
static void st_mirror_pass(V3 ce, V3 cw, V3 cu)
{
    if (!st_mirror_on()) return;
    V3 n = st_mir.n;
    V3 e = vsub(ce, vmul(n, 2.0f * vdot(n, vsub(ce, st_mir.p)))), t = vsub(cw, vmul(n, 2.0f * vdot(n, vsub(cw, st_mir.p))));
    V3 u = vsub(cu, vmul(n, 2.0f * vdot(n, cu)));
    float eye[3] = {e.x, e.y, e.z}, target[3] = {t.x, t.y, t.z}, up[3] = {u.x, u.y, u.z};
    render_begin(eye, target, up);
    int sky = 0;
    for (int i = 0; i < ninsts; i++) { /* Sky::init: arg0 0 makes its 鏡内モデル (the same model, the sky's matrix) */
        const Inst *in = &insts[i];
        if (!in->sky || in->md->radius <= 0 || sc->objs[in->row].arg[0] != 0) continue;
        float cm[16], k = 108000.0f / (in->md->radius * in->s);
        memcpy(cm, in->m, 64);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= k;
        cm[12] = e.x; cm[13] = e.y; cm[14] = e.z;
        inst_draw(in, in->md, cm);
        sky = 1;
    }
    if (sky) render_clear_depth();
    for (int i = 0; i < ninsts; i++)
        if (st_kind(sc->objs[insts[i].row].type) == ST_MIRRORMODEL && row_shown(insts[i].row)) inst_draw(&insts[i], insts[i].md, insts[i].m);
    render_mirror_capture();
}
static void st_mirror_draw(void)
{
    if (st_mir.at != sc || st_mir.inst < 0 || st_mir.inst >= ninsts) return;
    const Inst *in = &insts[st_mir.inst];
    if (!row_shown(in->row)) return;
    if (!render_mirror_surface(in->md, in->m)) inst_draw(in, in->md, in->m);
}
static void st_draw(void)
{
    st_mirror_draw();
    st_flags_draw();
    st_bbeamers_draw();
    st_columns_draw();
}
