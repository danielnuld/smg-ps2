/* OceanRing.cpp / WaterPoint.cpp / OceanRingDrawer.cpp after Petari (#included by game.c after race.inc: or_*): the
 * ring-shaped water along its rail. initPoints: a segment every 200 (sPointIntervalLine, length / count apart), 15
 * points across each (171.42857 x the width rate apart, point arg1 / 12), the two outer ones eased (sEdgePointNum 2);
 * WaterPoint::initAfterPlacement's map check (400 down from 200 up: its wave rate and alpha); movement: the wave
 * angles -0.04 / -0.06 a frame, updatePoints near the player (30 + 8 segments, arg1, faded 3 at the ends), the level
 * SEs at the nearest points to the camera's watch point and the camera; isInWater / calcWaterInfo (depth, bottom,
 * wave, edge, stream: point arg0 / 100 x 10 along the rail) for MR::getWaterAreaObj / getWaterAreaInfo (game.c
 * water_obj / water_info kind 4, in_water); OceanRingDrawer: parts of about ten segments, near ones (13000) at the
 * points' waves, far ones flat, the whole ring by its edges while clipped; its texture Water.bti scrolled
 * (-0.003, -0.001 a frame). ponytail: Water.bti's two stages baked as one sample (T x T / 2, tools/jpa.py), the
 * screen texture term (C1 x the frame behind) as the frame less 1 - C1 (render.c blend 0x200 | FIX), no indirect
 * warp, no bloom drawer (OceanRingBloomDrawer), no OceanRingPipe (arg0 0), no WaterSurface.kcl (collision category 2: the ring's surface stands in for getFirstPolyOnLineToWaterSurface) */
#define OR_MAX 4
#define OR_STRIDE 15
typedef struct { V3 orig; float h, off; u8 alpha; } OrPt; /* WaterPoint: mOrigPos, mHeight, the wave's offset along up, mAlpha (0 unchecked) */
typedef struct { V3 c; float rad; int a, n, wrap; u32 fresh; } OrPart; /* OceanRingPartDrawer: mPosition, its first segment, count, loop end; fresh: the update count its points last moved */
typedef struct {
    int row, nseg, arg1, clipped, npart, c2;
    RmRail rail;
    OrPt *pt; V3 *up; OrPart *part;
    float *cum, (*trig)[4]; u32 frame; /* the rail's lengths to each of its polyline's points; a segment's sin / cos of
                                         * 0.0003 and 0.0025 x its coord on the rail (calcHeight); updates run */
    float th0, th1, wh1, wh2, near_c, tu, tv;
    V3 near_p, near_d, bmin, bmax, cmin, cmax, watch_p, cam_p;
    u32 lv1, lv2;
} ORing;
static ORing orings[OR_MAX];
static int norings;
static const Scene *or_scene;

static OrPt *or_pt(const ORing *o, int j, int seg) { return &o->pt[j + seg * OR_STRIDE]; } /* getPoint(int, int) */
static float or_rate(const ORing *o, float coord, int arg, float def) /* calcCurrentWidthRate (arg 1, 12) / FlowSpeedRate (0, 100) */
{
    const RmRail *rr = &o->rail;
    RmRail at = *rr;
    at.coord = coord; at.dir = 1;
    int n = rr_npoints(rr), k = rr_point_no(&at), nx = k + 1 >= n ? (rr->cl ? 0 : n - 1) : k + 1;
    float cc = rr_point_coord(rr, k), nc = rr_point_coord(rr, nx); /* calcDistanceToCurrentAndNextRailPoint */
    float cur = fabsf(cc) < 1e-6f ? coord : fabsf(coord - cc), next = fabsf(nc) < 1e-6f ? rr->len - coord : fabsf(nc - coord);
    float sum = cur + next;
    if (sum < 1.0f) return 1.0f;
    int a = rr_point_arg(rr, k, arg), b = rr_point_arg(rr, nx, arg); /* getCurrent / NextRailPointArgNoInit */
    float va = a == -1 ? def : (float)a, vb = b == -1 ? def : (float)b;
    return (va * next + vb * cur) / sum / def;
}
static V3 or_rail(const ORing *o, float coord, V3 *dir) /* calcRailPosAndDirectionAtCoord (nrail_at by a binary search) */
{
    const float *p = o->rail.pts;
    int n = (int)o->rail.np, segs = o->rail.cl ? n : n - 1, lo = 0, hi = segs - 1;
    coord = fminf(fmaxf(coord, 0.0f), o->cum[segs]);
    while (lo < hi) { int m = (lo + hi + 1) / 2; if (o->cum[m] <= coord) lo = m; else hi = m - 1; }
    int b = (lo + 1) % n;
    V3 a = v3(p[3 * lo], p[3 * lo + 1], p[3 * lo + 2]), ab = vsub(v3(p[3 * b], p[3 * b + 1], p[3 * b + 2]), a);
    float l = o->cum[lo + 1] - o->cum[lo];
    *dir = l > 0 ? vmul(ab, 1.0f / l) : v3(0, 0, 1);
    return vadd(a, vmul(ab, l > 0 ? fminf(coord - o->cum[lo], l) / l : 0));
}
static float or_foot(V3 *out, V3 p, V3 a, V3 b) /* MR::calcPerpendicFootToLine */
{
    V3 d = vsub(b, a);
    float r = (vdot(p, d) - vdot(a, d)) / vdot(d, d);
    *out = vadd(a, vmul(d, r));
    return r;
}
/* calcNearestPos: the nearest segment centre, the foot towards its nearer neighbour; its coord (200 a segment) */
static float or_nearest(const ORing *o, V3 p, V3 *near, V3 *dir, V3 *foot)
{
    int idx = 0;
    V3 d0 = vsub(or_pt(o, 7, 0)->orig, p);
    float best = vdot(d0, d0); /* (squared: the same order) */
    for (int i = 1; i < o->nseg; i++) {
        V3 di = vsub(or_pt(o, 7, i)->orig, p);
        float d = vdot(di, di);
        if (d < best) { best = d; idx = i; }
    }
    float c = 0.0f;
    V3 cp = or_pt(o, 7, idx)->orig;
    if (idx > 0 && idx < o->nseg - 1) {
        V3 pv = or_pt(o, 7, idx - 1)->orig, nx = or_pt(o, 7, idx + 1)->orig;
        if (vlen(vsub(pv, p)) < vlen(vsub(nx, p))) c = 200.0f * ((float)idx - or_foot(near, p, cp, pv));
        else c = 200.0f * ((float)idx + or_foot(near, p, cp, nx));
    } else *near = cp; /* (the coord stays 0 at the ends) */
    if (foot) { /* the foot on the cross line through near, clamped to the width (calcPerpendicFootToLineInside) */
        int s = (int)(c / 200.0f);
        V3 d, rp = or_rail(o, c, &d), up = o->up[s < 0 ? 0 : s >= o->nseg ? o->nseg - 1 : s], side = vcross(d, up); /* (the gravity at its segment's rail point, kept from initPoints) */
        (void)rp;
        float w = 1200.0f * or_rate(o, c, 1, 12.0f);
        V3 a = vsub(*near, vmul(side, w)), b = vadd(*near, vmul(side, w)), ab = vsub(b, a);
        float r = vdot(ab, ab) > 1e-6f ? (vdot(p, ab) - vdot(a, ab)) / vdot(ab, ab) : 0.0f;
        *foot = vadd(a, vmul(ab, fminf(fmaxf(r, 0.0f), 1.0f)));
    }
    if (dir) or_rail(o, c, dir); /* calcRailDirectionAtCoord */
    return c;
}
static float or_height(const ORing *o, const OrPt *pt, float across, float on) /* WaterPoint::calcHeight */
{
    return pt->h * (o->wh1 * sinf(o->th0 + 0.003f * across + 0.0003f * on) + o->wh2 * sinf(o->th1 + 0.0025f * on));
}
static int or_box(V3 p, V3 a, V3 b) { return p.x >= a.x && p.y >= a.y && p.z >= a.z && p.x <= b.x && p.y <= b.y && p.z <= b.z; }

static int or_find(V3 p) /* WaterAreaFunction::tryInOceanArea's rings: OceanRing::isInWater */
{
    for (int k = 0; or_scene == sc && k < norings; k++) {
        const ORing *o = &orings[k];
        if (!or_box(p, o->cmin, o->cmax)) continue;
        V3 near;
        float c = or_nearest(o, p, &near, NULL, NULL);
        if (vlen(vsub(near, p)) > 1200.0f * or_rate(o, c, 1, 12.0f)) continue;
        V3 grav = vmul(up_at(p, v3(0, 1, 0)), -1.0f);
        if (vdot(vsub(p, near), grav) < 0.0f) continue;
        return k;
    }
    return -1;
}
static int or_row(V3 p) { int k = or_find(p); return k < 0 ? -1 : orings[k].row; }
static int or_in_water(V3 p, V3 up, float *depth) /* isInWater, with its depth along up (calcWaterInfo's) */
{
    int k = or_find(p);
    if (k < 0) return 0;
    if (depth) { V3 near; or_nearest(&orings[k], p, &near, NULL, NULL); *depth = vdot(vsub(near, p), up); }
    return 1;
}
static int or_info(int row, WInfo *w, V3 p, V3 grav) /* OceanRing::calcWaterInfo */
{
    const ORing *o = NULL;
    for (int k = 0; k < norings; k++) if (orings[k].row == row) o = &orings[k];
    if (!o) return 0;
    V3 near, dir;
    float c = or_nearest(o, p, &near, &dir, NULL), w0 = 1200.0f * or_rate(o, c, 1, 12.0f);
    w->stream = v3(0, 0, 0); /* calcStreamVec: along the player's nearest direction, near his nearest point */
    if (vlen(vsub(o->near_p, p)) < w0) w->stream = vmul(o->near_d, 10.0f * or_rate(o, c, 0, 100.0f));
    { /* calcWaveHeight (its normal is replaced by -grav below) */
        V3 d, rp = or_rail(o, c, &d), side = vcross(d, vmul(grav, -1.0f));
        float dot = vdot(vsub(p, rp), side);
        int j = (int)(7.0f + dot / 171.42857f), s = (int)(c / 200.0f);
        j = j < 0 ? 0 : j > OR_STRIDE - 1 ? OR_STRIDE - 1 : j;
        s = s < 0 ? 0 : s > o->nseg - 1 ? o->nseg - 1 : s;
        w->wave = or_height(o, or_pt(o, j, s), 1200.0f + dot, c);
    }
    V3 v = vsub(p, near), ug = vmul(grav, -1.0f);
    float up = vdot(v, ug); /* vecKillElement(v, -grav) */
    v = vsub(v, vmul(ug, up));
    w->floor = cosf(vlen(v) / w0 * 3.1415927f * 0.5f) * w0 + up;
    w->depth = -up;
    w->surf_n = vmul(up_at(p, ug), 1.0f);
    w->surf_pos = vadd(p, vmul(grav, up));
    V3 e = vsub(p, near);
    if (vlen(e) < 1e-6f) { w->edge_d = w0; w->edge_pos = vadd(vmul(grav, w0), near); }
    else { w->edge_pos = vadd(vmul(vnorm(e), w0), near); w->edge_d = vlen(vsub(w->edge_pos, p)); }
    return 1;
}

static void or_build(ORing *o, const Row *r)
{
    o->wh1 = 80.0f; o->wh2 = 100.0f; o->arg1 = 30;
    if (r->arg[0] == 2) { o->wh1 = 20.0f; o->wh2 = 30.0f; }
    else if (r->arg[0] != 0) { o->wh1 = 50.0f; o->wh2 = 80.0f; } /* (arg0 0 -1: the pipe's, ponytail) */
    if (r->arg[1] != -1) o->arg1 = r->arg[1];
    float len = o->rail.len;
    int segs = o->rail.cl ? (int)o->rail.np : (int)o->rail.np - 1;
    o->cum = mem_alloc(MEM_SCENE, (segs + 1) * sizeof(float));
    o->cum[0] = 0.0f;
    for (int i = 0; i < segs; i++) {
        const float *a = o->rail.pts + 3 * i, *b = o->rail.pts + 3 * ((i + 1) % o->rail.np);
        o->cum[i + 1] = o->cum[i] + vlen(v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
    }
    o->nseg = (int)(len / 200.0f) + 1; /* initPoints */
    float step = len / (float)o->nseg;
    o->pt = mem_alloc(MEM_SCENE, o->nseg * OR_STRIDE * sizeof(OrPt));
    o->up = mem_alloc(MEM_SCENE, o->nseg * sizeof(V3));
    o->trig = mem_alloc(MEM_SCENE, o->nseg * sizeof *o->trig);
    for (int i = 0; i < o->nseg; i++) {
        V3 d, rp = or_rail(o, i * step, &d), up = up_at(rp, v3(0, 1, 0));
        V3 side = vnorm(vcross(d, up));
        float wr = or_rate(o, i * step, 1, 12.0f);
        o->up[i] = up;
        float on = i * step;
        o->trig[i][0] = sinf(0.0003f * on); o->trig[i][1] = cosf(0.0003f * on); o->trig[i][2] = sinf(0.0025f * on); o->trig[i][3] = cosf(0.0025f * on);
        for (int j = -7; j <= 7; j++) {
            OrPt *pt = or_pt(o, j + 7, i);
            pt->orig = vadd(rp, vmul(side, wr * (171.42857f * j)));
            int a = j < 0 ? -j : j;
            pt->h = 7 - a < 2 ? sinf((float)(7 - a) / 2.0f * 3.1415927f / 2.0f) : 1.0f; /* getEaseOutValue */
            pt->off = 0.0f; pt->alpha = 0;
        }
    }
    /* calcClippingBox: the rail's box +200, its widest point (100 x arg1, 1200 at least); mBox 5000 more, the
     * clipping box mWidthMax more */
    float wide = 1200.0f;
    for (int k = 0; k < rr_npoints(&o->rail); k++) { int a = rr_point_arg(&o->rail, k, 1); wide = fmaxf(wide, 100.0f * (a == -1 ? 0 : a)); }
    V3 mn = v3(1e30f, 1e30f, 1e30f), mx = v3(-1e30f, -1e30f, -1e30f);
    for (u32 i = 0; i < o->rail.np; i++) {
        const float *q = o->rail.pts + 3 * i;
        mn = v3(fminf(mn.x, q[0]), fminf(mn.y, q[1]), fminf(mn.z, q[2])); mx = v3(fmaxf(mx.x, q[0]), fmaxf(mx.y, q[1]), fmaxf(mx.z, q[2]));
    }
    V3 g = v3(200.0f + wide, 200.0f + wide, 200.0f + wide);
    o->bmin = vsub(vsub(mn, g), v3(5000, 5000, 5000)); o->bmax = vadd(vadd(mx, g), v3(5000, 5000, 5000));
    o->cmin = vsub(vsub(mn, g), v3(1200, 1200, 1200)); o->cmax = vadd(vadd(mx, g), v3(1200, 1200, 1200));
    /* OceanRingDrawer::initParts */
    int nd = o->nseg / 10 + 1, c2 = o->nseg / nd;
    o->npart = nd; o->c2 = c2;
    o->part = mem_alloc(MEM_SCENE, nd * sizeof(OrPart));
    for (int i = 0; i < nd; i++) {
        OrPart *pa = &o->part[i];
        pa->a = c2 * i; pa->n = i < nd - 1 ? c2 + 1 : o->nseg - c2 * i; pa->wrap = 0;
        if (i == nd - 1 && o->rail.cl) { pa->wrap = 1; pa->n++; }
        V3 s = v3(0, 0, 0);
        for (int k = 0; k < pa->n; k++) { int v = pa->a + k; if (k == pa->n - 1 && pa->wrap) v = 0; s = vadd(s, or_pt(o, 6, v)->orig); }
        pa->c = vmul(s, 1.0f / pa->n); /* mPosition: the points (v x 15 - 1) + 7 */
        pa->rad = 0.0f;
        for (int k = 0; k < pa->n; k++) {
            int v = (pa->a + k) % o->nseg;
            pa->rad = fmaxf(pa->rad, fmaxf(vlen(vsub(or_pt(o, 0, v)->orig, pa->c)), vlen(vsub(or_pt(o, 14, v)->orig, pa->c))));
        }
    }
    printf("ocean ring: row %d, %d segments, %d parts, rail %d, waves %d %d\n", o->row, o->nseg, nd, (int)len, (int)o->wh1, (int)o->wh2);
}
static void or_init(void)
{
    norings = 0; or_scene = sc;
    int ty = scene_type(sc, "OceanRing");
    for (u32 i = 0; ty >= 0 && scene_blob_g && i < sc->nobjs && norings < OR_MAX; i++) {
        if (sc->objs[i].type != ty) continue;
        ORing *o = &orings[norings];
        memset(o, 0, sizeof *o);
        o->row = (int)i;
        if (!rr_init(&o->rail, &sc->objs[i])) continue;
        or_build(o, &sc->objs[i]);
        norings++;
    }
}

static void or_check(ORing *o, OrPt *pt) /* WaterPoint::initAfterPlacement (at its first update: its zone's map loaded) */
{
    pt->alpha = 255;
    if (pt->h == 0.0f) return;
    V3 hp, n; int inst;
    if (!kcl_line(vadd(pt->orig, v3(0, 200.0f, 0)), vsub(pt->orig, v3(0, 200.0f, 0)), &hp, &n, &inst)) return; /* calcMapGround */
    float diff = (hp.y - pt->orig.y) / 200.0f, rate = fminf(fmaxf(0.1f + fabsf(diff), 0.1f), 1.0f);
    pt->alpha = (u8)fminf(fmaxf(30.0f + 255.0f * -diff, 30.0f), 255.0f);
    pt->h = fminf(rate, pt->h);
    (void)o;
}
static void or_line(ORing *o, int a1, int a2, int a3, int a4) /* updatePointsInLine */
{
    float sa[OR_STRIDE], ca[OR_STRIDE], s1 = sinf(o->th1), c1 = cosf(o->th1);
    for (int j = 0; j < OR_STRIDE; j++) { sa[j] = sinf(o->th0 + 0.003f * (j * 171.42857f)); ca[j] = cosf(o->th0 + 0.003f * (j * 171.42857f)); }
    for (int s = a1; s < a2; s++) {
        float k = 1.0f;
        if (s < a1 + a3) k = (float)(s - a1) / (float)a3;
        if (s >= a2 - a4) k = (float)(a2 - 1 - s) / (float)a4;
        /* calcHeight's sines of (th0 + 0.003 across) + 0.0003 on and th1 + 0.0025 on as sin(a + b) */
        const float *tg = o->trig[s];
        float sb = tg[0], cb = tg[1], w2 = o->wh2 * (s1 * tg[3] + c1 * tg[2]);
        for (int j = 0; j < OR_STRIDE; j++) { /* WaterPoint::updatePos */
            OrPt *pt = or_pt(o, j, s);
            if (!pt->alpha) or_check(o, pt);
            pt->off = k * pt->h * (o->wh1 * (sa[j] * cb + ca[j] * sb) + w2);
        }
        int p = s / o->c2;
        o->part[p < o->npart ? p : o->npart - 1].fresh = o->frame;
    }
}
static void or_update(void)
{
    if (or_scene != sc) return;
    V3 eye, watch, cup; float fovy;
    camera_pose(&eye, &watch, &cup, &fovy);
    V3 zdir = vnorm(vsub(watch, eye));
    for (int k = 0; k < norings; k++) {
        ORing *o = &orings[k];
        o->th0 = fmodf(o->th0 - 0.04f, 6.2831853f); o->th1 = fmodf(o->th1 - 0.06f, 6.2831853f); /* sWaveSpeed0 / 1 (within a turn) */
        o->frame++;
        o->clipped = 1; /* updatePoints */
        if (or_box(mario->pos, o->bmin, o->bmax)) {
            o->near_c = or_nearest(o, mario->pos, &o->near_p, &o->near_d, NULL);
            if (vlen(vsub(o->near_p, mario->pos)) <= 5000.0f) { /* sUpdateDistanceMax */
                o->clipped = 0;
                int n = o->nseg, s = (int)(o->near_c / 200.0f), a, b;
                if (n < o->arg1 + 8) or_line(o, 0, n - 1, 0, 0);
                else {
                    if (vdot(zdir, o->near_d) < 0.0f) { b = s + 8; a = s - o->arg1; } else { a = s - 8; b = s + o->arg1; }
                    if (!o->rail.cl) or_line(o, a < 0 ? 0 : a > n - 1 ? n - 1 : a, b < 0 ? 0 : b > n - 1 ? n - 1 : b, 3, 3);
                    else if (a >= 0 && b < n) or_line(o, a, b, 3, 3);
                    else if (a < 0) { or_line(o, 0, b, 0, 3); or_line(o, (n - 1 + a) % (n - 1), n - 1, 3, 0); }
                    else if (b > n - 1) { or_line(o, a, n - 1, 3, 0); or_line(o, 0, (n - 1 + b) % (n - 1), 0, 3); }
                }
            }
        }
        o->tu = fmodf(o->tu - 0.003f + 1.0f, 1.0f); o->tv = fmodf(o->tv - 0.001f + 1.0f, 1.0f); /* OceanRingDrawer::update (TEX0) */
        V3 near;
        or_nearest(o, watch, &near, NULL, &o->watch_p);
        o->lv1 = se_level(SE_AT_LV_OCEAN_RING, &o->watch_p.x, o->lv1);
        or_nearest(o, eye, &near, NULL, &o->cam_p);
        o->lv2 = se_level(SE_AT_LV_OCEAN_RING_SUB, &o->cam_p.x, o->lv2);
    }
}

static float or_pos[2048][3], or_uv[2048][2];
static u8 or_al[2048];
static u16 or_tri[3 * 2 * 14 * 64];
/* loadMaterial's colour C1 x the frame behind (stage 3) as the frame less (1 - C1) x the vertex alpha (GX 76 D7 FF: 89 28 00
 * off, in GS units 44 14 00), then the texture added (T x T / 2, stages 0 and 1) at the vertex alpha */
static void or_mesh(int nv, int nt, int tex)
{
    static u32 c1[2048], c2[2048];
    for (int v = 0; v < nv; v++) {
        u32 a = or_al[v];
        c1[v] = 0x80000000u | (0x14u * a / 255u) << 8 | 0x44u * a / 255u;
        c2[v] = (a * 0x80u / 255u) << 24 | 0x808080u;
    }
    render_mesh_vc((const float (*)[3])or_pos, (const float (*)[2])or_uv, c1, nv, or_tri, nt, NO_TEX, 0x200 | 0x80);
    render_mesh_same();
    render_mesh_vc((const float (*)[3])or_pos, (const float (*)[2])or_uv, c2, nv, or_tri, nt, (u16)tex, 2);
}
static void draw_oceanrings(void)
{
    int tex = fx_ocean_ring_tex();
    if (or_scene != sc || tex < 0) return;
    float (*pos)[3] = or_pos, (*uv)[2] = or_uv;
    u16 *tri = or_tri;
    for (int k = 0; k < norings; k++) {
        const ORing *o = &orings[k];
        if (!row_shown(o->row)) continue;
        if (o->clipped) { /* drawGD: the whole ring by its edge points (j 14, j 0), flat */
            int n = o->nseg + o->rail.cl, nv = 0, nt = 0;
            for (int i = 0; i < n && nv + 2 <= 2048; i++) {
                int s = i == o->nseg ? 0 : i;
                const OrPt *e0 = or_pt(o, 14, s), *e1 = or_pt(o, 0, s);
                float u = fmodf(0.05f * i, 4.0f) + o->tu;
                pos[nv][0] = e0->orig.x; pos[nv][1] = e0->orig.y; pos[nv][2] = e0->orig.z; uv[nv][0] = u; uv[nv][1] = 0.7f + o->tv;
                pos[nv + 1][0] = e1->orig.x; pos[nv + 1][1] = e1->orig.y; pos[nv + 1][2] = e1->orig.z; uv[nv + 1][0] = u; uv[nv + 1][1] = o->tv;
                or_al[nv] = e0->alpha ? e0->alpha : 255; or_al[nv + 1] = e1->alpha ? e1->alpha : 255; /* (255 until checked) */
                if (i && nt + 2 <= 3 * 2 * 14 * 64 / 3) {
                    u16 *t = tri + 3 * nt; t[0] = nv - 2; t[1] = nv - 1; t[2] = nv; t[3] = nv - 1; t[4] = nv + 1; t[5] = nv; nt += 2;
                }
                nv += 2;
            }
            or_mesh(nv, nt, tex);
            continue;
        }
        for (int p = 0; p < o->npart; p++) { /* OceanRingPartDrawer::draw: drawDynamic near the player, the flat list else */
            const OrPart *pa = &o->part[p];
            float cc[3] = {pa->c.x, pa->c.y, pa->c.z};
            if (!render_visible(cc, pa->rad + 200.0f)) continue;
            /* sDistancePartDL; a flat part's 15 points across lie on one line: its two edges draw the same surface
             * (ponytail: a near part its update left is drawn flat, Petari draws its points' last waves) */
            int dyn = pa->fresh == o->frame && vlen(vsub(pa->c, mario->pos)) < 13000.0f, nv = 0, nt = 0, w = dyn ? OR_STRIDE : 2;
            float u0 = 0.05f * pa->a;
            u0 -= floorf(u0);
            for (int i = 0; i < pa->n && nv + w <= 2048; i++) {
                int s = pa->a + i;
                if (i == pa->n - 1 && pa->wrap) s = 0;
                if (s >= o->nseg) s = o->nseg - 1;
                V3 up = o->up[s];
                for (int jj = 0; jj < w; jj++) {
                    int j = dyn ? jj : jj * (OR_STRIDE - 1);
                    const OrPt *pt = or_pt(o, j, s);
                    V3 q = dyn ? vadd(pt->orig, vmul(up, pt->off)) : pt->orig;
                    pos[nv][0] = q.x; pos[nv][1] = q.y; pos[nv][2] = q.z;
                    uv[nv][0] = u0 + 0.05f * i + o->tu; uv[nv][1] = 0.05f * j + o->tv;
                    or_al[nv] = pt->alpha ? pt->alpha : 255;
                    if (i && jj && nt + 2 <= 2 * 14 * 64) {
                        u16 *t = tri + 3 * nt, a0 = nv - w - 1, a1 = nv - w, b0 = nv - 1, b1 = nv;
                        t[0] = a0; t[1] = a1; t[2] = b0; t[3] = a1; t[4] = b1; t[5] = b0; nt += 2;
                    }
                    nv++;
                }
            }
            or_mesh(nv, nt, tex);
        }
    }
}
