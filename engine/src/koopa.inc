/* Koopa.cpp (Bowser) with KoopaFunction.cpp, KoopaParts.cpp, KoopaSensorCtrl.cpp, KoopaSequencer.cpp,
 * KoopaSequencerVs1.cpp, KoopaBattleStairsBase / Vs1.cpp, KoopaBattleBase / Main.cpp, KoopaSubSequenceBattle.cpp, the
 * states KoopaStateGuard / AttackHipDrop / AttackShockWave / AttackFireShort / DamageEscape / JumpAway, the demos
 * KoopaDemoJumpToPlanet / PowerUp, KoopaFireShort / FireStairs / ShockWave / PlanetShadow, KoopaSwitchKeeper,
 * KoopaPowerUpSwitch, Map/KoopaBattleMapPlanet / DamagePlate / CoinPlate / Plate.cpp after Petari. #included by
 * game.c (kp_*). KoopaBattleVs1Galaxy: dead until its SW_APPEAR, the stairs battle (階段の戦い開始デモ: the
 * DemoBattleStairsStart anim camera, its two demo stairs fired at, then each KoopaBattleMapStair's fire request at its
 * mFireAttackStep: a KoopaFireStairs meteor timed to its break, the jumps to the next KoopaPos), at its SW_A the
 * クッパＶｓ１本戦開始 sheet, the jump to the planet, the battle in three levels (KoopaBattleMain's nerves and states,
 * the plates on KoopaBattleMapPlanet: a hip drop onto a damage plate burns it, the escape, a spin on its tail, a spin
 * on its body: down), the power up demos, クッパＶｓ１ダウン and the grand star at デモ中心.
 * ponytail: no ActorJointCtrl (face control), no bloom models, no star pointer (2P),
 * no center screen blur, no BRK / BTK of the plates and planet (Death, Revival, Break, PowerStarAppear: their effects
 * only), the binded collision's sensor (sendMsgToBindedSensor) is the plate nearest Koopa's feet within its model's
 * radius, makeMtxUpNoSupportPos's front any one, anim cameras start without their interpolation, the attacks on other
 * actors (tryKoopaAttackMapObj, the fires' pushes), one Anim shared by each pooled skinned model (posed per draw)
 * Vs3 (KoopaBattleVs3Galaxy): KoopaSequencerVs3, KoopaBattleStairsVs3, KoopaBattleVs3Lv1 (ChaseRoll: a spin on the
 * rolling rock's face, DamageReverse) / Lv2 (AttackRoll: a PunchingKinoko's blow on the roll ball, game.c kp_blow),
 * KoopaDemoFallToPlanetVs3Lv2 / Lv3, Lv3 / Final in KoopaBattleMain (AttackRoll) on the Lv3 planet's damage plates,
 * KoopaFigureBall, KoopaRockBreak, the three planets and the holed suns (insts), the demo casts Peach / KoopaJr /
 * KoopaJrShip (ModelObjNpc at デモ中心 with their clips), the keepers read by switch (KoopaViewSwitchKeeper,
 * KoopaSwitchKeeper), KoopaRestarterVs3 (tryRestartKoopa on its appear switch). ponytail: the demo meteors
 * (MeteorStrike's DemoKoopaBattleStairsVs3Start0n), the parts' FixPos (Koopa's base), Mario's
 * demo clips (startBckPlayer), the fires' useFront, the casts' end (the sheets' cast groups) */
#define KP_FIRE_SHORT 32   /* sFireShortNum */
#define KP_FIRE_STAIRS 16  /* sFireStairsNum */
#define KP_SHOCK 8         /* sShockWaveNum */
#define KP_PLATES 14       /* 6 damage, 4 coin, 4 plain (KoopaBattleMapPlanet::createPlate) */
enum { KJ_HIP1 = 3, KJ_LEGL2 = 7, KJ_FOOTL2 = 9, KJ_LEGR2 = 11, KJ_FOOTR2 = 13, KJ_SPINE = 14, KJ_FACE = 15, KJ_TONGUE2 = 46,
       KJ_ARML2 = 50, KJ_WRISTL = 51, KJ_ARMR2 = 59, KJ_WRISTR = 60, KJ_TAIL2 = 68, KJ_TAIL3 = 69 }; /* Koopa.bdl JNT1 */
enum { KPT_PUSH, KPT_ENEMY, KPT_ENEMY_ATTACK, KPT_ATTACK, KPT_RECEIVER }; /* ATYPE_KOOPA_PUSH, ENEMY, ENEMY_ATTACK, KOOPA_ATTACK, KOOPA_RECEIVER */
static const struct { const char *name; s8 joint, type, valid; float r, o[3]; } kp_sdef[] = { /* KoopaSensorCtrl::initSensor */
    {"Body", KJ_SPINE, KPT_PUSH, 1, 150, {-70, -30, 0}}, {"Face", KJ_FACE, KPT_PUSH, 1, 110, {80, 0, 0}},
    {"WristL", KJ_WRISTL, KPT_PUSH, 1, 60, {30, 0, 0}}, {"WristR", KJ_WRISTR, KPT_PUSH, 1, 60, {30, 0, 0}},
    {"ArmL", KJ_ARML2, KPT_PUSH, 1, 60, {0, 0, 0}}, {"ArmR", KJ_ARMR2, KPT_PUSH, 1, 60, {0, 0, 0}},
    {"LegL", KJ_LEGL2, KPT_PUSH, 1, 80, {30, 0, 0}}, {"LegR", KJ_LEGR2, KPT_PUSH, 1, 80, {30, 0, 0}},
    {"FootL", KJ_FOOTL2, KPT_PUSH, 1, 70, {-50, 0, 0}}, {"FootR", KJ_FOOTR2, KPT_PUSH, 1, 70, {-50, 0, 0}},
    {"Tail", KJ_TAIL2, KPT_PUSH, 1, 60, {50, 0, 0}}, {"TailTop", KJ_TAIL3, KPT_PUSH, 1, 50, {50, 0, 0}},
    {"GuardBody", KJ_SPINE, KPT_ENEMY, 0, 130, {0, -150, 0}},
    {"AttackShell", KJ_SPINE, KPT_ENEMY_ATTACK, 1, 180, {-50, 100, 0}}, {"AttackFootL", KJ_FOOTL2, KPT_ENEMY_ATTACK, 1, 50, {-50, 40, 0}},
    {"AttackFootR", KJ_FOOTR2, KPT_ENEMY_ATTACK, 1, 50, {-50, 40, 0}},
    {"AttackHipDrop", -1, KPT_ATTACK, 0, 200, {0, 250, 0}},
    {"ChaseRollAttack", KJ_SPINE, KPT_ENEMY_ATTACK, 0, 260, {0, 0, 0}}, {"ChaseRollFace", KJ_FACE, KPT_RECEIVER, 0, 200, {50, 0, 0}},
    {"ChaseRollStarPiece", KJ_SPINE, KPT_PUSH, 0, 280, {0, 0, 0}},
    {"AttackRoll", KJ_SPINE, KPT_ENEMY_ATTACK, 0, 180, {0, 0, 0}}, {"AttackRollReceiver", KJ_SPINE, KPT_RECEIVER, 0, 200, {0, 0, 0}},
    {"AttackRollStarPiece", KJ_SPINE, KPT_PUSH, 0, 200, {0, 0, 0}},
    {"ReceiverTail", KJ_TAIL2, KPT_RECEIVER, 0, 180, {50, 30, 0}}, {"ReceiverTailTop", KJ_TAIL3, KPT_RECEIVER, 0, 150, {100, 30, 0}},
    {"DamageEscapeBody", KJ_SPINE, KPT_RECEIVER, 0, 250, {0, 50, 0}}, {"DamageEscapePush", KJ_SPINE, KPT_ATTACK, 0, 180, {-70, -30, 0}},
    {"AttackSpin", -1, KPT_ATTACK, 0, 250, {0, 250, 0}}}; /* KoopaStateAttackSpin::init */
enum { KS_BODY, KS_FACE, KS_WRISTL, KS_WRISTR, KS_ARML, KS_ARMR, KS_LEGL, KS_LEGR, KS_FOOTL, KS_FOOTR, KS_TAIL, KS_TAILTOP,
       KS_GUARDBODY, KS_ATTACKSHELL, KS_ATTACKFOOTL, KS_ATTACKFOOTR, KS_ATTACKHIPDROP, KS_CHASEROLLATTACK, KS_CHASEROLLFACE,
       KS_CHASEROLLSTARPIECE, KS_ATTACKROLL, KS_ATTACKROLLRECEIVER, KS_ATTACKROLLSTARPIECE, KS_RECEIVERTAIL, KS_RECEIVERTAILTOP,
       KS_DAMAGEESCAPEBODY, KS_DAMAGEESCAPEPUSH, KS_ATTACKSPIN, KS_N };
/* a nerve: setNerve keeps it for the next update (Spine::changeNerve), where its step starts at 0 */
typedef struct { s8 nrv, next; s16 st; } Nv;
static void nv_set(Nv *n, int v) { n->next = (s8)v; }
static int nv_run(Nv *n) { if (n->next >= 0) { n->nrv = n->next; n->next = -1; n->st = 0; } else n->st++; return n->st; }
static int nv_is(const Nv *n, int v) { return n->next < 0 ? n->nrv == v : 0; } /* isNerve (a pending change is not yet the nerve) */
/* the sequencer Vs1, the stairs battle, KoopaSubSequenceBattle, KoopaBattleMain */
enum { KQ_BATTLESTAIRS, KQ_DEMOBATTLESTART, KQ_DEMOJUMPTOPLANET, KQ_BATTLE, KQ_WAITDEMOBATTLEEND, KQ_DEMOBATTLEEND, KQ_WAITDEMO };
enum { KSB_WAITDEMO, KSB_DEMO, KSB_WAIT, KSB_ATTACKFIRE, KSB_JUMPSTART, KSB_JUMPLOOP, KSB_JUMPEND };
enum { KSS_LV1, KSS_DEMOLV2, KSS_LV2, KSS_DEMOLV3, KSS_LV3 };
enum { KBM_CHASE, KBM_WANDER, KBM_SEARCH, KBM_FIND, KBM_JUMPAWAY, KBM_GUARD, KBM_FIRESHORT, KBM_FIRELONG, KBM_SHOCKWAVE, KBM_HIPDROP,
       KBM_SPIN, KBM_ROLL, KBM_DAMAGEESCAPE };
/* the states (ActorStateBase: its own nerve, mIsDead) */
enum { KST_GUARD, KST_HIPDROP, KST_SHOCKWAVE, KST_FIRESHORT, KST_DAMAGEESCAPE, KST_JUMPAWAY, KST_JUMPTOPLANET, KST_POWERUP, KST_SPIN, KST_FIRELONG, KST_CHASEROLL, KST_ROLL, KST_N };
enum { KG_FACE, KG_BODY, KG_TAIL };
enum { KH_RUN, KH_JUMPSTART, KH_JUMPUP, KH_JUMPWAITAIR, KH_JUMPATTACKDOWN, KH_LAND, KH_DAMAGE };
enum { KW_JUMPSTART, KW_JUMP, KW_JUMPFAST, KW_LAND };
enum { KF_START, KF_EMIT };
enum { KD_ESCAPESTART, KD_ESCAPESTARTLOOP, KD_ESCAPESTARTLAND, KD_ESCAPERUN, KD_ESCAPEFIND, KD_ESCAPEEND, KD_TAILRUNSTART,
       KD_TAILRUNSTARTFINAL, KD_TAILRUN, KD_TAILRUNFINAL, KD_TAILRUNEND, KD_TAILRUNENDFINAL, KD_DOWN, KD_DOWNLAND, KD_DOWNEND };
enum { KJA_JUMPSTART, KJA_JUMP, KJA_LAND };
enum { KJP_START, KJP_FALL, KJP_LAND, KJP_WAITPLAYER };
enum { KP_WAITDEMO, KP_DEMO };
enum { KSP_WAITTOSTART, KSP_RUN, KSP_START, KSP_SPIN, KSP_END, KSP_EVEN };
enum { KFL_START, KFL_EMIT };
enum { KR_WAITTOSTART, KR_START, KR_ROLLAIR, KR_ROLLGROUND, KR_ENDAIR, KR_ENDLAND }; /* KoopaStateChaseRoll / AttackRoll */
enum { K3_WAITPLAYER, K3_BATTLESTAIRS, K3_DEMOBATTLESTART, K3_DEMOJUMPTOPLANET, K3_LV1, K3_FALLLV2, K3_LV2, K3_FALLLV3, K3_LV3, K3_POWERUPFINAL,
       K3_FINAL, K3_WAITDEMODOWN, K3_DEMODOWN, K3_DEAD, K3_WAITDEMO }; /* KoopaSequencerVs3 */
typedef struct { float a, g, f, t; } KpMove;   /* MR::ActorMoveParam: accel, gravity, friction, turn degrees */
typedef struct { float d, h, v; } KpSight;     /* MR::ActorSightParam */
static struct {
    int row, vs, lv, angry, alive, hidden, calc_grav, init, keeper, powsw;
    Kuribo b;                    /* mPosition / mVelocity / mFront (b.front) / mGravity / its binder */
    Anim *anim; char act[40];
    Nv seq, stairs, sub, main;
    Nv st[KST_N]; u8 sdead[KST_N];
    int c2c, wander_time;        /* KoopaBattleMain::_2C, KoopaBattleBase::mWanderTime */
    /* KoopaBattleStairsVs1 */
    int far, nearr, req, can_jump, n4c, n50, n54, avail, jump_idx; V3 jp[3], jold, jnew, jdir;
    /* the states' members */
    int guard_n; V3 hip_ground; int hip_max, hip_att, hip_delay, hip_time;
    int sw_max, sw_att, sw_delay; float sw_jump, sw_grav;
    int fs_n, fs_max, fs_delay, fl_n;
    int sp_max, sp_att; const KpMove *sp_move;
    int de_time, de_max, de_cw, de_last, de_frames; const KpMove *de_run, *de_tail; float de_rot, de_away;
    /* parts: the tail thorns, the armor / thorn breaks (KoopaParts::createCommonParts) */
    int thorn, armor_break, thorn_break; Anim *a_tb;
    int shadow;                  /* KoopaPlanetShadow appeared */
    float sens[KS_N][4]; u8 sv[KS_N]; /* the sensors' places (and radius), valid */
    int spun;                    /* a spin of Mario's taken by a sensor (one message a spin) */
    const char *cam; int acam, acam_f; const char *acam_name; /* the actor camera on, the anim camera and its frame */
    const char *pd; int dx_k, dx_f;  /* its programmable demo; the sheet demo asked (-1 none) and its frame */
    int mbattle;                 /* startBckPlayer BattleWait in its demo */
    V3 mground;                  /* getPlayerGroundPos */
    u32 slv[3];
} kp = {.row = -1};
/* KoopaBattleMapPlanet and its plates: insts made at each rebind (kp_insts) */
enum { KPL_DAMAGE, KPL_COIN, KPL_PLATE };
static struct { int ki, kind, alive, nrv, st; V3 pos; float m[16]; } kpl[KP_PLATES];
static int kp_planet_ki = -1, kp_planet_ok, kp_planet_star = -1, kp_star_demo = -1; static float kp_planet_m[16]; static V3 kp_pc;
/* the pools */
static struct { int on, nrv, st, stair, dur; V3 pos, vel, front, a0; float rotx, mf; } kpfst[KP_FIRE_STAIRS]; /* KoopaFireStairs */
static struct { int on, nrv, st, dur, valid; V3 pos, vel, front, grav; float speed, off, f; u32 lv; } kpfs[KP_FIRE_SHORT]; /* KoopaFireShort */
static struct { int on, st; float ang; V3 up, front; float m94[16], mb[16], mm[16]; V3 sp; } ksw[KP_SHOCK];     /* KoopaShockWave */
static Anim *ka_fire, *ka_mbreak, *ka_sw, *ka_sws;
static const Model *m_kp, *m_kthorn, *m_karmorb, *m_kthornb, *m_kfire2, *m_ksw, *m_ksws, *m_kmeteor, *m_kmeteorb, *m_kpshadow;
static const KpMove kp_chase = {1.5f, 1.0f, 0.9f, 3.0f}, kp_wander = {0.5f, 1.0f, 0.9f, 1.3f}, kp_findp = {0.0f, 1.0f, 0.98f, 3.0f};
static const KpSight kp_chase_start = {1800.0f, 120.0f, 50.0f}, kp_chase_keep = {2200.0f, 125.0f, 50.0f}, kp_hip_sight = {1000.0f, 90.0f, 30.0f};

static const char *kp_cls(void) { return sc->types + 32 * sc->objs[kp.row].type; }
static V3 kp_up(void) { return vmul(kp.b.grav, -1.0f); }
static void kp_se(u32 id) { se_start(id, &kp.b.pos.x); }
static void kp_lv(int k, u32 id) { kp.slv[k] = se_level(id, &kp.b.pos.x, kp.slv[k]); }
static int kp_ballm; static float kp_bm[16]; /* Vs3: tryCalcAndSetBaseMtx (a roll state's figure ball's matrix) */
static void k3_insts(const Scene *s, int host);
static void kp_base(float m[16]) { if (kp_ballm) memcpy(m, kp_bm, 64); else basis_matrix(m, kp.b.pos, kp_up(), kp.b.front, 0); } /* calcAndSetBaseMtxFromGravityAndZAxis */
static int kp_near(float d) { return vlen(vsub(mario->pos, kp.b.pos)) < d; } /* isNearPlayer */
static void kp_act(const char *name) /* startAction */
{
    char n[48];
    snprintf(n, sizeof n, "%s/%s", kp_cls(), name);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    n[31] = 0; /* slice.py name32 */
    snprintf(kp.act, sizeof kp.act, "%s", name);
    if (kp.anim && kp.anim->skin) { kp.anim->clip_id = -1; anim_start(kp.anim, n, 0); kp.anim->speed = 1.0f; }
}
static int kp_act_end(void) { return !kp.anim || !kp.anim->skin || kp.anim->done; } /* isActionEnd / isBckStopped */
static void kp_rate(float r) { if (kp.anim) kp.anim->speed = r; } /* setBckRate */
static void kp_joint_m(int j, float o[16]) /* getJointMtx (j -1: its base) */
{
    float b[16];
    kp_base(b);
    if (j < 0 || !kp.anim || !kp.anim->skin || (u32)j >= kp.anim->skin->njoints) { memcpy(o, b, 64); return; }
    mat_mul(o, b, kp.anim->world[j]);
}
static V3 kp_joint(int j) { float m[16]; kp_joint_m(j, m); return v3(m[12], m[13], m[14]); } /* copyJointPos */
static int kp_shadow_demo; /* Shadow.bcsv group デモ用 valid, 戦闘用 not (KoopaSequencerVs1::exeDemoBattleStart) */
static int kp_gpos(const char *name, float m[16]) /* MR::findNamePos (its matrix) */
{
    const float *g = scene_gpos(name);
    if (!g) { printf("koopa: no GeneralPos %s\n", name); return 0; }
    float one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, g, g + 3, one);
    return 1;
}
static void kp_set_pos(const char *name) /* KoopaFunction::setKoopaPos: its gravity the -Y, front the Z, resetPosition */
{
    float m[16];
    if (!kp_gpos(name, m)) return;
    kp.b.grav = vnorm(v3(-m[4], -m[5], -m[6])); kp.b.front = vnorm(v3(m[8], m[9], m[10])); kp.b.pos = v3(m[12], m[13], m[14]);
    kp.b.vel = v3(0, 0, 0);
}
static void kp_player_at(const char *name) /* MR::setPlayerPosAndWait */
{
    float m[16];
    if (!kp_gpos(name, m)) return;
    m_set_trans(v3(m[12], m[13], m[14])); mario->up = vnorm(v3(m[4], m[5], m[6])); mario->face = vnorm(v3(m[8], m[9], m[10]));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
}
static int kp_sight(const KpSight *s) /* isKoopaSightPlayer: isInSightFanPlayer(front) */
{
    V3 to = vsub(mario->pos, kp.b.pos);
    float d = vlen(to);
    if (d >= s->d || d < 1e-6f) return 0;
    V3 h = vtangent(to, kp.b.grav);
    if (vlen(h) > 1e-6f && vdot(vnorm(h), kp.b.front) < cosf(s->h * 0.5f * 0.0174533f)) return 0;
    return fabsf(vdot(vnorm(to), kp.b.grav)) <= sinf(s->v * 0.5f * 0.0174533f);
}
static void kp_move(const KpMove *p, V3 dir) { w_move_turn(&kp.b, dir, p->a, p->g, p->f, p->t); } /* moveAndTurnToDirection */
static void kp_to_player(const KpMove *p) { kp_move(p, vsub(mario->pos, kp.b.pos)); }  /* moveAndTurnToPlayer */
static void kp_escape(const KpMove *p) { kp_move(p, vsub(kp.b.pos, mario->pos)); }     /* escapeKoopaFromPlayer (its binded attack: none) */
static void kp_turn_player(float deg) { turn_on_plane(&kp.b.front, vsub(mario->pos, kp.b.pos), kp.b.grav, cosf(deg * 0.0174533f)); }
static void kp_jump_set(float v) { kp.b.vel = vmul(kp.b.grav, -v); } /* setVelocityJump */
static void kp_jump_add(float v) { kp.b.vel = vadd(kp.b.vel, vmul(kp.b.grav, -v)); } /* addVelocityJump */
static void kp_grav_add(float v) { kp.b.vel = vadd(kp.b.vel, vmul(kp.b.grav, v)); } /* addVelocityToGravity */
static void kp_rumble(int k, float n, float m, float f) /* tryRumblePadAndCameraDistance Middle (0) / Strong (1) / VeryStrong (2) */
{
    static const u8 pad[3][3] = {{RUMBLE_MIDDLE, RUMBLE_WEAK, RUMBLE_VERY_WEAK}, {RUMBLE_STRONG, RUMBLE_MIDDLE, RUMBLE_WEAK}, {RUMBLE_VERY_STRONG, RUMBLE_STRONG, RUMBLE_MIDDLE}};
    static const u8 sh[3][3] = {{SHAKE_NORMAL_WEAK, SHAKE_WEAK, SHAKE_VERY_WEAK}, {SHAKE_NORMAL, SHAKE_NORMAL_WEAK, SHAKE_WEAK}, {SHAKE_STRONG, SHAKE_NORMAL, SHAKE_NORMAL_WEAK}};
    float d = vlen(vsub(mario->pos, kp.b.pos));
    int i = d < n ? 0 : d < m ? 1 : d < f ? 2 : -1;
    if (i >= 0) { pad_rumble(pad[k][i]); camera_shake(sh[k][i]); }
}
static void kp_fx(const char *name) { float m[16]; kp_base(m); fx_named(kp_cls(), name, m, NULL, NULL); } /* emitEffect */
static void kp_sensor(int s, int on) { kp.sv[s] = (u8)on; } /* validateHitSensor / invalidateHitSensor */
/* the actor cameras (initKoopaCamera: its ActorCameraInfo, the class's name in the camera table) */
static const char *kp_cam_cls(void) { return kp.vs == 1 ? "クッパ大王（ＶＳ１）" : kp.vs == 2 ? "クッパ大王（ＶＳ２）" : "クッパ大王"; }
static void kp_cam_start(const char *name) /* startKoopaCamera / startKoopaTargetCamera */
{
    if (row_camset[kp.row] == -1) return;
    float m[16];
    kp_base(m);
    if (camera_actor_start(kp_cam_cls(), name, (unsigned short)row_camset[kp.row], v3(m[4], m[5], m[6]), v3(m[8], m[9], m[10]), kp.b.pos))
        kp.cam = name;
}
static void kp_cam_end(const char *name) { if (kp.cam && !strcmp(kp.cam, name)) { camera_event_end(); kp.cam = NULL; } } /* endKoopaCamera */
static void kp_acam(const char *clip) /* startKoopaAnimCamera (NULL: endKoopaAnimCamera) */
{
    kp.acam_name = clip; kp.acam = clip != NULL; kp.acam_f = 0;
    if (clip) { char n[48]; snprintf(n, sizeof n, "koopa/%s", clip); n[31] = 0; for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c); printf("koopa: anim camera %s, %d frames\n", n, canm_frames(n)); }
    if (!clip) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45);
}
static void kp_acam_update(void)
{
    if (!kp.acam) return;
    char n[48];
    float m[16];
    snprintf(n, sizeof n, "koopa/%s", kp.acam_name); n[31] = 0; /* slice.py name32 */
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    kp_base(m);
    int cf = canm_frames(n);
    if (cf) demo_camera(n, kp.acam_f < cf ? kp.acam_f : cf - 1, m);
    kp.acam_f++;
}
static int kp_try_demo(const char *name, int mario) /* tryStartDemo / tryStartDemoMarioPuppetable */
{
    if (!pd_try_start(name, kp.row)) return 0;
    kp.pd = name; kp.mbattle = mario;
    return 1;
}
static void kp_end_demo(const char *name) { pd_end(name); kp.pd = NULL; kp.mbattle = 0; } /* endDemo */
static int kp_demo(void) { return kp.row >= 0 && kp.pd && pd.name && kp.mbattle; } /* its puppet demos: Mario BattleWait */
static int kp_dx_on(void) { return kp.dx_k >= 0 && dx.on && dx.cur == kp.dx_k; }
static int kp_dx_last(void) /* MR::isDemoLastStep */
{
    if (!kp_dx_on()) return 0;
    const Demo *d = &demos[dx.cur];
    return dx.part == d->h->nparts - 1 && dx.step >= d->parts[dx.part].total - 1;
}
static void kp_dx_request(const char *name) /* requestStartTimeKeepDemoMarioPuppetable */
{
    kp.dx_k = -1;
    for (int k = 0; k < ndemos; k++) if (!strcmp(demos[k].h->name, name)) kp.dx_k = k;
    if (kp.dx_k < 0 || !dx_request(name, 0)) printf("koopa: no demo %s\n", name);
    kp.dx_f = -1;
}
static const Row *kp_row_of(const char *cls) /* the zone's KoopaSwitchKeeper / KoopaPowerUpSwitch (initAfterPlacement: register) */
{
    int t = scene_type(sc, cls);
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) if ((int)sc->objs[i].type == t) return &sc->objs[i];
    return NULL;
}
static void kp_sw_of(const char *cls, int sw) { const Row *r = kp_row_of(cls); if (r && r->sw[sw] >= 0) sw_set(r, sw, 1); } /* onSwitchA / B */

/* --- KoopaParts: thorns, armor ----------------------------------------------------------------------------------- */
static void kp_recover_armor(void) { kp.armor_break = 0; kp.thorn_break = 0; } /* startRecoverKoopaArmor */
static void kp_break_thorn(void) { kp.thorn = 0; }                             /* startBreakKoopaTailThorn */
static void kp_recover_thorn(void) { kp.thorn = 1; }                           /* startRecoverKoopaTailThorn */
static void kp_break_armor(void) /* startBreakKoopaArmor: the armor / thorn breaks appear, Break */
{
    kp.armor_break = 1; kp.thorn_break = 1;
    if (kp.a_tb && m_kthornb && m_kthornb->skin) { anim_bind(kp.a_tb, m_kthornb->skin); kp.a_tb->clip_id = -1; anim_start(kp.a_tb, "koopathornbreak/break", 0); }
}

/* --- the plates and the planet ------------------------------------------------------------------------------------ */
static void kp_insts(const Scene *s) /* game_rebind_: KoopaBattleMapPlanet (惑星中心) and its plates, their state kept */
{
    kp_planet_ki = -1;
    for (int i = 0; i < KP_PLATES; i++) kpl[i].ki = -1;
    if (kp.row < 0 || !kp_planet_ok) return;
    static const char *const pm[3] = {"KoopaPlateDamage", "KoopaPlateCoin", "KoopaPlate"};
    int host = kp_planet_star >= 0 ? kp_planet_star : kp.row;
    for (int i = -1; i < KP_PLATES && ninsts < (int)s->nobjs; i++) {
        if (i < 0 && kp.vs == 3) { k3_insts(s, host); continue; }
        if (i >= 0 && kp.vs == 3 && kpl[i].kind != KPL_DAMAGE) continue;
        const char *name = i < 0 ? (kp.vs == 2 ? "KoopaBattleMapPlanetVs2" : "KoopaBattleMapPlanet") : pm[kpl[i].kind];
        const Model *md = scene_model(s, name);
        if (!md) continue;
        Inst *in = &insts[ninsts];
        memset(in, 0, sizeof *in);
        in->md = md; in->row = host; in->zone = s->objs[kp.row].zone; in->kmove = in->kparent = -1;
        memcpy(in->m, i < 0 ? kp_planet_m : kpl[i].m, 64);
        in->s = 1.0f; in->reach = md->radius;
        int on = i < 0 || (kpl[i].alive && !(kpl[i].kind == KPL_DAMAGE && kpl[i].nrv != 0));
        in->kcl = on ? scene_kcl(s, name) : NULL; in->invisible = i >= 0 && !kpl[i].alive;
        zone_grow(in);
        if (i < 0) kp_planet_ki = ninsts; else kpl[i].ki = ninsts;
        ninsts++;
    }
}
static void kp_plate_coll(int i, int on) /* validate / invalidateCollisionParts, kill */
{
    if (kpl[i].ki < 0 || kpl[i].ki >= ninsts) return;
    static const char *const pm[3] = {"KoopaPlateDamage", "KoopaPlateCoin", "KoopaPlate"};
    insts[kpl[i].ki].kcl = on ? scene_kcl(sc, pm[kpl[i].kind]) : NULL;
    insts[kpl[i].ki].invisible = !kpl[i].alive;
}
static void kp_plate_fx(int i, const char *name, const void *key) { fx_named(i < 6 ? "KoopaPlateDamage" : i < 10 ? "KoopaPlateCoin" : "KoopaPlate", name, kpl[i].m, NULL, key); } /* emitEffect (key: kept) */
static void kp_planet_init(void) /* KoopaParts::initVs1 / Vs2: createKoopaBattleMapPlanet(惑星中心), initVs3: the Lv3 one
                                   * (惑星Ｌｖ３) holds the plates; createPlate */
{
    float one[3] = {1, 1, 1};
    const float *g = scene_gpos(kp.vs == 3 ? "惑星Ｌｖ３" : "惑星中心");
    kp_planet_ok = g != NULL;
    if (!g) return;
    mat_translate_rot_scale(kp_planet_m, g, g + 3, one);
    kp_pc = v3(g[0], g[1], g[2]);
    static const float dmg[6][2] = {{0, 0}, {180, 0}, {90, 0}, {270, 0}, {0, 90}, {0, 270}};
    static const float coin[4][2] = {{55, 45}, {125, 135}, {55, 225}, {125, 315}}, pl[4][2] = {{125, 45}, {55, 135}, {125, 225}, {55, 315}};
    for (int i = 0; i < KP_PLATES; i++) {
        int k = i < 6 ? KPL_DAMAGE : i < 10 ? KPL_COIN : KPL_PLATE;
        const float *a = k == KPL_DAMAGE ? dmg[i] : k == KPL_COIN ? coin[i - 6] : pl[i - 10];
        V3 v = v3(0, k == KPL_DAMAGE ? 1180.0f : k == KPL_COIN ? 1200.0f : 1230.0f, 0);
        v = rot_axis(v, v3(0, 0, 1), a[0] * 0.0174533f);
        v = rot_axis(v, k == KPL_DAMAGE ? v3(1, 0, 0) : v3(0, 1, 0), a[1] * 0.0174533f);
        V3 w = vadd(v3(kp_planet_m[0] * v.x + kp_planet_m[4] * v.y + kp_planet_m[8] * v.z, kp_planet_m[1] * v.x + kp_planet_m[5] * v.y + kp_planet_m[9] * v.z,
                       kp_planet_m[2] * v.x + kp_planet_m[6] * v.y + kp_planet_m[10] * v.z), kp_pc);
        V3 up = up_at(w, vnorm(vsub(w, kp_pc))), f = vtangent(v3(0, 0, 1), up);
        if (vlen(f) < 1e-3f) f = vtangent(v3(1, 0, 0), up);
        kpl[i].kind = k; kpl[i].alive = kp.vs != 3 || k == KPL_DAMAGE; /* (Vs3 Lv3, _DD: createPlate's damage plates only) */ kpl[i].nrv = 0; kpl[i].st = 0; kpl[i].pos = w;
        basis_matrix(kpl[i].m, w, up, vnorm(f), 0); /* makeMtxUpNoSupportPos(-mGravity, mPosition) */
    }
    kp_planet_star = -1;
    int t = scene_type(sc, "KoopaVS1PartsBattleStage"); /* (the insts' row: one that is never killed) */
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) if ((int)sc->objs[i].type == t) kp_planet_star = (int)i;
}
static int kp_bind_plate(void) /* the binded collision's plate: the one under its feet */
{
    if (!kp.b.on_ground) return -1;
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < KP_PLATES; i++) {
        if (!kpl[i].alive || (kpl[i].kind == KPL_DAMAGE && kpl[i].nrv != 0)) continue; /* (Break / Recover: its collision off) */
        float d = vlen(vsub(kp.b.pos, kpl[i].pos)), r = kpl[i].ki >= 0 && kpl[i].ki < ninsts ? insts[kpl[i].ki].reach : 300.0f;
        if (d < r && d < bd) { bd = d; best = i; }
    }
    return best;
}
static int kp_plate_hipdrop(int i) /* receiveOtherMsg ACTMES_KOOPA_HIP_DROP_ATTACK */
{
    if (i < 0) return 0;
    if (kpl[i].kind == KPL_DAMAGE) {
        if (kpl[i].nrv != 0) return 0;
        kpl[i].nrv = 1; kpl[i].st = -1; kp_plate_fx(i, "Hit", NULL);
        kp_plate_coll(i, 0);
        return 1;
    }
    kp_plate_fx(i, "Hit", NULL);
    V3 up = vnorm(vsub(kpl[i].pos, kp_pc));
    if (kpl[i].kind == KPL_COIN) {
        se_start(SE_OJ_KOOPA_PLATE_COIN_BRK, &kpl[i].pos.x);
        V3 r = v3(((float)rand() / RAND_MAX * 2 - 1) * 30.0f, ((float)rand() / RAND_MAX * 2 - 1) * 30.0f, ((float)rand() / RAND_MAX * 2 - 1) * 30.0f);
        coin_to_velocity(kpl[i].pos, vadd(vtangent(r, up), vmul(up, 30.0f))); /* calcRandomVec(-30, 30), killed along gravity, hop 30 */
    } else {
        se_start(SE_OJ_KOOPA_PLATE_PLN_BRK, &kpl[i].pos.x);
        launch_pieces(kpl[i].pos, up, 5, 30.0f, 50.0f); /* appearStarPiece(5, 30, 50) */
        se_start(SE_OJ_STAR_PIECE_BURST_F, &kpl[i].pos.x);
    }
    kp_plate_fx(i, "Break", NULL);
    kpl[i].alive = 0; kp_plate_coll(i, 0);
    return 1;
}
static void kp_plates_update(void) /* KoopaBattleMapDamagePlate: Break 180 (burning Mario after 30), Recover */
{
    for (int i = 0; i < 6; i++) {
        if (!kpl[i].alive || kpl[i].nrv == 0) continue;
        int st = ++kpl[i].st;
        if (kpl[i].nrv == 1) {
            if (st == 0) kp_plate_fx(i, "Break", NULL);
            kp_plate_fx(i, "LavaBubble", &kpl[i]); /* (kept until Recover deletes it) */
            if (st > 30) { /* attackSensor: sendMsgEnemyAttackFire (its sensor 250, 150 under it) */
                V3 c = vsub(kpl[i].pos, vmul(vnorm(vsub(kpl[i].pos, kp_pc)), 150.0f));
                if (vlen(vsub(mw.c, c)) < 250.0f + HIT_BODY_R) mario_attack(ATK_FIRERUN, c);
            }
            if (st == 180) { kpl[i].nrv = 2; kpl[i].st = -1; }
        } else {
            if (st == 0) { kp_plate_fx(i, "Recover", NULL); se_start(SE_OJ_KOOPA_PLATE_CLOSE, &kpl[i].pos.x); }
            if (st >= 30) { kpl[i].nrv = 0; kp_plate_coll(i, 1); } /* isBrkStopped (Revival: 30 frames) */
        }
    }
    if (kp_star_demo >= 0 && ++kp_star_demo == (kp.vs == 3 ? 151 : 90)) kp_plate_fx(kp.vs == 3 ? 0 : 1, "GrandStarAppear", NULL); /* exeDemoGrandStarAppear: mDamagePlate (_DD, Vs3 Lv3: the first) */
}

/* --- KoopaFireStairs (MeteorStrike): flies to the stair's target at a constant speed, timed to its break; at the
 * break the Hit / MeteorStrikeBreak effects, its break model's Break, killed at its end ------------------------------ */
static void kp_stair_target(int k, V3 ref, V3 *out, float *dist) /* KoopaBattleMapStair::calcAndSetTargetPos */
{
    static const float off[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    const Row *r = &sc->objs[kss[k].row];
    const char *cls = sc->types + 32 * r->type;
    int big = !strcmp(cls, "KoopaBattleMapStairBig"), turn = !strcmp(cls, "KoopaBattleMapStairTurn"), sel = r->arg[5];
    V3 ax, az, ay;
    row_axes(r, &ax, &az); ay = vnorm(vcross(az, ax));
    V3 c = vadd(vmul(ay, 100.0f), kss[k].pos); /* sHalfHeight */
    float d = vlen(vsub(c, ref));
    *out = c;
    if (turn) { /* sTurnHalfWidthZ / X */
        V3 p = vsub(vadd(c, vmul(az, 300.0f)), vmul(ax, 140.0f));
        float e = vlen(vsub(p, ref));
        if (e <= d) { *out = p; d = e; }
    } else
        for (int i = 0; i < 8; i++) {
            V3 p = vadd(vadd(c, vmul(ax, (big ? 600.0f : 200.0f) * off[i][0])), vmul(az, (big ? 400.0f : 300.0f) * off[i][1]));
            if (sel >= 0) { if (sel == i) *out = p; continue; }
            float e = vlen(vsub(p, ref));
            if (e <= d) { *out = p; d = e; }
        }
    kss[k].ac = *out;
    *dist = d;
}
static int kp_stair_break_time(int k) { const Row *r = &sc->objs[kss[k].row]; return r->arg[0] != -1 ? r->arg[0] : 300; } /* mTimeToBreak */
static int kp_fire_stairs(int k, V3 from) /* KoopaParts::emitFireStairsToPos (useFront false: Vs1) */
{
    if (k < 0) return -1;
    for (int i = 0; i < KP_FIRE_STAIRS; i++) {
        if (kpfst[i].on) continue;
        typeof(&kpfst[0]) f = &kpfst[i];
        memset(f, 0, sizeof *f);
        f->on = 1; f->stair = k; f->pos = f->a0 = from; f->rotx = 0; f->st = -1;
        int bt = kp_stair_break_time(k) - kss[k].step; /* calcRemainTimeToBreak */
        V3 d = vsub(kss[k].ac, from);
        float len = vlen(d);
        f->vel = f->front = len > 1e-6f ? vnorm(d) : v3(0, 0, 1);
        f->vel = vmul(f->vel, len / (float)(bt - 1 > 0 ? bt - 1 : 1));
        se_start(SE_OJ_KOOPA_FIRE_SHOT, &f->pos.x); /* appear: emitEffect MeteorStrike */
        return i;
    }
    return -1;
}
static void kp_fire_stairs_update(void)
{
    for (int i = 0; i < KP_FIRE_STAIRS; i++) {
        typeof(&kpfst[0]) f = &kpfst[i];
        if (!f->on) continue;
        int st = ++f->st;
        float m[16];
        V3 up = up_at(f->pos, v3(0, 1, 0));
        basis_matrix(m, f->pos, up, f->front, 0);
        if (f->nrv == 0) { /* Fly */
            fx_named("MeteorStrike", "MeteorStrike", m, NULL, f);
            if (kss[f->stair].nrv == KS_WAITFALL) { /* mStair->isBreak() */
                V3 h = vadd(f->pos, vmul(vlen(f->vel) > 1e-6f ? vnorm(f->vel) : v3(0, 0, 0), 100.0f));
                float hm[16];
                basis_matrix(hm, h, up, f->front, 0);
                for (int q = 0; q < 12; q++) if (q % 4 != 3) hm[q] *= 2.0f; /* setEffectBaseScale 2 */
                fx_named("KoopaFireStairs", "Hit", hm, NULL, NULL); /* emitEffectHit */
                fx_named("MeteorStrike", "MeteorStrikeBreak", m, NULL, NULL);
                f->vel = v3(0, 0, 0);
                se_start(SE_OJ_KOOPA_FIRE_EXPLODE, &f->pos.x);
                pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_WEAK);
                f->nrv = 1; f->st = -1; f->mf = 0;
            } else f->rotx = fmodf(f->rotx + 15.0f, 360.0f); /* sRotateSpeed */
        } else { /* Break: the break model's Break (its BCK's length), kill at its end */
            if (st == 0 && ka_mbreak && m_kmeteorb && m_kmeteorb->skin) {
                anim_bind(ka_mbreak, m_kmeteorb->skin); ka_mbreak->clip_id = -1; anim_start(ka_mbreak, "meteorstrikebreak/break", 0);
                f->dur = anim_length(ka_mbreak);
            }
            f->mf += 1.0f;
            if (f->mf >= (float)(f->dur > 0 ? f->dur : 60)) f->on = 0;
        }
        f->pos = vadd(f->pos, f->vel);
    }
}
static void kp_fire_stairs_kill(void) { for (int i = 0; i < KP_FIRE_STAIRS; i++) kpfst[i].on = 0; } /* killFireStairsAll */

/* --- KoopaFireShort (KoopaFire): from Tongue2 along its Y on the ground plane, 15 (fast 30), 180 frames (long 900),
 * sinking 4 a frame for 50, its sensor (80) on after 30: Mario burnt, Disappear ---------------------------------------- */
static void kp_fire_short(int fast, int curve, int lng)
{
    for (int i = 0; i < KP_FIRE_SHORT; i++) {
        if (kpfs[i].on) continue;
        typeof(&kpfs[0]) f = &kpfs[i];
        memset(f, 0, sizeof *f);
        float t[16];
        kp_joint_m(KJ_TONGUE2, t);
        f->on = 1; f->pos = v3(t[12], t[13], t[14]); f->grav = vmul(up_at(f->pos, kp_up()), -1.0f);
        V3 y = v3(t[4], t[5], t[6]), v = vtangent(y, f->grav);
        f->vel = vmul(vlen(v) > 1e-6f ? vnorm(v) : y, 15.0f); /* sFlySpeedNormal */
        f->front = v3(0, 0, 1);
        f->speed = curve || fast ? 30.0f : 15.0f; f->dur = lng ? 900 : curve ? 300 : 180;
        if (curve) { V3 side = vcross(vmul(f->grav, -1.0f), kp.b.front); f->off = vdot(vsub(mario->pos, f->pos), side) > 0 ? 0.1f : -0.1f; } /* isPlayerLeftSide */
        return;
    }
}
static void kp_fire_short_fly(int i)
{
    typeof(&kpfs[0]) f = &kpfs[i];
    f->vel = vtangent(f->vel, f->grav); /* updateFrontAndVelocity */
    if (vlen(f->vel) > 1e-6f) { f->vel = vnorm(f->vel); f->front = f->vel; }
    f->vel = vmul(f->vel, f->speed);
    f->vel = rot_axis(f->vel, vmul(f->grav, -1.0f), f->off * 0.0174533f);
    f->lv = se_level(SE_OJ_LV_KOOPA_BULLET_FLY, &f->pos.x, f->lv);
}
static void kp_fire_short_update(void)
{
    for (int i = 0; i < KP_FIRE_SHORT; i++) {
        typeof(&kpfs[0]) f = &kpfs[i];
        if (!f->on) continue;
        int st = f->st++;
        f->grav = vmul(up_at(f->pos, vmul(f->grav, -1.0f)), -1.0f);
        if (f->nrv == 0) { /* Fly */
            if (st == 0) { se_start(SE_OJ_KOOPA_BULLET_SHOOT, &f->pos.x); f->f = 0; }
            if (st == 30) f->valid = 1; /* sStepToValidSensor */
            kp_fire_short_fly(i);
            if (st < 50) f->pos = vadd(f->pos, vmul(f->grav, 4.0f));
            if (st > f->dur) { f->nrv = 1; f->st = 0; f->f = 0; se_start(SE_OJ_KOOPA_BULLET_EXPLODE, &f->pos.x); }
            else if (f->valid && vlen(vsub(mw.c, f->pos)) < 80.0f + HIT_BODY_R && mario_attack(ATK_FIRERUN, f->pos)) { /* sendMsgEnemyAttackFire */
                f->speed = 0; f->vel = v3(0, 0, 0); f->nrv = 1; f->st = 0; f->f = 0; f->valid = 0;
                se_start(SE_OJ_KOOPA_BULLET_EXPLODE, &f->pos.x);
            }
        } else { /* Disappear */
            if (f->speed > 0) kp_fire_short_fly(i);
            if (f->f >= 20.0f) f->on = 0; /* isBckStopped (Disappear: 20 frames) */
        }
        f->f += 1.0f;
        f->pos = vadd(f->pos, f->vel);
    }
}

/* --- KoopaShockWave: a ring over the planet from Koopa's place round to its far side (0..180 degrees over 360 frames),
 * its sensor (15) at the ring where Mario's side is: Mario on the ground hurt, then ACTMES_KOOPA_PLAYER_DAMAGE ---------- */
static void kbm_player_damage(void);
static void kp_shock_wave(void)
{
    for (int i = 0; i < KP_SHOCK; i++) {
        if (ksw[i].on) continue;
        typeof(&ksw[0]) w = &ksw[i];
        w->on = 1; w->st = -1; w->ang = 0;
        w->up = vsub(kp.b.pos, kp_pc); w->up = vlen(w->up) > 1e-6f ? vnorm(w->up) : kp_up(); w->front = kp.b.front;
        basis_matrix(w->m94, vadd(kp_pc, vmul(w->up, 1300.0f)), kp_up(), kp.b.front, 0); /* makeMtxUpFrontPos(-gravity, front, up * radius) */
        se_start(SE_BM_KOOPA_SWAVE_SHOOT, &kp.b.pos.x);
        return;
    }
}
static void kp_shock_update(void)
{
    for (int i = 0; i < KP_SHOCK; i++) {
        typeof(&ksw[0]) w = &ksw[i];
        if (!w->on) continue;
        int st = ++w->st;
        w->ang = (st > 360 ? 1.0f : st / 360.0f) * 180.0f; /* calcNerveValue(360, 0, 180) */
        float c = cosf(w->ang * 0.0174533f);
        basis_matrix(w->mb, vadd(kp_pc, vmul(w->up, c * 1300.0f)), w->up, w->front, 0); /* makeShockWaveMtx */
        basis_matrix(w->mm, vadd(kp_pc, vmul(w->up, c * 1260.0f)), w->up, w->front, 0);
        float vol = w->ang <= 90.0f ? w->ang / 90.0f : 1.0f - (w->ang - 90.0f) / 90.0f;
        kp.slv[2] = se_level(SE_BM_LV_KOOPA_SWAVE_MOVE, &kp.b.pos.x, kp.slv[2]);
        if (kp.slv[2]) se_volpitch(kp.slv[2], vol, 1.0f);
        /* updateHitSensor: Mario's azimuth about the ring's axis (_94), the ring's point there */
        float *m = w->m94;
        V3 ax = v3(m[0], m[1], m[2]), ay = v3(m[4], m[5], m[6]), az = v3(m[8], m[9], m[10]), d = vsub(mario->pos, v3(m[12], m[13], m[14]));
        float lx = vdot(d, ax), lz = vdot(d, az), deg = atan2f(-lz, lx) * 57.29578f + 180.0f;
        V3 r = v3(0, 1300.0f, 0);
        r = rot_axis(r, v3(0, 0, 1), w->ang * 0.0174533f);
        r = rot_axis(r, v3(0, 1, 0), deg * 0.0174533f);
        w->sp = vadd(kp_pc, vadd(vadd(vmul(ax, r.x), vmul(ay, r.y)), vmul(az, r.z)));
        if (st > 0 && mario->grounded && vlen(vsub(mw.c, w->sp)) < 15.0f + HIT_BODY_R && mario_attack(ATK_ENEMY, w->sp)) kbm_player_damage(); /* attackSensor */
        if (st >= 360) w->on = 0;
    }
}

/* --- the sensors: their places at their joints (offsets in joint space), Mario's touch and spin --------------------- */
static void kp_sensors_place(void)
{
    float b[16], jm[16];
    kp_base(b);
    for (int s = 0; s < KS_N; s++) {
        const float *o = kp_sdef[s].o;
        if (kp_sdef[s].joint >= 0) kp_joint_m(kp_sdef[s].joint, jm); else memcpy(jm, b, 64);
        kp.sens[s][0] = jm[12] + jm[0] * o[0] + jm[4] * o[1] + jm[8] * o[2];
        kp.sens[s][1] = jm[13] + jm[1] * o[0] + jm[5] * o[1] + jm[9] * o[2];
        kp.sens[s][2] = jm[14] + jm[2] * o[0] + jm[6] * o[1] + jm[10] * o[2];
        kp.sens[s][3] = kp_sdef[s].r;
    }
}
static V3 kp_sc(int s) { return v3(kp.sens[s][0], kp.sens[s][1], kp.sens[s][2]); }
static int kp_touch(int s) { return vlen(vsub(mw.c, kp_sc(s))) < kp.sens[s][3] + HIT_BODY_R; } /* the two sensors meet */
/* the messages to Mario (sendMsgPush, sendMsgEnemyAttack(Maximum / Flip / FlipMaximum)) */
static int kp_push(int s) { mario_push_out(kp_sc(s), kp.sens[s][3]); return 1; }
static int kp_try_push(int s) { return kp_sdef[s].type == KPT_PUSH && kp_push(s); }                                       /* tryKoopaPushPlayer */
static int kp_try_body(int s) { return kp_sdef[s].type == KPT_ENEMY_ATTACK && mario_attack(ATK_ENEMY, kp_sc(s)); }         /* tryKoopaBodyAttackPlayer */
static int kp_try_body_max(int s) { return kp_sdef[s].type == KPT_ENEMY_ATTACK && mario_attack(ATK_MAXIMUM, kp_sc(s)); }   /* ...Maximum */
static int kp_try_shell(int s) { return s == KS_ATTACKSHELL && mario_attack(ATK_ENEMY, kp_sc(s)); }                      /* tryKoopaShellAttackPlayer */

/* --- KoopaBattleBase ---------------------------------------------------------------------------------------------- */
static void kbb_wall_jump(void) { if (kp.b.on_wall) kp_jump_add(15.0f); } /* isBindedWall: sendMsgEnemyAttackToBindedSensor (none), addVelocityJump(sChasePlayerJumpSpeed) */
static void kbb_chase(const KpMove *p, int st)
{
    if (st == 0) { kp_act("Walk"); kp.b.bind_off = 0; }
    kbb_wall_jump();
    kp_to_player(p);
    kp_rate(vlen(kp.b.vel) * 0.18f); /* sWalkAnimRate */
}
static int kbb_wander(const KpMove *p, int st)
{
    if (st == 0) { kp_act("Walk"); kp.b.bind_off = 0; kp.wander_time = 60 + rand() % 61; } /* getRandom(60, 120) */
    kbb_wall_jump();
    kp_move(p, kp.b.front);
    kp_rate(vlen(kp.b.vel) * 0.18f);
    return st == kp.wander_time;
}
static int kbb_search(int st)
{
    if (st == 0) { kp_act("Search"); kp.b.vel = v3(0, 0, 0); }
    kp.b.front = rot_axis(kp.b.front, kp.b.grav, 2.4f * 0.0174533f); /* rotateVecDegree(front, gravity, sSearchTurnSpeed) */
    return st == 120;
}
static int kbb_find(int st)
{
    if (st == 0) { kp_act("Find"); kp_jump_set(10.0f); }
    kp_to_player(&kp_findp);
    if (st != 0 && kp.b.on_ground) { kp_rumble(0, 800.0f, 1200.0f, 2000.0f); return 1; }
    return 0;
}
static void kbb_kill(void) /* KoopaBattleBase::kill */
{
    kp.b.vel = v3(0, 0, 0);
    kp_recover_armor(); kp_recover_thorn();
    render_cover(2);
}

/* --- the states ---------------------------------------------------------------------------------------------------- */
static Nv *kst(int k) { return &kp.st[k]; }
static void kst_kill(int k);
static void kr_appear(int k), kr_kill(int k), kr_update(int k);
static int kr_attack(int k, int s), kr_guardable(int k);
static void kr_set_1d(void);
static int kr_hit_player(void);
static void kst_appear(int k)
{
    Nv *n = kst(k);
    kp.sdead[k] = 0;
    switch (k) {
    case KST_GUARD: break; /* (tryStart set its nerve) */
    case KST_HIPDROP:
        kp.calc_grav = 1; kp.b.bind_off = 0; kp_sensor(KS_ATTACKHIPDROP, 1); kp.hip_att = 0;
        if (kp.vs == 1) { kp.hip_max = 3; kp.hip_delay = 30; nv_set(n, KH_RUN); } /* sJumpWaitAirStep */
        else if (kp.vs == 2) { kp.hip_max = 2; kp.hip_delay = 15; nv_set(n, KH_JUMPUP); }
        else { kp.hip_delay = 15; kp.hip_max = kp.angry ? 2 : 3; nv_set(n, kp_near(2000.0f) ? KH_JUMPUP : KH_RUN); }
        break;
    case KST_SHOCKWAVE:
        kp.sw_att = 0; kp_sensor(KS_ATTACKHIPDROP, 1);
        if (kp.vs == 1) { kp.sw_max = kp.lv == 3 ? 3 : kp.lv == 2 ? 2 : 1; kp.sw_jump = 40.0f; kp.sw_grav = 1.5f; kp.sw_delay = 15; }
        else if (kp.vs == 2) { kp.sw_max = kp.lv == 3 ? 5 : 3; kp.sw_jump = 40.0f; kp.sw_grav = 1.5f; kp.sw_delay = 10; }
        else if (kp.lv == 1) { kp.sw_max = 3; kp.sw_jump = 40.0f; kp.sw_grav = kp.angry ? 2.0f : 1.5f; kp.sw_delay = kp.angry ? 5 : 10; }
        else { kp.sw_jump = 40.0f; kp.sw_grav = 2.0f; kp.sw_max = kp.angry ? 5 : 3; kp.sw_delay = kp.angry ? 5 : 10; }
        nv_set(n, KW_JUMPSTART);
        break;
    case KST_FIRESHORT:
        kp.fs_n = 0;
        if (kp.vs == 1) { kp.fs_max = kp.lv == 2 ? 3 : 5; kp.fs_delay = 45; }
        else if (kp.vs == 2) { kp.fs_max = 5; kp.fs_delay = 30; }
        else if (kp.lv == 2) { kp.fs_max = kp.angry ? 5 : 3; kp.fs_delay = kp.angry ? 15 : 30; }
        else if (kp.lv == 3) { kp.fs_max = kp.angry ? 10 : 5; kp.fs_delay = 15; }
        else { kp.fs_max = 3; kp.fs_delay = 30; }
        nv_set(n, KF_START);
        break;
    case KST_DAMAGEESCAPE: {
        static const KpMove run1 = {7.0f, 1.5f, 0.8f, 0.0f}, run2 = {7.0f, 1.5f, 0.8f, 0.2f}, run3 = {7.0f, 1.5f, 0.8f, 0.4f};
        static const KpMove tvs1 = {0.8f, 1.5f, 0.8f, 0.0f}, tail = {6.0f, 1.5f, 0.8f, 0.0f}, tail3 = {8.0f, 1.5f, 0.8f, 0.0f};
        kp_cam_end("逃走開始");
        kp_sensor(KS_RECEIVERTAIL, 1); kp_sensor(KS_RECEIVERTAILTOP, 1);
        kp.de_time = 0; kp.de_frames = 600; /* sDamageTailStep */
        bgm_state(2, 90); /* changeBgmStateEscape */
        if (kp.lv == 1) {
            kp.de_run = &run1; kp.de_rot = 0.0f;
            if (kp.vs == 1) { kp.de_tail = &tvs1; kp.de_max = 900; } else { kp.de_max = 600; kp.de_tail = &tail3; }
            kp.de_away = 40.0f;
        } else if (kp.lv == 2) { kp.de_max = 600; kp.de_run = &run2; kp.de_rot = 40.0f; kp.de_tail = &tail; kp.de_away = 40.0f; }
        else { kp.de_max = 600; kp.de_run = &run3; kp.de_rot = 40.0f; kp.de_tail = &tail3; kp.de_away = 60.0f; }
        kp.de_last = kp.vs != 3 ? kp.lv == 3 : kp.angry;
        nv_set(n, KD_ESCAPESTART);
        break;
    }
    case KST_JUMPAWAY: nv_set(n, KJA_JUMPSTART); break;
    case KST_JUMPTOPLANET: break; /* (ActorStateBase::appear) */
    case KST_POWERUP: nv_set(n, KP_WAITDEMO); break;
    case KST_SPIN: { static const KpMove s1 = {2.0f, 1.0f, 0.9f, 0.0f}; kp_sensor(KS_ATTACKSPIN, 1); kp.sp_att = 0; kp.sp_max = kp.lv; kp.sp_move = &s1; nv_set(n, KSP_WAITTOSTART); break; }
    case KST_FIRELONG: kp.fl_n = 0; nv_set(n, KFL_START); break;
    case KST_CHASEROLL: case KST_ROLL: kr_appear(k); break;
    }
}
static void kst_kill(int k)
{
    kp.sdead[k] = 1;
    switch (k) {
    case KST_HIPDROP: kp_cam_end("ヒップドロップ攻撃"); kp_cam_end("ヒップドロップ着地"); kp_cam_end("ヒップドロップ着地（マリオ痺れ）"); kp_sensor(KS_ATTACKHIPDROP, 0); break;
    case KST_SHOCKWAVE: kp_sensor(KS_ATTACKHIPDROP, 0); break;
    case KST_DAMAGEESCAPE:
        bgm_state(1, 120); /* changeBgmStateNormal */
        kp_cam_end("逃走"); kp_cam_end("逃走（尻尾ダメージ）"); kp_cam_end("逃走ダウン");
        kp_sensor(KS_RECEIVERTAIL, 0); kp_sensor(KS_RECEIVERTAILTOP, 0); kp_sensor(KS_DAMAGEESCAPEBODY, 0); kp_sensor(KS_DAMAGEESCAPEPUSH, 0);
        break;
    case KST_JUMPTOPLANET: kp_cam_end("ウェイト（惑星までジャンプ）"); kp_cam_end("落下（惑星までジャンプ）"); kp_cam_end("ワープ後（惑星までジャンプ）"); break;
    case KST_POWERUP: kp_cam_end("パワーアップデモ"); kp_cam_end("最終パワーアップデモ"); break;
    case KST_SPIN: kp_sensor(KS_ATTACKSPIN, 0); break;
    case KST_CHASEROLL: case KST_ROLL: kr_kill(k); break;
    }
}
static int kst_guard_try(int s) /* KoopaStateGuard::tryStart (a spin of Mario's on sensor s) */
{
    if (s == KS_FACE || s == KS_GUARDBODY) { mario_attack(ATK_FLIP, kp_sc(s)); kp.guard_n = s == KS_FACE ? KG_FACE : KG_BODY; }
    else if (s == KS_TAIL || s == KS_TAILTOP) { mario_attack(ATK_FLIP_MAXIMUM, kp_sc(s)); kp.guard_n = KG_TAIL; }
    else return 0;
    nv_set(kst(KST_GUARD), kp.guard_n);
    return 1;
}
static int kst_hip_guardable(void) { Nv *n = kst(KST_HIPDROP); return nv_is(n, KH_RUN) || nv_is(n, KH_JUMPSTART) || nv_is(n, KH_JUMPUP) || nv_is(n, KH_LAND); } /* isEnableGuard */
static void kst_update(int k) /* ActorStateBase::update: updateNerve */
{
    static const KpMove guard_fall = {0.0f, 1.0f, 0.98f, 0.0f}, run_start = {3.0f, 1.0f, 0.9f, 3.0f}, jump_start = {0.0f, 1.0f, 0.95f, 0.0f},
                        fall = {0.0f, 1.0f, 1.0f, 0.0f}, esc_start = {1.8f, 1.5f, 0.95f, 3.0f}, esc_find = {0.0f, 1.0f, 0.9f, 0.0f},
                        esc_end = {0.0f, 1.0f, 0.95f, 0.0f}, tail_end = {0.0f, 1.0f, 0.98f, 0.0f},
                        jfall = {0.0f, 1.0f, 0.98f, 0.0f}, jwait = {0.0f, 1.0f, 0.9f, 1.0f};
    static const KpSight esc_sight = {1500.0f, 60.0f, 30.0f};
    Nv *n = kst(k);
    int st = nv_run(n), v = n->nrv;
    switch (k) {
    case KST_GUARD: /* GuardFace / GuardBody / GuardTail */
        if (st == 0) { kp_act(v == KG_FACE ? "GuardFace" : v == KG_BODY ? "GuardBody" : "AttackTail"); if (v != KG_TAIL) kp_se(SE_BV_KOOPA_GUARD); kp.b.vel = v3(0, 0, 0); }
        kp_to_player(&guard_fall);
        if (kp_act_end()) kst_kill(k);
        break;
    case KST_HIPDROP:
        switch (v) {
        case KH_RUN:
            if (st == 0) { kp_act("Run"); kp_cam_start("ヒップドロップ攻撃"); }
            kp_to_player(&run_start);
            if (kp.b.on_wall) kp_jump_add(10.0f); /* sRunWallJumpSpeed */
            if ((kp_near(1000.0f) || st > 180) && vdot(kp.b.vel, kp.b.grav) >= 0.0f) nv_set(n, KH_JUMPSTART); /* sDistanceToJump, sRunStepMax */
            break;
        case KH_JUMPSTART:
            if (st == 0) kp_act("HipDropJumpStart");
            kp_to_player(&jump_start);
            if (kp_act_end()) nv_set(n, KH_JUMPUP);
            break;
        case KH_JUMPUP:
            if (st == 0) {
                kp_act("HipDropJump"); kp_se(SE_BM_KOOPA_JUMP);
                kp.b.vel = vtangent(kp.b.vel, kp.b.grav);
                if (vlen(kp.b.vel) > 20.0f) kp.b.vel = vmul(vnorm(kp.b.vel), 20.0f); /* sJumpUpSpeedFrontMax */
                kp_jump_add(50.0f); /* sJumpUpSpeedUp */
                kp_cam_start("ヒップドロップ攻撃");
            }
            kp_grav_add(1.0f);
            kp_turn_player(3.0f);
            if (vdot(kp.b.vel, kp.b.grav) >= 0.0f) nv_set(n, KH_JUMPWAITAIR);
            break;
        case KH_JUMPWAITAIR:
            if (st == 0) kp.b.vel = v3(0, 0, 0);
            kp_turn_player(2.0f);
            if (st == 0) kp.hip_ground = kp.mground; /* getPlayerGroundPos */
            if (st == kp.hip_delay) nv_set(n, KH_JUMPATTACKDOWN);
            break;
        case KH_JUMPATTACKDOWN: {
            if (st == 0) {
                kp_act("HipDropAttackDown");
                V3 d = vsub(kp.hip_ground, kp.b.pos);
                kp.hip_time = (int)(vlen(d) / 35.0f) + 10;
                kp.b.vel = vlen(d) > 1e-6f ? vmul(vnorm(d), 35.0f) : v3(0, 0, 0);
                kp_se(SE_BV_KOOPA_HIPDROP);
            }
            if (!kp.b.on_ground && !(st > kp.hip_time)) break;
            if (kp.b.on_ground) {
                int pl = kp_bind_plate();
                if (kp_plate_hipdrop(pl)) { /* sendMsgToBindedSensor(ACTMES_KOOPA_HIP_DROP_ATTACK) */
                    kp_se(SE_BM_KOOPA_LAND); scene_stop = 15;
                    if (kpl[pl].kind == KPL_DAMAGE) { /* ACTMES_KOOPA_PLATE_DAMAGE: the damage plate takes it */
                        kp.b.vel = v3(0, 0, 0);
                        kp_rumble(2, 1500.0f, 3000.0f, 2000.0f);
                        kp_se(SE_OJ_KOOPA_PLATE_LAVA_ST);
                        nv_set(n, KH_DAMAGE);
                    } else kp_rumble(0, 1500.0f, 3000.0f, 2000.0f);
                    break;
                }
            }
            kp.b.vel = v3(0, 0, 0);
            kp_rumble(1, 1500.0f, 3000.0f, 2000.0f);
            kp_shock_wave();
            kp_cam_start("ヒップドロップ着地");
            kp_se(SE_BM_KOOPA_LAND);
            nv_set(n, KH_LAND);
            break;
        }
        case KH_LAND:
            if (st == 0) kp_act("HipDropLand");
            kp_to_player(&fall);
            if (kp_act_end()) {
                kp.hip_att++;
                if (kp.hip_att >= kp.hip_max || dm_damaging()) kst_kill(k); else nv_set(n, KH_RUN);
            }
            break;
        case KH_DAMAGE: break;
        }
        break;
    case KST_SHOCKWAVE:
        switch (v) {
        case KW_JUMPSTART:
            if (st == 0) kp_act("ShockWaveJumpStart");
            if (kp_act_end()) nv_set(n, KW_JUMP);
            break;
        case KW_JUMP: case KW_JUMPFAST:
            if (st == 0) {
                kp_act(kp.sw_att == kp.sw_max - 1 ? "ShockWaveJump3rd" : v == KW_JUMPFAST ? "JumpFast" : "ShockWaveJump");
                kp_se(SE_BM_KOOPA_JUMP);
                kp_jump_set(kp.sw_att == 0 ? 50.0f : kp.sw_jump); /* sJumpSpeed1st */
            }
            kp_grav_add(kp.sw_grav);
            if (kp.vs == 3 && kp.lv == 3) kp_turn_player(3.0f);
            if (w_on_ground(&kp.b)) { /* isOnGround */
                kp_rumble(0, 1500.0f, 2500.0f, 2000.0f); camera_shake(SHAKE_WEAK);
                kp_se(SE_BM_KOOPA_LAND);
                kp_shock_wave();
                kp.b.vel = v3(0, 0, 0);
                nv_set(n, KW_LAND);
            }
            break;
        case KW_LAND:
            if (st == 0) kp_act("ShockWaveLand");
            if (st == kp.sw_delay && ++kp.sw_att < kp.sw_max) nv_set(n, KW_JUMPFAST);
            else if (kp_act_end()) kst_kill(k);
            break;
        }
        break;
    case KST_FIRESHORT:
        if (v == KF_START) {
            if (st == 0) { kp.b.vel = v3(0, 0, 0); kp_act("AttackFireStart"); }
            kp_to_player(&fall);
            if (kp_act_end()) { kp_act("AttackFire"); nv_set(n, KF_EMIT); }
        } else {
            if (st == 0) kp.fs_n++;
            kp_turn_player(2.0f); /* sTurnSpeed */
            kp_to_player(&fall);
            if (st == 1) { /* sStepToEmitFire */
                if (kp.vs == 1 && kp.lv != 3) kp_fire_short(0, 0, 0);
                else if (kp.vs == 2) kp_fire_short(0, 1, 0);
                else kp_fire_short(1, 0, 0);
            }
            if (kp.fs_n < kp.fs_max) { if (st == kp.fs_delay) nv_set(n, KF_EMIT); }
            else if (st > 30) kst_kill(k);
        }
        break;
    case KST_DAMAGEESCAPE:
        switch (v) {
        case KD_ESCAPESTART:
            if (st == 0) {
                kp_act("EscapeRunStart"); kp_jump_set(80.0f); /* sEscapeStartJumpSpeed */
                kp_cam_start("逃走開始"); kp_break_thorn();
                kp_se(SE_BM_KOOPA_BURN); kp_se(SE_BV_KOOPA_BURN_START);
            }
            kp_escape(&esc_start);
            if (kp_act_end()) nv_set(n, KD_ESCAPESTARTLOOP);
            break;
        case KD_ESCAPESTARTLOOP:
            if (st == 0) kp_act("EscapeRunStartLoop");
            kp_escape(&esc_start);
            if (kp.b.on_ground) { kp_rumble(1, 800.0f, 1200.0f, 2000.0f); kp_se(SE_BM_KOOPA_LAND); nv_set(n, KD_ESCAPESTARTLAND); }
            break;
        case KD_ESCAPESTARTLAND:
            if (st == 0) kp_act("EscapeRunStartLand");
            kp_escape(&esc_start);
            if (kp_act_end()) nv_set(n, KD_ESCAPERUN);
            break;
        case KD_ESCAPERUN:
            if (st == 0) { kp_act("EscapeRun"); kp_cam_start("逃走"); }
            kp_escape(kp.de_run);
            if (++kp.de_time >= kp.de_max) { nv_set(n, KD_ESCAPEEND); break; }
            if (kp_sight(&esc_sight)) { nv_set(n, KD_ESCAPEFIND); break; }
            if (kp.b.on_wall) kp_jump_add(15.0f); /* sEscapeRunWallJumpSpeed */
            break;
        case KD_ESCAPEFIND:
            if (st == 0) kp_act("EscapeTurn");
            kp_escape(&esc_find);
            kp.de_time++;
            if (kp_act_end()) { kp.b.front = vmul(kp.b.front, -1.0f); kp_act("EscapeTurnEnd"); nv_set(n, KD_ESCAPERUN); }
            break;
        case KD_ESCAPEEND:
            if (st == 0) kp_act("EscapeEnd");
            kp_escape(&esc_end);
            if (kp_act_end()) { kp_recover_thorn(); kst_kill(k); }
            break;
        case KD_TAILRUNSTART: case KD_TAILRUNSTARTFINAL:
            if (st == 0) {
                kp_act(v == KD_TAILRUNSTART ? "DamageTailRunStart" : "DamageTailRunFastStart");
                V3 h = vtangent(vsub(kp.b.pos, mario->pos), kp.b.grav); /* calcVecFromPlayerH */
                kp.b.front = vlen(h) > 1e-6f ? vnorm(h) : kp.b.front;
                kp.b.vel = vmul(kp.b.front, kp.de_rot);
                kp_cam_start("逃走（尻尾ダメージ）");
                kp_sensor(KS_RECEIVERTAIL, 0); kp_sensor(KS_RECEIVERTAILTOP, 0); kp_sensor(KS_DAMAGEESCAPEBODY, 1); kp_sensor(KS_DAMAGEESCAPEPUSH, 1);
            }
            if (kp.b.on_wall) kp_jump_add(15.0f); /* sDamageTailWallJumpSpeed */
            kp_escape(kp.de_tail);
            kp_lv(0, SE_BM_LV_KOOPA_SWOON);
            kp_lv(1, v == KD_TAILRUNSTART ? SE_BM_LV_KOOPA_TAIL_RUN : SE_BM_LV_KOOPA_TAIL_RUN_FAST);
            if (st == 40) kp_break_armor(); /* sDamageTailRunStartBreakStep */
            if (kp_act_end()) nv_set(n, v == KD_TAILRUNSTART ? KD_TAILRUN : KD_TAILRUNFINAL);
            break;
        case KD_TAILRUN: case KD_TAILRUNFINAL:
            if (st == 0) { kp_act(v == KD_TAILRUN ? "DamageTailRun" : "DamageTailRunFast"); kp.de_cw = rand() & 1; }
            if (kp.b.on_wall) kp_jump_add(15.0f);
            kp_escape(kp.de_tail);
            kp_lv(0, SE_BM_LV_KOOPA_SWOON);
            kp_lv(1, v == KD_TAILRUN ? SE_BM_LV_KOOPA_TAIL_RUN : SE_BM_LV_KOOPA_TAIL_RUN_FAST);
            kp.b.front = rot_axis(kp.b.front, kp_up(), (kp.de_cw ? 0.1f : -0.1f) * 0.0174533f); /* sDamageTailTurnDegree */
            if (st == kp.de_frames) nv_set(n, v == KD_TAILRUN ? KD_TAILRUNEND : KD_TAILRUNENDFINAL);
            break;
        case KD_TAILRUNEND: case KD_TAILRUNENDFINAL:
            if (st == 0) kp_act("DamageTailRunEnd");
            kp_escape(&tail_end);
            if (st == 105) { kp_recover_armor(); kp_recover_thorn(); } /* sDamageTailRunEndRecoverStep */
            if (kp_act_end()) { kp.b.vel = v3(0, 0, 0); kst_kill(k); }
            break;
        case KD_DOWN: {
            if (st == 0) {
                kp_act("Down");
                V3 h = vtangent(vsub(kp.b.pos, mario->pos), kp.b.grav); /* setVelocityJumpAwayFromPlayer(35, mJumpAwayVelocity) */
                kp.b.vel = vmul(vlen(h) > 1e-6f ? vnorm(h) : v3(0, 0, 0), 35.0f);
                kp_jump_add(kp.de_away);
                kp_cam_start("逃走ダウン"); kp_fx("DownSmoke");
            }
            kp_grav_add(2.0f); /* sDownGravity */
            V3 h = vtangent(kp.b.vel, kp.b.grav);
            if (vlen(h) > 1e-3f) kp.b.front = vmul(vnorm(h), -1.0f);
            if (st != 0 && kp.b.on_ground) {
                kp_rumble(1, 1500.0f, 2000.0f, 2000.0f); kp_se(SE_BM_KOOPA_LAND);
                int pl = kp_bind_plate(); /* PLATE_DAMAGE first (a damage plate takes it), else the hip drop's message */
                if (!(pl >= 0 && kpl[pl].kind == KPL_DAMAGE) && kp_plate_hipdrop(pl)) { scene_stop = 5; break; } /* sDownStopSceneStep */
                kp_fx("DownSmoke"); kp.b.vel = v3(0, 0, 0);
                nv_set(n, KD_DOWNLAND);
            }
            break;
        }
        case KD_DOWNLAND:
            if (st == 0) {
                bgm_state(1, 120); kp_act("DownLand");
                if (kp.vs == 3 && kp.lv != 3 && !kp.angry) { launch_pieces(kp.b.pos, kp_up(), 10, 50.0f, 60.0f); kp_se(SE_OJ_STAR_PIECE_BURST); }
            }
            if (kp_act_end()) nv_set(n, KD_DOWNEND);
            break;
        case KD_DOWNEND: break;
        }
        break;
    case KST_JUMPAWAY:
        if (v == KJA_JUMPSTART) {
            if (st == 0) { kp_act("JumpAwayStart"); kp.b.vel = v3(0, 0, 0); }
            if (kp_act_end()) nv_set(n, KJA_JUMP);
        } else if (v == KJA_JUMP) {
            if (st == 0) { kp_act("JumpAway"); kp.b.vel = vmul(kp.b.front, -20.0f); kp_jump_add(35.0f); kp_se(SE_BM_KOOPA_JUMP); kp_se(SE_BV_KOOPA_JUMP); }
            kp_grav_add(1.0f);
            V3 h = vtangent(kp.b.vel, kp.b.grav);
            if (vlen(h) > 1e-3f) kp.b.front = vmul(vnorm(h), -1.0f);
            if (st != 0 && vdot(kp.b.vel, kp.b.grav) >= 0.0f && kp.b.on_ground) {
                kp_rumble(1, 1500.0f, 2000.0f, 2000.0f); kp_se(SE_BM_KOOPA_LAND); kp.b.vel = v3(0, 0, 0); nv_set(n, KJA_LAND);
            }
        } else {
            if (st == 0) kp_act("JumpAwayLand");
            if ((kp.vs == 3 && kp.lv == 3 && st > 30) || kp_act_end()) kst_kill(k); /* sLandStepVs3Lv3 */
        }
        break;
    case KST_JUMPTOPLANET:
        switch (v) {
        case KJP_START:
            if (st == 0) {
                kp_set_pos("戦闘開始（クッパ）"); kp_player_at("戦闘開始（マリオ）");
                kp_cam_start("落下（惑星までジャンプ）"); kp_act("JumpSoon");
                nv_set(n, KJP_FALL);
            }
            break;
        case KJP_FALL:
            if (st == 0) kp.shadow = 1;
            kp_to_player(&jfall);
            if (kp.vs == 3 && st == 90) { /* sFallWarpStepVs3 */
                kp_set_pos("Ｌｖ１開始（クッパ）"); kp_player_at("Ｌｖ１開始（マリオ）");
                kp_cam_end("落下（惑星までジャンプ）"); kp_cam_start("ワープ後（惑星までジャンプ）");
            }
            kp_lv(0, SE_BM_LV_KOOPA_FALL_TO_PLANET);
            if (kp.b.on_ground) { kp.b.vel = v3(0, 0, 0); nv_set(n, KJP_LAND); }
            break;
        case KJP_LAND:
            if (st == 0) { kp_act("Land"); kp_se(SE_BM_KOOPA_FLIP_RECOVER_END); kp_se(SE_BV_KOOPA_LAND_HEAVY); pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); }
            if (kp_act_end()) nv_set(n, KJP_WAITPLAYER);
            break;
        case KJP_WAITPLAYER:
            kp_to_player(&jwait);
            if (mario->grounded) kst_kill(k); /* isOnGroundPlayer */
            break;
        }
        break;
    case KST_SPIN: { /* KoopaStateAttackSpin */
        static const KpMove run = {3.0f, 1.0f, 0.9f, 3.0f}, brake = {1.0f, 1.0f, 0.9f, 0.0f}, end = {0.0f, 1.0f, 0.9f, 0.0f},
                            even = {0.0f, 1.0f, 0.95f, 0.5f}, spin2 = {3.0f, 1.0f, 0.9f, 0.5f};
        switch (v) {
        case KSP_WAITTOSTART:
            if (st == 0) { kp_act("Wait"); kp.b.vel = v3(0, 0, 0); }
            if (st == 30) nv_set(n, KSP_START); /* sWaitToStartStep */
            break;
        case KSP_RUN:
            if (st == 0) kp_act("Run");
            kp_to_player(&run); /* moveAndTurnKoopaToPlayer (its binded attack: none) */
            if (kp.b.on_wall) kp_jump_add(10.0f);
            if ((kp_near(1000.0f) || st > 120) && vdot(kp.b.vel, kp.b.grav) >= 0.0f) nv_set(n, KSP_START);
            break;
        case KSP_START:
            if (st == 0) { kp_act("SpinStart"); kp.b.vel = v3(0, 0, 0); }
            kp_turn_player(5.0f); /* sStartTurnSpeed */
            if (kp_act_end()) nv_set(n, KSP_SPIN);
            break;
        case KSP_SPIN:
            if (st == 0) { kp_act("Spin"); kp.b.vel = v3(0, 0, 0); }
            kp_to_player(st < 60 ? kp.sp_move : &brake); /* sSpinBrakeStep */
            if (kp.b.on_wall) kp_jump_add(15.0f); /* sWallJumpSpeed */
            if (kp_act_end()) {
                kp.sp_att++;
                if (!dm_damaging() && kp.sp_att < kp.sp_max) { kp.sp_move = &spin2; nv_set(n, KSP_START); } else nv_set(n, KSP_END);
            }
            break;
        case KSP_END:
            if (st == 0) kp_act("SpinEnd");
            kp_to_player(&end);
            if (kp_act_end()) { kp.b.vel = v3(0, 0, 0); kst_kill(k); }
            break;
        case KSP_EVEN:
            if (st == 0) { kp_act("SpinEven"); V3 h = vtangent(vsub(kp.b.pos, mario->pos), kp.b.grav); kp.b.vel = vmul(vlen(h) > 1e-6f ? vnorm(h) : v3(0, 0, 0), 30.0f); }
            kp_to_player(&even);
            if (kp_act_end()) {
                if (!dm_damaging() && kp.sp_att < kp.sp_max) { kp.sp_move = &spin2; nv_set(n, KSP_SPIN); } else kst_kill(k);
            }
            break;
        }
        break;
    }
    case KST_FIRELONG: { /* KoopaStateAttackFireLong */
        static const KpMove fall = {0.0f, 1.0f, 1.0f, 0.0f};
        if (v == KFL_START) {
            if (st == 0) { kp.b.vel = v3(0, 0, 0); kp_act("AttackFireStart"); }
            kp_to_player(&fall);
            if (kp_act_end()) { kp_act("AttackFire"); nv_set(n, KFL_EMIT); }
        } else {
            if (st == 0) kp.fl_n++;
            kp.b.front = rot_axis(kp.b.front, kp.b.grav, 3.0f * 0.0174533f); /* rotateVecDegree(front, gravity, sTurnSpeed) */
            kp_to_player(&fall);
            if (st == 0) kp_fire_short(0, 0, 1); /* emitKoopaFireLongTime */
            if (st == 15) { if (kp.fl_n >= 16) kst_kill(k); else nv_set(n, KFL_EMIT); } /* sStepEmit, sEmitNumMax */
        }
        break;
    }
    case KST_POWERUP:
        if (v == KP_WAITDEMO) {
            if (!kp_try_demo("パワーアップデモ", 1)) break;
            kp.calc_grav = 0;
            if (kp.vs != 3) {
                kp_set_pos("パワーアップデモ（クッパ）"); kp_player_at("パワーアップデモ（マリオ）");
                if (kp.lv == 3) { kp_act("DemoKoopaPowerUpFinal"); kp_cam_start("最終パワーアップデモ"); kp_sw_of("KoopaPowerUpSwitch", SW_B); }
                else { kp_act("DemoKoopaPowerUp"); kp_cam_start("パワーアップデモ"); kp_sw_of("KoopaPowerUpSwitch", SW_A); }
            } else {
                kp_set_pos("パワーアップデモＬｖ３（クッパ）"); kp_player_at("パワーアップデモＬｖ３（マリオ）");
                kp_act("DemoKoopaPowerUpFinal"); kp_cam_start("最終パワーアップデモ"); kp_sw_of("KoopaPowerUpSwitch", SW_A);
            }
            kp_recover_armor(); kp_recover_thorn();
            nv_set(n, KP_DEMO);
        } else {
            /* (startCenterScreenBlur at 65 / 50: ponytail) */
            if (!kp_act_end()) break;
            kp_cam_end(kp.vs == 3 || kp.lv == 3 ? "最終パワーアップデモ" : "パワーアップデモ");
            kp_end_demo("パワーアップデモ");
            kp.calc_grav = 1;
            launch_pieces(kp.b.pos, kp_up(), 10, 50.0f, 60.0f); /* appearStarPieceToDirection(-gravity, 10, 50, 60) */
            kp_se(SE_OJ_STAR_PIECE_BURST);
            kst_kill(k);
        }
        break;
    }
}
static int kst_run(int k) { if (k == KST_CHASEROLL || k == KST_ROLL) kr_update(k); else kst_update(k); return kp.sdead[k]; } /* update: its nerve, then mIsDead */
static int kst_run_once(int k, int first) /* MR::updateActorState(this, state): appear at the first step; kill if it ended alive */
{
    if (first) kst_appear(k);
    return kst_run(k);
}
static int kde_down_end(void) /* KoopaStateDamageEscape::isDownEnd */
{
    Nv *n = kst(KST_DAMAGEESCAPE);
    if (kp.vs != 3 && kp.lv == 3 && nv_is(n, KD_DOWN) && n->st > 45) return 1;
    if (kp.vs == 3 && kp.angry && nv_is(n, KD_DOWN) && ((kp.lv != 3 && n->st > 45) || (kp.lv == 3 && n->st > 20))) return 1;
    return nv_is(n, KD_DOWNEND);
}
static void kp_spin_hit_blow(void) /* KoopaFunction::startKoopaSpinHitBlow */
{
    pad_rumble(RUMBLE_VERY_STRONG); scene_stop = 10; /* stopSceneForDefaultHit(sKoopaHitBlowStopSceneStep) */
    se_start(SE_PM_SPIN_HIT, &kp.b.pos.x); /* startSpinHitSound / startBlowHitSound */
    kp_se(SE_BM_KOOPA_DAMAGE_S); kp_se(SE_BV_KOOPA_TAIL_RUN_START); se_start(SE_SY_VS_BOSS_DAMAGE_1, NULL);
}
static int kde_try_damage(int s) /* KoopaStateDamageEscape::tryDamage (Mario's spin on sensor s) */
{
    Nv *n = kst(KST_DAMAGEESCAPE);
    if (nv_is(n, KD_ESCAPERUN) || nv_is(n, KD_ESCAPEFIND) || nv_is(n, KD_ESCAPEEND)) {
        kp_spin_hit_blow(); bgm_state(3, 30); /* changeBgmStateDamageTail */
        nv_set(n, KD_TAILRUNSTART);
        return 1;
    }
    if (s != KS_DAMAGEESCAPEBODY) return 0;
    if (!((nv_is(n, KD_TAILRUNSTART) && n->st > 60) || nv_is(n, KD_TAILRUN) || (nv_is(n, KD_TAILRUNEND) && n->st < 60) ||
          nv_is(n, KD_TAILRUNFINAL) || (nv_is(n, KD_TAILRUNENDFINAL) && n->st < 60))) return 0;
    pad_rumble(RUMBLE_VERY_STRONG); se_start(SE_PM_SPIN_HIT, &kp.b.pos.x); kp_se(SE_BM_KOOPA_DAMAGE_L);
    if (kp.de_last && (nv_is(n, KD_TAILRUNSTART) || nv_is(n, KD_TAILRUN) || nv_is(n, KD_TAILRUNEND))) {
        static const KpMove final = {13.0f, 3.0f, 0.8f, 0.0f}; /* sDamageTailRunFinalParam */
        kp.de_tail = &final; kp.de_frames = 480; /* sDamageTailStepFinal */
        se_start(SE_SY_VS_BOSS_DAMAGE_2, NULL); kp_se(SE_BV_KOOPA_FLIP_DAMAGE_M); scene_stop = 10;
        nv_set(n, KD_TAILRUNSTARTFINAL);
        return 1;
    }
    bgm_state(4, 30); /* changeBgmStateFlyAway */
    if (kp.de_last && (nv_is(n, KD_TAILRUNFINAL) || nv_is(n, KD_TAILRUNENDFINAL))) {
        se_start(SE_SY_VS_BOSS_LAST_HIT, NULL); se_start(SE_SY_VS_KOOPA_LAST_HIT, NULL); kp_se(SE_BV_KOOPA_FLIP_DAMAGE_L);
    } else { se_start(SE_SY_VS_BOSS_DAMAGE_3, NULL); kp_se(SE_BV_KOOPA_FLIP_DAMAGE_M); }
    scene_stop = 20;
    nv_set(n, KD_DOWN);
    return 1;
}
static void kde_attack(int s) /* KoopaStateDamageEscape::attackSensor (Mario touching sensor s) */
{
    Nv *n = kst(KST_DAMAGEESCAPE);
    int run = nv_is(n, KD_ESCAPERUN) || (nv_is(n, KD_TAILRUN) && !(kp.vs == 1 && kp.lv == 1)) || nv_is(n, KD_TAILRUNFINAL);
    if (run && s == KS_DAMAGEESCAPEPUSH && mario_attack(ATK_FLIP_MAXIMUM, kp_sc(s))) return;
    if (kp_try_push(s)) return;
    if (nv_is(n, KD_ESCAPERUN)) kp_try_shell(s);
}
static int ksp_try_damage(int s) /* KoopaStateAttackSpin::tryDamage (a spin of Mario) */
{
    (void)s;
    Nv *n = kst(KST_SPIN);
    if (nv_is(n, KSP_EVEN)) return 0;
    mario_attack(ATK_FLIP_JUMP, kp.b.pos); /* sendMsgEnemyAttackFlipJump */
    pad_rumble(RUMBLE_STRONG);
    kp_se(SE_BM_KOOPA_SPIN_EVEN); kp_se(SE_BV_KOOPA_GUARD);
    scene_stop = 15; /* sEvenStopSceneStep */
    kp.sp_att++;
    if (kp.sp_max >= 2 && kp.sp_att >= kp.sp_max) { kst_kill(KST_SPIN); return 1; }
    nv_set(n, KSP_EVEN);
    return 1;
}
static void ksp_attack(int s) /* KoopaStateAttackSpin::attackSensor */
{
    if (kp_try_push(s)) return;
    Nv *n = kst(KST_SPIN);
    if (nv_is(n, KSP_SPIN) && n->st > 5 && n->st < 65 && kp_sdef[s].type == KPT_ATTACK) mario_attack(ATK_MAXIMUM, kp_sc(s)); /* tryKoopaAttackPlayerMaximum */
}
static void kh_attack(int s) /* KoopaStateAttackHipDrop::attackSensor */
{
    if (nv_is(kst(KST_HIPDROP), KH_JUMPATTACKDOWN) && s == KS_ATTACKHIPDROP && mario_attack(ATK_MAXIMUM, kp_sc(s))) return;
    if (kp_try_push(s)) return;
    kp_try_body_max(s);
}
static void kw_attack(int s) /* KoopaStateAttackShockWave::attackSensor */
{
    Nv *n = kst(KST_SHOCKWAVE);
    if ((nv_is(n, KW_JUMP) || nv_is(n, KW_JUMPFAST)) && s == KS_ATTACKHIPDROP && mario_attack(ATK_MAXIMUM, kp_sc(s))) return;
    if (kp_try_push(s)) return;
    kp_try_body(s);
}

/* --- KoopaBattleMain (its ActorStateKeeper: a nerve change kills the old state, the new one appears) ----------------- */
static int kbm_state(int nrv)
{
    switch (nrv) {
    case KBM_GUARD: return KST_GUARD; case KBM_HIPDROP: return KST_HIPDROP; case KBM_SHOCKWAVE: return KST_SHOCKWAVE;
    case KBM_FIRESHORT: return KST_FIRESHORT; case KBM_DAMAGEESCAPE: return KST_DAMAGEESCAPE; case KBM_JUMPAWAY: return KST_JUMPAWAY;
    case KBM_SPIN: return kp.vs != 1 ? KST_SPIN : -1; case KBM_FIRELONG: return kp.vs != 1 ? KST_FIRELONG : -1;
    case KBM_ROLL: return kp.vs != 1 ? KST_ROLL : -1;
    }
    return -1; /* (Vs1: no AttackSpin / AttackRoll / AttackFireLong) */
}
static int kbm_dead;
static void kbm_set(int v) { nv_set(&kp.main, v); }
static void kbm_appear(void) /* KoopaBattleMain::appear */
{
    kbm_dead = 0; kp.c2c = 1;
    if (kp.vs == 1) kbm_set(kp.lv == 1 ? KBM_SHOCKWAVE : KBM_FIRESHORT);
    else if (kp.vs == 2) kbm_set(kp.lv == 1 ? KBM_SHOCKWAVE : KBM_FIRELONG);
    else kbm_set(kp.angry ? KBM_FIRELONG : KBM_SHOCKWAVE);
}
static void kbm_main_attack(void) /* startMainAttack */
{
    if (!kp.c2c) { kp.c2c = 1; kbm_set(kp.vs == 2 ? KBM_SPIN : kp.vs == 3 ? KBM_ROLL : KBM_SHOCKWAVE); }
    else if (kp.vs == 1) kbm_set(kp.lv == 1 ? KBM_SHOCKWAVE : KBM_FIRESHORT);
    else if (kp.vs == 2) kbm_set(kp.lv == 1 ? KBM_FIRESHORT : KBM_FIRELONG);
    else kbm_set(kp.angry ? KBM_FIRELONG : KBM_SHOCKWAVE);
}
static int kbm_jump_away(void) /* tryJumpAway */
{
    if (kp_near(1500.0f)) { kbm_set(KBM_JUMPAWAY); return 1; } /* sDistanceMaxToJumpAway */
    kbm_set(KBM_CHASE);
    return 0;
}
static int kp_main_on(void) /* KoopaBattleMain is the sequencer's state (Vs3: Lv3, Final) */
{
    if (kp.vs == 3) return nv_is(&kp.seq, K3_LV3) || nv_is(&kp.seq, K3_FINAL);
    return nv_is(&kp.seq, KQ_BATTLE) && !nv_is(&kp.sub, KSS_DEMOLV2) && !nv_is(&kp.sub, KSS_DEMOLV3);
}
static void kbm_player_damage(void) /* receiveOtherMsg ACTMES_KOOPA_PLAYER_DAMAGE (only while battling) */
{
    if (kp.row < 0 || !kp_main_on() || kbm_dead) return;
    Nv *m = &kp.main;
    if (nv_is(m, KBM_ROLL)) return;
    if (!nv_is(m, KBM_HIPDROP) && !nv_is(m, KBM_SPIN) && !nv_is(m, KBM_SHOCKWAVE) && !nv_is(m, KBM_JUMPAWAY) && !nv_is(m, KBM_DAMAGEESCAPE)) kbm_jump_away();
}
static int kbm_update(int first) /* updateActorState(sub, battleMain): 1 when it is killed */
{
    if (first) kbm_appear();
    Nv *m = &kp.main;
    int prev = m->nrv, chg = m->next >= 0;
    int st = nv_run(m), v = m->nrv, sk = kbm_state(v), first_state = st == 0 && sk >= 0;
    if (chg) { int ok = kbm_state(prev); if (ok >= 0 && !kp.sdead[ok]) kst_kill(ok); } /* endState */
    if (first_state) kst_appear(sk); /* startState */
    switch (v) {
    case KBM_CHASE:
        kbb_chase(&kp_chase, st);
        if (kp.vs == 1 && (kp_near(500.0f) || (st > 60 && kp_sight(&kp_hip_sight)))) { kbm_set(KBM_HIPDROP); break; }
        if (st > 90) { kbm_main_attack(); break; } /* sChasePlayerStepToAttackFireShort */
        if (!kp_sight(&kp_chase_keep)) kbm_set(KBM_WANDER);
        break;
    case KBM_WANDER:
        if (kp_sight(&kp_chase_start)) { kbm_set(KBM_FIND); break; }
        if (kbb_wander(&kp_wander, st)) kbm_set(KBM_SEARCH);
        break;
    case KBM_SEARCH:
        if (st > 60) { /* sSearchStepToFind */
            if (kp.vs == 1 && kp_sight(&kp_hip_sight)) { kbm_set(KBM_HIPDROP); break; }
            if (kp_sight(&kp_chase_start)) { kbm_main_attack(); break; }
            if (kp_near(2000.0f)) { kbm_set(KBM_FIND); break; }
        }
        if (kbb_search(st)) kbm_set(KBM_SHOCKWAVE);
        break;
    case KBM_FIND: if (kbb_find(st)) kbm_set(KBM_CHASE); break;
    case KBM_JUMPAWAY:
        if (!kst_run(KST_JUMPAWAY)) break;
        kp.c2c = 0;
        if (kp.vs == 3 && kp.lv == 3) kr_set_1d(); /* mStateAttackRoll->_1D */
        kbm_main_attack();
        break;
    case KBM_GUARD:
        if (!kst_run(KST_GUARD)) break;
        kbm_set(kp.vs == 1 ? KBM_HIPDROP : kp.vs == 2 ? KBM_SPIN : KBM_ROLL);
        break;
    case KBM_FIRESHORT:
        if (!kst_run(KST_FIRESHORT)) break;
        kbm_set(kp.vs == 2 ? KBM_SPIN : KBM_SHOCKWAVE);
        break;
    case KBM_SHOCKWAVE:
        if (!kst_run(KST_SHOCKWAVE)) break;
        kbm_set(kp.vs == 1 ? KBM_WANDER : kp.vs == 2 ? KBM_SPIN : KBM_ROLL);
        break;
    case KBM_HIPDROP:
        if (kst_run(KST_HIPDROP)) { if (dm_damaging()) kbm_jump_away(); else kbm_main_attack(); }
        else if (nv_is(kst(KST_HIPDROP), KH_DAMAGE)) kbm_set(KBM_DAMAGEESCAPE);
        break;
    case KBM_DAMAGEESCAPE:
        if (kst_run(KST_DAMAGEESCAPE)) kbm_set(KBM_SEARCH);
        if (kde_down_end()) { kbb_kill(); kbm_dead = 1; }
        break;
    case KBM_SPIN:
        if (!kst_run(KST_SPIN)) break;
        if (dm_damaging()) kbm_jump_away(); else kbm_set(KBM_HIPDROP);
        break;
    case KBM_FIRELONG: if (kst_run(KST_FIRELONG)) kbm_set(KBM_SHOCKWAVE); break;
    case KBM_ROLL:
        if (!kst_run(KST_ROLL)) break;
        if (kr_hit_player()) kbm_jump_away(); else kbm_set(KBM_HIPDROP);
        break;
    }
    return kbm_dead;
}
static void kbm_attack(int s) /* KoopaBattleMain::attackSensor (Mario touching Koopa's sensor s) */
{
    Nv *m = &kp.main;
    if (nv_is(m, KBM_HIPDROP)) { kh_attack(s); return; }
    if (nv_is(m, KBM_SHOCKWAVE)) { kw_attack(s); return; }
    if (nv_is(m, KBM_JUMPAWAY)) { /* tryKoopaAttackMapObj: not the player */ }
    if (nv_is(m, KBM_ROLL)) { if (!kr_attack(KST_ROLL, s)) kp_try_push(s); return; }
    if (nv_is(m, KBM_SPIN)) { ksp_attack(s); return; }
    if (nv_is(m, KBM_DAMAGEESCAPE)) { kde_attack(s); return; }
    if (!kp_try_push(s)) kp_try_body(s);
}
static int kbm_receive(int s) /* KoopaBattleMain::receiveMsgPlayerAttack (Mario's spin on sensor s) */
{
    Nv *m = &kp.main;
    if (nv_is(m, KBM_DAMAGEESCAPE)) return kde_try_damage(s);
    if (nv_is(m, KBM_SPIN)) {
        if (!ksp_try_damage(s)) return 0;
        if (kp.sdead[KST_SPIN]) kbm_set(KBM_HIPDROP);
        return 1;
    }
    int ok = nv_is(m, KBM_CHASE) || nv_is(m, KBM_WANDER) || nv_is(m, KBM_SEARCH) || nv_is(m, KBM_FIND) || nv_is(m, KBM_JUMPAWAY) ||
             nv_is(m, KBM_FIRESHORT) || nv_is(m, KBM_FIRELONG) || (nv_is(m, KBM_ROLL) && kr_guardable(KST_ROLL)) || (nv_is(m, KBM_HIPDROP) && kst_hip_guardable()) || nv_is(m, KBM_SHOCKWAVE);
    if (ok && kst_guard_try(s)) { kbm_set(KBM_GUARD); return 1; }
    return 0; /* (tryKoopaReflectStarPiece: no star piece shots here) */
}

/* --- KoopaSubSequenceBattle ------------------------------------------------------------------------------------------ */
static int ksub_update(void) /* 1: killed (offSwitchB) */
{
    Nv *n = &kp.sub;
    int st = nv_run(n);
    switch (n->nrv) {
    case KSS_LV1: if (kbm_update(st == 0)) nv_set(n, KSS_DEMOLV2); break;
    case KSS_DEMOLV2: case KSS_DEMOLV3:
        if (st == 0) { kp.lv = n->nrv == KSS_DEMOLV2 ? 2 : 3; kp.angry = 0; printf("koopa: battle level %d\n", kp.lv); } /* startBattleLevel2 / 3 */
        if (kst_run_once(KST_POWERUP, st == 0)) nv_set(n, n->nrv == KSS_DEMOLV2 ? KSS_LV2 : KSS_LV3);
        break;
    case KSS_LV2: if (kbm_update(st == 0)) nv_set(n, KSS_DEMOLV3); break;
    case KSS_LV3:
        if (kbm_update(st == 0)) { const Row *r = &sc->objs[kp.row]; if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0); return 1; }
        break;
    }
    return 0;
}

/* --- KoopaBattleStairsVs1 ------------------------------------------------------------------------------------------- */
static const char *const kstb_pos[4] = {"階段の戦い０（クッパ）", "階段の戦い１（クッパ）", "階段の戦い２（クッパ）", "デモ中心"};
static int kstb_is_type(int k, int t) { int a = sc->objs[kss[k].row].arg[2]; return (a == -1 ? 0 : a) == t; } /* mType (arg2, default Normal) */
static int kstb_arg(int k, int a, int def) { int v = sc->objs[kss[k].row].arg[a]; return v == -1 ? def : v; }
static int kstb_request_vs1(int k) { return kstb_is_type(k, 0) && kss[k].nrv == KS_WAITFIRE && kss[k].step == kss[k].fstep; } /* isRequestAttackVs1 */
static int kstb_try_attack(void)
{
    int ok = 0;
    for (int k = 0; k < nkss; k++) if (kss[k].reg && kstb_request_vs1(k)) { ok = 1; kp.req = k; kp.avail++; }
    return ok;
}
static int kstb_next_pos_requested(void) /* tryRequestedToMoveNextPos */
{
    for (int k = 0; k < nkss; k++) {
        if (!kss[k].reg || !kstb_request_vs1(k)) continue;
        if (kstb_arg(k, 1, 0) > kp.jump_idx) return 1;
        if (kstb_arg(k, 6, 0)) kp.can_jump = 1;
    }
    return 0;
}
static int kstb_emit(int k) { return kp_fire_stairs(k, kp_joint(KJ_TONGUE2)); } /* emitFireStairs (from Tongue2) */
static void kstb_register(void) /* KoopaBattleMapStair::initAfterPlacement: registerStair (calcFireAttackStep) */
{
    float b[16];
    V3 save_p = kp.b.pos, save_f = kp.b.front, save_g = kp.b.grav;
    for (int j = 0; j < 3; j++) { kp_set_pos(kstb_pos[j]); kp_base(b); kp.jp[j] = kp_joint(KJ_TONGUE2); } /* calcFireStartPos (its pose then) */
    kp.b.pos = save_p; kp.b.front = save_f; kp.b.grav = save_g;
    kp.far = kp.nearr = -1; kp.n4c = kp.n50 = kp.n54 = 0;
    for (int i = 0; i < ninsts; i++) if (insts[i].kstair) ks_of(insts[i].row);
    for (int k = 0; k < nkss; k++) {
        const Row *r = &sc->objs[kss[k].row];
        if (!kss[k].init) { kss[k].init = 1; kss[k].pos = row_pos(r); ks_set(k, KS_WAITSWITCH); }
        if (kstb_is_type(k, 1)) continue; /* isTypeNoRequestFire */
        kss[k].reg = 1;
        if (kstb_is_type(k, 2)) kp.far = k;
        if (kstb_is_type(k, 3)) kp.nearr = k;
        int a1 = kstb_arg(k, 1, 0), j = a1 == 1 ? 1 : a1 == 2 ? 2 : 0;
        if (kstb_is_type(k, 0)) { if (j == 1) kp.n50++; else if (j == 2) kp.n54++; else kp.n4c++; }
        V3 t; float d;
        kp_stair_target(k, kp.jp[j], &t, &d);
        kss[k].a8 = (int)(d / 20.0f) - (int)(90.0f / 20.0f); /* sFireSpeed, sFireRadius */
        kss[k].fstep = kp_stair_break_time(k) - kss[k].a8 - 15; /* sStepToEmitFire */
    }
    printf("koopa: %d stairs registered (far %d, near %d; %d %d %d)\n", nkss, kp.far, kp.nearr, kp.n4c, kp.n50, kp.n54);
}
static void kstb_update(void)
{
    Nv *n = &kp.stairs;
    int st = nv_run(n);
    switch (n->nrv) {
    case KSB_WAITDEMO:
        if (kp_try_demo("階段の戦い開始デモ", 0)) {
            kp_acam("DemoBattleStairsStart"); /* startKoopaAnimCamera (sAnimCameraInterpoleFrame 60) */
            kp_act("DemoBattleStairsStart");
            bgm_stop(60);
            nv_set(n, KSB_DEMO);
        }
        break;
    case KSB_DEMO:
        if (st == 264) { kstb_emit(kp.far); if (kp.far >= 0) kss[kp.far].mov = 1; kp_se(SE_OJ_KOOPA_BULLET_SHOOT); }  /* requestMovementOn: they move in the demo */
        if (st == 292) { kstb_emit(kp.nearr); if (kp.nearr >= 0) kss[kp.nearr].mov = 1; kp_se(SE_OJ_KOOPA_BULLET_SHOOT); }
        if (st > 0 && kp_act_end()) {
            kp_acam(NULL);
            kp_end_demo("階段の戦い開始デモ");
            kp_sw_of("KoopaSwitchKeeper", SW_B);
            bgm_start("MBGM_GALAXY_15_HURRY");
            nv_set(n, KSB_WAIT);
        }
        break;
    case KSB_WAIT:
        if (st == 0) kp_act("Wait");
        if (kstb_try_attack()) { kp_act("AttackFire"); nv_set(n, KSB_ATTACKFIRE); }
        break;
    case KSB_ATTACKFIRE: {
        if (kstb_next_pos_requested()) { kp.req = -1; nv_set(n, KSB_JUMPSTART); break; }
        if (st == 15) { kstb_emit(kp.req); kp.req = -1; } /* sStepToEmitFire */
        int req = kp.req;
        if (st < 15 && kstb_try_attack()) { kstb_emit(req); nv_set(n, KSB_ATTACKFIRE); break; }
        if (st >= 15 && kstb_try_attack()) { nv_set(n, KSB_ATTACKFIRE); break; }
        if (st > 33) { /* sFireAttackStep */
            if (kp.can_jump || (kp.jump_idx == 0 && kp.avail >= kp.n4c) || (kp.jump_idx == 1 && kp.avail >= kp.n4c + kp.n50) ||
                (kp.jump_idx == 2 && kp.avail >= kp.n4c + kp.n50 + kp.n54)) { kp.req = -1; nv_set(n, KSB_JUMPSTART); break; }
            nv_set(n, KSB_WAIT);
        }
        break;
    }
    case KSB_JUMPSTART:
        if (st == 0) { kp_act("JumpStartFast"); kp_se(SE_BM_KOOPA_JUMP); kp.can_jump = 0; }
        if (kp_act_end()) nv_set(n, KSB_JUMPLOOP);
        break;
    case KSB_JUMPLOOP: {
        if (st == 0) {
            float m[16];
            kp_act("JumpFast");
            kp.jold = kp.b.pos;
            kp.jump_idx++;
            if (kp_gpos(kstb_pos[kp.jump_idx < 3 ? kp.jump_idx : 3], m)) { kp.jnew = v3(m[12], m[13], m[14]); kp.jdir = v3(m[8], m[9], m[10]); } /* getRotatedAxisZ */
        }
        float t = st / 60.0f, e = (1.0f - cosf(t * 3.1415927f)) / 2.0f; /* getEaseInOutValue */
        kp.b.pos = vadd(vmul(kp.jold, 1.0f - e), vmul(kp.jnew, e));
        kp.b.pos.y += 1000.0f * sinf(180.0f * t * 0.0174533f); /* sJumpHeight */
        turn_on_plane(&kp.b.front, kp.jdir, kp.b.grav, cosf(2.0f * 0.0174533f)); /* turnDirectionDegree(sTurnSpeed) */
        if (st == 60) { pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL); nv_set(n, KSB_JUMPEND); } /* sStepJumpToNextPosLoop */
        break;
    }
    case KSB_JUMPEND:
        if (st == 0) { kp_act("LandFast"); kp_se(SE_BM_KOOPA_LAND); }
        if (kstb_try_attack()) { kp_set_pos(kstb_pos[kp.jump_idx == 1 ? 1 : 2]); nv_set(n, KSB_ATTACKFIRE); break; }
        if (kp_act_end()) { kp_set_pos(kstb_pos[kp.jump_idx < 3 ? kp.jump_idx : 3]); nv_set(n, KSB_WAIT); }
        break;
    }
}
static void kstb_end(void) /* KoopaBattleStairsBase::end: the stairs dead, the fires killed */
{
    for (int k = 0; k < nkss; k++) row_kill(kss[k].row);
    kp_fire_stairs_kill();
    game_rebind();
}

/* --- KoopaSequencerVs1 ------------------------------------------------------------------------------------------- */
static void kq_update(void)
{
    Nv *n = &kp.seq;
    int st = nv_run(n);
    const Row *r = &sc->objs[kp.row];
    switch (n->nrv) {
    case KQ_BATTLESTAIRS:
        if (st == 0) kp_set_pos(kstb_pos[0]);
        kstb_update();
        if (r->sw[SW_A] >= 0 && sw_on(r, SW_A)) {
            kstb_end();
            nv_set(n, KQ_WAITDEMO);
            kp_set_pos("デモ中心"); kp_player_at("デモ中心");
            kp_dx_request("クッパＶｓ１本戦開始");
            render_cover(2);
            printf("koopa: stairs battle over\n");
        }
        break;
    case KQ_WAITDEMO:
        if (kp_dx_on()) nv_set(n, !strcmp(demos[kp.dx_k].h->name, "クッパＶｓ１ダウン") ? KQ_DEMOBATTLEEND : KQ_DEMOBATTLESTART); /* the demo started: its nerve */
        break;
    case KQ_DEMOBATTLESTART:
        if (st == 0) { kp.calc_grav = 0; kp_act("DemoKoopaVs1Start"); kp_acam("DemoKoopaVs1Start"); kp_shadow_demo = 1; } /* initDemoSheetTalkAnim */
        if (kp_dx_last() || !dx.on) {
            kp_acam(NULL); kp.calc_grav = 1; kp_shadow_demo = 0;
            const Row *rr = &sc->objs[kp.row];
            if (rr->sw[SW_B] >= 0) sw_set(rr, SW_B, 1); /* mJumpToPlanet->startReady: onSwitchB */
            nv_set(n, KQ_DEMOJUMPTOPLANET);
        }
        break;
    case KQ_DEMOJUMPTOPLANET:
        if (kst_run_once(KST_JUMPTOPLANET, st == 0)) { nv_set(n, KQ_BATTLE); printf("koopa: battle\n"); }
        break;
    case KQ_BATTLE:
        if (st == 0) nv_set(&kp.sub, KSS_LV1); /* updateActorState: appear (its nerve as made) */
        if (ksub_update()) nv_set(n, KQ_WAITDEMOBATTLEEND);
        break;
    case KQ_WAITDEMOBATTLEEND:
        if (st == 0) {
            kp.shadow = 0;
            kp_set_pos("デモ中心");
            nv_set(n, KQ_WAITDEMO);
            kp_dx_request("クッパＶｓ１ダウン");
        }
        break;
    case KQ_DEMOBATTLEEND:
        if (st == 0) {
            kp_se(SE_AT_KOOPA_FALLDOWN_LV1); bgm_stop(180);
            kp.calc_grav = 0; kp_act("DemoKoopaVs1Down"); kp_acam("DemoKoopaVs1Down");
            /* startBrk(planet, Death): ponytail (its BRK) */
        }
        if (st == 20) kp_se(SE_BV_KOOPA_BATTLE_END_FALL); /* sKoopaFallVoiceStep */
        if (kp_dx_last() || !dx.on) {
            kp_acam(NULL);
            render_cover(3);
            const float *g = scene_gpos("デモ中心");
            bgm_after_boss();
            note_star_row = kp.row; note_star_at = g ? v3(g[0], g[1], g[2]) : kp.b.pos; /* requestAppearPowerStar(デモ中心) */
            kp_star_demo = 0; /* startKoopaPlateDamageAnimPowerStarAppear */
            if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1); /* kill: onSwitchDead */
            row_kill(kp.row);
            kp.alive = 0;
            printf("koopa: down, its grand star\n");
        }
        break;
    }
    if (n->nrv == KQ_DEMOBATTLESTART || n->nrv == KQ_DEMOBATTLEEND) kp.dx_f++;
}
/* KoopaSequencerVs2: no stairs; the opening sheet at once (tryStartTimeKeepDemoMarioPuppetable), the down sheet with
 * DemoKoopaVs1Down, Mario at ダウンデモ後（マリオ） after it */
enum { KQ2_WAITDEMOBATTLESTART, KQ2_DEMOBATTLESTART, KQ2_DEMOJUMPTOPLANET, KQ2_BATTLE, KQ2_WAITDEMODOWN, KQ2_DEMODOWN, KQ2_WAITDEMO };
static void kq2_update(void)
{
    Nv *n = &kp.seq;
    int st = nv_run(n);
    const Row *r = &sc->objs[kp.row];
    switch (n->nrv) {
    case KQ2_WAITDEMOBATTLESTART:
        if (st == 0) { kp_set_pos("デモ中心"); kp_dx_request("クッパＶｓ２本戦開始"); }
        if (kp_dx_on()) nv_set(n, KQ2_DEMOBATTLESTART);
        break;
    case KQ2_DEMOBATTLESTART:
        if (st == 0) { kp.calc_grav = 0; kp_act("DemoKoopaVs2Start"); kp_acam("DemoKoopaVs2Start"); } /* initDemoSheetTalkAnim */
        if (kp_dx_last() || !dx.on) {
            kp_acam(NULL); kp.calc_grav = 1;
            if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1); /* startReady */
            nv_set(n, KQ2_DEMOJUMPTOPLANET);
        }
        break;
    case KQ2_DEMOJUMPTOPLANET:
        if (kst_run_once(KST_JUMPTOPLANET, st == 0)) { nv_set(n, KQ2_BATTLE); printf("koopa: battle\n"); }
        break;
    case KQ2_BATTLE:
        if (st == 0) nv_set(&kp.sub, KSS_LV1);
        if (ksub_update()) nv_set(n, KQ2_WAITDEMODOWN);
        break;
    case KQ2_WAITDEMODOWN:
        if (st == 0) { kp_set_pos("デモ中心"); nv_set(n, KQ2_WAITDEMO); kp_dx_request("クッパＶｓ２ダウン"); }
        break;
    case KQ2_WAITDEMO:
        if (kp_dx_on()) nv_set(n, KQ2_DEMODOWN);
        break;
    case KQ2_DEMODOWN:
        if (st == 0) { kp_se(SE_AT_KOOPA_FALLDOWN_LV1); bgm_stop(180); kp.calc_grav = 0; kp_act("DemoKoopaVs1Down"); kp_acam("DemoKoopaVs1Down"); }
        if (st == 20) kp_se(SE_BV_KOOPA_BATTLE_END_FALL);
        if (kp_dx_last() || !dx.on) {
            kp_acam(NULL); render_cover(3);
            kp_player_at("ダウンデモ後（マリオ）");
            const float *g = scene_gpos("デモ中心");
            bgm_after_boss();
            note_star_row = kp.row; note_star_at = g ? v3(g[0], g[1], g[2]) : kp.b.pos;
            kp_star_demo = 0;
            if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
            row_kill(kp.row); kp.alive = 0;
            printf("koopa: down, its grand star\n");
        }
        break;
    }
}

/* --- Vs3 --------------------------------------------------------------------------------------------------------- */
/* KoopaParts::initVs3: the three planets, the holed sun (outside / inside), the demo casts; createRock / RollBall */
enum { K3P_LV1, K3P_LV2, K3P_LV3, K3P_SUNOUT, K3P_SUNIN, K3P_N };
static const char *const k3p_model[K3P_N] = {"KoopaBattleMapPlanetVs3Lv1", "KoopaBattleMapPlanetVs3Lv2", "KoopaBattleMapPlanetVs3Lv3", "KoopaVS3HoleSunPlanet", "KoopaVS3HoleSunInsidePlanet"};
static const char *const k3p_pos[K3P_N] = {"惑星中心", "惑星Ｌｖ２", "惑星Ｌｖ３", "惑星Ｌｖ３", "惑星Ｌｖ３"};
static struct { int ok, dead, hidden, ki; float m[16]; V3 c; } k3p[K3P_N];
enum { KPR_ROCK, KPR_ROCKBREAK, KPR_ROLLBALL, KPR_PEACH, KPR_JR, KPR_SHIP, KPR_N };
static const char *const kpr_model[KPR_N] = {"KoopaRock", "KoopaRockBreak", "KoopaRollBall", "Peach", "KoopaJr", "KoopaJrShip"};
static struct { const Model *m; Anim *a; int on, own; float mtx[16]; } kpr[KPR_N];
static struct {
    Nv bat, fall;                 /* KoopaBattleVs3Lv1 / Lv2, KoopaDemoFallToPlanetVs3Lv2 / Lv3 */
    int bdead, fdead;
    int delay[2], roll_time, r1c, r1d; /* ChaseRoll / AttackRoll: mRollDelay, mRollTime, _1C, _1D */
    u8 a6[MAX_STAIRS];            /* KoopaBattleMapStair::_A6 (fired at) */
    int casts;                    /* the demo casts appeared */
    int restarter;                /* KoopaRestarterVs3 alive (syncStageSwitchAppear) */
} k3;
static struct { int on; Kuribo b; float ang, r; const KpMove *mp; } kfb; /* KoopaFigureBall (b.pos: the sphere's bottom) */
static void k3p_hide(int p, int h) { k3p[p].hidden = h; if (k3p[p].ki >= 0 && k3p[p].ki < ninsts) insts[k3p[p].ki].invisible = h || k3p[p].dead; }
static void k3p_kill(int p, int d) /* kill / appear: its collision with it */
{
    k3p[p].dead = d;
    if (k3p[p].ki < 0 || k3p[p].ki >= ninsts) return;
    insts[k3p[p].ki].invisible = d || k3p[p].hidden; insts[k3p[p].ki].kcl = d ? NULL : scene_kcl(sc, k3p_model[p]);
}
static void k3_insts(const Scene *s, int host) /* game_rebind_: the planets and suns */
{
    for (int p = 0; p < K3P_N; p++) {
        k3p[p].ki = -1;
        const Model *md = k3p[p].ok ? scene_model(s, k3p_model[p]) : NULL;
        if (!md || ninsts >= (int)s->nobjs) continue;
        Inst *in = &insts[ninsts];
        memset(in, 0, sizeof *in);
        in->md = md; in->row = host; in->zone = s->objs[kp.row].zone; in->kmove = in->kparent = -1;
        memcpy(in->m, k3p[p].m, 64); in->s = 1.0f; in->reach = md->radius;
        in->kcl = k3p[p].dead ? NULL : scene_kcl(s, k3p_model[p]); in->invisible = k3p[p].dead || k3p[p].hidden;
        zone_grow(in);
        k3p[p].ki = ninsts++;
    }
}
static void k3_planets_init(void) /* initVs3: createKoopaBattleMapPlanet x3 (Lv3 with its damage plates), the holed suns */
{
    float one[3] = {1, 1, 1};
    for (int p = 0; p < K3P_N; p++) {
        const float *g = scene_gpos(k3p_pos[p]);
        k3p[p].ok = g != NULL; k3p[p].dead = p == K3P_SUNIN; k3p[p].hidden = 0; k3p[p].ki = -1; /* mHoleSunPlanetInside->kill() */
        if (g) { mat_translate_rot_scale(k3p[p].m, g, g + 3, one); k3p[p].c = v3(g[0], g[1], g[2]); }
    }
}
static void k3_pc(int p) { if (k3p[p].ok) kp_pc = k3p[p].c; } /* KoopaParts::getPlanetPos (by level): the shadow's centre */
static void kpr_act(int p, const char *clip) /* startAction on a part */
{
    char n[48];
    snprintf(n, sizeof n, "%s/%s", kpr_model[p], clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    n[31] = 0;
    Anim *a = kpr[p].a;
    if (!a || !kpr[p].m || !kpr[p].m->skin) return;
    if (a->skin != kpr[p].m->skin) anim_bind(a, kpr[p].m->skin);
    a->clip_id = -1; anim_start(a, n, 0); a->speed = 1.0f;
}
static int kpr_end(int p) { Anim *a = kpr[p].a; return !a || !a->skin || a->done; }
static void kpr_appear(int p, int own) /* appear; mIsCalcOwnMtx: its matrix taken from Koopa's now */
{
    kpr[p].on = 1; kpr[p].own = own;
    if (own) kp_base(kpr[p].mtx);
}
static void k3_casts_act(const char *clip) { for (int p = KPR_PEACH; p <= KPR_SHIP; p++) { kpr[p].on = 1; kpr_act(p, clip); } k3.casts = 1; }
/* KoopaFigureBall: rolls to the player on its binder (radius r, no offset), its angle from the speed along its front */
static V3 kfb_c(void) { return vadd(kfb.b.pos, vmul(kfb.b.grav, -kfb.r)); }
static void kfb_appear(float r, const KpMove *mp) /* makeActorAppeared: at Koopa, its velocity and front */
{
    memset(&kfb.b, 0, sizeof kfb.b);
    kfb.on = 1; kfb.r = r; kfb.mp = mp; kfb.ang = 0.0f;
    kfb.b.row = kp.b.row; kfb.b.front = kp.b.front; kfb.b.vel = kp.b.vel;
    kfb.b.grav = vmul(up_at(kp.b.pos, kp_up()), -1.0f); /* calcGravity */
    kfb.b.pos = vadd(kp.b.pos, vmul(kfb.b.grav, r));
}
static void kfb_kill(void) { if (kfb.on) kp.b.front = kfb.b.front; kfb.on = 0; } /* makeActorDead: Koopa's front its own */
static void kfb_movement(void) /* LiveActor::movement: control, then the velocity and the binder */
{
    V3 c = kfb_c();
    kfb.b.grav = vmul(up_at(c, vmul(kfb.b.grav, -1.0f)), -1.0f);
    w_move_turn(&kfb.b, vsub(mario->pos, c), kfb.mp->a, kfb.mp->g, kfb.mp->f, kfb.mp->t); /* moveAndTurnToPlayer */
    if (kfb.b.on_wall) kfb.b.vel = vadd(kfb.b.vel, vmul(kfb.b.grav, -15.0f)); /* sWallJumpSpeed */
    kfb.ang += vdot(kfb.b.vel, kfb.b.front) * 180.0f / (kfb.r * 3.1415927f);
    basis_matrix(kp_bm, c, vmul(kfb.b.grav, -1.0f), kfb.b.front, 0); /* makeMtxUpFrontPos(-g, front, pos + g r) + y r */
    float ca = cosf(kfb.ang * 0.0174533f), sa = sinf(kfb.ang * 0.0174533f), y[3] = {kp_bm[4], kp_bm[5], kp_bm[6]}, z[3] = {kp_bm[8], kp_bm[9], kp_bm[10]};
    for (int i = 0; i < 3; i++) { kp_bm[4 + i] = y[i] * ca + z[i] * sa; kp_bm[8 + i] = z[i] * ca - y[i] * sa; } /* concat(rotate X angle) */
    w_bind(&kfb.b, kfb.r);
}
/* KoopaStateChaseRoll (k KST_CHASEROLL) / KoopaStateAttackRoll (KST_ROLL) */
static void kr_appear(int k)
{
    static const KpMove chase = {1.1f, 2.0f, 0.95f, 1.5f}, chase_fast = {1.5f, 2.0f, 0.95f, 1.5f}, lv2s = {3.0f, 2.0f, 0.9f, 1.0f},
                        lv2f = {4.5f, 2.0f, 0.9f, 1.0f}, lv3s = {8.0f, 2.0f, 0.8f, 0.5f}, lv3f = {10.0f, 2.0f, 0.8f, 1.0f};
    kp.b.vel = v3(0, 0, 0);
    int c = k == KST_CHASEROLL;
    if (c) {
        kp_sensor(KS_CHASEROLLFACE, 1); kp_sensor(KS_CHASEROLLATTACK, 1); kp_sensor(KS_CHASEROLLSTARPIECE, 1);
        kfb.mp = kp.angry ? &chase_fast : &chase; k3.delay[1] = 60; /* sWaitToStartStep(Angry) */
    } else {
        kp_sensor(KS_ATTACKROLL, 1); kp_sensor(KS_ATTACKROLLRECEIVER, 1); kp_sensor(KS_ATTACKROLLSTARPIECE, 1);
        k3.r1c = 0;
        if (kp.lv == 2) { k3.delay[0] = 30; kfb.mp = kp.angry ? &lv2f : &lv2s; k3.roll_time = kp.angry ? 300 : 600; }
        else {
            if (kp.angry) { kfb.mp = &lv3f; k3.roll_time = 320; k3.delay[0] = 75; } else { kfb.mp = &lv3s; k3.roll_time = 280; k3.delay[0] = 90; }
            if (k3.r1d) { k3.r1d = 0; k3.delay[0] = 0; }
        }
    }
    nv_set(kst(k), k3.delay[c] >= 0 ? KR_WAITTOSTART : KR_START);
}
static void kr_kill(int k)
{
    if (k == KST_CHASEROLL) {
        kp_sensor(KS_CHASEROLLFACE, 0); kp_sensor(KS_CHASEROLLATTACK, 0); kp_sensor(KS_CHASEROLLSTARPIECE, 0);
        kpr[KPR_ROCK].on = 0; /* tryDeleteEffect RollingSmoke, kill */
        kp_cam_end("ロール追跡開始");
    } else {
        kpr[KPR_ROLLBALL].on = 0;
        kp_sensor(KS_ATTACKROLL, 0); kp_sensor(KS_ATTACKROLLRECEIVER, 0); kp_sensor(KS_ATTACKROLLSTARPIECE, 0);
        kp_cam_end("ローリング攻撃開始"); kp_cam_end("ローリング攻撃");
    }
    kp_ballm = 0;
}
static void kr_update(int k)
{
    static const KpMove start = {0.0f, 0.5f, 1.0f, 3.0f}, end_c = {0.0f, 1.0f, 0.98f, 1.0f}, end_r = {0.0f, 1.0f, 0.95f, 1.0f};
    Nv *n = kst(k);
    int st = nv_run(n), c = k == KST_CHASEROLL, part = c ? KPR_ROCK : KPR_ROLLBALL;
    kp_ballm = 0;
    switch (n->nrv) {
    case KR_WAITTOSTART:
        if (st == 0) kp_act("Wait");
        if (st == k3.delay[c]) nv_set(n, KR_START);
        break;
    case KR_START:
        if (st == 0) {
            kpr_appear(part, 0);
            if (c) { kpr_appear(KPR_ROCKBREAK, 1); kp_act("ChaseRollStart"); kpr_act(KPR_ROCK, "AttackRollStart"); kpr_act(KPR_ROCKBREAK, "AttackRollStart"); kp_cam_start("ロール追跡開始"); }
            else { kp_act("AttackRollStart"); kpr_act(KPR_ROLLBALL, "AttackRollStart"); kp_cam_start("ローリング攻撃開始"); }
        }
        if (c) kp_lv(2, SE_BM_LV_KOOPA_ROCK_GATHER);
        if (st == 24) kp_jump_set(c ? 30.0f : 20.0f); /* sStartStepToJump, sStartJumpSpeed */
        if (st > 24) kp_to_player(&start);
        if (kp_act_end()) { kfb_appear(c ? 260.0f : 230.0f, kfb.mp); nv_set(n, KR_ROLLAIR); }
        else if (c && dm_damaging()) nv_set(n, KR_ENDAIR);
        break;
    case KR_ROLLAIR:
        if (st == 0) { kp_act(c ? "ChaseRoll" : "AttackRoll"); kpr_act(part, "AttackRoll"); }
        kfb.ang += 5.0f; /* sRotateSpeed */
        kfb_movement(); kp.b.pos = kfb_c(); kp_ballm = 1;
        if (!c) kp_lv(2, SE_BM_LV_KOOPA_ATTACK_ROLL);
        if (kfb.b.on_ground) nv_set(n, KR_ROLLGROUND);
        else if (dm_damaging()) nv_set(n, KR_ENDAIR);
        break;
    case KR_ROLLGROUND:
        if (st == 0) { if (c) kp_cam_end("ロール追跡開始"); else kp_cam_start("ローリング攻撃"); } /* (RollingSmoke: its effect) */
        if (st == 0 && c) { float m[16]; kp_base(m); fx_named("KoopaRock", "RollingSmoke", m, NULL, &kpr[KPR_ROCK]); }
        kfb_movement(); kp.b.pos = kfb_c(); kp_ballm = 1;
        /* sendMsgEnemyAttackToBindedSensor: ponytail (no binded sensors, as the header says) */
        if (!c) kp_lv(2, SE_BM_LV_KOOPA_ATTACK_ROLL);
        kp_lv(1, SE_BM_LV_KOOPA_CHACE_ROLL);
        if (c ? (dm_damaging() || st > 900) : (st > k3.roll_time || dm_damaging())) { /* sStepRollGround / mRollTime */
            kfb_kill(); kp_ballm = 0;
            if (!c && !dm_damaging() && kp.vs == 3 && kp.lv == 3) kst_kill(k); else nv_set(n, KR_ENDAIR);
        }
        break;
    case KR_ENDAIR:
        if (st == 0) {
            kp_jump_set(30.0f); /* sEndJumpSpeed */
            if (c) {
                kp_act("ChaseRollEnd"); kpr_act(KPR_ROCK, "AttackRollEnd");
                kpr_appear(KPR_ROCKBREAK, 1); kpr_act(KPR_ROCKBREAK, "AttackRollEnd"); kp_se(SE_BM_KOOPA_ROCK_BREAK);
            } else { kp_act("AttackRollEnd"); kpr_act(KPR_ROLLBALL, "AttackRollEnd"); kp_cam_end("ローリング攻撃"); }
        }
        kp_to_player(c ? &end_c : &end_r);
        if (st != 0 && kp.b.on_ground) { kp.b.vel = v3(0, 0, 0); nv_set(n, KR_ENDLAND); }
        break;
    case KR_ENDLAND:
        if (st == 0) kp_act(c ? "ChaseRollLand" : "AttackRollLand");
        if (kp_act_end()) kst_kill(k);
        break;
    }
}
static int kr_attack(int k, int s) /* attackSensor: its roll sensor on the player (EnemyAttackMaximum) */
{
    if (s != (k == KST_CHASEROLL ? KS_CHASEROLLATTACK : KS_ATTACKROLL) || !kp_try_body_max(s)) return 0;
    if (k == KST_ROLL) k3.r1c = 1;
    return 1;
}
static int kr_guardable(int k) { Nv *n = kst(k); return nv_is(n, KR_WAITTOSTART) || nv_is(n, KR_ENDAIR) || nv_is(n, KR_ENDLAND); } /* isEnableGuard */
static int kr_try_damage(int s) /* KoopaStateChaseRoll::tryDamage: a spin on its face while it rolls on the ground breaks the rock */
{
    if (s != KS_CHASEROLLFACE || !nv_is(kst(KST_CHASEROLL), KR_ROLLGROUND)) return 0;
    kpr_appear(KPR_ROCKBREAK, 1); kpr_act(KPR_ROCKBREAK, "Break"); kp_se(SE_BM_KOOPA_ROCK_BREAK);
    return 1;
}

static void kr_set_1d(void) { k3.r1d = 1; }
static int kr_hit_player(void) { return k3.r1c; }
/* KoopaBattleVs3Lv1 (ChaseRoll / ShockWave) and KoopaBattleVs3Lv2 (AttackRoll / FireShort) */
enum { K3B_ROLL, K3B_FIRE, K3B_SHOCK, K3B_DAMAGE, K3B_JUMPAWAY, K3B_GUARD, K3B_RECOVER };
static int kb3_state(int v)
{
    switch (v) {
    case K3B_ROLL: return kp.lv == 1 ? KST_CHASEROLL : KST_ROLL; case K3B_FIRE: return KST_FIRESHORT;
    case K3B_SHOCK: return kp.lv == 1 ? KST_SHOCKWAVE : -1; case K3B_DAMAGE: return KST_DAMAGEESCAPE;
    case K3B_JUMPAWAY: return KST_JUMPAWAY; case K3B_GUARD: return KST_GUARD;
    }
    return -1;
}
static int kb3_base(void) { return kp.lv == 1 ? K3B_SHOCK : K3B_FIRE; } /* the nerve each one goes back to */
static int kb3_update(int first) /* 1 when it is killed */
{
    Nv *n = &k3.bat;
    if (first) { k3.bdead = 0; nv_set(n, kb3_base()); } /* appear */
    int prev = n->nrv, chg = n->next >= 0;
    int st = nv_run(n), v = n->nrv, sk = kb3_state(v);
    if (chg) { int ok = kb3_state(prev); if (ok >= 0 && !kp.sdead[ok]) kst_kill(ok); } /* endState */
    if (st == 0 && sk >= 0) kst_appear(sk); /* startState */
    switch (v) {
    case K3B_ROLL:
        if (kst_run(sk)) nv_set(n, dm_damaging() ? K3B_JUMPAWAY : kb3_base());
        break;
    case K3B_SHOCK:
        if (kst_run(sk)) nv_set(n, dm_damaging() ? K3B_JUMPAWAY : K3B_ROLL);
        break;
    case K3B_FIRE:
        if (kst_run(sk)) nv_set(n, kp.lv == 1 ? K3B_SHOCK : dm_damaging() ? K3B_JUMPAWAY : K3B_ROLL);
        break;
    case K3B_DAMAGE:
        if (st == 0) { /* mStateDamageEscape->appear(), startDamageReverse */
            kst_appear(KST_DAMAGEESCAPE);
            kp_spin_hit_blow(); bgm_state(3, 30); /* changeBgmStateDamageTail */
            nv_set(kst(KST_DAMAGEESCAPE), KD_TAILRUNSTART);
        }
        if (kst_run(KST_DAMAGEESCAPE)) { nv_set(n, kb3_base()); break; }
        if (!kde_down_end()) break;
        if (kp.angry) {
            if (kp.lv == 1) bgm_state(1, 120); else bgm_stop(120);
            kbb_kill(); k3.bdead = 1;
        } else { kp.angry = 1; nv_set(n, K3B_RECOVER); } /* startKoopaAngry */
        break;
    case K3B_JUMPAWAY: case K3B_GUARD:
        if (kst_run(sk)) nv_set(n, kb3_base());
        break;
    case K3B_RECOVER: /* KoopaBattleBase::updateRecover */
        if (st == 0) { kp.b.vel = v3(0, 0, 0); kp_act("Recover"); kp_recover_armor(); }
        if (kp_act_end()) nv_set(n, kb3_base());
        break;
    }
    return k3.bdead;
}
static void kb3_attack(int s)
{
    Nv *n = &k3.bat;
    if (nv_is(n, K3B_ROLL)) { if (!kr_attack(kb3_state(K3B_ROLL), s)) kp_try_push(s); return; }
    if (kp.lv == 1 && nv_is(n, K3B_SHOCK)) { kw_attack(s); return; }
    if (nv_is(n, K3B_DAMAGE)) { kde_attack(s); return; } /* (JumpAway: tryKoopaAttackMapObj, not the player) */
    if (!kp_try_push(s)) kp_try_body(s);
}
static int kb3_receive(int s) /* receiveMsgPlayerAttack */
{
    Nv *n = &k3.bat;
    if (kp.lv == 1 && nv_is(n, K3B_ROLL) && kr_try_damage(s)) { nv_set(n, K3B_DAMAGE); return 1; }
    if (nv_is(n, K3B_DAMAGE)) return kde_try_damage(s);
    int ok = nv_is(n, K3B_FIRE) || (kp.lv == 1 && nv_is(n, K3B_SHOCK)) || (nv_is(n, K3B_ROLL) && kr_guardable(kb3_state(K3B_ROLL))) ||
             nv_is(n, K3B_JUMPAWAY) || nv_is(n, K3B_RECOVER);
    if (ok && kst_guard_try(s)) { nv_set(n, K3B_GUARD); return 1; }
    return 0; /* (tryKoopaReflectStarPiece: no star piece shots here) */
}
static int kb3_enemy_blow(int s) /* KoopaBattleVs3Lv2::receiveMsgEnemyAttack (ACTMES_TO_ENEMY_ATTACK_BLOW on sensor s) */
{
    Nv *n = &k3.bat;
    if (kp.lv != 2) return 0;
    if (nv_is(n, K3B_DAMAGE)) return kde_try_damage(s); /* mStateDamageEscape->tryDamage (isMsgToEnemyAttackBlow too) */
    if ((nv_is(n, K3B_ROLL) && s == KS_ATTACKROLLRECEIVER && !kp.sdead[KST_ROLL]) || nv_is(n, K3B_FIRE)) { nv_set(n, K3B_DAMAGE); return 1; }
    return 0;
}

static int kp_blow(V3 c, float r, V3 *at) /* sendMsgToEnemyAttackBlow (a PunchingKinoko) on Koopa: KoopaSequencerVs3::receiveMsgEnemyAttack */
{
    if (kp.row < 0 || !kp.alive || kp.vs != 3 || !nv_is(&kp.seq, K3_LV2) || demo_on) return 0;
    for (int s = 0; s < KS_N; s++)
        if (kp.sv[s] && vlen(vsub(c, kp_sc(s))) < r + kp.sens[s][3] && kb3_enemy_blow(s)) { *at = kp_sc(s); printf("koopa: blow on %s\n", kp_sdef[s].name); return 1; }
    return 0;
}

/* KoopaDemoFallToPlanetVs3Lv2 / Lv3 */
enum { K3F_WAIT, K3F_DEMO, K3F_FALL, K3F_LAND, K3F_WAITPLAYER };
static int k3_cam_demo(const char *demo, const char *clip, const char *pos) /* tryStartKoopaAndMarioCameraDemo */
{
    if (!kp_try_demo(demo, 1)) return 0;
    kp.calc_grav = 0;
    if (pos) { kp_set_pos(pos); kp_player_at(pos); }
    kp_act(clip); /* startBckPlayer(clip): ponytail (Mario stands in BattleWait; his demo clips are not in resident.bin) */
    kp_acam(clip);
    printf("koopa: demo %s\n", demo);
    return 1;
}
static int k3_cam_demo_end(const char *demo) /* tryEndKoopaCameraDemo */
{
    if (!kp_act_end()) return 0;
    kp_acam(NULL); kp_end_demo(demo); kp.calc_grav = 1;
    return 1;
}
static void k3_sw(const char *cls, int sw, int on) { const Row *r = kp_row_of(cls); if (r && r->sw[sw] >= 0) sw_set(r, sw, on); }
static int k3_sw_on(const char *cls, int sw) { const Row *r = kp_row_of(cls); return r && r->sw[sw] >= 0 && sw_on(r, sw); }
static int k3_fall_update(int lv3, int first) /* 1 when it is killed */
{
    static const KpMove fall = {0.0f, 1.0f, 0.98f, 0.0f}, wait = {0.0f, 1.0f, 0.9f, 1.0f};
    Nv *n = &k3.fall;
    if (first) { k3.fdead = 0; kp.shadow = 0; nv_set(n, K3F_WAIT); } /* appear: the planet shadow killed */
    int st = nv_run(n);
    const char *demo = lv3 ? "クッパＶｓ３惑星移動（Ｌｖ２からＬｖ３）" : "クッパＶｓ３惑星移動（Ｌｖ１からＬｖ２）", *cam = lv3 ? "惑星Ｌｖ３内側を落下" : "惑星Ｌｖ２まで落下（後半）";
    switch (n->nrv) {
    case K3F_WAIT:
        if (k3_cam_demo(demo, lv3 ? "DemoKoopaFallSun" : "DemoKoopaFall", "デモ中心")) nv_set(n, K3F_DEMO);
        break;
    case K3F_DEMO:
        if (lv3) {
            if (st == 0) k3p_hide(K3P_LV3, 0); /* startBrk(Lv2, Death): ponytail (its BRK) */
            if (st == 250) bgm_start("MBGM_BOSS_KOOPA_FINAL"); /* sFinalBgmStartStepSub */
            if (st == 266) { k3p_kill(K3P_SUNIN, 0); k3_sw("KoopaSwitchKeeper", SW_DEAD, 1); } /* sFallOutsideChangeSun */
            if (st == 267) {
                k3p_kill(K3P_LV1, 1); k3p_kill(K3P_LV2, 1); k3p_kill(K3P_SUNOUT, 1);
                k3_sw("KoopaSwitchKeeper", SW_A, 0); k3_sw("KoopaSwitchKeeper", SW_B, 1); k3_sw("KoopaSwitchKeeper", SW_DEAD, 0);
            }
            kp_lv(1, st < 214 ? SE_BM_LV_KOOPA_FALL_SUN_1 : st < 328 ? SE_BM_LV_KOOPA_FALL_SUN_2 : SE_BM_LV_KOOPA_FALL_SUN_3);
            if (st == 328) kp_se(SE_BM_KOOPA_FALL_SUN_3_WIND);
        }
        if (!k3_cam_demo_end(demo)) break;
        if (lv3) { kp_set_pos("Ｌｖ３内側（クッパ）"); kp_player_at("Ｌｖ３内側（マリオ）"); k3_pc(K3P_LV3); }
        else {
            const Row *r = &sc->objs[kp.row];
            if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0);
            k3_sw("KoopaSwitchKeeper", SW_A, 1);
            render_cover(2);
        }
        nv_set(n, K3F_FALL);
        if (lv3) { kp_cam_start(cam); kp_act("JumpSoon"); kp.shadow = 1; }
        break;
    case K3F_FALL:
        if (st == 0 && !lv3) {
            kp_set_pos("Ｌｖ２開始（クッパ）"); kp_player_at("Ｌｖ２開始（マリオ）"); k3_pc(K3P_LV2);
            kp_cam_start(cam); kp_act("JumpSoon"); kp.shadow = 1;
        }
        if (lv3) kp_lv(1, SE_BM_LV_KOOPA_FALL_SUN_1);
        kp_to_player(&fall);
        if (!lv3) kp_lv(0, SE_BM_LV_KOOPA_FALL_TO_PLANET);
        if (kp.b.on_ground) { kp.b.vel = v3(0, 0, 0); nv_set(n, K3F_LAND); }
        break;
    case K3F_LAND:
        if (st == 0) { kp_act("Land"); kp_se(SE_BM_KOOPA_FLIP_RECOVER_END); kp_se(SE_BV_KOOPA_LAND_HEAVY); pad_rumble(RUMBLE_STRONG); camera_shake(SHAKE_NORMAL_STRONG); }
        if (kp_act_end()) nv_set(n, K3F_WAITPLAYER);
        break;
    case K3F_WAITPLAYER:
        kp_to_player(&wait);
        if (mario->grounded) { k3.fdead = 1; kp_cam_end(cam); } /* isOnGroundPlayer: kill */
        break;
    }
    return k3.fdead;
}

/* KoopaBattleStairsVs3: every normal stair's request fired at from クッパ階段戦の砲弾出現 (sFireSpeed 30) */
enum { K3S_WAITDEMO, K3S_DEMO, K3S_WAIT };
static void kstb3_register(void) /* registerStair: calcFireAttackStep(stair, 30, 0, mNamePos) */
{
    float m[16];
    V3 np = v3(0, 0, 0);
    if (kp_gpos("クッパ階段戦の砲弾出現", m)) np = v3(m[12], m[13], m[14]);
    kp.jp[0] = np;
    for (int i = 0; i < ninsts; i++) if (insts[i].kstair) ks_of(insts[i].row);
    for (int k = 0; k < nkss; k++) {
        const Row *r = &sc->objs[kss[k].row];
        if (!kss[k].init) { kss[k].init = 1; kss[k].pos = row_pos(r); ks_set(k, KS_WAITSWITCH); }
        if (kstb_is_type(k, 1)) continue;
        kss[k].reg = 1; k3.a6[k] = 0;
        V3 t; float d;
        kp_stair_target(k, np, &t, &d);
        kss[k].a8 = (int)(d / 30.0f) - (int)(90.0f / 30.0f);
        kss[k].fstep = kp_stair_break_time(k) - kss[k].a8;
    }
    printf("koopa: %d stairs registered (vs3)\n", nkss);
}
static void kstb3_update(void)
{
    Nv *n = &kp.stairs;
    int st = nv_run(n);
    switch (n->nrv) {
    case K3S_WAITDEMO:
        if (!kp_try_demo("階段の戦い開始デモ", 0)) break; /* tryStartKoopaCameraDemo(デモ中心) */
        kp.calc_grav = 0; kp_set_pos("デモ中心"); kp_act("DemoKoopaBattleStairsVs3Start"); kp_acam("DemoKoopaBattleStairsVs3Start");
        for (int k = 0; k < nkss; k++) kss[k].mov = 1; /* (requestMovementOn: the casts and the demo meteors) */
        k3_casts_act("DemoKoopaBattleStairsVs3Start"); /* the demo meteors 1-3: ponytail (MeteorStrike's demo clips) */
        nv_set(n, K3S_DEMO);
        break;
    case K3S_DEMO:
        if (st == 0 || !k3_cam_demo_end("階段の戦い開始デモ")) break;
        k3_casts_act("DemoKoopaVs3Wait");
        nv_set(n, K3S_WAIT);
        break;
    case K3S_WAIT:
        if (st == 0) kp_act("Wait");
        if (!nkss && st % 60 == 0) kstb3_register(); /* (a stairs zone streamed in later: their initAfterPlacement then) */
        for (int k = 0; k < nkss; k++) /* tryAttack: isRequestAttackVs3 */
            if (kss[k].reg && !k3.a6[k] && kstb_is_type(k, 0) && kss[k].nrv == KS_WAITFIRE && (kss[k].step == kss[k].fstep || kss[k].fstep < 0)) {
                k3.a6[k] = 1; kp_fire_stairs(k, kp.jp[0]); /* emitFireStairsToTarget(useFront: ponytail, the fire's own front) */
            }
        break;
    }
}

/* KoopaSequencerVs3 */
static int k3_dx_is(const char *name) { return dx.on && !strcmp(demos[dx.cur].h->name, name); }
static int k3_dx_last(void) { const Demo *d = &demos[dx.cur]; return dx.on && dx.part == d->h->nparts - 1 && dx.step >= d->parts[dx.part].total - 1; }
static void kq3_update(void)
{
    Nv *n = &kp.seq;
    int st = nv_run(n);
    const Row *r = &sc->objs[kp.row];
    int rs = k3_sw_on("KoopaRestarterVs3", SW_APPEAR);
    if (rs && !k3.restarter && kp.hidden) { /* KoopaRestarterVs3::appear: tryRestartKoopa (ponytail: no face control) */
        kp.hidden = 0;
        k3_casts_act("DemoKoopaVs3Wait");
        printf("koopa: restarted\n");
    }
    k3.restarter = rs;
    switch (n->nrv) {
    case K3_WAITPLAYER:
        if (st == 0) { kp_set_pos("デモ中心"); kp_act("Wait"); k3p_hide(K3P_LV3, 1); }
        if (k3_sw_on("KoopaViewSwitchKeeper", SW_APPEAR) && kp.hidden) kp.hidden = 0;
        if (k3_sw_on("KoopaViewSwitchKeeper", SW_A)) {
            if (!k3p[K3P_LV1].hidden) { k3p_hide(K3P_LV1, 1); k3p_hide(K3P_LV2, 1); k3p_kill(K3P_SUNOUT, 1); }
        } else if (k3p[K3P_LV1].hidden) { k3p_hide(K3P_LV1, 0); k3p_hide(K3P_LV2, 0); k3p_kill(K3P_SUNOUT, 0); }
        if (k3_dx_is("クッパＶｓ３ピーチ登場")) {
            if (!k3.casts) { k3_casts_act("DemoKoopaMeetPeach"); kp_act("DemoKoopaMeetPeach"); } /* initTalkAnimDemoCast */
            if (k3_dx_last()) { k3_casts_act("DemoKoopaVs3Wait"); kp_act("Wait"); } /* endKoopaDemoMeetPeach */
        }
        if (k3_sw_on("KoopaSwitchKeeper", SW_APPEAR)) { render_cover(2); nv_set(n, K3_BATTLESTAIRS); printf("koopa: stairs battle\n"); }
        break;
    case K3_BATTLESTAIRS:
        if (st == 0) kstb3_register(); /* (the stairs' initAfterPlacement: their zone is in by now) */
        kstb3_update();
        if (r->sw[SW_A] >= 0 && sw_on(r, SW_A)) {
            kstb_end();
            nv_set(n, K3_WAITDEMO);
            kp_set_pos("デモ中心"); kp_player_at("デモ中心");
            kp_dx_request("クッパＶｓ３本戦開始");
            printf("koopa: stairs battle over\n");
        }
        break;
    case K3_WAITDEMO:
        if (kp_dx_on()) nv_set(n, K3_DEMOBATTLESTART);
        break;
    case K3_DEMOBATTLESTART:
        if (st == 0) { kp.calc_grav = 0; kp_act("DemoKoopaVs3Start"); kp_acam("DemoKoopaVs3Start"); k3_casts_act("DemoKoopaVs3Start"); } /* initDemoSheetTalkAnim, its casts */
        if (kp_dx_last() || !dx.on) {
            kp_acam(NULL); kp.calc_grav = 1;
            k3_casts_act("DemoKoopaVs3Wait");
            if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1); /* mJumpToPlanet->startReady */
            nv_set(n, K3_DEMOJUMPTOPLANET);
        }
        break;
    case K3_DEMOJUMPTOPLANET:
        if (kst_run_once(KST_JUMPTOPLANET, st == 0)) { nv_set(n, K3_LV1); printf("koopa: battle level 1\n"); }
        break;
    case K3_LV1:
        if (kb3_update(st == 0)) nv_set(n, K3_FALLLV2);
        break;
    case K3_FALLLV2: case K3_FALLLV3:
        if (st == 0) kp_se(SE_AT_KOOPA_FALLDOWN_LV1);
        if (st == 20) kp_se(SE_BV_KOOPA_BATTLE_END_FALL); /* sKoopaFallVoiceStep */
        if (k3_fall_update(n->nrv == K3_FALLLV3, st == 0)) nv_set(n, n->nrv == K3_FALLLV2 ? K3_LV2 : K3_LV3);
        break;
    case K3_LV2:
        if (st == 0) { kp.lv = 2; kp.angry = 0; printf("koopa: battle level 2\n"); } /* startBattleLevel2 */
        if (kb3_update(st == 0)) nv_set(n, K3_FALLLV3);
        break;
    case K3_LV3:
        if (st == 0) { kp.lv = 3; kp.angry = 0; printf("koopa: battle level 3\n"); }
        if (kbm_update(st == 0)) nv_set(n, K3_POWERUPFINAL);
        break;
    case K3_POWERUPFINAL:
        if (kst_run_once(KST_POWERUP, st == 0)) nv_set(n, K3_FINAL);
        break;
    case K3_FINAL:
        if (st == 0) { kp.angry = 1; printf("koopa: battle final\n"); }
        if (kbm_update(st == 0)) { kp.shadow = 0; nv_set(n, K3_WAITDEMODOWN); }
        break;
    case K3_WAITDEMODOWN:
        if (k3_cam_demo("クッパＶｓ３ダウン", "DemoKoopaVs3Down", "ダウンデモ")) nv_set(n, K3_DEMODOWN);
        break;
    case K3_DEMODOWN:
        if (st == 0) { bgm_stop(10); kp.calc_grav = 0; } /* startBrk(Lv3, Death): ponytail */
        if (st == 70) kp_se(SE_BM_KOOPA_DOWN_LAST_IMPACT);
        if (st == 471) kp_se(SE_BM_KOOPA_DOWN_TO_SUN_TRG);
        if (st > 471 && st < 1049) { kp_lv(0, SE_BM_LV_KOOPA_DOWN_TO_SUN); if (st > 544) kp_lv(1, SE_BM_LV_KOOPA_DOWN_LAVA); }
        if (st > 930 && st < 1049) kp_lv(2, SE_BM_LV_KOOPA_FALL_BURN);
        if (st == 1049) kp_se(SE_BM_KOOPA_DOWN_INTO_SUN);
        if (st == 640) render_cover(3); /* sDownCameraCoverStep */
        if (k3_cam_demo_end("クッパＶｓ３ダウン")) {
            kp_player_at("ダウンデモ後（マリオ）");
            bgm_after_boss();
            note_star_row = kp.row; note_star_at = k3p[K3P_LV3].c; /* requestAppearPowerStar(Lv3's position) */
            kp_star_demo = 0;
            if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
            row_kill(kp.row); kp.alive = 0;
            nv_set(n, K3_DEAD);
            printf("koopa: down, its grand star\n");
        }
        break;
    }
}

/* --- Koopa: control (the sequencer), gravity, binder, anim, sensors ----------------------------------------------- */
static void kp_models(const Scene *s)
{
    m_kp = scene_model(s, "KoopaVs1"); if (!m_kp) m_kp = scene_model(s, "KoopaVs2"); if (!m_kp) m_kp = scene_model(s, "KoopaVs3");
    for (int p = 0; p < KPR_N; p++) kpr[p].m = scene_model(s, kpr_model[p]);
    m_kthorn = scene_model(s, "KoopaThorn"); m_karmorb = scene_model(s, "KoopaArmorBreak"); m_kthornb = scene_model(s, "KoopaThornBreak");
    m_kfire2 = scene_model(s, "KoopaFire"); m_ksw = scene_model(s, "KoopaShockWave"); m_ksws = scene_model(s, "KoopaShockWaveShadow");
    m_kmeteor = scene_model(s, "MeteorStrike"); m_kmeteorb = scene_model(s, "MeteorStrikeBreak"); m_kpshadow = scene_model(s, "KoopaPlanetShadow");
}
static void kp_rebind(const Scene *s) { if (kp.row >= 0) { kp_models(s); kp_insts(s); } } /* game_rebind_ */
static void update_kp(void)
{
    if (kp.row < 0) return;
    const Row *r = &sc->objs[kp.row];
    if (!kp.init) { /* initAfterPlacement: its planet and plates, the stairs registered */
        kp.init = 1;
        if (kp.vs == 3) { k3_planets_init(); kp_planet_init(); k3_pc(K3P_LV1); } /* initKoopaVs3: Lv3 holds the plates */
        else { kp_planet_init(); if (kp.vs == 1) kstb_register(); } /* (only KoopaSequencerVs1 makes KoopaBattleStairsVs1) */
        game_rebind();
        printf("koopa: planet %s at %.0f %.0f %.0f\n", kp_planet_ok ? "made" : "missing", kp_pc.x, kp_pc.y, kp_pc.z);
    }
    if (kp.anim && m_kp && m_kp->skin && kp.anim->skin != m_kp->skin) { anim_bind(kp.anim, m_kp->skin); kp_act(kp.act[0] ? kp.act : "Wait"); }
    int moves = dx_cls_moves(kp_cls()) || kp_dx_on();
    if (!demo_on || kp.pd) { kp_fire_stairs_update(); kp_fire_short_update(); kp_shock_update(); }
    kp_plates_update();
    if (mario->grounded) kp.mground = mario->pos;
    if (!mario->spin_timer) kp.spun = 0;
    if (!kp.alive) {
        if (row_dead[kp.row] || kp.seq.nrv == KQ_DEMOBATTLEEND) return;
        if (r->sw[SW_APPEAR] >= 0 && !sw_on(r, SW_APPEAR)) return; /* syncStageSwitchAppear */
        kp.alive = 1; kp.hidden = 0;
        printf("koopa: appear\n");
    }
    if (!moves || scene_stop) return;
    if (kp.vs == 3) kq3_update(); else if (kp.vs == 2) kq2_update(); else kq_update(); /* Koopa::control: mSequencer->update() */
    for (int p = 0; p < KPR_N; p++) if (kpr[p].on && kpr[p].a && kpr[p].a->skin) { anim_update(kpr[p].a); if (p == KPR_ROCKBREAK && kpr_end(p)) kpr[p].on = 0; } /* KoopaRockBreak::control: killed at its action's end */
    if (!kp.alive) return;
    if (kp.calc_grav) kp.b.grav = vmul(up_at(vadd(kp.b.pos, vmul(kp_up(), 200.0f)), kp_up()), -1.0f); /* calcGravity */
    w_bind(&kp.b, 200.0f); /* initBinder(200, 200) */
    if (kp.anim && kp.anim->skin) anim_update(kp.anim);
    kp_acam_update();
    kp_sensors_place();
    int b3 = kp.vs == 3 && (nv_is(&kp.seq, K3_LV1) || nv_is(&kp.seq, K3_LV2)) && !demo_on; /* KoopaSequencerVs3: BattleVs3Lv1 / Lv2 */
    int battle = kp_main_on() && !demo_on;
    if (!battle && !b3) return;
    for (int s = 0; s < KS_N; s++) /* attackSensor: its sensors on Mario's */
        if (kp.sv[s] && kp_touch(s)) { if (b3) kb3_attack(s); else kbm_attack(s); }
    for (int s = 0; s < KS_N && !kp.spun; s++) /* receiveMsgPlayerAttack: Mario's spin on its sensors */
        if (kp.sv[s] && mario_offence(kp.sens[s], kp_sc(s), kp.sens[s][3], kp.b.grav) == MSG_HITALL && (b3 ? kb3_receive(s) : kbm_receive(s))) { m_punch(kp.sens[s], kp_sc(s), 0); kp.spun = 1; printf("koopa: spin on %s\n", kp_sdef[s].name); }
}
static int kp_pose(Anim *a, const Model *md, const char *clip, float frame) /* a pooled model's pose: its clip at frame */
{
    if (!a || !md || !md->skin) return 0;
    if (a->skin != md->skin) { anim_bind(a, md->skin); a->clip_id = -1; }
    if (a->clip_id < 0 || strcmp(sc->clips[a->clip_id].name, clip)) anim_start(a, clip, 0);
    a->frame = frame; a->speed = 0; a->done = 0;
    anim_update(a);
    return 1;
}
static void kp_draw(void)
{
    if (kp.row < 0) return;
    float b[16], jm[16], m[16];
    if (kp.alive && !kp.hidden && m_kp) {
        kp_base(b);
        if (m_kp->skin && kp.anim && kp.anim->skin == m_kp->skin) draw_skin(m_kp, b, kp.anim); else render_model(m_kp, b, 0x80, 0x80, 0x80);
        /* initShadowFromCSV: 戦闘用 ボリューム影(球) Center 250 dropped 3000 and 頭 Face 175.78 (1000); デモ用 Center 250 (3000) */
        draw_shadow(kp_joint(2), kp_up(), kp.b.front, 250.0f, 250.0f, 0.0f, 50.0f, 3000.0f);
        if (!kp_shadow_demo) draw_shadow(kp_joint(KJ_FACE), kp_up(), kp.b.front, 175.78f, 175.78f, 0.0f, 50.0f, 1000.0f);
        if (kp.thorn && m_kthorn) { kp_joint_m(KJ_TAIL2, jm); render_model(m_kthorn, jm, 0x80, 0x80, 0x80); kp_joint_m(KJ_TAIL3, jm); render_model(m_kthorn, jm, 0x80, 0x80, 0x80); }
        if (kp.armor_break && m_karmorb) { kp_joint_m(KJ_HIP1, jm); render_model(m_karmorb, jm, 0x80, 0x80, 0x80); }
        if (kp.thorn_break && m_kthornb) { /* (its JointName "": the host's base) */
            if (kp.a_tb && m_kthornb->skin && kp.a_tb->skin == m_kthornb->skin) { anim_update(kp.a_tb); draw_skin(m_kthornb, b, kp.a_tb); }
            else render_model(m_kthornb, b, 0x80, 0x80, 0x80);
        }
        if (kp.shadow && m_kpshadow) { /* KoopaPlanetShadow: at the planet's center, up towards Hip1 */
            V3 d = vsub(kp_joint(KJ_HIP1), kp_pc);
            if (vlen(d) > 1e-3f) { basis_matrix(m, kp_pc, vnorm(d), kp.b.front, 0); render_model(m_kpshadow, m, 0x80, 0x80, 0x80); }
        }
    }
    for (int p = 0; p < KPR_N && kp.vs == 3; p++) { /* the Vs3 parts: rock / roll ball on Koopa (ponytail: its body's base, not
                                                     * the archive's RockFixPos / RollBallFixPos), the rock break where it
                                                     * appeared, the demo casts at デモ中心 */
        if (!kpr[p].on || !kpr[p].m || (p < KPR_PEACH && (!kp.alive || kp.hidden))) continue;
        if (p >= KPR_PEACH) { if (!kp_gpos("デモ中心", m)) continue; }
        else if (kpr[p].own) memcpy(m, kpr[p].mtx, 64); else kp_base(m);
        if (kpr[p].m->skin && kpr[p].a && kpr[p].a->skin == kpr[p].m->skin) draw_skin(kpr[p].m, m, kpr[p].a); else render_model(kpr[p].m, m, 0x80, 0x80, 0x80);
    }
    for (int i = 0; i < KP_FIRE_SHORT; i++) {
        if (!kpfs[i].on || !m_kfire2) continue;
        basis_matrix(m, kpfs[i].pos, vmul(kpfs[i].grav, -1.0f), kpfs[i].front, 0);
        if (kp_pose(ka_fire, m_kfire2, kpfs[i].nrv ? "koopafire/disappear" : "koopafire/wait", kpfs[i].f)) draw_skin(m_kfire2, m, ka_fire);
        else render_model(m_kfire2, m, 0x80, 0x80, 0x80);
    }
    for (int i = 0; i < KP_FIRE_STAIRS; i++) {
        if (!kpfst[i].on) continue;
        V3 up = up_at(kpfst[i].pos, v3(0, 1, 0));
        basis_matrix(m, kpfst[i].pos, up, kpfst[i].front, 0); /* makeMtxFrontUpPos, rotateMtxLocalXDegree */
        float c = cosf(kpfst[i].rotx * 0.0174533f), s = sinf(kpfst[i].rotx * 0.0174533f), y[3] = {m[4], m[5], m[6]}, z[3] = {m[8], m[9], m[10]};
        for (int k = 0; k < 3; k++) { m[4 + k] = y[k] * c + z[k] * s; m[8 + k] = z[k] * c - y[k] * s; }
        if (kpfst[i].nrv == 0) { if (m_kmeteor) render_model(m_kmeteor, m, 0x80, 0x80, 0x80); }
        else if (m_kmeteorb && kp_pose(ka_mbreak, m_kmeteorb, "meteorstrikebreak/break", kpfst[i].mf)) draw_skin(m_kmeteorb, m, ka_mbreak);
    }
    for (int i = 0; i < KP_SHOCK; i++) { /* the Spread BCK's frame: sin(angle) x 10 */
        if (!ksw[i].on) continue;
        float f = sinf(ksw[i].ang * 0.0174533f) * 10.0f;
        if (m_ksw && kp_pose(ka_sw, m_ksw, "koopashockwave/spread", f)) draw_skin(m_ksw, ksw[i].mb, ka_sw);
        if (m_ksws && kp_pose(ka_sws, m_ksws, "koopashockwaveshadow/spread", f)) draw_skin(m_ksws, ksw[i].mm, ka_sws);
    }
}
static void kp_init(const Scene *s, const unsigned char *blob) /* Koopa::init (KoopaFunction::createKoopaVs1) */
{
    (void)blob;
    memset(&kp, 0, sizeof kp); kp.row = -1; kp.dx_k = -1; kp.req = -1;
    memset(kpfst, 0, sizeof kpfst); memset(kpfs, 0, sizeof kpfs); memset(ksw, 0, sizeof ksw);
    kp_planet_ok = 0; kp_star_demo = -1; kp_planet_ki = -1;
    int t1 = scene_type(s, "KoopaVs1"), t2 = scene_type(s, "KoopaVs2"), t3 = scene_type(s, "KoopaVs3");
    memset(&k3, 0, sizeof k3); memset(&kfb, 0, sizeof kfb); kp_ballm = 0;
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if ((int)r->type != t1 && (int)r->type != t2 && (int)r->type != t3) continue;
        V3 x, z;
        row_axes(r, &x, &z);
        kp.row = (int)i; kp.vs = (int)r->type == t3 ? 3 : (int)r->type == t2 ? 2 : 1; kp.lv = 1; kp.calc_grav = 1; kp.thorn = 1;
        kp.b.pos = row_pos(r); kp.b.grav = vmul(up_at(kp.b.pos, v3(0, 1, 0)), -1.0f); kp.b.front = vnorm(vtangent(z, kp_up()));
        kp.b.row = (int)i; kp.b.qy = kp_up();
        for (int k = 0; k < KS_N; k++) kp.sv[k] = (u8)kp_sdef[k].valid;
        kp.seq.next = kp.vs == 3 ? K3_WAITPLAYER : kp.vs == 2 ? KQ2_WAITDEMOBATTLESTART : KQ_BATTLESTAIRS; kp.stairs.next = KSB_WAITDEMO; kp.sub.next = KSS_LV1; kp.main.next = KBM_WANDER;
        static const s8 first[KST_N] = {KG_FACE, KH_RUN, KW_JUMPSTART, KF_START, KD_ESCAPESTART, KJA_JUMPSTART, KJP_START, KP_WAITDEMO, KSP_START, KFL_START, KR_START, KR_START};
        for (int k = 0; k < KST_N; k++) { kp.st[k].next = first[k]; kp.st[k].nrv = first[k]; kp.sdead[k] = 1; }
        kp.alive = r->sw[SW_APPEAR] < 0; kp.hidden = kp.alive; /* useStageSwitchReadAppear: makeActorDead, else hideModel */
        Anim **an[6 + KPR_N] = {&kp.anim, &kp.a_tb, &ka_fire, &ka_mbreak, &ka_sw, &ka_sws};
        for (int p = 0; p < KPR_N; p++) { memset(&kpr[p], 0, sizeof kpr[p]); an[6 + p] = kp.vs == 3 ? &kpr[p].a : NULL; }
        for (int k = 0; k < 6 + KPR_N; k++) if (an[k]) { *an[k] = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(*an[k], 0, sizeof(Anim)); (*an[k])->clip_id = -1; }
        kp_models(s);
        printf("koopa: row %d (vs%d), camera set %d\n", (int)i, kp.vs, row_camset[i]);
        break;
    }
}
/* Test builds (XF=-DKP_TEST, KoopaBattleVs1Galaxy/1): its SW_APPEAR on, the stairs battle until Koopa stands at
 * デモ中心, then its SW_A (Mario in the boss zone); in the battle Mario stands on a damage plate while Koopa hip drops,
 * behind its tail while it escapes (a spin), at its body in the tail run (a spin); down, to its grand star */
int game_test_kp(int frame)
{
    static int last = -1000, dead_at = -1;
    if (kp.row < 0) return -1;
    const Row *r = &sc->objs[kp.row];
    if (frame % 30 == 0)
        printf("kp test f %d: seq %d stairs %d(%d) sub %d main %d st hip %d sw %d de %d ja %d jtp %d pu %d | lv %d at %.0f %.0f %.0f (g %d) Mario %.0f %.0f %.0f life %d dx %d.%d.%d pd %s\n",
               frame, kp.seq.nrv, kp.stairs.nrv, kp.jump_idx, kp.sub.nrv, kp.main.nrv, kp.st[KST_HIPDROP].nrv, kp.st[KST_SHOCKWAVE].nrv,
               kp.st[KST_DAMAGEESCAPE].nrv, kp.st[KST_JUMPAWAY].nrv, kp.st[KST_JUMPTOPLANET].nrv, kp.st[KST_POWERUP].nrv, kp.lv,
               kp.b.pos.x, kp.b.pos.y, kp.b.pos.z, kp.b.on_ground, mario->pos.x, mario->pos.y, mario->pos.z, game_life(), dx.on, dx.part, dx.step, pd.name ? pd.name : "-");
    if (life < 3) life = 3;
    if (!kp.alive && row_dead[kp.row]) { /* to its grand star (arg7: the declarer) */
        int ty = scene_type(sc, "GrandStar");
        if (dead_at < 0) dead_at = frame;
        for (u32 i = 0; frame - dead_at >= 300 && frame % 20 == 0 && ty >= 0 && i < sc->nobjs; i++)
            if ((int)sc->objs[i].type == ty && sc->objs[i].arg[7] == kp.row) { mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        return 100;
    }
    if (!kp.alive) { if (frame > 60 && r->sw[SW_APPEAR] >= 0) sw_set(r, SW_APPEAR, 1); return 0; }
    if (kp.vs == 3) { /* Vs3: the keepers' appear switches, the stairs till its SW_A; Lv1 a spin on the rolling rock's face,
                       * Lv2 a blow on the rolling ball (a PunchingKinoko's message), then as Vs1 */
        static int t0 = -1;
        if (frame % 30 == 0) printf("kp3 test f %d: seq %d stairs %d bat %d fall %d roll %d/%d hidden %d\n", frame, kp.seq.nrv, kp.stairs.nrv,
                                    k3.bat.nrv, k3.fall.nrv, kp.st[KST_CHASEROLL].nrv, kp.st[KST_ROLL].nrv, kp.hidden);
        if (nv_is(&kp.seq, K3_WAITPLAYER)) {
            if (frame >= 60) k3_sw("KoopaViewSwitchKeeper", SW_APPEAR, 1);
            int ty = scene_type(sc, "KoopaBattleMapStairBig"), in_zone = 0;
            for (u32 i = 0; frame >= 120 && ty >= 0 && i < sc->nobjs; i++) /* Mario on the stairs' foot: the boss zone in */
                if ((int)sc->objs[i].type == ty) {
                    V3 p = row_pos(&sc->objs[i]), up = up_at(p, v3(0, 1, 0));
                    mario->pos = vadd(p, vmul(up, 150.0f)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0; break;
                }
            for (int i = 0; i < ninsts; i++) if (insts[i].kstair) in_zone = 1;
            if (in_zone) k3_sw("KoopaSwitchKeeper", SW_APPEAR, 1); /* (its foot cube, over a death area, written: test only) */
            return 1;
        }
        if (nv_is(&kp.seq, K3_BATTLESTAIRS)) {
            if (t0 < 0 && kp.stairs.nrv == K3S_WAIT) t0 = frame;
            if (t0 >= 0 && frame - t0 == 240 && r->sw[SW_A] >= 0) { sw_set(r, SW_A, 1); printf("kp test f %d: its SW_A\n", frame); }
            return 1;
        }
        if (demo_on || frame - last < 30) return 2;
        Nv *cr = kst(KST_CHASEROLL), *ar = kst(KST_ROLL);
        if (((nv_is(&kp.seq, K3_LV1) || nv_is(&kp.seq, K3_LV2)) && !nv_is(&k3.bat, K3B_DAMAGE)) ||
            ((nv_is(&kp.seq, K3_LV3) || nv_is(&kp.seq, K3_FINAL)) && (nv_is(&kp.main, KBM_ROLL) || nv_is(&kp.main, KBM_SHOCKWAVE)))) { /* held on the planet's far side, out of
                                                       * the shock waves' and fires' way (200 over it; 700: out of its gravity);
                                                       * a fire ball near: a side step to the other side of its path */
            static float side = 1.0f;
            static int moved = -1000;
            V3 d = vsub(kp.b.pos, kp_pc);
            float rr = vlen(d);
            for (int i = 0; i < KP_FIRE_SHORT; i++)
                if (kpfs[i].on && kpfs[i].nrv == 0 && vlen(vsub(kpfs[i].pos, mario->pos)) < 600.0f && frame - moved > 60) { side = -side; moved = frame; }
            if (rr > 1.0f && !dmg.st) {
                d = vmul(d, 1.0f / rr);
                V3 t = vtangent(kp.b.front, d), h = vnorm(vadd(vmul(d, -1.0f), vmul(vlen(t) > 1e-3f ? vnorm(t) : t, 0.5f * side)));
                mario->pos = vadd(kp_pc, vmul(h, rr + 200.0f)); mario->up = h; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            } else if (dmg.st && !mario->grounded) /* a damage state (airborne 360: a miss): pulled down onto the planet */
                mario->pos = vsub(mario->pos, vmul(vnorm(vsub(mario->pos, kp_pc)), 15.0f));
        }
        if (nv_is(&kp.seq, K3_LV1) && nv_is(&k3.bat, K3B_ROLL) && nv_is(cr, KR_ROLLGROUND) && cr->st > 60) { /* before its face, a spin */
            V3 up = kp_up(), f = kp_sc(KS_CHASEROLLFACE), h = vtangent(vsub(f, kp.b.pos), up);
            h = vlen(h) > 1e-3f ? vnorm(h) : kfb.b.front;
            mario->pos = vsub(vadd(f, vmul(h, 150.0f)), vmul(up, HIT_BODY_H)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->face = vmul(h, -1.0f); mario->spin_timer = SPIN_TIME;
            last = frame; printf("kp test f %d: face spin\n", frame);
            return 8;
        }
        if (nv_is(&kp.seq, K3_LV2) && nv_is(&k3.bat, K3B_ROLL) && nv_is(ar, KR_ROLLGROUND) && ar->st > 10) {
            V3 at; kp_blow(kp_sc(KS_ATTACKROLLRECEIVER), 1.0f, &at);
            last = frame; printf("kp test f %d: blow\n", frame);
            return 9;
        }
    }
    if (nv_is(&kp.seq, KQ_BATTLESTAIRS)) {
        static int t0 = -1;
        if (t0 < 0 && kp.stairs.nrv >= KSB_WAIT) t0 = frame;
        for (int k = 0; t0 >= 0 && k < nkss; k++) { /* the climb's SwitchCubes (21, 22) */
            const Row *q = &sc->objs[kss[k].row];
            if ((q->sw[SW_A] == 21 && frame - t0 > 500) || (q->sw[SW_A] == 22 && frame - t0 > 1100)) sw_set(q, SW_A, 1);
        }
        if (kp.jump_idx >= 3 && nv_is(&kp.stairs, KSB_WAIT) && r->sw[SW_A] >= 0) { sw_set(r, SW_A, 1); printf("kp test f %d: its SW_A\n", frame); }
        return 1;
    }
    if (demo_on || frame - last < 30) return 2;
    V3 up = kp_up();
    int hip = nv_is(&kp.main, KBM_HIPDROP), de = kp_main_on() ? nv_is(&kp.main, KBM_DAMAGEESCAPE) : kp.vs == 3 && nv_is(&k3.bat, K3B_DAMAGE);
    Nv *dn = kst(KST_DAMAGEESCAPE), *hn = kst(KST_HIPDROP);
    if (kp_main_on() && hip && (nv_is(hn, KH_RUN) || nv_is(hn, KH_JUMPWAITAIR) || nv_is(hn, KH_JUMPUP))) { /* on the nearest damage plate */
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < 6; i++) if (kpl[i].nrv == 0) { float d = vlen(vsub(kpl[i].pos, kp.b.pos)); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) {
            V3 pu = vnorm(vsub(kpl[best].pos, kp_pc));
            mario->pos = vadd(kp_pc, vmul(pu, 1320.0f)); mario->up = pu; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            last = frame; printf("kp test f %d: on damage plate %d\n", frame, best);
        }
        return 3;
    }
    if (kp_main_on() && (nv_is(&kp.main, KBM_WANDER) || nv_is(&kp.main, KBM_SEARCH))) { /* in its sight, 900 ahead on the planet */
        V3 d = vnorm(vsub(vadd(kp.b.pos, vmul(kp.b.front, 900.0f)), kp_pc)), pp = vadd(kp_pc, vmul(d, 1320.0f));
        mario->pos = pp; mario->up = d; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        last = frame; printf("kp test f %d: before it\n", frame);
        return 7;
    }
    if (de && (nv_is(dn, KD_ESCAPERUN) || nv_is(dn, KD_ESCAPEFIND) || nv_is(dn, KD_ESCAPEEND))) { /* at its tail, a spin */
        V3 t = kp_sc(KS_RECEIVERTAIL), h = vtangent(vsub(t, kp.b.pos), up);
        h = vlen(h) > 1e-3f ? vnorm(h) : vmul(kp.b.front, -1.0f);
        mario->pos = vsub(vadd(t, vmul(h, 60.0f)), vmul(up, HIT_BODY_H)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(h, -1.0f); mario->spin_timer = SPIN_TIME;
        last = frame; printf("kp test f %d: tail spin\n", frame);
        return 4;
    }
    if (de && (nv_is(dn, KD_TAILRUN) || nv_is(dn, KD_TAILRUNFINAL) || (nv_is(dn, KD_TAILRUNSTART) && dn->st > 60) || (nv_is(dn, KD_TAILRUNSTARTFINAL)))) { /* at its body, a spin */
        V3 c = kp_sc(KS_DAMAGEESCAPEBODY), h = vtangent(vsub(mario->pos, c), up);
        h = vlen(h) > 1e-3f ? vnorm(h) : kp.b.front;
        mario->pos = vsub(vadd(c, vmul(h, 200.0f)), vmul(up, HIT_BODY_H)); mario->up = up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(h, -1.0f); mario->spin_timer = SPIN_TIME;
        last = frame; printf("kp test f %d: body spin\n", frame);
        return 5;
    }
    return 6;
}
