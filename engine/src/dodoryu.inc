/* Dodoryu.cpp (Major Burrows; DodoryuBank, DodoryuRabbit, DodoryuLeadHill), DodoryuDemo.cpp, DodoryuHill.cpp
 * (MogucchiHill.cpp's hill and pieces), DodoryuMove.cpp, DodoryuStateBase / Wait / Lv1 / Lv2.cpp and DodoryuUtil.cpp
 * after Petari. #included by game.c (dd_*). CosmosGardenGalaxy 2 / 4's DandelionHillZone.
 * The boss runs its states in order: Wait (under its rail, its hill at half, until SW_A, the SwitchCube, and 30
 * frames free), DemoOpening (ドドリュウ開始デモ: the OpeningDemo anim camera on the zone's placement, BGM_PINCH_3, it
 * surfaces before its rabbit on the rail), Lv1 (Hide / AppearSign / AppearStart / Appear / AppearEnd on the rail; a hip
 * drop while it is up: JumpOut, Damage, a spin: SpinOut), DemoAppear (ドドリュウ出現デモ, MBGM_BOSS_05_A, 8 star pieces),
 * Lv2 (the chase: FindPos at Mario, ReadyChase, ChaseHide / ReadyAppear / AppearSign / ChaseAppear*; a hip drop: JumpOut,
 * Escape, a spin: KnockDown), DemoAngry (ドドリュウ怒りデモ, MBGM_BOSS_05_B, 16 star pieces), Lv3 (Lv2 with
 * sDodoryuChaseDataLv3: a hip drop first brings ChaseMore), DemoDown (ドドリュウ死亡デモ); then the after-boss BGM, its
 * power star from its place, the rabbit's Return. Its movement is DodoryuSub's MoveStateHolder (Rail, Chase, Detour by
 * the DodoryuClosedCylinder, Escape, JumpOut, Null). The hill is MogucchiHill (50 DodoryuHillA / B / C pieces: Lead,
 * Wait, Fade; a hip drop within 300, a trample, a star piece breaks one, a spin it and its neighbours, the boss's eye
 * sensor ones it runs over again).
 * ponytail: no fur (initFur), its BTK / BRK / BTP (startBtk / Brk / Btp: but a clip's same-name BTP) and
 * the pieces' Normal / Fade BRKs not played, no banmt interpolation (startBck at once), no depth of field / center
 * screen blur / image effects, the eye sensor's enemy attack only on its hill's pieces (map objects: CrystalCageS
 * left), the camera register vector (declareCameraRegisterVec), isPlayerInBind / isPlayerStaggering as Mario's damage,
 * the sudden-death wipes (isStageSuddenDeathDodoryu, hasRetryGalaxySequence), the rabbit's JointRumblers, DodoryuBank
 * (appearBank never makes it appear) */
enum { DS_WAIT, DS_OPENING, DS_LV1, DS_APPEAR, DS_LV2, DS_ANGRY, DS_LV3, DS_DOWN, DS_N };
enum { DO_DEMOWAIT, DO_UNDERGROUND, DO_UNDERGROUNDHIDE, DO_APPEAR, DO_WALK, DO_HIDEREADY, DO_HIDE };
enum { DM_DEMOWAIT, DM_PLAY }; /* DemoAppear / Angry / Down */
enum { L1_HIDE, L1_APPEARSIGN, L1_APPEARSTART, L1_APPEAR, L1_APPEAREND, L1_JUMPOUT, L1_DAMAGE, L1_DIVE, L1_SPINOUT };
enum { L2_START, L2_READYCHASE, L2_CHASEHIDE, L2_READYAPPEAR, L2_APPEARSIGN, L2_CHASEAPPEARSTART, L2_CHASEAPPEAR, L2_CHASEAPPEAREND,
       L2_JUMPOUT, L2_ESCAPE, L2_ESCAPEJUMP, L2_RECOVER, L2_DIVE, L2_KNOCKDOWN, L2_LAUGH, L2_LAUGHEND, L2_FINDPOS, L2_HIDEATTACK,
       L2_CHASEMORESTART, L2_CHASEMORE, L2_CHASEMOREEND };
enum { MV_NULL, MV_RAIL, MV_CHASE, MV_DETOUR, MV_ESCAPE, MV_JUMPOUT };
enum { DDR_ESCAPEWAITING, DDR_ESCAPE, DDR_ESCAPESLOW, DDR_REST, DDR_JUMP, DDR_WAIT, DDR_RETURN, DDR_PLEASURE };
enum { DDS_HEAD, DDS_BODY, DDS_LEGL, DDS_LEGR, DDS_ARML, DDS_ARMR, DDS_TAIL, DDS_EYE, DDS_STAR, DDS_N };
typedef struct { const char *bck, *btk, *brk, *bva, *lead; } DdAnimSet;
typedef struct { /* DodoryuChaseParam, its fields in order */
    int readyChase; float x4, x8, xC, x10, x14; int appearSign; float x1C, x20, x24, x28; int readyAppearMin, readyAppearMax;
    float x34, x38, x3C, x40, x44, x48; int chaseAppearMin, chaseAppearMax, chaseAppearEnd, x58; float x5C; int x60, x64;
    float x68, x6C, x70; int x74, laugh, findPosMin, findPosMax; float x84, x88, x8C; int x90, knockDown, x98, x9C;
    float xA0, xA4, xA8, xAC; int chaseMore; float xB4, xB8; int cmAccMin, cmAccMax, cmBrkMin, cmBrkMax;
    DdAnimSet aStart, aE0, aF4, aReadyAppear, a11C, aChaseHide, aChaseAppearStart, aChaseAppear, aChaseAppearEnd, aJumpOut,
        aEscape, aRecover, aDive, aKnockDown, aLaugh, aLaughEnd, a20C, aChaseMoreStart, aChaseMore, aChaseMoreEnd;
} DdChase;
static const DdChase dd_lv2p = {60, 16.0f, 0.1f, 1.5f, 0.0f, 600.0f, 30, 9.0f, 0.1f, 1.0f, 0.0f, 45, 55, 800.0f, 180.0f, 5.0f, 0.2f,
    2.0f, 0.0f, 120, 150, 5, 250, 1500.0f, 50, 70, 8.0f, 50.0f, 1.5f, 60, 120, 60, 90, 1200.0f, 1500.0f, 5.0f, 80, -1, 51, 0,
    10.0f, 1.0f, 3.0f, 0.92f, 360, 0.01f, 7.0f, 50, 60, 10, 20,
    {"Burrow", NULL, "Normal", "Normal", NULL}, {"UnderGroundWalk", NULL, "Normal", "Normal", NULL},
    {"UnderGroundWalk", NULL, NULL, "Normal", NULL}, {"EffectWalkAppear", NULL, "Normal", NULL, "EffectWalkAppear"},
    {"EffectWalk", NULL, NULL, NULL, "EffectWalk"}, {"EffectWalkHide", NULL, NULL, NULL, "EffectWalkHide"},
    {"Appear", NULL, NULL, "Careless", "Appear"}, {"Walk", NULL, NULL, "Normal", "Walk"}, {"Hide", NULL, NULL, NULL, "Hide"},
    {"SwoonWalkStart", NULL, NULL, "Cry", NULL}, {"SwoonWalk", NULL, NULL, NULL, NULL}, {"SwoonWalkEnd", NULL, NULL, NULL, NULL},
    {"Dive", NULL, NULL, "Normal", NULL}, {"AngryPunchDamage", NULL, NULL, NULL, NULL}, {"AttackHit", NULL, NULL, NULL, "AttackHit"},
    {"Hide", NULL, NULL, NULL, "Hide"}, {"Jump", NULL, NULL, "Careless", "Jump"}, {0}, {0}, {0}};
static const DdChase dd_lv3p = {60, 20.0f, 0.2f, 1.0f, 0.0f, 500.0f, 30, 9.0f, 0.1f, 1.0f, 0.0f, 55, 65, 750.0f, 180.0f, 6.0f, 0.2f,
    2.0f, 0.99f, 60, 75, 5, 140, 1500.0f, 40, 60, 9.0f, 70.0f, 2.0f, 60, 120, 60, 90, 1500.0f, 1600.0f, 5.0f, -1, 45, -1, 1,
    18.0f, 0.5f, 0.1f, 0.96f, 240, 0.1f, 7.0f, 50, 60, 10, 20,
    {"Burrow", NULL, "Angry", "Angry", NULL}, {"UnderGroundWalk", NULL, "Angry", "Angry", NULL},
    {"UnderGroundWalk", NULL, "Angry", "Angry", NULL}, {"AngryEffectWalkAppear", NULL, NULL, NULL, "EffectWalkAppear"},
    {"AngryEffectWalk", NULL, NULL, NULL, "EffectWalk"}, {"AngryEffectWalkHide", NULL, NULL, NULL, "EffectWalkHide"},
    {"AngryAppear", NULL, NULL, NULL, "Appear"}, {"AngryWalk", NULL, NULL, NULL, "Walk"}, {"AngryHide", NULL, NULL, NULL, "Hide"},
    {"SwoonWalkStart", NULL, "CoolDown", "Cry", NULL}, {"SwoonWalk", NULL, "Normal", NULL, NULL}, {"SwoonWalkEnd", NULL, NULL, NULL, NULL},
    {"AngryDive", NULL, "AngryStart", "Angry", NULL}, {"AngryPunchDamage", NULL, NULL, NULL, NULL},
    {"AngryAttackHit", NULL, NULL, NULL, "AttackHit"}, {"AngryHide", NULL, NULL, NULL, "Hide"}, {"AngryJump", NULL, NULL, "Cry", "Jump"},
    {"AngryChaseStart", NULL, NULL, "Cry", "AngryChaseStart"}, {"AngryChase", NULL, NULL, "Angry", "AngryChase"},
    {"AngryChaseEnd", NULL, NULL, NULL, "AngryChaseEnd"}};
typedef struct { /* DodoryuStateLv2 */
    const DdChase *cp;
    V3 v94, vA0;
    int readyAppearStep, chaseAppearStep, chaseAppearEndCounter, c8, cc, findPosStep, chaseMoreCounter, chaseMoreBrake, staggering;
} DdLv2;
#define DD_PIECES 50
typedef struct { int on, nrv, st, model, cc, cd, shown; float size, fr, m[16]; V3 pos; } DdPiece; /* MogucchiHillPiece */
static DdPiece ddp[DD_PIECES];
static struct { /* DodoryuHill (MogucchiHill) */
    int on, nrv, st, cc, n98, ec, f0, appear; /* _CC, _98, _EC, _F0, mAppearNum */
    float rm[16];                            /* mReserveMtx */
} ddh;
static struct {
    int row, zone, cur, nst[DS_N], st[DS_N];  /* _CC; each state's spine (nerve, step) */
    V3 X, Y, Z, pos, vel, grav, w134;         /* mBaseMtx's axes and trans, mVelocity, mGravity, _134 */
    int closed, on_a, rumble, rumble_n;       /* mClosedAreaObj, _144, _150, _154 */
    float zm[16];                             /* _148: the zone's placement */
    const float *rail; u32 rn; int rcl; float rlen, coord;
    int mv, mv_next; float r10, r14, c10, c14, c18, c1C; int e10, e14, e18; float e1C, e20, e24, e28; V3 e2C; float j10, j14; /* MoveStateHolder */
    int hideStep, appearStep, a4, pop; V3 d98; float popAccel, popSpeed, railSpeed, railSpeedMax; /* DodoryuStateLv1 */
    DdLv2 lv[2];                              /* DodoryuStateLv2 / Lv3 */
    int nextCounter;                          /* DodoryuStateWait */
    const char *cam; int cam_f, cam_self;     /* the demo's anim camera */
    int spin_cam; V3 spin_eye;                /* ドドリュウ吹っ飛び (CameraFixedThere) */
    int hidden, dead, sens, nostep;           /* hideModel, killed, the valid sensors' bits, invalidateShadowAll (nostep) */
    const char *bva;
    float ascy, ascv; int asc;                /* AnimScaleController _C.y, _18, its nerve (0 Stop, 2 HitReaction) */
    Anim *anim, *lh, *rb, *hp[3];
    int lh_on; float lh90;                    /* DodoryuLeadHill */
    int rb_nrv, rb_st, rb_cc, rb_d0, rb_msg, rb_talk; float rb_c4, rb_m[16], rb_prev; /* DodoryuRabbit */
    const Model *md, *lhmd, *rbmd, *hmd[3];
    u32 lv_se[4], rb_lv;
} dd = {.row = -1};
static const char *const dd_closed_cls = "DodoryuClosedCylinder";
static float dd_rnd(void) { return (float)rand() / ((float)RAND_MAX + 1.0f); } /* MR::getRandom() */
static int dd_rndi(int a, int b) { return a + (int)((float)(b - a) * dd_rnd()); } /* MR::getRandom(s32, s32) */
static float dd_rndf(float a, float b) { return a + (b - a) * dd_rnd(); }
static void dd_se(u32 id) { se_start(id, &dd.pos.x); }
static void dd_lv(int k, u32 id) { dd.lv_se[k] = se_level(id, &dd.pos.x, dd.lv_se[k]); } /* startLevelSound */
static void dd_key(char *n, int sz, const char *cls, const char *clip) /* slice.py clip_key */
{
    snprintf(n, sz, "%s/%s", cls, clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (strlen(n) > 31 && !strncmp(n, "dodoryuleadhill/", 16)) memmove(n + 4, n + 15, strlen(n + 15) + 1), memcpy(n, "ddlh", 4);
}
static void dd_bck(Anim *a, const char *cls, const char *clip) /* MR::startBck (from its start) */
{
    char n[48];
    if (!a || !a->skin || !clip) return;
    dd_key(n, sizeof n, cls, clip);
    anim_start(a, n, 0);
}
static int dd_bck_stopped(const Anim *a) { return !a || !a->skin || a->clip_id < 0 || (!a->loop && a->done); } /* isBckStopped */
static void dd_base(float m[16])
{
    mat_identity(m);
    m[0] = dd.X.x; m[1] = dd.X.y; m[2] = dd.X.z; m[4] = dd.Y.x; m[5] = dd.Y.y; m[6] = dd.Y.z;
    m[8] = dd.Z.x; m[9] = dd.Z.y; m[10] = dd.Z.z; m[12] = dd.pos.x; m[13] = dd.pos.y; m[14] = dd.pos.z;
}
static void dd_jm(int j, float o[16]) /* MR::getJointMtx */
{
    float b[16];
    dd_base(b);
    if (!dd.anim || !dd.anim->skin || (u32)j >= dd.anim->skin->njoints) { memcpy(o, b, 64); return; }
    mat_mul(o, b, dd.anim->world[j]);
}
static V3 dd_jp(int j, V3 off) /* addHitSensorAtJoint's place: the joint's matrix on the offset */
{
    float m[16];
    dd_jm(j, m);
    return v3(m[12] + m[0] * off.x + m[4] * off.y + m[8] * off.z, m[13] + m[1] * off.x + m[5] * off.y + m[9] * off.z,
              m[14] + m[2] * off.x + m[6] * off.y + m[10] * off.z);
}
static V3 dd_rotv(V3 v, V3 k, float a) /* v about the unit axis k by a (Rodrigues) */
{
    float c = cosf(a), s = sinf(a);
    return vadd(vadd(vmul(v, c), vmul(vcross(k, v), s)), vmul(k, vdot(k, v) * (1.0f - c)));
}
static void dd_rot_axes(V3 k, float a) { dd.X = dd_rotv(dd.X, k, a); dd.Y = dd_rotv(dd.Y, k, a); dd.Z = dd_rotv(dd.Z, k, a); }
/* TPos3f::setRotate(from, to) / makeRotate(from, to, max) / TQuat4f::setRotate(from, to, ratio) concatenated before
 * the base matrix: its axes turned from `a` towards `b` by at most `maxa` radians (< 0 all the way) */
static void dd_turn(V3 a, V3 b, float maxa)
{
    if (vlen(a) < 1e-6f || vlen(b) < 1e-6f) return;
    a = vnorm(a); b = vnorm(b);
    float c = vdot(a, b), ang = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);
    if (ang < 1e-6f) return;
    V3 k = vcross(a, b);
    if (vlen(k) < 1e-6f) { k = vcross(a, dd.X); if (vlen(k) < 1e-6f) k = vcross(a, dd.Y); }
    if (maxa >= 0.0f && ang > maxa) ang = maxa;
    dd_rot_axes(vnorm(k), ang);
}
static void dd_turn_up_to(V3 v) /* turnUpVecTo */
{
    dd_turn(dd.Y, v, -1.0f);
    dd.Y = vcross(dd.Z, dd.X); dd.Z = vcross(dd.X, dd.Y);
    if (vlen(dd.Y) > 1e-6f) dd.Y = vnorm(dd.Y);
    if (vlen(dd.Z) > 1e-6f) dd.Z = vnorm(dd.Z);
}
static void dd_calc_grav(void) { dd.grav = vmul(up_at(dd.pos, vmul(dd.grav, -1.0f)), -1.0f); } /* calcGravityVector */
static void dd_snap_ground(void) /* snapToGround */
{
    dd_calc_grav();
    V3 hp, n;
    int inst;
    if (map_line(vsub(dd.pos, vmul(dd.grav, 200.0f)), vmul(dd.grav, 1200.0f), &hp, &n, &inst)) {
        dd_turn_up_to(vmul(dd.grav, -1.0f));
        dd.pos = hp;
    }
}
static V3 dd_cyl_up(const Row *r) { V3 x, z; row_axes(r, &x, &z); return vnorm(vcross(z, x)); } /* calcCylinderUpVec */
static int dd_snap_wall(void) /* snapToWall */
{
    if (dd.closed < 0) return 0;
    const Row *r = &sc->objs[dd.closed];
    V3 c = row_pos(r), v = vtangent(vsub(dd.pos, c), dd_cyl_up(r));
    if (vlen(v) < 1e-6f) return 0;
    v = vnorm(v);
    dd.w134 = v;
    dd.pos = vadd(c, vmul(v, 500.0f * r->scale[0]));
    return 1;
}
static int dd_check_wall(void) /* checkWallWithVelocity */
{
    dd.closed = -1;
    int a = area_obj(dd_closed_cls, dd.pos);
    if (a < 0 || vlen(dd.vel) < 1e-6f) return 0;
    const Row *r = &sc->objs[a];
    V3 d = vtangent(vsub(dd.pos, row_pos(r)), dd_cyl_up(r));
    if (vlen(d) < 1e-6f || vdot(d, dd.vel) >= 0.0f) return 0;
    dd.w134 = vnorm(d);
    dd.closed = a;
    return 1;
}
static int dd_keep_off(V3 *dir) /* keepOffFromClosedArea */
{
    int a = area_obj(dd_closed_cls, dd.pos);
    if (a < 0) return 0;
    const Row *r = &sc->objs[a];
    V3 c = row_pos(r), up = dd_cyl_up(r), d = vsub(dd.pos, c);
    float h = vdot(up, d);
    d = vtangent(d, up);
    if (vlen(d) < 1e-6f) d = dd_rotv(dd.X, vlen(vcross(dd.Y, up)) > 1e-6f ? vnorm(vcross(dd.Y, up)) : dd.Y, acosf(fminf(fmaxf(vdot(dd.Y, up), -1.0f), 1.0f)));
    else d = vnorm(d);
    if (dir) *dir = d;
    dd.pos = vadd(vadd(c, vmul(up, h)), vmul(d, 500.0f * r->scale[0]));
    return 1;
}
static void dd_player_mtx(const float m[16]) /* MR::setPlayerBaseMtx, setPlayerStateWait */
{
    mario->pos = v3(m[12], m[13], m[14]); mario->up = vnorm(v3(m[4], m[5], m[6]));
    mario->face = vnorm(vtangent(v3(m[8], m[9], m[10]), mario->up)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
}
static int dd_name_pos(const char *name, float m[16]) /* MR::findNamePos */
{
    const float *g = scene_gpos(name);
    float one[3] = {1, 1, 1};
    if (!g) { printf("dodoryu: no %s" "\n", name); return 0; }
    mat_translate_rot_scale(m, g, g + 3, one);
    return 1;
}
static void dd_rumble(void) /* tryRumblePad */
{
    float d = vlen(vsub(mario->pos, dd.pos));
    if (d < 500.0f) m_pad_vib(0); else if (d < 1000.0f) m_pad_vib(1); /* sRumblePadNearDist / FarDist */
}
static V3 dd_mc(void) { return vadd(mario->pos, vmul(mario->up, HIT_BODY_H)); }
/* --- the rail --- */
static V3 dd_rail_at(float c) { return rail_at(dd.rail, dd.rn, c); }
static V3 dd_rail_dir(float c) /* calcRailDirectionAtCoord */
{
    float l = dd.rlen;
    V3 a = dd_rail_at(c), b = dd_rail_at(dd.rcl ? fmodf(c + 1.0f, l) : fminf(c + 1.0f, l));
    V3 d = vsub(b, a);
    if (vlen(d) < 1e-6f) d = vsub(a, dd_rail_at(c - 1.0f));
    return vlen(d) > 1e-6f ? vnorm(d) : dd.Z;
}
static void dd_move_coord(float v) /* moveCoord */
{
    dd.coord += v;
    if (dd.rcl) { dd.coord = fmodf(dd.coord, dd.rlen); if (dd.coord < 0) dd.coord += dd.rlen; }
    else dd.coord = fminf(fmaxf(dd.coord, 0.0f), dd.rlen);
}
/* --- DodoryuUtil --- */
static int dd_vert(V3 *o, V3 v) /* calcVerticalizedDir */
{
    *o = vtangent(v, dd.Y);
    if (vlen(*o) < 1e-6f) return 0;
    *o = vnorm(*o);
    return 1;
}
static void dd_turn_toward(V3 dir, float deg) { dd_turn(dd.Z, dir, deg * 3.14159265f / 180.0f); } /* turnToward */
static void dd_accel(V3 dir, float acc, float damp, float maxv) /* accelerate */
{
    V3 lat = vtangent(dd.vel, dir);
    dd.vel = vadd(vsub(dd.vel, lat), vmul(lat, damp));
    dd.vel = vadd(dd.vel, vmul(dir, acc));
    if (vlen(dd.vel) > maxv) dd.vel = vmul(vnorm(dd.vel), maxv);
    dd.vel = vtangent(dd.vel, dd.Y);
}
static void dd_add_vel(int snap) { dd.pos = vadd(dd.pos, dd.vel); if (snap) dd_snap_ground(); } /* addVelocity */
static void dd_rot_vel_wall(void) /* rotateVelocityByWall */
{
    float sp = vlen(dd.vel);
    V3 v = vtangent(dd.vel, dd.w134);
    if (vlen(v) < 1e-6f) v = vcross(dd.Y, dd.w134);
    dd.vel = vlen(v) > 1e-6f ? vmul(vnorm(v), sp) : v3(0, 0, 0);
}
static void dd_escape_dir(V3 *o, float maxa) /* calcEscapeDir */
{
    if (!dd_vert(o, vsub(dd.pos, mario->pos))) *o = dd.Z;
    float a = 2.0f * maxa * (dd_rnd() - 0.5f);
    *o = dd_rotv(*o, dd.Y, a * 3.14159265f / 180.0f);
}
/* --- DodoryuSub::MoveStateHolder --- */
static void dd_shift_rail(float v) { dd.mv_next = MV_RAIL; dd.r14 = v; }
static void dd_shift_chase(float a, float b, float c, float d)
{
    if (dd.mv != MV_CHASE && dd.mv != MV_DETOUR) dd.mv_next = MV_CHASE;
    dd.c10 = a; dd.c14 = b; dd.c18 = c; dd.c1C = d; /* (MoveStateDetour: the same four) */
}
static void dd_shift_escape(float a, int b, int c, float d, float e)
{
    if (dd.mv != MV_ESCAPE) { dd.mv_next = MV_ESCAPE; dd.e10 = 0; }
    dd.e24 = dd.e28 = a; dd.e14 = b; dd.e18 = c; dd.e1C = d; dd.e20 = e;
}
static void dd_shift_jumpout(float a, float b) { if (dd.mv != MV_JUMPOUT) dd.mv_next = MV_JUMPOUT; dd.j10 = b; dd.j14 = a; }
static void dd_shift_null(void) { dd.mv_next = MV_NULL; }
static void dd_brake(void)
{
    if (dd.mv == MV_ESCAPE) dd.e28 = 0.0f;
    else if (dd.mv == MV_CHASE) dd.vel = v3(0, 0, 0);
    else if (dd.mv == MV_RAIL) dd.r14 = 0.0f;
}
static void dd_move(void) /* MoveStateHolder::execute */
{
    if (dd.mv_next >= 0) { dd.mv = dd.mv_next; dd.mv_next = -1; }
    switch (dd.mv) {
    case MV_RAIL: /* MoveStateRail */
        dd.r14 += 0.05f; /* sRailAccel */
        if (dd.r14 > dd.r10) dd.r14 = dd.r10;
        if (!dd.rail) break;
        dd_move_coord(dd.r14);
        dd_turn(dd.Z, dd_rail_dir(dd.coord), -1.0f);
        dd.pos = dd_rail_at(dd.coord);
        dd_snap_ground();
        break;
    case MV_CHASE: { /* MoveStateChase */
        V3 to;
        if (dd_vert(&to, vsub(mario->pos, dd.pos))) { dd_turn_toward(to, dd.c1C); dd_accel(dd.Z, dd.c10, dd.c14, dd.c18); }
        if (dd_check_wall()) { dd.mv_next = MV_DETOUR; dd_rot_vel_wall(); }
        else dd_add_vel(1);
        break;
    }
    case MV_DETOUR: { /* MoveStateDetour */
        V3 a = dd.vel;
        if (vlen(a) > 1e-6f) { a = vnorm(a); dd_turn_toward(a, 1.8f); }
        else a = vcross(dd.Y, dd.w134);
        dd_accel(a, dd.c10, dd.c14, dd.c18);
        dd_add_vel(0);
        if (!dd_snap_wall()) { dd.mv_next = MV_CHASE; dd_snap_ground(); }
        else {
            dd_snap_ground(); dd_rot_vel_wall();
            if (vdot(dd.w134, vsub(mario->pos, dd.pos)) > 0.0f) dd.mv_next = MV_CHASE; /* isPlayerOppositeFromWall */
        }
        break;
    }
    case MV_ESCAPE: /* MoveStateEscape */
        if (dd.e10 == 0) { dd.e10 = dd_rndi(dd.e14, dd.e18 + 1); dd_escape_dir(&dd.e2C, dd.e1C); }
        dd.e28 += 0.05f; /* sEscapeAccel */
        if (dd.e28 > dd.e24) dd.e28 = dd.e24;
        dd_turn_toward(dd.e2C, dd.e20);
        dd.vel = vmul(dd.Z, dd.e28);
        if (dd_check_wall()) dd_rot_vel_wall();
        dd_add_vel(1);
        dd.vel = v3(0, 0, 0);
        if (--dd.e10 < 0) dd.e10 = 0;
        break;
    case MV_JUMPOUT: { /* MoveStateJumpOut */
        V3 aw = vsub(dd.pos, mario->pos);
        dd_vert(&aw, aw);
        dd_turn_toward(aw, dd.j10);
        dd.vel = vmul(aw, dd.j14);
        if (dd_check_wall()) dd_rot_vel_wall();
        dd_add_vel(1);
        dd.vel = v3(0, 0, 0);
        break;
    }
    }
}
/* --- DodoryuHill (MogucchiHill, 50 pieces) --- */
enum { H_MOVE, H_WAIT };
enum { P_APPEAR, P_WAIT, P_CRUMBLE };
static const char *const dd_hill_cls[3] = {"DodoryuHillA", "DodoryuHillB", "DodoryuHillC"};
static void ddh_reserve(void) { dd_base(ddh.rm); ddh.cc = 1; } /* reserveCurrentMtx */
static float ddh_dist(void) { return vlen(vsub(dd.pos, v3(ddh.rm[12], ddh.rm[13], ddh.rm[14]))); }
static void ddh_nrv(int n) { ddh.nrv = n; ddh.st = -1; }
static void ddh_start(void) { if (!ddh.on) ddh.on = 1; ddh_nrv(H_MOVE); ddh.f0 = 3; } /* start */
static void dd_start_hill(void) { ddh.ec = 1; ddh_start(); } /* startHill: startNaturally */
static void dd_stop_hill(void) { ddh.ec = 2; }              /* stopHill: endNaturally */
static void dd_stop_hill_force(void) { ddh_nrv(H_WAIT); }  /* stopHillForce: end */
static void dd_kill_pieces(void) { for (int i = 0; i < DD_PIECES; i++) ddp[i].on = 0; } /* killAllPieces */
static void ddp_nrv(DdPiece *p, int n) { p->nrv = n; p->st = -1; }
static void ddp_fx(DdPiece *p) { fx_named("DodoryuHill", "Break", p->m, NULL, NULL); } /* emitEffect Break (its AutoEffect "DodoryuHill") */
static void ddp_destroy(DdPiece *p) { ddp_fx(p); p->on = 0; } /* destroy */
static void ddp_crumble(DdPiece *p) /* crumble (never clipped here) */
{
    if (!p->on || p->nrv == P_CRUMBLE) return;
    ddp_nrv(p, P_CRUMBLE);
}
static void ddh_kill3(int i) /* killPieces */
{
    int idx = i - 2;
    for (int k = 0; k < 3; k++) {
        idx = (idx + DD_PIECES + 1) % DD_PIECES;
        if (ddp[idx].on) ddp_destroy(&ddp[idx]);
    }
}
static void ddh_appear_piece(void) /* appearPiece */
{
    ddp_crumble(&ddp[(DD_PIECES + (ddh.n98 - ddh.appear)) % DD_PIECES]); /* killPieceOverAppear */
    if (ddp[ddh.n98].on) ddp[ddh.n98].on = 0;                              /* killPieceIfAlive */
    if (ddh.ec == 1) { if (++ddh.f0 >= 3) { ddh.f0 = 3; ddh.ec = 0; } }
    else if (ddh.ec == 2) { if (--ddh.f0 <= 0) { ddh.f0 = 0; ddh_nrv(H_WAIT); ddh.ec = 0; return; } }
    if (ddh.f0 == 0) return;
    DdPiece *p = &ddp[ddh.n98];
    p->size = 0.6f + (1.0f - 0.6f) * (float)(ddh.f0 - 1) / 2.0f; /* setSize: sMinScale */
    memcpy(p->m, ddh.rm, 64);
    p->pos = v3(p->m[12], p->m[13], p->m[14]);
    p->on = 1; p->shown = 0; p->cc = 0; ddp_nrv(p, P_APPEAR); /* appear: hideModel, Appear */
    ddh.cc = 0;
    ddh.n98 = (ddh.n98 + DD_PIECES + 1) % DD_PIECES;
}
static float ddp_len(int model, const char *clip) { char n[40]; dd_key(n, sizeof n, dd_hill_cls[model], clip); int f = anim_clip_info(n, NULL); return f > 0 ? (float)f : 1.0f; }
static void ddh_update(void)
{
    if (ddh.on) { /* MogucchiHill: Move / Wait */
        int st = ++ddh.st;
        if (ddh.nrv == H_MOVE) {
            if (st == 0) ddh_reserve();
            if (ddh.cc && ddh_dist() >= 100.0f) ddh_appear_piece(); /* _D0 */
            if (ddh_dist() >= 150.0f) ddh_reserve();                /* _D4 */
        } else ddh.f0 = 0;
    }
    V3 mc = dd_mc();
    int hipland = mario->hip == HIP_LAND && mario->hip_t == 0;
    for (int i = 0; i < DD_PIECES; i++) {
        DdPiece *p = &ddp[i];
        if (!p->on) continue;
        int st = ++p->st;
        switch (p->nrv) { /* MogucchiHillPiece's nerves */
        case P_APPEAR:
            if (st == 0) { p->shown = 1; p->fr = 0; se_start(SE_EM_MOGHILL_APPEAR, &p->pos.x); } /* showModel, Lead, Normal BRK */
            if (++p->fr >= ddp_len(p->model, "Lead")) { p->fr = ddp_len(p->model, "Lead"); ddp_nrv(p, P_WAIT); } /* setNerveAtBckStopped */
            break;
        case P_WAIT:
            if (ddh.on && ddh.nrv == H_WAIT) break;
            if (!p->cd) p->cc = 1;
            p->cd = 0;
            break;
        case P_CRUMBLE:
            if (st == 0) p->fr = 0; /* Fade BCK / BRK */
            if (++p->fr >= ddp_len(p->model, "Fade")) { p->on = 0; continue; }
            break;
        }
        if (hipland && vlen(vsub(p->pos, mc)) < 300.0f) { ddp_destroy(p); continue; } /* control: sAppearDist */
        int crumbled = p->nrv == P_CRUMBLE && p->st >= 40; /* sCrumbleFrame */
        if (crumbled) continue;
        float r = 120.0f * p->size; /* its body sensor (sPieceSensorRadius x size) */
        int msg = mario_offence(p->pos, r, dd.grav); /* receiveMsgPlayerAttack */
        if (msg == MSG_HIPDROP) { m_pad_vib(0); se_start(SE_EM_MOGHILL_BREAK, &p->pos.x); ddp_destroy(p); continue; }
        if (msg == MSG_TRAMPLE) { m_pad_vib(0); se_start(SE_EM_MOGHILL_BREAK, &p->pos.x); ddp_destroy(p); mario_trample(); continue; }
        if (msg == MSG_HITALL) { m_pad_vib(2); se_start(SE_EM_MOGHILL_BREAK, &p->pos.x); ddh_kill3(i); continue; }
        if (vlen(vsub(mc, p->pos)) < r + HIT_BODY_R) { /* attackSensor */
            if (dm_damaging()) { ddp_destroy(p); continue; }
            if (mario->hip == HIP_FALL) continue;
            V3 mv = vadd(mario->hvel, vmul(mario->up, mario->vup));
            if (vdot(vsub(mc, p->pos), mv) >= 0.0f) continue; /* isTargetGoingAway */
            mario_push_out(p->pos, r);
        }
    }
}
static int ddh_shot(V3 at) /* a star piece on a piece: SE_EM_MOGHILL_BREAK, destroy */
{
    for (int i = 0; i < DD_PIECES; i++) {
        DdPiece *p = &ddp[i];
        if (!p->on || (p->nrv == P_CRUMBLE && p->st >= 40) || vlen(vsub(at, p->pos)) > 120.0f * p->size + 30.0f) continue;
        se_start(SE_EM_MOGHILL_BREAK, &p->pos.x); ddp_destroy(p);
        return 1;
    }
    return 0;
}
static void ddh_eye(V3 eye) /* Dodoryu::attackSensor: its eye sensor's sendMsgEnemyAttack on the pieces (receiveMsgEnemyAttack) */
{
    for (int i = 0; i < DD_PIECES; i++) {
        DdPiece *p = &ddp[i];
        if (!p->on || (p->nrv == P_CRUMBLE && p->st >= 40) || vlen(vsub(eye, p->pos)) >= 100.0f + 120.0f * p->size) continue;
        p->cd = 1;
        if (!p->cc) continue;
        m_pad_vib(0); se_start(SE_EM_MOGHILL_BREAK_SOFT, &p->pos.x); ddp_destroy(p);
    }
}
/* --- DodoryuLeadHill, DodoryuRabbit --- */
static void dd_lead(const char *clip) { if (!dd.lh_on) dd.lh_on = 1; dd_bck(dd.lh, "DodoryuLeadHill", clip); } /* startLeadHillBck */
static void dd_kill_lead(void) { dd.lh_on = 0; }
static float dd_rb_diff(void) { return dd.rb_c4 < dd.coord ? dd.rb_c4 + dd.rlen - dd.coord : dd.rb_c4 - dd.coord; } /* calcCoordDiff */
static void dd_rb_nrv(int n) { dd.rb_nrv = n; dd.rb_st = -1; }
static void dd_rb_pos(float v) /* updatePos */
{
    if (!dd.rail) return;
    dd.rb_c4 += v;
    if (dd.rb_c4 >= dd.rlen) dd.rb_c4 -= dd.rlen;
    V3 rp = dd_rail_at(dd.rb_c4), up = up_at(rp, v3(0, 1, 0)), g = vmul(up, -1.0f), rd = dd_rail_dir(dd.rb_c4), x = vcross(up, rd), hp, n;
    int inst;
    if (!map_line(vsub(rp, vmul(g, 200.0f)), vmul(g, 1200.0f), &hp, &n, &inst)) hp = rp;
    mat_identity(dd.rb_m);
    dd.rb_m[0] = x.x; dd.rb_m[1] = x.y; dd.rb_m[2] = x.z; dd.rb_m[4] = up.x; dd.rb_m[5] = up.y; dd.rb_m[6] = up.z;
    dd.rb_m[8] = rd.x; dd.rb_m[9] = rd.y; dd.rb_m[10] = rd.z; dd.rb_m[12] = hp.x; dd.rb_m[13] = hp.y; dd.rb_m[14] = hp.z;
}
static void dd_rb_reset(int now) { dd.rb_c4 = dd.coord + 1075.0f; dd_rb_nrv(DDR_ESCAPE); if (now) dd_rb_pos(0.0f); } /* reset */
static void dd_rb_node(int n) { if (dd.rb_talk < 0) return; talk_reset_node(dd.rb_talk); for (int i = 0; i < n; i++) talk_forward_node(dd.rb_talk); } /* resetAndForwardNode */
static int dd_rb_pass(float f) { const Anim *a = dd.rb; return a && a->clip_id >= 0 && dd.rb_prev < f && a->frame >= f; } /* checkPassBckFrame */
static void dd_rb_update(void)
{
    if (!dd.rb || !dd.rb->skin) return;
    dd.rb_prev = dd.rb->clip_id >= 0 ? dd.rb->frame : 0.0f;
    anim_update(dd.rb);
    int st = ++dd.rb_st, talk = dd.rb_msg && dd.rb_talk >= 0; /* tryTalk: mIsDisplayMessage */
    switch (dd.rb_nrv) {
    case DDR_ESCAPEWAITING:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "Run");
        if (talk) talk_near(dd.rb_talk);
        break;
    case DDR_ESCAPE:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "Run");
        if (dd_rb_pass(3.0f)) se_start(SE_SM_RABBIT_JUMP, &dd.rb_m[12]);
        dd_rb_pos(8.0f); /* sRabbitFastSpeed */
        if (talk) talk_near(dd.rb_talk);
        if (dd_rb_diff() > 1500.0f) dd_rb_nrv(DDR_ESCAPESLOW); /* sRabbitDistMax */
        break;
    case DDR_ESCAPESLOW:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "Walk");
        if (dd_rb_pass(3.0f)) se_start(SE_SM_RABBIT_JUMP, &dd.rb_m[12]);
        dd_rb_pos(4.0f); /* sRabbitSlowSpeed */
        if (talk) talk_near(dd.rb_talk);
        if (dd_rb_diff() < 650.0f) dd_rb_nrv(DDR_ESCAPE); else if (dd_rb_diff() > 2000.0f) dd_rb_nrv(DDR_REST); /* sRabbitDistMin, sRabbitRestDist */
        break;
    case DDR_REST:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "Tired");
        if (talk) talk_near(dd.rb_talk);
        if (dd_rb_diff() < 650.0f) dd_rb_nrv(DDR_ESCAPE);
        break;
    case DDR_JUMP:
        if (st == 0) { dd_bck(dd.rb, "DodoryuRabbit", "Jump"); memcpy(dd.rb_m, dd.zm, 64); } /* _94 = _148 */
        if (dd_bck_stopped(dd.rb)) dd_rb_nrv(DDR_WAIT);
        break;
    case DDR_WAIT:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "Wait");
        break;
    case DDR_RETURN:
        if (st == 0) dd_bck(dd.rb, "DodoryuRabbit", "StarJump");
        if (dd_bck_stopped(dd.rb)) dd_rb_nrv(DDR_PLEASURE);
        break;
    case DDR_PLEASURE:
        if (st == 0) { dd_bck(dd.rb, "DodoryuRabbit", "StarWait"); dd_rb_node(2); if (dd.rb_talk >= 0) talkers[dd.rb_talk].dist = 350.0f; }
        if (talk) talk_near(dd.rb_talk);
        break;
    }
    int esc = dd.rb_nrv == DDR_ESCAPEWAITING || dd.rb_nrv == DDR_ESCAPE || dd.rb_nrv == DDR_ESCAPESLOW;
    if (esc && --dd.rb_cc <= 0) { dd.rb_cc = 300; dd.rb_d0 = (dd.rb_d0 + 1) % 2; dd_rb_node(dd.rb_d0); } /* control */
    if (dd.rb_talk >= 0 && !(dd.rb_nrv == DDR_PLEASURE && dd.rb_st > 30)) { /* _E0: its Spine joint */
        float m[16];
        mat_mul(m, dd.rb_m, dd.rb->world[5 < dd.rb->skin->njoints ? 5 : 0]);
        talk_place(dd.rb_talk, m);
    }
    V3 sp = v3(dd.rb_m[12], dd.rb_m[13], dd.rb_m[14]); /* attackSensor: its body (50 at Spine) pushes Mario; a trample takes */
    if (dd.rb->skin->njoints > 5) { float m[16]; mat_mul(m, dd.rb_m, dd.rb->world[5]); sp = v3(m[12], m[13], m[14]); }
    if (mario_offence(sp, 50.0f, vmul(up_at(sp, v3(0, 1, 0)), -1.0f)) == MSG_TRAMPLE) mario_trample();
    else if (vlen(vsub(dd_mc(), sp)) < 50.0f + HIT_BODY_R) mario_push_out(sp, 50.0f);
}
/* --- the states --- */
static int *dd_nrvp(void) { return &dd.nst[dd.cur]; }
static int dd_step(void) { return dd.st[dd.cur]; }
static void dd_on_end(int s, int n);
static void dd_set(int n) /* Spine::setNerve: the nerve's end, the next one from step 0 */
{
    if (dd.st[dd.cur] >= 0) dd_on_end(dd.cur, dd.nst[dd.cur]);
    dd.nst[dd.cur] = n; dd.st[dd.cur] = -1;
}
static int dd_is(int n) { return dd.nst[dd.cur] == n; }
static DdLv2 *dd_l2(void) { return &dd.lv[dd.cur == DS_LV3]; }
static void dd_act(const char *clip) { dd_bck(dd.anim, "Dodoryu", clip); }
static void dd_cam_start(const char *name, int self) { static char n[40]; snprintf(n, sizeof n, "dodoryu/%s", name); for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c); dd.cam = n; dd.cam_f = 0; dd.cam_self = self; }
static int dd_cam_end(void) { return !dd.cam || dd.cam_f >= canm_frames(dd.cam); } /* isAnimCameraEnd */
static void dd_cam_stop(void) { if (dd.cam) { dd.cam = NULL; camera_override_end(-1); } } /* endAnimCamera(-1) */
static void dd_spin_cam(int on) /* startSpinOutCamera (startGlobalEventCamera ドドリュウ吹っ飛び, 120) / endSpinOutCamera (0) */
{
    if (on) { V3 w, u; float f; camera_pose(&dd.spin_eye, &w, &u, &f); dd.spin_cam = 1; }
    else if (dd.spin_cam) { dd.spin_cam = 0; camera_prog_end(0); }
}
static void dd_sens(int bits, int on) { dd.sens = on ? dd.sens | bits : dd.sens & ~bits; }
#define DD_ALL ((1 << DDS_N) - 1)
static void dd_star_pieces(int n) /* appearStarPiece(Nose, n, 20, 40), SE_OJ_STAR_PIECE_BURST */
{
    launch_pieces(dd_jp(17, v3(0, 0, 0)), vmul(dd.grav, -1.0f), n, 20.0f, 40.0f);
    dd_se(SE_OJ_STAR_PIECE_BURST);
}
static void dd_reset_cast(void) /* resetCastDisposition */
{
    float m[16];
    dd_kill_pieces();
    if (dd_name_pos("\xe3\x83\x89\xe3\x83\x89\xe3\x83\xaa\xe3\x83\xa5\xe3\x82\xa6\xe5\x86\x8d\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88", m)) { /* ドドリュウ再セット */
        dd.X = v3(m[0], m[1], m[2]); dd.Y = v3(m[4], m[5], m[6]); dd.Z = v3(m[8], m[9], m[10]); dd.pos = v3(m[12], m[13], m[14]);
    }
    dd_snap_ground();
    if (dd_name_pos("\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa\xe5\x86\x8d\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88", m)) dd_player_mtx(m); /* マリオ再セット */
}
static const char *const dd_demo_names[DS_N] = {NULL,
    "\xe3\x83\x89\xe3\x83\x89\xe3\x83\xaa\xe3\x83\xa5\xe3\x82\xa6\xe9\x96\x8b\xe5\xa7\x8b\xe3\x83\x87\xe3\x83\xa2", NULL, /* ドドリュウ開始デモ */
    "\xe3\x83\x89\xe3\x83\x89\xe3\x83\xaa\xe3\x83\xa5\xe3\x82\xa6\xe5\x87\xba\xe7\x8f\xbe\xe3\x83\x87\xe3\x83\xa2", NULL, /* ドドリュウ出現デモ */
    "\xe3\x83\x89\xe3\x83\x89\xe3\x83\xaa\xe3\x83\xa5\xe3\x82\xa6\xe6\x80\x92\xe3\x82\x8a\xe3\x83\x87\xe3\x83\xa2", NULL, /* ドドリュウ怒りデモ */
    "\xe3\x83\x89\xe3\x83\x89\xe3\x83\xaa\xe3\x83\xa5\xe3\x82\xa6\xe6\xad\xbb\xe4\xba\xa1\xe3\x83\x87\xe3\x83\xa2"}; /* ドドリュウ死亡デモ */
static void dd_start_state(void);
static void dd_end_state(void) /* mState[_CC]->end() */
{
    float m[16];
    switch (dd.cur) {
    case DS_OPENING:
        dd_cam_stop(); pd_end(dd_demo_names[DS_OPENING]);
        if (dd_name_pos("\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa\xe5\x86\x8d\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88" "1", m)) dd_player_mtx(m); /* getPlayerResetMtx(0): マリオ再セット1 */
        dd.rumble = 0; mario_hidden = 0;
        break;
    case DS_APPEAR: case DS_ANGRY: case DS_DOWN:
        dd_cam_stop(); pd_end(dd_demo_names[dd.cur]); mario_hidden = 0;
        break;
    }
}
static void dd_next_state(void) /* nextState */
{
    const Row *r = &sc->objs[dd.row];
    dd_end_state();
    if (++dd.cur >= DS_N) {
        bgm_after_boss(); /* startAfterBossBGM */
        note_star_row = dd.row; note_star_at = dd.pos; /* requestAppearPowerStar(this, mPosition) */
        dd_rb_nrv(DDR_RETURN);
        if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
        dd.dead = 1; row_kill(dd.row);
        printf("dodoryu: down, its star\n");
        return;
    }
    printf("dodoryu: state %d\n", dd.cur);
    dd_start_state(); /* (turnOnDepthOfField, setDepthOfFieldBlurIntensity 0.75: ponytail) */
}
static void dd_start_anim(const DdAnimSet *a) /* DodoryuStateLv2::startAnim */
{
    if (a->bck) dd_act(a->bck);
    if (a->bva) dd.bva = a->bva;
    if (a->lead) dd_lead(a->lead); else dd_kill_lead();
}
static void dd_start_state(void) /* start() */
{
    const Row *r = &sc->objs[dd.row];
    switch (dd.cur) {
    case DS_OPENING: case DS_APPEAR: case DS_ANGRY: case DS_DOWN: dd_set(DM_DEMOWAIT); break;
    case DS_LV1:
        dd_shift_rail(10.0f);
        if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0); /* endPlayerCenterCamera */
        dd_set(L1_HIDE);
        dd_sens(1 << DDS_STAR, 0);
        break;
    case DS_LV2: case DS_LV3: {
        DdLv2 *l = dd_l2();
        dd_set(L2_START); dd.nostep = 1; /* invalidateShadowAll */
        ddh.appear = DD_PIECES - 10; /* setHillAppearNumMax */
        dd_shift_null();
        l->chaseAppearEndCounter = 0;
        break;
    }
    }
}
/* DodoryuStateLv1 */
static void dd_l1_rail_speed(float v) { dd.railSpeedMax = v; if (dd.railSpeed > v) dd.railSpeed = v; } /* setRailSpeed */
static void dd_l1_pop(void) /* updatePop */
{
    if (!dd.pop) return;
    dd.pos = vadd(dd.pos, vmul(dd.grav, -dd.popAccel));
    dd.popSpeed += dd.popAccel;
    dd.popAccel -= 2.5f;
    if (dd.popSpeed < 0.0f) {
        dd.pos = vadd(dd.pos, vmul(dd.grav, dd.popSpeed));
        camera_shake(SHAKE_NORMAL);
        dd.pop = 0;
        float m[16]; dd_base(m); fx_named("Dodoryu", "SmallLandSmoke", m, (const float (*)[16])dd.anim->world, NULL);
    }
}
static void dd_lv1(void)
{
    int st = ++dd.st[dd.cur];
    switch (*dd_nrvp()) {
    case L1_HIDE:
        if (st == 0) {
            dd.nostep = 1; dd_start_hill(); dd_l1_rail_speed(8.0f); dd_shift_rail(dd.railSpeed); /* invalidateShadowAll, sSpeed */
            dd.hideStep = dd_rndi(150, 200); /* sHideTermMin / Max */
            camera_shake_infinity(&dd.hideStep, 0.2f, 2.0f); dd.bva = "Normal";
        }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) { dd_act("EffectWalk"); dd_lead("EffectWalk"); dd.bva = "Normal"; } /* isBckOneTimeAndStopped */
        dd_rumble(); dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_SHALLOW);
        if (st >= dd.hideStep) dd_set(L1_APPEARSIGN);
        break;
    case L1_APPEARSIGN:
        if (st == 0) { dd_act("EffectWalkHide"); dd_lead("EffectWalkHide"); dd_stop_hill(); }
        if (st >= 30) dd_set(L1_APPEARSTART); /* sAppearSignFrame */
        break;
    case L1_APPEARSTART:
        if (st == 0) {
            dd_act("OpeningAppear"); dd_lead("Appear"); dd_se(SE_BM_DODORYU_APPEAR_HEAD); dd_se(SE_BV_DODORYU_APPEAR_1);
            camera_shake(SHAKE_NORMAL); dd_l1_rail_speed(4.0f); dd_shift_rail(dd.railSpeed); dd_start_hill(); dd_sens(1 << DDS_STAR, 1); /* sAppearSpeed */
        }
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_SHALLOW);
        if (dd_bck_stopped(dd.anim)) dd_set(L1_APPEAR);
        break;
    case L1_APPEAR:
        if (st == 0) { dd_act("OpeningWalk"); dd_lead("Walk"); dd.appearStep = dd_rndi(15, 30); dd_sens(1 << DDS_STAR, 1); } /* sAppearTermMin / Max */
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND);
        if (st >= dd.appearStep) dd_set(L1_APPEAREND);
        break;
    case L1_APPEAREND:
        if (st == 0) { dd_act("Hide"); dd_lead("Hide"); dd.bva = "Normal"; }
        if (dd_bck_stopped(dd.anim)) { dd_act("EffectWalkAppear"); dd_lead("EffectWalkAppear"); dd_set(L1_HIDE); }
        break;
    case L1_JUMPOUT:
        if (st == 0) {
            dd_act("SwoonStart"); dd_se(SE_BM_DODORYU_JUMP_OUT); dd_se(SE_BV_DODORYU_HIPDRP_DAMAGE);
            dd.nostep = 0; dd_stop_hill_force(); dd_kill_lead(); dd_shift_null(); dd.bva = "Cry"; /* validateShadowAll */
        }
        if (dd_bck_stopped(dd.anim)) { dd.popAccel = 0.0f; dd.popSpeed = 0.0f; dd_set(L1_DAMAGE); }
        break;
    case L1_DAMAGE:
        if (st == 0) { dd_act("Swoon"); dd_stop_hill_force(); }
        dd_l1_pop();
        dd_lv(0, SE_BM_LV_DODORYU_SWOON); dd_lv(1, SE_BV_LV_DODORYU_SWOON);
        if (st >= 180) dd_set(L1_DIVE); /* sSwoonTerm */
        break;
    case L1_DIVE:
        if (st == 0) { dd_act("SwoonEnd"); dd_se(SE_BV_DODORYU_WAKEUP); dd.bva = "Normal"; }
        if (st == 50) dd.nostep = 1; /* sDiveShadowOffFrame: invalidateShadowAll */
        dd_l1_pop();
        if (dd_bck_stopped(dd.anim)) { dd_act("EffectWalkAppear"); dd_lead("EffectWalkAppear"); dd_set(L1_HIDE); }
        break;
    case L1_SPINOUT:
        if (st == 0) {
            dd_act("PunchDamage"); dd_se(SE_PM_SPIN_HIT); dd_stop_hill_force(); dd.bva = "Cry"; /* startBlowHitSound */
            dd_spin_cam(1); dd_se(SE_BV_DODORYU_SPIN_DMG_1); dd_sens(1 << DDS_EYE, 0);
        }
        dd.pos = vadd(dd.pos, vmul(dd.d98, 10.0f)); /* sKnockSpeed */
        dd_keep_off(NULL); dd_snap_ground(); dd_vert(&dd.d98, dd.d98);
        if (st == 60) dd_sens(1 << DDS_EYE, 1);
        if (st >= 80) { dd_spin_cam(0); dd_next_state(); dd_sens(1 << DDS_EYE, 1); } /* sSpinOutEndFrame */
        break;
    }
}
static void dd_lv1_control(void)
{
    if (dd.a4 && !dm_damaging()) dd.a4 = 0; /* isPlayerStaggering */
    if (dd.railSpeedMax > dd.railSpeed) { dd.railSpeed += 0.1f; if (dd.railSpeed > dd.railSpeedMax) dd.railSpeed = dd.railSpeedMax; } /* sRailAccel */
    dd.r14 = dd.railSpeed; /* setMoveRailSpeed */
}
static int dd_l1_attackable(void)
{
    int n = *dd_nrvp(), st = dd_step();
    return n == L1_HIDE || (n == L1_APPEARSTART && st > 16) || n == L1_APPEAR || n == L1_APPEAREND || (n == L1_DIVE && st > 20); /* sAppearStartAttackFrame, sDiveAttackInvalidFrame */
}
/* DodoryuStateLv2 (Lv2 / Lv3) */
static int dd_l2_any(const int *ns, int n) { for (int i = 0; i < n; i++) if (dd_is(ns[i])) return 1; return 0; }
static void dd_l2_hidejump(void) { dd_start_anim(&dd_l2()->cp->a20C); dd_se(SE_BM_DODORYU_HIPDRP_REACTION); } /* hideJump */
static void dd_l2_knockover(void) /* knockOver */
{
    DdLv2 *l = dd_l2();
    dd.pos = vadd(dd.pos, vmul(l->vA0, 10.0f)); /* sKnockSpeed */
    dd_keep_off(NULL); dd_snap_ground(); dd_vert(&l->vA0, l->vA0);
}
static void dd_l2_keep_vert_vel(void) { float sp = vlen(dd.vel); V3 h = vtangent(dd.vel, dd.grav); dd.vel = vlen(h) > 1e-6f ? vmul(vnorm(h), sp) : v3(0, 0, 0); } /* keepVerticalizedVelocity */
static void dd_l2_turn(V3 dir, float deg) { dd_turn(dd.Z, dir, deg * 3.14159265f / 180.0f); } /* turnToward: calcLimitedRotateMtx */
static void dd_l2_shake(int st, int at, float i, float s) { if (st == at) camera_shake_infinity(dd_l2(), i, s); if (st >= at) dd_rumble(); }
static V3 dd_to_player_h(void) { V3 v = vtangent(vsub(mario->pos, dd.pos), dd.grav); return vlen(v) > 1e-6f ? vnorm(v) : dd.Z; } /* calcVecToPlayerH */
static void dd_lv2(void)
{
    DdLv2 *l = dd_l2();
    const DdChase *p = l->cp;
    int st = ++dd.st[dd.cur];
    switch (*dd_nrvp()) {
    case L2_START:
        if (st == 0) { dd_start_anim(&p->aStart); dd_stop_hill_force(); dd_shift_null(); }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_FINDPOS);
        break;
    case L2_READYCHASE:
        if (st == 0) {
            dd_start_anim(&p->aE0); dd.hidden = 0; dd_stop_hill(); dd_sens(DD_ALL, 0); dd.anim->speed = 0.0f; /* showModel, invalidateHitSensors, stopBck */
            float m[16]; dd_base(m); fx_named("Dodoryu", "AttackSign", m, (const float (*)[16])dd.anim->world, NULL);
            dd_se(SE_BM_DODORYU_SAND_SPLASH); m_pad_vib(2); camera_shake(SHAKE_NORMAL); dd_shift_null();
        }
        dd_l2_shake(st, 0, 0.2f, 2.0f); /* updateReadyChaseShake: sReadyChaseShakeStart */
        if (st >= p->readyChase) { dd_start_anim(&p->aF4); dd_set(L2_CHASEHIDE); }
        break;
    case L2_CHASEHIDE:
        if (st == 0) { dd_shift_chase(p->x8, p->x10, p->x4, p->xC); dd_sens(DD_ALL, 1); dd_sens(1 << DDS_STAR, 0); } /* shiftChaseUnderground */
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) { dd_start_anim(&p->aF4); dd_stop_hill(); }
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_DEEP);
        if (st == 0) camera_shake_infinity(l, 0.15f, 2.0f); /* updateChaseHideShake: sChaseShakeStart / End */
        dd_rumble();
        if (st == 180) camera_shake_stop(l);
        if (vlen(vsub(mario->pos, dd.pos)) < p->x14) { dd_start_anim(&p->aReadyAppear); dd_set(L2_READYAPPEAR); } /* tryShiftToChaseAppear */
        break;
    case L2_READYAPPEAR:
        if (st == 0) {
            camera_shake_infinity(l, 0.2f, 2.0f); dd_start_hill();
            l->readyAppearStep = dd_rndi(p->readyAppearMin, p->readyAppearMax);
            dd_shift_chase(p->x20, p->x28, p->x1C, p->x24); /* shiftChaseExposingHead */
        }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_start_anim(&p->a11C);
        dd_rumble(); dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_SHALLOW);
        if (vlen(vsub(mario->pos, dd.pos)) >= p->x34) { dd_stop_hill(); dd_start_anim(&p->aChaseHide); dd_set(L2_CHASEHIDE); }
        else if (st >= l->readyAppearStep) dd_set(L2_APPEARSIGN); /* tryToShiftAppearSign */
        else if (vdot(dd.Z, dd_to_player_h()) < cosf(p->x38 * 3.14159265f / 180.0f)) dd_set(L2_APPEARSIGN); /* !isFaceToPlayerHorizontalDegree */
        break;
    case L2_APPEARSIGN:
        if (st == 0) { dd_start_anim(&p->aChaseHide); dd_stop_hill(); dd_shift_chase(p->x20, p->x28, p->x1C, p->x24); }
        if (st == 15) { float m[16]; dd_base(m); fx_named("Dodoryu", "AttackSign", m, (const float (*)[16])dd.anim->world, NULL); } /* sAppearSignEffectFrame */
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_start_anim(&p->aF4);
        if (st >= p->appearSign) dd_set(L2_CHASEAPPEARSTART);
        break;
    case L2_CHASEAPPEARSTART:
        if (st == 0) {
            dd_start_anim(&p->aChaseAppearStart); dd_start_hill(); dd_se(SE_BM_DODORYU_APPEAR_HEAD); dd_se(SE_BV_DODORYU_APPEAR_2);
            dd_shift_chase(p->x40, p->x48, p->x3C, p->x44); dd_sens(1 << DDS_STAR, 1); /* shiftChaseExposingBust */
        }
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND);
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_CHASEAPPEAR);
        break;
    case L2_CHASEAPPEAR:
        if (st == 0) { dd_start_anim(&p->aChaseAppear); dd_sens(1 << DDS_STAR, 1); }
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND);
        if (st == 0) l->chaseAppearStep = dd_rndi(p->chaseAppearMin, p->chaseAppearMax); /* tryShiftToChaseAppearEnd */
        if (st >= l->chaseAppearStep) dd_set(L2_CHASEAPPEAREND);
        break;
    case L2_CHASEAPPEAREND:
        if (st == 0) { dd_start_anim(&p->aChaseAppearEnd); l->chaseAppearEndCounter++; }
        dd_l2_shake(st, 0, 0.2f, 2.2f); /* updateHideShake */
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) {
            dd_start_anim(&p->aE0);
            dd_set(l->chaseAppearEndCounter >= p->chaseAppearEnd ? L2_FINDPOS : L2_CHASEHIDE);
        }
        break;
    case L2_JUMPOUT:
        if (st == 0) {
            dd_stop_hill_force(); l->v94 = dd.pos; dd.vel = v3(0, 0, 0); dd_start_anim(&p->aJumpOut);
            dd_se(SE_BM_DODORYU_JUMP_OUT); dd_se(SE_BV_DODORYU_HIPDRP_DAMAGE); dd.nostep = 0; dd_shift_jumpout(p->x8C, 3.0f);
        }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) { l->c8 = p->x58; l->cc = p->x74; dd_set(L2_ESCAPE); }
        break;
    case L2_ESCAPE:
        if (st == 0) { dd_start_anim(&p->aEscape); dd_shift_escape(p->x68, p->x60, p->x64, p->x6C, p->x70); dd_sens(1 << DDS_STAR, 1); }
        dd_lv(0, SE_BM_LV_DODORYU_SWOON); dd_lv(1, SE_BV_LV_DODORYU_ESCAPE);
        if (l->c8 > 0) l->c8--;
        if (l->cc > 0) l->cc--;
        if (l->cc <= 0 && (vlen(vsub(mario->pos, dd.pos)) >= p->x5C || l->c8 <= 0)) dd_set(L2_RECOVER); /* isEscapeEnd */
        break;
    case L2_ESCAPEJUMP:
        if (st == 0) { dd.vel = vmul(dd.grav, -30.0f); dd_shift_null(); }
        dd_add_vel(0);
        if (l->cc > 0) l->cc--;
        if (st >= 49) { dd_set(L2_ESCAPE); dd_snap_ground(); }
        else dd.vel = vadd(dd.vel, vmul(dd.grav, 1.2f));
        break;
    case L2_RECOVER:
        if (st == 0) { dd_start_anim(&p->aRecover); dd.vel = v3(0, 0, 0); dd_shift_null(); dd_se(SE_BV_DODORYU_WAKEUP); }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_DIVE);
        break;
    case L2_DIVE:
        if (st == 0) dd_start_anim(&p->aDive);
        dd_l2_shake(st, 40, 0.2f, 2.2f); /* updateDiveShake: sDiveShakeStartFrame */
        if (st == 50) dd.nostep = 1; /* sDiveShadowOffFrame */
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_FINDPOS);
        break;
    case L2_KNOCKDOWN:
        if (st == 0) { dd_start_anim(&p->aKnockDown); dd_se(SE_PM_SPIN_HIT); dd_se(SE_BV_DODORYU_SPIN_DMG_1); dd_shift_null(); dd_spin_cam(1); }
        if (p->x90 >= 0 && st >= p->x90) dd.vel = v3(0, 0, 0); else dd_l2_knockover();
        if ((p->knockDown >= 0 && st >= p->knockDown) || (p->knockDown < 0 && dd_bck_stopped(dd.anim) && !dd.anim->loop)) { dd_spin_cam(0); dd_next_state(); }
        break;
    case L2_LAUGH:
        if (st == 0) { dd_start_anim(&p->aLaugh); dd.vel = v3(0, 0, 0); dd_shift_null(); }
        if (st == 30) dd_se(SE_BV_DODORYU_ATK_SUCCESS); /* sLaughAtkSuccessSoundStep */
        dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND);
        if (st >= p->laugh) dd_set(L2_LAUGHEND);
        break;
    case L2_LAUGHEND:
        if (st == 0) dd_start_anim(&p->aLaughEnd);
        dd_l2_shake(st, 0, 0.2f, 2.2f);
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_FINDPOS);
        break;
    case L2_FINDPOS:
        if (st == 0) {
            l->findPosStep = dd_rndi(p->findPosMin, p->findPosMax);
            dd.anim->speed = 0.0f; dd_sens(DD_ALL, 0); dd_stop_hill_force(); /* stopBck, invalidateHitSensors */
            if (sc->objs[dd.row].sw[SW_B] >= 0) sw_set(&sc->objs[dd.row], SW_B, 1); /* startPlayerCenterCamera */
            dd.nostep = 1; dd.hidden = 1; dd_shift_null(); l->chaseAppearEndCounter = 0;
        }
        if (st == 1) { /* calcRandomVelocity */
            V3 f = dd_rotv(mario->face, vmul(mario->up, -1.0f), dd_rnd() * 2.0f * 3.14159265f);
            float sp = dd_rndf(p->x84, p->x88) / (float)l->findPosStep;
            dd.Y = mario->up; dd.Z = mario->face; dd.X = vcross(dd.Y, dd.Z); dd.pos = mario->pos; /* setMtx(getPlayerBaseMtx) */
            dd.vel = vmul(f, sp);
        }
        if (st >= 1) {
            dd_l2_keep_vert_vel(); dd_add_vel(0);
            V3 off;
            if (dd_keep_off(&off)) dd.pos = vadd(dd.pos, vmul(off, 150.0f));
            dd_snap_ground();
        }
        if (st >= l->findPosStep + 1) {
            dd_l2_turn(dd_to_player_h(), 180.0f); dd.vel = v3(0, 0, 0);
            if (sc->objs[dd.row].sw[SW_B] >= 0) sw_set(&sc->objs[dd.row], SW_B, 0); /* endPlayerCenterCamera */
            dd_set(L2_READYCHASE);
        }
        break;
    case L2_HIDEATTACK:
        if (st == 0) dd_shift_chase(p->x8, p->x10, p->x4, 0.0f); /* shiftHideAttack */
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_start_anim(&p->a11C);
        if (vlen(vsub(mario->pos, dd.pos)) > 1000.0f) { dd_stop_hill(); dd_start_anim(&p->aChaseHide); dd_set(L2_CHASEHIDE); dd_shift_null(); }
        break;
    case L2_CHASEMORESTART:
        if (st == 0) {
            dd_start_anim(&p->aChaseMoreStart); dd_stop_hill_force(); dd_shift_null(); dd_se(SE_BM_DODORYU_JUMP_OUT);
            V3 to = vsub(dd.pos, mario->pos);
            to = vlen(to) < 1e-6f ? mario->face : vnorm(to);
            int end = anim_clip_info(dd.anim->clip_id >= 0 ? sc->clips[dd.anim->clip_id].name : "", NULL);
            dd.vel = vmul(to, 1000.0f / (float)(end > 0 ? end : 1)); /* getBckCtrl()->getEnd() */
        }
        dd_lv(1, SE_BV_LV_DODORYU_COUNTER);
        if (st == 40) dd_se(SE_BM_DODORYU_HIDE_HEAD); /* sChaseMoreStartDiveSoundStep */
        dd_l2_keep_vert_vel(); dd_add_vel(1);
        dd_l2_turn(dd_to_player_h(), 180.0f);
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) { dd.vel = v3(0, 0, 0); dd_set(L2_CHASEMORE); }
        break;
    case L2_CHASEMORE:
        if (st == 0) {
            dd_start_anim(&p->aChaseMore); l->chaseMoreCounter = dd_rndi(p->cmAccMin, p->cmAccMax); l->chaseMoreBrake = 1;
            dd_start_hill(); dd_shift_chase(p->xA4, p->xAC, p->xA0, p->xA8); camera_shake_infinity(l, 0.2f, 2.0f); /* shiftChaseInAnger */
            dd_se(SE_BM_DODORYU_APPEAR_HEAD);
        }
        dd_lv(0, SE_BM_LV_DODORYU_ANGRY); dd_lv(1, SE_BM_LV_DODORYU_MOVE_GROUND); dd_lv(2, SE_BV_LV_DODORYU_ELBOW_ANG);
        dd_rumble();
        if (--l->chaseMoreCounter <= 0) {
            l->chaseMoreCounter = l->chaseMoreBrake ? dd_rndi(p->cmBrkMin, p->cmBrkMax) : dd_rndi(p->cmAccMin, p->cmAccMax);
            l->chaseMoreBrake = !l->chaseMoreBrake;
        }
        if (st >= p->chaseMore) dd_set(L2_CHASEMOREEND);
        break;
    case L2_CHASEMOREEND:
        if (st == 0) { dd_start_anim(&p->aChaseMoreEnd); dd_se(SE_BV_DODORYU_HIDE); }
        if (dd_bck_stopped(dd.anim) && !dd.anim->loop) dd_set(L2_FINDPOS);
        break;
    }
}
static void dd_on_end(int s, int n) /* the nerves' NEW_NERVE_ONEND ends */
{
    if (s == DS_LV1) {
        if (n == L1_HIDE) camera_shake_stop(&dd.hideStep);
        if (n == L1_APPEARSTART || n == L1_APPEAR) dd_sens(1 << DDS_STAR, 0);
    } else if (s == DS_LV2 || s == DS_LV3) {
        DdLv2 *l = &dd.lv[s == DS_LV3];
        switch (n) {
        case L2_READYCHASE: case L2_CHASEHIDE: case L2_READYAPPEAR: case L2_DIVE: case L2_LAUGHEND: case L2_CHASEMORE: camera_shake_stop(l); break;
        case L2_CHASEAPPEARSTART: case L2_CHASEAPPEAR: case L2_ESCAPE: dd_sens(1 << DDS_STAR, 0); break;
        case L2_CHASEAPPEAREND: camera_shake_stop(l); dd_stop_hill(); break;
        case L2_ESCAPEJUMP: dd.vel = v3(0, 0, 0); break;
        }
    }
}
static int dd_l2_enable_hip(void) /* isEnableToCatchHipDrop */
{
    static const int ns[] = {L2_CHASEHIDE, L2_READYAPPEAR, L2_CHASEAPPEARSTART, L2_CHASEAPPEAR, L2_CHASEAPPEAREND, L2_ESCAPE, L2_CHASEMORESTART, L2_CHASEMORE, L2_CHASEMOREEND};
    return dd_l2_any(ns, 9);
}
static int dd_l2_down(void) /* isDown */
{
    static const int ns[] = {L2_JUMPOUT, L2_ESCAPE, L2_ESCAPEJUMP, L2_RECOVER};
    return dd_l2_any(ns, 4) || (dd_is(L2_DIVE) && dd_step() <= 20); /* sDiveAttackInvalidFrame */
}
static int dd_l2_attackable(void) /* isAttackableNerve */
{
    static const int ns[] = {L2_CHASEHIDE, L2_READYAPPEAR, L2_CHASEAPPEARSTART, L2_CHASEAPPEAR, L2_CHASEAPPEAREND, L2_CHASEMORESTART, L2_CHASEMORE, L2_CHASEMOREEND};
    return dd_l2_any(ns, 8) || (dd_is(L2_DIVE) && dd_step() > 20);
}
static int dd_l2_reflect(void) /* isReflectSpinAttack */
{
    static const int ns[] = {L2_CHASEAPPEARSTART, L2_CHASEAPPEAR, L2_CHASEAPPEAREND, L2_CHASEMORESTART, L2_CHASEMORE, L2_CHASEMOREEND};
    return dd_l2_any(ns, 6);
}
static void dd_catch_hip(void) /* catchHipDrop */
{
    if (dd.cur == DS_LV1) {
        dd_l1_rail_speed(0.0f);
        int n = *dd_nrvp();
        float same = vdot(vmul(mario->up, -1.0f), dd.grav);
        if (n == L1_HIDE || n == L1_APPEARSIGN) { /* executeHideJump (appearBank: nothing) */
            dd_act("Jump"); dd_lead("Jump"); dd.bva = "Cry"; dd_se(SE_BM_DODORYU_HIPDRP_REACTION); dd_set(L1_HIDE);
            return;
        }
        if (same >= 0.0f && (n == L1_APPEARSTART || n == L1_APPEAR || n == L1_APPEAREND)) { scene_stop = 8; dd_set(L1_JUMPOUT); return; } /* isHipDropReactionJumpOut: reactJumpOutCommon */
        if (same >= 0.0f && n == L1_DAMAGE) { dd.pop = 1; dd.popAccel = 30.0f; dd.popSpeed = 0.0f; } /* isHipDropReactionPop: startPop */
        return;
    }
    if (dd.cur != DS_LV2 && dd.cur != DS_LV3) return;
    DdLv2 *l = dd_l2();
    if (!dd_l2_enable_hip()) return;
    dd.vel = v3(0, 0, 0);
    if (dd_is(L2_CHASEHIDE)) { float m[16]; dd_base(m); fx_named("Dodoryu", "HipDropJump", m, (const float (*)[16])dd.anim->world, NULL); dd_se(SE_BM_DODORYU_HIPDRP_REACTION); return; }
    if (dd_is(L2_READYAPPEAR) || dd_is(L2_APPEARSIGN) || (dd_is(L2_CHASEAPPEARSTART) && dd_step() <= 15)) { dd_l2_hidejump(); dd_set(L2_CHASEHIDE); return; } /* sInvalidHipDropFrame */
    if (dd_is(L2_ESCAPE)) { dd_set(L2_ESCAPEJUMP); return; }
    if (!dd_is(L2_CHASEMORESTART) && !dd_is(L2_CHASEMORE) && !dd_is(L2_CHASEMOREEND) && l->cp->x9C) { scene_stop = 8; dd_set(L2_CHASEMORESTART); return; }
    scene_stop = 8; /* reactJumpOutCommon: stopSceneForDefaultHit(sJumpOutStopSceneFrame) */
    dd_set(L2_JUMPOUT);
}
static int dd_head_needle(V3 head, V3 other) /* isHeadNeedle: Head's X along the other sensor 105 or more */
{
    float m[16];
    dd_jm(14, m);
    V3 x = vnorm(v3(m[0], m[1], m[2]));
    return vdot(x, vsub(other, head)) >= 105.0f;
}
static void dd_attack_dir(V3 *o, V3 from, V3 to) /* calcAttackDir */
{
    V3 d = vtangent(vsub(to, from), dd.grav);
    d = vlen(d) > 1e-6f ? vnorm(d) : dd.X;
    *o = vnorm(vadd(d, dd.Y));
}
static int dd_strong_dir(V3 s) { V3 d, mc = dd_mc(); dd_attack_dir(&d, s, mc); return mario_attack(ATK_STRONG, vsub(mc, d)); } /* sendMsgEnemyAttackStrongToDir */
static void dd_catch_attack(int k, V3 s, float r) /* catchAttackSensor: sensor k of radius r at s on Mario */
{
    V3 mc = dd_mc();
    if (dd.cur == DS_LV1) {
        if (k == DDS_HEAD && dd_head_needle(s, mc)) { if (!mario_attack(ATK_ENEMY, s)) mario_push_out(s, r); return; }
        if (!dd_l1_attackable() || dd.a4) { mario_push_out(s, r); return; }
        if (dd_strong_dir(s)) { camera_shake(SHAKE_NORMAL); return; }
        mario_push_out(s, r);
        return;
    }
    if (dd.cur != DS_LV2 && dd.cur != DS_LV3) return;
    if (dd_is(L2_DIVE) && k >= DDS_LEGL && k <= DDS_TAIL) return;
    if (!dd_l2_attackable() || dd_l2()->staggering) {
        if (k == DDS_HEAD && dd_head_needle(s, mc) && mario_attack(ATK_ENEMY, s)) return;
        mario_push_out(s, r);
        return;
    }
    if (mario_attack(ATK_STRONG, s)) { /* attackStrongToDir: sendMsgEnemyAttackStrong */
        camera_shake(SHAKE_NORMAL);
        if (dd_is(L2_DIVE)) return;
        dd_set(dd_is(L2_CHASEHIDE) || dd_is(L2_READYAPPEAR) || dd_is(L2_CHASEAPPEAREND) ? L2_HIDEATTACK : L2_LAUGH);
    } else mario_push_out(s, r);
}
static int dd_catch_player(int msg, int k, V3 s) /* catchPlayerAttack: Mario's msg on sensor k at s; 1 taken */
{
    V3 mc = dd_mc();
    if (dd.cur == DS_LV1) {
        int n = *dd_nrvp();
        if ((n == L1_APPEARSTART || n == L1_APPEAR || n == L1_APPEAREND) && msg == MSG_HITALL) {
            if (mario_attack(ATK_COUNTER_SPIN, s)) { dd.a4 = 1; return 1; } /* sendMsgEnemyAttackCounterSpin */
            return 0;
        }
        if (n != L1_JUMPOUT && n != L1_DAMAGE) return 0;
        if (k == DDS_HEAD && dd_head_needle(s, mc)) return 0;
        if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) return 1;
        if (msg == MSG_HITALL) {
            if (!dd_vert(&dd.d98, vsub(s, mc))) dd.d98 = vmul(dd.Z, -1.0f);
            m_pad_vib(2); scene_stop = 10; dd_set(L1_SPINOUT); /* sStopSceneFrame */
            return 1;
        }
        return 0;
    }
    if (dd.cur != DS_LV2 && dd.cur != DS_LV3) return 0;
    DdLv2 *l = dd_l2();
    if (dd_l2_reflect() && msg == MSG_HITALL && mario_attack(ATK_COUNTER_SPIN, s)) { l->staggering = 1; return 1; }
    if (!dd_l2_down()) return 0;
    if (k == DDS_HEAD && dd_head_needle(s, mc)) return 0;
    if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) return 1;
    if (msg == MSG_HITALL) {
        if (!dd_vert(&l->vA0, vsub(s, mc))) l->vA0 = vmul(dd.Z, -1.0f);
        dd_l2_turn(vmul(l->vA0, -1.0f), 180.0f);
        m_pad_vib(2); scene_stop = 10; dd_set(L2_KNOCKDOWN);
        return 1;
    }
    return 1;
}
/* the demos */
static void dd_turn_rabbit(void) /* turnToRabbit */
{
    V3 d = vtangent(vsub(v3(dd.rb_m[12], dd.rb_m[13], dd.rb_m[14]), dd.pos), dd.grav);
    if (vlen(d) > 1e-6f) dd_turn(dd.Z, vnorm(d), -1.0f);
}
static void dd_demo(void)
{
    int st = ++dd.st[dd.cur];
    const char *name = dd_demo_names[dd.cur];
    if (dd.cur == DS_OPENING) {
        switch (*dd_nrvp()) {
        case DO_DEMOWAIT:
            if (!pd_try_start(name, dd.row)) { dd.st[dd.cur] = -1; break; }
            dd.nostep = 1; se_start(SE_DM_MEET_BOSS, NULL); bgm_start("BGM_PINCH_3");
            dd_cam_start("OpeningDemo", 0); /* startAnimCameraTargetOther(_148) */
            mario_hidden = 1; dd_act("EffectWalk"); dd_lead("EffectWalk"); dd.bva = "Normal";
            dd_shift_rail(10.0f); dd_kill_pieces(); dd_stop_hill_force();
            dd.coord = 0.0f; dd_rb_reset(1); /* moveCoordToStartPos, resetRabbit */
            dd_set(DO_UNDERGROUND);
            break;
        case DO_UNDERGROUND:
            if (st == 0) { dd_start_hill(); dd.rumble = 1; dd.rumble_n = 0; }
            dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_SHALLOW); dd_turn_rabbit();
            if (st >= 60) dd_set(DO_UNDERGROUNDHIDE); /* sOpeningHideFrame */
            break;
        case DO_UNDERGROUNDHIDE:
            if (st == 0) { dd_act("EffectWalkHide"); dd_lead("EffectWalkHide"); }
            dd_turn_rabbit();
            if (dd_bck_stopped(dd.anim)) dd_set(DO_APPEAR);
            break;
        case DO_APPEAR:
            if (st == 0) { dd_act("OpeningAppear"); dd_lead("Appear"); dd_se(SE_BV_DODORYU_APPEAR_1); dd_se(SE_BM_DODORYU_APPEAR_HEAD); }
            dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND);
            if (st == 55) dd_se(SE_BV_DODORYU_FAINT_SMILE); /* sOpeningAppearSmileSoundStep */
            dd_turn_rabbit();
            if (dd_bck_stopped(dd.anim)) dd_set(DO_WALK);
            break;
        case DO_WALK:
            if (st == 0) { dd_act("OpeningWalk"); dd_lead("Walk"); }
            dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND); dd_turn_rabbit();
            if (st >= 60) { dd.rumble = 0; dd_set(DO_HIDEREADY); }
            break;
        case DO_HIDEREADY:
            if (st == 0) { dd_act("Hide"); dd_lead("Hide"); }
            dd_lv(0, SE_BM_LV_DODORYU_MOVE_GROUND); dd_turn_rabbit();
            if (dd_bck_stopped(dd.anim)) { dd_act("EffectWalkAppear"); dd_set(DO_HIDE); }
            break;
        case DO_HIDE:
            dd_turn_rabbit();
            if (dd_bck_stopped(dd.anim) && !dd.anim->loop) { dd_act("EffectWalk"); dd.rumble = 1; dd.rumble_n = 0; dd_lead("EffectWalk"); }
            break;
        }
        if (*dd_nrvp() != DO_DEMOWAIT && dd_cam_end()) { dd.rb_msg = 1; dd_next_state(); } /* control: tryFinish */
        return;
    }
    if (*dd_nrvp() == DM_DEMOWAIT) {
        if (!pd_try_start(name, dd.row)) { dd.st[dd.cur] = -1; return; }
        if (dd.cur == DS_APPEAR) {
            dd_cam_start("AppearDemo", 1); bgm_stop(60); dd.nostep = 1; dd_rb_nrv(DDR_JUMP); dd_reset_cast(); /* leaveRabbit */
        } else if (dd.cur == DS_ANGRY) {
            dd.nostep = 1; dd_reset_cast(); dd_cam_start("AngryDemo", 1);
        } else {
            dd_cam_start("DownDemo", 1); dd.nostep = 1; dd_reset_cast();
        }
        dd_set(DM_PLAY);
        return;
    }
    if (dd.cur == DS_APPEAR) {
        if (st == 0) { dd_act("AppearDemo"); dd.bva = "Normal"; mario_hidden = 1; }
        if (dd_bck_stopped(dd.anim)) { dd_next_state(); bgm_boss(6); dd_star_pieces(8); } /* BossBgmID_DodoryuA */
    } else if (dd.cur == DS_ANGRY) {
        if (st == 0) { dd_act("AngryDemo"); dd.bva = "Angry"; bgm_stop(60); mario_hidden = 1; }
        /* (step 120: startCenterScreenBlur: ponytail) */
        if (dd_bck_stopped(dd.anim)) { dd_next_state(); bgm_boss(7); dd_star_pieces(16); } /* BossBgmID_DodoryuB */
    } else {
        if (st == 0) { dd_act("DownDemo"); dd.bva = "Cry"; bgm_stop(30); mario_hidden = 1; }
        if (st == 242) dd_se(SE_BM_DODORYU_DIE); /* sDieSoundStep */
        if (dd_bck_stopped(dd.anim)) dd.hidden = 1;
        if (dd_cam_end()) dd_next_state();
    }
}
static void dd_wait(void) /* DodoryuStateWait */
{
    int st = ++dd.st[dd.cur];
    if (st == 0) { dd.nostep = 1; dd_act("UnderGroundWalk"); ddh.appear = DD_PIECES / 2; dd_start_hill(); dd_shift_rail(8.0f); } /* setHillAppearNumHalf */
    dd_lv(0, SE_BM_LV_DODORYU_MOVE_U_SHALLOW);
    if (dd.on_a) dd.nextCounter++; else dd.nextCounter = 0; /* (isPlayerInBind: ponytail) */
    if (dd.nextCounter > 30) dd_next_state();
}
static int dd_shot(V3 at) /* a star piece shot: receiveMsgPlayerAttack isMsgStarPieceAttack (its star sensor: the hit stop) */
{
    if (dd.row < 0 || dd.dead || dd.cur == DS_WAIT) return ddh_shot(at);
    if ((dd.sens >> DDS_STAR & 1) && vlen(vsub(at, dd_jp(2, v3(-150.0f, 0, 0)))) < 400.0f) { /* sStarPieceSensorRadius */
        dd_se(SE_OJ_STAR_PIECE_HIT_STOP); dd.asc = 2; dd.ascv = -1.0f; dd_brake(); /* startHitReaction, brake */
        return 1;
    }
    static const struct { int j; float r, x, y, z; } sn[7] = {{14, 210, 50, 30, 0}, {2, 250, 0, 0, 30}, {5, 110, 40, 0, 0}, {9, 110, 40, 0, 0}, {21, 50, 0, 0, 0}, {27, 50, 0, 0, 0}, {12, 120, 50, 0, 0}};
    for (int k = 0; k < 7; k++)
        if ((dd.sens >> k & 1) && vlen(vsub(at, dd_jp(sn[k].j, v3(sn[k].x, sn[k].y, sn[k].z)))) < sn[k].r) return 1;
    return ddh_shot(at);
}
static void dd_rebind(Anim *a, const Model *md) /* the zone's pack streamed again: its models moved, the clip kept */
{
    if (!md || !md->skin || a->skin == md->skin) return;
    int id = a->clip_id; float fr = a->frame, sp = a->speed; int loop = a->loop, done = a->done, had = a->skin != NULL;
    anim_bind(a, md->skin);
    a->clip_id = -1;
    if (had && id >= 0 && anim_play(a, sc->clips[id].name, sp, loop, 0)) { a->frame = fr; a->done = done; }
}
static void dd_models(void) /* scene_model again each frame (zone packs) */
{
    dd.md = scene_model(sc, "Dodoryu"); dd.lhmd = scene_model(sc, "DodoryuLeadHill"); dd.rbmd = scene_model(sc, "DodoryuRabbit");
    for (int k = 0; k < 3; k++) dd.hmd[k] = scene_model(sc, dd_hill_cls[k]);
    int fresh = !dd.anim->skin, rfresh = !dd.rb->skin;
    dd_rebind(dd.anim, dd.md); dd_rebind(dd.lh, dd.lhmd); dd_rebind(dd.rb, dd.rbmd);
    for (int k = 0; k < 3; k++) dd_rebind(dd.hp[k], dd.hmd[k]);
    if (fresh && dd.anim->skin) dd_act("UnderGroundWalk");
    if (rfresh && dd.rb->skin) dd_rb_reset(1);
}
static void update_dd(void)
{
    if (dd.row < 0 || !dx_cls_moves("Dodoryu")) return;
    const Row *r = &sc->objs[dd.row];
    dd_models();
    if (r->zone < MAX_ZONES && (!zone_kcl[r->zone] || zone_low[r->zone])) return; /* (its zone's ground in) */
    if (dd.rb_talk == -1 && (dd.rb_talk = talk_new("DandelionHillZone_DodoryuRabbit", dd.row, dd.row, 150.0f, 1500.0f)) < 0) dd.rb_talk = -2; /* createTalkCtrl (MessageId 0: its label cut at 31, msg.py), setDistanceToTalk 1500 */
    if (!dd.dead) {
        if (!dd.on_a && r->sw[SW_A] >= 0 && sw_on(r, SW_A)) { /* notifyOnSwitchA */
            dd.on_a = 1; ddh.appear = DD_PIECES;
            printf("dodoryu: SW_A on\n");
        }
        if (dd.anim->skin) anim_update(dd.anim);
        if (dd.lh_on && dd.lh->skin) { /* DodoryuLeadHill: its pose, its joints 1-17 turned about X by _90 (calcJoint), control */
            anim_update(dd.lh);
            float c = cosf(dd.lh90), sn = sinf(dd.lh90);
            for (u32 j = 1; j < dd.lh->skin->njoints && j <= 17; j++) {
                float *w = dd.lh->world[j];
                for (int i = 0; i < 3; i++) { float y = w[4 + i], z = w[8 + i]; w[4 + i] = y * c + z * sn; w[8 + i] = -y * sn + z * c; }
            }
            dd.lh90 += 0.07f;
        }
        dd_move();
        if (mario->hip == HIP_LAND && mario->hip_t == 0) dd_catch_hip(); /* checkHipDrop: isPlayerHipDropLand */
        int s0 = dd.cur;
        switch (dd.cur) {
        case DS_WAIT: dd_wait(); break;
        case DS_LV1: dd_lv1(); break;
        case DS_LV2: case DS_LV3: dd_lv2(); break;
        default: dd_demo(); break;
        }
        if (s0 == DS_LV1) dd_lv1_control();
        else if ((s0 == DS_LV2 || s0 == DS_LV3) && dd.lv[s0 == DS_LV3].staggering && !dm_damaging()) dd.lv[s0 == DS_LV3].staggering = 0;
        if (dd.rumble) { dd.rumble_n++; m_pad_vib(0); } /* updateRumblePad */
        if (dd.asc) { /* AnimScaleController HitReaction (sStaticAnimParam) */
            if (dd.ascv < -0.5f) { dd.ascy = 1.0f; dd.ascv = 0.06f; }
            dd.ascv = ((1.0f - dd.ascy) * 0.12f + dd.ascv) * 0.91f; dd.ascy += dd.ascv;
            dd.ascy = fminf(fmaxf(dd.ascy, 0.2f), 1.8f);
            if (fabsf(1.0f - dd.ascy) < 0.001f && fabsf(dd.ascv) < 0.001f) { dd.ascy = 1.0f; dd.ascv = 0.0f; dd.asc = 0; }
        }
        if (dd.cam && !dd.dead) { /* the anim camera: on the zone's placement (_148) or itself */
            float m[16];
            if (dd.cam_self) dd_base(m); else memcpy(m, dd.zm, 64);
            int n = canm_frames(dd.cam);
            if (n) demo_camera(dd.cam, dd.cam_f < n ? dd.cam_f : n - 1, m);
            dd.cam_f++;
        }
        if (dd.spin_cam) { /* CameraFixedThere on _14C: Hip x 0.7 + Mario x 0.3 */
            V3 t = vadd(vmul(dd_jp(2, v3(0, 0, 0)), 0.7f), vmul(mario->pos, 0.3f));
            camera_prog_start(t, dd.spin_eye, vmul(dd.grav, -1.0f), 120);
        }
        /* its sensors: attackSensor / receiveMsgPlayerAttack */
        if (!dd.dead && !demo_on) {
            static const struct { int j; float r, x, y, z; } sn[7] = {{14, 210, 50, 30, 0}, {2, 250, 0, 0, 30}, {5, 110, 40, 0, 0}, {9, 110, 40, 0, 0}, {21, 50, 0, 0, 0}, {27, 50, 0, 0, 0}, {12, 120, 50, 0, 0}};
            V3 mc = dd_mc();
            int took = 0;
            for (int k = 0; k < 7 && !took; k++) {
                if (!(dd.sens >> k & 1)) continue;
                V3 s = dd_jp(sn[k].j, v3(sn[k].x, sn[k].y, sn[k].z));
                int msg = mario_offence(s, sn[k].r, dd.grav);
                if (msg != MSG_NONE) { took = 1; if (dd_catch_player(msg, k, s) && msg == MSG_TRAMPLE) mario_trample(); }
            }
            for (int k = 0; k < 7; k++) {
                if (!(dd.sens >> k & 1)) continue;
                V3 s = dd_jp(sn[k].j, v3(sn[k].x, sn[k].y, sn[k].z));
                if (vlen(vsub(mc, s)) < sn[k].r + HIT_BODY_R) { dd_catch_attack(k, s, sn[k].r); break; }
            }
            if (dd.sens >> DDS_EYE & 1) ddh_eye(dd.pos);
        }
    }
    ddh_update();
    dd_rb_update();
}
static void dd_draw(void)
{
    if (dd.row < 0) return;
    dd_models();
    float m[16];
    if (!dd.dead && !dd.hidden && dd.md && dd.anim && dd.anim->skin) {
        dd_base(m);
        float sx = sqrtf(1.0f / dd.ascy), sc3[3] = {sx, dd.ascy, sx}; /* calcAndSetBaseMtx: _C x mScale */
        for (int j = 0; j < 12; j++) m[j] *= sc3[j / 4] * sc->objs[dd.row].scale[j / 4];
        char k[40];
        dd_key(k, sizeof k, "Dodoryu", dd.bva ? dd.bva : "Normal");
        bva_key = k; bva_frame = 0.0f; /* startBva */
        draw_skin(dd.md, m, dd.anim);
        if (!dd.nostep) { /* initShadowFromCSV (valid but between invalidateShadowAll / validateShadowAll: dd.nostep): Body Root sphere 300,
                           * ArmL ArmL1 oval (250, 100, 150), ArmR ArmR1 oval (200, 100, 150), dropped 1000 */
            static const struct { int j; float x, z; } sh[3] = {{1, 300.0f, 300.0f}, {19, 250.0f, 150.0f}, {25, 200.0f, 150.0f}};
            for (int i = 0; i < 3; i++) {
                if (sh[i].j >= (int)dd.anim->skin->njoints) continue;
                float w[16];
                mat_mul(w, m, dd.anim->world[sh[i].j]);
                V3 p = v3(w[12], w[13], w[14]);
                draw_shadow(p, up_at(p, v3(0, 1, 0)), v3(w[8], w[9], w[10]), sh[i].x, sh[i].z, 0.0f, 50.0f, 1000.0f);
            }
        }
    }
    if (!dd.dead && dd.lh_on && dd.lhmd && dd.lh && dd.lh->skin) { /* DodoryuLeadHill on its host's base */
        dd_base(m);
        draw_skin(dd.lhmd, m, dd.lh);
    }
    if (dd.rbmd && dd.rb && dd.rb->skin) draw_skin(dd.rbmd, dd.rb_m, dd.rb);
    for (int i = 0; i < DD_PIECES; i++) { /* the hill's pieces: their Lead / Fade at their frame */
        DdPiece *p = &ddp[i];
        if (!p->on || !p->shown || !dd.hmd[p->model]) continue;
        Anim *a = dd.hp[p->model];
        memcpy(m, p->m, 64);
        for (int j = 0; j < 12; j++) m[j] *= p->size;
        if (a && a->skin) {
            char k[40];
            dd_key(k, sizeof k, dd_hill_cls[p->model], p->nrv == P_CRUMBLE ? "Fade" : "Lead");
            anim_play(a, k, 0.0f, 0, 0);
            a->frame = p->fr; a->done = 0;
            anim_update(a);
            draw_skin(dd.hmd[p->model], m, a);
        } else render_model(dd.hmd[p->model], m, 0x80, 0x80, 0x80);
    }
}
static void dd_init(const Scene *s, const unsigned char *blob) /* Dodoryu::init, initAfterPlacement */
{
    memset(&dd, 0, sizeof dd); dd.row = -1; dd.rb_talk = -1; dd.mv_next = -1; dd.closed = -1;
    memset(ddp, 0, sizeof ddp); memset(&ddh, 0, sizeof ddh);
    int t = scene_type(s, "Dodoryu");
    for (u32 i = 0; t >= 0 && i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t) continue;
        dd.row = (int)i; dd.zone = r->zone;
        dd.md = scene_model(s, "Dodoryu"); dd.lhmd = scene_model(s, "DodoryuLeadHill"); dd.rbmd = scene_model(s, "DodoryuRabbit");
        for (int k = 0; k < 3; k++) dd.hmd[k] = scene_model(s, dd_hill_cls[k]);
        Anim **an[6] = {&dd.anim, &dd.lh, &dd.rb, &dd.hp[0], &dd.hp[1], &dd.hp[2]};
        for (int k = 0; k < 6; k++) { *an[k] = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(*an[k], 0, sizeof(Anim)); }
        u32 sz;
        const unsigned char *z = blob_chunk(blob, "ZONE", &sz);
        float one[3] = {1, 1, 1};
        mat_identity(dd.zm);
        if (z && r->zone < *(const u32 *)z) { const float *e = (const float *)(z + 4 + 56 * r->zone + 32); mat_translate_rot_scale(dd.zm, e, e + 3, one); } /* getZonePlacementMtx */
        if (r->path >= 0) dd.rail = find_rail_of(blob, r->zone, r->path, &dd.rn, &dd.rcl, 0);
        dd.rlen = w_rail_len(dd.rail, dd.rn);
        dd.pos = row_pos(r); dd.X = v3(1, 0, 0); dd.Y = v3(0, 1, 0); dd.Z = v3(0, 0, 1); dd.grav = v3(0, -1, 0); /* mBaseMtx.identity() */
        dd.w134 = v3(1, 0, 0); dd.ascy = 1.0f;
        dd.coord = w_rail_nearest(dd.rail, dd.rn, dd.pos); /* moveCoordToNearestPos */
        dd.r10 = dd.r14 = 8.0f; dd.e14 = dd.e18 = 60; dd.j10 = 3.0f; dd.j14 = 1.0f; /* MoveStateRail / Escape / JumpOut */
        dd.railSpeed = dd.railSpeedMax = 8.0f; /* DodoryuStateLv1: sSpeed */
        dd.lv[0].cp = &dd_lv2p; dd.lv[1].cp = &dd_lv3p; dd.lv[0].chaseMoreBrake = dd.lv[1].chaseMoreBrake = 1;
        dd.nst[DS_OPENING] = DO_UNDERGROUND; dd.nst[DS_APPEAR] = dd.nst[DS_ANGRY] = dd.nst[DS_DOWN] = DM_PLAY;
        dd.nst[DS_LV1] = L1_HIDE; dd.nst[DS_LV2] = dd.nst[DS_LV3] = L2_START;
        dd.st[DS_WAIT] = -1; /* (its first update: Wait from step 0) */
        dd.sens = DD_ALL;
        dd.bva = "Normal";
        ddh.appear = 5; /* createMogucchiHill: setAppearNum(5) */
        int prev = dd_rndi(0, 3), prev2 = -2, prev3 = -3; /* MogucchiHill::createPieces */
        for (int k = 0; k < DD_PIECES; k++) {
            int off = dd_rndi(0, 2), m = (prev + off + 1 + 3) % 3;
            if (prev == prev3 && prev2 == m) { off = (off + 2 + 1) % 2; m = (prev + off + 1 + 3) % 3; }
            prev3 = prev2; prev2 = prev; prev = m;
            ddp[k].model = m; ddp[k].size = 1.0f;
        }
        dd.rb_cc = 300;
        if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
        dd_snap_ground(); /* initAfterPlacement */
        printf("dodoryu: row %d, rail %u points (%.0f)\n", (int)i, dd.rn, dd.rlen);
        break;
    }
}
/* Test builds (XF=-DDD_TEST, CosmosGardenGalaxy/2): its SW_A on; Lv1: a hip drop 500 off while it is up, then a spin
 * on it; Lv2 / Lv3: a hip drop 500 off while it can take one, a spin on it while it is down; till its star */
int game_test_dd(int frame)
{
    static int last = -1000, dead_at = -1;
    if (dd.row < 0) return -1;
    const Row *r = &sc->objs[dd.row];
    if (frame % 30 == 0)
        printf("dd test f %d: state %d nerve %d step %d, move %d, %.0f from Mario, life %d, at %.0f %.0f %.0f, Mario %.0f %.0f %.0f, coord %.0f\n", frame, dd.cur, dd.nst[dd.cur], dd.st[dd.cur], dd.mv,
               vlen(vsub(mario->pos, dd.pos)), game_life(), dd.pos.x, dd.pos.y, dd.pos.z, mario->pos.x, mario->pos.y, mario->pos.z, dd.coord);
    if (life < 2) life = 3; /* (test: its hits are not dodged) */
    if (dd.dead) { /* to its star (arg7: the declarer) */
        int ty = scene_type(sc, "PowerStar");
        if (dead_at < 0) dead_at = frame;
        for (u32 i = 0; frame - dead_at >= 200 && frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++) /* (packs sliced before
                                                                      * Dodoryu was a declarer: its star's arg7 is -1) */
            if ((int)sc->objs[i].type == ty && (sc->objs[i].arg[7] == dd.row || sc->objs[i].arg[7] == -1)) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 100;
    }
    if (dd.cur == DS_WAIT) {
        float m[16];
        if (frame % 60 == 0 && vlen(vsub(mario->pos, dd.pos)) > 3000.0f && dd_name_pos("\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa\xe5\x86\x8d\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88" "1", m)) dd_player_mtx(m); /* (test: to its planet, マリオ再セット1) */
        if (frame > 600 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1); /* (test: the SwitchCube under the planet) */
        return 0;
    }
    if (demo_on || frame - last < 60) return dd.cur;
    V3 up = vmul(dd.grav, -1.0f), h = vtangent(vsub(mario->pos, dd.pos), up);
    h = vlen(h) > 1e-3f ? vnorm(h) : dd.X;
    int n = dd.nst[dd.cur], hip = 0, spin = 0;
    if (dd.cur == DS_LV1) { hip = n == L1_APPEAR || (n == L1_APPEARSTART && dd.st[dd.cur] > 16); spin = n == L1_DAMAGE && dd.st[dd.cur] > 20; }
    if (dd.cur == DS_LV2 || dd.cur == DS_LV3) { hip = dd_is(L2_CHASEAPPEAR) || (dd_is(L2_CHASEMORE) && dd_step() > 30); spin = dd_is(L2_ESCAPE) || dd_is(L2_RECOVER); }
    if (hip) { /* a hip drop 500 off */
        mario->pos = vadd(vadd(dd.pos, vmul(h, 500.0f)), vmul(up, 250.0f)); mario->up = up; mario->hvel = v3(0, 0, 0);
        mario->grounded = 0; mario->hip = HIP_FALL; mario->hip_t = 0; mario->vup = 0;
        last = frame;
        printf("dd test f %d: hip drop\n", frame);
    } else if (spin) { /* before it, a spin */
        mario->pos = vadd(dd.pos, vmul(h, 250.0f)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(h, -1.0f); mario->spin_timer = SPIN_TIME;
        last = frame;
        printf("dd test f %d: spin\n", frame);
    } else if (dd.cur != DS_LV1 && dd_is(L2_CHASEHIDE) && vlen(vsub(mario->pos, dd.pos)) >= 400.0f) { /* within its x14 */
        mario->pos = vadd(vadd(dd.pos, vmul(h, 400.0f)), vmul(up, 50.0f)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        last = frame;
        printf("dd test f %d: near it\n", frame);
    } else if (vlen(vsub(mario->pos, dd.pos)) < 900.0f && dd.cur == DS_LV1) { /* out of its way */
        mario->pos = vadd(dd.pos, vmul(h, 1100.0f)); mario->hvel = v3(0, 0, 0);
    }
    return dd.cur;
}
