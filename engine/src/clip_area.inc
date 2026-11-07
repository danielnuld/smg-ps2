/* ClipArea / ClipAreaHolder / ClipAreaShape / ClipAreaMovable / ClipAreaDrop(Holder) / ClipAreaDropLaser /
 * ClipFieldSwitch / FallOutFieldDraw / SimpleClipPartsObj after Petari (Game/MapObj). #included by game.c (ca_*).
 *
 * ClipAreaHolder (64 members, mIsActive): the clip areas (ClipAreaSphere / CenterBox / BottomBox / TopCone /
 * BottomCone: ClipAreaMovable, its rail and rotator the map part railer's / rotor's: in->m) and the drops.
 * isInArea: a member not dead whose shape (in its base matrix, over its scale) holds the point. FallOutFieldDraw's
 * appear switch (de)activates the holder; with none it is active. ClipFieldSwitch::control: Mario in a clip area
 * turns its SW_A on and SW_B off, else the reverse. SimpleClipPartsObj (DarkHopperPlanet*, DarknessRoomPlanet,
 * DarkHopperRotateStepA, KoopaVS2Parts*Clip*): its collision only while its SW_A is on (when it has one); with
 * DarknessRoomDoor (arg0 -1) the ClippedMapParts draw buffer, drawn first (GameScene::draw3D), then the clip areas'
 * volumes and FallOutFieldDraw's fill: black at the far depth outside every volume (render_clip_*).
 * ClipAreaDropLaser: Move along its rail at arg0 (20), its last 64 places drawn as a line (0x0040F080 added, width
 * 20); a point with arg0 > 0 passed: Splash, SE_OJ_DROP_LASER_SPLASH and a ClipAreaDrop of that radius there (eased
 * out to it over 15 frames, eased in-out to 0 from 60 to 240, then dead); its appear switch off: Wait (the line
 * shrinks a point a frame). ponytail: the sky drawn before (Petari: after, where the fill left the far depth) goes
 * black; the edge colour added, not lerped (render.c clip_edge); ClipAreaCollisionFilter's Teresa / KameckBeam exceptions not ported */
enum { CA_SPHERE = 1, CA_CENTERBOX, CA_BOTTOMBOX, CA_TOPCONE, CA_BOTTOMCONE };
static int ca_shape_of(const char *name)
{
    return !strcmp(name, "ClipAreaSphere") ? CA_SPHERE : !strcmp(name, "ClipAreaCenterBox") ? CA_CENTERBOX :
           !strcmp(name, "ClipAreaBottomBox") ? CA_BOTTOMBOX : !strcmp(name, "ClipAreaTopCone") ? CA_TOPCONE :
           !strcmp(name, "ClipAreaBottomCone") ? CA_BOTTOMCONE : 0;
}
/* SimpleClipPartsObj (NameObjFactory / PlanetMapCreator) 1; DarknessRoomDoorA 2 (ClippedMapParts with arg0 -1) */
static int ca_parts_of(const char *name)
{
    static const char *const parts[] = {"DarkHopperPlanetA", "DarkHopperPlanetB", "DarkHopperPlanetC", "DarkHopperPlanetD",
        "DarknessRoomPlanet", "DarkHopperRotateStepA", "KoopaVS2PartsRestClipC", "KoopaVS2PartsJoinedMoveClip",
        "KoopaVS2PartsSquareMoveClipA", "KoopaVS2PartsSquareMoveClipB"};
    for (u32 k = 0; k < sizeof parts / sizeof parts[0]; k++) if (!strcmp(name, parts[k])) return 1;
    return !strcmp(name, "DarknessRoomDoorA") ? 2 : 0;
}
static int ca_clipped(const Inst *in) /* drawn in the ClippedMapParts buffer */
{
    return in->clipp == 1 || (in->clipp == 2 && sc->objs[in->row].arg[0] == -1);
}
static int ca_solid(const Inst *in) /* kcl_each: SimpleClipPartsObj::control's validate / invalidate */
{
    const Row *r = &sc->objs[in->row];
    return in->clipp != 1 || r->sw[SW_A] < 0 || sw_on(r, SW_A);
}

#define CA_DROPS 32 /* sMaxClipAreaDrop */
#define CA_LASERS 8
static struct {
    const Scene *at;
    int fall, fall_t, sw_t, laser_t;             /* FallOutFieldDraw's row (-1 none), the class types */
    struct { int alive, step; V3 pos; float base, radius; } drop[CA_DROPS];
    struct { int row, nrv, step, count, skip; float ndraw, coord; V3 pts[64]; } laser[CA_LASERS];
    int nlaser;
} ca;
enum { CA_WAIT, CA_MOVE };
static u32 ca_cycles; /* ca_draw's EE time (CA_TEST) */

static void ca_reset(void)
{
    memset(&ca, 0, sizeof ca);
    ca.at = sc;
    ca.fall = -1;
    int ft = scene_type(sc, "FallOutFieldDraw"), lt = scene_type(sc, "ClipAreaDropLaser");
    ca.sw_t = scene_type(sc, "ClipFieldSwitch");
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type == ft && ca.fall < 0) ca.fall = (int)i;
        if ((int)r->type == lt && lt >= 0 && ca.nlaser < CA_LASERS) {
            typeof(&ca.laser[0]) L = &ca.laser[ca.nlaser++];
            L->row = (int)i; L->count = -1; L->skip = -1;
            L->nrv = r->sw[SW_APPEAR] >= 0 ? CA_WAIT : CA_MOVE; /* useStageSwitchReadAppear: Wait */
        }
    }
}
static int ca_holder(void) { return ca.fall >= 0 || ca.nlaser; } /* a clip area / FallOutFieldDraw placed */
static int ca_active(void) /* ClipAreaHolder::mIsActive */
{
    if (ca.fall < 0) return 1;
    const Row *r = &sc->objs[ca.fall];
    return r->sw[SW_APPEAR] < 0 || sw_on(r, SW_APPEAR);
}

/* ClipAreaShape*::isInArea on a point in the shape's space (over its scale); m: a margin (the camera test) */
static int ca_in_shape(int kind, V3 v, float m)
{
    switch (kind) {
    case CA_SPHERE: return vdot(v, v) < (500.0f + m) * (500.0f + m);
    case CA_CENTERBOX: return fabsf(v.x) < 500.0f + m && fabsf(v.y) < 500.0f + m && fabsf(v.z) < 500.0f + m;
    case CA_BOTTOMBOX: return fabsf(v.x) < 500.0f + m && v.y > -m && v.y < 1000.0f + m && fabsf(v.z) < 500.0f + m;
    default: { /* ClipAreaShapeCone: _8 500, _C 1000; Bottom: 1 - y / 1000 */
        float h = v.y / 1000.0f;
        if (h < 0.0f || h > 1.0f) return 0;
        if (kind == CA_BOTTOMCONE) h = 1.0f - h;
        return v.x * v.x + v.z * v.z < (h * 500.0f + m) * (h * 500.0f + m);
    }
    }
}
static V3 ca_local(const float *m, V3 p) /* inverse of an affine T R S: each axis over its length squared */
{
    V3 d = vsub(p, v3(m[12], m[13], m[14])), out;
    float *o = &out.x;
    for (int k = 0; k < 3; k++) {
        V3 a = v3(m[4 * k], m[4 * k + 1], m[4 * k + 2]);
        float l2 = vdot(a, a);
        o[k] = l2 > 1e-12f ? vdot(d, a) / l2 : 0.0f;
    }
    return out;
}
static void ca_drop_matrix(float *m, int k)
{
    float s = ca.drop[k].radius / 500.0f;
    mat_identity(m);
    m[0] = m[5] = m[10] = s;
    m[12] = ca.drop[k].pos.x; m[13] = ca.drop[k].pos.y; m[14] = ca.drop[k].pos.z;
}
/* MR::isInClipArea */
static int ca_in_area(V3 p)
{
    if (!ca_active()) return 0;
    FOR_KIND(i, clipa) {
        const Inst *in = &insts[i];
        if (!in->clipa || row_dead[in->row]) continue;
        if (ca_in_shape(in->clipa, ca_local(in->m, p), 0.0f)) return 1;
    }
    for (int k = 0; k < CA_DROPS; k++) {
        if (!ca.drop[k].alive || ca.drop[k].radius <= 0.0f) continue;
        if (vlen(vsub(p, ca.drop[k].pos)) < ca.drop[k].radius) return 1;
    }
    return 0;
}

/* MR::appearClipAreaDrop: a dead drop at pos, its base size */
static void ca_drop_appear(V3 pos, float base)
{
    for (int k = 0; k < CA_DROPS; k++) {
        if (ca.drop[k].alive) continue;
        ca.drop[k].alive = 1; ca.drop[k].step = 0; ca.drop[k].pos = pos; ca.drop[k].base = base; ca.drop[k].radius = 0.0f;
        return;
    }
}
/* ClipAreaDrop::exeWait: sExtendsEndTime 15, sShrinkStartTime 60, sShrinkEndTime 240 */
static void ca_drops_update(void)
{
    for (int k = 0; k < CA_DROPS; k++) {
        if (!ca.drop[k].alive) continue;
        int st = ca.drop[k].step;
        float base = ca.drop[k].base;
        if (st < 15) ca.drop[k].radius = base * sinf((float)st / 15.0f * 1.5707963f); /* calcNerveEaseOutValue */
        else {
            float t = st <= 60 ? 0.0f : st >= 240 ? 1.0f : (float)(st - 60) / 180.0f; /* calcNerveEaseInOutValue */
            ca.drop[k].radius = base + (0.0f - base) * (1.0f - cosf(t * 3.1415927f)) * 0.5f;
        }
        if (st > 240) ca.drop[k].alive = 0; /* isGreaterStep: kill */
        ca.drop[k].step++;
    }
}

/* ClipAreaDropLaser::exeMove / exeWait */
static void ca_lasers_update(void)
{
    for (int l = 0; l < ca.nlaser; l++) {
        typeof(&ca.laser[0]) L = &ca.laser[l];
        const Row *r = &sc->objs[L->row];
        int appear_valid = r->sw[SW_APPEAR] >= 0;
        u32 np = 0, nr = 0;
        int closed = 0;
        const float *pts = find_rail(scene_blob_g, r->zone, r->path, &np, &closed);
        const unsigned char *path = find_path(scene_blob_g, r->zone, r->path);
        if (!pts || !path || np < 2) continue;
        nr = *(const u32 *)(path + 12);
        const unsigned char *pinfo = path + 24 + 12 * np + 12 * nr; /* nr x {coord, arg0, arg5} */
        const float *raw = (const float *)(path + 24 + 12 * np);
        float len = nr ? *(const float *)(pinfo + 12 * (nr - 1)) : 0.0f;
        if (L->nrv == CA_WAIT) {
            if (L->ndraw > 0.0f) L->ndraw -= 1.0f;
            if (appear_valid && sw_on(r, SW_APPEAR)) { L->nrv = CA_MOVE; L->step = 0; }
            continue;
        }
        if (L->step == 0) { L->coord = 0.0f; L->ndraw = 0.0f; } /* moveCoordAndTransToRailStartPoint */
        L->step++;
        float speed = r->arg[0] != -1 ? (float)r->arg[0] : 20.0f, from = L->coord;
        L->coord += speed; /* moveCoordAndCheckPassPointNo */
        int point = -1;
        for (u32 i = 0; i < nr; i++) {
            float c = *(const float *)(pinfo + 12 * i);
            if (from < c && c <= L->coord) { point = (int)i; break; }
        }
        V3 pos = rail_at(pts, np, L->coord < len ? L->coord : len);
        if (++L->count >= 64) L->count -= 64; /* incrementDrawCount */
        if (L->ndraw < 64.0f) L->ndraw += 1.0f;
        if (L->coord >= len) { /* isRailReachedGoal */
            L->skip = L->count;
            point = (int)nr - 1;
            L->coord = 0.0f;
            pos = rail_at(pts, np, 0.0f);
        }
        L->pts[L->count] = pos;
        if (point >= 0) {
            V3 pp = v3(raw[3 * point], raw[3 * point + 1], raw[3 * point + 2]);
            int arg = *(const s32 *)(pinfo + 12 * point + 4);
            if (arg > 0) {
                float m[16];
                mat_identity(m); m[12] = pp.x; m[13] = pp.y; m[14] = pp.z;
                fx_named("ClipAreaDropLaser", "Splash", m, NULL, NULL); /* emitEffectHit */
                float sp[3] = {pp.x, pp.y, pp.z};
                se_start(SE_OJ_DROP_LASER_SPLASH, sp);
                ca_drop_appear(pp, (float)arg);
            }
        }
        if (appear_valid && !sw_on(r, SW_APPEAR)) { L->nrv = CA_WAIT; L->step = 0; }
    }
}

static void ca_update(void)
{
    if (ca.at != sc) ca_reset();
    if (!ca_holder() && ca.sw_t < 0) return;
    ca_drops_update();
    ca_lasers_update();
#ifdef CA_TEST /* XF=-DCA_TEST (plain): 60 frames after Mario is free, he and the camera go to clip area CA_TEST - 1
               * (1 the first), the camera 1500 off; the state logged every 60 frames */
    static int ct;
    if (game_test_free() && ++ct == 60) {
        int k = 0;
        FOR_KIND(i, clipa) if (insts[i].clipa && ++k == CA_TEST) {
            mario->pos = vadd(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), v3(0, 100, 0));
            camera_set_override(1, vadd(mario->pos, v3(0, 700, 1500)), mario->pos, v3(0, 1, 0), 45.0f);
            printf("clip test: Mario at area row %d" "\n", insts[i].row);
        }
    }
    if (ct && ct % 60 == 0) {
        int nd = 0;
        for (int k = 0; k < CA_DROPS; k++) nd += ca.drop[k].alive;
        printf("clip test: f %d active %d in %d drops %d lasers %d draw %u us" "\n", ct, ca_active(), ca_in_area(mario->pos), nd, ca.nlaser, ca_cycles / 295);
    }
#endif
    if (ca.sw_t < 0) return;
    int in = ca_in_area(mario->pos); /* ClipFieldSwitch::control (isInClipArea(player, 5)) */
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((int)r->type != ca.sw_t) continue;
        sw_set(r, SW_A, in);
        sw_set(r, SW_B, !in);
    }
}

/* the volume meshes in their shapes' spaces (outward winding): a sphere of 500 (16 x 10), a box (centre / bottom), a
 * cone (top: its apex at 0, bottom: at 1000) */
#define CA_SL 12
#define CA_ST 8
static float ca_sph[CA_SL * (CA_ST - 1) + 2][3], ca_box[2][8][3], ca_cone[2][CA_SL + 2][3];
static u16 ca_sph_ix[CA_SL * (CA_ST - 1) * 6], ca_box_ix[36], ca_cone_ix[CA_SL * 6];
static int ca_sph_n, ca_cone_n;
static void ca_orient(const float (*v)[3], u16 *t, V3 centre) /* a triangle turned to face away from the centre */
{
    V3 a = v3(v[t[0]][0], v[t[0]][1], v[t[0]][2]), b = v3(v[t[1]][0], v[t[1]][1], v[t[1]][2]), c = v3(v[t[2]][0], v[t[2]][1], v[t[2]][2]);
    V3 n = vcross(vsub(b, a), vsub(c, a)), g = vmul(vadd(vadd(a, b), c), 1.0f / 3.0f);
    if (vdot(n, vsub(g, centre)) < 0) { u16 x = t[1]; t[1] = t[2]; t[2] = x; }
}
static void ca_meshes(void)
{
    if (ca_sph_n) return;
    int nv = 0;
    ca_sph[nv][0] = 0; ca_sph[nv][1] = 500.0f; ca_sph[nv][2] = 0; nv++;
    for (int s = 1; s < CA_ST; s++)
        for (int k = 0; k < CA_SL; k++) {
            float th = 3.1415927f * s / CA_ST, ph = 6.2831853f * k / CA_SL;
            ca_sph[nv][0] = 500.0f * sinf(th) * cosf(ph); ca_sph[nv][1] = 500.0f * cosf(th); ca_sph[nv][2] = 500.0f * sinf(th) * sinf(ph);
            nv++;
        }
    ca_sph[nv][0] = 0; ca_sph[nv][1] = -500.0f; ca_sph[nv][2] = 0; nv++;
    int n = 0;
    for (int k = 0; k < CA_SL; k++) { /* the caps */
        u16 *t = &ca_sph_ix[3 * n++];
        t[0] = 0; t[1] = (u16)(1 + k); t[2] = (u16)(1 + (k + 1) % CA_SL);
        t = &ca_sph_ix[3 * n++];
        t[0] = (u16)(nv - 1); t[1] = (u16)(1 + (CA_ST - 2) * CA_SL + k); t[2] = (u16)(1 + (CA_ST - 2) * CA_SL + (k + 1) % CA_SL);
    }
    for (int s = 0; s + 1 < CA_ST - 1; s++)
        for (int k = 0; k < CA_SL; k++) {
            u16 a = (u16)(1 + s * CA_SL + k), b = (u16)(1 + s * CA_SL + (k + 1) % CA_SL);
            u16 c = (u16)(a + CA_SL), d = (u16)(b + CA_SL), *t = &ca_sph_ix[3 * n++];
            t[0] = a; t[1] = c; t[2] = b;
            t = &ca_sph_ix[3 * n++];
            t[0] = b; t[1] = c; t[2] = d;
        }
    for (int i = 0; i < n; i++) ca_orient((const float (*)[3])ca_sph, &ca_sph_ix[3 * i], v3(0, 0, 0));
    ca_sph_n = n;
    static const u16 faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (int bx = 0; bx < 2; bx++) {
        for (int i = 0; i < 8; i++) {
            ca_box[bx][i][0] = i & 1 ? 500.0f : -500.0f;
            ca_box[bx][i][1] = bx ? (i & 2 ? 1000.0f : 0.0f) : (i & 2 ? 500.0f : -500.0f);
            ca_box[bx][i][2] = i & 4 ? 500.0f : -500.0f;
        }
    }
    for (int f = 0; f < 6; f++) {
        u16 *t = &ca_box_ix[6 * f];
        t[0] = faces[f][0]; t[1] = faces[f][1]; t[2] = faces[f][2];
        t[3] = faces[f][0]; t[4] = faces[f][2]; t[5] = faces[f][3];
    }
    for (int i = 0; i < 12; i++) ca_orient((const float (*)[3])ca_box[0], &ca_box_ix[3 * i], v3(0, 0, 0));
    for (int c = 0; c < 2; c++) { /* 0 top cone: apex at y 0, its base (500) at 1000; 1 bottom: the reverse */
        float apex = c ? 1000.0f : 0.0f, base = c ? 0.0f : 1000.0f;
        ca_cone[c][0][0] = 0; ca_cone[c][0][1] = apex; ca_cone[c][0][2] = 0;
        ca_cone[c][1][0] = 0; ca_cone[c][1][1] = base; ca_cone[c][1][2] = 0;
        for (int k = 0; k < CA_SL; k++) {
            float ph = 6.2831853f * k / CA_SL;
            ca_cone[c][2 + k][0] = 500.0f * cosf(ph); ca_cone[c][2 + k][1] = base; ca_cone[c][2 + k][2] = 500.0f * sinf(ph);
        }
    }
    n = 0;
    for (int k = 0; k < CA_SL; k++) {
        u16 *t = &ca_cone_ix[3 * n++];
        t[0] = 0; t[1] = (u16)(2 + k); t[2] = (u16)(2 + (k + 1) % CA_SL);
        t = &ca_cone_ix[3 * n++];
        t[0] = 1; t[1] = (u16)(2 + (k + 1) % CA_SL); t[2] = (u16)(2 + k);
    }
    ca_cone_n = n;
}
/* one volume through matrix m into world space, both passes */
static void ca_volume(int kind, const float *m, V3 eye)
{
    static float w[CA_SL * (CA_ST - 1) + 2][3];
    const float (*v)[3];
    const u16 *ix;
    int nv, nt;
    if (kind == CA_SPHERE) { v = (const float (*)[3])ca_sph; nv = CA_SL * (CA_ST - 1) + 2; ix = ca_sph_ix; nt = ca_sph_n; }
    else if (kind == CA_CENTERBOX || kind == CA_BOTTOMBOX) { v = (const float (*)[3])ca_box[kind == CA_BOTTOMBOX]; nv = 8; ix = ca_box_ix; nt = 12; }
    else { v = (const float (*)[3])ca_cone[kind == CA_BOTTOMCONE]; nv = CA_SL + 2; ix = ca_cone_ix; nt = ca_cone_n; }
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++)
            w[i][k] = m[k] * v[i][0] + m[4 + k] * v[i][1] + m[8 + k] * v[i][2] + m[12 + k];
    /* off the view: no pixel of it (its bounding sphere, about the shape's middle) */
    float s = vlen(v3(m[0], m[1], m[2])), sy = vlen(v3(m[4], m[5], m[6])), sz = vlen(v3(m[8], m[9], m[10]));
    float smax = s > sy ? (s > sz ? s : sz) : (sy > sz ? sy : sz), cy = kind == CA_SPHERE || kind == CA_CENTERBOX ? 0.0f : 500.0f;
    float c[3] = {m[4] * cy + m[12], m[5] * cy + m[13], m[6] * cy + m[14]};
    if (!render_visible(c, (kind == CA_SPHERE ? 500.0f : 867.0f) * smax)) return;
    /* inside: the camera within the shape (or within the near plane's reach of it): its front pass the whole screen */
    int inside = ca_in_shape(kind, ca_local(m, eye), s > 1e-6f ? 100.0f / s : 0.0f);
    render_clip_volume((const float (*)[3])w, nv, ix, nt, inside);
}

/* GameScene::draw3D: the ClippedMapParts, ClipArea's volumes (nearest first), FallOutFieldDraw's fill */

static void ca_draw_(V3 eye);
static void ca_draw(V3 eye)
{
    u32 c0 = cycles();
    ca_draw_(eye);
    ca_cycles = cycles() - c0;
}
static void ca_draw_(V3 eye)
{
    if (ca.at != sc || !ca_holder()) return;
    render_xlu_now(1);
    FOR_KIND(i, clipp) {
        const Inst *in = &insts[i];
        if (!in->clipp || in->invisible || !ca_clipped(in) || !row_shown(in->row)) continue;
        inst_draw(in, in->md, in->m);
    }
    render_xlu_now(0);
    if (ca.fall < 0 || !ca_active()) return;
    ca_meshes();
    struct { float d; int kind, inst, drop; } vol[64 + CA_DROPS];
    int nvol = 0;
    FOR_KIND(i, clipa) {
        const Inst *in = &insts[i];
        if (!in->clipa || row_dead[in->row] || nvol >= 64) continue;
        vol[nvol].d = vlen(vsub(v3(in->m[12], in->m[13], in->m[14]), eye)); vol[nvol].kind = in->clipa; vol[nvol].inst = i; vol[nvol].drop = -1;
        nvol++;
    }
    for (int k = 0; k < CA_DROPS; k++) {
        if (!ca.drop[k].alive || ca.drop[k].radius <= 0.0f) continue;
        vol[nvol].d = vlen(vsub(ca.drop[k].pos, eye)); vol[nvol].kind = CA_SPHERE; vol[nvol].inst = -1; vol[nvol].drop = k;
        nvol++;
    }
    for (int a = 1; a < nvol; a++) /* nearest first (render.c's one-bit order) */
        for (int b = a; b > 0 && vol[b].d < vol[b - 1].d; b--) { typeof(vol[0]) x = vol[b]; vol[b] = vol[b - 1]; vol[b - 1] = x; }
    render_clip_begin();
    for (int k = 0; k < nvol; k++) {
        float dm[16];
        if (vol[k].drop >= 0) ca_drop_matrix(dm, vol[k].drop);
        ca_volume(vol[k].kind, vol[k].drop >= 0 ? dm : insts[vol[k].inst].m, eye);
    }
#ifndef CA_NOFILL
    render_clip_fill();
#endif
}

/* ClipAreaDropLaser::draw (DrawType_ClipAreaDropLaser): segments between its last places, but the one across its
 * restart; lines of width 20 (GX: 1/6 pixel) as quads facing the camera, (0x00, 0x40, 0xF0) x alpha 0x80 added */
static void ca_lasers_draw(V3 eye, float fovy)
{
    if (ca.at != sc) return;
    float px = tanf(fovy * 0.5f * 0.017453293f) / 224.0f * (20.0f / 6.0f); /* world size of the width at depth 1 */
    for (int l = 0; l < ca.nlaser; l++) {
        typeof(&ca.laser[0]) L = &ca.laser[l];
        float q[4 * 63][3];
        u16 ix[6 * 63];
        int nq = 0;
        for (int k = 1; k < L->ndraw && k < 64; k++) {
            int u1 = L->count - (k - 1), u2 = L->count - k;
            if (u1 < 0) u1 += 64;
            if (u2 < 0) u2 += 64;
            if (u1 == L->skip) continue;
            V3 a = L->pts[u1], b = L->pts[u2], mid = vmul(vadd(a, b), 0.5f), side = vcross(vsub(b, a), vsub(eye, mid));
            float sl = vlen(side);
            if (sl < 1e-6f) continue;
            side = vmul(side, 0.5f * px * vlen(vsub(eye, mid)) / sl);
            V3 c[4] = {vadd(a, side), vsub(a, side), vsub(b, side), vadd(b, side)};
            for (int j = 0; j < 4; j++) { q[4 * nq + j][0] = c[j].x; q[4 * nq + j][1] = c[j].y; q[4 * nq + j][2] = c[j].z; }
            u16 *t = &ix[6 * nq];
            t[0] = (u16)(4 * nq); t[1] = (u16)(4 * nq + 1); t[2] = (u16)(4 * nq + 2);
            t[3] = (u16)(4 * nq); t[4] = (u16)(4 * nq + 2); t[5] = (u16)(4 * nq + 3);
            nq++;
        }
        if (nq) render_mesh_c((const float (*)[3])q, NULL, 4 * nq, ix, 2 * nq, NO_TEX, 2, 0x40F04000u);
    }
}
