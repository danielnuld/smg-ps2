/* TripodBoss.cpp (Megaleg, TriLegLv1Galaxy's TriPodBossZone) with TripodBossLeg.cpp, IKJoint.cpp,
 * TripodBossMovableArea.cpp, TripodBossStepPoint / StepSequence / StepStartArea.cpp, TripodBossAccesser.cpp,
 * TripodBossBaseJointPosition.cpp, TripodBossFixPartsBase / FixParts / RotateParts.cpp, TripodBossCore / Shell.cpp,
 * TripodBossGuardWall / GuardWallPart.cpp, TripodBossKillerGenerater / KillerGeneraterCircle.cpp and
 * BaseMatrixFollowTargetHolder.cpp after Petari. #included by game.c (tb_*).
 * The boss is 22 bones: its body (21) and per leg (PART_ID 0 left, 1 back, 2 right; 7 each) the IK root / middle /
 * end joints and their separated local axes. Every part is placed in a design pose next to a TripodBossBaseJointPosition
 * of its joint (arg0, cJMapBoneIDToBoneIndexTable): its world matrix is bone * inverse(base) * its placement, and so are
 * the gravities and switch cubes that follow a TripodBossBaseJointPosition (Obj_ID: slice.py FOLW). The legs step on
 * the planet (MovableArea: a sphere of 3000 about the boss's placement) along the step sequences the
 * TripodBossStepStartAreas ask for, the killer cannons shoot HomingKillers at Mario, a killer at the shell breaks it
 * (SW 68: the guard wall rises in the ダメージ demo, the upper cannons come), one at the core (through a gap the
 * killers blow in the wall) breaks it: SW_B, the 破壊 demo (PainDemo, BreakDownDemo, ExplosionDemo), the grand star.
 * ponytail: no TripodBossBreakMovement (the parts stay in place until their arg2 frames, then explode), no
 * CameraTripodBoss / Joint / Planet (camera areas), no leg shadows, no DummyDisplayModel (13) on top, no
 * the cannons' 2ndDemo / Shoot clips not played and the circles' cannons without collision, the
 * core / shell break models and the parts' Warning BRKs not drawn, one 2ndDemo pose for all guard wall parts, isRideMario
 * by Mario's ground being a part (not the gravity host), end anim cameras without their blends, the gears' rotators
 * at a constant RotateSpeed, the guard wall parts' collision as their KCL's band (tb_wall_part), the killers' shadow
 * on a BREAKABLE_CAGE (processChase's aim at it) */
enum { TL_HOLD, TL_DEMO, TL_MOVE, TL_UP, TL_LEAVEOUT, TL_TOLAND, TL_STAMP, TL_LANDING, TL_DVIB, TL_DAMAGE, TL_BREAK };
enum { TB_NONACTIVE, TB_TRYDEMO, TB_WAIT, TB_STEP, TB_WAITSTEP, TB_DAMAGE, TB_CHANGESEQ, TB_LEAVEOUT, TB_STARTDEMO,
       TB_DAMAGEDEMO, TB_PAINDEMO, TB_BREAKDOWN, TB_EXPLOSION, TB_DEAD };
enum { TBK_NONE, TBK_HIDE, TBK_SHOWDEMO, TBK_READY, TBK_SHOOTSET, TBK_SHOOT, TBK_COOLDOWN, TBK_BREAK, TBK_STOP, TBK_RESTART, TBK_DEAD };
enum { TP_NONACTIVE, TP_STARTDEMO, TP_WAIT, TP_BREAK, TP_DEAD }; /* TripodBossFixParts (Core / Shell: Wait, Break) */
typedef struct { V3 pos, nrm, front; int prio, leg, wait, seq; } TbPoint;
typedef struct {
    int nrv, st, mode, demofx, press, dmg;       /* _24C, mDemoEffectTiming, mIsPressPlayer, _254 */
    int p94, p98;                                /* its step points (tb.pt) */
    float rlen, mlen, j0[16], j30[16], j60[16];  /* IKJoint: bone lengths, _0 (first pose), _30 (root), _60 (middle) */
    float a0[16], ry[16], ryz[16], ax[16], axz[16], end[16];
    V3 l210, fep, v234, v240;                    /* _210, mForceEndPoint, _234 its velocity, _240 its target */
    Quat q1f0, q200; float h250;
    u32 lv;
} TbLeg;
typedef struct { int row, single, joint, label, coll, chase, nrv, st, kph, kt, wait; float dist, e4[16], m[16]; Killer k; u32 lv; } TbCannon;
#define TB_CANNONS 32
static struct {
    int row, nrv, st, pend, cur, next, c630, s634, hid638, bgm63c, f640, nseq;
    int k1row, k1nrv, k1st, k1item; /* TripodBossKinokoOneUp: its row, 0 dead / 1 Active / 2 End, its step, its bitm */
    const char *dname;
    float body[16], ec[16], base[16], binv[22][16], att[22][16];
    TbLeg leg[3];
    TbPoint pt[48]; int npt;
    struct { int p[32], n, cur, on; } seq[32];
    V3 c, axis, front; float radius;              /* TripodBossMovableArea */
    V3 v5bc, p5c8, p5d4, v5e0, c5ec;
    float f600, f620;
    Anim *anim; const Model *md, *low, *killer, *cannon;
    u32 lv[4];
    /* parts: by OBJS row, its bone + 1 (0 none); followers' gravity records and their design copies */
    signed char *rbone; unsigned char *rpart;
    Gravity *grav0; int *gidx, ngf;
    int shell, core, wall, sh_nrv, sh_st, co_nrv, co_st, wa_nrv, wa_st, wp_nrv[8], wp_st[8];
    float wall_rot, wall_m[16];
    Anim *wanim; const Model *wmd;
    TbCannon can[TB_CANNONS]; int ncan;
    int brk_st, attok;                           /* (tb.att made once: the parts placed again in a rebind) */
} tb = {.row = -1};
enum { TBP_FIX = 1, TBP_ROTATE, TBP_WARN };     /* rpart: how a part's row moves */
static const signed char tb_conv[41] = {0, 1, 2, 7, 8, 9, 14, 15, 16, 21, 3, 4, 0, 1, 5, 6, 2, -1, -1, -1, 10,
                                        11, 7, 8, 12, 13, 9, -1, -1, -1, 17, 18, 14, 15, 19, 20, 16, -1, -1, -1, 21};
static int tb_bone(int id) { return id >= 0 && id < 41 ? tb_conv[id] : -1; } /* convertBoneIDToIndex */
static int tb_part_of_bone(int b) { return b < 0 ? -1 : b < 7 ? 0 : b < 14 ? 1 : b < 21 ? 2 : b == 21 ? 3 : -1; }

/* column-major helpers (Petari's TPos3f columns are its axes) */
static V3 tb_col(const float *m, int k) { return v3(m[4 * k], m[4 * k + 1], m[4 * k + 2]); }
static void tb_setcol(float *m, int k, V3 v) { m[4 * k] = v.x; m[4 * k + 1] = v.y; m[4 * k + 2] = v.z; }
static V3 tb_mulp(const float *m, V3 p) { return vadd(tb_col(m, 3), vadd(vmul(tb_col(m, 0), p.x), vadd(vmul(tb_col(m, 1), p.y), vmul(tb_col(m, 2), p.z)))); }
static void tb_side_up(float *m, V3 side, V3 up, V3 pos) /* makeMtxSideUpPos */
{
    V3 x = vnorm(side), z = vnorm(vcross(x, up)), y = vcross(z, x);
    mat_identity(m); tb_setcol(m, 0, x); tb_setcol(m, 1, y); tb_setcol(m, 2, z); tb_setcol(m, 3, pos);
}
static void tb_inv(float *o, const float *m) /* invert (a rigid matrix) */
{
    V3 x = tb_col(m, 0), y = tb_col(m, 1), z = tb_col(m, 2), t = tb_col(m, 3);
    float ix = 1.0f / vdot(x, x), iy = 1.0f / vdot(y, y), iz = 1.0f / vdot(z, z);
    mat_identity(o);
    o[0] = x.x * ix; o[4] = x.y * ix; o[8] = x.z * ix;
    o[1] = y.x * iy; o[5] = y.y * iy; o[9] = y.z * iy;
    o[2] = z.x * iz; o[6] = z.y * iz; o[10] = z.z * iz;
    V3 it = v3(-(o[0] * t.x + o[4] * t.y + o[8] * t.z), -(o[1] * t.x + o[5] * t.y + o[9] * t.z), -(o[2] * t.x + o[6] * t.y + o[10] * t.z));
    tb_setcol(o, 3, it);
}
static void tb_mul(float *o, const float *a, const float *b) { float t[16]; mat_mul_c(t, a, b); memcpy(o, t, 64); }
static Quat tb_quat(const float *m) { float q[4]; mtx_quat(m, q); return (Quat){q[0], q[1], q[2], q[3]}; }
static void tb_make_quat(float *m, Quat q) { V3 x, y, z; q_axes(q, &x, &y, &z); tb_setcol(m, 0, x); tb_setcol(m, 1, y); tb_setcol(m, 2, z); }
static void tb_tr(float *m, V3 p, const float rot[3]) { float one[3] = {1, 1, 1}, pp[3] = {p.x, p.y, p.z}; mat_translate_rot_scale(m, pp, rot, one); }

/* --- IKJoint --- */
static void ik_distance(TbLeg *l, float d) /* updateByDistanceOnly (checkReachIKTarget, calcIKRootAngleCosign) */
{
    float r = l->rlen, mm = l->mlen, c;
    if (d < fabsf(r - mm) && d <= r + mm) c = 1.0f;
    else { c = (mm * mm - d * d - r * r) / (-2.0f * r * d); c = c < -1 ? -1 : c > 1 ? 1 : c; }
    float s = sqrtf(1.0f - c * c);
    mat_identity(l->j30); l->j30[0] = c; l->j30[1] = s; l->j30[4] = -s; l->j30[5] = c;
    V3 mid = v3(c * r, s * r, 0), ms = vsub(v3(d, 0, 0), mid);
    ms = vlen(ms) > 0 ? vnorm(ms) : ms;
    mat_identity(l->j60); tb_setcol(l->j60, 0, ms); tb_setcol(l->j60, 1, v3(-ms.y, ms.x, 0)); tb_setcol(l->j60, 3, mid);
}
static void ik_apply(TbLeg *l, const float *m) { tb_mul(l->j30, m, l->j30); tb_mul(l->j60, m, l->j60); }
static void ik_up(TbLeg *l, V3 root, V3 target, V3 up) /* updateByUpVector */
{
    V3 d = vsub(target, root); float n = vlen(d), m[16];
    ik_distance(l, n);
    tb_side_up(m, n > 0 ? vmul(d, 1.0f / n) : d, up, root);
    ik_apply(l, m);
}
static void ik_local(TbLeg *l, const float *rm, V3 root, V3 target) /* updateByLocalRootAndWorldTarget */
{
    float inv[16], m[16], r[16];
    tb_inv(inv, rm);
    V3 d = vsub(tb_mulp(inv, target), root); float n = vlen(d);
    ik_distance(l, n);
    d = n > 0 ? vmul(d, 1.0f / n) : d;
    mat_identity(r); tb_make_quat(r, q_between(tb_col(l->j0, 0), d)); /* calcToTargetMatrixByFirstPose: setRotate(side, dir) */
    tb_mul(m, r, l->j0); tb_setcol(m, 3, root); tb_mul(m, rm, m);
    ik_apply(l, m);
}
static V3 ik_root(const TbLeg *l) { return tb_col(l->j30, 3); }
static V3 ik_end(const TbLeg *l) { return vadd(tb_col(l->j60, 3), vmul(tb_col(l->j60, 0), l->mlen)); }

/* --- TripodBossMovableArea --- */
static int ma_collide(V3 a2, V3 a4, V3 *h0, V3 *hc, V3 *hn) /* collideSphere(a3 0) */
{
    *hc = vadd(a2, a4);
    float r = tb.radius, sc = vlen(a4);
    V3 v29 = vsub(a2, tb.c), dir = sc > 0 ? vmul(a4, 1.0f / sc) : a4;
    float v11 = vdot(v29, dir), val = vdot(v29, v29) - r * r;
    if (v11 > 0 && val > 0) return 0;
    float v14 = v11 * v11 - val;
    if (v14 < 0) return 0;
    float v15 = -v11 - sqrtf(v14);
    if (v15 > sc) return 0;
    if (v15 < 0) v15 = 0;
    *h0 = vadd(a2, vmul(dir, v15));
    V3 v27 = vsub(*h0, tb.c); float v17 = vlen(v27);
    v27 = v17 > 0 ? vmul(v27, 1.0f / v17) : v27;
    if (v17 < r) { if (v17 < 1e-6f) v27 = v3(0, 1, 0); *h0 = vadd(tb.c, vmul(v27, r)); }
    *hn = v27;
    V3 v26 = vsub(*hc, *h0);
    v26 = vsub(v26, vmul(*hn, vdot(v26, *hn)));
    *hc = vadd(*h0, v26);
    return 1;
}
static V3 ma_near(V3 p) { V3 d = vsub(p, tb.c); float l = vlen(d); d = l < 1e-6f ? tb.axis : vmul(d, 1.0f / l); return vadd(tb.c, vmul(d, tb.radius)); }
static V3 ma_normal(V3 p) { V3 n = vsub(p, tb.c); return vlen(n) < 1e-6f ? tb.axis : vnorm(n); }
static V3 ma_front(V3 p)
{
    V3 v = ma_normal(p), a = vmul(tb.axis, -1.0f), f = vsub(a, vmul(v, vdot(a, v)));
    if (vlen(f) > 1e-6f) return vnorm(f);
    f = fabsf(tb.axis.x) <= fabsf(tb.axis.y) && fabsf(tb.axis.x) <= fabsf(tb.axis.z) ? v3(1, 0, 0) : fabsf(tb.axis.y) <= fabsf(tb.axis.z) ? v3(0, 1, 0) : v3(0, 0, 1);
    return vnorm(vsub(f, vmul(tb.axis, vdot(f, tb.axis))));
}
static Quat tb_base_quat(V3 n, V3 f) { float m[16]; tb_side_up(m, vmul(n, -1.0f), f, v3(0, 0, 0)); return tb_quat(m); } /* makeMtxSideUp(-normal, front) */

/* --- TripodBossLeg --- */
static void tl_set(TbLeg *l, int n) { l->nrv = n; l->st = -1; }
static void tl_se(TbLeg *l, u32 id) { se_start(id, &l->fep.x); } /* _260: at mForceEndPoint */
static void tl_lv(TbLeg *l, u32 id) { l->lv = se_level(id, &l->fep.x, l->lv); }
static void tl_sep_yzx(float *a1, float *a2, const float *a3, const float *a4) /* MR::separateMatrixRotateYZX */
{
    float inv[16], v17[16];
    tb_inv(inv, a3); tb_mul(v17, inv, a4);
    V3 v = tb_col(v17, 0); float h = sqrtf(v.x * v.x + v.z * v.z);
    if (h < 1e-6f) {
        float s = v.y >= 0 ? 1.0f : -1.0f, t[16];
        mat_identity(t); tb_setcol(t, 0, v3(0, s, 0)); tb_setcol(t, 1, v3(-s, 0, 0)); tb_setcol(t, 3, tb_col(a1, 3));
        tb_mul(a1, a3, t); memcpy(a2, a1, 64);
        return;
    }
    float l = vlen(v), c12 = h / l, s14 = v.y / l, cx = v.x / h, cz = v.z / h, b[16];
    mat_identity(a1); tb_setcol(a1, 0, v3(cx, 0, cz)); tb_setcol(a1, 2, v3(-cz, 0, cx));
    mat_identity(b); tb_setcol(b, 0, v3(c12, s14, 0)); tb_setcol(b, 1, v3(-s14, c12, 0));
    tb_mul(a1, a3, a1); tb_mul(a2, a1, b);
}
static void tl_ik(TbLeg *l) /* updateIKPose */
{
    if (l->mode == 0) {
        V3 lu = vsub(l->fep, tb.c); lu = vlen(lu) > 0 ? vnorm(lu) : lu; /* calcLegUpVector */
        lu = vsub(lu, vmul(tb.axis, vdot(lu, tb.axis))); lu = vlen(lu) > 0 ? vnorm(lu) : lu;
        ik_up(l, tb_mulp(tb.body, l->l210), l->fep, lu);
    } else ik_local(l, tb.body, l->l210, l->fep);
    float v5[16];
    tb_mul(v5, tb.body, l->a0); /* separateLocalAxisRootJoint */
    tl_sep_yzx(l->ry, l->ryz, v5, l->j30);
    V3 rp = ik_root(l); tb_setcol(l->ry, 3, rp); tb_setcol(l->ryz, 3, rp);
}
static int tl_bind(TbLeg *l) /* bindEndPosition */
{
    V3 a = tb_col(l->end, 3), h0, hc, hn;
    if (!ma_collide(a, vsub(ik_end(l), a), &h0, &hc, &hn)) return 0;
    l->fep = hc;
    if (vdot(l->v234, hn) < 0) l->v234 = vsub(l->v234, vmul(hn, vdot(l->v234, hn)));
    return 1;
}
static void tl_pose(TbLeg *l) { tl_ik(l); if (tl_bind(l)) tl_ik(l); l->fep = ik_end(l); } /* updatePose */
static void tl_to_target(TbLeg *l, V3 t, float a) /* addToTargetPower */
{
    V3 d = vsub(t, l->fep); float n = vlen(d);
    d = n > 0 ? vmul(d, 1.0f / n) : d;
    float k = n / 300.0f;
    l->v234 = vadd(l->v234, vmul(d, a * (k > 1 ? 1 : k < -1 ? -1 : k)));
    if (1.0f - k > 0) l->v234 = vsub(l->v234, vmul(l->v234, (1.0f - k) * 0.1f));
}
static void tl_limit(TbLeg *l) /* addIKLimitPower */
{
    V3 d = vsub(ik_root(l), l->fep); float n = vlen(d);
    d = n > 0 ? vmul(d, 1.0f / n) : d;
    float lo = fabsf(l->rlen - l->mlen), hi = l->rlen + l->mlen, v = (n - lo) / (hi - lo), v5, v6;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    if (v < 0.5f) { v = 1.0f - v; d = vmul(d, -1.0f); v5 = 0.5f; v6 = 0.0f; } else { v5 = 0.9f; v6 = 5.0f; }
    if (v5 < v) { float x = (v - v5) / (1.0f - v5); l->v234 = vadd(l->v234, vmul(d, (1.0f - cosf(x * 3.14159265f)) * 0.5f * v6)); }
}
static void tl_step_vel(TbLeg *l, float damp) { tl_limit(l); l->fep = vadd(l->fep, l->v234); l->v234 = vmul(l->v234, damp); }
static float tl_height(const TbLeg *l, int p) { return vdot(vsub(l->fep, tb.pt[p].pos), tb.pt[p].nrm); }
static void tl_ankle_up(TbLeg *l, float h) /* updateAnkleUp */
{
    float v = h / 1000.0f; v = v < 0 ? 0 : v > 1 ? 1 : v;
    tb_make_quat(l->end, q_slerp(tb_base_quat(ma_normal(l->fep), ma_front(l->fep)), l->q1f0, 1.0f - v * v * v));
}
static void tl_ankle_base(TbLeg *l) { tb_make_quat(l->end, q_slerp(tb_quat(l->end), tb_base_quat(ma_normal(l->fep), ma_front(l->fep)), 0.2f)); }
static void tl_ankle_land(TbLeg *l) /* updateAnkleLanding */
{
    float v = vdot(vsub(l->fep, tb.pt[l->p98].pos), tb.pt[l->p98].nrm) / l->h250;
    v = (v - 0.15f) / 0.85f; v = v < 0 ? 0 : v > 1 ? 1 : v;
    float u = 1.0f - v;
    tb_make_quat(l->end, q_slerp(l->q1f0, l->q200, u * u * u));
}
static void tl_up(TbLeg *l, int p, float a) { l->v234 = vadd(l->v234, vmul(tb.pt[p].nrm, a)); tl_step_vel(l, 0.9f); tl_pose(l); } /* addAccelUpLeg */
static int tl_stop(const TbLeg *l) { return l->nrv == TL_HOLD || l->nrv == TL_DEMO || l->nrv == TL_DAMAGE || l->nrv == TL_BREAK; }
static void tl_request(TbLeg *l, int p) /* requestStepTarget */
{
    if (l->nrv == TL_DAMAGE || l->nrv == TL_BREAK) return;
    l->p94 = l->p98; l->p98 = p;
    tl_set(l, l->nrv == TL_HOLD ? TL_UP : TL_TOLAND);
}
static void tl_exe(TbLeg *l)
{
    int s = ++l->st;
    TbPoint *p98 = &tb.pt[l->p98], *p94 = &tb.pt[l->p94];
    switch (l->nrv) {
    case TL_HOLD: case TL_DAMAGE: if (s == 0) l->v234 = v3(0, 0, 0); tl_ik(l); break;
    case TL_DEMO:
        if (s == 0) l->v234 = v3(0, 0, 0);
        if (l->demofx == 1) { tl_se(l, SE_BM_TRIPOD_LAND); fx_named("TripodBoss", "LegSmoke", l->end, NULL, l); }
        tl_ik(l); tl_ankle_up(l, 1000.0f);
        break;
    case TL_UP:
        if (s == 0) { tl_se(l, SE_BM_TRIPOD_LEG_UP); l->q1f0 = tb_base_quat(p94->nrm, p94->front); }
        tl_up(l, l->p94, 1.5f);
        { float h = tl_height(l, l->p94); tl_ankle_up(l, h); if (h > 1000.0f) tl_set(l, TL_TOLAND); }
        break;
    case TL_LEAVEOUT:
        if (s == 0) { tl_se(l, SE_BM_TRIPOD_LEG_UP); l->q1f0 = tb_base_quat(p98->nrm, p98->front); l->v240 = vadd(p98->pos, vmul(p98->nrm, 1000.0f)); }
        if (s < 60) { l->v234 = v3(0, 0, 0); tl_pose(l); }
        else if (tl_height(l, l->p98) < 1000.0f) { tl_up(l, l->p98, 0.8f); tl_ankle_up(l, tl_height(l, l->p98)); }
        else { tl_to_target(l, l->v240, 1.0f); tl_step_vel(l, 0.9f); tl_pose(l); tl_ankle_base(l); }
        break;
    case TL_MOVE: tl_lv(l, SE_BM_LV_TRIPOD_LEG_MOVE); tl_to_target(l, l->v240, 1.0f); tl_step_vel(l, 0.9f); tl_pose(l); break;
    case TL_TOLAND:
        if (s == 0) l->v240 = vadd(p98->pos, vmul(p98->nrm, 1000.0f));
        tl_lv(l, SE_BM_LV_TRIPOD_LEG_MOVE);
        tl_to_target(l, l->v240, 1.0f); tl_step_vel(l, 0.95f); tl_pose(l); tl_ankle_base(l);
        if (vdot(vsub(l->fep, l->v240), vsub(l->fep, l->v240)) < 90000.0f) tl_set(l, TL_STAMP);
        break;
    case TL_STAMP:
        if (s == 0) l->v240 = vadd(p98->pos, vmul(p98->nrm, 1000.0f));
        tl_lv(l, SE_BM_LV_TRIPOD_LEG_PREFALL);
        tl_to_target(l, l->v240, 1.0f); tl_step_vel(l, 0.9f);
        if (s > 20) l->fep = vadd(l->fep, vmul(p98->nrm, sinf(s * 6.2831853f / 10.0f) * 20.0f)); /* sStampSignVibration* */
        tl_pose(l); tl_ankle_base(l);
        if (s > 60) { l->v240 = p98->pos; tl_set(l, TL_LANDING); } /* sStampSignTime */
        break;
    case TL_LANDING: {
        if (s == 0) {
            tl_se(l, SE_BM_TRIPOD_FALL); l->v234 = v3(0, 0, 0);
            l->q1f0 = tb_quat(l->end); l->q200 = tb_base_quat(p98->nrm, p98->front);
            l->h250 = vdot(vsub(l->fep, p98->pos), p98->nrm);
        }
        V3 d = vsub(l->v240, l->fep); d = vlen(d) > 0 ? vnorm(d) : d;
        l->v234 = vadd(l->v234, d);
        tl_step_vel(l, 0.98f); tl_ik(l);
        V3 a = tb_col(l->end, 3), h0, hc, hn;
        if (ma_collide(a, vsub(ik_end(l), a), &h0, &hc, &hn)) {
            tl_se(l, SE_BM_TRIPOD_LAND);
            float dist = vlen(vsub(l->fep, mario->pos));
            camera_shake(dist < 2000.0f ? SHAKE_STRONG : dist < 3500.0f ? SHAKE_NORMAL : SHAKE_WEAK); /* sLandShake*Distance */
            fx_named("TripodBoss", "LegSmoke", l->end, NULL, l);
            l->fep = h0; l->v234 = v3(0, 0, 0);
            tl_set(l, l->dmg ? TL_DVIB : TL_HOLD);
            tl_ik(l); tl_ankle_land(l);
        } else { l->fep = ik_end(l); tl_ankle_land(l); }
        break;
    }
    case TL_DVIB: /* (no sensor answers ACTMES_TRIPODBOSS_LEG_IS_DAMAGE: never reached) */
        if (s == 0) { l->v234 = v3(0, 0, 0); l->v240 = l->fep; }
        l->fep = vadd(l->v240, vmul(p98->nrm, sinf(s * 6.2831853f * 0.25f) * 40.0f));
        tl_ik(l);
        if (s > 27) { l->fep = l->v240; tl_set(l, TL_DAMAGE); }
        break;
    case TL_BREAK: if (s == 0) l->v234 = v3(0, 0, 0); tl_ik(l); break; /* kill(): its pose stays */
    }
}
static void tl_control(TbLeg *l) /* control: mEndJointMtx's place, separateAnkleJointLocalAxis */
{
    l->dmg = 0;
    tb_setcol(l->end, 3, l->fep);
    tl_sep_yzx(l->axz, l->ax, l->end, l->j60);
    tb_setcol(l->ax, 3, l->fep); tb_setcol(l->axz, 3, l->fep);
    /* attackSensor: its body sensor (radius 1000 at (500, 200, 0) on the end joint) on Mario's */
    l->press = vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), tb_mulp(l->end, v3(500, 200, 0)))) < 1000.0f + HIT_BODY_R;
}

/* --- TripodBoss --- */
static int tb_ss(void) { return tb.s634 == 0 || tb.s634 == 1; } /* isStateSomething */
static int tb_start_demo_on(void) { return tb.nrv == TB_STARTDEMO; }
static int tb_damage_demo_on(void) { return tb.nrv == TB_DAMAGEDEMO; }
static int tb_end_demo_on(void) { return tb.nrv == TB_PAINDEMO || tb.nrv == TB_BREAKDOWN || tb.nrv == TB_EXPLOSION; }
static int tb_breakdown(void) { return tb.nrv == TB_BREAKDOWN || tb.nrv == TB_EXPLOSION || tb.nrv == TB_DEAD; } /* isEndBreakDownDemo || (dead) */
static int tb_is_demo(void) { return tb_start_demo_on() || tb_damage_demo_on() || tb_end_demo_on(); }
static int tb_started(void) { return tb.row >= 0 && tb.nrv != TB_NONACTIVE; }
static int tb_demo(void) { return tb.row >= 0 && (tb_is_demo() || tb.nrv == TB_TRYDEMO); } /* its puppet demos: Mario BattleWait */
static void tb_set(int n) { tb.nrv = n; tb.st = -1; }
static void tb_se(u32 id) { se_start(id, &tb.p5d4.x); }
static void tb_lvs(int k, u32 id) { tb.lv[k] = se_level(id, &tb.p5d4.x, tb.lv[k]); }
static const float *tb_bone_mtx(int b)
{
    if (b == 21) return tb.body;
    TbLeg *l = &tb.leg[b / 7];
    switch (b % 7) { case 0: return l->j30; case 1: return l->j60; case 2: return l->end; case 3: return l->ry; case 4: return l->ryz; case 5: return l->ax; default: return l->axz; }
}
static void tb_attach(void) /* getJointAttachMatrix: bone * _0 (setAttachBaseMatrix: the inverse of its base) */
{
    for (int b = 0; b < 22; b++) tb_mul(tb.att[b], tb_bone_mtx(b), tb.binv[b]);
    tb.attok = 1;
}
static void tb_body_tr(void) { tb_tr(tb.body, tb.p5d4, sc->objs[tb.row].rot); } /* makeMtxTR(mBodyMtx, _5D4, mRotation) */
static void tb_cam(const char *clip, int frame) /* startAnimCameraTargetSelf (held on its last frame) / endAnimCamera */
{
    char n[40];
    if (!clip) { camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45); return; }
    snprintf(n, sizeof n, "tripodboss/%s", clip);
    int cf = canm_frames(n);
    if (cf) demo_camera(n, frame < cf ? frame : cf - 1, tb.base);
}
static void tb_ik_placement(void) /* initLegIKPlacement */
{
    float hr = 0.1f, wr = sqrtf(1.0f - hr * hr), step = 2.0943952f; /* _618 */
    V3 side = vcross(tb.axis, tb.front), b = vmul(tb.axis, hr * tb.radius), f = vmul(tb.front, wr * tb.radius);
    side = vmul(side, wr * tb.radius);
    for (int i = 0; i < 3; i++) {
        TbLeg *l = &tb.leg[i];
        float a = 0.5f * step - i * step, x = sinf(a), z = cosf(a);
        V3 shadow = v3(x, 0, z), up = v3(0, 1, 0);
        l->rlen = 4000.0f; l->mlen = 4000.0f; /* _608, _60C */
        tb_side_up(l->j0, shadow, up, v3(0, 0, 0)); /* setFirstPose */
        l->l210 = vmul(shadow, 2400.0f); /* _610 (_614 0) */
        mat_identity(l->a0);
        V3 h = vnorm(v3(l->l210.x, 0, l->l210.z));
        tb_setcol(l->a0, 0, h); tb_setcol(l->a0, 2, v3(-h.z, 0, h.x)); tb_setcol(l->a0, 3, l->l210);
        TbPoint *p = &tb.pt[i];
        p->pos = vadd(vadd(b, vadd(vmul(side, x), vmul(f, z))), tb.c);
        p->nrm = ma_normal(p->pos); p->front = ma_front(p->pos);
        l->p94 = l->p98 = i; l->fep = p->pos; /* setStepTarget */
        tb_side_up(l->end, vmul(p->nrm, -1.0f), p->front, l->fep);
        tl_set(l, TL_HOLD);
    }
}
static void tb_demo_movement(void) /* calcDemoMovement: Body (blended from _EC by _600) and the legs' end points */
{
    static const int legj[3] = {3, 4, 1}; /* sLegBoneNameTable: LeftLeg, RightLeg, BackLeg (TripodBoss.bdl joints) */
    if (!tb.anim || !tb.anim->skin || tb.anim->skin->njoints < 5) return;
    anim_update(tb.anim);
    float j[16];
    tb_mul(j, tb.base, tb.anim->world[2]);
    V3 jt = tb_col(j, 3), et = tb_col(tb.ec, 3);
    Quat q = q_slerp(tb_quat(tb.ec), tb_quat(j), tb.f600); /* blendMtx (JMAQuatLerp) */
    mat_identity(tb.body); tb_make_quat(tb.body, q);
    tb.p5d4 = vadd(vmul(et, 1.0f - tb.f600), vmul(jt, tb.f600));
    tb_setcol(tb.body, 3, tb.p5d4);
    for (int i = 0; i < 3; i++) {
        tb_mul(j, tb.base, tb.anim->world[legj[i]]);
        TbLeg *l = &tb.leg[i];
        l->fep = tb_col(j, 3);
        int on = vlen(tb_col(j, 0)) > 1.5f; /* setDemoEffectTiming(scale.x > 1.5) */
        l->demofx = on ? (l->demofx == 0 ? 1 : 2) : 0;
    }
    for (int i = 0; i < 3; i++) { tl_exe(&tb.leg[i]); tl_control(&tb.leg[i]); } /* calcLegMovement */
}
static void tb_legs(void) { for (int i = 0; i < 3; i++) { tl_exe(&tb.leg[i]); tl_control(&tb.leg[i]); } } /* calcLegMovement */
static void tb_body_move(void) /* calcBodyMovement, addAccelToWeightPosition */
{
    V3 lo1 = tb.p5c8, hi1 = tb.p5c8, lo2 = tb.p5c8, hi2 = tb.p5c8;
    for (int i = 0; i < 3; i++) {
        TbLeg *l = &tb.leg[i];
        V3 e = l->fep;
        int w = l->nrv == TL_HOLD || l->nrv == TL_LANDING || (l->nrv == TL_LEAVEOUT && l->st < 60); /* canWeighting */
        if (w) { lo1 = v3(fminf(lo1.x, e.x), fminf(lo1.y, e.y), fminf(lo1.z, e.z)); hi1 = v3(fmaxf(hi1.x, e.x), fmaxf(hi1.y, e.y), fmaxf(hi1.z, e.z)); }
        lo2 = v3(fminf(lo2.x, e.x), fminf(lo2.y, e.y), fminf(lo2.z, e.z)); hi2 = v3(fmaxf(hi2.x, e.x), fmaxf(hi2.y, e.y), fmaxf(hi2.z, e.z));
    }
    V3 v20 = vmul(vadd(lo1, hi1), 0.5f), v19 = vmul(vadd(lo2, hi2), 0.5f), v18 = vadd(vmul(v19, 0.7f), vmul(v20, 0.3f));
    V3 d = vsub(v18, tb.c); v18 = vsub(d, vmul(tb.axis, vdot(d, tb.axis))); /* removeHeight */
    float r = tb.radius + 3000.0f; /* _604 (+ _5FC 0) */
    V3 v17 = vnorm(vadd(vmul(tb.axis, r), v18)), w = vadd(tb.c, vmul(v17, r)), v15 = vsub(w, tb.p5d4);
    float n = vlen(v15); if (n < 500.0f) n = 500.0f;
    tb.v5e0 = vadd(tb.v5e0, vmul(v15, 0.8f / n));
    tb.p5d4 = vadd(tb.p5d4, tb.v5e0); tb.v5e0 = vmul(tb.v5e0, 0.95f);
    tb_body_tr();
}
static int tb_all_stop(void) { for (int i = 0; i < 3; i++) if (!tl_stop(&tb.leg[i])) return 0; return 1; }
static int tb_seq_leg(int q) { return tb.pt[tb.seq[q].p[tb.seq[q].cur]].leg; }
static int tb_try_break(void) /* tryBreak: SW_B (the core's dead switch) */
{
    const Row *r = &sc->objs[tb.row];
    if (r->sw[SW_B] < 0 || !sw_on(r, SW_B)) return 0;
    for (int i = 0; i < 3; i++) if (tb.leg[i].nrv != TL_BREAK) tl_set(&tb.leg[i], TL_BREAK);
    tb.dname = "破壊"; tb.pend = TB_PAINDEMO; tb_set(TB_TRYDEMO);
    return 1;
}
static int tb_try_start_step(void)
{
    if (tb.next == -1 || tb_ss()) return 0;
    tb.cur = tb.next; tb.seq[tb.cur].cur = 0; tb_set(TB_STEP);
    return 1;
}
static int tb_try_change(void)
{
    if (tb.next == -1 || tb.next == tb.cur || tb_ss()) return 0;
    int leg = tb_seq_leg(tb.cur), any = 0;
    if (tb.leg[leg].nrv == TL_LANDING || tb.leg[leg].nrv == TL_STAMP) return 0; /* canCancelStep */
    tb.seq[tb.next].cur = 0; leg = tb_seq_leg(tb.next);
    for (int i = 0; i < 3; i++) if (i != leg && !tl_stop(&tb.leg[i])) { tl_request(&tb.leg[i], i); any = 1; }
    tb.cur = tb.next;
    tb_set(any ? TB_CHANGESEQ : TB_STEP);
    return 1;
}
static void tb_kill(void) /* kill: startAfterBossBGM, the legs, requestAppearPowerStar(_5D4), SW_DEAD */
{
    bgm_after_boss();
    tb.nrv = TB_DEAD;
    printf("tripod boss: dead, its star\n");
    row_kill(tb.row);
}
static void tb_end_demo(const char *name) { pd_end(name); for (int i = 0; i < 3; i++) if (tb.leg[i].nrv != TL_BREAK) { tb.leg[i].mode = 0; tl_set(&tb.leg[i], TL_HOLD); } }
static void tb_start_demo(void) { memcpy(tb.ec, tb.body, 64); for (int i = 0; i < 3; i++) { tb.leg[i].mode = 1; tl_set(&tb.leg[i], TL_DEMO); } } /* startDemo */
static void tb_parts_active(void);
static void tb_request_opening(void) /* requestOpeningDemo (listenStageSwitchOnA) */
{
    if (tb.nrv != TB_NONACTIVE) return;
    tb.hid638 = 0;
    tb_parts_active();
    tb.dname = "開始"; tb.pend = TB_STARTDEMO; tb_set(TB_TRYDEMO);
    printf("tripod boss: opening demo\n");
}
static int tb_try_damage_demo(void) /* tryDamageDemo (TripodBossGuardWall) */
{
    tb_set(TB_TRYDEMO); tb.dname = "ダメージ"; tb.pend = -1;
    if (pd_try_start("ダメージ", tb.row)) { tb_set(TB_DAMAGEDEMO); return 1; }
    return 0;
}
static void tb_end_damage_demo(void) /* requestEndDamageDemo: 24 star pieces */
{
    tb_end_demo("ダメージ"); tb_set(TB_WAIT);
    launch_pieces(tb_mulp(tb.body, v3(0, 3600, 0)), vnorm(tb_col(tb.body, 1)), 24, 50.0f, 60.0f);
    tb_se(SE_OJ_STAR_PIECE_BURST_F);
}
static void tb_ride(void) /* checkRideMario (ponytail: Mario's ground a part, not the gravity's host) */
{
    int on = mario_ground >= 0 && mario_ground < ninsts && tb.rbone[insts[mario_ground].row];
    if (on) tb.c630 = 120; else if (tb.c630 > 0) tb.c630--;
    int ss = tb_ss();
    tb.s634 = tb.c630 > 0 ? (ss ? 0 : 1) : (ss ? 3 : 2);
}
static void tb_bgm_state(void) /* changeBgmState */
{
    if (tb.f640 && !tb_is_demo()) { if (!bgm_stage_playing()) bgm_boss(3); tb.f640 = 0; } /* BossBgmID_TripodBossB */
    int k = tb_ss() ? 3 : 1;
    if (tb.bgm63c != k) bgm_state(k, 60);
    tb.bgm63c = k;
}
static void tb_update_boss(void)
{
    if (tb.nrv == TB_DEAD) return;
    const Row *r = &sc->objs[tb.row];
    if (tb.nrv == TB_NONACTIVE) {
        if (r->sw[SW_A] >= 0 && sw_on(r, SW_A)) tb_request_opening();
    } else { tb.next = -1; }
    /* the step start areas (TripodBossStepStartArea::movement: requestStartTripodBossStepSequence(arg0)) */
    int ta = scene_type(sc, "TripodBossStepStartArea");
    for (u32 i = 0; tb.nrv != TB_NONACTIVE && ta >= 0 && i < sc->nobjs; i++) {
        const Row *a = &sc->objs[i];
        if (a->type != (u16)ta || vlen(vsub(mario->pos, row_pos(a))) >= 500.0f * a->scale[0]) continue;
        int q = a->arg[0];
        if (q < 0 || q >= 32 || !tb.seq[q].on || !tb.seq[q].n) continue;
        if (tb.cur == -1 || tb.cur == q || tb.next == -1) tb.next = q;
    }
    if (tb.nrv != TB_NONACTIVE) { tb_ride(); tb_bgm_state(); }
    int s = ++tb.st;
    switch (tb.nrv) {
    case TB_NONACTIVE: break;
    case TB_TRYDEMO:
        if (tb.pend >= 0 && pd_try_start(tb.dname, tb.row)) tb_set(tb.pend);
        break;
    case TB_WAIT:
        tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE); tb_body_move(); tb_legs();
        if (!tb_try_break()) tb_try_start_step();
        break;
    case TB_STEP: {
        if (s == 0) {
            int q = tb.cur, leg = tb_seq_leg(q);
            if (tb.leg[leg].nrv != TL_DAMAGE && tb.leg[leg].nrv != TL_BREAK) tl_request(&tb.leg[leg], tb.seq[q].p[tb.seq[q].cur]);
            else if (++tb.seq[q].cur >= tb.seq[q].n) tb.seq[q].cur = 0;
        }
        tb_body_move(); tb_legs(); tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE);
        if (tb_try_break()) break;
        int leg = tb_seq_leg(tb.cur);
        if (tb.leg[leg].nrv == TL_DVIB || tb.leg[leg].nrv == TL_DAMAGE) { tb_set(TB_DAMAGE); break; } /* tryDamage */
        if (tb_try_change()) break;
        if (tb.leg[leg].nrv == TL_HOLD) tb_set(TB_WAITSTEP); /* tryWaitStep: isLanding */
        break;
    }
    case TB_WAITSTEP: {
        tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE); tb_body_move(); tb_legs();
        if (tb_try_break()) break;
        if (mario->press) { /* tryLeaveLegOutOfPlayer: isPlayerOnPress, the leg pressing him */
            int k = -1;
            for (int i = 0; i < 3 && k < 0; i++) if (tb.leg[i].press) k = i;
            if (k >= 0) { tl_set(&tb.leg[k], TL_LEAVEOUT); tb_set(TB_LEAVEOUT); break; }
        }
        if (tb_try_change()) break;
        if (tb.next == -1) { tb.cur = -1; tb_set(TB_WAIT); break; } /* tryEndSequence */
        if (tb_ss()) break; /* tryNextStep */
        int q = tb.cur, leg = tb_seq_leg(q);
        if (s > tb.pt[tb.seq[q].p[tb.seq[q].cur]].wait || tb.leg[leg].nrv == TL_BREAK) {
            if (++tb.seq[q].cur >= tb.seq[q].n) tb.seq[q].cur = 0;
            tb_set(TB_STEP);
        }
        break;
    }
    case TB_CHANGESEQ:
        tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE); tb_body_move(); tb_legs();
        if (!tb_try_break() && tb_all_stop() && !tb_ss()) tb_set(TB_STEP); /* tryNextSequence */
        break;
    case TB_LEAVEOUT:
        tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE); tb_body_move(); tb_legs();
        if (!mario->press) tb_set(TB_WAIT);
        break;
    case TB_DAMAGE:
        if (s > 30) {
            V3 v = vsub(tb.p5d4, tb.c); v = vlen(v) > 0 ? vnorm(v) : v;
            tb.v5e0 = s % 6 < 3 ? vsub(tb.v5e0, vmul(v, 79.5f)) : vadd(tb.v5e0, vmul(v, 80.0f));
        }
        tb_body_move(); tb_legs(); tb_lvs(0, SE_BM_LV_TRIPOD_BOTTOM_MOVE);
        if (s > 129) tb_set(TB_WAIT);
        break;
    case TB_STARTDEMO:
        if (s == 0) {
            tb_start_demo(); bgm_stop(10); tb.f600 = 1.0f;
            if (tb.anim && tb.anim->skin) { tb.anim->clip_id = -1; int ok = anim_start(tb.anim, "tripodboss/startdemo", 0); tb.anim->loop = 0; tb.anim->speed = 1; tb.anim->done = 0; tb.anim->frame = 0; printf("tripod boss: StartDemo clip %d, %d frames\n", ok, anim_length(tb.anim)); }
        }
        tb_cam("startdemo", s);
        if (s < 120) tb_lvs(1, SE_BM_LV_TRIPOD_SIREN);
        if (s == 120) bgm_boss(2); /* BossBgmID_TripodBossA */
        if (s > 120) tb_lvs(2, SE_BM_LV_TRIPOD_START_DEMO);
        tb_demo_movement();
        if (!tb.anim || !tb.anim->skin || tb.anim->done || s > 4000) {
            tb_end_demo("開始"); tb_cam(NULL, 0); tb_ik_placement(); tb_set(TB_WAIT);
            printf("tripod boss: opening done (%d)\n", s);
        }
        break;
    case TB_DAMAGEDEMO:
        if (s == 0) { bgm_stop(30); tb_start_demo(); }
        tb_lvs(1, SE_BM_LV_TRIPOD_SIREN); tb_lvs(2, SE_BM_LV_TRIPOD_MID_DEMO);
        if (s == 90) tb_se(SE_BM_TRIPOD_CANNON_APPEAR); /* sKillerGeneraterIncreaseSeTiming */
        tb_legs();
        tb.f640 = 1;
        break;
    case TB_PAINDEMO:
        if (s == 0) {
            int ground = mario->grounded;
            tb_se(SE_BM_TRIPOD_HALT); bgm_stop(30); tb_start_demo();
            (void)ground; /* (startBckPlayer BattleWait on the ground: tb_demo) */
            tb.f620 = 5.0f; tb.f600 = 0.0f;
        }
        if (s == 90) {
            V3 p = tb_mulp(tb.base, v3(0, 2160, 1260)); /* sEndMarioPosition: setPlayerBaseMtx */
            mario->pos = p; mario->up = vnorm(tb_col(tb.base, 1)); mario->face = vnorm(tb_col(tb.base, 2)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            if (tb.anim && tb.anim->skin) { tb.anim->clip_id = -1; anim_start(tb.anim, "tripodboss/enddemo", 0); tb.anim->loop = 0; tb.anim->speed = 1; tb.anim->done = 0; tb.anim->frame = 0; }
            fx_named("TripodBoss", "BreakLight", tb.body, NULL, &tb);
        }
        tb.v5bc.y = s < 90 ? ease_in_out(s, 0, 70, 3200.0f, 3400.0f) : 3400.0f + 1600.0f * (s >= 150 ? 1.0f : (float)(s - 90) / 60.0f) * (s >= 150 ? 1.0f : (float)(s - 90) / 60.0f);
        if (s >= 90) tb_cam("enddemo", s - 90);
        if (s > 90) {
            tb_lvs(1, SE_BM_LV_TRIPOD_END_DEMO); tb_lvs(2, SE_BM_LV_TRIPOD_BREAK_LIGHT);
            tb_demo_movement();
            tb.f600 += 1.0f / 30.0f; if (tb.f600 > 1.0f) tb.f600 = 1.0f;
            if (!tb.anim || !tb.anim->skin || tb.anim->done || s > 4000) tb_set(TB_BREAKDOWN);
        }
        break;
    case TB_BREAKDOWN:
        if (s == 0) { pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_STRONG); tb_se(SE_BM_TRIPOD_ALL_BREAK); printf("tripod boss: break down\n"); }
        tb_cam("enddemo", 1 << 20);
        if (s > 240) tb_set(TB_EXPLOSION); /* sBreakDownTime */
        break;
    case TB_EXPLOSION:
        if (s == 0) { pad_rumble(RUMBLE_VERY_STRONG_LONG); tb_se(SE_BM_TRIPOD_KILL_HEAD); } /* sHeadExplodeSeTiming (0) */
        tb_cam("enddemo", 1 << 20);
        if (s > 150) { tb_end_demo("破壊"); tb_cam(NULL, 0); tb_kill(); } /* sExplosionTime */
        break;
    }
}

/* --- the parts (TripodBossFixPartsBase::updateTripodMatrix, BaseMatrixFollower) --- */
static int tb_inst_bone(const Inst *in) { return in->row >= 0 && tb.rbone ? tb.rbone[in->row] - 1 : -1; }
static void tb_place_inst(Inst *in, int b, int step) /* step 0: placed again after a rebind (no turn, no carry) */
{
    const Row *r = &sc->objs[in->row];
    float local[16], old[16];
    memcpy(old, in->m, 64);
    if (step && tb.rpart[in->row] == TBP_ROTATE && tb.nrv != TB_NONACTIVE && !tb_breakdown()) { /* TripodBossRotateParts: MapPartsRotator (exeWaitOwn) */
        row_angle[in->row] = fmodf(row_angle[in->row] + r->arg[4] * 0.01f, 360.0f);
        tb.lv[3] = se_level(SE_BM_LV_TRIPOD_PARTS_MOVE, &in->m[12], tb.lv[3]);
    }
    if (tb.rpart[in->row] == TBP_ROTATE) { rotor_matrix(in); memcpy(local, in->m, 64); }
    else mat_translate_rot_scale(local, r->pos, r->rot, r->scale);
    tb_mul(in->m, tb.att[b], local);
    if (step && mario_ground == (int)(in - insts)) carry(old, in);
}
static void tb_rebind_place(Inst *in) /* game_rebind_: a part keeps its joint's place (not its design one) */
{
    int b;
    if (tb.row >= 0 && tb.attok && tb.rbone && (b = tb_inst_bone(in)) >= 0 && tb.rpart[in->row]) tb_place_inst(in, b, 0);
}
static void tb_followers(void) /* GraviryFollower::update: the gravity's records moved by its target's attach matrix */
{
    for (int k = 0; k < tb.ngf; k++) {
        Gravity *g = (Gravity *)&grav[tb.gidx[k]];
        const Gravity *g0 = &tb.grav0[k];
        int b = tb.rbone[g0->row] - 1;
        if (b < 0) continue;
        const float *a = tb.att[b];
        for (int j = 0; j < 3; j++) { V3 v = v3(g0->m[j][0], g0->m[j][1], g0->m[j][2]); V3 w = vsub(tb_mulp(a, v), tb_col(a, 3)); g->m[j][0] = w.x; g->m[j][1] = w.y; g->m[j][2] = w.z; }
        V3 t = tb_mulp(a, v3(g0->m[3][0], g0->m[3][1], g0->m[3][2]));
        g->m[3][0] = t.x; g->m[3][1] = t.y; g->m[3][2] = t.z;
    }
    if (tb.ngf) { gravity_reset(); grav_epoch++; } /* (its bounds and grid made again, up_at's answers dropped) */
}
static V3 tb_unfollow(const Row *r, V3 p) /* area_inside: an area following a joint, p taken back to its design place */
{
    if (tb.row < 0 || !tb.rbone) return p;
    int i = (int)(r - sc->objs), b;
    if (i < 0 || (u32)i >= sc->nobjs || (b = tb.rbone[i] - 1) < 0 || tb.rpart[i]) return p;
    float inv[16];
    tb_inv(inv, tb.att[b]);
    return tb_mulp(inv, p);
}
static int tb_hidden(int row) { return tb.row >= 0 && tb.rpart && tb.rpart[row] && tb.hid638; } /* hideTripodBossParts (makeActorDead) */

/* --- TripodBossKillerGenerater: a HomingKiller (initWithoutIter: its defaults) per cannon --- */
static void tbk_set(TbCannon *c, int n) { c->nrv = n; c->st = -1; }
static V3 tbk_pos(const TbCannon *c) { return tb_col(c->m, 3); }
static void tbk_break_killer(TbCannon *c) { if (c->kph) { c->kph = 0; } }
static int tbk_request_break(TbCannon *c) /* requestBreak (an explosion on its sensor) */
{
    if (c->nrv != TBK_READY && c->nrv != TBK_SHOOTSET && c->nrv != TBK_SHOOT && c->nrv != TBK_COOLDOWN) return 0;
    if (c->nrv == TBK_SHOOTSET) tbk_break_killer(c);
    tbk_set(c, TBK_STOP);
    return 1;
}
static V3 tb_row_world(int row);
static void tb_explosion(V3 p, float r); /* a killer's explosion on the parts' sensors */
/* TripodBossGuardWallPart's collision (TriPodBossGuardWall.kcl: r 739..800, y 0..440, -22.5..22.5 degrees about its Z,
 * part k turned 45k about the wall's Y): the part a sphere at p (radius r) touches, -1 none; brk: requestBreak.
 * ponytail: the kcl's band, not its triangles (the moving wall has no KCL in the port) */
static int tb_wall_part(V3 p, float r, int brk)
{
    if (tb.wall < 0 || tb.wa_nrv < 2 || tb.wa_nrv > 3) return -1;
    float ib[16]; tb_inv(ib, tb.wall_m);
    V3 l = tb_mulp(ib, p); float rr = sqrtf(l.x * l.x + l.z * l.z);
    if (l.y < -r || l.y > 440.0f + r || rr < 739.0f - r || rr > 800.0f + r) return -1;
    int k = (int)floorf(fmodf(atan2f(l.x, l.z) * 57.29578f + 22.5f + 720.0f, 360.0f) / 45.0f) & 7;
    if (tb.wp_nrv[k] != 2 && tb.wp_nrv[k] != 3) return -1; /* (Break: invalidateCollisionParts; Repair: validated) */
    if (brk && tb.wp_nrv[k] == 3) { tb.wp_nrv[k] = 4; tb.wp_st[k] = -1; printf("tripod boss: guard wall part %d breaks\n", k); } /* sendMsgEnemyAttackExplosion: requestBreak (Active) */
    return k;
}
static void tb_wall_push(void) /* the parts' collision against Mario: out of the band to its nearer side */
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    if (tb_wall_part(c, HIT_BODY_R, 0) < 0) return;
    float ib[16]; tb_inv(ib, tb.wall_m);
    V3 l = tb_mulp(ib, mario->pos); float rr = sqrtf(l.x * l.x + l.z * l.z);
    if (rr < 1e-3f) return;
    float to = rr > 769.5f ? 800.0f + HIT_BODY_R : 739.0f - HIT_BODY_R;
    l.x *= to / rr; l.z *= to / rr;
    mario->pos = tb_mulp(tb.wall_m, l);
}
static void tbk_killer(TbCannon *c) /* HomingKiller: Appear, Move (straight), Chase, Break (game.c update_killers' rules, its defaults) */
{
    Killer *k = &c->k;
    if (!c->kph) return;
    int t = ++c->kt;
    V3 g = vmul(up_at(k->pos, k->up), -1.0f);
    if (c->kph == 1) { /* exeAppear: along its front from the cannon */
        V3 f = vnorm(tb_col(c->m, 2)), base = tbk_pos(c);
        int s = t < 20 ? t : 20;
        k->front = f; k->up = vmul(g, -1.0f); k->rotz = 0;
        k->pos = vadd(base, vmul(f, 200.0f * s / 20.0f));
        if (t < 20) c->lv = se_level(SE_EM_LV_KILLER_STANDBY, &k->pos.x, c->lv);
        if (t == 20) se_start(SE_EM_KILLER_STANDBY_END, &k->pos.x);
        if (t > 20 && t < 42) { int u = t - 20; k->pos = vadd(k->pos, vmul(f, (22 - u) * sinf(u * 100.0f * 0.0174533f) * 18.0f / 22.0f)); }
        if (t >= 72) { float m[16]; basis_matrix(m, k->pos, k->up, k->front, 0); fx_emit(FX_HOMINGKILLER_SHOOT, m); se_start(SE_EM_KILLER_FIRING, &k->pos.x); c->kph = 2; c->kt = 0; }
        return;
    }
    int brk = 0;
    V3 to = vsub(mario->pos, k->pos);
    float d = vlen(to);
    if (c->kph == 2) {
        V3 lvl = vnorm(vtangent(k->front, g));
        if (vlen(lvl) > 0.5f) k->front = turn_to(k->front, lvl, 0.9995f);
        if (t > 600) brk = 1;
        if (t >= 35 && c->chase && d < 2000.0f && d > 0 && vdot(vmul(to, 1.0f / d), k->front) > -1.0f) /* initWithoutIter: cChaseStartAngle 360 */ { c->kph = 3; se_start(SE_EM_KILLER_JET, &k->pos.x); }
        k->up = vmul(g, -1.0f);
    } else {
        V3 tg = killer_target(k, g, 200.0f), ts = v3(0, 0, 0);
        float tr = 0; /* GoToTarget: a BREAKABLE_CAGE sensor (the shell's killer_terget 900, the core's body 300) in its eye (750) */
        if (tb.shell >= 0 && tb.sh_nrv == TP_WAIT) { ts = tb_row_world(tb.shell); tr = 900.0f; }
        else if (tb.core >= 0 && tb.co_nrv == TP_WAIT) { ts = tb_row_world(tb.core); tr = 300.0f; }
        if (tr > 0 && vlen(vsub(ts, k->pos)) < 750.0f + tr) { V3 tt = vnorm(vsub(ts, k->pos)); if (vdot(tt, tg) <= vdot(tt, k->front)) tg = k->front; } /* isUpdateChaseFrontVec */
        k->front = turn_to(k->front, tg, 1.0f - 3 * 0.0001f);
        killer_rotz(k, tg, g);
        if (d > 3000.0f) brk = 1;
        k->up = turn_to(k->up, vmul(g, -1.0f), cosf(2.0f * 0.0174533f));
    }
    c->lv = se_level(SE_EM_LV_KILLER_FLY, &k->pos.x, c->lv);
    if (!brk) k->pos = vadd(k->pos, vmul(k->front, 12.0f));
    Hit h[1]; h[0].inst = -9;
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    if (!brk && vlen(vsub(mc, k->pos)) < 70.0f + HIT_BODY_R) { mario_attack(ATK_EXPLOSION, k->pos); brk = 1; }
    if (!brk && (c->kph == 3 || t >= 35)) {
        tb_explosion(k->pos, 70.0f); /* attackSensor: the parts' sensors it touches (sendMsgEnemyAttackExplosion) */
        if (!c->kph) return;
        if (kcl_query(k->pos, 70.0f, h, 1)) { /* tryBindedBreak: the core's collision gets the explosion */
            brk = 1;
            if (tb.core >= 0 && tb.co_nrv == TP_WAIT && h[0].inst >= 0 && insts[h[0].inst].row == tb.core) { tb.co_nrv = TP_BREAK; tb.co_st = -1; }
        }
        if (!brk && tb_wall_part(k->pos, 70.0f, 1) >= 0) brk = 1;
    }
    if (brk) {
        float m[16];
        basis_matrix(m, k->pos, vmul(g, -1.0f), k->front, 0);
        fx_emit(FX_HOMINGKILLER_EXPLOSION, m);
        se_start(SE_EM_KILLER_EXPLOSION, &k->pos.x);
        rumble_shake_weak(k->pos, RUMBLE_STRONG, RUMBLE_MIDDLE, 1000.0f, 2000.0f);
        tb_explosion(k->pos, 70.0f);
        c->kph = 0;
    }
}
static int tbk_stepping(const TbCannon *c) { int p = tb_part_of_bone(tb_bone(c->joint)); return p >= 0 && p < 3 && !tl_stop(&tb.leg[p]); } /* isSteppingTripodBossJointID */
static void tbk_update(TbCannon *c)
{
    const Row *r = &sc->objs[c->row];
    int b = tb_bone(c->joint), s = ++c->st;
    if (b >= 0) tb_mul(c->m, tb.att[b], c->e4); else memcpy(c->m, c->e4, 64); /* updateTripodMatrix */
    int brk = tb_breakdown(), abort_ = !tb_started() || tb_is_demo();
    switch (c->nrv) {
    case TBK_NONE: if (tb_started()) tbk_set(c, c->label > 0 ? TBK_HIDE : TBK_READY); break; /* activateTripodBoss */
    case TBK_HIDE: if (tb_damage_demo_on()) tbk_set(c, TBK_SHOWDEMO); break;
    case TBK_SHOWDEMO:
        if (s == 90 && vlen(vsub(mario->pos, tbk_pos(c))) < 230.0f) { tbk_set(c, TBK_RESTART); break; } /* sDemoAnimStartDelayTime, sCanRestartPlayerDistance */
        if (!tb_damage_demo_on()) tbk_set(c, TBK_READY);
        break;
    case TBK_READY:
        if (brk) { tbk_set(c, TBK_BREAK); break; }
        if (abort_) { c->kph = 0; tbk_set(c, TBK_READY); break; }
        if (r->sw[SW_APPEAR] >= 0 && !sw_on(r, SW_APPEAR)) break; /* tryShootSetting */
        if (tbk_stepping(c) || c->kph) break;
        if (vlen(vsub(mario->pos, tbk_pos(c))) < c->dist) { c->kph = 1; c->kt = 0; c->k.pos = tbk_pos(c); c->k.up = up_at(c->k.pos, tb_col(c->m, 1)); tbk_set(c, TBK_SHOOTSET); }
        break;
    case TBK_SHOOTSET:
        if (brk) { tbk_set(c, TBK_BREAK); break; }
        if (abort_) { c->kph = 0; tbk_set(c, TBK_READY); break; }
        if (c->kph >= 2) tbk_set(c, TBK_SHOOT); /* isMoveStart */
        break;
    case TBK_SHOOT: case TBK_COOLDOWN:
        if (brk) { tbk_set(c, TBK_BREAK); break; }
        if (abort_) { c->kph = 0; tbk_set(c, TBK_READY); break; }
        if (c->nrv == TBK_SHOOT && !c->kph) tbk_set(c, TBK_COOLDOWN);
        else if (c->nrv == TBK_COOLDOWN && s > 0) tbk_set(c, TBK_READY);
        break;
    case TBK_STOP:
        if (s == 0) { fx_named("TripodBoss", "ExplosionS", c->m, NULL, NULL); se_start(SE_BM_TRIPOD_CANNON_BREAK, &c->m[12]); }
        if (s > 300) tbk_set(c, TBK_RESTART); /* sStopTime */
        break;
    case TBK_RESTART:
        if (s == 0) c->wait = 1;
        if (c->wait && vlen(vsub(mario->pos, tbk_pos(c))) >= 230.0f) { se_start(SE_BM_TRIPOD_CANNON_RESTART, &c->m[12]); c->wait = 0; c->st = 0; }
        if (!c->wait && c->st > 30) tbk_set(c, TBK_READY); /* (isBckStopped 2ndDemo: ponytail, its clip not played) */
        break;
    case TBK_BREAK: c->kph = 0; fx_named("TripodBoss", "ExplosionM", c->m, NULL, NULL); tbk_set(c, TBK_DEAD); break;
    }
    tbk_killer(c);
}

/* --- TripodBossShell / TripodBossCore / TripodBossGuardWall(Part) --- */
static Inst *tb_inst_of(int row) { for (int i = 0; i < ninsts; i++) if (insts[i].row == row) return &insts[i]; return NULL; }
static V3 tb_row_world(int row) { Inst *in = tb_inst_of(row); return in ? v3(in->m[12], in->m[13], in->m[14]) : row_pos(&sc->objs[row]); }
static void tb_explosion(V3 p, float r)
{
    if (tb.shell >= 0 && tb.sh_nrv == TP_WAIT && vlen(vsub(p, tb_row_world(tb.shell))) < 900.0f + r) { tb.sh_nrv = TP_BREAK; tb.sh_st = -1; } /* killer_terget */
    if (tb.core >= 0 && tb.co_nrv == TP_WAIT && vlen(vsub(p, tb_row_world(tb.core))) < 300.0f + r) { tb.co_nrv = TP_BREAK; tb.co_st = -1; } /* body (ATYPE_BREAKABLE_CAGE) */
    for (int i = 0; i < tb.ncan; i++) if (vlen(vsub(p, tbk_pos(&tb.can[i]))) < r) tbk_request_break(&tb.can[i]);
}
static void tb_update_shell_core(void)
{
    if (tb.shell >= 0) {
        const Row *r = &sc->objs[tb.shell];
        int s = ++tb.sh_st;
        if (tb.sh_nrv == TP_NONACTIVE && tb_started()) tb.sh_nrv = TP_WAIT;
        if (tb.sh_nrv == TP_BREAK) {
            V3 at = tb_row_world(tb.shell);
            if (s == 0) {
                se_start(SE_BM_TRIPOD_CORE_BREAK, &at.x); row_demo_hide[tb.shell] = 255; game_rebind();
                V3 d = vsub(mario->pos, at); float n = vlen(d);
                if (n <= 1000.0f && n > 0 && vdot(d, up_at(at, v3(0, 1, 0))) > 0) { m_set_jv(mario, vmul(d, 40.0f / n)); mario->grounded = 0; } /* forceJumpPlayer */
                printf("tripod boss: the shell breaks\n");
            }
            if (s == 4) { scene_stop = 30; if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1); } /* sBreakStopStep, sBreakStopFrame */
            if (s > 60) { tb.sh_nrv = TP_DEAD; row_dead[tb.shell] = 1; } /* (its Break model's clip: ponytail) */
        }
    }
    if (tb.core >= 0) {
        const Row *r = &sc->objs[tb.core];
        int s = ++tb.co_st;
        if (tb.co_nrv == TP_NONACTIVE && tb_started()) tb.co_nrv = TP_WAIT;
        if (tb.co_nrv == TP_BREAK) {
            V3 at = tb_row_world(tb.core);
            if (s == 0) {
                se_start(SE_BM_TRIPOD_CORE_BREAK, &at.x); Inst *in = tb_inst_of(tb.core);
                if (in) fx_named("TripodBossCore", "BlackSmoke", in->m, NULL, &tb.core);
                row_demo_hide[tb.core] = 255; game_rebind();
                if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
                printf("tripod boss: the core breaks\n");
            }
            if (s == 2) { scene_stop = 16; camera_shake(SHAKE_NORMAL); } /* sBreakStopStep, sBreakStopFrame */
            if (tb_breakdown()) { tb.co_nrv = TP_DEAD; row_dead[tb.core] = 1; }
        }
    }
    if (tb.wall >= 0) { /* TripodBossGuardWall: requestStart on its appear switch (the shell's dead switch) */
        const Row *r = &sc->objs[tb.wall];
        int s = ++tb.wa_st;
        if (tb.wa_nrv == 0 && r->sw[SW_APPEAR] >= 0 && sw_on(r, SW_APPEAR) && tb_started()) { tb.wa_nrv = 1; tb.wa_st = s = 0; if (tb_try_damage_demo()) tb.wa_nrv = 2; }
        else if (tb.wa_nrv == 1) { if (tb_try_damage_demo()) { tb.wa_nrv = 2; tb.wa_st = s = 0; } }
        if (tb.wa_nrv == 2) { /* exeDemo */
            if (s >= 150) { /* startAnimCameraTargetOther 2ndDemo on its CameraTargetMtx */
                V3 at = tb_col(tb.wall_m, 3), up = vnorm(tb_col(tb.wall_m, 1)), f = vsub(at, mario->pos);
                f = vsub(f, vmul(up, vdot(f, up)));
                f = vlen(f) < 1e-6f ? vnorm(tb_col(tb.wall_m, 2)) : vnorm(f);
                float cm[16]; basis_matrix(cm, at, up, f, 0);
                int cf = canm_frames("tbwall/2nddemo");
                if (cf) demo_camera("tbwall/2nddemo", s - 150 < cf ? s - 150 : cf - 1, cm);
            }
            if (s == 180) { for (int k = 0; k < 8; k++) { tb.wp_nrv[k] = 1; tb.wp_st[k] = -1; } if (tb.wanim && tb.wanim->skin) { tb.wanim->clip_id = -1; anim_start(tb.wanim, "tripodbossguardwall/2nddemo", 0); tb.wanim->loop = 0; } }
            if (s > 180) {
                int all = 1;
                for (int k = 0; k < 8; k++) if (tb.wp_nrv[k] != 3) all = 0;
                if (s < 58 + 180) tb.lv[1] = se_level(SE_BM_LV_TRIPOD_WALL_UP, &tb.wall_m[12], tb.lv[1]); /* sMoveSeLength */
                if (all) { tb_cam(NULL, 0); tb_end_damage_demo(); tb.wa_nrv = 3; printf("tripod boss: guard wall up\n"); }
            }
        }
        if (tb.wa_nrv == 3) { /* exeRotate */
            tb.wall_rot = fmodf(tb.wall_rot - 0.2f, 360.0f); /* sRotateSpeed */
            tb.lv[1] = se_level(SE_BM_LV_TRIPOD_C_WALL_MOVE, &tb.wall_m[12], tb.lv[1]);
            if (tb_breakdown()) { tb.wa_nrv = 4; row_dead[tb.wall] = 1; fx_named("TripodBoss", "ExplosionM", tb.wall_m, NULL, NULL); }
        }
        static const int timing[8] = {4, 8, 12, 16, 20, 24, 28, 32};
        for (int k = 0; k < 8 && tb.wa_nrv >= 2 && tb.wa_nrv < 4; k++) { /* TripodBossGuardWallPart: 0 NonActive 1 Demo 2 Repair 3 Active 4 Break */
            int t = ++tb.wp_st[k];
            if (tb.wp_nrv[k] == 1 && t > timing[k]) { tb.wp_nrv[k] = 2; tb.wp_st[k] = -1; }
            else if (tb.wp_nrv[k] == 2) { if (t == 38) se_start(SE_BM_TRIPOD_WALL_UP_STOP, &tb.wall_m[12]); if (t > 60) tb.wp_nrv[k] = 3; } /* sMoveStopSeTiming; (the clip's end) */
            else if (tb.wp_nrv[k] == 4) {
                if (t == 0) se_start(SE_BM_TRIPOD_C_COVER_BREAK, &tb.wall_m[12]);
                if (t > 600 && vlen(vsub(mario->pos, tb_col(tb.wall_m, 3))) > 1000.0f) { tb.wp_nrv[k] = 2; tb.wp_st[k] = -1; } /* sRepairTiming, sRepairPlayerDistance */
            }
        }
    }
}

static void tb_parts_active(void) /* activeTripodBossParts: shown, ACTMES_TRIPODBOSS_STARTED */
{
    if (tb.k1row >= 0 && tb.k1nrv == 0) { tb.k1nrv = 1; tb.k1st = -1; } /* TripodBossKinokoOneUp: makeActorAppeared (Active) */
    game_rebind();
}
/* TripodBossKinokoOneUp.cpp after Petari: dead until activeTripodBossParts; Active: _BC = its placement (RT) x the
 * boss's attach matrix of arg0's joint (concatTripodBossAttachJointMatrix), its BenefitItemOneUp (setFollowMtx(_BC),
 * appeared at the first step: Wait, taken by a touch) kept at _BC; the boss broken (isBrokenTripodBoss) or the mushroom
 * gone: it is killed, End */
static void tb_kinoko_update(void)
{
    if (tb.k1row < 0) return;
    if (!row_dead[tb.k1row]) { row_dead[tb.k1row] = 1; sw_changed = 1; } /* (no model of its own: only its mushroom is drawn) */
    if (tb.k1nrv != 1) return;
    const Row *r = &sc->objs[tb.k1row];
    int st = ++tb.k1st, b = tb_bone(r->arg[0]);
    float m8c[16], mbc[16];
    tb_tr(m8c, row_pos(r), r->rot); /* getJMapInfoMatrixFromRT */
    if (b >= 0 && tb.attok) tb_mul(mbc, tb.att[b], m8c); else memcpy(mbc, m8c, 64);
    if (st == 0) { tb.k1item = bitem_new(mbc, 0); if (tb.k1item >= 0) bitm[tb.k1item].nrv = BI_WAIT; printf("tripod boss kinoko %d: its 1-up out (bitm %d)%c", tb.k1row, tb.k1item, 10); }
    typeof(&bitm[0]) k = tb.k1item >= 0 ? &bitm[tb.k1item] : NULL;
    if (k && k->on) { /* the follow matrix */
        k->pos = v3(mbc[12], mbc[13], mbc[14]);
        memcpy(k->base, mbc, 64);
        for (int c = 0; c < 3; c++) { V3 ax = vnorm(v3(mbc[4 * c], mbc[4 * c + 1], mbc[4 * c + 2])); k->rot[4 * c] = ax.x; k->rot[4 * c + 1] = ax.y; k->rot[4 * c + 2] = ax.z; }
    }
    if (tb_breakdown() || !k || !k->on) {
        if (k && k->on) k->on = 0; /* mOneUp->kill */
        tb.k1nrv = 2;
        printf("tripod boss kinoko %d: end (%s)%c", tb.k1row, tb_breakdown() ? "broken" : "taken", 10);
    }
}
static void tb_parts_end(void) /* TripodBossFixParts::exeWait isEndBreakDownDemoTripodBoss: Break, kill after arg2 frames */
{
    int s = ++tb.brk_st, gone = 0;
    for (u32 i = 0; i < sc->nobjs; i++) {
        if (!tb.rpart[i] || row_dead[i] || (int)i == tb.shell || (int)i == tb.core || (int)i == tb.wall) continue;
        const Row *r = &sc->objs[i];
        int life = r->arg[2] >= 0 ? r->arg[2] : 180; /* _E8 */
        if (s < life) continue;
        Inst *in = tb_inst_of((int)i);
        static const u32 se[4] = {SE_BM_TRIPOD_KILL_S, SE_BM_TRIPOD_KILL_M, SE_BM_TRIPOD_KILL_M, SE_BM_TRIPOD_KILL_L};
        static const char *const fx[4] = {"ExplosionS", "ExplosionM", "ExplosionM", "ExplosionL"};
        int lv = r->arg[1]; /* _E4 */
        if (in && lv >= 0 && lv < 4) { fx_named("TripodBoss", fx[lv], in->m, NULL, NULL); se_start(se[lv], &in->m[12]); if (lv == 3) camera_shake(SHAKE_STRONG); if (lv == 2) camera_shake(SHAKE_VERY_WEAK); }
        row_dead[i] = 1; gone = 1;
    }
    if (gone) game_rebind();
}

static void update_tb(void)
{
    if (tb.row < 0 || !dx_cls_moves("TripodBoss")) return;
    if (!tb.md) tb.md = scene_model(sc, "TripodBoss"); /* (its zone's pack may come in later) */
    if (!tb.low) tb.low = scene_model(sc, "TripodBossLow");
    if (!tb.killer) tb.killer = scene_model(sc, "HomingKiller");
    if (!tb.cannon) tb.cannon = scene_model(sc, "TripodBossUpperKillerCannon");
    if (!tb.wmd) tb.wmd = scene_model(sc, "TripodBossGuardWall");
    if (tb.anim && tb.md && tb.md->skin && tb.anim->skin != tb.md->skin) { /* initPose: StartDemo at frame 0, calcDemoMovement */
        anim_bind(tb.anim, tb.md->skin); anim_start(tb.anim, "tripodboss/startdemo", 0); tb.anim->speed = 0; tb.anim->loop = 0;
        if (tb.nrv == TB_NONACTIVE) tb_demo_movement();
    }
    if (tb.wanim && tb.wmd && tb.wmd->skin && tb.wanim->skin != tb.wmd->skin) anim_bind(tb.wanim, tb.wmd->skin);
    tb_update_boss();
    tb_kinoko_update();
    if (tb.nrv == TB_NONACTIVE) { /* clippingModel: the low model far off, the parts near */
        V3 ce, cw, cu; float fy;
        camera_pose(&ce, &cw, &cu, &fy);
        V3 lo = tb.p5d4, hi = tb.p5d4; /* calcClippingSphere */
        for (int i = 0; i < 3; i++) { V3 e = tb.leg[i].fep; lo = v3(fminf(lo.x, e.x), fminf(lo.y, e.y), fminf(lo.z, e.z)); hi = v3(fmaxf(hi.x, e.x), fmaxf(hi.y, e.y), fmaxf(hi.z, e.z)); }
        tb.c5ec = vmul(vadd(lo, hi), 0.5f);
        int hid = vlen(vsub(ce, tb.c5ec)) >= 34000.0f; /* sClipFarZ (calcCameraDistanceZ) */
        if (hid != tb.hid638) { tb.hid638 = hid; game_rebind(); }
    }
    tb_attach();
    if (tb.wall >= 0) { /* TripodBossGuardWall::updateMatrix */
        const Row *w = &sc->objs[tb.wall]; float lm[16], rot[3] = {w->rot[0], w->rot[1] + tb.wall_rot, w->rot[2]};
        int b = tb_bone(w->arg[0]);
        tb_tr(lm, row_pos(w), rot);
        if (b >= 0) tb_mul(tb.wall_m, tb.att[b], lm); else memcpy(tb.wall_m, lm, 64);
    }
    for (int i = 0; i < ninsts; i++) { int b = tb_inst_bone(&insts[i]); if (b >= 0 && tb.rpart[insts[i].row]) tb_place_inst(&insts[i], b, 1); }
    tb_wall_push();
    tb_followers();
    for (int i = 0; i < tb.ncan; i++) if (tb.can[i].nrv != TBK_DEAD) tbk_update(&tb.can[i]);
    tb_update_shell_core();
    if (tb.wanim && tb.wanim->skin) anim_update(tb.wanim);
    if (tb_breakdown()) tb_parts_end();
}
static void tb_draw(void)
{
    if (tb.row < 0) return;
    if (tb.nrv == TB_NONACTIVE && tb.hid638 && tb.low) render_model(tb.low, tb.base, 0x80, 0x80, 0x80); /* mLowModel */
    for (int i = 0; i < tb.ncan; i++) {
        TbCannon *c = &tb.can[i];
        if (c->nrv == TBK_DEAD || c->nrv == TBK_HIDE || tb.hid638) continue;
        if (!c->single && tb.cannon) render_model(tb.cannon, c->m, 0x80, 0x80, 0x80);
        if (c->kph && tb.killer) {
            Killer *k = &c->k;
            float cm[16], rz[16], tm[16];
            basis_matrix(tm, k->pos, vnorm(vtangent(k->up, k->front)), k->front, 0);
            mat_identity(rz); rz[0] = rz[5] = cosf(k->rotz * 0.0174533f); rz[1] = sinf(k->rotz * 0.0174533f); rz[4] = -rz[1];
            mat_mul(cm, tm, rz);
            render_model(tb.killer, cm, 0x80, 0x80, 0x80);
        }
    }
    if (tb.wall >= 0 && tb.wmd && tb.wa_nrv >= 2 && tb.wa_nrv < 4) /* the parts at 45 degrees about its Y (one pose: ponytail) */
        for (int k = 0; k < 8; k++) {
            if (tb.wp_nrv[k] != 2 && tb.wp_nrv[k] != 3) continue;
            float ry[16], m[16];
            mat_identity(ry); float a = k * 45.0f * 0.0174533f; ry[0] = cosf(a); ry[2] = -sinf(a); ry[8] = sinf(a); ry[10] = cosf(a);
            tb_mul(m, tb.wall_m, ry);
            if (tb.wmd->skin && tb.wanim && tb.wanim->skin == tb.wmd->skin) draw_skin(tb.wmd, m, tb.wanim); else render_model(tb.wmd, m, 0x80, 0x80, 0x80);
        }
}
static void tb_init(const Scene *s, const unsigned char *blob) /* TripodBoss::init, initAfterPlacement; its parts' */
{
    memset(&tb, 0, sizeof tb); tb.row = tb.shell = tb.core = tb.wall = -1;
    tb.k1row = tb.k1item = -1;
    for (u32 i = 0, tk = (u32)scene_type(s, "TripodBossKinokoOneUp"); i < s->nobjs; i++) if (s->objs[i].type == tk) tb.k1row = (int)i;
    int t = scene_type(s, "TripodBoss");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) if (s->objs[i].type == (u16)t) { tb.row = (int)i; break; }
    if (tb.row < 0) return;
    const Row *r = &s->objs[tb.row];
    tb.cur = tb.next = -1; tb.s634 = 2; tb.bgm63c = 1; tb.f600 = 1.0f; tb.f620 = 2.0f; tb.v5bc = v3(0, 3200, 0); tb.hid638 = 1;
    tb_tr(tb.base, row_pos(r), r->rot);
    tb.c = row_pos(r); tb.radius = 3000.0f; tb.axis = vnorm(tb_col(tb.base, 1)); tb.front = vnorm(tb_col(tb.base, 2)); /* initMovableArea (_61C) */
    tb.p5d4 = tb.p5c8 = vadd(tb.c, vmul(tb.axis, 3000.0f + tb.radius)); /* initBodyPosition (_604) */
    tb_body_tr();
    for (int b = 0; b < 22; b++) mat_identity(tb.binv[b]);
    tb.rbone = mem_alloc(MEM_OBJECTS, s->nobjs); memset(tb.rbone, 0, s->nobjs);
    tb.rpart = mem_alloc(MEM_OBJECTS, s->nobjs); memset(tb.rpart, 0, s->nobjs);
    tb.npt = 3;
    for (int q = 0; q < 32; q++) tb.seq[q].on = 1; /* _88 */
    tb_ik_placement();
    static const char *const fixc[] = {"TriPodBossBodyA", "TripodBossBodyABloom", "TriPodBossLeg1A", "TriPodBossLeg1B", "TriPodBossLeg1C",
        "TriPodBossLeg2A", "TriPodBossLeg2ABloom", "TriPodBossLeg3A", "TriPodBossLeg3B", "TriPodBossLeg3C", "TripodBossFoot",
        "TripodBossFootBloom", "TriPodBossLegBottom", "TripodBossEyeLight", "TripodBossEyeLightBloom", "TripodBossCore", "TripodBossShell"};
    static const char *const rotc[] = {"TriPodBossGearA", "TriPodBossGearB", "TriPodBossGearC", "TriPodBossGearSmallA", "TriPodBossGearSmallB",
        "TriPodBossGearSmallC", "TriPodBossGearBody"};
    int tj = scene_type(s, "TripodBossBaseJointPosition"), tp = scene_type(s, "TripodBossStepPoint");
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *o = &s->objs[i];
        const char *cn = s->types + 32 * o->type;
        if (o->zone != r->zone) continue;
        int kind = 0;
        for (unsigned k = 0; k < sizeof fixc / sizeof *fixc; k++) if (!strcmp(cn, fixc[k])) kind = TBP_FIX;
        for (unsigned k = 0; k < sizeof rotc / sizeof *rotc; k++) if (!strcmp(cn, rotc[k])) kind = TBP_ROTATE;
        if (kind) { tb.rpart[i] = (unsigned char)kind; tb.rbone[i] = (signed char)(tb_bone(o->arg[0]) + 1); }
        if (!strcmp(cn, "TripodBossShell")) tb.shell = (int)i;
        if (!strcmp(cn, "TripodBossCore")) tb.core = (int)i;
        if (!strcmp(cn, "TripodBossGuardWall")) { tb.wall = (int)i; row_demo_hide[i] = 255; } /* (its parts are drawn: tb_draw; ponytail: their collision) */
        if ((int)o->type == tj) { /* setTripodBossJointAttachBaseMatrix(makeMtxTR, arg0) */
            int b = tb_bone(o->arg[0]); float m[16];
            if (b >= 0) { tb_tr(m, row_pos(o), o->rot); tb_inv(tb.binv[b], m); }
        }
        if ((int)o->type == tp && o->arg[3] >= 0 && o->arg[3] < 32 && tb.npt < 48) { /* TripodBossStepPoint, addStepPoint */
            TbPoint *p = &tb.pt[tb.npt];
            float m[16]; tb_tr(m, row_pos(o), o->rot);
            p->prio = o->arg[0] >= 0 ? o->arg[0] : 0; p->leg = o->arg[1] >= 0 ? o->arg[1] : 0; p->wait = o->arg[2] >= 0 ? o->arg[2] : 120; p->seq = o->arg[3];
            p->pos = ma_near(tb_col(m, 3)); p->nrm = ma_normal(p->pos); p->front = ma_front(p->pos);
            int q = p->seq, n = tb.seq[q].n++, j; /* TripodBossStepSequence::addStepPoint (by mSequencePriority) */
            for (j = n; j > 0 && tb.pt[tb.seq[q].p[j - 1]].prio >= p->prio; j--) tb.seq[q].p[j] = tb.seq[q].p[j - 1];
            tb.seq[q].p[j] = tb.npt++;
        }
        /* TripodBossKillerGenerater / KillerGeneraterCircle (SetUpData) */
        static const float up7[] = {42, 96, 140, 180, -140, -96, -42}, un6[] = {78, 120, 160, -160, -120, -78}, bo3[] = {0, 120, 240};
        static const struct { const char *n; const float *a; int na; float c, x10, x14, x18; int label, coll; } cs[] = {
            {"TripodBossUpperKillerCannon", up7, 7, 2150.0f, 49.0f, -29.0f, 975.0f, 1, 1},
            {"TripodBossUnderKillerCannon", un6, 6, 2150.0f, 5.8f, 39.2f, 975.0f, 0, 1},
            {"TripodBossBottomKillerCannon", bo3, 3, 500.0f, -50.0f, -30.0f, -1300.0f, 0, 0}};
        if (!strcmp(cn, "TripodBossKillerGenerater") && tb.ncan < TB_CANNONS) {
            TbCannon *c = &tb.can[tb.ncan++];
            memset(c, 0, sizeof *c); c->row = (int)i; c->single = 1; c->joint = o->arg[0]; c->coll = 1;
            tb.rpart[i] = TBP_FIX; tb.rbone[i] = (signed char)(tb_bone(o->arg[0]) + 1); /* (its instance: the cannon's model and collision) */
            c->dist = o->arg[1] >= 0 ? (float)o->arg[1] : 2000.0f; c->chase = o->arg[2] != 0;
            tb_tr(c->e4, row_pos(o), o->rot); memcpy(c->m, c->e4, 64);
        }
        for (int k = 0; k < 3; k++) {
            if (strcmp(cn, cs[k].n)) continue;
            for (int a = 0; a < cs[k].na && tb.ncan < TB_CANNONS; a++) { /* placementGenerater */
                TbCannon *c = &tb.can[tb.ncan++];
                row_demo_hide[i] = 255; /* (a NameObj: no model of its own) */
                memset(c, 0, sizeof *c); c->row = (int)i; c->joint = o->arg[0]; c->label = cs[k].label; c->coll = cs[k].coll;
                c->dist = o->arg[1] >= 0 ? (float)o->arg[1] : 2000.0f; c->chase = o->arg[2] != 0;
                float ex[16], m2[16], tr[16], rot[3] = {-cs[k].x10, cs[k].a[a], 0}, one[3] = {1, 1, 1}, zero[3] = {0, 0, 0};
                float ca = cosf(-cs[k].x14 * 0.0174533f), sa = sinf(-cs[k].x14 * 0.0174533f);
                mat_identity(ex); ex[5] = ca; ex[6] = sa; ex[9] = -sa; ex[10] = ca; ex[14] = cs[k].c; /* setEulerX, setTrans(0, 0, _34) */
                mat_translate_rot_scale(m2, zero, rot, one); /* setRotate */
                tb_mul(m2, m2, ex);
                m2[13] += cs[k].x18; /* addTransMtx (0, _30, 0) */
                tb_tr(tr, row_pos(o), o->rot);
                tb_mul(c->e4, tr, m2); memcpy(c->m, c->e4, 64);
            }
        }
    }
    /* the followers: FOLW (row, the l_id of its TripodBossBaseJointPosition) */
    u32 fsz;
    const unsigned char *f = blob_chunk(blob, "FOLW", &fsz);
    int nf = f ? *(const int *)f : 0;
    const short *fp = f ? (const short *)(f + 4) : NULL;
    for (int k = 0; k < nf; k++) {
        int row = fp[2 * k], lid = fp[2 * k + 1];
        if (row < 0 || (u32)row >= s->nobjs || s->objs[row].zone != r->zone) continue;
        for (u32 i = 0; tj >= 0 && i < s->nobjs; i++)
            if ((int)s->objs[i].type == tj && s->objs[i].zone == r->zone && s->objs[i].l_id == lid) { tb.rbone[row] = (signed char)(tb_bone(s->objs[i].arg[0]) + 1); break; }
    }
    int ng = 0;
    for (int g = 0; g < ngrav; g++) if (grav[g].row >= 0 && (u32)grav[g].row < s->nobjs && tb.rbone[grav[g].row]) ng++;
    if (ng) {
        tb.grav0 = mem_alloc(MEM_OBJECTS, ng * sizeof(Gravity)); tb.gidx = mem_alloc(MEM_OBJECTS, ng * sizeof(int));
        for (int g = 0; g < ngrav; g++) if (grav[g].row >= 0 && (u32)grav[g].row < s->nobjs && tb.rbone[grav[g].row]) { tb.grav0[tb.ngf] = grav[g]; tb.gidx[tb.ngf++] = g; }
    }
    tb.md = scene_model(s, "TripodBoss"); tb.low = scene_model(s, "TripodBossLow"); tb.killer = scene_model(s, "HomingKiller");
    tb.cannon = scene_model(s, "TripodBossKillerGenerater"); if (!tb.cannon) tb.cannon = scene_model(s, "TripodBossUpperKillerCannon");
    tb.wmd = scene_model(s, "TripodBossGuardWall");
    tb.anim = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(tb.anim, 0, sizeof(Anim));
    tb.wanim = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(tb.wanim, 0, sizeof(Anim));
    tb.nrv = TB_NONACTIVE; tb.st = -1;
    for (int i = 0; i < 3; i++) { tb.leg[i].mode = 1; tl_set(&tb.leg[i], TL_DEMO); } /* initPose: requestStartDemo */
    printf("tripod boss: row %d, %d step points, %d cannons, %d followed gravities, shell %d core %d wall %d\n", tb.row, tb.npt - 3, tb.ncan, tb.ngf, tb.shell, tb.core, tb.wall);
}

/* Test builds (XF=-DTB_TEST, TriLegLv1Galaxy/1): Mario put on the boss planet, into the SwitchCube writing its SW_A
 * (the opening demo), then up on the body (near the shell): the killers hit the shell / wall / core; till its star */
int game_test_tb(int frame)
{
    if (tb.row < 0) return -1;
    const Row *r = &sc->objs[tb.row];
    if (frame % 60 == 0)
        printf("tb test f %d: nrv %d st %d seq %d/%d legs %d %d %d, body %.0f %.0f %.0f, Mario %.0f %.0f %.0f ground %d, shell %d core %d wall %d, life %d\n",
               frame, tb.nrv, tb.st, tb.cur, tb.next, tb.leg[0].nrv, tb.leg[1].nrv, tb.leg[2].nrv, tb.p5d4.x, tb.p5d4.y, tb.p5d4.z,
               mario->pos.x, mario->pos.y, mario->pos.z, mario_ground, tb.sh_nrv, tb.co_nrv, tb.wa_nrv, game_life());
    if (life < 2) life = 3;
    if (tb.nrv == TB_DEAD) {
        static int dead_at = -1;
        if (dead_at < 0) dead_at = frame;
        for (int i = 0; frame - dead_at >= 200 && frame % 20 == 0 && i < ninsts; i++)
            if (insts[i].item == ITEM_STAR && star_wait[insts[i].row] == tb.row + 1) { int st; V3 p = star_pos(&insts[i], &st); if (st) { mario->pos = p; mario->hvel = v3(0, 0, 0); mario->vup = 0; } }
        return 100;
    }
    if (tb.nrv == TB_NONACTIVE) {
        if (frame < 60) return 0;
        V3 p = vadd(tb.c, vmul(tb.axis, -(tb.radius + 50.0f))); /* the planet's bottom: the SwitchCube (SW_A) */
        int ty = scene_type(sc, "SwitchCube");
        for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].zone == r->zone && sc->objs[i].sw[SW_A] == r->sw[SW_A]) p = row_pos(&sc->objs[i]);
        if (frame % 30 == 0) { mario->pos = p; mario->up = up_at(p, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        if (frame > 600 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1); /* (ponytail, test only: if the cube did not) */
        return 0;
    }
    if (tb.nrv == TB_WAIT || tb.nrv == TB_STEP || tb.nrv == TB_WAITSTEP || tb.nrv == TB_CHANGESEQ) {
        static int put = 0, w0 = -1, ttop = -1;
        if (w0 < 0 && tb.st >= 5) w0 = frame;
        if (w0 < 0) return tb.nrv;
        if (frame - w0 >= 60 && frame - w0 < 300) { /* (a look at the whole boss from afar) */
            V3 eye = vadd(tb.c, vadd(vmul(tb.axis, 9000.0f), vmul(tb.front, 16000.0f)));
            camera_set_override(1, eye, vadd(tb.c, vmul(tb.axis, 3000.0f)), tb.axis, 45);
        }
        if (frame - w0 == 300) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45);
        if (!put && frame - w0 >= 300 && tb.shell >= 0) { /* onto the body, 1500 off the shell: the cannons' killers chase him over it */
            /* the body's top is the disk gravity of the highest priority (1010, radius 1000, range 4000) on its bone:
             * dropped 600 over it, 1400 out towards an under cannon (within its 1200) */
            int top = -1;
            for (int k = 0; k < tb.ngf; k++) if (grav[tb.gidx[k]].kind == GRAV_DISK && (top < 0 || grav[tb.gidx[k]].priority > grav[tb.gidx[top]].priority)) top = k;
            const Gravity *tg = &grav[tb.gidx[top < 0 ? 0 : top]]; ttop = tb.gidx[top < 0 ? 0 : top];
            V3 dc = v3(tg->m[3][0], tg->m[3][1], tg->m[3][2]), up = vnorm(v3(tg->m[1][0], tg->m[1][1], tg->m[1][2])), cp = dc;
            for (int i = 0; i < tb.ncan; i++) if (tb.can[i].label == 0 && !tb.can[i].single && tb.can[i].coll) { cp = tbk_pos(&tb.can[i]); break; } /* by an under cannon */
            V3 rad = vsub(cp, dc); rad = vsub(rad, vmul(up, vdot(rad, up)));
            mario->pos = vadd(dc, vadd(vmul(up, 600.0f), vmul(vnorm(rad), 1400.0f))); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            printf("tb test: top disk %.0f %.0f %.0f, under cannon %.0f off\n", dc.x, dc.y, dc.z, vlen(vsub(cp, mario->pos)));
            int ty = scene_type(sc, "TripodBossUnderKillerCannon");
            for (u32 i = 0; ty >= 0 && i < sc->nobjs; i++) if ((int)sc->objs[i].type == ty && sc->objs[i].sw[SW_APPEAR] >= 0) sw_set(&sc->objs[i], SW_APPEAR, 1);
            put = 1; printf("tb test f %d: Mario on the body %.0f %.0f %.0f\n", frame, mario->pos.x, mario->pos.y, mario->pos.z);
        }
        if (put && tb.sh_nrv == TP_DEAD && tb.wa_nrv == 3 && tb.core >= 0) {
            /* the core (test only: the upper cannons' killers dive round the body on its gravities and seldom come
             * back over the top): an upper cannon's killer set chasing 1400 out from the wall's axis over the top,
             * aimed through a broken part's gap (else at part 0), Mario on the core's far side, 500 from the axis */
            static int ck = -1, n;
            V3 wc = tb_col(tb.wall_m, 3), wn = vnorm(tb_col(tb.wall_m, 1));
            if (ck >= 0 && tb.can[ck].kph) return tb.nrv;
            ck = -1;
            if (n++ % 90) return tb.nrv;
            int gap = 0;
            for (int k = 0; k < 8; k++) if (tb.wp_nrv[k] == 4) gap = k;
            float a = gap * 45.0f * 0.0174533f;
            V3 out = vnorm(vtangent(vsub(tb_mulp(tb.wall_m, v3(sinf(a), 0, cosf(a))), wc), wn));
            for (int i = 0; ck < 0 && i < tb.ncan; i++) {
                TbCannon *c = &tb.can[i];
                if (!c->label || c->kph || c->nrv != TBK_READY) continue;
                c->kph = 3; c->kt = 40; c->k.pos = vadd(wc, vadd(vmul(wn, 350.0f), vmul(out, 1400.0f)));
                c->k.front = vmul(out, -1.0f); c->k.up = wn; c->k.rotz = 0; tbk_set(c, TBK_SHOOT); ck = i;
            }
            if (ck < 0) return tb.nrv;
            mario->pos = vadd(wc, vadd(vmul(wn, 300.0f), vmul(out, -500.0f))); mario->up = up_at(mario->pos, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            printf("tb test f %d: killer %d at part %d's side, Mario behind the core\n", frame, ck, gap);
            return tb.nrv;
        }
        /* none flying: Mario is put on the dome by the nearest ready under (upper, after the damage demo) cannon */
        int fly = 0, nc = -1; float nd = 1e9f;
        static int tick; tick++;
        for (int i = 0; i < tb.ncan; i++) if (tb.can[i].kph && tb.can[i].coll) fly = 1;
        for (int i = 0; put && !fly && tick % 120 == 0 && i < tb.ncan; i++) {
            const TbCannon *c = &tb.can[i]; const Row *cr = &sc->objs[c->row];
            if (c->single || !c->coll || c->nrv != TBK_READY || (cr->sw[SW_APPEAR] >= 0 && !sw_on(cr, SW_APPEAR))) continue;
            float d = vlen(vsub(mario->pos, tbk_pos(c))); if (d < nd) { nd = d; nc = i; }
        }
        if (nc >= 0 && nd > 1100.0f) {
            V3 cp = tbk_pos(&tb.can[nc]); /* out along its front and up the body: the disk gravity drops him on the dome by it */
            mario->pos = vadd(cp, vadd(vmul(vnorm(tb_col(tb.can[nc].m, 2)), 400.0f), vmul(vnorm(tb_col(tb.body, 1)), 500.0f))); mario->up = up_at(mario->pos, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            printf("tb test f %d: Mario by cannon %d (label %d)\n", frame, nc, tb.can[nc].label);
        }
        /* a killer out: Mario waits on its side of the shell (core), then steps to the far side as it nears him, so it
         * flies into the shell (core) after him */
        static char side[TB_CANNONS];
        for (int i = 0; ttop >= 0 && i < tb.ncan; i++) {
            TbCannon *c = &tb.can[i];
            if (!c->kph || !c->coll) { side[i] = 0; continue; }
            const Gravity *tg = &grav[ttop]; /* (on the top, 1100 out from its centre) */
            V3 dc = v3(tg->m[3][0], tg->m[3][1], tg->m[3][2]), n = vnorm(v3(tg->m[1][0], tg->m[1][1], tg->m[1][2])), k = c->k.pos;
            V3 out = vsub(k, dc); out = vnorm(vsub(out, vmul(n, vdot(out, n))));
            if (c->kph == 2 && side[i] == 0) {
                mario->pos = vadd(dc, vadd(vmul(n, 300.0f), vmul(out, c->label ? -500.0f : 1100.0f))); side[i] = c->label ? 2 : 1; /* an upper cannon's: past the core */
            } else if (c->kph == 3 && side[i] == 1 && vlen(vsub(mario->pos, k)) < 700.0f) {
                mario->pos = vadd(dc, vadd(vmul(n, 300.0f), vmul(out, -1100.0f))); side[i] = 2;
            } else continue;
            mario->up = up_at(mario->pos, mario->up); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            printf("tb test f %d: Mario %s the %s (killer %d)\n", frame, side[i] == 1 ? "before" : "behind", tb.sh_nrv == TP_WAIT ? "shell" : "core", i);
        }
    }
    return tb.nrv;
}
