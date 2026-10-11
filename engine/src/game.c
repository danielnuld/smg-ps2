#include "game.h"
#include "anim.h"
#include "fx.h"
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "gravity.h"
#include "kcl.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "vmath.h"

/* Gameplay subset of HeavensDoorGalaxy scenario 2, data-oriented: one compact array per type, sized at load,
 * no allocation per object. Behaviour follows Petari where noted; differences are listed in docs/phase1-report.md. */

typedef struct { V3 pos; short alive, row; } Pickup;             /* 16 B: Coin, PurpleCoin, StarPiece; row: the
                                                                  * placement it came from, -1 a drop */
enum { PICK_GONE, PICK_ON, PICK_POP_NEW, PICK_POP_BLINK }; /* alive: popping coins cannot be taken while new and
                                                             * are not drawn on their blink frames */
typedef struct { V3 pos, vel, up; int state, timer, row, kind; int phase, dir; float from, to, len; } Kuribo; /* walking
                                         * enemies; row: its placement; phase .. len: Takobo's patrol (takobo_update) */
enum { KURIBO_WALK, KURIBO_STUN, KURIBO_DEAD };
enum { WALK_KURIBO, WALK_TAKOHEI, WALK_PACKUN, WALK_KOTEBUG, WALK_SPIDER, WALK_MERAMERA, WALK_TAKOBO, WALK_MKOOPA, WALK_KARIKARI, WALK_KMINI, WALK_KARON, WALK_TERESA, WALK_BUTTERFLY, WALK_STINKBUG, WALK_UNIZO, WALK_POIHANA };
static int kari_cling, kari_timer; /* Karikari clinging to Mario (KarikariDirector: 5 at most), frames to its damage */

typedef struct {
    V3 pos, up, hvel, face, ground_n, wall_n;
    float vup;
    int grounded, spin_timer, spin_used, hurt_timer;
    int jump_kind;      /* Petari's _430: 0-2 jump / 2nd / 3rd, 5 long jump, 6 back flip, 8 spin; -1 on ground */
    int land_timer;     /* frames since landing (_3CE), for the 2nd / 3rd jump window */
    int air_timer;      /* frames since take-off */
    int last_jump;      /* jump_kind of the last jump, kept after landing */
    int squat, wall;    /* squatting; touching a wall this frame (wall_n valid) */
    int wall_timer;     /* frames since the last wall contact */
    int low_grav;       /* frames left at the spin's low gravity */
    int slip_keep;      /* frames Mario may still stand on a 60-85 degree slope before it slips (Petari _41C) */
    int launch, launch_t; /* launch star carrying Mario (-1 none) and frames since it caught him */
    int hip, hip_t;     /* hip drop: HIP_START / HIP_FALL / HIP_LAND (0 none), frames in that phase */
    V3 launch_from;     /* where Mario was caught */
} Player;

/* SuperSpinDriver (Petari SuperSpinDriver.cpp, SpinDriverShootPath.cpp, ParabolicPath.cpp): bind radius 300,
 * ShootStart 45 frames (Mario pulled to the centre over 15), then Shoot along the path for mFlightTime (arg0,
 * default 300). Path: rails of at most 2 points are a parabola from the star to the rail's end whose top is the
 * rail's start (initFromMaxHeight); longer rails are followed by length with the star's offset eased out over the
 * first half. ponytail: rails are straight segments between control points (the game's are Bezier), no camera of
 * its own, no pull-in (arg2), no operate ring. SuperSpinDriverPink / Green are the same class (Green needs no star).
 * SpinDriver (SpinDriver.cpp): bind radius 300, ShootStart 20 frames; on a rail the same path for arg0 frames (default
 * 300), else Mario is thrown along its Y axis at arg0 (default 40) */
typedef struct {
    V3 pos;
    int flight, cool;
    int row;          /* placement row (appear switch) */
    int start;        /* ShootStart frames: 45 SuperSpinDriver, 20 SpinDriver */
    const float *pts; /* NULL: no rail, Mario leaves with `kick` */
    u32 npts;
    int parab;
    V3 py, pz, off;   /* parabola axes / rail start offset */
    float pa, pv, plen, len;
    V3 kick;
    int starter;      /* ScenarioStarter's flight: no ShootStart, its own motion times and first pose */
    float initrot;    /* its arg5: the first pose's turn about the path (calcInitPose) */
} Launch;
#define LAUNCH_BIND 300.0f

static void init_launch(Launch *L, const Row *r, const float *pts, u32 np, int parab);
static V3 launch_pos(const Launch *L, float t);

/* Mario constants from Petari MarioConst.cpp gMarioConstDefault (units per frame at 60 Hz). Indices of the arrays
 * are the jump kinds of MarioJump.cpp (tryJump _430, trySquatJump 5, tryBackJump 6, trySpinJump / tryTornadoJump 8) */
static const float JUMP_HEIGHT[9] = {22.0f, 26.0f, 36.0f, 24.0f, 32.0f, 15.0f, 32.0f, 22.0f, 1.0f}; /* mJumpHeight */
static const float JUMP_GRAV[12] = {1.8f, 1.8f, 1.8f, 1.8f, 1.8f, 0.95f, 1.8f, 2.2f, 0.4f, 0.9f, 1.8f, 0.5f}; /* mGravityJumping */
#define WALK_SPEED 13.0f          /* mWalkSpeed: full stick on the ground */
#define INERTIA_MAX 0.9f          /* mInertiaStandardMax: while the stick is held */
#define INERTIA_STOP 0.94f        /* mInertiaStandardStop: stick released */
#define INERTIA_SQUAT 0.95f       /* mInertiaSquat */
#define SQUAT_WALK 0.2f           /* mSquatWalkMaxSpeed (ratio of WALK_SPEED) */
#define JUMP_FRONT 12.5f          /* mJumpFrontSpeed (times the walk ratio) */
#define JUMP_CONNECT_TIME 7       /* mJumpConnectTime */
#define JUMP_CONNECT_SPEED 0.6f   /* mJumpConnectSpeed (walk ratio needed for the 3rd jump) */
#define SQUAT_JUMP_FRONT 16.5f    /* mSquatJumpFrontSpeed (long jump) */
#define SQUAT_JUMP_BACK 5.0f      /* mSquatJumpBackSpeed (back flip) */
#define LONG_JUMP_MIN 0.3f        /* trySquatJump when mWalkSpeed > 0.3 (MarioJump.cpp tryJump) */
#define JUMP_FRONT_RED 0.98f      /* mJumpFrontReduction, from mJumpFrontReductionBeginTime */
#define JUMP_FRONT_RED_T 15
#define SQUAT_FRONT_RED 0.99f     /* mSquatJumpFrontReduction, from mSquatJumpFrontReductionBTime */
#define SQUAT_FRONT_RED_T 30
#define MAX_DROP 35.0f            /* mMaxDropSpeed[0] */
#define MAX_JUMP_SPEED 32.0f      /* mMaxJumpSpeed: horizontal cap in the air */
#define TORNADO_FRONT 5.0f        /* mJumpTornadoSpeed */
#define SPIN_HEIGHT 12.0f         /* mSpinJumpHeight */
#define SPIN_AIR_TIME 24          /* mAirWalkTimeSpin: frames at the spin's gravity */
#define SPIN_TIME 30              /* mStartSpinTime: ground spin / cooldown */
#define WALL_JUMP_XZ 13.0f        /* mWallJumpPowerXZ */
#define WALL_JUMP_Y 30.0f         /* mWallJumpPowerY */
#define SLIP_ANGLE_COS 0.5f       /* mSlipAngle 60: steeper ground is a slope Mario slips down */
#define WALL_ANGLE_COS 0.087f     /* mForceWallAngle 85: steeper faces are walls; up to here they are ground */
#define SLIP_KEEP 15              /* Mario::updateFloorCode: frames on a slope before its floor code turns slip */
#define SLIP_ACCEL 1.2f           /* ponytail: slide speed gain per frame x sin(slope); Petari's slip move is not
                                   * ported, tune by eye */
#define SLIP_MAX 30.0f
#define WALL_KEEP 6               /* frames a wall contact stays valid (wall slide clip, wall kick) */
#define GROUNDING 20.0f           /* mGravityGrounding: downward step while on the ground keeps Mario on crests */
#define DROP_WAIT 5               /* mDropWaitTime: frames in the air before the fall clip (MarioMove.cpp:138) */
#define TURN_STAND 0.9f           /* mTurnAngleSpeed (rad per frame, standing) */
#define TURN_MOVE 0.15f           /* mTurnAngleSpeed2, above mFastTurnSpeed 0.1 (MarioMove.cpp:368-373) */
#define AIR_CONTROL 0.1f          /* ponytail: one air steering factor instead of Petari's air-walk timers */
#define GRAVITY_HIP_DROP 12.0f    /* mGravityHipDrop */
#define LIMIT_SPEED_HIP_DROP 150.0f /* mLimitSpeedHipDrop */
#define HIP_DROP_LIMIT_HEIGHT 100.0f /* mHipDropLimitHeight: no hip drop closer to the ground */
#define HIP_DROP_ZERO_GR_TIME 2   /* mHipDropZeroGrTime: frames hanging after the start clip */
enum { HIP_START = 1, HIP_FALL, HIP_LAND };
#define BODY_RADIUS 50.0f        /* ponytail: one sphere for Mario's body; SMG uses several collision checks */
#define HIT_BODY_H 80.0f          /* Mario's hit sensor for touches: centre this high, radius HIT_BODY_R (the
                                   * collision sphere is lower and smaller); ponytail: one sphere, not his sensors */
#define HIT_BODY_R 70.0f

static const Scene *sc;
/* Placed static models (planets and every class without behaviour yet): drawn, and collided if they have a KCL */
typedef struct {
    const Model *md;
    Anim *anim;      /* skinned models (slice.py SKINNED_OBJ): its pose, from obj_anim */
    const unsigned char *kcl;
    float m[16];     /* world matrix (uniform scale s) */
    float s, reach;  /* scale, bounding radius in world units */
    int zone;
    int row;         /* OBJS index */
    int breaks;      /* breakable (breakable()): a spin breaks it, then it drops its coins / star pieces */
    int collapse;    /* CollapsePlane: frames from standing on it to vanishing (arg0, default 140), 0 if not one */
    int hazard;      /* 1 SpaceMine (explodes on touch), 2 NeedlePlant (pricks on touch), 0 none */
    int item;        /* touch to take: ITEM_STAR, ITEM_ONEUP, ITEM_LIFEUP, ITEM_QCOIN; 0 none */
    int firebar;     /* FireBar core: its balls turn around it and burn Mario */
    int invisible;   /* InvisiblePolygonObj (InvisibleWall*): collision only, never drawn */
    int kcl_ready;   /* plants: the scene's collision is bound (set at the end of game_rebind) */
    int flip;        /* FlipPanel / FlipPanelReverse: row_step[row] holds FLIP_* bits */
    const Model *md_flip[2]; /* its "#PanelA" ("!", turned) and "#PanelEnd" (group complete) models (slice.py EXTRA) */
    int npc;         /* talking NPC (npc_class): turns to face Mario when he is near */
    int hipsw;       /* hip drop switch: 1 PicketSwitch(Big) / HipDropSwitch (SW_A for good), 2 HipDropTimerSwitch */
    int rotor;       /* map part with a RotateSpeed: turns (rotor_matrix), carries Mario */
    int sky;         /* sky dome (Petari Sky): centred on the camera, drawn first */
    int steam;       /* LavaSteam: vents on a cycle and burns (update_steam) */
    int geyser;      /* LavaGeyser: shoots up on a cycle and burns (update_geyser, geyser_height) */
    int assembly;    /* AssemblyBlock: floats at its home, flies into place when Mario is near (assembly_place) */
    V3 gup;          /* AssemblyBlock: up at its home (its bobbing) */
    int jstand;      /* JumpStand: holds Mario landing on it, then throws him up (update_jumpstands) */
    int warppod;     /* WarpPod: takes Mario to its pair (warp_try, warp_update) */
    int kiraira;     /* Kiraira: a chained mine, explodes on touch and recovers (update_kiraira, kiraira_pos) */
    const float *meteor; /* MeteorStrike(Environment): its rail's JMap points; meteors fall along them (update_meteor) */
    int killer;      /* HomingKiller launcher: shoots a bullet that may chase Mario (update_killers) */
    int gcap;        /* GCaptureTarget: the pointer (R2) pulls Mario to it (gcap_try, gcap_update) */
    int tspot;       /* TreasureSpot 1 / CoinFlower 2: a star piece shot pops its coin (update_shots) */
    int beeflower;   /* BeeFlowerHover: turns; Mario (never a bee here) stepping on it goes through (update_beeflowers) */
    const unsigned char *ticorail; /* TicoRail: its PATH entry; the Luma strolls along it (update_ticorails) */
    int jbranch;     /* JumpBranch: Mario grabs it in the air and swings round it (jbranch_try, jbranch_update) */
    int giver;       /* DeadLeaves 1 / Pompon2Plant, Pompon4Plant 2: their item comes out once (update_givers) */
    int biri;        /* BigBirikyu 1 / Birikyu 2: an electric ball on its rail or circling (update_birikyu) */
    const unsigned char *biri_rail;
    int punch;       /* PunchingKinoko: a spin punches its head away on its stalk, it swings back (update_punch) */
    V3 head, hvel;   /* PunchingKinoko: its head (GroundChecker) and its velocity; its own matrix in m0.
                      * CocoNut: head its centre, m0 its turned rotation */
    float m0[16];
    int coco;        /* CocoNut: a spin / hip drop / kick sends it rolling (update_coconuts) */
    int sambo;       /* CocoSambo: a thrown coconut knocks its head down, then a stomp / spin (update_sambos) */
    int fat;         /* TicoFat: fed by star piece shots (update_shots), full: SW_A, the row dies */
    int bubble;      /* AirBubble 1 (touched: oxygen), AirBubbleGenerator 2 (update_bubbles) */
    int stray;       /* StrayTico: touched it follows Mario; all of its zone's: the CollectTico's star (update_strays) */
    int fountain;    /* FountainBig: a water column on a cycle, Mario in it is thrown up (update_fountains); m0 its matrix */
    int snowman;     /* SnowMan: a fire ball melts it (update_snowmen) */
    int candle;      /* PhantomCandlestand 1 / TeresaMansionCandlestand 2: burns Mario, a spin puts it out (update_candles) */
    int screw;       /* ScrewSwitch 1 / ScrewSwitchReverse 2: a spin on it screws it, then its SW_A (update_screws) */
    int jet;         /* JetTurtle: Mario takes it, a spin throws it (update_jets); head its place, hvel its velocity */
    int fluff;       /* Fluff: head its place, hvel its velocity, m0 its home (update_fluffs, fluff_*) */
    int cocoon;      /* SpaceCocoon: head its cocoon, hvel its spring velocity, m0 its base (update_cocoons, cocoon_*) */
    V3 cfront;       /* CocoNut: _94 its rolling direction, cspd _8C its speed, cfall _90 its fall speed */
    float cspd, cfall;
    int cstrong;     /* CocoNut: _13C sent by a spin: light gravity, blows walkers away, until it lands */
    const float *prom; u32 prom_n; /* LavaProminence: its rail (polyline); a lava ball rides it (update_prom) */
    const unsigned char *tsuki; /* Tsukidashikun: its PATH entry; pushes along it and back (update_tsuki) */
    int shoot;       /* ShootingStar: falls, bursts into star pieces (update_shooting) */
    int plant;       /* PlantGroup (Flower / FlowerBlue / CutBush Group): its model drawn at each member (plants) */
    const unsigned char *railer; /* map part on a rail: its PATH entry; moves along it (update_railers) */
} Inst;
enum { ITEM_STAR = 1, ITEM_ONEUP, ITEM_LIFEUP, ITEM_QCOIN, ITEM_CHIP, ITEM_KEY, ITEM_FIRE, ITEM_BEE };
static V3 firebar_ball(const Inst *in, int j, int k);
static V3 star_pos(const Inst *in, int *state);
static void basis_matrix(float m[16], V3 pos, V3 up, V3 fwd, float angle);
/* a one-shot effect standing on `up` at p */
static void fx_at(int fx, V3 p, V3 up)
{
    float m[16];
    basis_matrix(m, p, up, v3(0, 0, 1), 0);
    fx_emit(fx, m);
}
#define MAX_ZONES 64

/* Stage switches (Petari StageSwitch.cpp): ids below 1000 belong to the row's zone (128 each), the rest are the
 * galaxy's (id - 1000). One bit each, all off at the start; sw_changed makes the frame rebind what appear switches
 * show. ponytail: switches are not saved */
#define SW_PER_ZONE 128
static unsigned char sw_bits[(MAX_ZONES + 1) * SW_PER_ZONE / 8];
static int sw_changed;

static int sw_bit(const Row *r, int k, int add)
{
    int id = r->sw[k], n = (id >= 1000 ? id - 1000 : id) + add;
    if (id < 0 || n >= SW_PER_ZONE || r->zone >= MAX_ZONES) return -1;
    return (id >= 1000 ? 0 : (r->zone + 1) * SW_PER_ZONE) + n;
}

static int sw_on_n(const Row *r, int k, int add)
{
    int b = sw_bit(r, k, add);
    return b >= 0 && (sw_bits[b >> 3] >> (b & 7) & 1);
}
#define sw_on(r, k) sw_on_n(r, k, 0)

static void sw_set(const Row *r, int k, int on)
{
    int b = sw_bit(r, k, 0);
    if (b < 0 || (sw_bits[b >> 3] >> (b & 7) & 1) == on) return;
    sw_bits[b >> 3] ^= 1 << (b & 7);
    sw_changed = 1;
}
static unsigned short *row_step; /* per OBJS row: CollapsePlane frames since Mario stood on it (0: not yet) */
static float *row_angle;         /* per OBJS row: a rotating map part's angle (degrees) */
static signed char *row_turn;    /* per OBJS row: its direction (+1 / -1, 0 not started) and */
static unsigned short *row_hold; /* frames it still holds at the end of its turn */
static float *row_coord, *row_speed; /* per OBJS row: a rail-moving map part's distance along its rail, its speed */
static signed char *row_rdir;    /* its direction (+1 / -1; 0 not started, 2 stopped) */
static unsigned short *row_rhold; /* frames it still waits at a rail point */
static const unsigned char *scene_blob_g; /* the scene blob (PATH) for rebinds */
static int hip_hit, hip_inst;   /* Mario landed a hip drop this frame, on instance hip_inst (-1 none) */
static int mario_ground = -1;   /* instance Mario stands on this frame, -1 in the air */
static float cur_x = 320, cur_y = 224; /* the star pointer's stand-in, moved by the right stick (HUD units) */
static struct { int phase, t, target; V3 vel; float power; } gcap; /* GCapture (gcap_update) */
static int test_r2; /* test builds: R2 held */
static struct { int inst, cool; V3 pt, vel, side, front; } jb = {.inst = -1}; /* JumpBranch (jbranch_update) */
static struct { int inst, phase, t; } jet = {.inst = -1}; /* the JetTurtle Mario holds or threw (update_jets) */
static struct { int inst, phase, t, spins; } flf = {.inst = -1}; /* the Fluff Mario rides (fluff_update) */
static struct { int inst, phase, t, wait; V3 vel; } coc = {.inst = -1, .wait = -1}; /* SpaceCocoon Mario hangs on (cocoon_update) */
typedef struct { V3 pos, vel; int t; } Shot; /* star pieces shot at the pointer (update_shots) */
#define MAX_SHOTS 8
static Shot shots[MAX_SHOTS];
static int test_l1; /* test builds: L1 pressed this frame */
static int test_sq; /* test builds: Square pressed this frame (Fluff) */
static int test_feed; /* test builds: the pointer on a TicoFat with L1 held */
static int nfats; /* TicoFats seen (update_ticofat's table) */
enum { CS_HIDEWAIT, CS_APPEAR, CS_WAIT, CS_HIDE, CS_ATTACK, CS_ATTACKINT, CS_FALLHEAD, CS_FALLHEADHIDE, CS_RECOVERWAIT,
       CS_RECOVER, CS_PRESSY, CS_BLOW, CS_DEAD };
enum { CH_CONNECTED, CH_FALL, CH_FALLLAND, CH_SWOON, CH_SWOONEND, CH_BLOW };
#define MAX_SAMBOS 4
static struct {
    int row, nrv, t, hnrv, ht;
    V3 front, up, pos, hpos, hvel, hfront;
    float s;
    Anim *head;
} sambos[MAX_SAMBOS];
static int nsambos;
static const Model *m_sambohead;
static int test_cross; /* test builds: X held (Bee Mario) */
static unsigned char zone_kcl[64]; /* zones with collision bound (game_rebind): walkers wait in the others */
static int test_nolost; /* test builds: no miss for falling off (Mario sent to a zone still streaming in) */
static void notes_spawn(void);
static int note_rail(const Row *r, const unsigned char **e);
static int fire_time; /* Fire Mario: frames left (mFireModeTime 1200), 0 normal */
/* DinoPackun (Petari Boss/DinoPackun*.cpp, the first fight: DinoPackunVs1): one per scene, its state here */
static struct {
    int row, phase, nrv, t, hits;  /* phase: 0 wait (SW_A), 1 egg, 2 battle, 3 punched (the ball out), 4 demo, 5 down, 6 gone */
    float side;                    /* the way it turns (selectTurnDirection) */
    V3 pos, vel, up, f;
    int bstate, bt;                /* its tail ball: 0 Wait, 1 Shoot, 2 Charge, 3 Reverse, 4 Rebound */
    V3 bpos, bvel;
    int seq, nt, dnrv, dt, ddir;   /* DinoPackunVs1's step, its nerve's frames, its damage state, its frames, the hit's side
                                    * (1 left, 2 right, 3 top) */
    int prev_nrv, react;           /* the nerve a punch interrupted; a star piece's hit reaction (frames left) */
    V3 chain[9];                   /* its tail: TailRoot .. TailBall, model space (DinoPackunTail) */
    int chain_ok;
    Anim *anim;
} dino = {.row = -1};
static const Model *m_dino, *m_dball, *m_degg, *m_deggbrk;
/* OtaKing (Petari Boss/OtaKing.cpp, Enemy/CocoNutBall.cpp, Enemy/FireBall.cpp, Enemy/FireBubble.cpp): one per scene */
#define OTA_BALLS 3
#define OTA_FIRES 6
#define OTA_BUBBLES 6
static struct {
    int row, nrv, t, hits, ec;       /* its nerve (OTA_*), the nerve's frames, mHits, _EC (coconuts thrown) */
    float yaw, turn;                 /* mRotation.y from its placed front (degrees), mTurnSpeed */
    float c0, c4;                    /* the coconuts' throw height range (arg0 _C0 450, arg2 _C4 0) */
    Anim *anim, *foot[2];            /* its body, its front feet (OtaKingFoot: mFeet[0] the R clips, [1] the L ones) */
    V3 pos, up, f, f0;               /* f: its front now, f0: its placed front */
    struct { V3 pos, vel, grav, up; int state, t, rally, alt; } ball[OTA_BALLS]; /* CocoNutBall: 0 dead, 1 Throw, 2
                                      * HitBackToHost, 3 HitBackToPlayer, 4 Rebound; rally _9C, alt _BD, grav _90 */
    struct { V3 pos, vel, up; int alive; } fire[OTA_FIRES];                     /* FireBall (Throw) */
    struct { V3 pos, vel, front; int state, t, act, chase; } bub[OTA_BUBBLES];  /* FireBubble: 0 dead, 1 Appear, 2
                                      * Wait, 3 Chase, 4 Down */
} ota = {.row = -1};
enum { OTA_WAITONSWITCH, OTA_APPEARDEMO, OTA_WAIT, OTA_THROWCOCOWAIT, OTA_THROWCOCO, OTA_THROWFIREWAIT, OTA_THROWFIRE,
       OTA_DAMAGE, OTA_POWERUP, OTA_DOWN, OTA_DOWNDEMO, OTA_APPEARSTAR, OTA_DEAD, OTA_HITBACKSTART, OTA_HITBACK,
       OTA_NEARATTACK, OTA_BUBBLEATTACK };
static const Model *m_ota, *m_otafoot, *m_coco, *m_fireball2, *m_firebubble;
#define MAX_BUBBLES 8 /* AirBubbleHolder's free bubbles (update_bubbles) */
static struct { V3 pos, vel; int t, life; } bubbles[MAX_BUBBLES];
static int time_limit, time_left; /* a Red / Black comet's limit and frames left (CometEventExecutorTimeLimit), 0 none */
static int swimming, oxygen = 3600; /* Mario in water (swim_update); oxygen left (mOxygenMax 3600) */
static float swim_pitch; /* his swim direction's angle over the horizontal (radians) */
static V3 stray_at[64]; /* StrayTico: where the rescued ones are (kept over a rebind), by row % 64 */
static int bee_mode, bee_meter; /* Bee Mario (MarioBee.cpp): on until hurt; flight left (mAirWalkTime 180) */
static float bee_rise, bee_drop; /* -_770 while flying (up to mBeeUpSpeedMax 5), its free drop (to mBeeFreeDropMaxSpd 5) */
#define MAX_FIREBALLS 3 /* FireMarioBall: MarioActor's _B54 */
static struct { V3 pos, vel; int t, wall; } fireballs[MAX_FIREBALLS];
static const Model *m_marioball, *m_bubble;
#define MAX_NOTES 512 /* NoteFairy notes (update_notes) */
static struct { V3 pos; float coord; short row, alive; } notes[MAX_NOTES];
static int nnotes;
#define MAX_WPLANTS 1024 /* WaterPlant (place_waterplants, draw_waterplants) */
static struct { V3 pos, up, dir; float h; short row, type; } wplants[MAX_WPLANTS];
static int nwplants;
typedef struct { int row; V3 pos, front; int freeze; } Killer; /* HomingKiller: its bullet (update_killers) */
typedef struct { int first, n; const float *pts; u32 np; float len, step, speed; } Flow; /* StarPieceFlow (update_flows) */
#define MAX_FLOWS 64
static Flow flows[MAX_FLOWS];
static int nflows;
#define MAX_KILLERS 32
static Killer killers[MAX_KILLERS];
static int nkillers;
static int ground_inst = -1;     /* instance under the last collide_n's ground normal, -1 none */
static void zone_spheres(void);
static void zone_grow(const Inst *in);

/* Breakable classes and what they drop (Petari PunchBox.cpp, WoodBox.cpp, CrystalCage.cpp, HipDropRock.cpp):
 * PunchBox / WoodBox arg0 coins (default 1), WoodBox also arg2 star pieces; CrystalCageS / M one hit, a coin when the
 * item inside (arg7, DummyDisplayModel) is 0; HipDropRock (a hip drop only) a coin when arg0 is 0, arg1 star pieces
 * (default 6) when it is 1; ItemBlock (ItemBlock.cpp, hit from below): TimerCoinBlock 10 coins, SplashCoinBlock arg0
 * (default 8), TimerPieceBlock 20 star pieces, SplashPieceBlock arg0 (default 8), ItemBlockSwitch nothing (its dead
 * switch). Returns 1 (spin or hip drop), 2 (hip drop only), 3 (hit from below) or 0 for anything else. ponytail:
 * a timer block gives everything on its first hit. CrystalCageL takes 2 hits 20 frames apart (its star: star_pos);
 * BreakableCage(Rotate / L) breaks on a spin too (ponytail: in SMG only what Mario rides or throws breaks it) */
static int breakable(const char *name, const Row *r, int *coins, int *pieces)
{
    *coins = *pieces = 0;
    if (strcmp(name, "PunchBox") == 0 || strcmp(name, "WoodBox") == 0) {
        *coins = r->arg[0] >= 0 ? r->arg[0] : 1;
        if (name[0] == 'W' && r->arg[2] > 0) *pieces = r->arg[2];
        return 1;
    }
    if (strcmp(name, "CrystalCageS") == 0 || strcmp(name, "CrystalCageM") == 0) {
        *coins = r->arg[7] == 0;
        return 1;
    }
    if (strcmp(name, "CrystalCageL") == 0 || strcmp(name, "BreakableCage") == 0 || strcmp(name, "BreakableCageRotate") == 0 ||
        strcmp(name, "BreakableCageL") == 0)
        return 1;
    if (strcmp(name, "TimerCoinBlock") == 0 || strcmp(name, "SplashCoinBlock") == 0 ||
        strcmp(name, "TimerPieceBlock") == 0 || strcmp(name, "SplashPieceBlock") == 0 || strcmp(name, "ItemBlockSwitch") == 0) {
        int n = name[0] == 'T' ? (name[5] == 'C' ? 10 : 20) : r->arg[0] >= 0 ? r->arg[0] : 8;
        if (name[0] == 'I') n = 0;
        if (strstr(name, "Coin")) *coins = n; else *pieces = n;
        return 3;
    }
    /* SimpleBreakableObj.cpp: IcicleRock and SandUpDownTowerBreakableWallA / B break at a spin (mBreakFlags 2),
     * TeresaMansionHoleCover at a hip drop (4); arg3 -1 arg6 coins (1), 3 arg6 star pieces (5). ponytail: no
     * 1-up / power star items, KoopaStatue / ConcreteBlock / SnowBlockA (explosions, ball dash, fire) stay whole */
    if (strcmp(name, "IcicleRock") == 0 || strncmp(name, "SandUpDownTowerBreakableWall", 28) == 0 ||
        strcmp(name, "TeresaMansionHoleCover") == 0) {
        if (r->arg[3] == -1) *coins = r->arg[6] > 0 ? r->arg[6] : 1;
        else if (r->arg[3] == 3) *pieces = r->arg[6] > 0 ? r->arg[6] : 5;
        return name[0] == 'T' ? 2 : 1;
    }
    if (strcmp(name, "HipDropRock") == 0) {
        *coins = r->arg[0] == 0;
        *pieces = r->arg[0] == 1 ? (r->arg[1] >= 0 ? r->arg[1] : 6) : 0;
        return 2;
    }
    return 0;
}
static unsigned char *row_dead; /* per OBJS row: broken, never comes back (zone swaps rebind from the rows) */
/* per OBJS row: a PowerStar some event brings out (declarePowerStar / requestAppearPowerStar; slice.py STAR_DECLARERS
 * puts the declarer's row in its arg7): that row + 1, -1 a hundred coins, 0 none. Out once the declarer is done
 * (row_dead), then row_step counts its appear demo (star_pos) */
static short *star_wait;

/* A row with an appear switch is out only while it is on (syncStageSwitchAppear); a dead row never again */
#if defined(METEOR_TEST)
#define TEST_SHOWN "MeteorStrike"
#elif defined(KILLER_TEST)
#define TEST_SHOWN "HomingKiller"
#elif defined(MKOOPA_TEST)
#define TEST_SHOWN "MechanicKoopaMini"
#endif
/* SuperSpinDriver.cpp: one in a group (惑星出現, a TicoFat's) waits dead until the group is called (TicoFat full) */
#define MAX_GROUP_WAIT 16
static struct { int row, fat; } group_wait[MAX_GROUP_WAIT];
static int ngroup_wait;
static int row_shown(int i)
{
    const Row *r = &sc->objs[i];
    for (int k = 0; k < ngroup_wait; k++)
        if (group_wait[k].row == i && !row_dead[group_wait[k].fat]) return 0;
#ifdef TEST_SHOWN /* a test build's class out at once (its appear switch belongs to a fight or an event) */
    if (strcmp(sc->types + 32 * r->type, TEST_SHOWN) == 0) return 1;
#endif
    /* SleepControlFunc: SW_SLEEP off makes it dead, on appeared (TicoFat's planet) */
    return !row_dead[i] && (r->sw[SW_APPEAR] < 0 || sw_on(r, SW_APPEAR)) && (r->sw[SW_SLEEP] < 0 || sw_on(r, SW_SLEEP));
}

/* The row's object is gone for good: its dead switch goes on */
static void row_kill(int i)
{
    row_dead[i] = 1;
    sw_set(&sc->objs[i], SW_DEAD, 1);
}

/* Coins (Coin.cpp, CoinGroup.cpp) also go when their SW_B turns on. take: can Mario take it now (else: is it drawn) */
static int pickup_on(const Pickup *p, int take)
{
    if (p->alive == PICK_GONE || p->alive == (take ? PICK_POP_NEW : PICK_POP_BLINK)) return 0;
    return p->row < 0 || (row_shown(p->row) && !sw_on(&sc->objs[p->row], SW_B));
}
#define pickup_shown(p) pickup_on(p, 0)
static int box_pieces;          /* star pieces WoodBoxes hold (arg2) */
/* Coins popping out of what broke (Petari CoinHolder::appearCoinPop, Coin::exeMove): 25 against gravity plus up to 4
 * at random, gravity 1, 0.75 rebound off the ground (impacts of 2 and more), 0.9 / 0.995 damping on the ground / in
 * the air, not to be taken for 30 frames, blinking at the end, gone after 600 */
typedef struct { int coin, t; V3 vel; } Pop;
static Pop *pops;
static int npops, box_coins;
static Inst *insts;
static Anim *obj_anim(int row, const Model *md, const char *cls);
/* A demo's camera (CameraAnim::calc, startAnimCameraTargetSelf): the CANM `name`'s frame in the actor's frame `m`
 * (side, up, front, position), the roll about the view; 0 once it is over (or missing: no override) */
static int demo_camera(const char *name, int frame, const float m[16])
{
    const u8 *q = sc->canm;
    for (u32 i = 0; i < sc->ncanm; i++) {
        u32 n = *(const u32 *)(q + 32);
        if (!strcmp((const char *)q, name)) {
            if (frame < 0 || (u32)frame >= n) { camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45); return 0; }
            const float *f = (const float *)(q + 36) + 8 * frame;
            V3 ax = v3(m[0], m[1], m[2]), ay = v3(m[4], m[5], m[6]), az = v3(m[8], m[9], m[10]), o = v3(m[12], m[13], m[14]);
            V3 eye = vadd(o, vadd(vadd(vmul(ax, f[0]), vmul(ay, f[1])), vmul(az, f[2])));
            V3 watch = vadd(o, vadd(vadd(vmul(ax, f[3]), vmul(ay, f[4])), vmul(az, f[5])));
            /* CameraAnim: the up is the target's Y (setUpVec), rolled by the twist; a view along it uses its front */
            V3 fwd = vnorm(vsub(watch, eye)), up0 = vsub(ay, vmul(fwd, vdot(ay, fwd)));
            if (vlen(up0) < 0.05f) up0 = vsub(vmul(az, -1.0f), vmul(fwd, vdot(vmul(az, -1.0f), fwd)));
            up0 = vnorm(up0);
            V3 side = vcross(fwd, up0);
            float r = f[6] * 0.0174533f;
            V3 up = vnorm(vadd(vmul(up0, cosf(r)), vmul(side, sinf(r))));
            camera_set_override(1, eye, watch, up, f[7] > 1.0f ? f[7] : 45.0f);
            return 1;
        }
        q += 36 + 32 * n;
    }
    return 0;
}
static int demo_on; /* a demo is running: Mario waits (no input, no hurt) */
static int dino_demo_on, ota_demo_on; /* whose demo camera runs */
static int demo_dino_damage; /* tryStartDemoMarioPuppetable "ディノパックンダメージ": Mario waits from Pulled to its damage's end */
static int star_demo;      /* PowerStar::exeStageClearDemo: frames since a star was taken + 1, 0 none */
static float star_demo_m[16]; /* its camera's frame: at the star, facing the way Mario looked */
static int obj_play(const Inst *in, const char *clip, int loop, int interp);
/* Skinned placed models (slice.py SKINNED_OBJ): a pool of poses kept by row across rebinds, the class's "wait" clip
 * (else its first) looping unless its behaviour plays another (obj_play). ponytail: 40 at once (instances and walkers), the rest drawn in
 * the bind pose of the nearest free one's skin is skipped (not drawn) */
#define MAX_OBJ_ANIM 40
static struct { int row; unsigned last; Anim *a; } oanim[MAX_OBJ_ANIM];
static int ninsts;
static const Gravity *grav;
static int ngrav;
static Pickup *coins, *purple, *pieces;
static int ncoins, npurple, npieces;
static Kuribo *kuribo;
static int nkuribo;
static Player *mario;
static V3 start_pos;
#define MAX_LIFE 3              /* Petari: 3 life, a coin gives one back; a LifeUp mushroom makes it 6 until a miss */
#define START_LIVES 4
static int life = MAX_LIFE, max_life = MAX_LIFE, misses, lives = START_LIVES, stars;
static void miss(void);
static int got_coins, got_purple, got_pieces;
static float spin_angle;
static V3 cam_pos;
static const Model *m_coin, *m_purple, *m_piece, *m_kuribo, *m_takohei, *m_packun, *m_kotebug, *m_spider, *m_meramera, *m_takobo, *m_mkoopa, *m_kari, *m_kmini, *m_karon, *m_teresa, *m_bfly, *m_stink, *m_unizo[3], *m_note, *m_poihana;
static const Model *m_mario_skin; /* Mario.arc, skinned (SKIN chunk) */
static Anim *mario_anim;
/* ElectricRail (Petari ElectricRail.cpp, ElectricRailHolder.cpp): one segment per pair of rail points, arg0 rails
 * stacked 100 apart against gravity (default 1) */
typedef struct { V3 a, b; int h; } Spark;
static Spark *erails;
static int nerails;
static const Model *m_erail, *m_fireball, *m_rock;
/* RockCreator (Petari RockCreator.cpp, Rock.cpp): rocks of radius 225 * scale roll along its rail at arg0 (default 10)
 * a frame, one every arg1 seconds, or a single one when arg1 is unset; a touch hits Mario hard, a spin breaks the rock;
 * its appear switch shows it, SW_A ends it. ponytail: at most ROCKS_PER on a rail, a single rock starts over at the
 * rail's end, no star pieces from a broken rock, no bounce, no Wanwan */
#define ROCKS_PER 4
typedef struct { const float *pts; u32 np; float len, d; int row, alive; } Roller;
/* IronCannonLauncherPoint / IronCannonLauncher (Petari IronCannonShell.cpp, KoopaJrShipCannonShell.cpp): every arg0
 * frames (default 300) a shell leaves 75 (the launcher: 100) along its z at arg1 a frame (default 30), at most 3 out;
 * SW_A, when set, must be on (the Point); its appear switch shows it. A shell flies straight for 360 frames and
 * explodes on what it hits (after 10 frames) or on Mario (hurt); stomped, it drops under gravity (0.55 rebound, 0.95
 * / 0.99 damping) and misfires after 45. The Point's own model is the shell (its archive). ponytail: no star pointer
 * freeze, launchers fire only within 8000 of Mario */
#define SHELLS_PER 3
typedef struct { V3 pos, vel, dir; int row, t, state; } Shell; /* state 0 none, 1 flying, 2 stomped */
static Shell *shells;
static int nshells;
static const Model *m_shell;
static Roller *rollers;
static int nrollers; /* ROCKS_PER per creator, consecutive */
static V3 rail_at(const float *p, u32 n, float d);
static unsigned game_frame;      /* frames since the scene started (FireBar angles) */
/* EarthenPipe (Petari EarthenPipe.cpp): pipes sharing arg3 are a pair; the top is 100 * scale.y along the pipe's up
 * (-gravity, or its Y axis when arg6); a binder of radius 70 at 50 below the top takes Mario if he is within 50 of
 * the axis or above the top, and the partner throws him out with Y * arg1 + Z * arg0 (default 20, 5). A pipe works
 * again once Mario stood on ground 30 frames after the warp. ponytail: instant warp (no in / out clips, no hidden
 * pipes rising, no camera), no water pipes' stream */
typedef struct { V3 top, up, front; float hf, vf; int id, partner, zone, row, mode; } Pipe; /* mode arg2: 1 / 2
                    * hidden until the partner is used (row_step[row] 1 while out), 1 hides again after the exit */
static int pipe_out = -1; /* the mode 1 pipe Mario came out of, hidden once the pipes re-arm */
static Pipe *pipes;
static int npipes, pipe_wait, pipe_zone = -1;
/* Switch logic rows (no model): TimerSwitch, SwitchSynchronizerReverse, GroupSwitchWatcher */
enum { LOGIC_TIMER = 1, LOGIC_REVERSE, LOGIC_GROUP, LOGIC_BLOCKER, LOGIC_FLIPS, LOGIC_CHIPS, LOGIC_SWAREA, LOGIC_DEATH, LOGIC_RESTART, LOGIC_PIECESPOT };
static int area_shape(const char *name);
/* FlipPanel (Petari FlipPanel.cpp): row_step bits. Back = flipped (ACTMES_SUCCESS to the observer); a flip locks the
 * panel until Mario stood on other ground; it flips once he has stood on it 3 frames (checkPlayerOnTop). Done: the
 * group is complete (FlipPanelNrvEnd) */
enum { FLIP_BACK = 1, FLIP_LOCK = 2, FLIP_DONE = 4, FLIP_STEP = 8 };
typedef struct { int row, kind; } Logic;
static Logic *logic;
static int nlogic; /* pipe_zone: the exit pipe's zone, wanted in full */
/* PlantGroup.cpp: arg0 members (default 7) in hexagonal rings 160 apart (ring k: 6k of them), each dropped onto
 * the ground along gravity (a 1000 ray from 100 above), dropped when nothing is under it. Placed once per group, the
 * first rebind its zone has collision (plant_row; plant_state 0 not yet, 1 placed). arg1 of the members (shuffled)
 * hide a coin, or a star piece when arg2 is 1: Mario moving through one (within 50 * scale_y of his touch sphere,
 * PlantMember::tryPush) or spinning by it brings it out (update_plants). ponytail: the ray is stepped spheres; no
 * shaking, the star piece is dropped, not thrown */
typedef struct { V3 pos, up; int ok, item; } Plant;
static Plant *plants;
static int *plant_first;           /* per OBJS row: index of its first member in plants, -1 not a group */
static unsigned char *plant_state; /* per OBJS row */
static int plant_count(const Row *r);
static int stone_row(const Row *r);
static Launch *launches;
static int nlaunches;
static int starter; /* ScenarioStarter's flight (a launch, -1 none): Mario flies it in at the scenario's start */
static short *row_camset; /* per OBJS row: its CameraSetId (CSET chunk, ActorCameraInfo), -1 none */
static int ground_flips, slip_frames;
#ifdef SLOPE_TEST
V3 slope_n;
#endif /* bench: ground <-> air changes (flicker) and frames spent slipping */

/* Interpolation frames per clip from Petari's marioAnimeTable (MarioAnimatorData.hpp, XanimeGroupInfo _8):
 * locomotion (基本) and squat 6, fall 15, long jump and its landing 6, back flip and its landing 6, triple-jump
 * landing 6, wall slide 10; jumps, plain landings, spins and the wall kick snap (0). */
static void mario_play(const char *clip, float speed, int loop)
{
    static const struct { const char *clip; int frames; } t[] = {
        {"wait", 6}, {"walk", 6}, {"run", 6}, {"squatwait", 6}, {"squatwalk", 6}, {"fall", 15}, {"jumpbroad", 6},
        {"jumpbroadland", 6}, {"jumpback", 6}, {"jumpbackland", 6}, {"jumprollland", 6}, {"wallslide", 10}, {"slidehipforward", 6}};
    int f = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (strcmp(t[i].clip, clip) == 0) f = t[i].frames;
    anim_play(mario_anim, clip, speed, loop, f);
}

/* --- placement ------------------------------------------------------------------------------------------- */

static V3 row_pos(const Row *r) { return v3(r->pos[0], r->pos[1], r->pos[2]); }

/* SMG placement axes (rotation Z * Y * X, degrees), same as render.c mat_translate_rot_scale */
static void row_axes(const Row *r, V3 *x, V3 *z)
{
    float m[16], one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, r->pos, r->rot, one);
    *x = v3(m[0], m[1], m[2]);
    *z = v3(m[8], m[9], m[10]);
}

static int count_type(const char *name, int per_row_arg0)
{
    int t = scene_type(sc, name), n = 0;
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == t)
            n += per_row_arg0 ? (sc->objs[i].arg[0] > 0 ? sc->objs[i].arg[0] : 0) : 1;
    return t < 0 ? 0 : n;
}

/* a rail from the PATH chunk: {u16 zone, u16 closed, s32 id, u32 npts, u32 nraw, s32 stop_type, s32 pad, f32
 * pts[npts][3], f32 raw[nraw][3], nraw x {f32 coord, s32 arg0, s32 arg5}}: the polyline along it (Bezier parts sampled
 * by tools/slice.py), or with raw its JMap points (nraw of them) */
static const unsigned char *find_path(const unsigned char *blob, int zone, int id)
{
    const unsigned char *p = blob_chunk(blob, "PATH", NULL);
    u32 n = *(const u32 *)p;
    p += 4;
    for (u32 i = 0; i < n; i++) {
        u32 np = *(const u32 *)(p + 8), nr = *(const u32 *)(p + 12);
        if (*(const int *)(p + 4) == id && *(const unsigned short *)p == zone) /* rail ids are per zone */
            return p;
        p += 24 + 12 * np + 24 * nr;
    }
    return NULL;
}

static const float *find_rail_of(const unsigned char *blob, int zone, int id, u32 *npts, int *closed, int raw)
{
    const unsigned char *p = find_path(blob, zone, id);
    if (!p) return NULL;
    u32 np = *(const u32 *)(p + 8), nr = *(const u32 *)(p + 12);
    *npts = raw ? nr : np;
    *closed = *(const unsigned short *)(p + 2);
    return (const float *)(p + 24 + (raw ? 12 * np : 0));
}
#define find_rail(blob, zone, id, npts, closed) find_rail_of(blob, zone, id, npts, closed, 0)

/* RailCoin::placementNormalRail: count coins evenly spaced by length (loop rails: length / n).
 * ponytail: straight segments between control points; the game's rails are Bezier curves. */
static int place_on_rail(Pickup *out, const float *pts, u32 np, int closed, int count)
{
    float total = 0;
    u32 segs = closed ? np : np - 1;
    for (u32 i = 0; i < segs; i++) {
        const float *a = pts + 3 * i, *b = pts + 3 * ((i + 1) % np);
        total += vlen(v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
    }
    float step = count <= 1 ? 0 : total / (closed ? count : count - 1);
    for (int c = 0; c < count; c++) {
        float d = step * c;
        for (u32 i = 0; i < segs; i++) {
            const float *a = pts + 3 * i, *b = pts + 3 * ((i + 1) % np);
            V3 ab = v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
            float l = vlen(ab);
            if (d <= l || i == segs - 1) {
                float t = l > 0 ? (d < l ? d : l) / l : 0;
                out[c].pos = v3(a[0] + ab.x * t, a[1] + ab.y * t, a[2] + ab.z * t);
                out[c].alive = 1;
                break;
            }
            d -= l;
        }
    }
    return count;
}

/* CoinGroup (Petari CoinGroup.cpp, CircleCoinGroup.cpp, RailCoin.cpp): arg0 coins; on a rail spread by length, else a
 * circle of radius arg2 (default 200) in the placement's X / Z plane. ponytail: no Mercator rails, no time limit
 * (arg1), every group is out from the start (no appear switch / demo) */
static int coin_group(Pickup *out, const Row *r, const unsigned char *blob)
{
    if (r->arg[0] <= 0) return 0;
    if (r->path >= 0) {
        u32 np; int closed;
        const float *pts = find_rail(blob, r->zone, r->path, &np, &closed);
        return pts && np > 1 ? place_on_rail(out, pts, np, closed, r->arg[0]) : 0;
    }
    V3 side, front, c = row_pos(r);
    row_axes(r, &side, &front);
    float radius = r->arg[2] > 0 ? (float)r->arg[2] : 200.0f;
    for (int k = 0; k < r->arg[0]; k++) {
        float a = 6.2831853f * k / r->arg[0];
        out[k].pos = vadd(c, vadd(vmul(front, radius * cosf(a)), vmul(side, radius * sinf(a))));
        out[k].alive = 1;
    }
    return r->arg[0];
}

/* StarPieceGroup (Petari StarPieceGroup.cpp init / placementAllPiece): arg0 pieces (default 6); on a rail, spread by
 * length, or one per rail point when arg2 == 1; else a circle of radius arg1 (default 400) in the placement's X / Z
 * plane, scale included (makeMtxTRS). StarPieceFlow rows go through star_flow. */
static int star_group(Pickup *out, const Row *r, const unsigned char *blob)
{
    int n = r->arg[0] > 0 ? r->arg[0] : 6;
    u32 np = 0;
    int closed = 0;
    const float *pts = r->path >= 0 ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
    if (pts && np > 0) {
        if (r->arg[2] == 1) {
            pts = find_rail_of(blob, r->zone, r->path, &np, &closed, 1);
            for (u32 i = 0; out && i < np; i++) {
                out[i].pos = v3(pts[3 * i], pts[3 * i + 1], pts[3 * i + 2]);
                out[i].alive = 1;
            }
            return np;
        }
        return out ? place_on_rail(out, pts, np, closed, n) : n;
    }
    if (out) {
        V3 x, z, c = row_pos(r);
        row_axes(r, &x, &z);
        x = vmul(x, r->scale[0]);
        z = vmul(z, r->scale[2]);
        float radius = r->arg[1] > 0 ? (float)r->arg[1] : 400.0f;
        for (int i = 0; i < n; i++) {
            float a = 6.2831853f * i / n;
            out[i].pos = n == 1 ? c : vadd(c, vadd(vmul(z, radius * cosf(a)), vmul(x, radius * sinf(a))));
            out[i].alive = 1;
        }
    }
    return n;
}

/* StarPieceFlow (StarPieceGroup.cpp, Flow): arg1 pieces (default 1) spread along its rail like placementPieceOnRail,
 * all moving on at arg0 a frame (default 10), round again from the start at the end (exeFlow: repeat). Without out:
 * the count. ponytail: a closed rail's last segment (back to the start) is not ridden */
static int star_flow(Pickup *out, int first, const Row *r, const unsigned char *blob)
{
    int n = r->arg[1] > 0 ? r->arg[1] : 1;
    if (!out) return n;
    u32 np = 0;
    int closed = 0;
    const float *pts = r->path >= 0 ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
    if (!pts || np < 2 || nflows == MAX_FLOWS) {
        for (int i = 0; i < n; i++) { out[i].pos = row_pos(r); out[i].alive = 1; }
        return n;
    }
    Flow *f = &flows[nflows++];
    f->first = first; f->n = n; f->pts = pts; f->np = np;
    f->len = 0;
    for (u32 i = 0; i + 1 < np; i++)
        f->len += vlen(v3(pts[3 * i + 3] - pts[3 * i], pts[3 * i + 4] - pts[3 * i + 1], pts[3 * i + 5] - pts[3 * i + 2]));
    f->step = n == 1 ? 0 : f->len / (closed ? n : n - 1.0f);
    f->speed = r->arg[0] > 0 ? (float)r->arg[0] : 10.0f;
    place_on_rail(out, pts, np, 0, n);
    return n;
}

/* --- physics shared by Mario and Kuribo ------------------------------------------------------------------- */

static V3 up_at(V3 p, V3 prev)
{
    float g[3];
    if (!gravity_at(grav, ngrav, (const float *)&p, g))
        return prev;
    return v3(-g[0], -g[1], -g[2]);
}

/* Push a sphere out of the KCL; returns 1 if it rests on ground (a face within 60 degrees of up). */
unsigned int game_kcl_cycles;

typedef struct { V3 n; float dist; int inst; } Hit; /* world normal, penetration, instance */

/* Sphere against every placed KCL: the sphere goes to the instance's object space (KCL data is in model space),
 * normals and depths come back to world. ponytail: uniform scale only (scale.x), like every planet seen so far. */
static int kcl_query(V3 c, float radius, Hit *out, int max)
{
    int n = 0;
    for (int i = 0; i < ninsts && n < max; i++) {
        const Inst *in = &insts[i];
        if (!in->kcl) continue;
        if (in->beeflower && row_turn && row_turn[in->row]) continue; /* BeeFlowerHover gone through */
        V3 d = vsub(c, v3(in->m[12], in->m[13], in->m[14]));
        if (vdot(d, d) > (in->reach + radius) * (in->reach + radius)) continue;
        const float *m = in->m;
        float k = 1.0f / (in->s * in->s); /* columns are R * s */
        float lc[3] = {vdot(d, v3(m[0], m[1], m[2])) * k, vdot(d, v3(m[4], m[5], m[6])) * k,
                       vdot(d, v3(m[8], m[9], m[10])) * k};
        KclHit h[16];
        int got = kcl_check_sphere(in->kcl, lc, radius / in->s, h, max - n < 16 ? max - n : 16);
        for (int j = 0; j < got; j++) {
            const float *fn = kcl_face_normal(in->kcl, h[j].prism);
            V3 w = vadd(vadd(vmul(v3(m[0], m[1], m[2]), fn[0]), vmul(v3(m[4], m[5], m[6]), fn[1])),
                        vmul(v3(m[8], m[9], m[10]), fn[2]));
            out[n].n = vmul(w, 1.0f / in->s);
            out[n].inst = i;
            out[n++].dist = h[j].dist * in->s;
        }
    }
    return n;
}

/* Fell into space: farther than 4000 from every collision mesh's bounding sphere */
static int lost(V3 p)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].kcl) continue;
        float r = insts[i].reach + 4000.0f;
        V3 d = vsub(p, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]));
        if (vdot(d, d) < r * r) return 0;
    }
    return 1;
}

static int collide_n(V3 *center, V3 up, float radius, V3 *ground_n, V3 *wall_n, int *wall)
{
    unsigned int c0 = cycles();
    int ground = 0;
    ground_inst = -1;
    float flat = WALL_ANGLE_COS; /* ground normal = the flattest ground face touched: it is what holds Mario up, and
                                  * taking the deepest one flipped between the two faces of a crease */
    if (wall) *wall = 0;
    for (int it = 0; it < 3; it++) {
        Hit hits[16];
        int n = kcl_query(*center, radius, hits, 16);
        if (!n)
            break;
        int deepest = 0;
        for (int i = 0; i < n; i++) {
            if (hits[i].dist > hits[deepest].dist)
                deepest = i;
            float di = vdot(hits[i].n, up);
            if (di > flat) {
                flat = di;
                ground = 1;
                if (ground_n) *ground_n = hits[i].n;
                ground_inst = hits[i].inst;
            }
        }
        V3 nrm = hits[deepest].n;
        float d = vdot(nrm, up);
        /* walkable ground pushes straight up (depth / cos): pushing along a slope's normal would shift Mario sideways
         * every frame by the grounding step, so he crept downhill while standing; steeper faces push along the normal */
        if (d > SLIP_ANGLE_COS) *center = vadd(*center, vmul(up, (hits[deepest].dist + 0.01f) / d));
        else *center = vadd(*center, vmul(nrm, hits[deepest].dist + 0.01f));
        if (d > -0.3f && d < WALL_ANGLE_COS && wall) {
            *wall = 1;
            *wall_n = nrm;
        }
    }
    if (!ground) { /* resting check: a sphere a little lower touches ground */
        V3 probe = vsub(*center, vmul(up, 4.0f));
        Hit hits[16];
        int n = kcl_query(probe, radius, hits, 16);
        for (int i = 0; i < n; i++)
            if (vdot(hits[i].n, up) > flat) {
                flat = vdot(hits[i].n, up);
                ground = 1;
                if (ground_n) *ground_n = hits[i].n;
                ground_inst = hits[i].inst;
            }
    }
    game_kcl_cycles += cycles() - c0;
    return ground;
}

static int collide(V3 *center, V3 up, float radius) { return collide_n(center, up, radius, NULL, NULL, NULL); }

/* --- init ------------------------------------------------------------------------------------------------ */

void game_init(const Scene *s, const unsigned char *blob)
{
    sc = s;
    fx_init(s);
    u32 gsz;
    const unsigned char *g = blob_chunk(blob, "GRAV", &gsz);
    ngrav = *(const int *)g;
    grav = (const Gravity *)(g + 4);
    m_mario_skin = scene_model(s, "Mario");
    if (m_mario_skin && m_mario_skin->skin) {
        anim_init(s);
        mario_anim = mem_alloc(MEM_PLAYER, sizeof(Anim));
        anim_bind(mario_anim, m_mario_skin->skin);
        mario_play("wait", 1.0f, 1);
    } else {
        printf("game: no skinned Mario in the blob\n");
        m_mario_skin = NULL;
    }

    ncoins = count_type("Coin", 0) + count_type("CircleCoinGroup", 1) + count_type("RailCoin", 1);
    for (u32 i = 0; i < s->nobjs; i++) { /* room for what breakables drop */
        int c, p;
        if (breakable(s->types + 32 * s->objs[i].type, &s->objs[i], &c, &p)) {
            ncoins += c;
            box_coins += c;
            box_pieces += p;
        }
        if (strcmp(s->types + 32 * s->objs[i].type, "StarPieceSpot") == 0)
            box_pieces += s->objs[i].arg[0] >= 0 ? s->objs[i].arg[0] : 5;
        if (strcmp(s->types + 32 * s->objs[i].type, "ShootingStar") == 0)
            box_pieces += s->objs[i].arg[3] >= 0 ? s->objs[i].arg[3] : 5;
        const char *tn = s->types + 32 * s->objs[i].type;
        if ((!strcmp(tn, "FlowerGroup") || !strcmp(tn, "FlowerBlueGroup") || !strcmp(tn, "CutBushGroup")) && s->objs[i].arg[1] > 0) {
            if (s->objs[i].arg[2] == 1) box_pieces += s->objs[i].arg[1];
            else { ncoins += s->objs[i].arg[1]; box_coins += s->objs[i].arg[1]; }
        }
        if (stone_row(&s->objs[i]) && s->objs[i].arg[2] >= 0) box_pieces += 8; /* a star piece per stone */
        if (!strcmp(tn, "Butterfly") && s->objs[i].arg[1] < 0) box_pieces++; /* the pointer brings one out */
        if (!strcmp(tn, "StinkBugSmall")) box_pieces += 3; /* declareStarPiece(3) */
        if (!strcmp(tn, "PhantomCandlestand") || !strcmp(tn, "TeresaMansionCandlestand") || !strcmp(tn, "CandlestandIceVolcano")) {
            /* arg0 -1 a coin, 0 a star piece */
            if (s->objs[i].arg[0] == -1) { ncoins++; box_coins++; } else if (s->objs[i].arg[0] == 0) box_pieces++;
        }
        if (!strncmp(tn, "Unizo", 5) && strcmp(tn, "UnizoLauncher")) box_pieces += 6; /* sStarPieceNum */
        if (!strcmp(tn, "DeadLeaves")) { /* arg0 0 a coin, 1 three star pieces */
            if (s->objs[i].arg[0] == 0) { ncoins++; box_coins++; } else if (s->objs[i].arg[0] == 1) box_pieces += 3;
        }
        if (!strcmp(tn, "Pompon2Plant") || !strcmp(tn, "Pompon4Plant")) { /* arg1 (1) coins, star pieces with arg0 0 */
            int n = s->objs[i].arg[1] > 0 ? s->objs[i].arg[1] : 1;
            if (s->objs[i].arg[0] == 0) box_pieces += n; else { ncoins += n; box_coins += n; }
        }
    }
    row_dead = mem_alloc(MEM_OBJECTS, s->nobjs);
    star_wait = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(short));
    for (u32 i = 0; i < s->nobjs; i++) {
        int a = s->objs[i].arg[7];
        star_wait[i] = strcmp(s->types + 32 * s->objs[i].type, "PowerStar") ? 0 : a >= 0 ? a + 1 : a == -2 ? -1 : 0;
    }
    row_step = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(unsigned short));
    row_angle = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(float));
    row_turn = mem_alloc(MEM_OBJECTS, s->nobjs);
    row_hold = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(unsigned short));
    row_coord = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(float));
    row_speed = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(float));
    row_rdir = mem_alloc(MEM_OBJECTS, s->nobjs);
    row_rhold = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(unsigned short));
    for (u32 i = 0; i < s->nobjs; i++) /* ShootingStar: star pieces it still holds (declareStarPiece arg3) */
        if (strcmp(s->types + 32 * s->objs[i].type, "ShootingStar") == 0)
            row_hold[i] = s->objs[i].arg[3] >= 0 ? s->objs[i].arg[3] : 5;
    scene_blob_g = blob;
    npurple = count_type("PurpleCoin", 0) + count_type("CirclePurpleCoinGroup", 1) + count_type("PurpleRailCoin", 1);
    ngroup_wait = 0;
    nfats = 0; test_feed = 0; nsambos = 0;
    for (u32 i = 0; i < s->nobjs; i++) { /* the spin drivers a TicoFat's group brings out */
        const char *tn = s->types + 32 * s->objs[i].type;
        if (strncmp(tn, "SuperSpinDriver", 15) || s->objs[i].group < 0) continue;
        for (u32 k = 0; k < s->nobjs && ngroup_wait < MAX_GROUP_WAIT; k++)
            if (s->objs[k].group == s->objs[i].group && s->objs[k].zone == s->objs[i].zone &&
                strcmp(s->types + 32 * s->objs[k].type, "TicoFat") == 0) {
                group_wait[ngroup_wait].row = i; group_wait[ngroup_wait++].fat = k;
                break;
            }
    }
    for (int i = 0; i < MAX_OBJ_ANIM; i++) {
        oanim[i].row = -1;
        if (!oanim[i].a) oanim[i].a = mem_alloc(MEM_PLAYER, sizeof(Anim));
    }
    memset(&dino, 0, sizeof dino); dino.row = -1;
    memset(&ota, 0, sizeof ota); ota.row = -1;
    for (u32 i = 0; i < s->nobjs; i++)
        if (strcmp(s->types + 32 * s->objs[i].type, "OtaKing") == 0) {
            V3 x, z;
            row_axes(&s->objs[i], &x, &z);
            ota.row = i; ota.pos = row_pos(&s->objs[i]); ota.up = up_at(ota.pos, v3(0, 1, 0)); ota.f = ota.f0 = vnorm(vtangent(z, ota.up));
            ota.c0 = s->objs[i].arg[0] >= 0 ? (float)s->objs[i].arg[0] : 450.0f; /* CocoNutBall _C0 / _C4 from arg0 / arg2 */
            ota.c4 = s->objs[i].arg[2] >= 0 ? (float)s->objs[i].arg[2] : 0.0f;
            for (int k = 0; k < 3; k++) {
                Anim *an = mem_alloc(MEM_PLAYER, sizeof(Anim));
                memset(an, 0, sizeof(Anim));
                if (k) ota.foot[k - 1] = an; else ota.anim = an;
            }
            break;
        }
    for (u32 i = 0; i < s->nobjs; i++)
        if (strcmp(s->types + 32 * s->objs[i].type, "DinoPackun") == 0) {
            V3 x, z;
            row_axes(&s->objs[i], &x, &z);
            dino.row = i; dino.pos = row_pos(&s->objs[i]); dino.up = up_at(dino.pos, v3(0, 1, 0));
            dino.f = vnorm(vtangent(z, dino.up)); dino.side = 1.0f;
            dino.bpos = vsub(dino.pos, vmul(dino.f, 600.0f));
            dino.anim = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(dino.anim, 0, sizeof(Anim));
            break;
        }
    npieces = count_type("StarPiece", 0) + box_pieces;
    int t_spg = scene_type(s, "StarPieceGroup"), t_flow = scene_type(s, "StarPieceFlow");
    for (u32 i = 0; i < s->nobjs; i++)
        if (s->objs[i].type == t_spg)
            npieces += star_group(NULL, &s->objs[i], blob);
        else if (s->objs[i].type == t_flow)
            npieces += star_flow(NULL, 0, &s->objs[i], blob);
    nkuribo = count_type("Poihana", 0) + count_type("Unizo", 0) + count_type("UnizoLand", 0) + count_type("UnizoShoal", 0) + count_type("StinkBugSmall", 0) + count_type("Kuribo", 0) + count_type("TakoHei", 0) + count_type("PackunPetit", 0) + count_type("KoteBug", 0) +
              count_type("JumpSpider", 0) + count_type("Meramera", 0) + count_type("Takobo", 0) +
              count_type("MechanicKoopaMini", 0) + count_type("Karikari", 0) +
              count_type("KuriboMini", 0) + count_type("Karon", 0) +
              count_type("Teresa", 0) + count_type("Butterfly", 0);
    int ntspots = count_type("TreasureSpot", 0) + count_type("CoinFlower", 0); /* a coin each when shot */
    box_coins += nkuribo + ntspots; /* each drops a coin (appearCoinPop) */
    ncoins += nkuribo + ntspots;
    coins = mem_alloc(MEM_OBJECTS, ncoins * sizeof(Pickup));
    purple = mem_alloc(MEM_OBJECTS, npurple * sizeof(Pickup));
    pieces = mem_alloc(MEM_OBJECTS, npieces * sizeof(Pickup));
    kuribo = mem_alloc(MEM_OBJECTS, nkuribo * sizeof(Kuribo));
    pops = mem_alloc(MEM_OBJECTS, (box_coins + 1) * sizeof(Pop));
    mario = mem_alloc(MEM_PLAYER, sizeof(Player));
    nkillers = 0;
    nflows = 0;
    kari_cling = 0;

    int ic = 0, ip = 0, is = 0, ik = 0;
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    int t_tako = scene_type(s, "TakoHei"), t_packun = scene_type(s, "PackunPetit"), t_kote = scene_type(s, "KoteBug");
    int t_spider = scene_type(s, "JumpSpider"), t_mera = scene_type(s, "Meramera"), t_takobo = scene_type(s, "Takobo");
    int t_mkoopa = scene_type(s, "MechanicKoopaMini"), t_kari = scene_type(s, "Karikari"), t_kmini = scene_type(s, "KuriboMini");
    int t_karon = scene_type(s, "Karon"), t_teresa = scene_type(s, "Teresa"), t_bfly = scene_type(s, "Butterfly");
    int t_stink = scene_type(s, "StinkBugSmall"), t_uni = scene_type(s, "Unizo"), t_unil = scene_type(s, "UnizoLand"),
        t_unis = scene_type(s, "UnizoShoal"), t_poi = scene_type(s, "Poihana");
    int t_gcircle = scene_type(s, "CircleCoinGroup"), t_grail = scene_type(s, "RailCoin");
    insts =mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(Inst));
    zone_spheres();
    game_rebind();
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        int c0 = ic, p0 = ip, s0 = is;
        if (r->type == t_coin) { coins[ic].pos = row_pos(r); coins[ic++].alive = 1; }
        else if (r->type == t_pc) { purple[ip].pos = row_pos(r); purple[ip++].alive = 1; }
        else if (r->type == t_piece) { pieces[is].pos = row_pos(r); pieces[is++].alive = 1; }
        else if (r->type == t_spg) is += star_group(pieces + is, r, blob);
        else if (r->type == t_flow) is += star_flow(pieces + is, is, r, blob);
        else if (r->type == t_kuribo || r->type == t_tako || r->type == t_packun || r->type == t_kote || r->type == t_spider ||
                 r->type == t_mera || r->type == t_takobo || r->type == t_mkoopa || r->type == t_kari || r->type == t_kmini || r->type == t_karon || r->type == t_teresa || r->type == t_bfly || r->type == t_stink ||
                 r->type == t_uni || r->type == t_unil || r->type == t_unis || r->type == t_poi) {
            memset(&kuribo[ik], 0, sizeof(Kuribo));
            kuribo[ik].dir = r->type == t_unil ? 1 : r->type == t_unis ? 2 : 0; /* Unizo: sea, land, shoal */
            kuribo[ik].kind = r->type == t_poi ? WALK_POIHANA : r->type == t_uni || r->type == t_unil || r->type == t_unis ? WALK_UNIZO : r->type == t_stink ? WALK_STINKBUG : r->type == t_bfly ? WALK_BUTTERFLY : r->type == t_teresa ? WALK_TERESA : r->type == t_karon ? WALK_KARON : r->type == t_kmini ? WALK_KMINI : r->type == t_kari ? WALK_KARIKARI : r->type == t_mkoopa ? WALK_MKOOPA : r->type == t_takobo ? WALK_TAKOBO : r->type == t_tako ? WALK_TAKOHEI : r->type == t_packun ? WALK_PACKUN :
                              r->type == t_kote ? WALK_KOTEBUG : r->type == t_spider ? WALK_SPIDER :
                              r->type == t_mera ? WALK_MERAMERA : WALK_KURIBO;
            kuribo[ik].pos = row_pos(r);
            kuribo[ik].row = i;
            kuribo[ik].up = up_at(kuribo[ik].pos, v3(0, 1, 0));
            kuribo[ik++].state = KURIBO_WALK;
        } else if (r->type == t_circle || r->type == t_rail)
            ip += coin_group(purple + ip, r, blob);
        else if (r->type == t_gcircle || r->type == t_grail)
            ic += coin_group(coins + ic, r, blob);
        for (int k = c0; k < ic; k++) coins[k].row = i;
        for (int k = p0; k < ip; k++) purple[k].row = i;
        for (int k = s0; k < is; k++) pieces[k].row = i;
    }
    npurple = ip;
    notes_spawn();
    ncoins = ic; /* box coins and star pieces are appended as boxes break */
    npieces = is;
    int t_super[3] = {scene_type(s, "SuperSpinDriver"), scene_type(s, "SuperSpinDriverPink"),
                      scene_type(s, "SuperSpinDriverGreen")}, t_spin = scene_type(s, "SpinDriver");
    launches = mem_alloc(MEM_OBJECTS, (count_type("SuperSpinDriver", 0) + count_type("SuperSpinDriverPink", 0) +
                                       count_type("SuperSpinDriverGreen", 0) + count_type("SpinDriver", 0) +
                                       count_type("ScenarioStarter", 0) + 1) * sizeof(Launch));
    int t_starter = scene_type(s, "ScenarioStarter");
    row_camset = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(short));
    for (u32 i = 0; i < s->nobjs; i++) row_camset[i] = -1;
    {
        u32 csz;
        const unsigned char *cs = blob_chunk(blob, "CSET", &csz);
        for (u32 i = 0; cs && i < *(const u32 *)cs; i++) {
            u16 row = *(const u16 *)(cs + 4 + 4 * i);
            if (row < s->nobjs) row_camset[row] = *(const s16 *)(cs + 6 + 4 * i);
        }
    }
    starter = -1;
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        int super = r->type == t_super[0] || r->type == t_super[1] || r->type == t_super[2] || r->type == t_starter;
        if (!super && r->type != t_spin) continue;
        u32 np = 0; int closed;
        const float *pts = r->path >= 0 ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
        Launch *L = &launches[nlaunches];
        u32 nraw = 0;
        if (pts) find_rail_of(blob, r->zone, r->path, &nraw, &closed, 1);
        if (pts && np >= 2) {
            init_launch(L, r, pts, np, nraw <= 2);
            if (r->type == t_starter) { /* ScenarioStarter: its rail as it is (mPosition = the rail's start), from the start */
                L->pos = v3(pts[0], pts[1], pts[2]); L->off = v3(0, 0, 0);
                L->starter = 1; L->initrot = r->arg[5] != -1 ? (float)r->arg[5] : 0.0f; /* arg5 mInitalPlayerRotation */
                starter = nlaunches;
            }
        }
        else if (super)
            continue;
        else { /* SpinDriver without a rail: thrown along its Y axis at arg0 */
            V3 x, z;
            row_axes(r, &x, &z);
            memset(L, 0, sizeof *L);
            L->pos = row_pos(r);
            L->kick = vmul(vcross(z, x), r->arg[0] > 0 ? (float)r->arg[0] : 40.0f);
        }
        L->start = r->type == t_starter ? 0 : super ? 45 : 20; /* ScenarioStarter flies at once (exeRailMove) */
        L->row = i;
        nlaunches++;
    }
    int t_erail = scene_type(s, "ElectricRail");
    for (int pass = 0; pass < 2; pass++) { /* count the segments, then fill them */
        int n = 0;
        for (u32 i = 0; i < s->nobjs; i++) {
            const Row *r = &s->objs[i];
            u32 np; int closed;
            const float *pts = r->type == t_erail ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
            if (!pts || np < 2) continue;
            for (u32 k = 0; k < (closed ? np : np - 1); k++, n++) {
                if (!pass) continue;
                const float *a = pts + 3 * k, *b = pts + 3 * ((k + 1) % np);
                erails[n].a = v3(a[0], a[1], a[2]);
                erails[n].b = v3(b[0], b[1], b[2]);
                erails[n].h = r->arg[0] > 0 ? (r->arg[0] < 8 ? r->arg[0] : 8) : 1;
            }
        }
        if (!pass) erails = mem_alloc(MEM_OBJECTS, (n + 1) * sizeof(Spark));
        nerails = n;
    }
    {
        int t_fg = scene_type(s, "FlowerGroup"), t_fb = scene_type(s, "FlowerBlueGroup"), t_cb = scene_type(s, "CutBushGroup");
        int total = 0;
        plant_first = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(int));
        plant_state = mem_alloc(MEM_OBJECTS, s->nobjs);
        for (u32 i = 0; i < s->nobjs; i++) {
            int t = s->objs[i].type;
            plant_first[i] = -1;
            if (t == t_fg || t == t_fb || t == t_cb || stone_row(&s->objs[i])) { plant_first[i] = total; total += plant_count(&s->objs[i]); }
        }
        plants = mem_alloc(MEM_OBJECTS, (total + 1) * sizeof(Plant));
        for (u32 i = 0; i < s->nobjs; i++) { /* initMember: the first arg1 hold an item, then shuffled */
            if (plant_first[i] < 0) continue;
            Plant *g = &plants[plant_first[i]];
            int n = plant_count(&s->objs[i]), items = stone_row(&s->objs[i]) ? 0 : s->objs[i].arg[1] > 0 ? s->objs[i].arg[1] : 0;
            for (int k = 0; k < n; k++) g[k].item = k < items;
            for (int k = 0; k < n; k++) {
                int j = rand() % (k + 1), t = g[k].item;
                g[k].item = g[j].item; g[j].item = t;
            }
        }
    }
    shells = mem_alloc(MEM_OBJECTS, ((count_type("IronCannonLauncherPoint", 0) + count_type("IronCannonLauncher", 0)) *
                                     SHELLS_PER + 1) * sizeof(Shell));
    for (u32 i = 0; i < s->nobjs; i++) {
        const char *tn = s->types + 32 * s->objs[i].type;
        if (strcmp(tn, "IronCannonLauncherPoint") && strcmp(tn, "IronCannonLauncher")) continue;
        for (int k = 0; k < SHELLS_PER; k++) {
            memset(&shells[nshells], 0, sizeof(Shell));
            shells[nshells++].row = i;
        }
    }
    int t_rockc = scene_type(s, "RockCreator");
    rollers = mem_alloc(MEM_OBJECTS, (count_type("RockCreator", 0) * ROCKS_PER + 1) * sizeof(Roller));
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        u32 np; int closed;
        const float *pts = r->type == t_rockc && r->path >= 0 ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
        if (!pts || np < 2) continue;
        float len = 0;
        for (u32 k = 0; k + 1 < np; k++)
            len += vlen(vsub(v3(pts[3 * k + 3], pts[3 * k + 4], pts[3 * k + 5]), v3(pts[3 * k], pts[3 * k + 1], pts[3 * k + 2])));
        for (int k = 0; k < ROCKS_PER; k++) {
            Roller *o = &rollers[nrollers++];
            o->pts = pts; o->np = np; o->len = len; o->row = i;
        }
    }
    int t_pipe = scene_type(s, "EarthenPipe"), t_wpipe = scene_type(s, "EarthenPipeInWater");
    pipes = mem_alloc(MEM_OBJECTS, (count_type("EarthenPipe", 0) + count_type("EarthenPipeInWater", 0) + 1) * sizeof(Pipe));
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if (r->type != t_pipe && r->type != t_wpipe) continue;
        Pipe *p = &pipes[npipes];
        V3 x, z, pos = row_pos(r);
        row_axes(r, &x, &z);
        p->up = r->arg[6] > 0 ? vcross(z, x) : up_at(pos, vcross(z, x));
        p->front = z;
        p->top = vadd(pos, vmul(p->up, 100.0f * r->scale[1]));
        p->hf = r->arg[0] >= 0 ? (float)r->arg[0] : 5.0f;
        p->vf = r->arg[1] >= 0 ? (float)r->arg[1] : 20.0f;
        p->id = r->arg[3];
        p->zone = r->zone;
        p->row = i;
        p->mode = r->arg[2];
        p->partner = -1;
        for (int k = 0; k < npipes; k++) /* EarthenPipeMediator::entry: the first pipe with the same id */
            if (pipes[k].id == p->id && pipes[k].partner < 0) { pipes[k].partner = npipes; p->partner = k; break; }
        npipes++;
    }
    logic = mem_alloc(MEM_OBJECTS, (count_type("TimerSwitch", 0) + count_type("SwitchSynchronizerReverse", 0) +
                                    count_type("GroupSwitchWatcher", 0) + count_type("CollisionBlocker", 0) +
                                    count_type("FlipPanelObserver", 0) + count_type("YellowChipGroup", 0) +
                                    count_type("BlueChipGroup", 0) + count_type("SwitchCube", 0) +
                                    count_type("SwitchSphere", 0) + count_type("SwitchCylinder", 0) +
                                    count_type("DeathCube", 0) + count_type("DeathSphere", 0) +
                                    count_type("DeathCylinder", 0) + count_type("RestartCube", 0) +
                                    count_type("StarPieceSpot", 0) + 1) *
                                       sizeof(Logic));
    int t_timer = scene_type(s, "TimerSwitch"), t_rev = scene_type(s, "SwitchSynchronizerReverse"),
        t_group = scene_type(s, "GroupSwitchWatcher"), t_block = scene_type(s, "CollisionBlocker"),
        t_flipobs = scene_type(s, "FlipPanelObserver"), t_ychips = scene_type(s, "YellowChipGroup"),
        t_bchips = scene_type(s, "BlueChipGroup");
    for (u32 i = 0; i < s->nobjs; i++) {
        int t = s->objs[i].type;
        const char *tn = s->types + 32 * t;
        int k = strncmp(tn, "Switch", 6) == 0 && area_shape(tn) >= 0 ? LOGIC_SWAREA :
                strncmp(tn, "Death", 5) == 0 && area_shape(tn) >= 0 ? LOGIC_DEATH :
                strcmp(tn, "RestartCube") == 0 ? LOGIC_RESTART : strcmp(tn, "StarPieceSpot") == 0 ? LOGIC_PIECESPOT :
                t == t_timer ? LOGIC_TIMER : t == t_rev ? LOGIC_REVERSE : t == t_group ? LOGIC_GROUP :
                t == t_block ? LOGIC_BLOCKER : t == t_flipobs ? LOGIC_FLIPS : t == t_ychips || t == t_bchips ? LOGIC_CHIPS : 0;
        if (k) { logic[nlogic].row = i; logic[nlogic++].kind = k; }
    }
    const Row *st = scene_start(s);
    start_pos = row_pos(st);
#ifdef SLOPE_TEST
    { /* Gateway planet face with a vertex at local (1093, 1353, 1265), normal (-0.22, 0.88, -0.43), 79 degrees off the
       * radial up, 733 tall: start 150 above the vertex along the radial up, in world space through the placement */
        extern V3 slope_n;
        const float *pm = insts[0].m;
        V3 ru = vnorm(v3(1093, 1353, 1265));
        V3 lp = vadd(v3(1093, 1353, 1265), vmul(ru, 150)), ln = v3(-0.22f, 0.88f, -0.43f);
        start_pos = v3(pm[0] * lp.x + pm[4] * lp.y + pm[8] * lp.z + pm[12], pm[1] * lp.x + pm[5] * lp.y + pm[9] * lp.z + pm[13],
                       pm[2] * lp.x + pm[6] * lp.y + pm[10] * lp.z + pm[14]);
        slope_n = vnorm(v3(pm[0] * ln.x + pm[4] * ln.y + pm[8] * ln.z, pm[1] * ln.x + pm[5] * ln.y + pm[9] * ln.z,
                           pm[2] * ln.x + pm[6] * ln.y + pm[10] * ln.z));
    }
#endif
    mario->pos = start_pos;
    mario->up = up_at(mario->pos, v3(0, 1, 0));
    mario->face = vnorm(vtangent(v3(0, 0, 1), mario->up));
    mario->jump_kind = mario->last_jump = -1;
    mario->wall_timer = 1000;
    mario->slip_keep = SLIP_KEEP;
    mario->launch = -1;
    cam_pos = vadd(mario->pos, vadd(vmul(mario->up, 500), vmul(mario->face, -1000)));
    {   /* SMG cameras: CAMS + CARE chunks (tools/camera.py), each camera in its zone's frame (ZONE: world placement) */
        u32 sz;
        const unsigned char *c = blob_chunk(blob, "CAMS", &sz), *a = blob_chunk(blob, "CARE", &sz);
        const unsigned char *z = blob_chunk(blob, "ZONE", &sz);
        u32 nz = *(const u32 *)z;
        float (*zm)[16] = mem_alloc(MEM_SCENE, nz * 64);
        static const float one[3] = {1, 1, 1};
        for (u32 i = 0; i < nz; i++) {
            const float *e = (const float *)(z + 4 + 56 * i + 32);
            mat_translate_rot_scale(zm[i], e, e + 3, one);
        }
        CamTarget t = {mario->pos, mario->face, mario->up, 0, 0, {0, 0, 0}};
        camera_init(c ? (const CamParam *)(c + 16) : NULL, c ? *(const int *)c : 0,
                    a ? (const CamArea *)(a + 16) : NULL, a ? *(const int *)a : 0, (const float (*)[16])zm, &t);
    }
    printf("game: %d launch stars, %d electric rail segments\n", nlaunches, nerails);
    printf("game: walkers %d: kuribo %d, takohei %d, packun %d\n", nkuribo, game_kind_alive(WALK_KURIBO),
           game_kind_alive(WALK_TAKOHEI), game_kind_alive(WALK_PACKUN));
    printf("game: %d static instances, coins %d, purple %d, star pieces %d, kuribo %d, start (%d %d %d)\n", ninsts,
           ncoins, npurple, npieces, nkuribo, (int)start_pos.x, (int)start_pos.y, (int)start_pos.z);
}

/* a chip's group (same arg0, ChipHolder) waiting for its appear switch keeps the chip hidden */
static int chip_group_out(const Row *chip, const char *name)
{
    for (int i = 0; i < nlogic; i++) {
        const Row *g = &sc->objs[logic[i].row];
        if (logic[i].kind == LOGIC_CHIPS && g->arg[0] == chip->arg[0] && sc->types[32 * g->type] == name[0])
            return row_shown(logic[i].row);
    }
    return 1;
}

/* NameObjFactory's Sky kind: domes that follow the camera (Petari Sky.cpp: setBaseTRMtx at the camera position) */
static int sky_class(const char *name)
{
    static const char *const skies[] = {"VROrbit", "VRDarkSpace", "VRSandwichSun", "SummerSky", "GalaxySky", "MilkyWaySky",
                                        "HalfGalaxySky", "GreenPlanetOrbitSky", "PhantomSky", "KoopaVS1Sky", "KoopaVS2Sky",
                                        "FamicomMarioSky", "DesertSky", "ChildRoomSky", "AuroraSky", "CloudSky",
                                        "RockPlanetOrbitSky", "StarrySky", "AstroDomeSkyA"};
    for (unsigned i = 0; i < sizeof skies / sizeof skies[0]; i++)
        if (strcmp(name, skies[i]) == 0) return 1;
    return 0;
}

/* NPCs that turn to Mario within NPC_TURN (Petari NPCActor: the talk range; ponytail: an instant turn, no talk, no
 * idle animation) */
#define NPC_TURN 1000.0f
static int npc_class(const char *name)
{
    static const char *const npcs[] = {"Kinopio", "KinopioAstro", "Tico", "TicoAstro", "TicoFat", "TicoShop", "TicoReading",
                                       "TicoGalaxy", "Penguin", "PenguinRacer", "PenguinCoach", "PenguinMaster", "Caretaker",
                                       "HoneyBee", "Rabbit", "Rosetta", "CollectTico"};
    for (unsigned i = 0; i < sizeof npcs / sizeof npcs[0]; i++)
        if (strcmp(name, npcs[i]) == 0) return 1;
    return 0;
}

/* MapPartsRotator.cpp: the placement turned by row_angle about its own X / Y / Z axis (RotateAxis), about its origin */
static void rotor_matrix(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    float base[16], rot[16];
    mat_translate_rot_scale(base, r->pos, r->rot, r->scale);
    int ax = r->arg[6] >= 0 && r->arg[6] <= 2 ? r->arg[6] : 0;
    V3 a = vnorm(v3(base[4 * ax], base[4 * ax + 1], base[4 * ax + 2]));
    float t = row_angle[in->row] * 0.0174533f, c = cosf(t), s = sinf(t), k = 1 - c;
    mat_identity(rot); /* axis-angle, column-major */
    rot[0] = c + a.x * a.x * k;       rot[4] = a.x * a.y * k - a.z * s; rot[8] = a.x * a.z * k + a.y * s;
    rot[1] = a.y * a.x * k + a.z * s; rot[5] = c + a.y * a.y * k;       rot[9] = a.y * a.z * k - a.x * s;
    rot[2] = a.z * a.x * k - a.y * s; rot[6] = a.z * a.y * k + a.x * s; rot[10] = c + a.z * a.z * k;
    base[12] = base[13] = base[14] = 0;
    mat_mul(in->m, rot, base);
    in->m[12] = r->pos[0]; in->m[13] = r->pos[1]; in->m[14] = r->pos[2];
}

/* AssemblyBlock.cpp. Its home (slice.py: the linked GeneralPos 合体ブロック故郷点, else 1000 up and along z; args 2-4
 * position, arg5 rx << 16 | ry, arg6 rz, degrees) and its place (the row). row_turn: 0 Wait (at home, turning 0.1
 * degree a frame about its local x, y and z: one turn about x + y + z; bobbing sin(step) along gravity a frame), 1
 * Assemble (home -> place in 10 frames), 2 AssembleWait, 3 Return (10 frames), 4 Timer; row_step its frames.
 * ponytail: the blend lerps the matrices and re-orthonormalizes, no slerp; collision stays on while it flies */
static void assembly_home(const Row *r, float m[16])
{
    float pos[3] = {(float)r->arg[2], (float)r->arg[3], (float)r->arg[4]};
    float rot[3] = {(float)(r->arg[5] >> 16 & 0xFFFF), (float)(r->arg[5] & 0xFFFF), (float)r->arg[6]};
    mat_translate_rot_scale(m, pos, rot, r->scale);
}

static void assembly_place(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    float h[16], p[16];
    int st = row_step[in->row], ph = row_turn[in->row];
    assembly_home(r, h);
    mat_translate_rot_scale(p, r->pos, r->rot, r->scale);
    if (ph == 0) { /* Wait */
        V3 a = vnorm(vadd(vadd(v3(h[0], h[1], h[2]), v3(h[4], h[5], h[6])), v3(h[8], h[9], h[10])));
        float t = (in->row & 1 ? 0.1732f : -0.1732f) * game_frame * 0.0174533f, c = cosf(t), s = sinf(t), k = 1 - c;
        float rot[16], base[16];
        mat_identity(rot);
        rot[0] = c + a.x * a.x * k;       rot[4] = a.x * a.y * k - a.z * s; rot[8] = a.x * a.z * k + a.y * s;
        rot[1] = a.y * a.x * k + a.z * s; rot[5] = c + a.y * a.y * k;       rot[9] = a.y * a.z * k - a.x * s;
        rot[2] = a.z * a.x * k - a.y * s; rot[6] = a.z * a.y * k + a.x * s; rot[10] = c + a.z * a.z * k;
        memcpy(base, h, 64);
        base[12] = base[13] = base[14] = 0;
        mat_mul(in->m, rot, base);
        V3 hp = v3(h[12], h[13], h[14]), d = vmul(in->gup, -57.3f * (1.0f - cosf(st * 0.0174533f)));
        in->m[12] = hp.x + d.x; in->m[13] = hp.y + d.y; in->m[14] = hp.z + d.z;
        return;
    }
    if (ph != 1 && ph != 3) { memcpy(in->m, p, 64); return; }
    float u = st >= 10 ? 1.0f : st / 10.0f;
    if (ph == 3) u = 1.0f - u;
    for (int j = 0; j < 16; j++) in->m[j] = h[j] + (p[j] - h[j]) * u;
    V3 x = vnorm(v3(in->m[0], in->m[1], in->m[2])), y = v3(in->m[4], in->m[5], in->m[6]);
    y = vnorm(vsub(y, vmul(x, vdot(x, y))));
    V3 z = vcross(x, y);
    float sc = r->scale[0];
    in->m[0] = x.x * sc; in->m[1] = x.y * sc; in->m[2] = x.z * sc;
    in->m[4] = y.x * sc; in->m[5] = y.y * sc; in->m[6] = y.z * sc;
    in->m[8] = z.x * sc; in->m[9] = z.y * sc; in->m[10] = z.z * sc;
}

static void carry(const float old[16], const Inst *in);
static void update_assembly(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->assembly) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        float dist = r->arg[0] > 0 ? (float)r->arg[0] : 2.0f * in->reach;
        float near = vlen(vsub(mario->pos, v3(r->pos[0], r->pos[1], r->pos[2])));
        int alive = life > 0;
        float old[16];
        memcpy(old, in->m, 64);
        if (*t < 0xFFFF) ++*t;
        if (*ph == 0 && near < dist && alive) { *ph = 1; *t = 0; }
        else if (*ph == 1 && *t >= 10) { *ph = r->arg[7] == 0 ? 4 : 2; *t = 0; }
        else if (*ph == 2 && near > dist + 50.0f && alive) { *ph = 3; *t = 0; }
        else if (*ph == 3 && *t >= 10) { *ph = 0; *t = 0; }
        else if (*ph == 4 && *t >= (r->arg[1] > 0 ? r->arg[1] : 300)) {
            printf("assembly block %d vanishes\n", in->row);
            row_kill(in->row); game_rebind(); return;
        }
        assembly_place(in);
        if (mario_ground == i) carry(old, in);
    }
}

/* Tsukidashikun.cpp: on its rail from the nearest point to its placement; Relax until SW_B (if it has one), Wait arg1
 * frames (default 120), Sign 60, Move at arg0 (default 10) to the rail's end, reverse, Wait, Sign, Move back... row_turn
 * 0 not started, 1 relax, 2 wait, 3 sign, 4 move; row_rdir its direction (+1 / -1); row_coord, row_step. ponytail: no
 * Sign shake (its BCK) */
static float tsuki_len(const unsigned char *e)
{
    u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
    return *(const float *)(e + 24 + 12 * np + 12 * nr + 12 * (nr - 1));
}

static void ticorail_place(Inst *in);
static void biri_place(Inst *in);
static void coco_spawn(Inst *in);
static V3 sambo_joint(int k, const Inst *in, int j, V3 off);
static V3 sambo_head_pos(int k, const Inst *in);
static int sambo_try_fall(int k);
static int sambo_of(int row);
static int sambo_swoon(int k);
static V3 sambo_hpos(int k);
static V3 cocoon_neutral(const Inst *in);
static void cocoon_place(Inst *in);
static void fluff_place(Inst *in);
static void jet_place(Inst *in);
static V3 stink_front(const Kuribo *k);
static void place_waterplants(void);
static void tsuki_place(Inst *in)
{
    V3 p = rail_at((const float *)(in->tsuki + 24), *(const u32 *)(in->tsuki + 8), row_coord[in->row]);
    in->m[12] = p.x; in->m[13] = p.y; in->m[14] = p.z;
}

static void update_tsuki(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->tsuki) continue;
        const Row *r = &sc->objs[in->row];
        int row = in->row;
        unsigned short *t = &row_step[row];
        signed char *ph = &row_turn[row];
        if (!*ph) { /* moveCoordToNearestPos */
            const float *p = (const float *)(in->tsuki + 24);
            u32 np = *(const u32 *)(in->tsuki + 8);
            V3 at = v3(r->pos[0], r->pos[1], r->pos[2]);
            float best = 1e30f, acc = 0, c = 0;
            for (u32 k = 0; k + 1 < np; k++) {
                V3 a = v3(p[3 * k], p[3 * k + 1], p[3 * k + 2]), b = v3(p[3 * k + 3], p[3 * k + 4], p[3 * k + 5]);
                V3 ab = vsub(b, a);
                float l = vlen(ab), u = l > 0 ? vdot(vsub(at, a), ab) / (l * l) : 0;
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                float d = vlen(vsub(at, vadd(a, vmul(ab, u))));
                if (d < best) { best = d; c = acc + u * l; }
                acc += l;
            }
            row_coord[row] = c; row_rdir[row] = 1;
            *ph = r->sw[SW_B] >= 0 ? 1 : 2; *t = 0;
        }
        if (*ph == 1) {
            if (!sw_on(r, SW_B)) continue;
            *ph = 2; *t = 0;
        }
        ++*t;
        if (*ph == 2 && *t >= (r->arg[1] >= 0 ? r->arg[1] : 120)) { *ph = 3; *t = 0; }
        else if (*ph == 3 && *t >= 60) { *ph = 4; *t = 0; }
        else if (*ph == 4) {
            float old[16], len = tsuki_len(in->tsuki), n = row_coord[row] + (r->arg[0] > 0 ? r->arg[0] : 10) * row_rdir[row];
            memcpy(old, in->m, 64);
            if (n >= len || n <= 0) { /* isRailReachedGoal: reverse, wait */
                n = n >= len ? len : 0;
                row_rdir[row] = -row_rdir[row];
                *ph = 2; *t = 0;
            }
            row_coord[row] = n;
            tsuki_place(in);
            if (mario_ground == i) carry(old, in);
        }
    }
}

/* Kiraira.cpp: drifts along gravity (velocity 1.5 sin(45 + 2 t) a frame: 43 (cos 45 - cos(45 + 2 t)) from its place),
 * turns to Mario within 1500; touching its body (120) explodes it (an explosion attack), SW_B on; Dead 300 frames,
 * RecoverSign 120, Revival ~60 (KIRA_DOWN in all), then it waits again (SW_B off). row_turn 1 while down, row_step its
 * frames. ponytail: hidden while Dead (its Down BCK), faces Mario at once, SpaceMine's explosion, no chain, no group
 * alarm, no KirairaRail */
#define KIRA_DOWN 480
static void hurt_mario(V3 from, float speed);
static V3 kiraira_pos(const Inst *in, V3 *up)
{
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    *up = up_at(p, v3(in->m[4], in->m[5], in->m[6]));
    float t = (game_frame * 2.0f + 45.0f) * 0.0174533f;
    return vsub(p, vmul(*up, 43.0f * (0.7071068f - cosf(t))));
}

static void update_kiraira(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->kiraira) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        if (*ph) {
            if (++*t >= KIRA_DOWN) { *ph = 0; *t = 0; if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 0); }
            continue;
        }
        V3 up, p = kiraira_pos(in, &up);
        if (vlen(vsub(c, p)) >= 120.0f * in->s + HIT_BODY_R) continue;
        hurt_mario(p, 25.0f);
        float m[16];
        memcpy(m, in->m, 64);
        m[12] = p.x; m[13] = p.y; m[14] = p.z;
        fx_emit(FX_SPACEMINE_EXPLOSION, m);
        *ph = 1; *t = 0;
        if (r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
        printf("kiraira %d explodes\n", in->row);
    }
}

/* MeteorStrikeLauncher.cpp / MeteorStrike.cpp: every arg1 x 60 frames (once if arg1 is unset) a meteor leaves its rail's
 * point 0 towards point 1 at arg0 a frame (default 10), turning 7.5 degrees a frame, until it hits the map (binder 80)
 * or passes point 1; its body (90) hits Mario (a strong attack) and breaks it. MeteorStrikeEnvironment ones touch
 * nothing. row_turn 0 waiting (row_step the interval), 1 falling (row_step its steps), 2 done. ponytail: one meteor at
 * a time per launcher (no arg2 multi meteors, no start part-way when off screen), no shadow, no break model / lava
 * column, no meteor at all before the first frame it is on screen is not checked */
static int kcl_query(V3 c, float radius, Hit *out, int max);
static V3 meteor_pos(const Inst *in, V3 *dir)
{
    const float *p = in->meteor;
    V3 a = v3(p[0], p[1], p[2]), b = v3(p[3], p[4], p[5]);
    const Row *r = &sc->objs[in->row];
    *dir = vnorm(vsub(b, a));
    return vadd(a, vmul(*dir, (r->arg[0] > 0 ? r->arg[0] : 10) * (float)row_step[in->row]));
}

static void update_meteor(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->meteor || !row_shown(in->row)) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        if (*ph == 2) continue;
        if (*ph == 0) {
            if (*t && ++*t < r->arg[1] * 60) continue;
            *ph = 1; *t = 0;
        }
        ++*t;
        V3 dir, p = meteor_pos(in, &dir), b = v3(in->meteor[3], in->meteor[4], in->meteor[5]);
        int env = strcmp(sc->types + 32 * r->type, "MeteorStrikeEnvironment") == 0, end = 0;
        Hit h[1];
        if (!env && vlen(vsub(c, p)) < 90.0f + HIT_BODY_R) {
            hurt_mario(p, 25.0f);
            printf("meteor %d hits Mario\n", in->row);
            end = 1;
        } else if (vdot(vsub(p, b), dir) > 0)
            end = 1;
        else if (!env && kcl_query(p, 80.0f, h, 1)) {
            printf("meteor %d breaks on the ground after %d steps\n", in->row, *t);
            end = 1;
        }
        if (end) {
            *ph = r->arg[1] > 0 ? 0 : 2;
            *t = 1; /* the interval counts from 1 */
        }
    }
}

/* HomingKiller.cpp (HomingKillerLauncher): the bullet leaves its place along its Z (level with gravity), 200 in 20
 * frames, shakes 22, stops 30; then flies at 12, kept level (cos 0.9995 a frame); from its 35th frame of flight it
 * binds (radius 70: the map breaks it) and, unless arg2 (a straight shot), starts chasing when Mario is within arg0
 * (default 2000) and in front of it (90 degrees each side, every way with arg3): it turns towards 200 up Mario at
 * cos(1 - arg1 x 0.0001) a frame (arg1 default 3). It breaks on the map, on Mario (an explosion attack), 3000 from him
 * while chasing, after 600 frames of straight flight; 180 frames later the next one comes out. row_turn 0 appear, 1
 * move, 2 chase, 3 broken; row_step its frames. ponytail: no star pointer freeze, no roll (rotation z), the
 * explosion is SpaceMine's, Torpedo / MagnumKiller not handled */
static void hurt_mario(V3 from, float speed);
static int kcl_query(V3 c, float radius, Hit *out, int max);
static Killer *killer_of(int row)
{
    for (int i = 0; i < nkillers; i++) if (killers[i].row == row) return &killers[i];
    if (nkillers == MAX_KILLERS) return NULL;
    killers[nkillers].row = row;
    return &killers[nkillers++];
}

static V3 turn_to(V3 v, V3 to, float cosmax) /* turnVecToVecCos: at most acos(cosmax) towards to */
{
    float c = vdot(v, to);
    if (c >= cosmax) return to;
    V3 side = vnorm(vsub(to, vmul(v, c)));
    float s = sqrtf(1.0f - cosmax * cosmax);
    return vnorm(vadd(vmul(v, cosmax), vmul(side, s)));
}

static int pointer_a(void);
static int cursor_sees(V3 p, float px);
static void update_killers(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->killer || !row_shown(in->row)) continue;
        Killer *k = killer_of(in->row);
        if (!k) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        V3 base = v3(in->m[12], in->m[13], in->m[14]), up = up_at(base, v3(in->m[4], in->m[5], in->m[6]));
        ++*t;
        if (*ph == 3) {
            if (*t >= 180) { *ph = 0; *t = 0; }
            continue;
        }
        if (*ph == 0) {
            V3 f = vnorm(vtangent(v3(in->m[8], in->m[9], in->m[10]), up));
            int s = *t < 20 ? *t : 20;
            k->front = f;
            k->pos = vadd(base, vmul(f, 200.0f * s / 20.0f));
            if (*t > 20 && *t < 42) {
                int u = *t - 20;
                k->pos = vadd(k->pos, vmul(f, (22 - u) * sinf(u * 100.0f * 0.0174533f) * 18.0f / 22.0f));
            }
            if (*t >= 72) { *ph = 1; *t = 0; }
            continue;
        }
        V3 g = up_at(k->pos, up);
        int brk = 0;
        if (pointer_a() && cursor_sees(k->pos, 40.0f)) { /* tryFreeze: R2 pointing at it */
            if (!k->freeze) printf("homing killer %d frozen\n", in->row);
            k->freeze = 20;
        }
        if (k->freeze) { /* Freeze: stopped, its timers held (ponytail: no shake) */
            k->freeze--; --*t;
            if (vlen(vsub(c, k->pos)) < 70.0f + HIT_BODY_R) { hurt_mario(k->pos, 25.0f); brk = 1; }
            if (!brk) continue;
        }
        if (*ph == 1) {
            V3 lvl = vnorm(vtangent(k->front, g));
            if (vlen(lvl) > 0.5f) k->front = turn_to(k->front, lvl, 0.9995f);
            if (*t > 600) brk = 1;
            V3 to = vsub(mario->pos, k->pos);
            float d = vlen(to), start = r->arg[0] > 0 ? (float)r->arg[0] : 2000.0f;
            if (*t >= 35 && r->arg[2] <= 0 && d < start && d > 0 &&
                (r->arg[3] > 0 || vdot(vmul(to, 1.0f / d), k->front) > 0.0f)) { *ph = 2; printf("homing killer %d chases\n", in->row); }
        } else {
            V3 aim = vnorm(vsub(vadd(mario->pos, vmul(mario->up, 200.0f)), k->pos));
            k->front = turn_to(k->front, aim, 1.0f - (r->arg[1] >= 0 ? r->arg[1] : 3) * 0.0001f);
            if (vlen(vsub(mario->pos, k->pos)) > 3000.0f) brk = 1;
        }
        k->pos = vadd(k->pos, vmul(k->front, 12.0f));
        Hit h[1];
        if (vlen(vsub(c, k->pos)) < 70.0f + HIT_BODY_R) {
            hurt_mario(k->pos, 25.0f);
            printf("homing killer %d hits Mario\n", in->row);
            brk = 1;
        } else if ((*ph == 2 || *t >= 35) && kcl_query(k->pos, 70.0f, h, 1)) {
            printf("homing killer %d breaks on the map\n", in->row);
            brk = 1;
        }
        if (brk) {
            float m[16];
            basis_matrix(m, k->pos, g, k->front, 0);
            fx_emit(FX_SPACEMINE_EXPLOSION, m);
            *ph = 3; *t = 0;
        }
    }
}

static void update_flows(void)
{
    for (int i = 0; i < nflows; i++) {
        const Flow *f = &flows[i];
        for (int k = 0; k < f->n; k++) {
            Pickup *p = &pieces[f->first + k];
            if (p->alive != PICK_ON) continue;
            float c = fmodf(f->step * k + f->speed * game_frame, f->len);
            p->pos = rail_at(f->pts, f->np, c);
        }
    }
}

/* LavaProminence.cpp: Wait arg0 frames (default 180; SW_A, if it has one, must be on), Sign 90, MoveStartExtra (it
 * comes in along the rail's start direction from 300 before it at arg1 a frame, default 20), MoveLoop along the rail
 * to its length - arg3, MoveEndExtra on along the end direction until 300 past the end, hidden, Wait again; its body
 * (120 x scale) burns Mario. row_turn 0 wait, 1 sign, 2 coming in, 3 on the rail, 4 going out; row_step its frames.
 * ponytail: no Sign / Start / End / drop effects, the ball not turned to the rail (its front as placed) */
static float prom_len(const Inst *in)
{
    float l = 0;
    for (u32 i = 0; i + 1 < in->prom_n; i++)
        l += vlen(v3(in->prom[3 * i + 3] - in->prom[3 * i], in->prom[3 * i + 4] - in->prom[3 * i + 1], in->prom[3 * i + 5] - in->prom[3 * i + 2]));
    return l;
}

static V3 prom_pos(const Inst *in)
{
    const Row *r = &sc->objs[in->row];
    const float *p = in->prom;
    u32 n = in->prom_n;
    float speed = r->arg[1] > 0 ? (float)r->arg[1] : 20.0f, t = row_step[in->row];
    V3 a = v3(p[0], p[1], p[2]), b = v3(p[3 * n - 3], p[3 * n - 2], p[3 * n - 1]);
    switch (row_turn[in->row]) {
    case 2: return vadd(a, vmul(vnorm(v3(p[3] - p[0], p[4] - p[1], p[5] - p[2])), speed * t - 300.0f));
    case 3: return rail_at(p, n, speed * t);
    default: return vadd(b, vmul(vnorm(v3(p[3 * n - 3] - p[3 * n - 6], p[3 * n - 2] - p[3 * n - 5], p[3 * n - 1] - p[3 * n - 4])), speed * t));
    }
}

static void update_prom(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->prom || !row_shown(in->row)) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        float speed = r->arg[1] > 0 ? (float)r->arg[1] : 20.0f;
        if (*ph == 0 && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) { *t = 0; continue; }
        ++*t;
        if (*ph == 0 && *t >= (r->arg[0] > 0 ? r->arg[0] : 180)) { *ph = 1; *t = 0; }
        else if (*ph == 1 && *t >= 90) { *ph = 2; *t = 0; }
        else if (*ph == 2 && *t >= (int)(300.0f / speed)) { *ph = 3; *t = 0; }
        else if (*ph == 3 && speed * *t >= prom_len(in) - (r->arg[3] > 0 ? r->arg[3] : 0)) { *ph = 4; *t = 0; }
        else if (*ph == 4 && speed * *t > 300.0f) { *ph = 0; *t = 0; }
        if (*ph < 2) continue;
        if (vlen(vsub(c, prom_pos(in))) < 120.0f * r->scale[0] + HIT_BODY_R && !mario->hurt_timer) {
            hurt_mario(prom_pos(in), 15.0f);
            printf("lava prominence %d burns" "\n", in->row);
        }
    }
}

/* BeeFlowerHover.cpp: turns arg0 / 100 degrees a frame about its Y (default 0.5); Mario touching it from above when he
 * is not a bee (Bee Mario stands on it) is HardTouch: its collision goes at once, it shows ThroughMario (~30
 * frames, ponytail: it just shows on), hides, and 60 frames later Appears (~30) with its collision back. row_turn 1
 * while gone, row_step its frames. ponytail: no rail mover, no low model LOD */
static void carry(const float old[16], const Inst *in);
static void update_beeflowers(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->beeflower) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        if (*ph) {
            if (++*t >= 120) { *ph = 0; *t = 0; }
        } else if (mario_ground == i && !bee_mode) { /* isMsgFloorTouch, not a bee */
            *ph = 1; *t = 0;
            printf("bee flower %d: Mario goes through" "\n", in->row);
        }
        float base[16], rot[16], old[16], a = (r->arg[0] > 0 ? r->arg[0] / 100.0f : 0.5f) * game_frame * 0.0174533f;
        memcpy(old, in->m, 64);
        mat_translate_rot_scale(base, r->pos, r->rot, r->scale);
        mat_identity(rot);
        rot[0] = rot[10] = cosf(a);
        rot[8] = sinf(a); rot[2] = -rot[8];
        mat_mul(in->m, base, rot);
        if (mario_ground == i && !*ph) carry(old, in);
    }
}

/* TicoRail.cpp: a Luma on its rail from the nearest point: Move (speed 0 to 15 over 200 frames; at the rail's end it
 * turns back, round again on a closed rail), after 500 frames + a random bit Stop (15 to 0 over ~30), Wait 60 +
 * random, LookAround 160 (turning 1.2 degrees a frame left, right, left), MoveSign (~30, half the time it turns
 * back), Move again. row_turn 0 not started, 1 move, 2 stop, 3 wait, 4 look around, 5 sign; row_rdir its direction;
 * row_coord, row_speed, row_step. ponytail: no talk, the look-around is its facing only, the Spin / Turn BCKs not
 * played (static Tico model) */
static void ticorail_place(Inst *in)
{
    const unsigned char *e = in->ticorail;
    const float *p = (const float *)(e + 24);
    u32 np = *(const u32 *)(e + 8);
    int row = in->row;
    V3 at = rail_at(p, np, row_coord[row]), ahead = rail_at(p, np, row_coord[row] + 5.0f * row_rdir[row]);
    V3 up = up_at(at, v3(in->m[4], in->m[5], in->m[6])), f = vsub(ahead, at);
    float a = row_turn[row] == 4 ? 1.2f * 0.0174533f * (row_step[row] < 40 ? row_step[row] : row_step[row] < 120 ? 80 - row_step[row] : row_step[row] - 160)
                                 : 0.0f; /* the look around */
    basis_matrix(in->m, at, up, vlen(f) > 1e-3f ? f : v3(in->m[8], in->m[9], in->m[10]), a);
    for (int j = 0; j < 12; j++) if (j % 4 != 3) in->m[j] *= in->s;
}

static void update_ticorails(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->ticorail) continue;
        int row = in->row;
        unsigned short *t = &row_step[row];
        signed char *ph = &row_turn[row];
        const unsigned char *e = in->ticorail;
        const float *p = (const float *)(e + 24);
        u32 np = *(const u32 *)(e + 8);
        float len = tsuki_len(e);
        if (!*ph) { /* moveCoordAndTransToNearestRailPos; Wait or Move at random */
            const Row *r = &sc->objs[row];
            V3 at = v3(r->pos[0], r->pos[1], r->pos[2]);
            float best = 1e30f, acc = 0, c = 0;
            for (u32 k = 0; k + 1 < np; k++) {
                V3 a = v3(p[3 * k], p[3 * k + 1], p[3 * k + 2]), b = v3(p[3 * k + 3], p[3 * k + 4], p[3 * k + 5]);
                V3 ab = vsub(b, a);
                float l = vlen(ab), u = l > 0 ? vdot(vsub(at, a), ab) / (l * l) : 0;
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                float d = vlen(vsub(at, vadd(a, vmul(ab, u))));
                if (d < best) { best = d; c = acc + u * l; }
                acc += l;
            }
            row_coord[row] = c; row_rdir[row] = 1; row_speed[row] = 0;
            *ph = rand() & 1 ? 1 : 3; *t = 0;
        }
        ++*t;
        int closed = *(const unsigned short *)(e + 2);
        float speed = 0;
        switch (*ph) {
        case 1: speed = *t < 200 ? 15.0f * *t / 200.0f : 15.0f;
            if (*t >= 500 + (row * 37) % 120) { *ph = 2; *t = 0; }
            break;
        case 2: speed = *t < 30 ? 15.0f * (1.0f - *t / 30.0f) : 0.0f;
            if (*t >= 30) { *ph = 3; *t = 0; }
            break;
        case 3: if (*t >= 60 + (row * 53) % 60) { *ph = 4; *t = 0; } break;
        case 4: if (*t >= 160) { *ph = 5; *t = 0; if (rand() & 1) row_rdir[row] = -row_rdir[row]; } break;
        default: if (*t >= 30) { *ph = 1; *t = 0; } break;
        }
        float n = row_coord[row] + speed * row_rdir[row];
        if (n >= len || n <= 0) {
            if (closed) n = n >= len ? n - len : n + len;
            else { n = n >= len ? len : 0; row_rdir[row] = -row_rdir[row]; }
        }
        row_coord[row] = n;
        ticorail_place(in);
    }
}

/* DeadLeaves.cpp: a spin by it (its sensor 70 at 30 up) brings out its item once (arg0 0 a coin, 1 three star
 * pieces). PomponPlant.cpp (Pompon2Plant / Pompon4Plant): Mario touching its flower (sensor 70 at (55, 170, -100)
 * in its frame) bounces off it (sendMsgTouchJump) and it gives arg1 (default 1) coins, or star pieces with arg0 0,
 * once; the pointer touching it (cursor) or a star piece shot do too. row_turn 1 once given. ponytail: no Spin /
 * Swing animation, the bounce is a plain 20 up */
static void cursor_ray(V3 *o, V3 *d);
static int cursor_sees(V3 p, float px);
static int pointer_a(void);
static void pop_coins(V3 p, V3 up, int n);
static void drop_pieces(V3 p, V3 up, int n);
static void giver_give(const Inst *in, V3 at, V3 up)
{
    const Row *r = &sc->objs[in->row];
    row_turn[in->row] = 1;
    if (in->giver == 1) {
        if (r->arg[0] == 0) pop_coins(at, up, 1);
        else if (r->arg[0] == 1) drop_pieces(at, up, 3);
    } else {
        int n = r->arg[1] > 0 ? r->arg[1] : 1;
        if (r->arg[0] == 0) drop_pieces(at, up, n); else pop_coins(at, up, n);
    }
    printf("%s %d gives its item" "\n", in->giver == 1 ? "dead leaves" : "pompon plant", in->row);
}

static V3 giver_sensor(const Inst *in)
{
    if (in->giver == 1) return vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(vnorm(v3(in->m[4], in->m[5], in->m[6])), 30.0f));
    V3 x = v3(in->m[0], in->m[1], in->m[2]), y = v3(in->m[4], in->m[5], in->m[6]), z = v3(in->m[8], in->m[9], in->m[10]);
    return vadd(v3(in->m[12], in->m[13], in->m[14]), vadd(vadd(vmul(x, 55.0f), vmul(y, 170.0f)), vmul(z, -100.0f)));
}

static void update_givers(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->giver || row_turn[in->row]) continue;
        V3 s = giver_sensor(in), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        float d = vlen(vsub(c, s));
        if (in->giver == 1) {
            if (mario->spin_timer > 0 && d < 70.0f + 100.0f) giver_give(in, s, up);
        } else if (d < 70.0f + HIT_BODY_R) { /* the touch jump */
            mario->vup = 20.0f; mario->grounded = 0; mario->jump_kind = 0;
            giver_give(in, s, up);
        } else if (pointer_a() && cursor_sees(s, 30.0f))
            giver_give(in, s, up);
    }
}

/* Birikyu.cpp (BigBirikyu; BirikyuWithFace as Birikyu): on its rail it goes back and forth at arg0 (default 10) a
 * frame from the nearest point, waiting at an end point its point_arg0 frames (Move / WaitAtEdge); without a rail it
 * circles 400 round its place about its Y at arg0 / 400 radians a frame (MoveCircle, starting 400 along its Z).
 * Touching Mario (120 x scale, 50 for Birikyu) is an electric attack and it stops 90 frames (Attack); R2 with the
 * pointer on it holds it still (StopPointing). row_turn 0 not started, 1 moving, 2 attack, 3 waiting at an end;
 * row_coord / row_rdir on a rail, row_angle the circle; row_step its frames. ponytail: no group stop, no shadow */
static void biri_place(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    V3 p;
    if (in->biri_rail) p = rail_at((const float *)(in->biri_rail + 24), *(const u32 *)(in->biri_rail + 8), row_coord[in->row]);
    else {
        float base[16], one[3] = {1, 1, 1};
        mat_translate_rot_scale(base, r->pos, r->rot, one);
        V3 y = v3(base[4], base[5], base[6]), z = v3(base[8], base[9], base[10]), x = vcross(y, z);
        float a = row_angle[in->row];
        p = vadd(row_pos(r), vmul(vadd(vmul(z, cosf(a)), vmul(x, sinf(a))), 400.0f));
    }
    in->m[12] = p.x; in->m[13] = p.y; in->m[14] = p.z;
}

static void update_birikyu(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->biri) continue;
        const Row *r = &sc->objs[in->row];
        int row = in->row;
        unsigned short *t = &row_step[row];
        signed char *ph = &row_turn[row];
        float speed = r->arg[0] > 0 ? (float)r->arg[0] : 10.0f;
        if (!*ph) { /* moveCoordAndTransToNearestRailPos, or 400 along its Z */
            row_coord[row] = 0; row_rdir[row] = 1; row_angle[row] = 0;
            if (in->biri_rail) {
                const float *p = (const float *)(in->biri_rail + 24);
                u32 np = *(const u32 *)(in->biri_rail + 8);
                V3 at = row_pos(r);
                float best = 1e30f, acc = 0;
                for (u32 k = 0; k + 1 < np; k++) {
                    V3 a = v3(p[3 * k], p[3 * k + 1], p[3 * k + 2]), ab = vsub(v3(p[3 * k + 3], p[3 * k + 4], p[3 * k + 5]), a);
                    float l = vlen(ab), u = l > 0 ? vdot(vsub(at, a), ab) / (l * l) : 0;
                    u = u < 0 ? 0 : u > 1 ? 1 : u;
                    float d = vlen(vsub(at, vadd(a, vmul(ab, u))));
                    if (d < best) { best = d; row_coord[row] = acc + u * l; }
                    acc += l;
                }
            }
            *ph = 1; *t = 0;
        }
        ++*t;
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        float rad = (in->biri == 1 ? 120.0f : 50.0f) * r->scale[0];
        if (*ph == 2) { if (*t >= 90) { *ph = 1; *t = 0; } }
        else if (*ph == 3) {
            const unsigned char *e = in->biri_rail;
            u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
            const unsigned char *pa = e + 24 + 12 * np + 12 * nr;
            int wait = *(const int *)(pa + 12 * (row_coord[row] > 0 ? nr - 1 : 0) + 4);
            if (*t >= wait) { *ph = 1; *t = 0; }
        } else if (!(pointer_a() && cursor_sees(p, 30.0f))) { /* not held by the pointer */
            if (in->biri_rail) {
                const unsigned char *e = in->biri_rail;
                u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
                const unsigned char *pa = e + 24 + 12 * np + 12 * nr;
                float len = tsuki_len(e), n = row_coord[row] + speed * row_rdir[row];
                if (n >= len || n <= 0) { /* isRailReachedGoal: reverse, wait its point_arg0 */
                    n = n >= len ? len : 0;
                    row_rdir[row] = -row_rdir[row];
                    if (*(const int *)(pa + 12 * (n > 0 ? nr - 1 : 0) + 4) > 0) { *ph = 3; *t = 0; }
                }
                row_coord[row] = n;
            } else
                row_angle[row] = fmodf(row_angle[row] + speed / 400.0f, 6.2831853f);
        }
        biri_place(in);
        p = v3(in->m[12], in->m[13], in->m[14]);
        if (*ph != 2 && vlen(vsub(c, p)) < rad + HIT_BODY_R && !mario->hurt_timer) { /* sendMsgEnemyAttackElectric */
            hurt_mario(p, 15.0f);
            *ph = 2; *t = 0;
            printf("birikyu %d shocks Mario" "\n", row);
        }
    }
}

/* PunchingKinoko.cpp: a mushroom whose head (a GroundChecker 130 up its stalk) is pulled back to its place
 * (velocity += offset x 0.008, x 0.94 at rest, x 0.97 swinging). A spin by the head punches it (+50 away from Mario
 * along the ground, Punched 5 frames, PunchedBrake up to 40 at x 0.9), then it swings back (Swing, until slower
 * than 40 and within 50 of its place); punched or swinging it blows walkers it touches away, swinging it knocks
 * Mario (faster than 15; 30 and up a hard knock); a star piece pushes it 15; a hip drop crushes it 180 frames (flat,
 * head on the ground), 60 more to come back; landing on it pushes it 5 down. row_turn 0 wait, 1 swing, 2 punched,
 * 3 brake, 4 crushed, 5 crushed end; row_step its frames. ponytail: the model is posed static (no Ball joint), so it
 * is tilted to the head and stretched along its stalk; the head keeps its height over the base plane, not over the
 * ground (addVelocityKeepHeight's shadow); no collision for the head, no 2P pointer snap, no breakables hit */
static void punch_place(Inst *in)
{
    V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), up0 = vnorm(v3(in->m0[4], in->m0[5], in->m0[6]));
    V3 d = vsub(in->head, base);
    float l = vlen(d), s = in->s;
    V3 up = l > 1e-3f ? vmul(d, 1.0f / l) : up0;
    float stretch = row_turn[in->row] == 4 ? 0.25f : l / 130.0f;
    basis_matrix(in->m, base, up, v3(in->m0[8], in->m0[9], in->m0[10]), 0);
    for (int j = 0; j < 3; j++) { in->m[j] *= s; in->m[4 + j] *= s * stretch; in->m[8 + j] *= s; }
}

static void update_punch(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->punch || !row_shown(in->row)) continue;
        int row = in->row;
        signed char *ph = &row_turn[row];
        unsigned short *t = &row_step[row];
        V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), up = up_at(base, v3(in->m0[4], in->m0[5], in->m0[6]));
        V3 rest = vadd(base, vmul(up, 130.0f)), off = vsub(rest, in->head);
        ++*t;
        V3 to = vsub(in->head, c);
        float d = vlen(to), speed = vlen(in->hvel);
        if (*ph == 4) { /* Crushed: flat on the ground */
            in->head = vadd(base, vmul(up, 20.0f)); in->hvel = v3(0, 0, 0);
            if (*t >= 180) { *ph = 5; *t = 0; }
            punch_place(in);
            continue;
        }
        if ((*ph == 0 || *ph == 1) && mario->spin_timer > 0 && d < 70.0f + 100.0f) { /* requestPunch */
            V3 dir = vtangent(to, up);
            dir = vlen(dir) > 1e-3f ? vnorm(dir) : vnorm(vtangent(mario->face, up));
            in->hvel = vadd(in->hvel, vmul(dir, 50.0f));
            *ph = 2; *t = 0;
            printf("punching kinoko %d punched" "\n", row);
        } else if ((*ph == 0 || *ph == 5) && mario->hip == HIP_FALL && d < 70.0f + HIT_BODY_R && vdot(to, up) < 0) { /* requestCrush */
            *ph = 4; *t = 0; mario->hip = HIP_LAND; mario->hip_t = 0; mario->vup = 0;
            printf("punching kinoko %d crushed" "\n", row);
            punch_place(in);
            continue;
        } else if (*ph != 5 && mario->vup < 0 && d < 70.0f + HIT_BODY_R && vdot(to, up) < -40.0f) { /* requestTrample */
            in->hvel = vsub(in->hvel, vmul(up, 5.0f));
            mario->vup = 15.0f; mario->grounded = 0;
        }
        switch (*ph) {
        case 0: case 5: /* Wait, CrushedEnd */
            in->hvel = vmul(vadd(in->hvel, vmul(off, 0.008f)), 0.94f);
            if (*ph == 5 && *t >= 60) { *ph = 0; *t = 0; }
            break;
        case 1: /* Swing */
            in->hvel = vmul(vadd(in->hvel, vmul(off, 0.008f)), 0.97f);
            if (vlen(in->hvel) < 40.0f && vlen(off) < 50.0f) { *ph = 0; *t = 0; }
            break;
        default: { /* Punched, PunchedBrake: keep the height over the base plane */
            float h = vdot(vsub(in->head, base), up), dv = (130.0f - h) * 0.5f;
            dv = dv > 60.0f ? 60.0f : dv < -60.0f ? -60.0f : dv;
            in->hvel = vadd(vtangent(in->hvel, up), vmul(up, dv));
            in->hvel = vmul(in->hvel, *ph == 2 ? 0.99f : 0.9f);
            if (*ph == 2 && *t > 5) { *ph = 3; *t = 0; }
            else if (*ph == 3 && *t > 40) { *ph = 1; *t = 0; }
            break;
        }
        }
        in->head = vadd(in->head, in->hvel);
        if (*ph >= 1 && *ph <= 3) { /* sendMsgToEnemyAttackBlow: walkers it touches are blown away */
            for (int k = 0; k < nkuribo; k++) {
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_PACKUN || e->kind == WALK_SPIDER ||
                    e->kind == WALK_BUTTERFLY || e->kind == WALK_TERESA || e->kind == WALK_KARON ||
                    vlen(vsub(in->head, vadd(e->pos, vmul(e->up, 50.0f)))) > 70.0f + 60.0f) continue;
                e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up); pop_coins(e->pos, e->up, 1);
                printf("punching kinoko %d blows walker row %d" "\n", row, e->row);
            }
        }
        if (*ph == 1 && speed >= 15.0f && vlen(vsub(c, in->head)) < 70.0f + HIT_BODY_R && !mario->hurt_timer) { /* isEnableHitPlayer */
            hurt_mario(in->head, speed >= 30.0f ? 25.0f : 12.0f);
            in->hvel = vmul(in->hvel, 0.3f);
            *ph = 0; *t = 0;
            printf("punching kinoko %d hits Mario" "\n", row);
        }
        punch_place(in);
    }
}

/* CocoNut.cpp: a coconut (radius 55 x scale) waiting where it is placed. A spin by it sends it off at 35 (up 20,
 * light gravity 0.4 until it lands: then it blows away walkers it touches and breaks), a hip drop at 25 (up 15,
 * Mario jumps away), Mario running into it faster than it rolls kicks it (at least 18, up 0.7 of his speed), a star
 * piece at 10 (up 5). Rolling (Move): gravity adds 1 a frame up to 25; landing faster than 2.5 bounces at -0.5;
 * on the ground the speed goes x 0.925; a wall reflects its direction (x 0.8); slower than 1.5 on the ground it stops.
 * It turns as it rolls (updateRotate). A walker hit, a death area, the void or water (ponytail: not checked) bring it
 * back to its place after a break (60 frames hidden). row_turn 0 wait, 1 move, 2 broken; row_step its frames.
 * ponytail: no effects or sounds, no coconut-to-coconut hits, no breakables, no moving ground (velocity areas) */
static int area_inside(const Row *r, V3 p);
static void coco_spawn(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    in->head = row_pos(r);
    in->cfront = vnorm(v3(in->m[8], in->m[9], in->m[10]));
    in->cspd = in->cfall = 0; in->cstrong = 0;
    memcpy(in->m0, in->m, 64);
    for (int j = 0; j < 12; j++) in->m0[j] /= in->s; /* the rotation alone */
    row_turn[in->row] = 0; row_step[in->row] = 0;
}

static void coco_place(Inst *in)
{
    memcpy(in->m, in->m0, 64);
    for (int j = 0; j < 12; j++) in->m[j] *= in->s;
    in->m[12] = in->head.x; in->m[13] = in->head.y; in->m[14] = in->head.z;
}

/* hit: off along `dir` (flattened to the ground) at speed `spd`, up at -fall */
static void coco_hit(Inst *in, V3 dir, float spd, float fall, V3 up)
{
    V3 f = vtangent(dir, up);
    if (vlen(f) > 1e-3f) in->cfront = vnorm(f);
    in->cspd = spd; in->cfall = fall;
    row_turn[in->row] = 1; row_step[in->row] = 0;
}

static void update_coconuts(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->coco || !row_shown(in->row)) continue;
        int row = in->row;
        signed char *ph = &row_turn[row];
        unsigned short *t = &row_step[row];
        float rad = 55.0f * in->s;
        V3 up = up_at(in->head, mario->up);
        ++*t;
        if (*ph == 2) { /* Break, then ReplaceReady back at its place */
            if (*t >= 60) { coco_spawn(in); coco_place(in); }
            continue;
        }
        V3 to = vsub(in->head, c);
        float d = vlen(to);
        if (*ph == 0 || (*ph == 1 && *t > 15)) {
            if (mario->spin_timer > 0 && d < rad + 100.0f && (*ph == 0 || *t > SPIN_TIME)) { /* once a spin */ /* isMsgPlayerSpinAttack */
                coco_hit(in, to, 35.0f, -20.0f, up);
                in->cstrong = 1;
                printf("coconut %d spun" "\n", row);
            } else if (mario->hip == HIP_FALL && d < rad + HIT_BODY_R) { /* isMsgPlayerHipDrop: sendMsgAwayJump */
                coco_hit(in, to, 25.0f, -15.0f, up);
                mario->hip = 0; mario->vup = 20.0f; mario->hvel = vmul(vnorm(vtangent(vmul(to, -1.0f), up)), 5.0f);
                printf("coconut %d hip dropped" "\n", row);
            } else if (d < rad + HIT_BODY_R * 0.6f) {
                V3 mv = mario->hvel;
                float ms = vlen(mv), along = ms > 1e-3f ? vdot(vnorm(to), vmul(mv, 1.0f / ms)) : 0;
                float cs = *ph == 1 ? vlen(vadd(vmul(in->cfront, in->cspd), vmul(up, -in->cfall))) : 0;
                if (in->cfall >= 0 && ms > 1.0f && ms >= cs && along >= 0.7071f) { /* isValidPushedFromPlayer: a kick */
                    float k = ms; /* _94 . his velocity, _94 being his direction */
                    coco_hit(in, mv, k * 1.1f > 18.0f ? k * 1.1f : 18.0f, -0.7f * k, up);
                    printf("coconut %d kicked" "\n", row);
                } else { /* sendMsgPush: Mario is pushed out */
                    V3 out = vtangent(vmul(to, -1.0f), up);
                    float l = vlen(out);
                    if (l > 1e-3f) mario->pos = vadd(mario->pos, vmul(out, (rad + HIT_BODY_R * 0.6f - d) / l));
                }
            }
        }
        if (*ph == 0) { coco_place(in); continue; }
        /* Move: processMove */
        float spd0 = vlen(vadd(vmul(in->cfront, in->cspd), vmul(up, -in->cfall)));
        V3 vel = vadd(vmul(in->cfront, in->cspd), vmul(up, -in->cfall));
        V3 gn = up, wn;
        int wall = 0;
        V3 center = vadd(in->head, vel);
        int ground = collide_n(&center, up, rad, &gn, &wn, &wall);
        in->head = center;
        if (ground && in->cfall > 0) {
            if (in->cfall > 2.5f) in->cfall *= -0.5f; /* bounce */
            else in->cfall = 0;
            in->cspd *= 0.925f;
            in->cstrong = 0;
        }
        if (wall && vdot(in->cfront, wn) < 0) { /* reflect off the wall */
            V3 n = vnorm(vtangent(wn, up));
            in->cfront = vnorm(vsub(in->cfront, vmul(n, 2.0f * vdot(in->cfront, n))));
            in->cspd *= 0.8f;
        }
        float fall = in->cfall + (in->cstrong ? 0.4f : 1.0f); /* updateGravity */
        in->cfall = fall > 25.0f ? 25.0f : fall;
        { /* updateRotate: about front x up, |v| x 180 / (r pi) degrees a frame */
            V3 ax = vcross(in->cfront, up);
            float a = -spd0 * (ground ? 1.0f : 0.75f) / rad;
            if (vlen(ax) > 1e-3f && a != 0) {
                float rm[16], o[16];
                ax = vnorm(ax);
                float cs = cosf(a), sn = sinf(a), k = 1 - cs;
                float R[9] = {cs + ax.x * ax.x * k, ax.y * ax.x * k + ax.z * sn, ax.z * ax.x * k - ax.y * sn,
                              ax.x * ax.y * k - ax.z * sn, cs + ax.y * ax.y * k, ax.z * ax.y * k + ax.x * sn,
                              ax.x * ax.z * k + ax.y * sn, ax.y * ax.z * k - ax.x * sn, cs + ax.z * ax.z * k};
                memcpy(o, in->m0, 64);
                memcpy(rm, in->m0, 64);
                for (int col = 0; col < 3; col++) /* R x column */
                    for (int j = 0; j < 3; j++)
                        rm[4 * col + j] = R[j] * o[4 * col] + R[3 + j] * o[4 * col + 1] + R[6 + j] * o[4 * col + 2];
                memcpy(in->m0, rm, 48);
            }
        }
        int broken = 0;
        if (in->cstrong) /* sendMsgToEnemyAttackBlow */
            for (int k = 0; k < nkuribo && !broken; k++) {
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY || e->kind == WALK_TERESA ||
                    vlen(vsub(in->head, vadd(e->pos, vmul(e->up, 50.0f)))) > rad + 60.0f) continue;
                e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up); pop_coins(e->pos, e->up, 1);
                printf("coconut %d blows walker row %d" "\n", row, e->row);
                broken = 1;
            }
        for (int k = 0; k < nsambos && in->cspd > 1.5f && !broken; k++) { /* CocoSambo(Head)::receiveMsgEnemyAttack: tryToFallHead */
            const Inst *bi = NULL;
            for (int q = 0; q < ninsts; q++) if (insts[q].row == sambos[k].row) bi = &insts[q];
            for (int j = 0; j < 4 && !broken; j++) {
                V3 sp = j < 3 ? sambo_joint(k, bi, j, v3(40.0f * sambos[k].s, 0, 0)) : sambo_head_pos(k, bi);
                if (vlen(vsub(in->head, sp)) < (j < 3 ? 80.0f : 100.0f) * sambos[k].s + rad && sambo_try_fall(k)) {
                    printf("coconut %d knocks coco sambo row %d down" "\n", row, sambos[k].row);
                    broken = 1;
                }
            }
        }
        if (!broken && lost(in->head)) broken = 1; /* tryDisappear */
        for (int k = 0; k < nlogic && !broken; k++)
            if (logic[k].kind == LOGIC_DEATH && row_shown(logic[k].row) && area_inside(&sc->objs[logic[k].row], in->head)) broken = 1;
        if (broken) { *ph = 2; *t = 0; in->m[12] = 1e9f; continue; } /* hidden until it comes back */
        if (ground && in->cspd < 1.5f) { /* tryMoveEnd: statusToWait (calcMoveSpeed: the rolling speed) */
            *ph = 0; in->cspd = in->cfall = 0;
            printf("coconut %d stops" "\n", row);
        }
        coco_place(in);
    }
}

/* JumpStand.cpp: Mario landing on it is held (bound); TrampleBound throws him at step 5 along -gravity at 26, 32 if
 * he pressed jump in those 5 frames; HipDropBound (a hip drop landing) sinks him 6 - ease a frame for 15 frames, then
 * 43 along its up. row_turn: 0 wait, 1 trample, 2 high trample, 3 hip drop; row_step frames; jstand_pos the held
 * place. ponytail: no stick steering on the throw; the bound is a squash of the model (jstand_squash), not its BCK */
static V3 jstand_pos;
static int jstand_prev = -1; /* mario_ground last frame */

static float jstand_squash(const Inst *in)
{
    int t = row_step[in->row], ph = row_turn[in->row], n = ph == 3 ? 20 : 10;
    return ph && t < n ? 1.0f - (ph == 3 ? 0.4f : 0.25f) * sinf(3.14159f * t / n) : 1.0f;
}

static void update_jumpstands(void)
{
    int prev = jstand_prev;
    jstand_prev = mario_ground;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->jstand) continue;
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        if (!*ph) {
            if (mario_ground != i || prev == i || mario->launch >= 0) continue;
            *ph = hip_hit ? 3 : 1; *t = 0;
            jstand_pos = mario->pos;
            printf("jump stand %d: %s\n", in->row, hip_hit ? "hip drop" : "trample");
        }
        ++*t;
        int hold = *ph == 3 ? 15 : 5;
        if (*t <= hold) {
            if (*ph == 1 && pad_pressed(PAD_CROSS)) *ph = 2;
            if (*ph == 3) {
                float u = (float)*t / 15.0f;
                jstand_pos = vsub(jstand_pos, vmul(up, 6.0f - (1.0f - (1.0f - u) * (1.0f - u))));
            }
            mario->pos = jstand_pos;
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hip = 0;
            if (*t == hold) { /* endBindAndShootUp */
                float v = *ph == 3 ? 43.0f : *ph == 2 ? 32.0f : 26.0f;
                if (*ph == 3) mario->up = up;
                mario->vup = v; mario->grounded = 0; mario->jump_kind = 0; mario->air_timer = 0;
                printf("jump stand %d throws at %d\n", in->row, (int)v);
            }
        } else if (*t >= (*ph == 3 ? 20 : 10)) *ph = 0;
    }
}

/* a rail-moving part's translation: its rail at row_coord */
static V3 rail_at(const float *p, u32 n, float d);
static void railer_place(Inst *in)
{
    const unsigned char *e = in->railer;
    V3 p = rail_at((const float *)(e + 24), *(const u32 *)(e + 8), row_coord[in->row]);
    in->m[12] = p.x; in->m[13] = p.y; in->m[14] = p.z;
}

/* SmallStone.cpp (SmallStone / CircleShell / CircleStrawberry): 8 members on a ring of 300 around it, each dropped
 * onto the ground and raised arg0; a spin within 400 of its centre (120 up) breaks them all (star pieces when arg2
 * is set: one each, ponytail for appearStarPiece's burst). They share the plant groups' members (plants) */
static int stone_row(const Row *r)
{
    const char *n = sc->types + 32 * r->type;
    return strcmp(n, "SmallStone") == 0 || strcmp(n, "CircleShell") == 0 || strcmp(n, "CircleStrawberry") == 0;
}

static int plant_count(const Row *r) { return stone_row(r) ? 8 : r->arg[0] > 0 ? r->arg[0] : 7; }

/* the first collision under `from` along -up within len, stepped by spheres of radius 20 */
static int kcl_query(V3 c, float radius, Hit *out, int max);
static int drop_to_ground(V3 from, V3 up, float len, V3 *hit)
{
    Hit h[1];
    for (float d = 0; d <= len; d += 30.0f) {
        V3 p = vsub(from, vmul(up, d));
        if (kcl_query(p, 20.0f, h, 1)) { *hit = vsub(p, vmul(up, 20.0f)); return 1; }
    }
    return 0;
}

static void place_plants(const Inst *in)
{
    int row = in->row;
    if (plant_state[row] || !in->kcl_ready) return;
    const Row *r = &sc->objs[row];
    V3 x, z, c = row_pos(r), up = up_at(c, v3(0, 1, 0));
    row_axes(r, &x, &z);
    x = vnorm(vtangent(x, up));
    if (vlen(x) < 0.5f) x = vnorm(vtangent(z, up));
    V3 y = vcross(up, x);
    int ring = 0, n = plant_count(r), hits = 0, stone = stone_row(r);
    float angle = 0, step = stone ? 6.2831853f / 8 : 6.2831853f, radius = stone ? 300.0f : 0;
    for (int k = 0; k < n; k++) {
        Plant *p = &plants[plant_first[row] + k];
        V3 at = vadd(c, vadd(vmul(x, cosf(angle) * radius), vmul(y, sinf(angle) * radius)));
        p->up = up_at(at, up);
        hits += p->ok = drop_to_ground(vadd(at, vmul(p->up, 100.0f)), p->up, 1000.0f, &p->pos);
        if (stone && r->arg[0] > 0) p->pos = vadd(p->pos, vmul(p->up, (float)r->arg[0])); /* mPlacementOffset */
        angle += step;
        if (stone) continue;
        if (angle >= 6.2831f) { ring++; angle = 0; step = 6.2831853f / (ring * 6); radius = 160.0f * ring; }
    }
    plant_state[row] = hits > 0; /* none: its ground is not loaded yet, try again at the next rebind */
}

void game_rebind(void)
{
    const Scene *s = sc;
    m_coin = scene_model(s, "Coin");
    m_purple = scene_model(s, "PurpleCoin");
    m_piece = scene_model(s, "StarPiece");
    m_kuribo = scene_model(s, "Kuribo");
    m_takohei = scene_model(s, "TakoHei");
    m_packun = scene_model(s, "PackunPetit");
    m_kotebug = scene_model(s, "KoteBug");
    m_spider = scene_model(s, "JumpSpider");
    m_meramera = scene_model(s, "Meramera");
    m_takobo = scene_model(s, "Takobo");
    m_mkoopa = scene_model(s, "MechanicKoopaMini");
    m_kari = scene_model(s, "Karikari");
    m_kmini = scene_model(s, "KuriboMini");
    m_karon = scene_model(s, "Karon");
    m_teresa = scene_model(s, "Teresa");
    m_bfly = scene_model(s, "Butterfly");
    m_stink = scene_model(s, "StinkBugSmall");
    m_note = scene_model(s, "NoteFairy");
    m_poihana = scene_model(s, "Poihana");
    m_marioball = scene_model(s, "MarioFireBall");
    m_bubble = scene_model(s, "AirBubble");
    m_ota = scene_model(s, "OtaKing"); m_otafoot = scene_model(s, "OtaKingFoot"); m_coco = scene_model(s, "CocoNut");
    m_fireball2 = scene_model(s, "FireBall"); m_firebubble = scene_model(s, "FireBubble");
    m_sambohead = scene_model(s, "CocoSamboHead");
    m_dino = scene_model(s, "DinoPackun"); m_dball = scene_model(s, "DinoPackunTailBall"); m_degg = scene_model(s, "DinoPackunEggShell");
    m_deggbrk = scene_model(s, "DinoPackunEggShellBreak");
    m_unizo[0] = scene_model(s, "Unizo"); m_unizo[1] = scene_model(s, "UnizoLand"); m_unizo[2] = scene_model(s, "UnizoShoal");
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    int t_spg = scene_type(s, "StarPieceGroup"), t_flow = scene_type(s, "StarPieceFlow"), t_erail = scene_type(s, "ElectricRail"), t_tako = scene_type(s, "TakoHei"),
        t_packun = scene_type(s, "PackunPetit"), t_kote = scene_type(s, "KoteBug"),
        t_spider = scene_type(s, "JumpSpider"), t_mera = scene_type(s, "Meramera"), t_takobo = scene_type(s, "Takobo"),
        t_mkoopa = scene_type(s, "MechanicKoopaMini"), t_kari = scene_type(s, "Karikari"), t_kmini = scene_type(s, "KuriboMini"),
        t_karon = scene_type(s, "Karon"), t_teresa = scene_type(s, "Teresa"), t_bfly = scene_type(s, "Butterfly"),
        t_stink = scene_type(s, "StinkBugSmall"), t_uni = scene_type(s, "Unizo"), t_unil = scene_type(s, "UnizoLand"),
        t_unis = scene_type(s, "UnizoShoal"), t_poi = scene_type(s, "Poihana");
    m_erail = scene_model(s, "ElectricRail");
    m_fireball = scene_model(s, "FireBarBall");
    m_rock = scene_model(s, "RockCreator");
    m_shell = scene_model(s, "KoopaJrShipCannonShell");
    if (!m_shell) m_shell = scene_model(s, "IronCannonLauncherPoint");
    /* what Mario rides (Fluff, SpaceCocoon, JumpBranch) keeps its state across the rebuild: found again by its row */
    int ride_row[3] = {flf.inst >= 0 ? insts[flf.inst].row : -1, coc.inst >= 0 ? insts[coc.inst].row : -1,
                       jb.inst >= 0 ? insts[jb.inst].row : -1};
    V3 ride_head[2] = {flf.inst >= 0 ? insts[flf.inst].head : v3(0, 0, 0), coc.inst >= 0 ? insts[coc.inst].head : v3(0, 0, 0)};
    V3 ride_vel[2] = {flf.inst >= 0 ? insts[flf.inst].hvel : v3(0, 0, 0), coc.inst >= 0 ? insts[coc.inst].hvel : v3(0, 0, 0)};
    int wait_row = coc.wait >= 0 ? insts[coc.wait].row : -1;
    ninsts = 0;
    for (u32 i = 0; i < s->nobjs; i++) { /* every placed model without behaviour: planets and the rest */
        const Row *r = &s->objs[i];
        if (r->type == t_coin || r->type == t_pc || r->type == t_piece || r->type == t_kuribo || r->type == t_circle ||
            r->type == t_tako || r->type == t_packun || r->type == t_kote || r->type == t_spider || r->type == t_mera ||
            r->type == t_takobo || r->type == t_mkoopa || r->type == t_kari || r->type == t_kmini || r->type == t_karon || r->type == t_teresa || r->type == t_bfly ||
            r->type == t_rail || r->type == t_spg || r->type == t_flow || r->type == t_erail || r->type == t_stink || r->type == scene_type(s, "NoteFairy") ||
            r->type == t_uni || r->type == t_unil || r->type == t_unis || r->type == t_poi || r->type == scene_type(s, "RockCreator") ||
            r->type == scene_type(s, "DinoPackun") || r->type == scene_type(s, "OtaKing"))
            continue;
        const char *name = s->types + 32 * r->type;
        const Model *md = scene_model(s, name);
        if (!md || (row_dead && !row_shown(i))) continue;
        if (row_step && (r->arg[2] == 1 || r->arg[2] == 2) && !row_step[i] && strcmp(name, "EarthenPipe") == 0)
            continue; /* a hidden pipe (EarthenPipeNrvHide) */
        if (logic && (strcmp(name, "YellowChip") == 0 || strcmp(name, "BlueChip") == 0) && !chip_group_out(r, name))
            continue;
        Inst *in = &insts[ninsts++];
        in->md = md;
        in->row = i;
        in->anim = md->skin ? obj_anim(i, md, name) : NULL;
        int dc, dp;
        in->breaks = breakable(name, r, &dc, &dp);
        in->collapse = strcmp(name, "CollapsePlane") == 0 ? (r->arg[0] > 0 ? r->arg[0] : 140) : 0;
        in->hazard = strcmp(name, "SpaceMine") == 0 ? 1 : strcmp(name, "NeedlePlant") == 0 ? 2 :
                     strcmp(name, "BlackHole") == 0 ? 3 : strcmp(name, "BlackHoleCube") == 0 ? 4 : 0;
        in->item = strcmp(name, "PowerStar") == 0 ? ITEM_STAR : strcmp(name, "BenefitItemOneUp") == 0 ? ITEM_ONEUP :
                   strcmp(name, "BenefitItemLifeUp") == 0 ? ITEM_LIFEUP : strcmp(name, "QuestionCoin") == 0 ? ITEM_QCOIN :
                   strcmp(name, "YellowChip") == 0 || strcmp(name, "BlueChip") == 0 ? ITEM_CHIP :
                   strcmp(name, "KeySwitch") == 0 ? ITEM_KEY : strcmp(name, "MorphItemNeoFire") == 0 ? ITEM_FIRE :
                   strcmp(name, "MorphItemNeoBee") == 0 ? ITEM_BEE : 0; /* KeySwitch.cpp: taken (isMsgItemGet), its SW_A on */
        in->firebar = strcmp(name, "FireBar") == 0;
        in->invisible = strncmp(name, "InvisibleWall", 13) == 0 || strcmp(name, "IronCannonLauncherPoint") == 0;
        in->flip = strcmp(name, "FlipPanel") == 0 || strcmp(name, "FlipPanelReverse") == 0;
        if (in->flip) {
            char v[48];
            snprintf(v, sizeof v, "%s#PanelA", name);
            in->md_flip[0] = scene_model(sc, v);
            snprintf(v, sizeof v, "%s#PanelEnd", name);
            in->md_flip[1] = scene_model(sc, v);
        }
        in->npc = npc_class(name);
        in->fat = strcmp(name, "TicoFat") == 0;
        in->sambo = strcmp(name, "CocoSambo") == 0;
        in->sky = sky_class(name);
        in->shoot = strcmp(name, "ShootingStar") == 0;
        in->steam = strcmp(name, "LavaSteam") == 0;
        in->geyser = strcmp(name, "LavaGeyser") == 0;
        in->assembly = strncmp(name, "AssemblyBlock", 13) == 0;
        in->jstand = strcmp(name, "JumpStand") == 0;
        in->warppod = strcmp(name, "WarpPod") == 0;
        in->kiraira = strcmp(name, "Kiraira") == 0;
        in->meteor = NULL;
        in->killer = strcmp(name, "HomingKiller") == 0;
        in->gcap = strcmp(name, "GCaptureTarget") == 0;
        in->tspot = strcmp(name, "TreasureSpot") == 0 ? 1 : strcmp(name, "CoinFlower") == 0 ? 2 : 0;
        in->prom = NULL;
        in->beeflower = strcmp(name, "BeeFlowerHover") == 0;
        in->jbranch = strcmp(name, "JumpBranch") == 0;
        in->biri = strcmp(name, "BigBirikyu") == 0 ? 1 : strcmp(name, "Birikyu") == 0 ? 2 : 0;
        in->punch = strcmp(name, "PunchingKinoko") == 0;
        in->coco = strcmp(name, "CocoNut") == 0;
        in->cocoon = strcmp(name, "SpaceCocoon") == 0;
        in->fluff = strcmp(name, "Fluff") == 0;
        in->jet = strcmp(name, "JetTurtle") == 0;
        in->fountain = strcmp(name, "FountainBig") == 0;
        in->stray = strcmp(name, "StrayTico") == 0;
        in->bubble = strcmp(name, "AirBubble") == 0 ? 1 : strcmp(name, "AirBubbleGenerator") == 0 ? 2 : 0;
        in->snowman = strcmp(name, "SnowMan") == 0;
        in->candle = strcmp(name, "PhantomCandlestand") == 0 ? 1 : strcmp(name, "TeresaMansionCandlestand") == 0 ? 2 :
                     strcmp(name, "CandlestandIceVolcano") == 0 ? 3 : 0;
        in->screw = strcmp(name, "ScrewSwitch") == 0 ? 1 : strcmp(name, "ScrewSwitchReverse") == 0 ? 2 : 0;
        in->biri_rail = in->biri && scene_blob_g && r->path >= 0 ? find_path(scene_blob_g, r->zone, r->path) : NULL;
        in->giver = strcmp(name, "DeadLeaves") == 0 ? 1 : strcmp(name, "Pompon2Plant") == 0 || strcmp(name, "Pompon4Plant") == 0 ? 2 : 0;
        in->ticorail = scene_blob_g && r->path >= 0 && strcmp(name, "TicoRail") == 0 ? find_path(scene_blob_g, r->zone, r->path) : NULL;
        if (scene_blob_g && r->path >= 0 && strncmp(name, "LavaProminence", 14) == 0 && strcmp(name, "LavaProminenceTriple")) {
            int cl;
            in->prom = find_rail(scene_blob_g, r->zone, r->path, &in->prom_n, &cl);
            if (in->prom_n < 2) in->prom = NULL;
        }
        if (scene_blob_g && r->path >= 0 && (strcmp(name, "MeteorStrike") == 0 || strcmp(name, "MeteorStrikeEnvironment") == 0)) {
            u32 n; int cl;
            const float *p = find_rail_of(scene_blob_g, r->zone, r->path, &n, &cl, 1);
            in->meteor = p && n >= 2 ? p : NULL;
        }
        in->tsuki = scene_blob_g && r->path >= 0 && strcmp(name, "Tsukidashikun") == 0 ?
                    find_path(scene_blob_g, r->zone, r->path) : NULL;
        in->plant = strcmp(name, "FlowerGroup") == 0 || strcmp(name, "FlowerBlueGroup") == 0 || strcmp(name, "CutBushGroup") == 0 ||
                    stone_row(r);
        in->hipsw = strncmp(name, "PicketSwitch", 12) == 0 || strcmp(name, "HipDropSwitch") == 0 ? 1 :
                    strcmp(name, "HipDropTimerSwitch") == 0 ? 2 : 0;
        in->rotor = r->arg[7] > 0 && (r->arg[7] >> 30 & 1) && r->arg[4] != -1 && r->arg[4] != 0; /* -1: not a map part */
        in->railer = scene_blob_g && r->arg[7] > 0 && (r->arg[7] >> 30 & 1) && r->path >= 0 ?
                     find_path(scene_blob_g, r->zone, r->path) : NULL;
        in->kcl = scene_kcl(s, name);
        in->zone = r->zone;
        mat_translate_rot_scale(in->m, r->pos, r->rot, r->scale);
        in->s = r->scale[0];
        if (in->hazard >= 3) { /* the core model: 0.5 * arg0 / 1000, else the scale (the cube: 1) */
            float k = 0.5f * (r->arg[0] > 0 ? r->arg[0] / 1000.0f : in->hazard == 3 ? r->scale[0] : 1.0f);
            float one[3] = {k, k, k};
            mat_translate_rot_scale(in->m, r->pos, r->rot, one);
            in->s = k;
        }
        in->reach = md->radius * in->s;
        if (in->rotor && row_angle) rotor_matrix(in);
        if (in->railer && row_coord) railer_place(in);
        if (in->tsuki && row_coord && row_turn[i]) tsuki_place(in);
        if (in->ticorail && row_coord && row_turn[i]) ticorail_place(in);
        if (in->biri && row_coord && row_turn[i]) biri_place(in);
        if (in->coco) coco_spawn(in);
        if (in->fountain || in->stray) memcpy(in->m0, in->m, 64);
        if (in->stray) in->head = row_turn[i] ? (stray_at[i % 64].x || stray_at[i % 64].y || stray_at[i % 64].z ? stray_at[i % 64] : v3(in->m[12], in->m[13], in->m[14])) : v3(in->m[12], in->m[13], in->m[14]);
        if (in->jet) { memcpy(in->m0, in->m, 64); in->head = v3(in->m[12], in->m[13], in->m[14]); in->hvel = v3(0, 0, 0); }
        if (in->fluff) { memcpy(in->m0, in->m, 64); in->head = v3(in->m[12], in->m[13], in->m[14]); in->hvel = v3(0, 0, 0); }
        if (in->cocoon) { /* at rest at its neutral point, arg1 (default 100) up its axis */
            memcpy(in->m0, in->m, 64);
            in->head = cocoon_neutral(in);
            in->hvel = v3(0, 0, 0);
            cocoon_place(in);
        }
        if (in->punch) { /* its head 130 up (calcPositionUpOffset), at rest */
            memcpy(in->m0, in->m, 64);
            V3 base = v3(in->m[12], in->m[13], in->m[14]);
            in->head = vadd(base, vmul(up_at(base, v3(in->m[4], in->m[5], in->m[6])), 130.0f)); /* -gravity */
            in->hvel = v3(0, 0, 0);
        }
        if (in->assembly) {
            float h[16];
            assembly_home(r, h);
            in->gup = up_at(v3(h[12], h[13], h[14]), v3(0, 0, 0));
            if (row_step) assembly_place(in);
        }
        in->kcl_ready = 0;
        zone_grow(in);
    }
    for (int i = 0; plant_first && i < ninsts; i++)
        if (insts[i].plant) { insts[i].kcl_ready = 1; place_plants(&insts[i]); }
    memset(zone_kcl, 0, sizeof zone_kcl);
    for (int i = 0; i < ninsts; i++) if (insts[i].kcl && insts[i].zone < 64) zone_kcl[insts[i].zone] = 1;
    int jet_row = jet.inst >= 0 ? insts[jet.inst].row : -1;
    V3 jet_head = jet.inst >= 0 ? insts[jet.inst].head : v3(0, 0, 0), jet_vel = jet.inst >= 0 ? insts[jet.inst].hvel : v3(0, 0, 0);
    flf.inst = coc.inst = jb.inst = coc.wait = -1;
    jet.inst = -1;
    for (int i = 0; i < ninsts && jet_row >= 0; i++)
        if (insts[i].row == jet_row) { jet.inst = i; insts[i].head = jet_head; insts[i].hvel = jet_vel; jet_place(&insts[i]); }
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (in->row == ride_row[0]) { flf.inst = i; in->head = ride_head[0]; in->hvel = ride_vel[0]; fluff_place(in); }
        if (in->row == ride_row[1]) { coc.inst = i; in->head = ride_head[1]; in->hvel = ride_vel[1]; cocoon_place(in); }
        if (in->row == ride_row[2]) jb.inst = i;
        if (in->row == wait_row) coc.wait = i;
    }
}

/* Zone spheres for streaming: every placement row of the zone (centre of their box, farthest + 500), grown to cover
 * each model of the zone once one is bound (game_rebind). Zones with no planet (objects only) are found too. */
static V3 zone_c[MAX_ZONES];
static float zone_r[MAX_ZONES];

static void zone_spheres(void)
{
    V3 lo[MAX_ZONES], hi[MAX_ZONES];
    int seen[MAX_ZONES] = {0};
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->zone >= MAX_ZONES) continue;
        V3 p = row_pos(r);
        if (!seen[r->zone]) { lo[r->zone] = hi[r->zone] = p; seen[r->zone] = 1; }
        lo[r->zone] = v3(fminf(lo[r->zone].x, p.x), fminf(lo[r->zone].y, p.y), fminf(lo[r->zone].z, p.z));
        hi[r->zone] = v3(fmaxf(hi[r->zone].x, p.x), fmaxf(hi[r->zone].y, p.y), fmaxf(hi[r->zone].z, p.z));
    }
    for (int z = 0; z < MAX_ZONES; z++) {
        zone_r[z] = seen[z] ? 0.0f : -1.0f;
        if (seen[z]) zone_c[z] = vmul(vadd(lo[z], hi[z]), 0.5f);
    }
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->zone >= MAX_ZONES) continue;
        float d = vlen(vsub(row_pos(r), zone_c[r->zone])) + 500.0f;
        if (d > zone_r[r->zone]) zone_r[r->zone] = d;
    }
}

static void zone_grow(const Inst *in)
{
    if (in->zone >= MAX_ZONES || zone_r[in->zone] < 0) return;
    float d = vlen(vsub(v3(in->m[12], in->m[13], in->m[14]), zone_c[in->zone])) + in->reach;
    if (d > zone_r[in->zone]) zone_r[in->zone] = d;
}

/* The zones Mario needs in full (need: within 1000 of a zone's sphere, a full zone whose model he is inside, the exit
 * pipe's zone; 2: at once, he is out of every full zone's model or came out of a pipe) and soon (on foot within 5000, flying the launch star's landing point within 1500), each with its
 * priority dist[z] (distance to its sphere, smaller first). main.c keeps as many of their chains full as the memory
 * room allows: overlapping zones load together (CosmosGarden's PuffballZone sits in other zones' spheres) */
void game_zone_sets(const unsigned char *full, unsigned char *need, unsigned char *soon, float *dist)
{
    V3 fly = mario->launch >= 0 ? launch_pos(&launches[mario->launch], 1.0f) : mario->pos;
    for (int z = 0; z < MAX_ZONES; z++) {
        need[z] = soon[z] = 0;
        dist[z] = 1e30f;
        if (zone_r[z] < 0) continue;
        float d = vlen(vsub(mario->pos, zone_c[z])) - zone_r[z];
        dist[z] = d;
        if (d < 1000.0f) need[z] = 1;
        if (mario->launch < 0 ? d < 5000.0f : vlen(vsub(fly, zone_c[z])) - zone_r[z] < 1500.0f) soon[z] = 1;
    }
    int on_full = 0;
    for (int i = 0; i < ninsts; i++) { /* standing in a full zone's model: it stays (no going low and back) */
        const Inst *in = &insts[i];
        V3 d = vsub(mario->pos, v3(in->m[12], in->m[13], in->m[14]));
        if (in->zone < MAX_ZONES && full[in->zone] && vdot(d, d) < in->reach * in->reach) {
            need[in->zone] = 1;
            if (dist[in->zone] > 0) dist[in->zone] = 0;
            on_full = 1;
        }
    }
    if (!on_full) /* out of every full model: the zones he is near or soon reaches are wanted at once (2), else read in
                   * the background (OceanRing 4 starts 2000 over a zone only `soon`: no collision until it streamed in,
                   * a miss a frame) */
        for (int z = 0; z < MAX_ZONES; z++)
            if ((need[z] || (soon[z] && mario->launch < 0)) && !full[z]) need[z] = 2;
    if (pipe_zone >= 0 && pipe_zone < MAX_ZONES) { /* came out of a pipe in a low zone: its collision is needed now */
        if (full[pipe_zone]) pipe_zone = -1;
        else { need[pipe_zone] = 2; dist[pipe_zone] = -1e30f; }
    }
}

int game_test_block(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->breaks != 3) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = up_at(p, v3(0, 1, 0));
        mario->up = up;
        mario->pos = vadd(p, vmul(up, -(2.0f * BODY_RADIUS + 120.0f))); /* under it, jumping */
        mario->hvel = v3(0, 0, 0); mario->vup = 20.0f; mario->grounded = 0;
        return in->row;
    }
    return -1;
}

/* Test builds: Mario put on the k-th live enemy of a kind: how 0 above it falling (stomp), 1 beside it spinning, 2
 * beside it standing, 3 standing 200 off, 4 in a hip drop fall 250 over it, 5 a hip drop starting 300 over
 * Mario's own place. Returns its row or -1. */
int game_test_enemy(int kind, int k, int how)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (e->kind != kind || e->state == KURIBO_DEAD || k--) continue;
        V3 side = vnorm(vtangent(v3(1, 0, 0), e->up));
        if (vlen(side) < 0.5f) side = vnorm(vtangent(v3(0, 0, 1), e->up));
        if (how == 5) { /* a hip drop from 300 over where Mario is */
            mario->pos = vadd(mario->pos, vmul(mario->up, 300.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->grounded = 0; mario->hip = HIP_START; mario->hip_t = 0;
            return e->row;
        }
        mario->up = e->up;
        mario->pos = how == 0 ? vadd(e->pos, vmul(e->up, 150.0f)) : vadd(e->pos, vmul(side, how == 1 ? 140.0f : how == 3 ? 200.0f : 90.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = how == 0 ? -5.0f : 0.0f; mario->grounded = 0;
        mario->spin_timer = how == 1 ? SPIN_TIME : 0;
        if (how == 4) { mario->pos = vadd(e->pos, vmul(e->up, 250.0f)); mario->hip = HIP_FALL; mario->hip_t = 0; }
        return e->row;
    }
    return -1;
}

int game_kind_alive(int kind)
{
    int n = 0;
    for (int i = 0; i < nkuribo; i++) n += kuribo[i].kind == kind && kuribo[i].state != KURIBO_DEAD;
    return n;
}

int game_test_box(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->breaks != 1) continue; /* spin-breakable */
#ifdef BOX_ONLY_NEW /* the SimpleBreakableObj ones only */
        { const char *n = sc->types + 32 * sc->objs[in->row].type; if (!strncmp(n, "Punch", 5) || !strncmp(n, "Wood", 4) || !strncmp(n, "Crystal", 7)) continue; }
#endif
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = up_at(p, v3(0, 1, 0));
        mario->pos = vadd(p, vmul(up, in->reach + 20.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->spin_timer = SPIN_TIME;
        return in->row;
    }
    return -1;
}

int game_test_cage(int on_star)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->item != ITEM_STAR || !star_wait[in->row] || (!on_star && star_wait[in->row] < 0)) continue; /* -1: the 100-coin one */
        if (on_star) {
            int state;
            V3 p = star_pos(in, &state);
            mario->pos = vadd(p, vmul(up_at(p, mario->up), -HIT_BODY_H));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return state;
        }
        int row = star_wait[in->row] - 1; /* its declarer: Mario spinning on it (by row: its zone may be loaded low) */
        if (row_dead[row]) return -2;
        float reach = 300.0f * sc->objs[row].scale[0];
        for (int k = 0; k < ninsts; k++)
            if (insts[k].row == row) reach = insts[k].reach;
        V3 p = row_pos(&sc->objs[row]), up = up_at(p, v3(0, 1, 0));
        mario->pos = vadd(p, vmul(up, reach + 20.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->spin_timer = SPIN_TIME;
        return row;
    }
    return -1;
}

int game_test_spot(void)
{
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *b = &sc->objs[i];
        int c, p;
        if (row_dead[i] || b->sw[SW_DEAD] < 0 || !breakable(sc->types + 32 * b->type, b, &c, &p)) continue;
        for (u32 k = 0; k < sc->nobjs; k++) {
            const Row *s = &sc->objs[k];
            if (strcmp(sc->types + 32 * s->type, "StarPieceSpot") || s->sw[SW_APPEAR] != b->sw[SW_DEAD] || s->zone != b->zone)
                continue;
            float reach = 150.0f * b->scale[0];
            for (int j = 0; j < ninsts; j++)
                if (insts[j].row == (int)i) reach = insts[j].reach;
            V3 q = row_pos(b), up = up_at(q, v3(0, 1, 0));
            mario->pos = vadd(q, vmul(up, reach + 20.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME;
            return i;
        }
    }
    return -1;
}

int game_test_plant(int k)
{
    for (u32 i = 0; i < sc->nobjs; i++) { /* groups holding items: Mario walking through their k-th placed member */
        if (plant_first[i] < 0 || sc->objs[i].arg[1] <= 0) continue;
        V3 p = row_pos(&sc->objs[i]);
        if (!plant_state[i]) { /* not placed yet: Mario over the group, its zone streams in */
            mario->pos = vadd(p, vmul(up_at(p, mario->up), 100.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
        for (int j = 0; j < plant_count(&sc->objs[i]); j++) {
            const Plant *m = &plants[plant_first[i] + j];
            if (!m->ok || k--) continue;
            mario->pos = m->pos;
            mario->up = m->up;
            mario->hvel = vmul(vnorm(vtangent(v3(1, 0, 0), m->up)), 2.0f);
            mario->vup = 0;
            return i;
        }
    }
    return -1;
}

int game_test_steam(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].steam) continue;
        V3 p = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
        mario->pos = vadd(p, vmul(vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6])), 100.0f - HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    return -1;
}

int game_test_assembly(int k)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].assembly || k-- > 0) continue;
        const Row *r = &sc->objs[insts[i].row];
        mario->pos = v3(r->pos[0], r->pos[1], r->pos[2]);
        mario->pos = vadd(mario->pos, vmul(mario->up, 300.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    return -1;
}

int game_assembled(void)
{
    int n = 0;
    for (int i = 0; i < ninsts; i++)
        if (insts[i].assembly && row_turn[insts[i].row] >= 1) n++;
    return n;
}

int game_test_jumpstand(int hip)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].jstand) continue;
        V3 up = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6]));
        mario->pos = vadd(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), vmul(up, 600.0f));
        mario->up = up;
        mario->hvel = v3(0, 0, 0); mario->vup = -5.0f; mario->grounded = 0;
        if (hip) { mario->hip = HIP_FALL; mario->hip_t = 0; }
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario over its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "JumpStand") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = v3(r->pos[0], r->pos[1] + 600.0f, r->pos[2]);
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

static int warp_pair(int row);
static V3 row_axis_y(const Row *r);
int game_test_warppod(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].warppod || warp_pair(insts[i].row) < 0) continue;
        const Row *r = &sc->objs[insts[i].row];
        mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(row_axis_y(r), 10.0f - HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    return -1;
}

int game_test_tsuki(int drop, float *coord)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].tsuki) continue;
        *coord = row_coord[insts[i].row];
        if (drop) {
            mario->pos = vadd(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), vmul(mario->up, insts[i].reach + 100.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
        }
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario over its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "Tsukidashikun") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 400.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_kiraira(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].kiraira) continue;
        V3 up, p = kiraira_pos(&insts[i], &up);
        mario->pos = vsub(p, vmul(mario->up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario over its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "Kiraira") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 600.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_meteor(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].meteor || strcmp(sc->types + 32 * sc->objs[insts[i].row].type, "MeteorStrike")) continue;
        const float *p = insts[i].meteor;
        V3 a = v3(p[0], p[1], p[2]), b = v3(p[3], p[4], p[5]);
        mario->pos = vsub(vadd(a, vmul(vsub(b, a), 0.5f)), vmul(mario->up, HIT_BODY_H)); /* half way down its rail */
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "MeteorStrike") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = v3(r->pos[0], r->pos[1], r->pos[2]); /* its zone's full pack streams in */
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            game_rebind();
            return -2;
        }
    return -1;
}

int game_test_freeze(int on) /* test builds: R2 held with the cursor on the first flying bullet */
{
    test_r2 = 0;
    if (!on) return 0;
    for (int i = 0; i < nkillers; i++) {
        const Killer *k = &killers[i];
        if (row_turn[k->row] != 1 && row_turn[k->row] != 2) continue;
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(k->pos, ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) continue;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        test_r2 = 1;
        return k->row;
    }
    return -1;
}

int game_test_kari_point(int on) /* test builds: R2 with the cursor on the first clinging Karikari */
{
    test_r2 = 0;
    if (!on) return 0;
    for (int i = 0; i < nkuribo; i++) {
        const Kuribo *k = &kuribo[i];
        if (k->kind != WALK_KARIKARI || k->phase != 2 || k->state == KURIBO_DEAD) continue;
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(k->pos, ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) continue;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        test_r2 = 1;
        return k->row;
    }
    return -1;
}

int game_test_killer(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].killer) continue;
        V3 p = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), f = v3(insts[i].m[8], insts[i].m[9], insts[i].m[10]);
        mario->pos = vadd(p, vmul(vnorm(f), 1500.0f)); /* 1500 in front of the launcher */
        mario->hvel = v3(0, 0, 0);
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "HomingKiller") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = v3(r->pos[0], r->pos[1], r->pos[2]);
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_flow(void)
{
    if (!nflows) return -1;
    const Flow *f = &flows[0];
    for (int k = 0; k < f->n; k++)
        if (pieces[f->first + k].alive == PICK_ON) {
            mario->pos = vsub(pieces[f->first + k].pos, vmul(mario->up, HIT_BODY_H));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return f->first + k;
        }
    return -2;
}

int game_test_gcap(int hold)
{
    if (hold == 2) { /* Mario held 300 over the first target */
        for (u32 i = 0; i < sc->nobjs; i++)
            if (strcmp(sc->types + 32 * sc->objs[i].type, "GCaptureTarget") == 0) {
                const Row *r = &sc->objs[i];
                V3 p = v3(r->pos[0], r->pos[1], r->pos[2]);
                mario->pos = vadd(p, vmul(up_at(p, mario->up), 300.0f)); /* the camera settles behind him */
                mario->hvel = v3(0, 0, 0); mario->vup = 0;
                return (int)i;
            }
        return -1;
    }
    test_r2 = hold;
    if (!hold) return gcap.phase;
    for (int i = 0; i < ninsts; i++) { /* the cursor onto the first target near Mario */
        if (!insts[i].gcap) continue;
        V3 p = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
        if (vlen(vsub(p, mario->pos)) > 2400.0f) continue;
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(p, ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) continue;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        return insts[i].row;
    }
    return -1;
}

int game_test_shoot(int kind)
{
    for (int i = 0; i < nkuribo; i++) { /* the cursor onto the first live enemy of the kind, L1 */
        const Kuribo *e = &kuribo[i];
        if (e->kind != kind || e->state == KURIBO_DEAD) continue;
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 p = vadd(e->pos, vmul(e->up, 50.0f));
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(p, ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) return -2;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        if (got_pieces < 1) got_pieces = 1;
        test_l1 = 1;
        return e->row;
    }
    return -1;
}

int game_test_tspot(void)
{
    for (int i = 0; i < ninsts; i++) { /* Mario 600 from the first unopened one, the cursor on it, L1 */
        const Inst *in = &insts[i];
        if (!in->tspot || row_turn[in->row]) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(vadd(p, vmul(up, 50.0f)), ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) return -2;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        if (got_pieces < 1) got_pieces = 1;
        test_l1 = 1;
        return in->row;
    }
    return -1;
}

void game_test_tspot_near(void) /* Mario held 600 off the first unopened one (its row while its zone streams in) */
{
    for (u32 i = 0; i < sc->nobjs; i++) {
        const char *n = sc->types + 32 * sc->objs[i].type;
        if ((strcmp(n, "TreasureSpot") && strcmp(n, "CoinFlower")) || row_dead[i] || row_turn[i]) continue;
        const Row *r = &sc->objs[i];
        V3 p = v3(r->pos[0], r->pos[1], r->pos[2]), up = up_at(p, mario->up);
        mario->pos = vadd(vadd(p, vmul(up, 300.0f)), vmul(vnorm(vtangent(v3(1, 0, 0), up)), 600.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return;
    }
}

int game_test_prom(void)
{
    for (int i = 0; i < ninsts; i++) { /* Mario held on the middle of the first one's rail */
        const Inst *in = &insts[i];
        if (!in->prom) continue;
        V3 p = rail_at(in->prom, in->prom_n, prom_len(in) * 0.5f);
        mario->pos = vsub(p, vmul(mario->up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strncmp(sc->types + 32 * sc->objs[i].type, "LavaProminence", 14) == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = v3(r->pos[0], r->pos[1], r->pos[2]);
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_stone(int spin)
{
    for (u32 i = 0; i < sc->nobjs; i++) { /* Mario at the first unbroken one, spinning */
        const Row *r = &sc->objs[i];
        if (!stone_row(r) || row_dead[i]) continue;
        mario->pos = vadd(row_pos(r), vmul(mario->up, 50.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        if (spin) mario->spin_timer = SPIN_TIME;
        return (int)i;
    }
    return -1;
}

int game_test_beeflower(void)
{
    for (int i = 0; i < ninsts; i++) { /* Mario dropped onto the first one standing */
        const Inst *in = &insts[i];
        if (!in->beeflower || row_turn[in->row]) continue;
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        mario->pos = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(up, in->reach + 50.0f));
        mario->up = up;
        mario->hvel = v3(0, 0, 0); mario->vup = -5.0f; mario->grounded = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "BeeFlowerHover") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 600.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

void game_test_hold(int set) /* test builds: Mario kept where he is (set: from now on), still */
{
    static V3 at;
    if (set) at = mario->pos;
    mario->pos = at;
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
}

void game_walker_state(int kind, char *out, int n) /* test builds: the first live walker of a kind */
{
    for (int i = 0; i < nkuribo; i++) {
        const Kuribo *e = &kuribo[i];
        if (e->kind != kind || e->state == KURIBO_DEAD) continue;
        snprintf(out, n, "row %d phase %d timer %d dist %d facing %d", e->row, e->phase, e->timer, (int)vlen(vsub(mario->pos, e->pos)),
                 vdot(mario->face, vsub(e->pos, mario->pos)) > 0);
        return;
    }
    snprintf(out, n, "none");
}

int game_test_point_walker(int kind)
{
    for (int i = 0; i < nkuribo; i++) { /* the cursor onto the first live one of the kind */
        const Kuribo *e = &kuribo[i];
        if (e->kind != kind || e->state == KURIBO_DEAD) continue;
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(e->pos, ce);
        float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
        if (z <= 0) return -2;
        cur_x = 320.0f + f * vdot(to, s) / z; cur_y = 224.0f - f * vdot(to, u) / z;
        return e->row;
    }
    return -1;
}

int game_test_ticorail(float *coord)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].ticorail) continue;
        *coord = row_coord[insts[i].row];
        return insts[i].row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "TicoRail") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_jbranch(int phase)
{
    for (int i = 0; i < ninsts; i++) { /* phase 0: Mario jumping up into the first one; 1: X (the jump off) */
        const Inst *in = &insts[i];
        if (!in->jbranch) continue;
        if (phase == 1) return jb.inst >= 0 ? in->row : -1;
        V3 up = up_at(v3(in->m[12], in->m[13], in->m[14]), mario->up);
        mario->up = up;
        mario->pos = vsub(v3(in->m[12], in->m[13], in->m[14]), vmul(up, HIT_BODY_H + 60.0f));
        mario->hvel = vmul(vnorm(v3(in->m[8], in->m[9], in->m[10])), 8.0f); mario->vup = 10.0f; mario->grounded = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "JumpBranch") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_giver(int kind)
{
    for (int i = 0; i < ninsts; i++) { /* Mario at the first ungiven one: by DeadLeaves spinning, into a Pompon's flower */
        const Inst *in = &insts[i];
        if (in->giver != kind || row_turn[in->row]) continue;
        V3 s = giver_sensor(in);
        mario->pos = vsub(s, vmul(mario->up, HIT_BODY_H));
        if (kind == 1) { mario->pos = vadd(mario->pos, vmul(vnorm(v3(in->m[0], in->m[1], in->m[2])), 120.0f)); mario->spin_timer = SPIN_TIME; }
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) { /* not bound yet: Mario at its row, its zone streams in */
        const char *n = sc->types + 32 * sc->objs[i].type;
        if (kind == 1 ? strcmp(n, "DeadLeaves") : strcmp(n, "Pompon2Plant") && strcmp(n, "Pompon4Plant")) continue;
        if (row_turn[i]) continue;
        const Row *r = &sc->objs[i];
        mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return -2;
    }
    return -1;
}

/* Test builds: phase 0 Mario in the air at the first SpaceCocoon's bind point (its row, -2 streaming, -1 none);
 * 1 the cursor on the cocoon and R2 held; 2 the cursor `drag` px lower; 3 R2 let go. Returns coc.phase (0 none) for
 * phases 1-3; 4 only coc.phase */
/* Test builds: phase 0 Mario spinning by the first Fluff (its row, -2 streaming, -1 none); 1 Mario jumping into it
 * (its row_turn, or 9 riding); 2 flf.spins (-1 not riding); 3 Square pressed this frame */
/* Test builds: Mario 300 off the first water plant placed (how many so far, -1 none yet) */
int game_test_waterplant(void)
{
    if (!nwplants) return -1;
    V3 side = vnorm(vcross(wplants[0].up, wplants[0].dir));
    mario->up = wplants[0].up;
    mario->pos = vadd(wplants[0].pos, vmul(side, 300.0f));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return nwplants;
}

int game_test_fluff(int phase)
{
    if (phase == 2) return flf.inst >= 0 ? flf.spins : -1;
    if (phase == 3) { test_sq = 1; return 0; }
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->fluff) continue;
        if (phase < 0) return in->row; /* bound already */
        if (flf.inst >= 0) return 9;
        V3 up = up_at(in->head, mario->up);
        mario->up = up;
        if (phase == 0) {
            mario->pos = vadd(in->head, vmul(vnorm(vtangent(v3(in->m0[0], in->m0[1], in->m0[2]), up)), 120.0f));
            mario->spin_timer = SPIN_TIME;
        } else {
            mario->pos = vsub(in->head, vmul(up, HIT_BODY_H + 40.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 20.0f; mario->grounded = 0; /* jumping up into it */
            return row_turn[in->row];
        }
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "Fluff") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

/* Test builds: Mario 600 in front of the first live StinkBugSmall (how 0), or in a hip drop over it (1); its row,
 * -1 none. game_stink_state: its phase and distance from home */
/* Test builds: Mario 800 off the first live Unizo; its row, -1 none. game_unizo_state: its phase, Mario's distance */
/* Test builds: the first NoteFairy forced out (phase 0; its row, -1 none), Mario onto its k-th live note (phase 1;
 * the notes left, -1 none) */
/* Test builds: Mario onto the first KeySwitch shown (its row; -2 one waits hidden, -1 none); game_key_taken: how
 * many were taken */
/* Test builds: phase 0 Mario onto the first JetTurtle (its row, -1 none); 1 a spin (the throw); game_jet_state:
 * jet.phase (0 none) */
/* Test builds: Mario onto the first waiting screw switch, spinning (its row, -1 none); game_screw_state: the first
 * one's row_turn */
void game_test_spin_now(void) { mario->spin_timer = SPIN_TIME; } /* test builds: a spin this frame */

/* Test builds: Mario by the first burning candlestand, spinning (how 1) or in its flame (0); its row, -1 none */
/* Test builds: Mario onto the first FountainBig's base (its row, -1 none) */
/* Test builds: Fire Mario now (phase 0), a spin (1); game_fire_state: balls flying */
/* Test builds: Bee Mario now (phase 0), A held (1) or let go (2) for the next frames; game_bee_state: flight left */
void game_test_jump(void) { mario->vup = 20.0f; mario->grounded = 0; mario->air_timer = 0; mario->jump_kind = 0; }

int game_test_bee(int phase)
{
    if (phase == 0) { bee_mode = 1; bee_meter = 180; }
    test_cross = phase == 1;
    return bee_mode;
}

int game_bee_state(void) { return bee_mode ? bee_meter : -1; }

int game_test_fire(int phase)
{
    if (phase == 0) fire_time = 1200; else mario->spin_timer = SPIN_TIME;
#ifdef FIRE_AT_CANDLE /* Mario 400 off the first unlit CandlestandIceVolcano, facing it */
    for (int i = 0; i < ninsts; i++)
        if (insts[i].candle == 3 && row_turn[insts[i].row] == 3) {
            V3 up = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6])), x = vnorm(v3(insts[i].m[0], insts[i].m[1], insts[i].m[2]));
            V3 base = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
            if (phase == 0) { mario->up = up; mario->pos = vadd(base, vmul(x, 120.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
            mario->face = vmul(x, -1.0f);
            test_nolost = 0;
            if (phase == 2) { /* a ball dropped right on its flame */
                fireballs[0].pos = vadd(base, vmul(up, 220.0f * insts[i].s)); fireballs[0].vel = v3(0, 0, 0);
                fireballs[0].t = 1; fireballs[0].wall = 0;
            }
            return fire_time;
        }
    if (phase == 0) { /* not bound yet: Mario to the first one's row, held while its zone streams in */
        int t = scene_type(sc, "CandlestandIceVolcano");
        for (u32 i = 0; t >= 0 && i < sc->nobjs; i++)
            if (sc->objs[i].type == t && sc->objs[i].sw[SW_A] >= 0) {
                mario->pos = vadd(row_pos(&sc->objs[i]), vmul(mario->up, 300.0f));
                mario->hvel = v3(0, 0, 0); mario->vup = 0; test_nolost = 1;
                return -2;
            }
    }
#endif
    float best = 1e30f; /* facing the nearest live walker */
    for (int i = 0; i < nkuribo; i++) {
        V3 d = vtangent(vsub(kuribo[i].pos, mario->pos), mario->up);
        if (kuribo[i].state != KURIBO_DEAD && vlen(d) < best && vlen(d) > 1.0f) { best = vlen(d); mario->face = vnorm(d); }
    }
    return fire_time;
}

int game_fire_state(void)
{
    int n = 0;
    for (int b = 0; b < MAX_FIREBALLS; b++) n += fireballs[b].t > 0;
    return n;
}

/* Test builds: Mario onto the first StrayTico not rescued (its row; -1 none left) */
/* Test builds: Mario 300 under the first water area's surface (its row, -1 none); game_swim_state: swimming x 10000 + oxygen */
int game_test_swim(void)
{
    int t[3] = {scene_type(sc, "WaterCube"), scene_type(sc, "WaterSphere"), scene_type(sc, "WaterCylinder")};
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != t[0] && r->type != t[1] && r->type != t[2]) continue;
        V3 x, z;
        row_axes(r, &x, &z);
        mario->pos = r->type == t[1] ? vadd(row_pos(r), vmul(mario->up, 500.0f * r->scale[0] - 300.0f))
                                     : vadd(row_pos(r), vmul(vcross(z, x), (r->type == t[2] ? 500.0f : 1000.0f) * r->scale[1] - 300.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return (int)i;
    }
    return -1;
}

int game_swim_state(void) { return swimming * 10000 + oxygen; }

/* Test builds: Mario 400 from the k-th live walker of class `cls` (its row; -1 none), looking at it */
int game_test_walker(const char *cls, int k)
{
    int t = scene_type(sc, cls);
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (sc->objs[e->row].type != t || e->state == KURIBO_DEAD || k--) continue;
        V3 side = vnorm(vtangent(vcross(e->up, v3(0.3f, 0.1f, 0.9f)), e->up));
        mario->pos = vadd(vadd(e->pos, vmul(side, 400.0f)), vmul(e->up, 50.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
        mario->face = vmul(side, -1.0f);
        return e->row;
    }
    return -1;
}

/* Test builds: Mario onto the next purple coin left (purple coins taken; once all are, onto the waiting star:
 * 1000 + its state) */
int game_test_purple(void)
{
    for (int i = 0; i < npurple; i++)
        if (purple[i].alive == PICK_ON) {
            int z = purple[i].row >= 0 ? sc->objs[purple[i].row].zone : 0;
            mario->pos = vsub(purple[i].pos, vmul(mario->up, HIT_BODY_H)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            test_nolost = 1; /* some float off any planet (taken in flight): no miss while held there */
            if (z < 64 && !zone_kcl[z]) return -2;
            return got_purple;
        }
    return 1000 + game_test_cage(1);
}

/* Test builds: Mario 1500 in front of OtaKing (before it is up: on its SwitchCylinder), spinning when a coconut is
 * within 300 coming at him; game_ota_state: phase x 10 + hits (-1 no OtaKing) */
int game_test_ota(void)
{
    if (ota.row < 0) return -1;
    V3 at = vadd(ota.pos, vmul(ota.f0, 1800.0f)); /* on its bath's rim (1600 - 2000 out, about its base's height) */
    if (ota.nrv == OTA_WAITONSWITCH) { mario->pos = vadd(at, vmul(ota.up, 300.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    for (int b = 0; b < OTA_BALLS; b++) /* a ball coming back: he steps onto its line (sideways, as a player would) */
        if (ota.ball[b].state == 3) {
            V3 d = vnorm(ota.ball[b].vel), rel = vsub(mario->pos, ota.ball[b].pos);
            V3 on = vadd(ota.ball[b].pos, vmul(d, vdot(rel, d)));
            V3 step = vtangent(vsub(on, mario->pos), mario->up);
            if (vdot(rel, d) > 0 && vlen(step) < 700.0f) mario->pos = vadd(mario->pos, step);
        }
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int b = 0; b < OTA_BALLS; b++)
        if ((ota.ball[b].state == 1 || ota.ball[b].state == 3) && vlen(vsub(ota.ball[b].pos, c)) < 380.0f) mario->spin_timer = SPIN_TIME;
    mario->hurt_timer = 8; /* the test watches the rallies: fire balls and bubbles do not wear him down */
    return (ota.nrv == OTA_WAITONSWITCH ? 0 : ota.nrv >= OTA_APPEARSTAR && ota.nrv <= OTA_DEAD ? 5 : 1) * 10 + ota.hits;
}

/* Test builds: Mario 600 in front of the first hungry TicoFat with star pieces, a shot at it (pieces it ate x 1000 +
 * pieces left; -1 none left) */
static int test_fat_nodrop; /* the route test flies there by the pink spin driver instead */
void game_test_fat_nodrop(void) { test_fat_nodrop = 1; }

static int fat_state(int row); /* update_ticofat's table: eaten x 1000 + its nerve */
int game_test_fat(void)
{
    static int f0;
    if (!f0++ || (f0 & 31) == 0)
        for (int k = 0; k < ngroup_wait; k++) printf("fat test: group spin driver row %d shown %d\n", group_wait[k].row, row_shown(group_wait[k].row));
    for (int i = 0; i < ninsts; i++) { /* Mario 200 in front (its talk distance), the pointer on it, L1 held */
        Inst *in = &insts[i];
        if (!in->fat || row_dead[in->row]) continue;
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), z = vnorm(v3(in->m[8], in->m[9], in->m[10]));
        V3 base = v3(in->m[12], in->m[13], in->m[14]);
        if (fat_state(in->row) % 1000 < 5 && (vlen(vsub(mario->pos, base)) > 400.0f || fat_state(in->row) % 1000 == 0)) { /* while it eats */ mario->pos = vadd(vadd(base, vmul(z, 240.0f)), vmul(up, 40.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        if (got_pieces < 3) got_pieces = 3;
        test_feed = 1;
        return fat_state(in->row);
    }
    test_feed = 0;
    int t = scene_type(sc, "TicoFat"); /* none loaded: Mario to its row, its zone streams in */
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++)
        if (sc->objs[i].type == t && !row_dead[i]) {
            mario->pos = row_pos(&sc->objs[i]); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    static int once; /* all fed: Mario dropped 900 over the planet it became (its SW_SLEEP rows), once */
    once |= test_fat_nodrop;
    int pd = scene_type(sc, "StarEggRoadEggPlanetD");
    for (u32 i = 0; pd >= 0 && i < sc->nobjs && !once; i++)
        if (sc->objs[i].type == pd) {
            once = 1;
            mario->pos = vadd(row_pos(&sc->objs[i]), v3(0, 900.0f, 0)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return row_shown(i) ? -3 : -4;
        }
    return -1;
}

/* Test builds: Mario next to DinoPackun's tail ball, on its far side from the head, spinning when it waits there;
 * game_dino_state: phase x 100 + hits x 10 + ball state (-1 no DinoPackun) */
int game_test_dino(void)
{
    if (dino.row < 0) return -1;
    if (dino.phase == 0) { mario->pos = vadd(dino.pos, vadd(vmul(dino.f, 700.0f), vmul(dino.up, 200.0f))); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
    else if ((dino.phase == 1 || dino.phase == 2) && dino.bstate == 0) {
        V3 out = vnorm(vtangent(vsub(dino.bpos, dino.pos), dino.up));
        mario->pos = vadd(dino.bpos, vmul(out, 150.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hurt_timer = 0;
        mario->spin_timer = SPIN_TIME;
    }
    return dino.phase * 100 + dino.hits * 10 + dino.bstate;
}

/* Test builds: Mario 300 over the first AirBubbleGenerator (its row, -1 none), his oxygen down to 100 */
int game_test_bubble(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].bubble == 2) {
            V3 up = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6]));
            mario->pos = vadd(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), vmul(up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; oxygen = 100;
            return insts[i].row;
        }
    return -1;
}

int game_test_stray(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].stray && !row_turn[insts[i].row]) {
            mario->pos = vsub(insts[i].head, vmul(mario->up, HIT_BODY_H));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; test_nolost = 0;
            return insts[i].row;
        }
    int st = scene_type(sc, "StrayTico");
    for (u32 i = 0; st >= 0 && i < sc->nobjs; i++) /* not bound yet: to its row, held while its zone streams in */
        if (sc->objs[i].type == st && !row_turn[i]) {
            mario->pos = vadd(row_pos(&sc->objs[i]), vmul(mario->up, 200.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; test_nolost = 1;
            return -2;
        }
    return -1;
}

int game_test_fountain(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].fountain) {
            mario->up = vnorm(v3(insts[i].m0[4], insts[i].m0[5], insts[i].m0[6]));
            mario->pos = vadd(v3(insts[i].m0[12], insts[i].m0[13], insts[i].m0[14]), vmul(mario->up, 20.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return insts[i].row;
        }
    return -1;
}

int game_test_candle(int how)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].candle && !row_turn[insts[i].row]) {
            V3 up = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6])), x = vnorm(v3(insts[i].m[0], insts[i].m[1], insts[i].m[2]));
            V3 base = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
            mario->up = up;
            mario->pos = how ? vadd(base, vmul(x, 200.0f)) : vsub(vadd(base, vmul(up, (insts[i].candle == 1 ? 220.0f : 300.0f) * insts[i].s)), vmul(up, HIT_BODY_H));
            if (how) mario->spin_timer = SPIN_TIME;
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return insts[i].row;
        }
    return -1;
}

int game_test_screw(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].screw && !row_turn[insts[i].row]) {
            V3 up = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6]));
            mario->up = up;
            mario->pos = vadd(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), vmul(up, 160.0f * insts[i].s));
            mario->hvel = v3(0, 0, 0); mario->vup = -5.0f;
            return insts[i].row;
        }
    return -1;
}

int game_screw_state(void)
{
    for (int i = 0; i < ninsts; i++) if (insts[i].screw) return row_turn[insts[i].row];
    return -1;
}

int game_test_jet(int phase)
{
    if (phase == 1) { mario->spin_timer = SPIN_TIME; return jet.phase; }
    for (int i = 0; i < ninsts; i++)
        if (insts[i].jet) {
            mario->pos = vsub(insts[i].head, vmul(mario->up, 20.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return insts[i].row;
        }
    return -1;
}

int game_jet_state(void) { return jet.phase; }

int game_test_key(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].item == ITEM_KEY) {
            mario->pos = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return insts[i].row;
        }
    int t = scene_type(sc, "KeySwitch");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) if (sc->objs[i].type == t && !row_dead[i]) return -2;
    return -1;
}

int game_key_taken(void)
{
    int t = scene_type(sc, "KeySwitch"), n = 0;
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) n += sc->objs[i].type == t && row_dead[i];
    return n;
}

int game_test_note(int phase)
{
    int t = scene_type(sc, "NoteFairy");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) {
        if (sc->objs[i].type != t) continue;
        int has = 0;
        for (int k = 0; k < nnotes; k++) has |= notes[k].row == (short)i;
        if (!has) continue;
        if (phase == 0) { /* out now, whatever its appear switch */
            const unsigned char *e;
            if (!row_turn[i] && note_rail(&sc->objs[i], &e)) {
                int n = 0;
                for (int k = 0; k < nnotes; k++) n += notes[k].row == (short)i;
                row_turn[i] = 1; row_coord[i] = 0;
                printf("note test: row %d, %d notes, rail %d long\n", (int)i, n, (int)tsuki_len(e));
            }
            return (int)i;
        }
        int left = 0;
        for (int k = 0; k < nnotes; k++) {
            if (notes[k].row != (short)i || notes[k].alive != 1) continue;
            if (!left) { mario->pos = vsub(notes[k].pos, vmul(mario->up, HIT_BODY_H)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
            left++;
        }
        return left ? left : -row_turn[i];
    }
    return -1;
}

/* Test builds: Mario 600 in front of the first Poihana (how 0) or spinning by it (1); its row, -1 none.
 * game_poihana_state: its phase */
int game_test_poihana(int how)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_POIHANA || k->state == KURIBO_DEAD) continue;
        int bound = 0, zone = sc->objs[k->row].zone;
        for (int j = 0; j < ninsts && !bound; j++) bound = insts[j].kcl && insts[j].zone == zone;
        test_nolost = !bound; /* held there (no miss) while its zone streams in */
        mario->pos = vadd(k->pos, vmul(stink_front(k), how ? 150.0f : 300.0f));
        mario->up = k->up;
        if (how) mario->spin_timer = SPIN_TIME;
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return bound ? k->row : -2;
    }
    return -1;
}

int game_poihana_state(void)
{
    for (int i = 0; i < nkuribo; i++) if (kuribo[i].kind == WALK_POIHANA) return kuribo[i].phase;
    return -1;
}

int game_test_unizo(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_UNIZO || k->state == KURIBO_DEAD) continue;
        V3 x, z;
        row_axes(&sc->objs[k->row], &x, &z);
        mario->up = k->up;
        mario->pos = vadd(k->pos, vmul(vnorm(vtangent(z, k->up)), 800.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return k->row;
    }
    return -1;
}

int game_unizo_state(int *dist)
{
    for (int i = 0; i < nkuribo; i++) {
        const Kuribo *k = &kuribo[i];
        if (k->kind != WALK_UNIZO) continue;
        *dist = (int)vlen(vsub(k->pos, mario->pos));
        return k->state == KURIBO_DEAD ? 9 : k->phase;
    }
    return -1;
}

int game_test_stink(int how)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_STINKBUG || k->state == KURIBO_DEAD) continue;
        V3 f = stink_front(k);
        mario->up = k->up;
        if (how == 0) mario->pos = vadd(k->pos, vmul(f, 600.0f));
        else { mario->pos = vadd(k->pos, vmul(k->up, 250.0f)); mario->grounded = 0; mario->hip = HIP_FALL; mario->hip_t = 0; }
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return k->row;
    }
    return -1;
}

int game_stink_state(int *dist)
{
    for (int i = 0; i < nkuribo; i++) {
        const Kuribo *k = &kuribo[i];
        if (k->kind != WALK_STINKBUG) continue;
        *dist = (int)vlen(vsub(k->pos, row_pos(&sc->objs[k->row])));
        return k->state == KURIBO_DEAD ? 9 : k->phase;
    }
    return -1;
}

int game_test_cocoon(int phase, float drag)
{
    if (phase == 4) return coc.inst < 0 ? 0 : coc.phase;
    if (phase) {
        if (coc.inst < 0) return 0;
        Inst *in = &insts[coc.inst];
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw), to = vsub(cocoon_neutral(in), ce);
        float f = 224.0f / tanf(fovy * 3.14159265f / 360.0f), z = vdot(to, fw);
        if (z > 1.0f && phase < 3) { cur_x = 320.0f + vdot(to, s) * f / z; cur_y = 224.0f - vdot(to, u) * f / z + (phase == 2 ? drag : 0); }
        test_r2 = phase < 3;
        return coc.phase;
    }
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->cocoon) continue;
        V3 up = vnorm(v3(in->m0[4], in->m0[5], in->m0[6]));
        mario->pos = vsub(vadd(in->head, vmul(up, 100.0f)), vmul(mario->up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "SpaceCocoon") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_coco(int how)
{
    for (int i = 0; i < ninsts; i++) { /* how 0: Mario spinning by the first waiting one; 1: running into it; 2: none */
        Inst *in = &insts[i];
        if (!in->coco || row_turn[in->row] != 0) continue;
        if (how == 2) return in->row; /* only bound? */
        V3 up = up_at(in->head, mario->up), side = vnorm(vtangent(v3(in->m0[0], in->m0[1], in->m0[2]), up));
        mario->up = up;
        mario->pos = vsub(vadd(in->head, vmul(side, how ? 130.0f : 120.0f)), vmul(up, 55.0f * in->s));
        mario->hvel = how ? vmul(side, -12.0f) : v3(0, 0, 0); mario->vup = 0;
        if (!how) mario->spin_timer = SPIN_TIME;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "CocoNut") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

/* the first CocoNut that is not waiting: its row_turn and its distance from its place, else -1 */
int game_coco_state(int *dist)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->coco || !row_turn[in->row]) continue;
        *dist = (int)vlen(vsub(in->head, row_pos(&sc->objs[in->row])));
        return row_turn[in->row];
    }
    return -1;
}

int game_test_punch(int how)
{
    for (int i = 0; i < ninsts; i++) { /* how 0: Mario spinning by the first one's head; 1: hip drop onto it */
        Inst *in = &insts[i];
        if (!in->punch) continue;
        if (how == 2) return in->row; /* only bound? */
        V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), up = up_at(base, v3(in->m0[4], in->m0[5], in->m0[6]));
        V3 side = vnorm(vtangent(v3(in->m0[0], in->m0[1], in->m0[2]), up));
        if (how == 0) {
            mario->pos = vadd(base, vmul(side, 150.0f));
            mario->spin_timer = SPIN_TIME;
        } else {
            mario->pos = vadd(base, vmul(up, 300.0f));
            mario->grounded = 0; mario->hip = HIP_FALL; mario->hip_t = 0;
        }
        mario->up = up;
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) /* not bound yet: Mario at its row, its zone streams in */
        if (strcmp(sc->types + 32 * sc->objs[i].type, "PunchingKinoko") == 0) {
            const Row *r = &sc->objs[i];
            mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

/* the first PunchingKinoko's state (row_turn) and its head's distance from its place */
int game_punch_state(int *dist)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->punch) continue;
        V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), rest = vadd(base, vmul(up_at(base, v3(in->m0[4], in->m0[5], in->m0[6])), 130.0f));
        *dist = (int)vlen(vsub(in->head, rest));
        return row_turn[in->row];
    }
    return -1;
}

int game_test_biri(void)
{
    for (int i = 0; i < ninsts; i++) { /* Mario held on the first one's path: its place (circle centre / row) */
        const Inst *in = &insts[i];
        if (!in->biri) continue;
        const Row *r = &sc->objs[in->row];
        V3 p = in->biri_rail ? rail_at((const float *)(in->biri_rail + 24), *(const u32 *)(in->biri_rail + 8), tsuki_len(in->biri_rail) * 0.5f)
                             : v3(in->m[12], in->m[13], in->m[14]);
        (void)r;
        mario->pos = vsub(p, vmul(mario->up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    for (u32 i = 0; i < sc->nobjs; i++) { /* not bound yet: Mario at its row, its zone streams in */
        const char *n = sc->types + 32 * sc->objs[i].type;
        if (strcmp(n, "BigBirikyu") && strcmp(n, "Birikyu")) continue;
        const Row *r = &sc->objs[i];
        mario->pos = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(mario->up, 300.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return -2;
    }
    return -1;
}

int game_test_geyser(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].geyser) continue;
        V3 p = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
        mario->pos = vadd(p, vmul(vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6])), 300.0f - HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    return -1;
}

int game_test_cannon(void)
{
    if (!nshells) return -1;
    const Row *r = &sc->objs[shells[0].row];
    V3 x, z;
    row_axes(r, &x, &z);
    mario->pos = vsub(vadd(row_pos(r), vmul(z, 600.0f)), vmul(up_at(row_pos(r), mario->up), HIT_BODY_H));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return shells[0].row;
}

int game_test_restart(int k)
{
    if (k < 0) { miss(); return 0; }
    for (int i = 0; i < nlogic; i++) {
        if (logic[i].kind != LOGIC_RESTART || k--) continue;
        const Row *r = &sc->objs[logic[i].row];
        V3 x, z;
        row_axes(r, &x, &z);
        mario->pos = vadd(row_pos(r), vmul(vcross(z, x), 100.0f)); /* inside, above its base */
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return r->arg[0];
    }
    return -1;
}

int game_test_collapse(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->collapse) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = up_at(p, v3(0, 1, 0));
        mario->pos = vadd(p, vmul(up, 40.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->row;
    }
    return -1;
}

int game_test_collapse_step(int row) { return row < 0 ? -2 : row_dead[row] ? -1 : row_step[row]; }

int game_test_hip(void)
{
    int best = -1, rank = 0; /* a hip drop switch, else a HipDropRock, else any breakable */
    for (int i = 0; i < ninsts; i++) {
        int k = insts[i].hipsw ? 3 : insts[i].breaks == 2 ? 2 : insts[i].breaks == 1 ? 1 : 0;
        if (k > rank) { rank = k; best = i; }
    }
    if (best < 0) return -1;
    const Inst *in = &insts[best];
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    mario->up = up_at(p, mario->up);
    mario->pos = vadd(p, vmul(mario->up, in->reach + 500.0f));
    mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
    mario->hip = HIP_START; mario->hip_t = 0; /* as if R1 was pressed up there */
    if (mario_anim) mario_play("hipdropstart", 1.0f, 0);
    return in->row;
}

int game_hip_state(void) { return mario->hip; }

int game_test_pipe(void)
{
    for (int i = 0; i < npipes; i++)
        if (pipes[i].partner >= 0) {
            mario->pos = vadd(pipes[i].top, vmul(pipes[i].up, 30.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
            return i;
        }
    return -1;
}

int game_test_flip(int k)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].flip || k--) continue;
        V3 p = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
        V3 y = vnorm(v3(insts[i].m[4], insts[i].m[5], insts[i].m[6])); /* the panel's own up: side panels' origins
                                                                           * lie outside their face's gravity box */
        mario->pos = vadd(p, vmul(y, 200.0f)); /* the panel's top is about 100 up its axis */
        mario->up = up_at(mario->pos, y);
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return insts[i].row;
    }
    return -1;
}

int game_flips_back(void)
{
    int n = 0;
    for (int i = 0; i < ninsts; i++) n += insts[i].flip && (row_step[insts[i].row] & FLIP_BACK);
    return n;
}

int game_test_rockcreator(void)
{
    if (!nrollers) return -1;
    mario->pos = rail_at(rollers[0].pts, rollers[0].np, 0); /* where its rocks start: inside the area that wakes it */
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return rollers[0].row;
}

int game_test_rock(void)
{
    for (int i = 0; i < nrollers; i++) {
        Roller *o = &rollers[i];
        if (!o->alive) continue;
        V3 p = rail_at(o->pts, o->np, o->d + 300.0f); /* on its way, 300 ahead */
        mario->pos = p;
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return o->row;
    }
    return -1;
}

int game_rocks_alive(void)
{
    int n = 0;
    for (int i = 0; i < nrollers; i++) n += rollers[i].alive;
    return n;
}

int game_test_rotor(int rail)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (rail ? !in->railer : !in->rotor) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        mario->up = up_at(p, mario->up);
        mario->pos = vadd(p, vmul(mario->up, in->reach * 0.5f + 100.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        return in->row;
    }
    return -1;
}

float game_rotor_angle(int row) { return row >= 0 ? row_angle[row] : 0; }
float game_rail_coord(int row) { return row >= 0 ? row_coord[row] : 0; }

int game_test_firebar(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].firebar) {
            V3 p = firebar_ball(&insts[i], 0, 2);
            mario->pos = vadd(p, vmul(up_at(p, mario->up), -HIT_BODY_H));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return insts[i].row;
        }
    return -1;
}

int game_test_spark(void)
{
    if (!nerails) return -1;
    V3 p = vmul(vadd(erails[0].a, erails[0].b), 0.5f);
    mario->pos = vadd(p, vmul(up_at(p, mario->up), -HIT_BODY_H));
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return 0;
}

int game_test_hole(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].hazard >= 3) {
            mario->pos = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]);
            return insts[i].row;
        }
    return -1;
}

static V3 item_sensor(const Inst *in, float *r);

int game_test_item(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->item) continue;
        float r;
        V3 p = item_sensor(in, &r);
        mario->pos = vadd(p, vmul(up_at(p, mario->up), -HIT_BODY_H)); /* his touch sphere on the item's */
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return in->item;
    }
    return 0;
}

int game_test_hazard(int kind)
{
    /* by placement row, so a hazard in a zone loaded low counts too (the zone swap brings its model) */
    const char *want = kind == 1 ? "SpaceMine" : "NeedlePlant";
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (strcmp(sc->types + 32 * r->type, want) != 0 || row_dead[i]) continue;
        V3 p = row_pos(r), up = up_at(p, v3(0, 1, 0));
        V3 side = vnorm(vtangent(v3(1, 0, 0), up));
        mario->pos = vadd(p, vmul(side, kind == 1 ? 150.0f : 60.0f)); /* within reach of its sensor */
        mario->hvel = v3(0, 0, 0);
        return i;
    }
    return -1;
}

int game_hurt_timer(void) { return mario->hurt_timer; }

int game_test_launch(int i)
{
    if (i >= nlaunches) return -1;
    mario->pos = launches[i].pos;
    mario->launch = i; mario->launch_t = 0; mario->launch_from = mario->pos;
    if (mario_anim) mario_play(launches[i].start < 45 ? "spin" : "superspindriverstart", 1.0f, 0);
    return i;
}

int game_test_spindriver(int small)
{
    for (int i = 0; i < nlaunches; i++) { /* a shown one landing by something placed (OceanPhantomCave 1's first one
                                            * flies to a planet of other scenarios: nothing there, a miss loop; Phantom 6's
                                            * lands by areas only) */
        if ((launches[i].start < 45) != small || !row_shown(launches[i].row)) continue;
        V3 e = launch_pos(&launches[i], 1.0f);
        for (u32 k = 0; k < sc->nobjs; k++)
            if ((int)k != launches[i].row && vlen(vsub(row_pos(&sc->objs[k]), e)) < 3000.0f &&
                area_shape(sc->types + 32 * sc->objs[k].type) < 0 && !strstr(sc->types + 32 * sc->objs[k].type, "Gravity"))
                return game_test_launch(i); /* not an area (a RestartCube, DeathCube...) or a gravity: something solid */
    }
    return -1;
}

int game_launch_state(void) { return mario->launch; }

/* Test builds, a mission played through: Mario from where he is to the first row of class `cls` by launch stars, as a
 * player would. Each hop: the shown launch star within 6000 of him (on his planet) whose landing comes nearest the
 * goal, at least 500 nearer than he is (he is set on it and flies its real path). None: a hidden one that would come
 * nearer, and the row its appear switch waits for (a chip group, a cage's dead switch...) becomes the goal first
 * (a stack: that row may itself be far, behind other hidden stars); by it, it is done (route_solve), then that star
 * is taken. 1 on the way, 0 there (within 2500), -1 no way found */
#define ROUTE_DEPTH 6
static struct { int writer, launch; } route_stack[ROUTE_DEPTH];
static int route_n, route_last = -1;

/* the stack's top done as a player would: a chip group's chips taken one by one (Mario set on each), else a spin by
 * the row every 40 frames (a cage, a box: its dead switch) */
/* the zone of the planet under Mario (the nearest collision instance), -1 none: a goal in it (not the galaxy's
 * own zone) is walked to, planet to planet */
static int route_zone(void)
{
    int z = -1;
    float bd = 1e9f;
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].kcl) continue;
        float d = vlen(vsub(mario->pos, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]))) - insts[i].reach;
        if (d < bd) { bd = d; z = insts[i].zone; }
    }
    return z;
}

static int route_solve(void)
{
    static int t;
    int w = route_stack[route_n - 1].writer, l = route_stack[route_n - 1].launch;
    if (row_shown(launches[l].row)) { /* out: walked to it and taken */
        printf("route: launch %d out" "\n", l);
        route_n--; route_last = l; test_nolost = 0;
        game_test_launch(l);
        return 1;
    }
    if (++t % 40) return 1;
    const Row *wr = &sc->objs[w];
    const char *wn = sc->types + 32 * wr->type;
    test_nolost = 1;
    if (!strcmp(wn, "YellowChipGroup") || !strcmp(wn, "BlueChipGroup")) {
        for (int i = 0; i < ninsts; i++) {
            const Inst *in = &insts[i];
            if (in->item != ITEM_CHIP || sc->objs[in->row].zone != wr->zone || sc->types[32 * sc->objs[in->row].type] != wn[0]) continue;
            V3 cp = v3(in->m[12], in->m[13], in->m[14]);
            mario->pos = vadd(cp, vmul(up_at(cp, mario->up), -HIT_BODY_H)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return 1;
        }
        return 1;
    }
    if (!strcmp(wn, "CocoSambo")) { /* a coconut kicked into it (Mario set behind the nearest one), then a stomp */
        V3 sp = row_pos(wr);
        int sk = sambo_of(w);
        if (sk >= 0 && sambo_swoon(sk)) { /* its head down: a stomp on it */
            mario->pos = vadd(sambo_hpos(sk), vmul(up_at(sambo_hpos(sk), mario->up), 260.0f * wr->scale[0])); mario->hvel = v3(0, 0, 0); mario->vup = -5.0f;
            mario->grounded = 0;
            return 1;
        }
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < ninsts; i++)
            if (insts[i].coco && row_turn[insts[i].row] == 0 && vlen(vsub(insts[i].head, sp)) < bd) { bd = vlen(vsub(insts[i].head, sp)); best = i; }
        if (best >= 0) {
            V3 su = up_at(sp, mario->up), dir = vnorm(vtangent(vsub(insts[best].head, sp), su));
            if (bd > 500.0f) { /* rolled over by him first: 450 off its base */
                insts[best].head = vadd(vadd(sp, vmul(dir, 450.0f)), vmul(su, 60.0f));
                coco_place(&insts[best]);
                return 1;
            }
            V3 cp = insts[best].head, up = up_at(cp, mario->up), back = vnorm(vtangent(vsub(cp, sp), up));
            mario->pos = vadd(vsub(cp, vmul(up, HIT_BODY_H)), vmul(back, 120.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME;
        }
        return 1;
    }
    for (int i = 0; i < ninsts; i++)
        if (insts[i].row == w) {
            V3 cp = v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), up = up_at(cp, mario->up);
            mario->pos = vadd(cp, vmul(up, insts[i].reach + 20.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME;
            return 1;
        }
    return 1;
}

int game_test_route(const char *cls)
{
    static int wait;
    int t = scene_type(sc, cls);
    if (t < 0) return -1;
    V3 goal = v3(0, 0, 0);
    u32 k = 0;
    for (; k < sc->nobjs; k++) if (sc->objs[k].type == t && !row_dead[k]) { goal = row_pos(&sc->objs[k]); break; }
    if (k == sc->nobjs) return -1;
    if (mario->launch >= 0) { wait = 0; return 1; }
    for (int j = 0; j < route_n - 1; j++) /* a deeper goal at hand: the ones above it are not needed any more */
        if (vlen(vsub(mario->pos, row_pos(&sc->objs[route_stack[j].writer]))) < 6000.0f || row_shown(launches[route_stack[j].launch].row) ||
            (sc->objs[route_stack[j].writer].zone > 0 && sc->objs[route_stack[j].writer].zone == route_zone())) {
            printf("route: back to goal %d" "\n", j); route_n = j + 1; break;
        }
    if (route_n && row_shown(launches[route_stack[route_n - 1].launch].row) && vlen(vsub(mario->pos, launches[route_stack[route_n - 1].launch].pos)) > 6000.0f)
        route_n--; /* out, but far from him: route to the goal again */
    if (route_n) goal = row_pos(&sc->objs[route_stack[route_n - 1].writer]);
    float d = vlen(vsub(mario->pos, goal));
    int wz = route_n ? sc->objs[route_stack[route_n - 1].writer].zone : -1;
    if (route_n && (d < 6000.0f || (wz > 0 && wz == route_zone()))) return route_solve();
    if (!route_n && d < 2500.0f) return 0;
    if (!mario->grounded && wait++ < 240) return 1; /* landing first */
    wait = 0;
    int best = -1;
    float bd = d - 500.0f;
    for (int i = 0; i < nlaunches; i++) {
        if (!row_shown(launches[i].row) || vlen(vsub(launches[i].pos, mario->pos)) > 6000.0f || i == route_last) continue;
        float e = vlen(vsub(launch_pos(&launches[i], 1.0f), goal));
        if (e < bd) { bd = e; best = i; }
    }
    if (best >= 0) {
        printf("route: launch %d (row %d), %d to go, lands %d off" "\n", best, launches[best].row, (int)d, (int)bd);
        game_test_launch(best);
        route_last = best; test_nolost = 0;
        return 1;
    }
    if (route_n == ROUTE_DEPTH) return -1;
    int hw = -1, hl = -1; /* a hidden one coming nearer: what its appear switch waits for */
    float hd = d - 500.0f;
    for (int i = 0; i < nlaunches; i++) {
        const Row *lr = &sc->objs[launches[i].row];
        if (row_shown(launches[i].row) || lr->sw[SW_APPEAR] < 0) continue;
        float e = vlen(vsub(launch_pos(&launches[i], 1.0f), goal));
        if (e >= hd) continue;
        for (u32 w = 0; w < sc->nobjs; w++) {
            const Row *wr = &sc->objs[w];
            if ((int)w == launches[i].row || row_dead[w] || (lr->sw[SW_APPEAR] < 1000 && wr->zone != lr->zone)) continue;
            if (wr->sw[SW_A] == lr->sw[SW_APPEAR] || wr->sw[SW_DEAD] == lr->sw[SW_APPEAR]) { hd = e; hw = w; hl = i; }
        }
    }
    if (hw < 0) return -1;
    printf("route: launch %d waits for row %d (%s)" "\n", hl, hw, sc->types + 32 * sc->objs[hw].type);
    route_stack[route_n].writer = hw; route_stack[route_n++].launch = hl;
    return 1;
}


/* Test builds: Mario onto the nearest shown power star (its distance, -1 none) */
int game_test_star_near(void)
{
    int best = -1;
    float bd = 1e9f;
    V3 bp = v3(0, 0, 0);
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->item != ITEM_STAR || !row_shown(in->row)) continue;
        int state;
        V3 p = star_pos(in, &state);
        if (star_wait[in->row] && !state) continue;
        float d = vlen(vsub(p, mario->pos));
        if (d < bd) { bd = d; best = i; bp = p; }
    }
    if (best < 0) return -1;
    mario->pos = vadd(bp, vmul(up_at(bp, mario->up), -HIT_BODY_H)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return (int)bd;
}

int game_teleport_low(const unsigned char *full)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (full[in->zone]) continue;
        V3 c = v3(in->m[12], in->m[13], in->m[14]), u = up_at(c, v3(0, 1, 0));
        mario->pos = vadd(c, vmul(u, -(in->reach + 300.0f))); /* gravity points down: up = -g, go against it */
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
        return in->zone;
    }
    return -1;
}

/* WaterPlant.cpp: arg0 (16) water plants scattered within arg1 (500) of the row, each dropped onto the ground under
 * it (calcMapGround from 500 up, 1000 down; ten tries), a strip 40 wide of three segments at 0.5, 0.8 and 1 of a
 * height between h and 2h (h by type arg3: 150, 200, 300, 250), texture WaterPlantA-D by type, swaying 20 along a
 * direction turned 15 degrees per plant (sSwingPos table: one sine, the segments 3 and 6 entries apart). Facing the
 * camera about the up axis. Placed when the row's zone has its collision (game_rebind), drawn within 5000 of the
 * camera. ponytail: no spin wave (Mario's spin bending them), no fog, the plants' random spots not Petari's */
static struct { int row, k, n; unsigned seed; V3 dir; } wpc = {.row = -1}; /* the row being planted, its next plant */
static unsigned char wp_done[8192];                                     /* rows planted (kept across rebinds) */

/* a few plants a frame (each one's ground search is a dozen KCL queries: all at once took 600 ms in HeavenlyBeach),
 * rows whose zone has its collision bound */
static void place_waterplants(void)
{
    int t = scene_type(sc, "WaterPlant");
    if (t < 0 || !row_step) return;
    for (int budget = 2; budget > 0 && nwplants < MAX_WPLANTS;) {
        if (wpc.row < 0) { /* the next row to plant */
            for (u32 i = 0; i < sc->nobjs && i < sizeof wp_done && wpc.row < 0; i++) {
                const Row *r = &sc->objs[i];
                if (r->type != t || wp_done[i] || !row_shown(i)) continue;
                int bound = 0;
                for (int k = 0; k < ninsts && !bound; k++) bound = insts[k].kcl && insts[k].zone == r->zone;
                if (!bound) continue;
                V3 c = row_pos(r), up = up_at(c, v3(0, 1, 0));
                wpc.row = i; wpc.k = 0; wpc.n = r->arg[0] > 0 ? r->arg[0] : 16; wpc.seed = 2654435761u * (i + 1);
                wpc.dir = vnorm(vcross(up, fabsf(up.x) < 0.9f ? v3(1, 0, 0) : v3(0, 0, 1)));
            }
            if (wpc.row < 0) return;
        }
        const Row *r = &sc->objs[wpc.row];
        int type = r->arg[3] >= 0 && r->arg[3] <= 3 ? r->arg[3] : 0;
        float rad = r->arg[1] > 0 ? (float)r->arg[1] : 500.0f, h0 = (const float[]){150, 200, 300, 250}[type];
        V3 c = row_pos(r), up = up_at(c, v3(0, 1, 0)), a = vnorm(vcross(up, fabsf(up.x) < 0.9f ? v3(1, 0, 0) : v3(0, 0, 1)));
        V3 b = vcross(up, a);
        budget--;
        for (int tries = 0; tries < 10; tries++) {
            wpc.seed = wpc.seed * 1103515245u + 12345u; float u = (wpc.seed >> 8) / 16777216.0f * 2.0f - 1.0f;
            wpc.seed = wpc.seed * 1103515245u + 12345u; float w = (wpc.seed >> 8) / 16777216.0f * 2.0f - 1.0f;
            V3 top = vadd(vadd(c, vmul(up, 500.0f)), vadd(vmul(a, u * rad), vmul(b, w * rad)));
            Hit hits[4];
            int found = 0;
            for (float d = 0; d <= 1500.0f && !found; d += 25.0f) /* calcMapGround: down to 1000 under the row */
                if (kcl_query(vsub(top, vmul(up, d)), 20.0f, hits, 4)) { top = vsub(top, vmul(up, d + 20.0f)); found = 1; }
            if (!found) continue;
            wpc.seed = wpc.seed * 1103515245u + 12345u;
            wplants[nwplants].pos = top; wplants[nwplants].up = up; wplants[nwplants].dir = wpc.dir;
            wplants[nwplants].h = h0 * (1.0f + (wpc.seed >> 8) / 16777216.0f);
            wplants[nwplants].row = (short)wpc.row; wplants[nwplants].type = (short)type;
            nwplants++;
            break;
        }
        float cs = cosf(0.2617994f), sn = sinf(0.2617994f); /* makeMtxRotateY(15) for the next one */
        wpc.dir = vadd(vmul(wpc.dir, cs), vmul(vcross(up, wpc.dir), sn));
        if (++wpc.k >= wpc.n) { wp_done[wpc.row] = 1; wpc.row = -1; }
    }
}

static void draw_waterplants(void)
{
    static Strip st[64];
    static float angle;
    int ns = 0;
    angle += 0.03f; /* sSwingSpeed */
    for (int i = 0; i < nwplants; i++) {
        const typeof(wplants[0]) *w = &wplants[i];
        int tex = fx_waterplant_tex(w->type);
        if (tex < 0) return;
        V3 to = vsub(w->pos, cam_pos);
        if (vdot(to, to) > 5000.0f * 5000.0f) continue;
        V3 side = vtangent(to, w->up);
        side = vlen(side) > 1e-3f ? vmul(vnorm(vcross(w->up, side)), 20.0f) : vmul(w->dir, 20.0f);
        static const float lv[4] = {0, 0.5f, 0.8f, 1.0f};
        Strip *s = &st[ns++];
        for (int l = 0; l < 4; l++) {
            float sw = l ? 20.0f * sinf(angle + i * 0.7f - (3 - l) * 0.3f) * lv[l] : 0;
            V3 p = vadd(vadd(w->pos, vmul(w->up, w->h * lv[l])), vmul(w->dir, sw));
            V3 q0 = vsub(p, side), q1 = vadd(p, side);
            s->p[l][0][0] = q0.x; s->p[l][0][1] = q0.y; s->p[l][0][2] = q0.z;
            s->p[l][1][0] = q1.x; s->p[l][1][1] = q1.y; s->p[l][1][2] = q1.z;
        }
        s->rgba = 0x80808080u;
        s->tex = (u16)tex;
        if (ns == 64) { render_strips(st, ns); ns = 0; }
    }
    if (ns) render_strips(st, ns);
}

/* --- update ---------------------------------------------------------------------------------------------- */

static void start_jump(Player *m, int kind, V3 hvel, float vup, const char *clip)
{
    m->jump_kind = m->last_jump = kind;
    m->hvel = hvel;
    m->vup = vup;
    m->grounded = 0;
    m->air_timer = 0;
    m->squat = 0;
    if (mario_anim) mario_play(clip, 1.0f, 0);
#ifdef BENCH
    printf("jump %s\n", clip);
#endif
}

/* Petari MarioJump.cpp: tryJump (2nd / 3rd jump within mJumpConnectTime of landing, the 3rd only above
 * mJumpConnectSpeed), trySquatJump (long jump), tryBackJump (back flip), trySpinJump, tryWallJump. */
static void try_jump(Player *m, float ratio)
{
    if (m->squat) {
        if (ratio > LONG_JUMP_MIN)
            start_jump(m, 5, vmul(m->face, SQUAT_JUMP_FRONT), JUMP_HEIGHT[5], "jumpbroad");
        else
            start_jump(m, 6, vmul(m->face, -SQUAT_JUMP_BACK), JUMP_HEIGHT[6], "jumpback");
        return;
    }
    int k = 0;
    if (m->land_timer <= JUMP_CONNECT_TIME && m->last_jump >= 0 && m->last_jump < 2)
        k = m->last_jump + 1;
    if (k == 2 && ratio < JUMP_CONNECT_SPEED)
        k = 0;
    static const char *clips[3] = {"jump", "jump2", "jumproll"};
    start_jump(m, k, vmul(m->face, ratio * JUMP_FRONT), JUMP_HEIGHT[k], clips[k]);
}

/* point at length d along a polyline */
static V3 rail_at(const float *p, u32 n, float d)
{
    for (u32 i = 0; i + 1 < n; i++) {
        V3 a = v3(p[3 * i], p[3 * i + 1], p[3 * i + 2]), b = v3(p[3 * i + 3], p[3 * i + 4], p[3 * i + 5]);
        float l = vlen(vsub(b, a));
        if (d <= l || i + 2 == n)
            return vadd(a, vmul(vsub(b, a), l > 0 ? (d < l ? d : l) / l : 0));
        d -= l;
    }
    return v3(p[0], p[1], p[2]);
}

/* SpinDriverShootPath::calcPosition at t in 0..1 */
static V3 launch_pos(const Launch *L, float t)
{
    if (L->parab)
        return vadd(L->pos, vadd(vmul(L->py, t * (L->pv + L->pa * t)), vmul(L->pz, L->plen * t)));
    float h = t < 0.5f ? t / 0.5f : 1.0f;
    return vadd(rail_at(L->pts, L->npts, t * L->len), vmul(L->off, 1.0f - sinf(h * 1.5707963f)));
}

static void init_launch(Launch *L, const Row *r, const float *pts, u32 np, int parab)
{
    L->starter = 0; L->initrot = 0;
    L->pos = row_pos(r);
    L->flight = r->arg[0] > 0 ? r->arg[0] : 300;
    L->pts = pts;
    L->npts = np;
    V3 rs = v3(pts[0], pts[1], pts[2]), re = v3(pts[3 * np - 3], pts[3 * np - 2], pts[3 * np - 1]);
    L->parab = parab; /* SpinDriverShootPath: rails of at most 2 JMap points are a parabola */
    if (L->parab) { /* ParabolicPath::initFromMaxHeight(star, rail end, rail start) + calcParabolicFunctionParam */
        V3 dir = vnorm(vsub(rs, re));
        float top = vdot(dir, vsub(rs, L->pos)), end = vdot(dir, vsub(re, L->pos));
        V3 z = vsub(vsub(re, L->pos), vmul(dir, end));
        L->py = dir;
        L->plen = vlen(z);
        L->pz = L->plen > 1e-3f ? vmul(z, 1.0f / L->plen) : v3(0, 0, 0);
        float disc = top * (top - end);
        if (disc <= 0) { L->pa = -top; L->pv = end + top; }
        else if (fabsf(end) < 1e-4f) { L->pa = -4 * top; L->pv = 4 * top; }
        else {
            float rt = sqrtf(disc), t1 = (top + rt) / end, t2 = (top - rt) / end;
            float t = t1 >= 0 && t1 <= 1 ? t1 : t2 >= 0 && t2 <= 1 ? t2 : 1.0f;
            L->pa = -top / (t * t);
            L->pv = -2.0f * t * L->pa;
        }
    } else {
        L->len = 0;
        for (u32 i = 0; i + 1 < np; i++)
            L->len += vlen(vsub(v3(pts[3 * i + 3], pts[3 * i + 4], pts[3 * i + 5]), v3(pts[3 * i], pts[3 * i + 1], pts[3 * i + 2])));
        L->off = vsub(L->pos, rs);
    }
}

/* Mario while a launch star holds him: returns 0 once it lets go */
/* Mario's pose in a launch star's flight (SuperSpinDriver.cpp / SpinDriver.cpp updateBindActorMatrix): his head (Y)
 * turns towards the path's direction at 0.4 a frame (turnBindHead), he spins about it (_138: 0 to 2 pi x
 * floor((t1 - t0) x 0.05 / 2 pi), eased out over t0..t1) and flips over (_148: 0 to pi over t1..t2) before the end;
 * t0 = the loop clip (flight / 5, at most 90), t1 = flight - 70 (SpaceFlyEnd, 50 frames, + 20), t2 = flight - 20
 * (Fall) (calcShootMotionTime). fly_on: the draw takes this basis. ponytail: no -75 ring offset, no stick steering */
static V3 fly_x, fly_y, fly_z; /* the drawn basis */
static V3 fly_hy, fly_hz;      /* the head's basis before the spin and flip (turned towards the path) */
static int fly_on;

static void fly_pose(Player *m, V3 dir, int s, int flight, int t0_in, const Launch *L)
{
    if (s <= 1 && L && L->starter) { /* SpinDriverShootPath::calcInitPose: up the path's first direction, front the
                                    * start-to-end way across it, turned arg5 degrees about it */
        V3 d0 = vnorm(vsub(launch_pos(L, 0.01f), launch_pos(L, 0.0f))), f0 = vtangent(vsub(launch_pos(L, 1.0f), launch_pos(L, 0.0f)), d0);
        if (vlen(f0) < 1e-3f) f0 = vtangent(fabsf(d0.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0), d0);
        f0 = vnorm(f0);
        float a = L->initrot * 0.0174533f;
        fly_hy = d0; fly_hz = vnorm(vadd(vmul(f0, cosf(a)), vmul(vcross(d0, f0), sinf(a))));
    } else if (s <= 1) { fly_hy = m->up; fly_hz = m->face; }
    if (vlen(dir) > 1e-3f) fly_hy = vnorm(vadd(fly_hy, vmul(vsub(vnorm(dir), fly_hy), 0.4f)));
    fly_hz = vnorm(vtangent(fly_hz, fly_hy));
    if (vlen(fly_hz) < 0.5f) fly_hz = vnorm(vtangent(m->face, fly_hy));
    V3 hx = vcross(fly_hy, fly_hz);
    int t1 = flight - 70, t2 = flight - 20, t0 = t0_in;
    if (flight < 70) { t0 = -1; t1 = 0; }
    if (t1 <= t0) t0 = 0;
    float spin = 6.2831853f * floorf((t1 - t0) * 0.05f / 6.2831853f), b = 0, a = 0;
    if (s >= t0 && t1 > t0) { float v = s >= t1 ? 1.0f : (float)(s - t0) / (t1 - t0); b = spin * (1 - (1 - v) * (1 - v)); }
    if (s >= t1 && t2 > t1) a = 3.14159265f * (s >= t2 ? 1.0f : (float)(s - t1) / (t2 - t1));
    V3 y = vadd(vmul(fly_hy, cosf(a)), vmul(fly_hz, sinf(a))), z = vsub(vmul(fly_hz, cosf(a)), vmul(fly_hy, sinf(a)));
    V3 x = vsub(vmul(hx, cosf(b)), vmul(z, sinf(b)));
    z = vadd(vmul(hx, sinf(b)), vmul(z, cosf(b)));
    fly_x = x; fly_y = y; fly_z = z;
    fly_on = 1;
}

static int update_launch(Player *m)
{
    Launch *L = &launches[m->launch];
    int t = ++m->launch_t;
    if (t <= L->start) { /* exeShootStart: to the centre over 15 frames, then the wind-up */
        float k = t < 15 ? t / 15.0f : 1.0f;
        m->pos = vadd(m->launch_from, vmul(vsub(L->pos, m->launch_from), k));
        return 1;
    }
    int small = !L->starter && L->start < 45, s = t - L->start;
    if (!L->pts) { /* SpinDriver without a rail: endBindAndSpinDriverJump with its kick */
        L->cool = 60;
        m->launch = -1;
        m->vup = vdot(L->kick, m->up); m->hvel = vtangent(L->kick, m->up); m->grounded = 0;
        m->jump_kind = 0; m->air_timer = 0; m->spin_used = 0;
        if (mario_anim) mario_play("spaceflyshortstart", 1.0f, 0);
        return 0;
    }
    float u = (float)s / L->flight;
    int t0 = L->flight / 5 < 90 ? L->flight / 5 : 90;
    if (L->starter) { /* ScenarioStarter::calcShootMotionTime: a whole number of SpaceFlyStart (15) within min(0.2 x
                       * flight, 90); SpaceFlyEnd (50) + 20 before the end, Fall 20 before it */
        int fly = (int)(0.2f * L->flight) > 90 ? 90 : (int)(0.2f * L->flight);
        t0 = 15 * (fly / 15);
        if (L->flight - 70 <= t0) t0 = 0;
    }
    if (s == 1 && mario_anim) mario_play(small ? "spaceflyshortstart" : "spaceflystart", 1.0f, 0);
    if (s == t0 && mario_anim) mario_play(small ? "spaceflyshort" : "spaceflyloop", 1.0f, 1);
    if (s == L->flight - 70 && mario_anim) mario_play("spaceflyend", 1.0f, 0);
    if (s == L->flight - 20 && mario_anim) mario_play("fall", 1.0f, 1);
    V3 p = launch_pos(L, u > 1 ? 1 : u), q = launch_pos(L, u + 0.01f > 1 ? 1 : u + 0.01f);
    V3 dir = vsub(q, p), step = vsub(p, m->pos); /* _D0: this frame's move, kept when he is let go */
    m->pos = p;
    if (vlen(dir) > 1e-3f) m->face = vnorm(vtangent(dir, m->up));
    fly_pose(m, dir, s, L->flight, t0, L);
    if (s == 1 && row_camset[L->row] >= 0) /* SpinDriverCamera::start (its target: the flight's first pose) */
        camera_event_start(camera_class_hash(sc->types + 32 * sc->objs[L->row].type), row_camset[L->row], fly_hy, fly_hz, m->pos);
    camera_event_target(dir, m->pos); /* SpinDriverCamera::update */
    if (s < L->flight)
        return 1;
    /* endBindAndSpinDriverJump(_D0): he goes on with the path's last velocity; the star rests 60 frames */
    camera_event_end(); /* SpinDriverCamera::end: endForceAtLanding */
    L->cool = 60;
    m->launch = -1;
    fly_on = 0;
    m->hvel = vtangent(step, m->up); m->vup = vdot(step, m->up); m->grounded = 0;
    m->jump_kind = 0; m->air_timer = DROP_WAIT + 1; m->spin_used = 0;
    return 0;
}

/* WarpPod.cpp / MarioWarp.cpp: a WarpPod pairs with the other one of its GroupId in its zone. Mario touching it
 * (its eye sensor: 15 x scale, 120 x scale for an invisible one, arg1 0) goes in 60 frames to 160 up its y, then
 * flies an arc (an angle of pi / 4 each side, max(radius / 25, 120) frames, sine-eased, following at half the gap)
 * to 160 up the pair, which waits 60 frames (row_step) before it takes him again, longer while he stays within 200.
 * ponytail: every pod is active (no grand star count, no path flags), no event camera, no pod glow / path drawing */
static struct { int from, to, t, n; V3 dest, axis, centre, start; float angle; } warp = {.from = -1};

static int warp_pair(int row)
{
    const Row *r = &sc->objs[row];
    if (r->group < 0) return -1;
    for (u32 i = 0; i < sc->nobjs; i++)
        if ((int)i != row && sc->objs[i].type == r->type && sc->objs[i].group == r->group && sc->objs[i].zone == r->zone)
            return (int)i;
    return -1;
}

static V3 row_axis_y(const Row *r)
{
    float m[16], one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, r->pos, r->rot, one);
    return v3(m[4], m[5], m[6]);
}

static void warp_try(Player *m)
{
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->warppod) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *d = &row_step[in->row];
        float dist = vlen(vsub(m->pos, v3(r->pos[0], r->pos[1], r->pos[2])));
        if (*d) { /* mDelay: held at 1 while Mario stays within 200 */
            if (*d > 1 || dist >= 200.0f) --*d;
            continue;
        }
        float rad = (r->arg[1] == 0 ? 120.0f : 15.0f) * r->scale[0] + HIT_BODY_R;
        if (vlen(vsub(c, v3(r->pos[0], r->pos[1], r->pos[2]))) >= rad) continue;
        int to = warp_pair(in->row);
        if (to < 0) continue;
        const Row *p = &sc->objs[to];
        warp.from = in->row; warp.to = to; warp.t = 0;
        warp.dest = vadd(v3(p->pos[0], p->pos[1], p->pos[2]), vmul(row_axis_y(p), 160.0f));
        m->hvel = v3(0, 0, 0); m->vup = 0; m->hip = 0; m->grounded = 0;
        printf("warp pod %d -> %d\n", warp.from, warp.to);
        return;
    }
}

/* returns 1 while the warp holds Mario */
static int warp_update(Player *m)
{
    const Row *r = &sc->objs[warp.from];
    m->hvel = v3(0, 0, 0); m->vup = 0;
    if (warp.t < 60) { /* to 160 up the pod (displacement x (1 - left / 60)) */
        V3 to = vadd(v3(r->pos[0], r->pos[1], r->pos[2]), vmul(row_axis_y(r), 160.0f));
        m->pos = vadd(m->pos, vmul(vsub(to, m->pos), (warp.t + 1) / 60.0f));
        if (++warp.t == 60) { /* calcAxis */
            V3 disp = vsub(warp.dest, m->pos);
            float dist = vlen(disp), a = 0.7853982f;
            warp.axis = vnorm(vcross(disp, vmul(m->up, -1.0f)));
            V3 normal = vnorm(vcross(warp.axis, disp));
            float radius = 0.5f * dist / sinf(a), h = sqrtf(radius * radius - 0.25f * dist * dist);
            warp.centre = vadd(vadd(m->pos, vmul(disp, 0.5f)), vmul(normal, h));
            warp.start = vmul(normal, -radius);
            warp.angle = a;
            warp.n = (int)(radius / 25.0f) < 120 ? 120 : (int)(radius / 25.0f);
        }
        return 1;
    }
    int k = warp.t - 60; /* frames flown */
    if (k >= warp.n) {
        m->pos = warp.dest;
        row_step[warp.to] = 60;
        printf("warp pod %d: arrived at %d\n", warp.from, warp.to);
        warp.from = -1;
        return 0;
    }
    float p = (1.0f + sinf(((float)k - 0.5f * warp.n) / warp.n * 3.14159265f)) * 0.5f;
    float ang = -warp.angle * (1.0f - p) + warp.angle * p, c = cosf(ang), s = sinf(ang);
    V3 a = warp.axis, v = warp.start; /* Rodrigues */
    V3 rot = vadd(vadd(vmul(v, c), vmul(vcross(a, v), s)), vmul(a, vdot(a, v) * (1.0f - c)));
    m->pos = vadd(m->pos, vmul(vsub(vadd(warp.centre, rot), m->pos), 0.5f));
    warp.t++;
    return 1;
}

/* GCapture.cpp / GCaptureTarget.cpp with the pointer on the right stick (cursor_ray) and A on R2: R2 with the cursor
 * on a target (within 200 of its ray, StarPointerTarget radius) and within arg1 (default 2500) of Mario captures it;
 * 30 frames later (the ribbon reaching him; R2 let go before: cancelled) Traction pulls him: velocity += 0.7 x power
 * towards it (power up to 1 in 30 frames), damped 0.99 (down to 0.75 inside 500 of it), rebounding off the map at
 * half speed; R2 let go (after 30 frames) leaves him in Hold: damped 0.99 until 180 frames or the map touches him,
 * then he falls free with that velocity (endBindAndPlayerWeakGravityLimitJump). R2 on a target again recaptures;
 * R1 (Wii Z) or coming within arg0 of the target breaks it. phase 0 none, 1 capture, 2 traction, 3 hold. ponytail:
 * no ribbon, no spin, power ramp guessed (sTractionPowerUpSpeed is not in the decompilation), railed targets stay */
static void cursor_ray(V3 *o, V3 *d);
static int pointer_a(void) { return pad_held(PAD_R2) || test_r2; } /* the pointer's A */

static int gcap_pick(void) /* the target under the cursor, -1 none */
{
    V3 o, d;
    cursor_ray(&o, &d);
    int best = -1;
    float bz = 1e30f;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->gcap || !row_shown(in->row)) continue;
        const Row *r = &sc->objs[in->row];
        V3 p = v3(in->m[12], in->m[13], in->m[14]), to = vsub(p, o);
        float z = vdot(to, d), range = r->arg[1] > 0 ? (float)r->arg[1] : 2500.0f;
        if (z <= 0 || vlen(vsub(to, vmul(d, z))) > 200.0f || vlen(vsub(p, mario->pos)) > range || z >= bz) continue;
        bz = z; best = i;
    }
    return best;
}

static void gcap_release(Player *m)
{
    m->hvel = vtangent(gcap.vel, m->up);
    m->vup = vdot(gcap.vel, m->up);
    m->grounded = 0; m->jump_kind = 0; m->air_timer = 0;
    printf("gcapture: released" "\n");
    gcap.phase = 0;
}

/* returns 1 while the capture holds Mario */
static int gcap_update(Player *m)
{
    static int prev_a;
    int a = pointer_a(), trig = a && !prev_a;
    prev_a = a;
    if (!gcap.phase) {
        if (trig) {
            int t = gcap_pick();
            if (t >= 0) { gcap.phase = 1; gcap.t = 0; gcap.target = t; gcap.power = 0; printf("gcapture: target row %d" "\n", insts[t].row); }
        }
        return 0;
    }
    if (gcap.target >= ninsts || !insts[gcap.target].gcap) { gcap.phase = 0; return 0; } /* rebound away */
    const Inst *in = &insts[gcap.target];
    const Row *r = &sc->objs[in->row];
    V3 tp = v3(in->m[12], in->m[13], in->m[14]);
    gcap.t++;
    if (gcap.phase == 1) { /* the ribbon on its way: Mario still free */
        if (!a && gcap.t > 30) { gcap.phase = 0; return 0; }
        if (gcap.t >= 30) { gcap.phase = 2; gcap.t = 0; gcap.vel = v3(0, 0, 0); m->hip = 0; } /* requestBind: zeroVelocity */
        return 0;
    }
    if (trig && gcap.phase == 3) { /* tryRecapture */
        int t = gcap_pick();
        if (t >= 0) { gcap.target = t; gcap.phase = 2; gcap.t = 0; return 1; }
    }
    if (pad_pressed(PAD_R1) || (r->arg[0] >= 0 && vlen(vsub(tp, m->pos)) <= r->arg[0])) { gcap_release(m); return 0; }
    V3 c = vadd(m->pos, vmul(m->up, BODY_RADIUS));
    if (gcap.phase == 2) {
        V3 to = vsub(tp, c);
        float dist = vlen(to), damp = dist < 500.0f ? 0.99f * dist / 500.0f + 0.75f * (1.0f - dist / 500.0f) : 0.99f;
        gcap.power = gcap.power + 1.0f / 30.0f > 1.0f ? 1.0f : gcap.power + 1.0f / 30.0f;
        gcap.vel = vmul(vadd(gcap.vel, vmul(vnorm(to), 0.7f * gcap.power)), damp);
        if (!a && gcap.t > 30) { gcap.phase = 3; gcap.t = 0; }
    } else
        gcap.vel = vmul(gcap.vel, 0.99f);
    V3 moved = vadd(c, gcap.vel), pushed = moved;
    int wall = 0, ground = collide_n(&pushed, m->up, BODY_RADIUS, NULL, NULL, &wall);
    if (ground || wall) { /* reboundVelocityFromCollision 0.5: the pushed-out part reflected */
        V3 n = vnorm(vsub(pushed, moved));
        if (vlen(n) > 0.5f && vdot(gcap.vel, n) < 0) gcap.vel = vsub(gcap.vel, vmul(n, 1.5f * vdot(gcap.vel, n)));
        if (gcap.phase == 3) { m->pos = vsub(pushed, vmul(m->up, BODY_RADIUS)); gcap_release(m); return 0; }
    }
    m->pos = vsub(pushed, vmul(m->up, BODY_RADIUS));
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    if (gcap.phase == 3 && gcap.t > 180) { gcap_release(m); return 0; }
    return 1;
}

/* JumpBranch.cpp / SwingRopePoint.cpp: Mario in the air whose body is within 80 of the branch's axis (its X, the
 * grab point clamped 50 along it) and 70 of its plane grabs it: a point 30 from the axis, thrown on at his speed,
 * swings round it (gravity 0.2 a frame, strained to 30, friction 0.995) and he hangs under it, the branch at his
 * hands. X jumps off (16 up, plus 10 along the branch's front by the stick), a spin lets go; then it ignores him 30
 * frames (WaitInvalid). jb.inst -1 none. ponytail: hangwait / jumpspin stand in for JumpBranchWait and its spins,
 * gravity's up for the jump's world Y */
static int jbranch_try(Player *m)
{
    if (jb.cool) { jb.cool--; return 0; }
    if (m->grounded || m->launch >= 0) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->jbranch) continue;
        V3 bar = v3(in->m[12], in->m[13], in->m[14]), side = vnorm(v3(in->m[0], in->m[1], in->m[2]));
        V3 front = vnorm(v3(in->m[8], in->m[9], in->m[10])), d = vsub(c, bar);
        float along = vdot(d, side);
        along = along < -50 ? -50 : along > 50 ? 50 : along;
        V3 grab = vadd(bar, vmul(side, along)), off = vsub(c, grab);
        if (vlen(off) > 80.0f + HIT_BODY_R * 0.5f || fabsf(vdot(off, front)) > 70.0f) continue;
        V3 radial = vtangent(off, side);
        radial = vlen(radial) > 1e-3f ? vnorm(radial) : vmul(m->up, -1.0f);
        V3 v = vadd(m->hvel, vmul(m->up, m->vup));
        jb.inst = i; jb.side = side; jb.front = front;
        jb.pt = vadd(bar, vmul(radial, 30.0f));
        jb.vel = vtangent(vtangent(v, side), radial); /* his speed along the swing */
        m->hip = 0; m->spin_timer = 0;
        if (mario_anim) mario_play("hangwait", 1.0f, 1);
        printf("jump branch row %d: grabbed" "\n", in->row);
        return 1;
    }
    return 0;
}

/* returns 1 while Mario swings */
static int jbranch_update(Player *m)
{
    const Inst *in = &insts[jb.inst];
    V3 bar = v3(in->m[12], in->m[13], in->m[14]), g = vmul(up_at(bar, m->up), -1.0f);
    if (pad_pressed(PAD_CROSS) || pad_pressed(PAD_SQUARE)) { /* updateBind: A jumps, a swing (spin) falls */
        int jump = pad_pressed(PAD_CROSS);
        V3 cam_fwd = vnorm(vtangent(vsub(m->pos, cam_pos), m->up)), cam_right = vnorm(vcross(cam_fwd, m->up));
        V3 stick = vadd(vmul(cam_right, pad_lx()), vmul(cam_fwd, pad_ly()));
        float proj = vlen(stick) > 0.1f ? vdot(vnorm(stick), jb.front) : 0.0f;
        V3 v = jump ? vadd(vmul(jb.front, 10.0f * proj), vmul(m->up, 16.0f)) : v3(0, 0, 0);
        m->hvel = vtangent(v, m->up); m->vup = vdot(v, m->up);
        m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_used = 0;
        if (mario_anim) mario_play(jump ? "jumpspin" : "fall", 1.0f, 0);
        printf("jump branch row %d: %s" "\n", in->row, jump ? "jump" : "let go");
        jb.inst = -1; jb.cool = 30;
        return 0;
    }
    jb.vel = vadd(jb.vel, vmul(g, 0.2f)); /* addAccel, strain, updatePos */
    V3 next = vsub(vadd(jb.pt, jb.vel), bar);
    next = vtangent(next, jb.side);
    V3 dir = vlen(next) > 1e-3f ? vnorm(next) : g;
    jb.vel = vsub(jb.vel, vsub(next, vmul(dir, 30.0f)));
    jb.pt = vadd(jb.pt, jb.vel);
    jb.vel = vmul(jb.vel, 0.995f);
    V3 up = vmul(dir, -1.0f); /* from the point to the branch */
    m->pos = vsub(bar, vmul(up, 150.0f)); /* he hangs under it, hands at the branch */
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    m->face = vnorm(vcross(jb.side, up));
    return 1;
}

/* SpaceCocoon.cpp: a cocoon on a springy stalk (neutral point arg1, default 100, up its axis; spring velocity x 0.95
 * + offset x 0.02). Mario in the air within 200 of its bind point (100 over it) hangs on (AutoRushBegin: the spring
 * takes his speed along the ground). R2 with the pointer on it starts the aim (BindAim): while R2 is held the cocoon
 * follows the pointer on the plane through its neutral point (x 0.03 a frame, at most 500 off); let go 100 or more
 * off and Mario flies off towards the neutral point at 60 (BindAttack) for arg0 (60) frames, x 0.98 a frame in the
 * last 90, until he hits the map after 20; closer it goes back to waiting. X lets go (RushCancel). Then it ignores
 * him until he is 300 off or on the ground (FreeInvalid). A pointer sweep over it (R2 off) and star pieces push the
 * cocoon. coc.phase 1 wait, 2 aim, 3 flight; coc.wait the FreeInvalid inst. ponytail: no stalk drawn (the cocoon
 * model only, tilted along it), no CocoonWait / Stretch / Fly animations, no breakables hit at the end */
static V3 cocoon_neutral(const Inst *in)
{
    const Row *r = &sc->objs[in->row];
    float h = r->arg[1] > 0 ? (float)r->arg[1] : 100.0f;
    return vadd(v3(in->m0[12], in->m0[13], in->m0[14]), vmul(vnorm(v3(in->m0[4], in->m0[5], in->m0[6])), h));
}

static void cocoon_place(Inst *in)
{
    V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), d = vsub(in->head, base);
    V3 up = vlen(d) > 1e-3f ? vnorm(d) : vnorm(v3(in->m0[4], in->m0[5], in->m0[6]));
    basis_matrix(in->m, in->head, up, v3(in->m0[8], in->m0[9], in->m0[10]), 0);
    for (int j = 0; j < 12; j++) if (j % 4 != 3) in->m[j] *= in->s;
}

/* updateSpringPoint: returns 1 at rest */
static int cocoon_spring(Inst *in)
{
    V3 n = cocoon_neutral(in);
    in->hvel = vadd(vmul(in->hvel, 0.95f), vmul(vsub(n, in->head), 0.02f));
    in->head = vadd(in->head, in->hvel);
    if (vlen(in->hvel) < 0.1f && vlen(vsub(in->head, n)) < 1.0f) { in->head = n; in->hvel = v3(0, 0, 0); return 1; }
    return 0;
}

static void cursor_ray(V3 *o, V3 *d);
static int cursor_sees(V3 p, float px);
static int pointer_a(void);
static void update_cocoons(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->cocoon) continue;
        if (coc.wait == i) { /* FreeInvalid */
            V3 b = vadd(in->head, vmul(vnorm(v3(in->m0[4], in->m0[5], in->m0[6])), 100.0f));
            if (vlen(vsub(mario->pos, b)) > 300.0f || mario->grounded) coc.wait = -1;
        }
        if (coc.inst == i && coc.phase == 2) continue; /* the aim moves it */
        if (coc.inst != i && !pointer_a() && cursor_sees(in->head, 30.0f) && !row_turn[in->row]) { /* tryTouch */
            V3 o, d, up = vnorm(v3(in->m0[4], in->m0[5], in->m0[6]));
            cursor_ray(&o, &d);
            in->hvel = vadd(in->hvel, vmul(vnorm(vtangent(vcross(d, up), up)), 5.0f));
            row_turn[in->row] = 20; /* sTouchTimerMax */
        }
        if (row_turn[in->row] > 0) row_turn[in->row]--;
        cocoon_spring(in);
        cocoon_place(in);
    }
}

static int cocoon_try(Player *m)
{
    if (coc.inst >= 0 || m->grounded || m->launch >= 0) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->cocoon || coc.wait == i) continue;
        V3 up = vnorm(v3(in->m0[4], in->m0[5], in->m0[6])), b = vadd(in->head, vmul(up, 100.0f));
        if (vlen(vsub(c, b)) > 200.0f + HIT_BODY_R * 0.5f) continue;
        coc.inst = i; coc.phase = 1; coc.t = 0;
        in->hvel = vtangent(vadd(m->hvel, vmul(m->up, m->vup)), up); /* mSpringVel: his speed along side and front */
        m->hip = 0; m->spin_timer = 0;
        if (mario_anim) mario_play("cocoonland", 1.0f, 0);
        printf("space cocoon row %d: Mario hangs on" "\n", in->row);
        return 1;
    }
    return 0;
}

static void cocoon_release(Player *m, V3 v)
{
    m->hvel = vtangent(v, m->up); m->vup = vdot(v, m->up);
    m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_used = 0;
    coc.wait = coc.inst; coc.inst = -1; coc.phase = 0;
}

/* returns 1 while Mario hangs on or flies */
static int cocoon_update(Player *m)
{
    Inst *in = &insts[coc.inst];
    const Row *r = &sc->objs[in->row];
    V3 up = vnorm(v3(in->m0[4], in->m0[5], in->m0[6])), n = cocoon_neutral(in);
    coc.t++;
    if (coc.phase == 3) { /* BindAttack: Mario flies, the cocoon springs back */
        int time = r->arg[0] > 0 ? r->arg[0] : 60;
        if (coc.t >= time - 90) coc.vel = vmul(coc.vel, 0.98f);
        V3 c = vadd(m->pos, coc.vel);
        int wall = 0;
        V3 gn, wn;
        int hit = collide_n(&c, m->up, 50.0f, &gn, &wn, &wall) || wall;
        m->pos = c;
        m->up = up_at(m->pos, m->up);
        if ((hit && coc.t > 20) || coc.t >= time) {
            printf("space cocoon row %d: flight ends (%s) at %d" "\n", in->row, hit ? "hit" : "time", coc.t);
            cocoon_release(m, hit ? v3(0, 0, 0) : coc.vel);
            if (mario_anim) mario_play("fall", 1.0f, 0);
            return 0;
        }
        m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
        m->face = vnorm(vtangent(coc.vel, m->up));
        return 1;
    }
    if (coc.phase == 1 && pad_pressed(PAD_CROSS)) { /* RushCancel */
        printf("space cocoon row %d: let go" "\n", in->row);
        cocoon_release(m, v3(0, 0, 0));
        return 0;
    }
    if (coc.phase == 1 && pointer_a() && cursor_sees(in->head, 40.0f)) { coc.phase = 2; coc.t = 0; } /* BindAim */
    if (coc.phase == 2) {
        if (!pointer_a()) { /* tryRelease */
            float off = vlen(vsub(in->head, n));
            if (off < 100.0f) coc.phase = 1;
            else {
                coc.vel = vmul(vnorm(vsub(n, in->head)), 60.0f);
                coc.phase = 3; coc.t = 0;
                in->hvel = v3(0, 0, 0);
                if (mario_anim) mario_play("cocoonfly", 1.0f, 1);
                printf("space cocoon row %d: launched from %d off" "\n", in->row, (int)off);
                return 1;
            }
        } else { /* updateHang: the pointer on the plane, at most 500 off */
            V3 o, d;
            cursor_ray(&o, &d);
            float k = vdot(d, up);
            if (fabsf(k) > 1e-3f) {
                float t = vdot(vsub(n, o), up) / k;
                if (t > 0) {
                    V3 p = vadd(o, vmul(d, t)), off = vsub(p, n);
                    if (vlen(off) > 500.0f) p = vadd(n, vmul(vnorm(off), 500.0f));
                    in->head = vadd(vmul(p, 0.03f), vmul(in->head, 0.97f));
                }
            }
            cocoon_place(in);
        }
    }
    m->pos = vsub(in->head, vmul(m->up, HIT_BODY_H)); /* on the cocoon */
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    return 1;
}

/* Fluff.cpp: a dandelion fluff on its leaf. A spin by it sends it up at 10 (FreeWaitAir; again at 5 after 60
 * frames in the air); it drifts down (gravity 0.15, at most 0.8 down) and gone on the ground for 120 frames, then
 * blooms again at home. Mario in the air within 150 of it while it floats takes hold (RideStart: up 0.2 a frame to
 * 15, his speed along the ground kept), then flies (RideFly: x 0.98 a frame, the stick 0.2 along the ground, gravity
 * 0.15); a spin lifts him (RideSpinUp: +0.8 a frame for 30 frames, 3 times; after the last it falls turning), X jumps
 * off (18 up), touching the map lets go. row_turn 0 on its leaf, 1 floating, 2 gone; flf.phase 1 start, 2 fly, 3 spin
 * up; row_step its frames. ponytail: no FluffWind, no leaf model, no Bloom / Spin animations, Mario hangs 130 under it */
/* JetTurtle.cpp (on land): a Koopa shell waiting at its home. Mario touching it takes it (TakenStart: held in front
 * of him); a spin throws it along his front (Throwing) at sThrowSpdStraight (30, arg1 0: 20) a frame; it blows away the
 * walkers it touches (JetTurtleAttack: Unizos too) and is gone (reset) on a wall, after sResetStep (300, arg1 0: 480)
 * frames or 10000 from home; then it is back at home (Restart). jet.phase 1 held, 2 thrown. ponytail: no water jet
 * (swimming), no homing on enemies, no bounding off rebound walls, no gravity in its flight */
static void jet_place(Inst *in)
{
    memcpy(in->m, in->m0, 64);
    in->m[12] = in->head.x; in->m[13] = in->head.y; in->m[14] = in->head.z;
}

static void jet_reset(Inst *in)
{
    in->head = v3(in->m0[12], in->m0[13], in->m0[14]); in->hvel = v3(0, 0, 0);
    jet_place(in);
    row_turn[in->row] = 30; /* a moment before it can be taken again */
    printf("jet turtle row %d: back home" "\n", in->row);
}

/* ScrewSwitch.cpp / ScrewSwitchReverse.cpp: Mario standing on the screw (its binder, 150 up) spins: he is drawn
 * to its middle (Adjust, 3 frames) and held while it screws (ScrewSwitchOn: 34 frames, the reverse one 75), then
 * its SW_A goes on and he is let go; it stays done. row_turn 0 waiting, 1 screwing, 2 done; row_step its frames.
 * ponytail: the screw does not go down (no ScrewSwitchOn joint pose), no ScrewSwitchOn animation for Mario */
/* Candlestand.cpp (PhantomCandlestand, TeresaMansionCandlestand): a candle burning (Burn, its Fire effect / Fire01
 * and Fire02); its flame (sensor 50 x scale at 220 up; 70 at 300) burns Mario (EnemyAttackFire). A spin within 350 x
 * scale puts it out (Extinguish): 15 frames later its item comes out (arg0 -1 a coin, 0 a star piece, at 0 up; 220
 * for TeresaMansion) and it stays out. CandlestandIceVolcano (mCanUseSwitch, sensor 150 at 220): with a SW_A it
 * waits unlit (WaitFire) until a fire ball within 150 x scale lights it (Fire: 60 frames, then its SW_A, Burn); a
 * spin only makes it flicker (30 frames). row_turn 0 burning, 1 going out, 2 out, 3 unlit, 4 lighting, 5 flicker;
 * row_step its frames */
/* FountainBig.cpp: hidden 120 frames (Wait), its sign 80 (Sign), 30 more (SignStop), then the column (Spout, 180
 * frames; its height scale 0 to 1 over the first 20) and down over 10 (SpoutEnd). Mario within 60 of its axis, 600
 * up, is thrown up (ACTMES_FOUNTAINJUMP -> MarioFlow: mJumpHeightFlow 18 with mGravityFlow 0.3, about 540 up: here
 * 44 up with Mario's own gravity, mJumpDistFlow 2 ahead), once until he lands. row_step its cycle frame, row_turn 1
 * while he flies off it; its FountainBigSign and FountainBig effects. ponytail: no Bee Mario flip */
/* SnowMan.cpp: a fire ball on its head or body melts that part (MeltHead / MeltBody: SW_A, SW_B), the second
 * one brings it down (kill: SW_DEAD). Head sensor 100 at 250 up, body 150 at 100 (scale). row_turn bit 1 head
 * melted, bit 2 body melted. ponytail: no melting animation (the model squashes as parts go), spins only shake it */
static int fireball_hits_snowman(V3 p)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->snowman || row_dead[in->row]) continue;
        const Row *r = &sc->objs[in->row];
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), base = v3(in->m[12], in->m[13], in->m[14]);
        int head = vlen(vsub(p, vadd(base, vmul(up, 250.0f * in->s)))) < 100.0f * in->s + 80.0f;
        int body = !head && vlen(vsub(p, vadd(base, vmul(up, 100.0f * in->s)))) < 150.0f * in->s + 80.0f;
        if (!head && !body) continue;
        signed char *st = &row_turn[in->row];
        if (*st) { /* the second part: down */
            if (r->sw[SW_DEAD] >= 0) sw_set(r, SW_DEAD, 1);
            row_kill(in->row);
            printf("snowman row %d melted down" "\n", in->row);
        } else {
            *st = head ? 1 : 2;
            if (head && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
            if (!head && r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
            for (int j = 4; j < 7; j++) in->m[j] *= 0.6f; /* a part gone */
            printf("snowman row %d: %s melted" "\n", in->row, head ? "head" : "body");
        }
        return 1;
    }
    return 0;
}

/* CollectTico.cpp / StrayTico.cpp: a stray Luma waits (in its bubble) until Mario touches it (Glad), then follows him
 * (Chase: behind him, 0.97 friction, warped back when 2000 off). When every StrayTico of a CollectTico's zone is
 * rescued the CollectTico's power star comes out (the row dies: star_wait). row_turn 1 rescued. ponytail: no bubble,
 * no complete demo (the ring of Lumas, CollectTicoLight), the star flies from the CollectTico's row, not 300 over
 * Mario */
static void update_strays(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    int t = scene_type(sc, "CollectTico"), k = 0;
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->stray || row_dead[in->row]) continue;
        int row = in->row;
        if (!row_turn[row]) {
            if (vlen(vsub(c, in->head)) < 60.0f + HIT_BODY_R) {
                row_turn[row] = 1;
                printf("stray tico row %d rescued" "\n", row);
            }
        } else { /* Chase: a place behind Mario, one after another */
            V3 at = vadd(vsub(mario->pos, vmul(mario->face, 150.0f + 80.0f * k)), vmul(mario->up, 150.0f));
            if (vlen(vsub(at, in->head)) > 2000.0f) in->head = at; /* updateWarp */
            in->head = vadd(in->head, vmul(vsub(at, in->head), 0.08f));
            k++;
        }
        stray_at[row % 64] = in->head;
        memcpy(in->m, in->m0, 64);
        in->m[12] = in->head.x; in->m[13] = in->head.y; in->m[14] = in->head.z;
    }
    for (u32 j = 0; t >= 0 && j < sc->nobjs; j++) { /* CollectTico::exeWait: all rescued */
        const Row *r = &sc->objs[j];
        if (r->type != t || row_dead[j]) continue;
        int left = 0, any = 0, st = scene_type(sc, "StrayTico");
        for (u32 q = 0; q < sc->nobjs; q++)
            if (sc->objs[q].type == st && sc->objs[q].zone == r->zone) { any++; left += !row_turn[q]; }
        if (any && !left) {
            if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
            row_kill(j);
            printf("collect tico row %d: all %d rescued" "\n", (int)j, any);
        }
    }
}

/* AirBubble.cpp: touched by Mario (130 x scale) or a spin within cSwingRange 250 it breaks and fills his oxygen
 * (incPlayerOxygen 8: all of it), its row dies. AirBubbleGenerator.cpp: every arg0 (180) frames a bubble comes out
 * 120 along its up and rises (cMoveStartAccel 7 up, then 0.3 a frame, x 0.85) for arg1 (cDefaultLife 360) frames.
 * row_step the generator's frames. ponytail: no swaying (cCoriolis), no RecoveryBubbleBreak effect, no rail bubbles */
static void update_bubbles(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        int row = in->row;
        if (!in->bubble || row_dead[row]) continue;
        const Row *r = &sc->objs[row];
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        if (in->bubble == 1) {
            float d = vlen(vsub(c, p));
            if (d < 130.0f * in->s + HIT_BODY_R || (mario->spin_timer > 0 && d < 250.0f)) {
                oxygen = 3600; row_kill(row);
                printf("air bubble row %d taken" "\n", row);
            }
            continue;
        }
        int wait = r->arg[0] > 0 ? r->arg[0] : 180;
        if (++row_step[row] % wait || !row_shown(row) || vlen(vsub(p, mario->pos)) > 6000.0f) continue;
        for (int b = 0; b < MAX_BUBBLES; b++) {
            if (bubbles[b].life) continue;
            V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
            bubbles[b].pos = vadd(p, vmul(up, 120.0f)); bubbles[b].t = 0;
            bubbles[b].vel = vmul(up_at(bubbles[b].pos, up), 7.0f);
            bubbles[b].life = r->arg[1] > 0 ? r->arg[1] : 360;
            break;
        }
    }
    for (int b = 0; b < MAX_BUBBLES; b++) {
        if (!bubbles[b].life) continue;
        V3 up = up_at(bubbles[b].pos, v3(0, 1, 0));
        bubbles[b].vel = vmul(vadd(bubbles[b].vel, vmul(up, 0.3f)), 0.85f);
        bubbles[b].pos = vadd(bubbles[b].pos, bubbles[b].vel);
        if (vlen(vsub(c, bubbles[b].pos)) < 130.0f + HIT_BODY_R) {
            oxygen = 3600; bubbles[b].life = 0;
            printf("air bubble taken" "\n");
        } else if (++bubbles[b].t >= bubbles[b].life) bubbles[b].life = 0;
    }
}

/* DinoPackun, the first fight (Petari Boss/DinoPackun.cpp, DinoPackunVs1.cpp, DinoPackunBattleEgg.cpp,
 * DinoPackunBattleVs1Lv1.cpp / Lv2.cpp, DinoPackunAction.cpp, DinoPackunStateDamage.cpp, DinoPackunBall.cpp,
 * DinoPackunDemo.cpp). DinoPackunVs1's sequence (dino.seq): WaitStart until SW_A (listenStageSwitchOnA), OpeningDemo
 * (360), BattleEgg, CryDemo (150), BattleLv1, AngryDemo (200), BattleLv2 (setMiddleBattle, from the demo: Start),
 * BattleLv3 (setLastBattle, continued: Find), DownDemo (319, then AppearPowerStar 60). Its battle nerves (dino.nrv):
 * Start (updateStart 60: Find then Wait), Turn, Walk, Find, Chase, CoolDown, AttackHit, Damage; the egg only Turns
 * (120, 0.3) and Walks (300, 0.5). Damage (DinoPackunStateDamage, dino.dnrv): Punched (it stops) until the ball's
 * Charge pulls the tail, Pulled (PunchedTailTop / Right / Left from the ball's side, turning to the tail beyond 800)
 * until the ball hits its head, then EggBroken (120: the shell breaks at 30, EggDamageTop) / Damage / LastDamage
 * (the clip of the side). Contact: the egg's shell pushes Mario; Chase sends a blow (Lv1) and the other nerves a hit
 * (Lv2: Chase and Walk a blow), then AttackHit; a spin on the body flips Mario away (FlipWeakJump), a star piece
 * stops its chase 15 frames (isHitReaction). ponytail: no damage camera (startDamageCamera), no footprints, no
 * star pieces (appearStarPiece 8), no Mario puppet clips in the demos, the head sensor at the Head joint only once the
 * model has streamed in (else 350 up, 200 ahead), velocity kept on the ground by collision (no binder) */
enum { DSEQ_WAIT, DSEQ_OPENING, DSEQ_EGG, DSEQ_CRY, DSEQ_LV1, DSEQ_ANGRY, DSEQ_LV2, DSEQ_LV3, DSEQ_DOWN, DSEQ_GONE };
enum { DN_START, DN_TURN, DN_WALK, DN_FIND, DN_CHASE, DN_COOLDOWN, DN_ATTACKHIT, DN_DAMAGE };
enum { DD_PUNCHED, DD_PULLED, DD_EGGBROKEN, DD_DAMAGE, DD_LASTDAMAGE };
static const int dseq_len[] = {0, 360, 0, 150, 0, 200, 0, 0, 319 + 60};

/* the old phase codes the test builds read: 0 waiting, 1 egg, 2 battle, 3 damage, 4 demo, 5 down, 6 gone, 7 opening */
static void dino_phase(void)
{
    static const int ph[] = {0, 7, 1, 4, 2, 4, 2, 2, 5, 6};
    dino.phase = dino.seq != DSEQ_GONE && (dino.seq == DSEQ_EGG || dino.seq >= DSEQ_LV1) && dino.seq <= DSEQ_LV3 && dino.nrv == DN_DAMAGE ? 3 : ph[dino.seq];
}

/* DinoPackun::startDemoAndReset / resetPosition: back at its placed spot, Mario set at sMarioSetLocalPos (0, -550, 800)
 * in its frame, facing it */
static void dino_demo_reset(void)
{
    const Row *r = &sc->objs[dino.row];
    V3 x, z;
    row_axes(r, &x, &z);
    dino.pos = row_pos(r); dino.up = up_at(dino.pos, dino.up); dino.f = vnorm(vtangent(z, dino.up)); dino.vel = v3(0, 0, 0);
    dino.chain_ok = 0;
    mario->pos = vadd(dino.pos, vadd(vmul(dino.up, -550.0f), vmul(dino.f, 800.0f)));
    mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->launch = -1;
    mario->up = up_at(mario->pos, mario->up);
    mario->face = vnorm(vtangent(vsub(dino.pos, mario->pos), mario->up));
}

/* its clip (DinoPackunBattle*.cpp startBck / startAction, DinoPackunStateDamage, DinoPackunDemo) */
static const char *dino_clip(int *loop)
{
    static const char *const side[] = {"top", "left", "right", "top"};
    static char buf[24];
    int angry = dino.seq >= DSEQ_LV2;
    *loop = 1;
    switch (dino.seq) {
    case DSEQ_WAIT: case DSEQ_OPENING: *loop = 0; return "openingdemo";
    case DSEQ_CRY: *loop = 0; return "crydemo";
    case DSEQ_ANGRY: *loop = 0; return "angrydemo";
    case DSEQ_DOWN: case DSEQ_GONE: *loop = 0; return "downdemo";
    default: break;
    }
    if (dino.nrv == DN_DAMAGE) {
        *loop = 0;
        switch (dino.dnrv) {
        case DD_PUNCHED: break; /* exePunched starts no clip: the one playing goes on */
        case DD_PULLED: if (dino.seq != DSEQ_EGG) { snprintf(buf, sizeof buf, "punchedtail%s", side[dino.ddir]); return buf; } break;
        case DD_EGGBROKEN: if (dino.dt >= 30) return "eggdamagetop"; break;
        case DD_DAMAGE: snprintf(buf, sizeof buf, "damage%s", side[dino.ddir]); return buf;
        default: snprintf(buf, sizeof buf, "lastdamage%s", side[dino.ddir]); return buf;
        }
        dino.nrv = dino.prev_nrv; /* the clip of the nerve it was in */
        const char *c = dino_clip(loop);
        dino.nrv = DN_DAMAGE;
        return c;
    }
    if (dino.seq == DSEQ_EGG) return "eggwalk";
    switch (dino.nrv) {
    case DN_START: *loop = 0; return dino.nt < 44 ? "find" : angry ? "angrywait" : "wait";
    case DN_TURN: return angry ? "angrywalk" : dino.side > 0 ? "turnright" : "turnleft";
    case DN_WALK: return angry ? "angrywalk" : "walk";
    case DN_FIND: *loop = 0; return "find";
    case DN_CHASE: return angry ? "angrychase" : "chase";
    case DN_ATTACKHIT: *loop = 0; return "attackhit";
    default: *loop = 0; return "cooldown";
    }
}

/* DinoPackunTail, lean: TailRoot follows its clip, Tail1..7 hang between it and the ball (FABRIK on the clip's link
 * lengths, from last frame's chain), each turned along the chain (turnJointLocalXDir); the ball (its physics) is held
 * within the tail's length. Off (pure clip, the ball on its joint) before the fight and in the demos
 * (mTail->deactivate / startDemoAndReset). */
static void dino_pose(void)
{
    Anim *a = dino.anim;
    if (!a || !m_dino || !m_dino->skin) return;
    if (a->skin != m_dino->skin) { anim_bind(a, m_dino->skin); dino.chain_ok = 0; }
    int loop;
    char n[48];
    snprintf(n, sizeof n, "dinopackun/%s", dino_clip(&loop));
    anim_play(a, n, dino.seq == DSEQ_WAIT ? 0.0f : 1.0f, loop, 8);
    anim_pose(a);
    float m[16];
    basis_matrix(m, dino.pos, dino.up, dino.f, 0);
    int chain = (dino.seq == DSEQ_EGG || (dino.seq >= DSEQ_LV1 && dino.seq <= DSEQ_LV3 && dino.seq != DSEQ_ANGRY)) && a->skin->njoints > 20;
    if (!chain) { /* the ball on its joint */
        float *w = a->world[20];
        V3 lp = v3(w[12], w[13], w[14]);
        dino.bpos = vadd(v3(m[12], m[13], m[14]), vadd(vadd(vmul(v3(m[0], m[1], m[2]), lp.x), vmul(v3(m[4], m[5], m[6]), lp.y)), vmul(v3(m[8], m[9], m[10]), lp.z)));
        dino.bvel = v3(0, 0, 0);
        dino.chain_ok = 0;
        anim_skin(a);
        return;
    }
    V3 org = v3(m[12], m[13], m[14]), ax = v3(m[0], m[1], m[2]), ay = v3(m[4], m[5], m[6]), az = v3(m[8], m[9], m[10]);
    V3 d = vsub(dino.bpos, org), target = v3(vdot(d, ax), vdot(d, ay), vdot(d, az)); /* the ball, model space */
    V3 anim_p[9];
    float len[9], total = 0;
    for (int i = 0; i < 9; i++) anim_p[i] = v3(a->world[12 + i][12], a->world[12 + i][13], a->world[12 + i][14]);
    for (int i = 1; i < 9; i++) { len[i] = vlen(vsub(anim_p[i], anim_p[i - 1])); total += len[i]; }
    if (!dino.chain_ok) { for (int i = 0; i < 9; i++) dino.chain[i] = anim_p[i]; dino.chain_ok = 1; }
    V3 root = anim_p[0], rt = vsub(target, root);
    if (vlen(rt) > total * 0.98f) { /* the tail holds the ball */
        target = vadd(root, vmul(vnorm(rt), total * 0.98f));
        V3 w = vadd(org, vadd(vadd(vmul(ax, target.x), vmul(ay, target.y)), vmul(az, target.z)));
        V3 out = vnorm(vsub(dino.bpos, w));
        float vo = vdot(dino.bvel, out);
        if (vo > 0) dino.bvel = vsub(dino.bvel, vmul(out, vo));
        dino.bpos = w;
    }
    V3 *p = dino.chain;
    for (int i = 1; i < 8; i++) p[i] = vadd(p[i], vmul(vsub(anim_p[i], p[i]), 0.05f)); /* keep its bend (sKeepBendPower) */
    for (int it = 0; it < 4; it++) {
        p[8] = target;
        for (int i = 7; i >= 0; i--) p[i] = vadd(p[i + 1], vmul(vnorm(vsub(p[i], p[i + 1])), len[i + 1]));
        p[0] = root;
        for (int i = 1; i < 9; i++) p[i] = vadd(p[i - 1], vmul(vnorm(vsub(p[i], p[i - 1])), len[i]));
    }
    for (int i = 1; i < 9; i++) { /* Tail1 .. TailBall: at the chain, Tail1 .. Tail7 turned along it */
        float *w = a->world[12 + i];
        if (i < 8) {
            V3 x = vnorm(vsub(p[i + 1], p[i])), z0 = v3(w[8], w[9], w[10]), y = vcross(z0, x);
            if (vlen(y) < 1e-3f) y = v3(w[4], w[5], w[6]);
            y = vnorm(y);
            V3 z = vcross(x, y);
            float sx = vlen(v3(w[0], w[1], w[2])), sy = vlen(v3(w[4], w[5], w[6])), sz = vlen(v3(w[8], w[9], w[10]));
            w[0] = x.x * sx; w[1] = x.y * sx; w[2] = x.z * sx;
            w[4] = y.x * sy; w[5] = y.y * sy; w[6] = y.z * sy;
            w[8] = z.x * sz; w[9] = z.y * sz; w[10] = z.z * sz;
        }
        w[12] = p[i].x; w[13] = p[i].y; w[14] = p[i].z;
    }
    anim_skin(a);
}

/* the head sensor (addHitSensorAtJointEnemy "head", joint Head, offset sHeadHitOffset (140, -110, 0), radius 270) */
static V3 dino_head(void)
{
    if (dino.anim && m_dino && dino.anim->skin == m_dino->skin && dino.anim->skin->njoints > 27) {
        float m[16], jm[16];
        basis_matrix(m, dino.pos, dino.up, dino.f, 0);
        mat_mul(jm, m, dino.anim->world[27]);
        return v3(jm[12] + 140.0f * jm[0] - 110.0f * jm[4], jm[13] + 140.0f * jm[1] - 110.0f * jm[5], jm[14] + 140.0f * jm[2] - 110.0f * jm[6]);
    }
    return vadd(dino.pos, vadd(vmul(dino.up, 350.0f), vmul(dino.f, 200.0f)));
}

static void dino_set_nrv(int n) { dino.nrv = n; dino.nt = 0; }

/* the sequencer's next action once one ends (DinoPackunVs1 exe*: updateActorStateAndNextNerve) */
static void dino_next_seq(void)
{
    dino.seq++; dino.t = 0;
    if (dino.seq == DSEQ_EGG) dino_set_nrv(DN_TURN); /* DinoPackunBattleEgg::appear */
    else if (dino.seq == DSEQ_LV1 || dino.seq == DSEQ_LV2) dino_set_nrv(DN_START); /* appear (Lv2: setStartFromDemo) */
    else if (dino.seq == DSEQ_LV3) dino_set_nrv(DN_FIND); /* setStartContinueBattle */
    if (dino.seq == DSEQ_OPENING || dino.seq == DSEQ_CRY || dino.seq == DSEQ_ANGRY || dino.seq == DSEQ_DOWN) dino_demo_reset();
    printf("dino packun: seq %d" "\n", dino.seq);
}

static void update_dino(void)
{
    if (dino.row < 0 || dino.seq == DSEQ_GONE || !row_shown(dino.row)) return;
    const Row *r = &sc->objs[dino.row];
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    dino.up = up_at(dino.pos, dino.up);
    dino.f = vnorm(vtangent(dino.f, dino.up));
    V3 up = dino.up, side = vcross(up, dino.f), body = vadd(dino.pos, vmul(up, 200.0f)), head = dino_head();
    int egg = dino.seq == DSEQ_EGG, lv2 = dino.seq >= DSEQ_LV2;
    dino.t++; dino.nt++;
    if (dino.react) dino.react--;
    if (dino.seq == DSEQ_WAIT) {
        if (r->sw[SW_A] < 0 || sw_on(r, SW_A)) { printf("dino packun: start" "\n"); dino_next_seq(); }
        dino_phase(); dino_pose();
        return;
    }
    if (dseq_len[dino.seq]) { /* a demo */
        if (dino.t >= dseq_len[dino.seq]) {
            if (dino.seq == DSEQ_DOWN) { /* AppearPowerStar: its star, it is gone */
                dino.seq = DSEQ_GONE; row_kill(dino.row);
                printf("dino packun: down, its star" "\n");
                dino_phase();
                return;
            }
            dino_next_seq();
        }
    }
    float accel = 0;
    V3 to = vtangent(vsub(mario->pos, dino.pos), up);
    float dm = vlen(to), face = dm > 1e-3f ? vdot(vnorm(to), dino.f) : 1.0f;
    #define DINO_TURN_TO(v, rate) do { V3 tt = (v); if (vlen(tt) > 1e-3f) { \
        float a = atan2f(vdot(tt, side), vdot(tt, dino.f)), mx = (rate) * 0.0174533f; a = a > mx ? mx : a < -mx ? -mx : a; \
        dino.f = vnorm(vadd(vmul(dino.f, cosf(a)), vmul(side, sinf(a)))); } } while (0)
    int battle = egg || dino.seq == DSEQ_LV1 || dino.seq == DSEQ_LV2 || dino.seq == DSEQ_LV3;
    if (battle) {
        /* tryFind: isInSightConePlayer 1000 / 80 (Lv2 1300 / 90) */
        int sees = !egg && dm < (lv2 ? 1300.0f : 1000.0f) && face > cosf((lv2 ? 90.0f : 80.0f) * 0.0174533f);
        switch (dino.nrv) {
        case DN_START: /* updateStart: 60 frames, then Chase */
            if (dino.nt >= 60) dino_set_nrv(DN_CHASE);
            break;
        case DN_TURN: /* updateTurn(60, 1) / egg (120, 0.3) / Lv2: 1 a frame with 1.3 forward, 30 */
            DINO_TURN_TO(vmul(side, dino.side), egg ? 0.3f : 1.0f);
            if (lv2) accel = 1.3f;
            if (dino.nt > (egg ? 120 : lv2 ? 30 : 60)) dino_set_nrv(DN_WALK);
            else if (sees) dino_set_nrv(DN_FIND);
            break;
        case DN_WALK: /* updateWalk(180, 0.5) / egg (300, 0.5) / Lv2 (180, 1.3); selectTurnDirection at its end */
            accel = lv2 ? 1.3f : 0.5f;
            if (dino.nt > (egg ? 300 : 180)) { dino_set_nrv(DN_TURN); dino.side = (rand() & 1) ? 1.0f : -1.0f; }
            else if (sees) dino_set_nrv(DN_FIND);
            break;
        case DN_FIND: /* updateFind(14, 1) / Lv2 (14, 4): until its Find clip stops (44) */
            DINO_TURN_TO(to, lv2 ? 4.0f : 1.0f);
            if (dino.nt >= 44) dino_set_nrv(DN_CHASE);
            break;
        case DN_CHASE: { /* updateChase(240, 180, 1, 1.5, 60) / Lv2 (300, 180, 1.3, 1.8, 90); a hit reaction: nothing */
            int len = lv2 ? 300 : 240, ramp = lv2 ? 90 : 60;
            if (!dino.react) {
                DINO_TURN_TO(to, lv2 ? 1.3f : 1.0f);
                accel = (lv2 ? 1.8f : 1.5f) * (dino.nt < ramp ? (float)dino.nt / ramp : 1.0f);
            }
            if (dino.nt > len) dino_set_nrv(DN_COOLDOWN);
            break;
        }
        case DN_ATTACKHIT: /* updateAttackHit: its AttackHit action (239) */
            if (dino.nt >= 239) dino_set_nrv(DN_TURN);
            break;
        case DN_COOLDOWN: /* updateCoolDown(90) / Lv2 (30) */
            if (dino.nt > (lv2 ? 30 : 90)) dino_set_nrv(DN_TURN);
            break;
        case DN_DAMAGE: /* DinoPackunStateDamage */
            dino.dt++;
            if (dino.dnrv == DD_PULLED && vlen(vsub(dino.bpos, dino.pos)) > 800.0f) /* turnDirectionFromTargetDegree, 6 */
                DINO_TURN_TO(vmul(vtangent(vsub(dino.chain_ok ? dino.bpos : dino.bpos, dino.pos), up), -1.0f), 6.0f);
            if ((dino.dnrv == DD_EGGBROKEN && dino.dt > 120) || (dino.dnrv == DD_DAMAGE && dino.dt >= 79) ||
                (dino.dnrv == DD_LASTDAMAGE && dino.dt >= 49)) { /* its state is over: the sequencer's next */
                demo_dino_damage = 0;
                dino_next_seq();
            }
            break;
        }
        /* contact (attackSensor) */
        float dist = vlen(vsub(c, body));
        if (egg && dist < 400.0f + HIT_BODY_R) { /* the shell pushes him off (sendMsgPush) */
            V3 out = vnorm(vtangent(vsub(c, body), up));
            mario->pos = vadd(mario->pos, vmul(out, 400.0f + HIT_BODY_R - dist));
        } else if (!egg && dino.nrv != DN_DAMAGE && dino.nrv != DN_ATTACKHIT && dist < 200.0f + HIT_BODY_R) {
            if (mario->spin_timer > 0 && !mario->hurt_timer) { /* isMsgPlayerSpinAttack: sendMsgEnemyAttackFlipWeakJump */
                V3 out = vnorm(vtangent(vsub(c, body), up));
                mario->hvel = vmul(out, 10.0f); mario->vup = 15.0f; mario->grounded = 0;
            } else if (!mario->hurt_timer) { /* blow (Chase; Lv2 Walk too) or hit, then AttackHit */
                int blow = dino.nrv == DN_CHASE || (lv2 && dino.nrv == DN_WALK);
                hurt_mario(dino.pos, blow ? 25.0f : 15.0f);
                dino_set_nrv(DN_ATTACKHIT);
            }
        }
    }
    if (accel > 0) dino.vel = vadd(dino.vel, vmul(dino.f, accel)); /* addVelocityMoveToDirection, updateNormalVelocity */
    if (!battle || dino.nrv == DN_DAMAGE || dino.nrv == DN_ATTACKHIT || dino.nrv == DN_START || dino.nrv == DN_FIND || dino.nrv == DN_COOLDOWN)
        dino.vel = vmul(dino.vel, 0.9f);
    if (dino.nrv == DN_DAMAGE) dino.vel = v3(0, 0, 0); /* zeroVelocity */
    dino.vel = vmul(vtangent(dino.vel, up), 0.9f);
    V3 center = vadd(vadd(dino.pos, dino.vel), vmul(up, 150.0f - 4.0f)), gn, wn;
    int wall = 0;
    /* only in the battle it moves (the demos hold it where resetPosition put it); no ground under it (its zone still
     * streaming, the egg's nest) keeps it where it is rather than falling through */
    if (battle && (r->zone >= 64 || zone_kcl[r->zone]) && collide_n(&center, up, 150.0f, &gn, &wn, &wall))
        dino.pos = vsub(center, vmul(up, 150.0f));

    /* its tail ball (DinoPackunBall): Wait, Shoot, Charge, Reverse, Rebound */
    V3 rest = vsub(dino.pos, vmul(dino.f, 600.0f)), bup = up_at(dino.bpos, up);
    dino.bt++;
    switch (dino.bstate) {
    case 0: case 4: /* exeWait / exeRebound: gravity 1, x 0.9 (dragged by the tail when it has no chain yet) */
        dino.bvel = vmul(vadd(dino.bvel, vmul(bup, -1.0f)), 0.9f);
        if (!dino.chain_ok) dino.bvel = vadd(dino.bvel, vmul(vsub(rest, dino.bpos), 0.05f));
        if (dino.bstate == 4 && dino.bt >= 180) { dino.bstate = 0; dino.bt = 0; }
        if (dino.bstate == 0 && battle && dino.nrv != DN_DAMAGE && mario->spin_timer > 0 &&
            vlen(vsub(c, dino.bpos)) < 135.0f + HIT_BODY_R + 60.0f) { /* requestPunch: PUNCHED_BALL to the head */
            V3 away = vtangent(vsub(dino.bpos, mario->pos), bup);
            away = vlen(away) > 1e-3f ? vnorm(away) : vmul(dino.f, -1.0f);
            dino.bvel = vadd(vmul(away, 120.0f), vmul(bup, 40.0f)); /* addVelocitySeparateHV 120, 40 */
            dino.bstate = 1; dino.bt = 0;
            dino.prev_nrv = dino.nrv; dino_set_nrv(DN_DAMAGE); dino.dnrv = DD_PUNCHED; dino.dt = 0; /* setNerve Damage */
            printf("dino packun: tail ball punched" "\n");
        }
        break;
    case 1: { /* exeShoot: addDodgeTargetVelocity, x 1; 30 frames or 1500 from the head */
        V3 v = vtangent(vsub(dino.bpos, head), up);
        float dd = vlen(v);
        if (dd > 1e-3f) {
            v = vmul(v, 1.0f / dd);
            float vin = vdot(dino.bvel, v);
            if (vin < 0) dino.bvel = vsub(dino.bvel, vmul(v, vin * (1.0f - fminf(fmaxf((dd - 500.0f) / 1000.0f, 0.0f), 1.0f))));
            if (dd < 500.0f) dino.bvel = vadd(dino.bvel, v);
        }
        if (dino.bt > 30 || vlen(vsub(dino.bpos, head)) >= 1500.0f) {
            dino.bstate = 2; dino.bt = 0;
            if (dino.nrv == DN_DAMAGE && dino.dnrv == DD_PUNCHED) { /* PULLED_TAIL: TryPulledDemo, Pulled */
                V3 dir = vnorm(vsub(dino.bpos, head));
                dino.ddir = vdot(up, dir) > 0.7f ? 3 : vdot(side, dir) < 0.0f ? 2 : 1;
                dino.dnrv = DD_PULLED; dino.dt = 0; demo_dino_damage = 1;
            }
        }
        break;
    }
    case 2: /* exeCharge: x 0.88 for 35 */
        dino.bvel = vmul(dino.bvel, 0.88f);
        if (dino.bt > 35) { dino.bstate = 3; dino.bt = 0; }
        break;
    case 3: /* exeReverse: 8 a frame at the head, x 0.98, until it hits it (BALL_ATTACK: x 0.3 back, Rebound) */
        dino.bvel = vmul(vadd(dino.bvel, vmul(vnorm(vsub(head, dino.bpos)), 8.0f)), 0.98f);
        if (vlen(vsub(head, dino.bpos)) < 270.0f + 135.0f) {
            dino.hits++;
            dino.bvel = vmul(vnorm(vsub(dino.bpos, head)), 0.3f * vlen(dino.bvel));
            dino.bstate = 4; dino.bt = 0;
            if (dino.nrv == DN_DAMAGE) { /* DinoPackunStateDamage: EggBroken / Damage / LastDamage */
                dino.dnrv = egg ? DD_EGGBROKEN : dino.seq == DSEQ_LV3 ? DD_LASTDAMAGE : DD_DAMAGE; dino.dt = 0;
            }
            printf("dino packun: hit %d" "\n", dino.hits);
        } else if (dino.bt >= 150) { /* ponytail: a ball that never reaches the head (Petari's tail chain brings it) */
            dino.bstate = 4; dino.bt = 0;
            if (dino.nrv == DN_DAMAGE) { demo_dino_damage = 0; dino_set_nrv(DN_TURN); }
        }
        break;
    }
    V3 bc = vadd(dino.bpos, dino.bvel);
    if (dino.bstate == 0 || dino.bstate == 4) collide_n(&bc, bup, 135.0f, &gn, &wn, &wall);
    dino.bpos = bc;
    #undef DINO_TURN_TO
    dino_phase();
    dino_pose();
    { /* its demos' cameras (DinoPackunDemo: OpeningDemo, CryDemo, AngryDemo, DownDemo) */
        const char *cn = dino.seq == DSEQ_OPENING ? "dinopackun/openingdemo" : dino.seq == DSEQ_CRY ? "dinopackun/crydemo" :
                         dino.seq == DSEQ_ANGRY ? "dinopackun/angrydemo" : dino.seq == DSEQ_DOWN && dino.t < 385 ? "dinopackun/downdemo" : NULL;
        static int was;
        float m[16];
        basis_matrix(m, dino.pos, dino.up, dino.f, 0);
        int on = cn && demo_camera(cn, dino.t, m);
        if (was && !on) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45);
        was = on;
        dino_demo_on = on || demo_dino_damage;
    }
}


/* OtaKing (Lv1), after Petari OtaKing.cpp: WaitOnSwitch until SW_A (startAppearDemo), AppearDemo (its Appear clip, the
 * "Appear" camera, Mario hidden), then ThrowFireBallWait. Wait (cWaitFrame 75) / ThrowCocoNutWait (45): a coconut when
 * isValidThrowCocoNut (one free, Mario within cThrowDistance 4000, none already nearer than him + 500), at twice the
 * wait fire balls when isValidThrowFireBall (three free, within 4000). ThrowCocoNut: Attack, the coconut at
 * cThrowCocoNutStep 70; ThrowFireBall: FireAttack, three at cThrowFireBallStep 94 (15 a frame, 0 / -30 / +30 degrees).
 * Coconut rallies (CocoNutBall): a spin within 400 sends it back at its body; one with a rally count up to its hits
 * makes it HitBack (the ball back at Mario at 15 + 3 x rally), one over them damages it (cHP 3: Damage, PowerUp at its
 * last point, Down); the face sensor first makes it HitBackStart. A ball's end after a rally (RUSH_END) sends it on to
 * BubbleAttack / fire balls. A touch in the attacking nerves is a NearAttack (EnemyAttackMaximum). BubbleAttack (one
 * point left): six FireBubbles at cBubbleAppearStep 100. Down: DownDemo (300, the "Down" camera, its star asked at its
 * start), AppearStar (gone), Dead. It turns to Mario at cTurnSpeedAccel 0.003 up to cTurnSpeedMax 1 a frame (dirToPlayer).
 * ponytail: no OtaKingMagma (the bath's ripples), no long feet (OtaKingLongFoot), no star pointer (coconut freeze, fire
 * reflect, bubble reflect), no star pieces (appearStarPiece), no crown material hiding (PowerUp step 83), no effects */
static int mario_hidden; /* MR::hidePlayer in a demo */
static V3 ota_world(const float m[16], V3 l) { return v3(m[12] + m[0] * l.x + m[4] * l.y + m[8] * l.z, m[13] + m[1] * l.x + m[5] * l.y + m[9] * l.z, m[14] + m[2] * l.x + m[6] * l.y + m[10] * l.z); }
/* a joint's point (its world matrix x the local offset), else the model's base point + offset when not streamed in */
static V3 ota_joint(int j, V3 off)
{
    float m[16];
    basis_matrix(m, ota.pos, ota.up, ota.f, 0);
    if (ota.anim && m_ota && ota.anim->skin == m_ota->skin && (u32)j < ota.anim->skin->njoints) {
        float jm[16];
        mat_mul(jm, m, ota.anim->world[j]);
        return ota_world(jm, off);
    }
    return ota_world(m, vadd(off, v3(0, 600.0f, 0)));
}
static float ota_rnd(float a, float b) { return a + (b - a) * (float)(rand() & 0x7FFF) / 32767.0f; }
static V3 ota_rot(V3 v, V3 axis, float deg) /* v about axis by deg */
{
    float a = deg * 0.0174533f, c = cosf(a), s = sinf(a);
    return vadd(vadd(vmul(v, c), vmul(vcross(axis, v), s)), vmul(axis, vdot(axis, v) * (1 - c)));
}
static int ota_dead_balls(void) { int n = 0; for (int i = 0; i < OTA_BALLS; i++) n += !ota.ball[i].state; return n; }
static int ota_dead_fires(void) { int n = 0; for (int i = 0; i < OTA_FIRES; i++) n += !ota.fire[i].alive; return n; }
static int ota_dead_bubbles(void) { int n = 0; for (int i = 0; i < OTA_BUBBLES; i++) n += !ota.bub[i].state; return n; }
static int ota_valid_coco(void)
{
    if (!ota_dead_balls()) return 0;
    float d = vlen(vsub(mario->pos, ota.pos));
    if (d > 4000.0f) return 0;
    for (int i = 0; i < OTA_BALLS; i++)
        if (ota.ball[i].state && vlen(vsub(ota.ball[i].pos, ota.pos)) < d + 500.0f) return 0;
    return 1;
}
static int ota_valid_fire(void) { return ota_dead_fires() >= 3 && vlen(vsub(mario->pos, ota.pos)) < 4000.0f; }
static int ota_valid_bubble(void) { return ota_dead_bubbles() == OTA_BUBBLES && ota.hits + 1 >= 3; }
static int ota_damage_nrv(void) { return ota.nrv == OTA_DAMAGE || ota.nrv == OTA_POWERUP || ota.nrv == OTA_DOWN; }
static void ota_set(int n) { ota.nrv = n; ota.t = 0; }
/* the angle (degrees) of Mario about its up from its placed front, nearest its yaw (repeat(.., rotY - 180, 360)) */
static float ota_player_angle(void)
{
    V3 to = vtangent(vsub(mario->pos, ota.pos), ota.up), s0 = vcross(ota.up, ota.f0);
    float a = atan2f(vdot(to, s0), vdot(to, ota.f0)) * 57.29578f;
    while (a < ota.yaw - 180.0f) a += 360.0f;
    while (a >= ota.yaw + 180.0f) a -= 360.0f;
    return a;
}

static void ota_damage(void) /* OtaKing::damage */
{
    if (ota_damage_nrv()) return;
    ota.hits++;
    if (ota.hits < 3) ota_set(ota.hits + 1 >= 3 ? OTA_POWERUP : OTA_DAMAGE);
    else ota_set(OTA_DOWN);
    printf("ota king: hit %d" "\n", ota.hits);
}

/* CocoNutBall::setVelocityToPlayer(speed, angle) with _BE: at Mario turned `deg` about its up, the target height kept
 * between _C4 and _C0 over its host and raised over the first clear line (getFirstPolyOnLineToMap) */
static void ota_ball_aim(int b, float speed, float deg)
{
    typeof(&ota.ball[0]) k = &ota.ball[b];
    V3 tgt = vadd(k->pos, ota_rot(vsub(mario->pos, k->pos), k->up, deg));
    float hy = vdot(ota.pos, k->up), ty = vdot(tgt, k->up), miny = ota.c4 + hy, maxy = ota.c0 + hy;
    if (ty < miny) { tgt = vadd(tgt, vmul(k->up, miny - ty)); ty = miny; }
    int clear = 0;
    while (ty < maxy - 120.0f) {
        V3 seg = vmul(vsub(tgt, k->pos), 1.5f);
        Hit h[1];
        int hit = 0;
        float l = vlen(seg);
        for (float s = 60.0f; s < l && !hit; s += 60.0f) hit = kcl_query(vadd(k->pos, vmul(seg, s / l)), 10.0f, h, 1) > 0;
        if (!hit) { clear = 1; tgt = vadd(tgt, vmul(k->up, 120.0f)); break; }
        tgt = vadd(tgt, vmul(k->up, 60.0f)); ty += 60.0f;
    }
    if (!clear) tgt = vadd(tgt, vmul(k->up, maxy - vdot(tgt, k->up)));
    k->vel = vmul(vnorm(vsub(tgt, k->pos)), speed);
}

static void ota_ball_kill(int b) /* CocoNutBall::kill: a rally's end tells its host (ACTMES_RUSH_END) */
{
    int rally = ota.ball[b].rally;
    ota.ball[b].state = 0;
    if (rally <= 0 || ota_damage_nrv()) return;
    if (ota.nrv == OTA_WAIT || ota.nrv == OTA_THROWCOCOWAIT || ota.nrv == OTA_HITBACKSTART)
        ota_set(ota_valid_bubble() ? OTA_BUBBLEATTACK : ota.hits > 0 && ota_valid_fire() ? OTA_THROWFIRE : OTA_THROWFIREWAIT);
    else if (ota.nrv == OTA_THROWCOCO || ota.nrv == OTA_HITBACK)
        ota.ec = 2;
}

static void update_ota_balls(V3 c, V3 body, V3 face)
{
    for (int b = 0; b < OTA_BALLS; b++) {
        typeof(&ota.ball[0]) k = &ota.ball[b];
        if (!k->state) continue;
        k->t++;
        if (k->state == 2 && k->t >= 1) k->vel = vadd(k->vel, k->grav); /* exeHitBackToHost: + _90 */
        if (k->state == 4) k->vel = vadd(k->vel, vmul(k->up, -2.0f)); /* exeRebound: twice the gravity */
        k->pos = vadd(k->pos, k->vel);
        Hit h[1];
        int bound = k->state != 2 && kcl_query(k->pos, 60.0f, h, 1) > 0; /* tryToKill: binded (offBind going home) */
        float far = k->state == 1 || k->state == 3 ? vlen(vsub(k->pos, ota.pos)) : vlen(vsub(k->pos, mario->pos));
        if (bound || far > 5000.0f) { ota_ball_kill(b); continue; }
        float dc = vlen(vsub(c, k->pos));
        /* isValidReceivePunch: Throw / HitBackToPlayer / Rebound within 400 of his centre: HitBackToHost */
        if ((k->state == 1 || k->state == 3 || k->state == 4) && mario->spin_timer > 0 && dc < 400.0f && k->t > 1) {
            k->rally++;
            V3 hb = vnorm(vtangent(vsub(ota.pos, mario->pos), k->up)), kb = vnorm(vtangent(vsub(k->pos, mario->pos), k->up));
            int right = vdot(k->up, vcross(hb, kb)) > 0.0f, front = vdot(vmul(hb, -1.0f), kb) < -0.95f;
            V3 dst = vadd(body, vmul(k->up, 100.0f));
            if (!front) dst = vadd(dst, vmul(vnorm(vcross(k->up, hb)), right ? 150.0f : -150.0f)); /* calcHitBackDstPos */
            V3 dir = vsub(dst, k->pos), h = vtangent(dir, k->up);
            float dv = vdot(dir, k->up), f1 = vlen(h) / 42.0f;
            if (f1 < 1.0f) f1 = 1.0f;
            V3 cr = vnorm(vcross(k->up, vnorm(h)));
            k->grav = vmul(k->up, -2.2f);
            k->vel = vadd(vmul(vnorm(h), 42.0f), vmul(k->up, dv / f1 + 1.1f * f1)); /* calcHitBackVelocitAndGravity */
            if (!front) {
                float sf = right ? 1.2f : -1.2f;
                k->vel = vadd(k->vel, vmul(cr, sf * f1 / 2.0f));
                k->grav = vsub(k->grav, vmul(cr, sf));
            }
            k->state = 2; k->t = 0;
            printf("ota king: coconut hit back (rally %d)" "\n", k->rally);
            continue;
        }
        if ((k->state == 1 || k->state == 3) && dc < 40.0f + HIT_BODY_R) { /* FLIP_VERYWEAK (no damage), Rebound */
            V3 away = vnorm(vtangent(vsub(k->pos, mario->pos), k->up));
            k->vel = vadd(vmul(k->up, 15.0f), vmul(away, 5.0f)); /* cReboundVelocity (0, 15, 5) */
            k->state = 4; k->t = 0;
            continue;
        }
        if (k->state == 2) { /* its host's sensors: face (HitBackStart), body (HitBack or damage) */
            int b1 = k->rally <= ota.hits;
            if (vlen(vsub(k->pos, body)) < 400.0f + 40.0f) {
                if (b1) {
                    if (ota.nrv != OTA_HITBACK) ota_set(OTA_HITBACK);
                    float r = ota_rnd(12.5f, 17.5f); /* hitBackToPlayer */
                    if (k->alt) { r = -r; k->alt = 0; } else k->alt = 1;
                    ota_ball_aim(b, 3.0f * k->rally + 15.0f, r);
                    k->state = 3; k->t = 0;
                    printf("ota king: hits it back" "\n");
                } else {
                    ota_damage();
                    ota_ball_kill(b);
                }
            } else if (vlen(vsub(k->pos, face)) < 400.0f + 40.0f && b1 && ota.nrv != OTA_HITBACKSTART && ota.nrv != OTA_HITBACK)
                ota_set(OTA_HITBACKSTART);
        }
    }
}

static void update_ota_fire(V3 c)
{
    for (int i = 0; i < OTA_FIRES; i++) { /* FireBall::exeThrow: straight on, gone on the map or 5000 off; a touch burns */
        if (!ota.fire[i].alive) continue;
        ota.fire[i].pos = vadd(ota.fire[i].pos, ota.fire[i].vel);
        Hit h[1];
        if (kcl_query(ota.fire[i].pos, 60.0f, h, 1) > 0 || vlen(vsub(ota.fire[i].pos, mario->pos)) > 5000.0f) { ota.fire[i].alive = 0; continue; }
        if (vlen(vsub(c, ota.fire[i].pos)) < 40.0f + HIT_BODY_R) { hurt_mario(ota.fire[i].pos, 20.0f); ota.fire[i].alive = 0; }
    }
    for (int i = 0; i < OTA_BUBBLES; i++) { /* FireBubble: Appear (gravity 0.75), Wait, Chase (4, gravity 2), Down */
        typeof(&ota.bub[0]) q = &ota.bub[i];
        if (!q->state) continue;
        q->t++;
        V3 up = up_at(q->pos, ota.up);
        if (q->state == 1) q->vel = vadd(q->vel, vmul(up, -0.75f));
        else if (q->state == 3 || q->state == 4) {
            V3 to = vtangent(vsub(vadd(mario->pos, vmul(mario->up, 40.0f)), q->pos), up);
            if (vlen(to) > 1e-3f) { /* updateChaseFrontVec: 1 degree a frame */
                V3 s = vcross(up, q->front);
                float a = atan2f(vdot(vnorm(to), s), vdot(vnorm(to), q->front)), mx = 0.0174533f;
                a = a > mx ? mx : a < -mx ? -mx : a;
                q->front = vnorm(vadd(vmul(q->front, cosf(a)), vmul(s, sinf(a))));
            }
            q->vel = vadd(vmul(q->front, 4.0f), vmul(up, vdot(q->vel, up) - 2.0f));
        } else q->vel = v3(0, 0, 0);
        V3 cc = vadd(vadd(q->pos, q->vel), vmul(up, 40.0f)), gn, wn;
        int wall = 0, ground = collide_n(&cc, up, 40.0f, &gn, &wn, &wall);
        q->pos = vsub(cc, vmul(up, 40.0f));
        if (ground) q->vel = vtangent(q->vel, up);
        if (q->state == 1 && ground && q->t > 2) { q->state = 2; q->t = 0; q->act = (rand() % 121) + 600; }
        else if (q->state == 2) {
            if (q->t > 30 && vlen(vsub(q->pos, mario->pos)) < 2000.0f) { q->state = 3; q->t = 0; q->act = (rand() % 121) + 300; q->chase = 0; }
            else if (q->t >= q->act) { q->state = 4; q->t = 0; }
        } else if (q->state == 3 && ++q->chase > q->act) { q->state = 4; q->t = 0; }
        else if (q->state == 4 && q->t >= 60) { q->state = 0; continue; } /* its Down clip, then killOrWaitToApppear */
        if (q->state != 1 && q->state != 4 && vlen(vsub(c, q->pos)) < 30.0f + HIT_BODY_R) { hurt_mario(q->pos, 20.0f); q->state = 0; }
    }
}

static void ota_pose(void);
static void update_ota(void)
{
    if (ota.row < 0 || ota.nrv == OTA_DEAD) return;
    const Row *r = &sc->objs[ota.row];
    if (ota.nrv == OTA_WAITONSWITCH) {
        if (!row_shown(ota.row) || !(r->sw[SW_A] < 0 || sw_on(r, SW_A))) return;
        ota_set(OTA_APPEARDEMO); ota.yaw = 30.0f; ota.turn = 0; /* cAppearDemoRotate */
        printf("ota king: appears" "\n");
    }
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    ota.t++;
    /* control: dirToPlayer, except in the demos and Down */
    if (ota.nrv == OTA_DOWN || ota.nrv == OTA_APPEARSTAR || ota.nrv == OTA_APPEARDEMO || ota.nrv == OTA_DOWNDEMO) ota.turn = 0;
    else {
        float a = ota_player_angle(), d = a - ota.yaw;
        if ((d > 0 && ota.turn < d) || (d < 0 && d < ota.turn)) {
            if (d * ota.turn < 0) ota.turn = 0;
            ota.yaw += ota.turn;
            ota.turn = d > 0 ? fminf(ota.turn + 0.003f, 1.0f) : fmaxf(ota.turn - 0.003f, -1.0f);
        } else { ota.yaw = a; ota.turn = 0; }
    }
    ota.f = vnorm(ota_rot(ota.f0, ota.up, ota.yaw));
    V3 body = vadd(ota.pos, vmul(ota.up, 150.0f)), face = ota_joint(4, v3(0, 0, 0)); /* "body" cSensorOffset, "face" at Face */
    float throw_ang = ota.yaw - ota_player_angle();
    switch (ota.nrv) {
    case OTA_APPEARDEMO:
        if (ota.t >= 424) { ota.yaw = ota_player_angle(); ota_set(OTA_THROWFIREWAIT); }
        break;
    case OTA_WAIT: case OTA_THROWCOCOWAIT: { /* tryThrowCocoNutOrFireBallIfWait */
        int w = ota.nrv == OTA_WAIT ? 75 : 45;
        if (ota.t > w && ota_valid_coco()) { if (ota.nrv == OTA_WAIT) ota.ec = 0; ota_set(OTA_THROWCOCO); }
        else if (ota.t > 2 * w && ota_valid_fire()) ota_set(OTA_THROWFIRE);
        break;
    }
    case OTA_THROWCOCO:
        if (ota.t == 70) { /* throwCocoNut: from mThrowPos (Lip, -50 0 0) */
            for (int b = 0; b < OTA_BALLS; b++) if (!ota.ball[b].state) {
                typeof(&ota.ball[0]) k = &ota.ball[b];
                k->pos = ota_joint(11, v3(-50.0f, 0, 0)); k->up = up_at(k->pos, ota.up);
                k->alt = rand() & 1; k->rally = 0; k->t = 0; k->state = 1;
                float rnd = ota_rnd(2.5f, 5.0f);
                ota_ball_aim(b, 15.0f, throw_ang + (ota.ec % 2 == ota.hits % 2 ? rnd : -rnd));
                ota.ec++;
                printf("ota king: coconut thrown" "\n");
                break;
            }
        }
        if (ota.t >= 171) {
            if (ota_valid_bubble()) ota_set(OTA_BUBBLEATTACK);
            else if (ota.hits > 0) ota_set(ota_valid_fire() ? OTA_THROWFIRE : OTA_THROWFIREWAIT);
            else ota_set(ota.ec >= 2 ? OTA_THROWFIREWAIT : OTA_THROWCOCOWAIT);
        }
        break;
    case OTA_THROWFIREWAIT:
        if (ota.t > 60 && ota_valid_fire()) ota_set(OTA_THROWFIRE);
        break;
    case OTA_THROWFIRE:
        if (ota.t == 94) { /* throwFireBall: three, 0 / -30 / +30 */
            V3 tp = ota_joint(11, v3(-50.0f, 0, 0));
            for (int i = 0, n = 0; i < OTA_FIRES && n < 3; i++) if (!ota.fire[i].alive) {
                float off = n == 0 ? 0.0f : (n % 2 == 0 ? 30.0f : -30.0f) * ((n + 1) / 2);
                V3 up = up_at(tp, ota.up);
                ota.fire[i].pos = tp; ota.fire[i].up = up; ota.fire[i].alive = 1;
                ota.fire[i].vel = ota_rot(vmul(vnorm(vsub(vadd(mario->pos, vmul(up, 120.0f)), tp)), 15.0f), up, throw_ang + off);
                n++;
            }
        }
        if (ota.t >= 155) ota_set(ota_valid_bubble() ? OTA_BUBBLEATTACK : ota.hits > 0 && ota_valid_coco() ? OTA_THROWCOCO : OTA_WAIT);
        break;
    case OTA_DAMAGE: case OTA_POWERUP:
        if (ota.t >= (ota.nrv == OTA_POWERUP ? 256 : 179)) {
            if (ota.nrv == OTA_POWERUP && r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
            ota_set(ota_valid_bubble() ? OTA_BUBBLEATTACK : ota_valid_fire() ? OTA_THROWFIRE : OTA_THROWFIREWAIT);
        }
        break;
    case OTA_DOWN:
        if (ota.t == 1) {
            for (int b = 0; b < OTA_BALLS; b++) ota.ball[b].state = 0;
            for (int i = 0; i < OTA_FIRES; i++) ota.fire[i].alive = 0;
            for (int i = 0; i < OTA_BUBBLES; i++) ota.bub[i].state = 0;
        }
        if (ota.t >= 3) { ota_set(OTA_DOWNDEMO); ota.yaw = 43.0f; row_kill(ota.row); /* requestAppearPowerStar */ }
        break;
    case OTA_DOWNDEMO:
        ota.yaw = 43.0f; /* cDownDemoRotate */
        if (ota.t >= 300) { ota_set(OTA_APPEARSTAR); printf("ota king: down, its star" "\n"); }
        break;
    case OTA_APPEARSTAR:
        if (ota.t >= 60) ota_set(OTA_DEAD);
        break;
    case OTA_HITBACK: case OTA_NEARATTACK:
        if (ota.t >= 103) {
            if (ota_valid_bubble()) ota_set(OTA_BUBBLEATTACK);
            else if (ota.nrv == OTA_NEARATTACK) ota_set(OTA_WAIT);
            else if (ota.hits > 0) ota_set(ota_valid_fire() ? OTA_THROWFIRE : OTA_THROWFIREWAIT);
            else ota_set(ota.ec >= 2 ? OTA_THROWFIREWAIT : OTA_THROWCOCOWAIT);
        }
        break;
    case OTA_BUBBLEATTACK:
        if (ota.t == 100) { /* appearBubble: six round it from mBubbleAppearPos (0, 10, 500) */
            float m[16];
            basis_matrix(m, ota.pos, ota.up, ota.f, 0);
            V3 bp = ota_world(m, v3(0, 10.0f, 500.0f)), d0 = vnorm(vtangent(vsub(bp, ota.pos), ota.up));
            for (int i = 0, n = 0; i < OTA_BUBBLES; i++) if (!ota.bub[i].state) {
                V3 fr = ota_rot(d0, ota.up, (n + 0.5f) * 60.0f + ota_rnd(-30.0f, 30.0f));
                ota.bub[i].pos = bp; ota.bub[i].front = fr; ota.bub[i].state = 1; ota.bub[i].t = 0;
                ota.bub[i].vel = vadd(vmul(ota.up, 37.5f), vmul(fr, 15.0f)); /* cBubbleAppearVelocity */
                n++;
            }
        }
        if (ota.t >= 189) ota_set(OTA_WAIT);
        break;
    default: break;
    }
    mario_hidden = (ota.nrv == OTA_APPEARDEMO || ota.nrv == OTA_DOWNDEMO) && ota.t >= 1;
    /* attackSensor: body / face touching Mario: NearAttack in the attacking nerves, else a push */
    float db = vlen(vsub(c, body)), df = vlen(vsub(c, face));
    if ((db < 400.0f + HIT_BODY_R || df < 400.0f + HIT_BODY_R) && ota.nrv != OTA_DOWNDEMO && ota.nrv != OTA_APPEARSTAR && ota.nrv != OTA_APPEARDEMO) {
        int near_ok = ota.nrv == OTA_WAIT || ota.nrv == OTA_THROWCOCOWAIT || ota.nrv == OTA_THROWCOCO || ota.nrv == OTA_THROWFIREWAIT ||
                      ota.nrv == OTA_THROWFIRE || ota.nrv == OTA_BUBBLEATTACK;
        if (near_ok && !mario->hurt_timer) { hurt_mario(body, 30.0f); ota_set(OTA_NEARATTACK); }
        V3 out = vtangent(vsub(c, body), ota.up);
        if (vlen(out) > 1e-3f && db < 400.0f + HIT_BODY_R) mario->pos = vadd(mario->pos, vmul(vnorm(out), 400.0f + HIT_BODY_R - db));
    }
    update_ota_balls(c, body, face);
    update_ota_fire(c);
    ota_pose();
    { /* its demos' cameras (startAnimCameraTargetSelf "Appear" / "Down") */
        static int was;
        float m[16];
        basis_matrix(m, ota.pos, ota.up, ota.f, 0);
        const char *cn = ota.nrv == OTA_APPEARDEMO ? "otaking/appear" : ota.nrv == OTA_DOWNDEMO ? "otaking/down" : NULL;
        int on = cn && demo_camera(cn, ota.t, m);
        if (was && !on) camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45);
        was = on;
        ota_demo_on = on;
    }
}

/* its clip (startBckWithFrontFoot: the feet the same with R / L; startBckWaitIfNotPlaying) */
static void ota_pose(void)
{
    static const char *const clip[] = {"appear", "appear", "wait", "wait", "attack", "wait", "fireattack", "damage",
                                       "crowndamage", "down", "down", "down", "down", "hitbackstart", "hitback", "hitback",
                                       "bubbleattack"};
    const char *c = clip[ota.nrv];
    int loop = !strcmp(c, "wait");
    const Model *mds[3] = {m_ota, m_otafoot, m_otafoot};
    for (int k = 0; k < 3; k++) {
        Anim *a = k ? ota.foot[k - 1] : ota.anim;
        if (!a || !mds[k] || !mds[k]->skin) continue;
        if (a->skin != mds[k]->skin) anim_bind(a, mds[k]->skin);
        char n[48];
        snprintf(n, sizeof n, k ? "otakingfoot/%s%s" : "otaking/%s%s", c, k == 1 ? "r" : k == 2 ? "l" : "");
        anim_play(a, n, ota.nrv == OTA_WAITONSWITCH ? 0.0f : 1.0f, loop, 8);
        anim_update(a);
    }
}


/* CocoSambo (Petari Enemy/CocoSambo.cpp: CocoSambo and CocoSamboHead): HideWait underground until Mario is within
 * mPlayerSearchDistance (arg0, 1800), Appear (its clip, turning to him), Wait (1 degree a frame to him): within 700
 * Attack, then AttackInterval (60), beyond the distance + 100 Hide. Its spines' sensors (Spine1-3, (40, 0, 0), 80 x
 * scale) and its head's (Head, (60, 0, 0), 100 / trample 150 x scale) hurt Mario (EnemyAttackStrong) unless it is
 * down. A coconut (or walker) on it in Appear / Wait / Attack / AttackInterval (tryToFallHead): Hit, its head falls
 * (CocoSamboHead Fall: to its base in cFallFrame 45 under twice the gravity, Land, Swoon cSwoonFrame 180, SwoonEnd,
 * back on the body) while the body hides (FallHeadHide, RecoverWait 90, Recover, Appear). On the swooning head (Land,
 * Swoon, SwoonEnd's first 60) a stomp / hip drop presses it (PressY, cPressFrame 35) and a spin blows it away (Blow:
 * (0, 20, -100) in its frame, cBlowFrame 20): dead, its dead switch. ponytail: no star pointer (DpdPointing), no anim
 * scale reactions, no needle material hiding, no effects */
static void sambo_set(int k, int n) { sambos[k].nrv = n; sambos[k].t = 0; }
static void sambo_hset(int k, int n) { sambos[k].hnrv = n; sambos[k].ht = 0; }
static int sambo_dying(int k) { return sambos[k].nrv == CS_PRESSY || sambos[k].nrv == CS_BLOW; }
static int sambo_down(int k) { return sambos[k].nrv == CS_FALLHEAD || sambos[k].nrv == CS_FALLHEADHIDE || sambos[k].nrv == CS_RECOVERWAIT; }
static int sambo_swoon(int k) /* CocoSamboHead::isNerveSwoon */
{
    return sambos[k].hnrv == CH_FALLLAND || sambos[k].hnrv == CH_SWOON || (sambos[k].hnrv == CH_SWOONEND && sambos[k].ht < 60);
}
static int sambo_of(int row) { for (int k = 0; k < nsambos; k++) if (sambos[k].row == row) return k; return -1; }
static V3 sambo_hpos(int k) { return sambos[k].hpos; }
static void sambo_body_mtx(int k, float m[16]) /* calcAndSetBaseMtx: up / front at its place, its scale */
{
    basis_matrix(m, sambos[k].pos, sambos[k].up, sambos[k].front, 0);
    for (int j = 0; j < 12; j++) m[j] *= sambos[k].s;
}
/* a body joint's point (+ local offset), its head's sensor centre */
static V3 sambo_joint(int k, const Inst *in, int j, V3 off)
{
    float m[16];
    sambo_body_mtx(k, m);
    if (in && in->anim && (u32)j < in->anim->skin->njoints) {
        float jm[16];
        mat_mul(jm, m, in->anim->world[j]);
        return v3(jm[12] + jm[0] * off.x, jm[13] + jm[1] * off.x, jm[14] + jm[2] * off.x);
    }
    return vadd(sambos[k].pos, vmul(sambos[k].up, (100.0f + 120.0f * j) * sambos[k].s));
}
static V3 sambo_head_pos(int k, const Inst *in)
{
    return sambos[k].hnrv == CH_CONNECTED ? sambo_joint(k, in, 3, v3(60.0f * sambos[k].s, 0, 0)) : vadd(sambos[k].hpos, vmul(sambos[k].up, 60.0f * sambos[k].s));
}
static int sambo_try_fall(int k) /* tryToFallHead */
{
    int n = sambos[k].nrv;
    if (n != CS_APPEAR && n != CS_WAIT && n != CS_ATTACK && n != CS_ATTACKINT) return 0;
    sambo_set(k, CS_FALLHEAD);
    return 1;
}
static void sambo_turn(int k, float deg) /* dirToPlayer */
{
    V3 to = vtangent(vsub(mario->pos, sambos[k].pos), sambos[k].up);
    if (vlen(to) < 1e-3f) return;
    to = vnorm(to);
    V3 side = vcross(sambos[k].up, sambos[k].front);
    float a = atan2f(vdot(to, side), vdot(to, sambos[k].front)), mx = deg * 0.0174533f;
    a = a > mx ? mx : a < -mx ? -mx : a;
    sambos[k].front = vnorm(vadd(vmul(sambos[k].front, cosf(a)), vmul(side, sinf(a))));
}
static void update_sambos(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->sambo) continue;
        int k = sambo_of(in->row);
        const Row *r = &sc->objs[in->row];
        if (k < 0) { /* init: HideWait */
            if (nsambos == MAX_SAMBOS) continue;
            k = nsambos++;
            memset(&sambos[k], 0, sizeof sambos[k]);
            V3 x, z;
            row_axes(r, &x, &z);
            sambos[k].row = in->row; sambos[k].pos = row_pos(r); sambos[k].up = vnorm(vcross(z, x)); sambos[k].front = z;
            sambos[k].s = r->scale[0];
            sambos[k].head = mem_alloc(MEM_OBJECTS, sizeof(Anim)); memset(sambos[k].head, 0, sizeof(Anim));
        }
        typeof(&sambos[0]) q = &sambos[k];
        if (q->nrv == CS_DEAD) continue;
        q->t++; q->ht++;
        float search = r->arg[0] > 0 ? (float)r->arg[0] : 1800.0f, dm = vlen(vsub(mario->pos, q->pos));
        const char *body = NULL, *head = NULL;
        int bloop = 0;
        switch (q->nrv) {
        case CS_HIDEWAIT: body = "hidewait"; bloop = 1; if (dm < search) sambo_set(k, CS_APPEAR); break;
        case CS_APPEAR: body = "appear"; if (q->t == 1) sambo_turn(k, 180.0f); sambo_turn(k, 1.0f); if (q->t >= 139) sambo_set(k, CS_WAIT); break;
        case CS_WAIT: body = "wait"; bloop = 1; sambo_turn(k, 1.0f);
            if (dm < 700.0f) sambo_set(k, CS_ATTACK); else if (dm >= search + 100.0f) sambo_set(k, CS_HIDE);
            break;
        case CS_HIDE: body = "hide"; if (q->t >= 99) sambo_set(k, CS_HIDEWAIT); break;
        case CS_ATTACK: body = "attack"; if (q->t < 30) sambo_turn(k, 1.0f); if (q->t >= 169) sambo_set(k, CS_ATTACKINT); break;
        case CS_ATTACKINT: body = "wait"; bloop = 1; sambo_turn(k, 1.0f); if (q->t >= 60) sambo_set(k, CS_WAIT); break;
        case CS_FALLHEAD: /* Hit; its head falls (HeadFall from its Head joint to the base) */
            body = "hit";
            if (q->t == 1) {
                q->hpos = sambo_joint(k, in, 3, v3(0, 0, 0));
                V3 off = vsub(q->pos, q->hpos), vo = vmul(q->up, vdot(off, q->up)), ho = vtangent(off, q->up);
                q->hvel = vadd(vmul(ho, 1.0f / 45.0f), vmul(q->up, (2.0f * vdot(vo, q->up) + 2.0f * 45.0f * 45.0f) / (2.0f * 45.0f)));
                V3 tf = vtangent(vsub(mario->pos, q->hpos), q->up);
                q->hfront = vlen(tf) > 1e-3f ? vnorm(tf) : q->front;
                sambo_hset(k, CH_FALL);
                printf("coco sambo row %d: its head falls" "\n", in->row);
            }
            if (q->t >= 80) sambo_set(k, CS_FALLHEADHIDE);
            break;
        case CS_FALLHEADHIDE: body = "hidewait"; bloop = 1; if (q->hnrv == CH_CONNECTED) sambo_set(k, CS_RECOVERWAIT); break;
        case CS_RECOVERWAIT: body = "recover"; if (q->t >= 90) sambo_set(k, CS_RECOVER); break;
        case CS_RECOVER: body = "recover"; head = "recover"; if (q->t == 1) sambo_turn(k, 180.0f); if (q->t >= 89) sambo_set(k, CS_APPEAR); break;
        case CS_PRESSY: head = "pressy"; if (q->t >= 35) { sambo_set(k, CS_DEAD); row_kill(in->row); } break;
        case CS_BLOW: if (q->t >= 20) { sambo_set(k, CS_DEAD); row_kill(in->row); } break;
        }
        switch (q->hnrv) { /* CocoSamboHead */
        case CH_FALL:
            head = "fall";
            q->hvel = vadd(q->hvel, vmul(q->up, -2.0f)); /* twice the gravity */
            q->hpos = vadd(q->hpos, q->hvel);
            if (q->ht >= 45) { q->hpos = q->pos; q->hvel = v3(0, 0, 0); sambo_hset(k, CH_FALLLAND); }
            break;
        case CH_FALLLAND: head = "land"; if (q->ht >= 14) sambo_hset(k, CH_SWOON); break;
        case CH_SWOON: head = "swoon"; if (q->ht >= 180) sambo_hset(k, CH_SWOONEND); break;
        case CH_SWOONEND: head = "swoonend"; if (q->ht >= 109) sambo_hset(k, CH_CONNECTED); break;
        case CH_BLOW: head = "blow"; q->hvel = vadd(q->hvel, vmul(q->up, -1.0f)); q->hpos = vadd(q->hpos, q->hvel); break;
        default: break;
        }
        /* the swooning head: a stomp / hip drop presses it, a spin blows it away */
        if (sambo_swoon(k) && !sambo_dying(k)) {
            V3 hc = vadd(q->hpos, vmul(q->up, 60.0f * q->s)), rel = vsub(c, hc);
            float hh = vdot(vsub(mario->pos, hc), q->up), side = vlen(vtangent(rel, q->up));
            if (mario->vup < 0 && hh > 0 && side < 150.0f * q->s + HIT_BODY_R && hh < 150.0f * q->s + 100.0f) { /* trample / hip drop */
                mario->vup = 15.0f; mario->grounded = 0;
                sambo_set(k, CS_PRESSY);
                printf("coco sambo row %d pressed" "\n", in->row);
            } else if (mario->spin_timer > 0 && vlen(rel) < 100.0f * q->s + 150.0f) { /* isMsgPlayerHitAll */
                V3 tf = vtangent(vsub(mario->pos, q->hpos), q->up);
                q->hfront = vlen(tf) > 1e-3f ? vnorm(tf) : q->hfront;
                q->hvel = vadd(vmul(q->up, 20.0f), vmul(q->hfront, -100.0f)); /* cBlowVelocity in its frame */
                sambo_hset(k, CH_BLOW);
                sambo_set(k, CS_BLOW);
                printf("coco sambo row %d blown away" "\n", in->row);
            }
        }
        /* its sensors on Mario: EnemyAttackStrong and a push unless it is down or dying */
        if (!sambo_dying(k) && !sambo_down(k) && q->nrv != CS_HIDEWAIT) {
            for (int j = 0; j < 4; j++) {
                V3 sp = j < 3 ? sambo_joint(k, in, j, v3(40.0f * q->s, 0, 0)) : sambo_head_pos(k, in);
                float rad = (j < 3 ? 80.0f : 100.0f) * q->s, d = vlen(vsub(c, sp));
                if (d < rad + HIT_BODY_R) {
                    hurt_mario(sp, 20.0f);
                    V3 out = vtangent(vsub(c, sp), q->up);
                    if (vlen(out) > 1e-3f) mario->pos = vadd(mario->pos, vmul(vnorm(out), rad + HIT_BODY_R - d));
                    break;
                }
            }
        }
        if (body) obj_play(in, body, bloop, 4);
        if (q->hnrv == CH_CONNECTED && !head) head = body; /* startBckThisAndHead */
        if (head && m_sambohead && m_sambohead->skin) {
            if (q->head->skin != m_sambohead->skin) anim_bind(q->head, m_sambohead->skin);
            char n[48];
            snprintf(n, sizeof n, "cocosambohead/%s", head);
            anim_play(q->head, n, (q->nrv == CS_RECOVERWAIT && q->hnrv == CH_CONNECTED) ? 0.0f : 1.0f, bloop, 4);
            anim_update(q->head);
        }
        float m[16];
        sambo_body_mtx(k, m);
        memcpy(in->m, m, 64);
    }
}

static void draw_sambo_heads(void)
{
    for (int k = 0; m_sambohead && k < nsambos; k++) {
        if (sambos[k].nrv == CS_DEAD) continue;
        const Inst *in = NULL;
        for (int i = 0; i < ninsts; i++) if (insts[i].row == sambos[k].row) in = &insts[i];
        if (!in) continue;
        float m[16];
        if (sambos[k].hnrv == CH_CONNECTED && in->anim) { /* initFixedPosition on Head, rotated (0, -90, -90) */
            float bm[16], jm[16], r[16];
            sambo_body_mtx(k, bm);
            mat_mul(jm, bm, in->anim->world[3]);
            mat_identity(r); /* (0, -90, -90): x -> -z ... y -> x, z -> -y */
            r[0] = 0; r[1] = 0; r[2] = -1; r[4] = 1; r[5] = 0; r[6] = 0; r[8] = 0; r[9] = -1; r[10] = 0;
            mat_mul(m, jm, r);
        } else {
            basis_matrix(m, sambos[k].hpos, sambos[k].up, sambos[k].hfront, 0);
            for (int j = 0; j < 12; j++) m[j] *= sambos[k].s;
        }
        if (m_sambohead->skin && sambos[k].head->skin == m_sambohead->skin) render_skinned(m_sambohead, m, (const float (*)[16])sambos[k].head->drw);
    }
}

/* TicoFat (Petari NPC/TicoFat.cpp), a hungry Luma: Prep until it may eat (startEat: its talk's event, or arg2 set),
 * then Wait / Point (Excite) while the pointer is on it near Mario (updateGuidance _174 8 within its talk distance
 * 280 x min(scale, 1.5), updatePointing _178 4); B (L1 here) held while pointing gifts a star piece every _1F0 5
 * frames (giftStarPieceToTarget: one flies to its Mouth), each eaten (receiveStarPiece) makes it Eat (EatStart then
 * Eat) and grow (calcScale 1 + 0.9 x eaten / arg1, default 10); none left in flight: Chem, then Point / Wait. All
 * eaten: Test, Fullness (Joy), tryMetamorphosis: arg0 -1 Demo (its Demo clip, hidden at 90), Fly along its rail
 * (SpinDriverShootPath, eased, _1FC = arg4 (100) a frame), WipeOut / WipeIn (60 each: SW_A on, it is gone), Info,
 * After (60): disappear (its group's members appear); else Meta. Clips by its size (getActionName: ..0 / ..1 / ..2
 * past 1.3 / 1.6) through its ActorAnimCtrl actions. ponytail: no talk messages (the talk's end taken at once), no
 * fullness meter, no "変身" / "飛行" multi-actor cameras, no white wipes or information message (their frames only),
 * no TicoSeedGuidance, no item (arg0 0 / 1), no FatRed BRK, no BVA eyes */
#define MAX_FATS 4
static struct {
    int row, nrv, t, left, total, inflight, eat, g174, g178, item, has_rail;
    float scale, flyd, flylen, m0[16]; /* m0: its matrix without its scale (setScale works on it) */
    Launch fly;
    struct { V3 pos; int on; } gift[8];
} fats[MAX_FATS];
enum { TF_PREP, TF_WAIT, TF_POINT, TF_EAT, TF_CHEM, TF_TEST, TF_FULLNESS, TF_DEMO, TF_FLY, TF_WIPEOUT, TF_WIPEIN, TF_INFO,
       TF_AFTER, TF_META, TF_GONE };
static const char *fat_size(const char *base, float scale) /* getActionName */
{
    static char buf[24];
    snprintf(buf, sizeof buf, "%s%d", base, scale > 1.6f ? 2 : scale > 1.3f ? 1 : 0);
    return buf;
}
static void fat_set(int k, int n) { fats[k].nrv = n; fats[k].t = 0; }
static void update_ticofat(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->fat) continue;
        int k = 0;
        while (k < nfats && fats[k].row != in->row) k++;
        const Row *r = &sc->objs[in->row];
        if (k == nfats) { /* TicoFat::init */
            if (nfats == MAX_FATS) continue;
            memset(&fats[k], 0, sizeof fats[k]);
            fats[k].row = in->row; nfats++;
            fats[k].total = fats[k].left = r->arg[1] > 0 ? r->arg[1] : 10;
            fats[k].item = r->arg[0];
            fats[k].eat = r->arg[2] != -1;
            fats[k].scale = 1.0f;
            memcpy(fats[k].m0, in->m, 64);
            u32 np = 0; int closed;
            const float *pts = r->path >= 0 ? find_rail(scene_blob_g, r->zone, r->path, &np, &closed) : NULL;
            if (pts && np >= 2) { /* SpinDriverShootPath::initUsingParabolic from its place */
                u32 nraw = 0;
                find_rail_of(scene_blob_g, r->zone, r->path, &nraw, &closed, 1);
                init_launch(&fats[k].fly, r, pts, np, nraw <= 2);
                fats[k].has_rail = 1;
                V3 prev = launch_pos(&fats[k].fly, 0.0f);
                for (int s = 1; s <= 32; s++) { V3 q = launch_pos(&fats[k].fly, s / 32.0f); fats[k].flylen += vlen(vsub(q, prev)); prev = q; }
            }
        }
        typeof(&fats[0]) f = &fats[k];
        if (f->nrv == TF_GONE) continue;
        f->t++;
        V3 up = vnorm(v3(f->m0[4], f->m0[5], f->m0[6])), base = v3(f->m0[12], f->m0[13], f->m0[14]);
        V3 front = vnorm(v3(f->m0[8], f->m0[9], f->m0[10]));
        V3 mid = vadd(base, vmul(up, 100.0f * f->scale)), mouth = vadd(base, vadd(vmul(up, 130.0f * f->scale), vmul(front, 60.0f * f->scale)));
        /* TalkMessageCtrl::isNearPlayer: within the talk distance across the gravity and along it, from his centre */
        V3 rc = vsub(base, vadd(mario->pos, vmul(mario->up, HIT_BODY_H)));
        float talk = 280.0f * fminf(f->scale, 1.5f), dm = fabsf(vdot(rc, up)) <= talk ? vlen(vtangent(rc, up)) : 1e9f;
        /* updateGuidance / updatePointing (the pointer: the cursor's ray within 110 x scale of it) */
        if (dm < talk) f->g174 = 8; else if (f->g174 > 0) f->g174--;
        int pointing = 0;
        if (f->g174 > 0) {
            V3 o, d;
            cursor_ray(&o, &d);
            V3 to = vsub(mid, o);
            float z = vdot(to, d);
            pointing = (z > 0 && vlen(vsub(to, vmul(d, z))) < 110.0f * f->scale) || test_feed;
        }
        if (pointing) f->g178 = 4; else if (f->g178 > 0) f->g178--;
        int feeding = f->eat && f->left > 0 && f->nrv <= TF_CHEM;
        if (feeding && f->g178 > 0 && (pad_held(PAD_L1) || test_feed) && f->t % 5 == 0 && got_pieces > 0) /* shootStarPiece */
            for (int g = 0; g < 8; g++) if (!f->gift[g].on) {
                f->gift[g].on = 1; f->gift[g].pos = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
                got_pieces--; f->inflight++;
                break;
            }
        for (int g = 0; g < 8; g++) { /* the gifted pieces, flying to its Mouth */
            if (!f->gift[g].on) continue;
            V3 to = vsub(mouth, f->gift[g].pos);
            if (vlen(to) < 40.0f) { /* receiveStarPiece(1) */
                f->gift[g].on = 0; f->inflight--;
                if (f->left > 0) f->left--;
                f->scale = 1.0f + (float)(f->total - f->left) / f->total * 0.9f;
                if (f->nrv != TF_EAT) fat_set(k, TF_EAT);
                continue;
            }
            f->gift[g].pos = vadd(f->gift[g].pos, vmul(vnorm(to), fminf(40.0f, vlen(to))));
        }
        const char *clip = NULL;
        int loop = 1;
        switch (f->nrv) {
        case TF_PREP: /* the talk (onRootNodeAutomatic, in front) starts the eating at its end */
            clip = fat_size("wait", f->scale);
            if (!f->eat && dm < talk && vdot(vsub(mario->pos, base), front) > 0) { f->eat = 1; printf("tico fat row %d: eats" "\n", in->row); }
            if (f->eat) fat_set(k, TF_WAIT);
            break;
        case TF_WAIT: clip = fat_size("wait", f->scale); if (f->g178 > 0) fat_set(k, TF_POINT); break;
        case TF_POINT: clip = fat_size("excite", f->scale); if (f->g178 <= 0) fat_set(k, TF_WAIT); break;
        case TF_EAT: clip = f->t < 41 ? fat_size("eatstart", f->scale) : fat_size("eat", f->scale); if (!f->inflight) fat_set(k, TF_CHEM); break;
        case TF_CHEM:
            clip = "chem";
            if (f->t >= 76) { if (f->g178 > 0) { f->g174 = 8; fat_set(k, TF_POINT); } else { f->g174 = 0; fat_set(k, TF_WAIT); } }
            break;
        case TF_TEST: clip = fat_size("excite", f->scale); fat_set(k, TF_FULLNESS); break;
        case TF_FULLNESS: /* Joy; its talk at its end (tryTalkForceWithoutDemoAtEnd) */
            clip = fat_size("joy", f->scale);
            if (f->t >= 41) fat_set(k, f->item == -1 ? TF_DEMO : TF_META);
            break;
        case TF_DEMO: clip = "demo"; loop = 0;
            if (f->t >= 96) fat_set(k, f->item == -1 && f->has_rail ? TF_FLY : TF_WIPEOUT);
            break;
        case TF_FLY: { /* exeFly */
            clip = "fly";
            f->flyd += 100.0f;
            float v = f->flylen > 0 ? fminf(f->flyd / f->flylen, 1.0f) : 1.0f, e;
            if (v < 0.5f) { float x = 2 * v * 2 * v; e = 0.5f * x; } /* getEaseInValue(x, 0, 0.5, 1) */
            else { float x = 2 * (1 - v); x *= x; e = 1.0f - 0.5f * x; }
            V3 p = launch_pos(&f->fly, e), dir = vsub(launch_pos(&f->fly, fminf(e + 0.01f, 1.0f)), p);
            f->m0[12] = p.x; f->m0[13] = p.y; f->m0[14] = p.z;
            if (vlen(dir) > 1e-3f) { V3 fz = vnorm(dir); f->m0[8] = fz.x; f->m0[9] = fz.y; f->m0[10] = fz.z; }
            if (e >= 1.0f) fat_set(k, TF_WIPEOUT);
            break;
        }
        case TF_WIPEOUT: clip = "fly"; if (f->t >= 60) fat_set(k, TF_WIPEIN); break;
        case TF_WIPEIN:
            if (f->t == 60 && r->sw[SW_A] >= 0) { sw_set(r, SW_A, 1); printf("tico fat row %d: its planet" "\n", in->row); }
            if (f->t >= 120) fat_set(k, TF_INFO);
            break;
        case TF_INFO: fat_set(k, TF_AFTER); break;
        case TF_AFTER:
            if (f->t >= 60) { row_kill(in->row); sw_changed = 1; fat_set(k, TF_GONE); } /* disappear: its group appears */
            break;
        case TF_META: clip = "metamorphosis"; loop = 0;
            if (f->t >= 80) { row_kill(in->row); sw_changed = 1; fat_set(k, TF_GONE); }
            break;
        }
        if (f->eat && f->left == 0 && f->nrv <= TF_CHEM && !f->inflight) { fat_set(k, TF_TEST); printf("tico fat row %d: full (%d)" "\n", in->row, f->total); }
        if (clip) obj_play(in, clip, loop, 6);
        /* setScale; hidden from Demo's frame 90 (hideModelAndOnCalcAnim) */
        float sc_ = (f->nrv == TF_DEMO && f->t >= 90) || f->nrv >= TF_WIPEIN ? 0.0f : f->scale;
        memcpy(in->m, f->m0, 64);
        for (int j = 0; j < 12; j++) in->m[j] *= sc_;
    }
}
static int fat_state(int row)
{
    for (int k = 0; k < nfats; k++) if (fats[k].row == row) return (fats[k].total - fats[k].left) * 1000 + fats[k].nrv;
    return 0;
}
static int fat_demo(void) /* Test .. After: Mario waits (its demo) */
{
    for (int k = 0; k < nfats; k++) if (fats[k].nrv >= TF_TEST && fats[k].nrv <= TF_AFTER) return 1;
    return 0;
}

static void update_fountains(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->fountain) continue;
        int row = in->row, t = row_step[row] = (row_step[row] + 1) % 420;
        float k = t < 230 ? 0 : t < 250 ? (t - 230) / 20.0f : t < 410 ? 1 : 1.0f - (t - 410) / 10.0f;
        memcpy(in->m, in->m0, 64);
        for (int j = 4; j < 7; j++) in->m[j] *= k > 0.01f ? k : 0.01f;
        if (k <= 0) in->m[12] = 1e9f; /* hideModel */
        int near = vlen(vsub(v3(in->m0[12], in->m0[13], in->m0[14]), cam_pos)) < 3000.0f; /* effects for the near ones only */
        if (near && t >= 120 && t < 200) fx_keep(FX_FOUNTAINBIG_FOUNTAINBIGSIGN, &sc->objs[row], in->m0); /* Sign */
        if (near && t >= 230 && t < 410) fx_keep(FX_FOUNTAINBIG_FOUNTAINBIG, (const char *)&sc->objs[row] + 1, in->m0); /* Spout */
        if (row_turn[row] && mario->grounded) row_turn[row] = 0;
        if (t < 230 || t >= 410 || row_turn[row]) continue;
        V3 up = vnorm(v3(in->m0[4], in->m0[5], in->m0[6])), base = v3(in->m0[12], in->m0[13], in->m0[14]);
        V3 rel = vsub(mario->pos, base);
        float along = vdot(rel, up);
        if (along < -50.0f || along > 600.0f * k || vlen(vtangent(rel, up)) > 60.0f + HIT_BODY_R * 0.5f) continue;
        mario->vup = 44.0f; mario->hvel = vmul(mario->face, 2.0f); mario->grounded = 0; mario->jump_kind = 0; mario->hip = 0;
        row_turn[row] = 1;
        printf("fountain row %d throws Mario up" "\n", row);
    }
}

static void update_candles(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->candle) continue;
        int row = in->row;
        const Row *r = &sc->objs[row];
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), base = v3(in->m[12], in->m[13], in->m[14]);
        float s = in->s, h = in->candle == 2 ? 300.0f : 220.0f, rad = (in->candle == 1 ? 50.0f : in->candle == 2 ? 70.0f : 150.0f) * s;
        if (in->candle == 3 && row_turn[row] == 0 && !row_step[row] && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) row_turn[row] = 3; /* WaitFire */
        if (row_turn[row] == 3 || row_turn[row] == 4 || row_turn[row] == 5) {
            if (row_turn[row] == 4) {
                fx_keep(FX_CANDLESTANDICEVOLCANO_FIRE, r, in->m);
                if (++row_step[row] >= 60) { row_turn[row] = 0; if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
                    printf("candlestand row %d lit\n", row); }
            } else if (row_turn[row] == 5 && ++row_step[row] >= 30) row_turn[row] = 0;
            continue;
        }
        if (row_turn[row] == 0) {
            if (in->candle == 3) fx_keep(FX_CANDLESTANDICEVOLCANO_FIRE, r, in->m);
            else if (in->candle == 1) fx_keep(FX_PHANTOMCANDLESTAND_FIRE, r, in->m);
            else { fx_keep(FX_TERESAMANSIONCANDLESTAND_FIRE01, r, in->m); fx_keep(FX_TERESAMANSIONCANDLESTAND_FIRE02, (const char *)r + 1, in->m); }
            V3 flame = vadd(base, vmul(up, h * s));
            if (vlen(vsub(c, flame)) < rad + HIT_BODY_R && !mario->hurt_timer) { /* sendMsgEnemyAttackFire */
                hurt_mario(flame, 20.0f);
                printf("candlestand row %d burns Mario" "\n", row);
            }
            if (mario->spin_timer > 0 && vlen(vsub(c, vadd(base, vmul(up, 100.0f * s)))) < 350.0f * s) { /* isInSpinStormRange */
                if (in->candle == 3) { row_turn[row] = 5; row_step[row] = 0; continue; } /* Flicker */
                row_turn[row] = 1; row_step[row] = 0;
                fx_emit(in->candle == 1 ? FX_PHANTOMCANDLESTAND_EXTINGUISH : FX_TERESAMANSIONCANDLESTAND_EXTINGUISH01, in->m);
                printf("candlestand row %d: put out" "\n", row);
            }
        } else if (row_turn[row] == 1 && ++row_step[row] >= 15) { /* appearItem */
            V3 at = vadd(base, vmul(up, in->candle == 1 ? 0.0f : 220.0f * s));
            if (r->arg[0] == -1) pop_coins(at, up, 1); else if (r->arg[0] == 0) drop_pieces(at, up, 1);
            row_turn[row] = 2;
        }
    }
}

/* Fire Mario (MarioActorMorph: playerMode 2 for mFireModeTime 1200 frames from a MorphItemNeoFire) and his balls
 * (MarioActorParts.cpp shootFireBall, FireMarioBall.cpp): each spin throws one of three from his hand along front + up
 * at cThrowSpeed 30; gravity cGravityAcc 2, the ground and roofs bound it (x cBoundReduction 0.9); a wall touched 3
 * frames, 3000 from Mario or a hit puts it out. A hit (ACTMES_FIREBALL_ATTACK, sensor 80) kills walkers (a coin;
 * a Unizo: 6 star pieces), melts SnowMen (update_snowmen). ponytail: Mario keeps his colours (no fire textures),
 * no fire throw / spin animations, no BGM_FIRE */
static void fireball_hit_walkers(int b)
{
    for (int k = 0; k < nkuribo; k++) {
        Kuribo *e = &kuribo[k];
        if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY ||
            vlen(vsub(fireballs[b].pos, vadd(e->pos, vmul(e->up, 60.0f)))) > 80.0f + 60.0f) continue;
        e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up);
        if (e->kind == WALK_UNIZO) drop_pieces(e->pos, e->up, 6); else pop_coins(e->pos, e->up, 1);
        printf("fire ball burns walker row %d" "\n", e->row);
        fireballs[b].t = 0;
        return;
    }
}

static int fireball_hits_snowman(V3 p); /* update_snowmen */
static int fireball_lights_candle(V3 p) /* Candlestand::receiveMsgPlayerAttack: a WaitFire one within 150 x scale */
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (in->candle != 3 || row_turn[in->row] != 3) continue;
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), flame = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(up, 220.0f * in->s));
        if (vlen(vsub(p, flame)) > 150.0f * in->s + 80.0f) continue;
        row_turn[in->row] = 4; row_step[in->row] = 1;
        fx_emit(FX_CANDLESTANDICEVOLCANO_IGNITION, in->m);
        return 1;
    }
    return 0;
}

static void update_fire(void)
{
    if (fire_time > 0) {
        if (--fire_time == 0) printf("fire mario over" "\n");
        static int prev_spin;
        int spin_start = mario->spin_timer > prev_spin; /* a spin begins */
        prev_spin = mario->spin_timer;
        if (spin_start) /* a spin: a ball if one is free */
            for (int b = 0; b < MAX_FIREBALLS; b++)
                if (!fireballs[b].t) {
                    V3 dir = vnorm(vadd(mario->face, mario->up));
                    fireballs[b].pos = vadd(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), vmul(mario->face, 30.0f));
                    fireballs[b].vel = vmul(dir, 30.0f);
                    fireballs[b].t = 1; fireballs[b].wall = 0;
                    printf("fire ball thrown" "\n");
                    break;
                }
    }
    for (int b = 0; b < MAX_FIREBALLS; b++) {
        if (!fireballs[b].t) continue;
        fireballs[b].t++;
        V3 up = up_at(fireballs[b].pos, mario->up), p = vadd(fireballs[b].pos, fireballs[b].vel), gn, wn;
        int wall = 0, ground = collide_n(&p, up, 50.0f, &gn, &wn, &wall);
        fireballs[b].pos = p;
        if (ground) { /* bound off the ground */
            float into = vdot(fireballs[b].vel, gn);
            if (into < 0) fireballs[b].vel = vsub(fireballs[b].vel, vmul(gn, into * (1.0f + 0.9f)));
        } else fireballs[b].vel = vsub(fireballs[b].vel, vmul(up, 2.0f));
        fireballs[b].wall = wall ? fireballs[b].wall + 1 : 0;
        if (fireballs[b].wall >= 3 || vlen(vsub(p, mario->pos)) > 3000.0f || fireballs[b].t > 600) { fx_at(FX_MARIOFIREBALL_BREAK, p, up); fireballs[b].t = 0; continue; }
        fireball_hit_walkers(b);
        if (fireballs[b].t && (fireball_hits_snowman(p) || fireball_lights_candle(p))) fireballs[b].t = 0;
        if (fireballs[b].t) { /* its Fire trail */
            float fm[16];
            basis_matrix(fm, p, up, fireballs[b].vel, 0);
            fx_keep(FX_MARIOFIREBALL_FIRE, &fireballs[b], fm);
        }
    }
}

static void update_screws(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->screw) continue;
        int row = in->row;
        V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), top = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(up, 150.0f * in->s));
        if (row_turn[row] == 0) {
            if (mario_ground == i && mario->spin_timer > 0) { row_turn[row] = 1; row_step[row] = 0;
                printf("screw switch row %d: screwing" "\n", row); }
            continue;
        }
        if (row_turn[row] != 1) continue;
        int len = in->screw == 1 ? 34 : 75;
        ++row_step[row];
        float k = row_step[row] < 3 ? row_step[row] / 3.0f : 1.0f; /* Adjust */
        V3 at = vsub(top, vmul(up, 1.0f)); /* just on it */
        mario->pos = vadd(vmul(mario->pos, 1.0f - k), vmul(at, k));
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        if (row_step[row] >= 3 + len) {
            row_turn[row] = 2;
            if (sc->objs[row].sw[SW_A] >= 0) sw_set(&sc->objs[row], SW_A, 1);
            printf("screw switch row %d: on" "\n", row);
        }
    }
}

static void update_jets(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->jet) continue;
        if (row_turn[in->row] > 0) row_turn[in->row]--;
        const Row *r = &sc->objs[in->row];
        int type = r->arg[1] + 1 >= 0 && r->arg[1] + 1 <= 2 ? r->arg[1] + 1 : 0;
        if (jet.inst == i && jet.phase == 1) { /* held: in front of him at his hands */
            in->head = vadd(vadd(mario->pos, vmul(mario->face, 70.0f)), vmul(mario->up, 60.0f));
            if (mario->spin_timer > 0) { /* thrown */
                in->hvel = vmul(mario->face, (const float[]){30, 20, 30}[type]);
                jet.phase = 2; jet.t = 0;
                printf("jet turtle row %d: thrown" "\n", in->row);
            }
            jet_place(in);
            continue;
        }
        if (jet.inst == i && jet.phase == 2) { /* Throwing */
            V3 p = vadd(in->head, in->hvel), up = up_at(p, mario->up), gn, wn;
            int wall = 0;
            collide_n(&p, up, 60.0f, &gn, &wn, &wall);
            in->head = p;
            int gone = wall || ++jet.t >= (const int[]){300, 480, 300}[type] ||
                       vlen(vsub(p, v3(in->m0[12], in->m0[13], in->m0[14]))) > 10000.0f;
            for (int k = 0; k < nkuribo; k++) { /* ACTMES_JET_TURTLE_ATTACK */
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY ||
                    vlen(vsub(p, vadd(e->pos, vmul(e->up, 60.0f)))) > 60.0f + 80.0f) continue;
                e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up);
                if (e->kind == WALK_UNIZO) drop_pieces(e->pos, e->up, 6); else pop_coins(e->pos, e->up, 1);
                printf("jet turtle row %d blows walker row %d" "\n", in->row, e->row);
            }
            if (gone) { jet.inst = -1; jet.phase = 0; jet_reset(in); continue; }
            jet_place(in);
            continue;
        }
        if (jet.inst < 0 && !row_turn[in->row] && vlen(vsub(c, vadd(in->head, vmul(mario->up, 50.0f)))) < 80.0f + HIT_BODY_R) {
            jet.inst = i; jet.phase = 1; jet.t = 0; /* TakenStart */
            printf("jet turtle row %d: taken" "\n", in->row);
        }
    }
}

static void fluff_place(Inst *in)
{
    memcpy(in->m, in->m0, 64);
    in->m[12] = in->head.x; in->m[13] = in->head.y; in->m[14] = in->head.z;
    if (row_turn[in->row] == 2) in->m[12] = 1e9f; /* gone */
}

static void update_fluffs(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->fluff || flf.inst == i) continue;
        int row = in->row;
        signed char *ph = &row_turn[row];
        unsigned short *t = &row_step[row];
        V3 up = up_at(in->head, v3(in->m0[4], in->m0[5], in->m0[6]));
        ++*t;
        if (*ph == 2) { /* FreeWaitInvalid, then FreeBloom at home */
            if (*t >= 120) { *ph = 0; *t = 0; in->head = v3(in->m0[12], in->m0[13], in->m0[14]); in->hvel = v3(0, 0, 0); }
            fluff_place(in);
            continue;
        }
        if (mario->spin_timer > 0 && vlen(vsub(c, in->head)) < 150.0f + 100.0f && (*ph == 0 || *t > 60)) {
            in->hvel = vmul(up, *ph == 0 ? 10.0f : 5.0f); /* receiveMsgPlayerSpinAttack */
            *ph = 1; *t = 0;
            printf("fluff row %d: up" "\n", row);
        }
        if (*ph == 1) { /* FreeWaitAir */
            in->hvel = vsub(in->hvel, vmul(up, 0.15f));
            float down = -vdot(in->hvel, up);
            if (down > 0.8f) in->hvel = vmul(vnorm(in->hvel), 0.8f);
            V3 p = vadd(in->head, in->hvel);
            if (vdot(in->hvel, up) < 0 && collide(&p, up, 50.0f)) { *ph = 2; *t = 0; /* isOnGround, coming down */ printf("fluff row %d: lands, gone" "\n", row); }
            in->head = p;
        }
        fluff_place(in);
    }
}

static int fluff_try(Player *m)
{
    if (flf.inst >= 0 || m->grounded || m->launch >= 0) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->fluff || row_turn[in->row] != 1 || vlen(vsub(c, in->head)) > 150.0f + HIT_BODY_R * 0.5f) continue;
        V3 up = up_at(in->head, m->up), v = vadd(m->hvel, vmul(m->up, m->vup));
        if (vdot(v, up) < 0) v = vtangent(v, up); /* no falling speed */
        in->hvel = v;
        flf.inst = i; flf.phase = 1; flf.t = 0; flf.spins = 3;
        m->hip = 0; m->spin_timer = 0;
        printf("fluff row %d: Mario takes hold" "\n", in->row);
        return 1;
    }
    return 0;
}

static void fluff_end(Player *m, float jump)
{
    Inst *in = &insts[flf.inst];
    V3 up = up_at(in->head, m->up), v = vadd(vtangent(in->hvel, up), vmul(up, jump));
    m->hvel = vtangent(v, m->up); m->vup = jump > 0 ? vdot(v, m->up) : 0;
    m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_used = 0;
    row_turn[in->row] = 2; row_step[in->row] = 0; /* FreeWaitInvalid */
    fluff_place(in);
    printf("fluff row %d: %s" "\n", in->row, jump > 0 ? "jump off" : "let go");
    flf.inst = -1;
}

/* returns 1 while Mario rides */
static int fluff_update(Player *m)
{
    Inst *in = &insts[flf.inst];
    V3 up = up_at(in->head, m->up);
    flf.t++;
    if (pad_pressed(PAD_CROSS)) { fluff_end(m, 18.0f); return 0; } /* updateRide: A jumps off */
    if (flf.phase == 1) { /* RideStart */
        if (vdot(in->hvel, up) < 15.0f) in->hvel = vadd(in->hvel, vmul(up, 0.2f));
        if (flf.t >= 40) { flf.phase = 2; flf.t = 0; }
    }
    if (flf.phase >= 2 && flf.spins > 0 && (pad_pressed(PAD_SQUARE) || test_sq) && (flf.phase == 2 || flf.t > 60)) { /* trySpinUp */
        flf.spins--; flf.phase = 3; flf.t = 0;
        printf("fluff row %d: spin up, %d left" "\n", in->row, flf.spins);
    }
    test_sq = 0;
    if (flf.phase == 3 && flf.t <= 30) in->hvel = vadd(in->hvel, vmul(up, 0.8f));
    if (flf.phase == 3 && flf.t > 90) { flf.phase = 2; flf.t = 0; }
    in->hvel = vmul(in->hvel, 0.98f); /* friction, the stick, gravity */
    V3 cam_fwd = vnorm(vtangent(vsub(m->pos, cam_pos), up)), cam_right = vnorm(vcross(cam_fwd, up));
    V3 stick = vadd(vmul(cam_right, pad_lx()), vmul(cam_fwd, pad_ly()));
    if (vlen(stick) > 0.1f) { in->hvel = vadd(in->hvel, vmul(vnorm(stick), 0.2f)); m->face = vnorm(stick); }
    in->hvel = vsub(in->hvel, vmul(up, 0.15f));
    V3 h = vtangent(in->hvel, up);
    if (vlen(h) > 6.0f) in->hvel = vadd(vmul(vnorm(h), 6.0f), vmul(up, vdot(in->hvel, up))); /* restrictVelocity 6 */
    V3 p = vadd(in->head, in->hvel);
    int wall = 0;
    V3 gn, wn, q = vsub(p, vmul(up, 80.0f)); /* initBinder(50, -80) */
    if (flf.phase >= 2 && (collide_n(&q, up, 50.0f, &gn, &wn, &wall) || wall)) { /* RideStart: offBind */ in->head = p; fluff_end(m, 0); return 0; } /* isBinded */
    in->head = p;
    m->pos = vsub(p, vmul(up, 130.0f)); /* hanging under it */
    m->up = up;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    fluff_place(in);
    return 1;
}

/* WaterArea.cpp (WaterCube / WaterSphere / WaterCylinder areas): p in water, *depth how far under its surface (the
 * box / cylinder top, the sphere's skin along up) */
static int area_inside(const Row *r, V3 p);
static int in_water(V3 p, V3 up, float *depth)
{
    static int types[5] = {-2, -2, -2, -2, -2};
    if (types[0] == -2) { types[0] = scene_type(sc, "WaterCube"); types[1] = scene_type(sc, "WaterSphere"); types[2] = scene_type(sc, "WaterCylinder");
        types[3] = scene_type(sc, "OceanSphere"); types[4] = scene_type(sc, "OceanBowl"); }
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if ((r->type == types[3] || r->type == types[4]) && row_shown(i)) {
            /* OceanSphere::isInWater: within 100 x scale; OceanBowl: that and under its plane (its up) */
            float rad = 100.0f * r->scale[0], d = vlen(vsub(p, row_pos(r)));
            if (d > rad) continue;
            V3 x, z;
            row_axes(r, &x, &z);
            float over = vdot(vsub(p, row_pos(r)), vcross(z, x));
            if (r->type == types[4] && over > 0.0f) continue;
            if (depth) *depth = r->type == types[3] ? rad - d : -over;
            return 1;
        }
        if ((r->type != types[0] && r->type != types[1] && r->type != types[2]) || !row_shown(i) || !area_inside(r, p)) continue;
        if (depth) {
            if (r->type == types[1]) *depth = 500.0f * r->scale[0] - vdot(vsub(p, row_pos(r)), up) ; /* the sphere's top along up */
            else {
                V3 x, z;
                row_axes(r, &x, &z);
                *depth = (r->type == types[2] ? 500.0f : 1000.0f) * r->scale[1] - vdot(vsub(p, row_pos(r)), vcross(z, x));
            }
        }
        return 1;
    }
    return 0;
}

/* MarioSwim.cpp, lean: in water (his middle under the surface) he swims: X strokes along his swim direction
 * (mSwimFrontAcc 4 up to mSwimFrontMaxSpeed 11.5), the stick turns him (left / right) and tilts the direction (up /
 * down); his speed goes x mSwimFrontIne 0.985 (mSwimStopIne 0.992 still), he floats up (mSwimUpAcc 0.1 to
 * mSwimUpMaxSpeed 1); within mSwimUpSurfaceDist 100 of the surface he stays at it, and X there jumps out. Under it his
 * oxygen runs down from mOxygenMax 3600 (60 s; an AirBubble fills it); at 0 he loses a life piece every 60 frames; at the surface it fills
 * back. Returns 1 while swimming. ponytail: no spin dash, no swim animations, no current / drift, no damage swim */
static void hurt_mario(V3 from, float push);
static int swim_update(Player *m)
{
    float depth = 0;
    V3 mid = vadd(m->pos, vmul(m->up, 60.0f));
    if (!in_water(mid, m->up, &depth)) {
        if (swimming) { swimming = 0; printf("swim: out of the water" "\n"); }
        oxygen = 3600;
        return 0;
    }
    if (!swimming) { swimming = 1; swim_pitch = 0; m->hip = 0; printf("swim: in the water" "\n"); }
    V3 cam_fwd = vnorm(vtangent(vsub(m->pos, cam_pos), m->up)), cam_right = vnorm(vcross(cam_fwd, m->up));
    float sx = pad_lx(), sy = pad_ly();
    if (fabsf(sx) > 0.1f || fabsf(sy) > 0.1f) { /* steer along the camera's view, tilt with the stick's forward part */
        V3 want = vnorm(vadd(vmul(cam_right, sx), vmul(cam_fwd, sy)));
        m->face = vnorm(vadd(vmul(m->face, 0.9f), vmul(want, 0.1f)));
    }
    if (pad_held(PAD_R1)) swim_pitch -= 0.04f; /* mSwimRotSpeedX: R1 dives, L1 rises */
    else if (pad_held(PAD_L1)) swim_pitch += 0.04f;
    else swim_pitch *= 0.95f;
    swim_pitch = swim_pitch > 1.3f ? 1.3f : swim_pitch < -1.3f ? -1.3f : swim_pitch;
    V3 dir = vadd(vmul(m->face, cosf(swim_pitch)), vmul(m->up, sinf(swim_pitch)));
    V3 v = vadd(m->hvel, vmul(m->up, m->vup));
    int surface = depth < 100.0f;
    if (pad_pressed(PAD_CROSS)) {
        if (surface && swim_pitch >= -0.2f) { /* out of the water */
            m->vup = 30.0f; m->hvel = vmul(m->face, 6.0f); m->grounded = 0; m->jump_kind = 0; m->air_timer = 0;
            swimming = 0;
            printf("swim: jump out" "\n");
            return 0;
        }
        v = vadd(v, vmul(dir, 4.0f)); /* a stroke */
        if (vlen(v) > 11.5f) v = vmul(vnorm(v), 11.5f);
    }
    int idle = fabsf(sx) < 0.1f && fabsf(sy) < 0.1f && !pad_held(PAD_CROSS);
    v = vmul(v, idle ? 0.992f : 0.985f);
    float up = vdot(v, m->up);
    if (up < 1.0f) v = vadd(v, vmul(m->up, 0.1f < 1.0f - up ? 0.1f : 1.0f - up)); /* floating up */
    if (surface && vdot(v, m->up) > 0 && depth < 60.0f) v = vtangent(v, m->up); /* held at the surface */
    V3 c = vadd(vadd(m->pos, vmul(m->up, BODY_RADIUS)), v), gn, wn;
    int wall = 0;
    collide_n(&c, m->up, BODY_RADIUS, &gn, &wn, &wall);
    m->pos = vsub(c, vmul(m->up, BODY_RADIUS));
    m->hvel = vtangent(v, m->up); m->vup = vdot(v, m->up); m->grounded = 0;
    if (!surface) {
        if (oxygen > 0) oxygen--;
        else if (game_frame % 60 == 0) { mario->hurt_timer = 0; hurt_mario(m->pos, 0.0f); }
    } else oxygen = oxygen + 30 < 3600 ? oxygen + 30 : 3600;
    if (m->spin_timer) m->spin_timer--;
    if (m->hurt_timer) m->hurt_timer--;
    return 1;
}

static void update_mario(void)
{
    Player *m = mario;
    m->up = up_at(m->pos, m->up);
    if (demo_on) { /* a demo: he waits (startBckPlayer PowerStarGet when it is the star's) */
        m->hvel = v3(0, 0, 0); m->vup = 0;
        if (m->hurt_timer) m->hurt_timer--;
        if (mario_anim) { mario_play(star_demo ? "powerstarget" : "wait", 1.0f, !star_demo); anim_update(mario_anim); }
        return;
    }
    if (swim_update(m)) {
        if (mario_anim) anim_update(mario_anim);
        return;
    }
    if (flf.inst >= 0 || (warp.from < 0 && jb.inst < 0 && coc.inst < 0 && fluff_try(m))) {
        if (flf.inst >= 0 && fluff_update(m)) {
            if (mario_anim) anim_update(mario_anim);
            return;
        }
    }
    if (coc.inst >= 0 || (warp.from < 0 && jb.inst < 0 && cocoon_try(m))) {
        if (coc.inst >= 0 && cocoon_update(m)) {
            if (mario_anim) anim_update(mario_anim);
            return;
        }
    }
    if (jb.inst >= 0 || (warp.from < 0 && gcap.phase < 2 && jbranch_try(m))) {
        if (jb.inst >= 0 && jbranch_update(m)) {
            if (mario_anim) anim_update(mario_anim);
            return;
        }
    }
    if (m->launch < 0 && warp.from < 0 && gcap_update(m) && gcap.phase >= 2) {
        if (mario_anim) anim_update(mario_anim);
        return;
    }
    if (warp.from >= 0 && warp_update(m)) {
        if (mario_anim) anim_update(mario_anim);
        return;
    }
    if (m->launch < 0) warp_try(m);
    if (warp.from >= 0) return;
    for (int i = 0; i < nlaunches; i++)
        if (launches[i].cool) launches[i].cool--;
    if (m->launch >= 0) {
        if (update_launch(m)) {
            if (mario_anim) anim_update(mario_anim);
            return;
        }
    } else if (pad_pressed(PAD_SQUARE)) { /* the spin near a launch star binds Mario (tryBind) */
        for (int i = 0; i < nlaunches; i++)
            if (!launches[i].cool && row_shown(launches[i].row) && vlen(vsub(vadd(m->pos, vmul(m->up, BODY_RADIUS)), launches[i].pos)) < LAUNCH_BIND) {
                m->launch = i; m->launch_t = 0; m->launch_from = m->pos; m->hip = 0;
                if (mario_anim) mario_play(launches[i].start < 45 ? "spin" : "superspindriverstart", 1.0f, 0);
#ifdef BENCH
                printf("launch %d\n", i);
#endif
                if (mario_anim) anim_update(mario_anim);
                return;
            }
    }
    /* camera-relative stick on the plane under Mario */
    V3 cam_fwd = vnorm(vtangent(vsub(m->pos, cam_pos), m->up));
    V3 cam_right = vnorm(vcross(cam_fwd, m->up));
    V3 want = vadd(vmul(cam_right, pad_lx()), vmul(cam_fwd, pad_ly()));
#ifdef SLOPE_TEST
    { /* push into the 77 degree face slope_test() chose for 240 frames, then let go */
        static int t;
        extern V3 slope_n;
        want = ++t < 240 ? vmul(vnorm(vtangent(vmul(slope_n, -1.0f), m->up)), 0.6f) : v3(0, 0, 0);
    }
#endif
    if (pad_held(PAD_UP)) want = vadd(want, cam_fwd);
    if (pad_held(PAD_DOWN)) want = vsub(want, cam_fwd);
    if (pad_held(PAD_RIGHT)) want = vadd(want, cam_right);
    if (pad_held(PAD_LEFT)) want = vsub(want, cam_right);
    if (vlen(want) > 1.0f) want = vnorm(want);
    float stick = vlen(want);
    m->hvel = vtangent(m->hvel, m->up);
    float ratio = vlen(m->hvel) / WALK_SPEED;

    int slipping = m->grounded && m->slip_keep == 0;
    if (slipping) { /* slide down the slope (hip slide), facing downhill; jumping still works */
        V3 downhill = vtangent(vmul(m->up, -1.0f), m->ground_n);
        float sn = vlen(downhill);
        if (sn > 1e-3f) {
            downhill = vmul(downhill, 1.0f / sn);
            m->hvel = vadd(vmul(m->hvel, 0.98f), vmul(downhill, SLIP_ACCEL * sn));
            if (vlen(m->hvel) > SLIP_MAX) m->hvel = vmul(vnorm(m->hvel), SLIP_MAX);
            m->face = vnorm(vtangent(downhill, m->up));
        }
        if (pad_pressed(PAD_CROSS)) try_jump(m, ratio);
    } else if (m->hip == HIP_LAND) { /* procHipDrop landing: stuck until the clip ends, the stick (after 15) or A (5) */
        m->hvel = v3(0, 0, 0);
        m->hip_t++;
        if (pad_pressed(PAD_CROSS) && m->hip_t > 5) { m->hip = 0; try_jump(m, 0); }
        else if ((m->hip_t > 15 && stick > 0.1f) || (mario_anim && mario_anim->done) || m->hip_t > 60) m->hip = 0;
    } else if (m->hip) { /* jumpToHipDrop: hang while the start clip plays (+2 frames), then fall straight down */
        m->hvel = v3(0, 0, 0);
        m->hip_t++;
        if (m->hip == HIP_START) {
            m->vup = 0;
            if ((!mario_anim || mario_anim->done || m->hip_t > 30) && m->hip_t > HIP_DROP_ZERO_GR_TIME) {
                m->hip = HIP_FALL; m->hip_t = 0;
                if (mario_anim) mario_play("hipdrop", 1.0f, 1);
            }
        } else {
            float rate = fminf(fmaxf(1.1f - fabsf(m->vup) / LIMIT_SPEED_HIP_DROP, 0.0f), 1.0f);
            m->vup = fmaxf(m->vup - GRAVITY_HIP_DROP * rate, -LIMIT_SPEED_HIP_DROP);
        }
    } else if (m->grounded) {
        if (stick > 0.1f) { /* turn towards the stick at most TURN_* radians per frame */
            float lim = ratio > 0.1f ? TURN_MOVE : TURN_STAND;
            V3 f = vnorm(vtangent(m->face, m->up));
            float c = vdot(f, want) / stick;
            if (c > cosf(lim)) m->face = vnorm(want);
            else {
                V3 side = vnorm(vtangent(want, f)); /* in-plane direction from f towards want */
                if (vlen(side) < 0.5f) side = vcross(m->up, f); /* want is straight behind: pick a side */
                m->face = vnorm(vadd(vmul(f, cosf(lim)), vmul(side, sinf(lim))));
            }
        }
        m->squat = pad_held(PAD_R1); /* Wii Z */
        float top = m->squat ? SQUAT_WALK : 1.0f;
        float in = stick > 0.1f ? (m->squat ? INERTIA_SQUAT : INERTIA_MAX) : (m->squat ? INERTIA_SQUAT : INERTIA_STOP);
        m->hvel = vadd(vmul(m->hvel, in), vmul(m->face, stick * top * WALK_SPEED * (1.0f - in)));
        m->land_timer++;
        if (pad_pressed(PAD_CROSS)) try_jump(m, ratio);
        else if (pad_pressed(PAD_SQUARE) && m->spin_timer == 0) m->spin_timer = SPIN_TIME;
        bee_meter = 180; bee_rise = bee_drop = 0; /* landing refills the flight */
    } else {
        m->air_timer++;
        /* air steering; front speed decays after a while (mJumpFrontReduction / mSquatJumpFrontReduction) */
        m->hvel = vadd(m->hvel, vmul(want, AIR_CONTROL * WALK_SPEED * 0.1f));
        if (m->jump_kind == 5 ? m->air_timer > SQUAT_FRONT_RED_T : m->air_timer > JUMP_FRONT_RED_T)
            m->hvel = vmul(m->hvel, m->jump_kind == 5 ? SQUAT_FRONT_RED : JUMP_FRONT_RED);
        if (vlen(m->hvel) > MAX_JUMP_SPEED) m->hvel = vmul(vnorm(m->hvel), MAX_JUMP_SPEED);
        if (stick > 0.1f && m->jump_kind != 6) m->face = vnorm(vadd(vmul(m->face, 0.9f), vmul(want, 0.1f)));
        Hit h[1];
        if (pad_pressed(PAD_R1) && m->jump_kind != 5 && /* jumpToHipDrop: not from a long jump, not near the ground */
            !kcl_query(vsub(m->pos, vmul(m->up, HIP_DROP_LIMIT_HEIGHT * 0.5f)), HIP_DROP_LIMIT_HEIGHT * 0.5f, h, 1)) {
            m->hip = HIP_START; m->hip_t = 0;
            m->hvel = v3(0, 0, 0); m->vup = 0; m->low_grav = 0;
            if (mario_anim) mario_play("hipdropstart", 1.0f, 0);
        } else if (m->wall_timer <= WALL_KEEP && pad_pressed(PAD_CROSS)) { /* wall kick: away from the wall (tryWallJump) */
            m->face = vnorm(vtangent(m->wall_n, m->up));
            start_jump(m, 0, vmul(m->face, WALL_JUMP_XZ), WALL_JUMP_Y, "walljump");
        } else if (pad_pressed(PAD_SQUARE) && !m->spin_used && m->spin_timer == 0) { /* trySpinJump */
            m->spin_used = 1;
            m->spin_timer = SPIN_TIME;
            m->low_grav = SPIN_AIR_TIME;
            m->jump_kind = 8;
            m->vup = SPIN_HEIGHT;
            m->hvel = vadd(m->hvel, vmul(m->face, ratio * TORNADO_FRONT));
            if (mario_anim) mario_play("spin", 1.0f, 0);
#ifdef BENCH
            printf("jump spin\n");
#endif
        }
        float g = m->low_grav > 0 ? JUMP_GRAV[8] : JUMP_GRAV[m->jump_kind >= 0 ? m->jump_kind : 0];
        if (m->low_grav > 0) m->low_grav--;
        /* MarioBee.cpp: A held after mBeeAirWalkInhibitTime (30) with flight left lifts him (mBeeFlyConstantFactor 5 x
         * mBeeAccelRatio 0.02 a frame up to mBeeUpSpeedMax 5, his own rise x mBeePushRiseGravityEraser 0.9); let go
         * he drops slowly (mBeeFreeDropAcc 0.1 to 5). ponytail: no wall walk, no honey walls, no bee animations */
        if (bee_mode && (pad_held(PAD_CROSS) || test_cross) && m->air_timer > 30 && bee_meter > 0) {
            bee_meter--; bee_drop = 0;
            bee_rise += 5.0f * 0.02f;
            if (bee_rise > 5.0f) bee_rise = 5.0f;
            m->vup = m->vup > bee_rise ? m->vup * 0.9f : bee_rise;
        } else if (bee_mode && m->vup <= 0) {
            bee_rise = 0;
            if (bee_drop < 5.0f) bee_drop += 0.1f;
            m->vup = -bee_drop;
        } else
        m->vup -= g * (2.0f + kari_cling) * 0.5f; /* Karikari clinging: (2 + n) / 2 */
        if (m->vup < -MAX_DROP) m->vup = -MAX_DROP;
    }
    if (m->spin_timer) m->spin_timer--;
    if (slipping) slip_frames++;

    float down = m->grounded ? -GROUNDING : m->vup; /* on the ground: step down to follow the floor */
    V3 move = vadd(m->hvel, vmul(m->up, down)), center = vadd(m->pos, vmul(m->up, BODY_RADIUS));
    V3 gn = m->up;
    int was_ground = m->grounded;
    V3 wn = m->wall_n;
    int ground = 0, steps = (int)(vlen(move) / BODY_RADIUS) + 1; /* no step longer than the body: a hip drop falls
                                                                   * 150 a frame, thin floors would let it through */
    for (int s = 0; s < steps && !ground; s++) {
        center = vadd(center, vmul(move, 1.0f / steps));
        ground = collide_n(&center, m->up, BODY_RADIUS, &gn, &wn, &m->wall);
    }
    if (ground && ground_inst >= 0 && insts[ground_inst].collapse && !row_step[insts[ground_inst].row])
        row_step[insts[ground_inst].row] = 1; /* CollapsePlane::exeWait: Mario on it starts the collapse */
    if (m->wall) { m->wall_n = wn; m->wall_timer = 0; } else if (m->wall_timer < 1000) m->wall_timer++;
    m->pos = vsub(center, vmul(m->up, BODY_RADIUS));
    if (ground && m->vup <= 0) {
        if (m->hip == HIP_FALL) { /* hip drop landing: hip_hit tells the objects under Mario this frame */
            m->hip = HIP_LAND; m->hip_t = 0;
            hip_hit = 1; hip_inst = ground_inst;
            if (mario_anim) mario_play("hipdropland", 1.0f, 0);
        } else if (!was_ground && (m->jump_kind > 0 || m->last_jump >= 0 || m->air_timer > DROP_WAIT)) {
            /* landing from a jump or a real fall: keep the jump kind for the 2nd / 3rd jump window */
            static const char *land[9] = {"land", "jump2land", "jumprollland", "land", "land", "jumpbroadland",
                                          "jumpbackland", "land", "land"};
            m->land_timer = 0;
            if (mario_anim && m->jump_kind >= 0) mario_play(land[m->jump_kind], 1.0f, 0);
        }
        m->grounded = 1; m->vup = 0; m->spin_used = 0; m->jump_kind = -1; m->low_grav = 0;
        m->ground_n = gn;
        m->hvel = vtangent(m->hvel, gn); /* walk along the slope */
        if (vdot(gn, m->up) < SLIP_ANGLE_COS) { if (m->slip_keep) m->slip_keep--; }
        else m->slip_keep = SLIP_KEEP;
    } else if (!ground) {
        if (m->grounded) { m->jump_kind = 0; m->air_timer = 0; m->last_jump = -1; }
        m->grounded = 0;
    }
    if (m->wall && !m->grounded) { /* stop pushing into the wall */
        float into = vdot(m->hvel, m->wall_n);
        if (into < 0) m->hvel = vsub(m->hvel, vmul(m->wall_n, into));
    }
    mario_ground = m->grounded ? ground_inst : -1;
    if (m->grounded != was_ground) ground_flips++;
#ifdef SLOPE_TEST
    {
        static int f;
        if (++f % 10 == 0 && f <= 400)
            printf("slope f %d grounded %d slip_keep %d flips %d wall %d d %.2f clip %s pos %d %d %d\n", f, m->grounded,
                   m->slip_keep, ground_flips, m->wall, vdot(m->ground_n, m->up),
                   mario_anim && mario_anim->clip_id >= 0 ? sc->clips[mario_anim->clip_id].name : "-",
                   (int)m->pos.x, (int)m->pos.y, (int)m->pos.z);
    }
#endif
    if (!test_nolost && lost(m->pos)) /* fell off */
        miss();
    if (m->hurt_timer) m->hurt_timer--;

    if (mario_anim) { /* animation: the one-shot clip of a move plays out, then the state picks a loop */
        float sp = vlen(m->hvel) / WALK_SPEED;
        int busy = mario_anim->clip_id >= 0 && !mario_anim->loop && !mario_anim->done;
        if (m->hip) ; /* the hip drop plays its own clips */
        else if (m->grounded && m->slip_keep == 0) mario_play("slidehipforward", 1.0f, 1); /* 坂すべり下向きあおむけ */
        else if (m->grounded && m->spin_timer > 0) mario_play("spinground", 1.0f, 0);
        else if (m->grounded && (!busy || stick > 0.1f)) { /* moving cancels a landing */
            if (m->squat) mario_play(sp > 0.05f ? "squatwalk" : "squatwait", 1.0f, 1);
            else if (sp > 0.6f || (sp > 0.5f && mario_anim->clip_id >= 0 && mario_anim->loop && anim_length(mario_anim) &&
                                   strcmp(sc->clips[mario_anim->clip_id].name, "run") == 0))
                mario_play("run", sp, 1); /* hysteresis: run above 0.6, back to walk below 0.5 */
            else if (sp > 0.05f) mario_play("walk", 0.5f + sp, 1);
            else mario_play("wait", 1.0f, 1);
        } else if (!m->grounded && !busy && m->air_timer > DROP_WAIT) {
            if (m->wall_timer <= WALL_KEEP && m->vup < 0) mario_play("wallslide", 1.0f, 1);
            else mario_play("fall", 1.0f, 1);
        }
        anim_update(mario_anim);
    }
}

/* A miss (no life left, or fell into space): one Mario less, back to the start with 3 life. ponytail: no miss
 * animation, the stage is not reset, game over just refills the lives */
static void miss(void)
{
    time_left = time_limit; /* the comet's time starts over with the scenario */
    mario->pos = start_pos; mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hurt_timer = 0; mario->hip = 0;
    mario->launch = -1; /* out of any launch star's flight (it would carry him back) */
    life = max_life = MAX_LIFE;
    if (--lives < 0) { printf("game over\n"); lives = START_LIVES; }
    printf("miss %d, lives %d\n", ++misses, lives);
}

/* Mario hit by something at `from`: one life gone, knocked away from it and blinking */
/* Mario's own effects (AutoEffectList group Mario): at his body (k 1) or his feet (k 0), facing his front.
 * ponytail: not on his joints */
static void mario_fx(int fx, float k)
{
    float m[16];
    basis_matrix(m, vadd(mario->pos, vmul(mario->up, k * BODY_RADIUS)), mario->up, mario->face, 0);
    fx_emit(fx, m);
}

static void hurt_mario(V3 from, float push)
{
    if (mario->hurt_timer) return;
    if (bee_mode) { bee_mode = 0; printf("bee mario lost\n"); } /* the bee suit goes at a hit */
    mario_fx(FX_MARIO_DAMAGE, 1.0f);
    if (--life <= 0) { miss(); return; }
    V3 away = vtangent(vsub(mario->pos, from), mario->up);
    if (vlen(away) < 1e-3f) away = mario->face;
    mario->hvel = vmul(vnorm(away), push);
    mario->vup = 10.0f; mario->grounded = 0; mario->hurt_timer = 60; mario->hip = 0;
}

/* n coins popping out at p (CoinHolder::appearCoinPop): 25 against gravity, randomized by 4 when several */
static void pop_coins(V3 p, V3 up, int n)
{
    for (int k = 0; k < n; k++) {
        V3 rnd = n > 1 ? v3((rand() % 801 - 400) / 100.0f, (rand() % 801 - 400) / 100.0f, (rand() % 801 - 400) / 100.0f)
                       : v3(0, 0, 0);
        pops[npops].coin = ncoins;
        pops[npops].t = 0;
        pops[npops++].vel = vadd(vmul(up, 25.0f), rnd);
        coins[ncoins].pos = vadd(p, vmul(up, 50.0f));
        coins[ncoins].row = -1;
        coins[ncoins++].alive = PICK_POP_NEW;
    }
}

/* Takobo.cpp: patrols its X axis (arg1 0 / 1, else Z; 1 and 3 start backwards) between -arg3 and arg3 (default 500)
 * about its place at arg2 (default 5): each leg eased in-out over 1.3 x its length / speed (exeMove); within 250 of
 * Mario it stops and punches (Attack: its fist hurts at frames 85-90, ~120 frames, then Wait 60). A stomp or hip drop
 * presses it (a coin), a spin blows it away; touching it from the side hurts. phase 0 move, 1 attack, 2 wait; timer
 * its frames; dir the leg (0 out, 1 back); from / to / len the leg. ponytail: faces Mario at once (no 0.05 turn), no
 * stun, no star pieces, no fire floor bounce */
static void takobo_update(Kuribo *k, V3 to, float d)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z, base = row_pos(r);
    row_axes(r, &x, &z);
    V3 axis = r->arg[1] == 0 || r->arg[1] == 1 ? x : z;
    float half = r->arg[3] >= 0 ? (float)r->arg[3] : 500.0f, speed = r->arg[2] >= 0 ? (float)r->arg[2] : 5.0f;
    float above = vdot(to, k->up), s = r->scale[1];
    if (d < 150.0f * s && mario->vup < 0 && above > 60.0f * s) { /* tryPress */
        k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up);
        pop_coins(k->pos, k->up, 1);
        mario->vup = 15.0f; mario->grounded = 0;
        return;
    }
    if (d < 170.0f * s && mario->spin_timer > 0) { /* HitPunch */
        k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up);
        pop_coins(k->pos, k->up, 1);
        return;
    }
    if (k->phase == 3) { /* StunStart / Stun (a star piece): a spin blows it away (above) */
        if (++k->timer >= 240) { k->phase = 2; k->timer = 0; }
        return;
    }
    k->timer++;
    if (k->phase == 0) {
        if (k->timer == 1) { /* exeMove's first step: from here to the end of the leg */
            if (!k->dir && !k->len && !k->from) k->dir = r->arg[1] == 1 || r->arg[1] == 3;
            k->from = vdot(vsub(k->pos, base), axis);
            k->to = k->dir ? -half : half;
            k->len = fabsf(k->to - k->from) / speed * 1.3f;
            if (k->len < 1) k->len = 1;
        }
        float u = k->timer / k->len;
        u = u > 1 ? 1 : u;
        float e = k->from + (k->to - k->from) * u * u * (3.0f - 2.0f * u);
        V3 center = vadd(vadd(base, vmul(axis, e)), vmul(k->up, 45.0f + vdot(vsub(k->pos, base), k->up)));
        collide(&center, k->up, 45.0f);
        k->pos = vsub(center, vmul(k->up, 45.0f));
        if (k->timer > k->len) { k->dir = !k->dir; k->timer = 0; }
        else if (d < 250.0f) { k->phase = 1; k->timer = 0; }
    } else if (k->phase == 1) {
        if (k->timer >= 85 && k->timer < 90) {
            V3 fist = vadd(vadd(k->pos, vmul(k->up, 70.0f * s)), vmul(vnorm(vtangent(to, k->up)), 90.0f * s));
            if (vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), fist)) < 80.0f * s + HIT_BODY_R)
                hurt_mario(fist, 20.0f);
        }
        if (k->timer >= 120) { k->phase = 2; k->timer = 0; }
    } else if (k->timer >= 60) { k->phase = 0; k->timer = 0; }
    if (d < 120.0f * s && above <= 60.0f * s && k->phase != 2 && !mario->hurt_timer) { /* body: from the side */
        hurt_mario(k->pos, 15.0f);
        k->phase = 2; k->timer = 0; /* HitReaction, then Wait */
    }
}

/* MechanicKoopaMini.cpp: within 900 of Mario it hops (Find: 26 up) and pursues (accel 1.3, damp 0.8, gravity 2);
 * within 300 PreFire (60 frames) then Fire (120: its flame hurts in front, the fire sensor ~29 at the mouth), FireEnd
 * (30), back to waiting; beyond 1100 it gives up. A stomp (Trample) or a spin (SpinHit) stuns it 60 frames, then it
 * fires; a hip drop breaks it (a coin). phase 0 wait, 1 find, 2 pursue, 3 prefire, 4 fire, 5 fire end, 6 stunned;
 * timer its frames. ponytail: waits instead of walking its rail, no wall jump, faces Mario at once, flame reach 160 */
static void mkoopa_update(Kuribo *k, V3 to, float d)
{
    float above = vdot(to, k->up), s = sc->objs[k->row].scale[1];
    V3 dir = vnorm(vtangent(to, k->up));
    if (mario->hip == HIP_FALL && d < 200.0f * s && above > 60.0f * s) { /* HipDropped: a hip drop onto it */
        k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up);
        pop_coins(k->pos, k->up, 1);
        return;
    }
    if (k->phase != 6 && ((d < 200.0f * s && mario->vup < 0 && above > 80.0f * s) || (d < 200.0f * s && mario->spin_timer > 0))) {
        if (mario->vup < 0 && above > 80.0f * s) { mario->vup = 15.0f; mario->grounded = 0; }
        k->phase = 6; k->timer = 0;
    }
    k->timer++;
    float accel = 0;
    switch (k->phase) {
    case 0: if (d < 900.0f) { k->phase = 1; k->timer = 0; k->vel = vadd(k->vel, vmul(k->up, 26.0f)); } break;
    case 1: if (k->timer >= 30) { k->phase = 2; k->timer = 0; } break;
    case 2:
        accel = 1.3f;
        if (d > 1100.0f) { k->phase = 0; k->timer = 0; }
        else if (d < 300.0f) { k->phase = 3; k->timer = 0; }
        break;
    case 3: if (k->timer >= 60) { k->phase = 4; k->timer = 0; } break;
    case 4:
        if (vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), vadd(vadd(k->pos, vmul(k->up, 80.0f * s)), vmul(dir, 100.0f * s)))) <
            60.0f * s + HIT_BODY_R && !mario->hurt_timer)
            hurt_mario(k->pos, 15.0f); /* sendMsgEnemyAttackFire */
        if (k->timer > 120) { k->phase = 5; k->timer = 0; }
        break;
    case 5: if (k->timer >= 30) { k->phase = 0; k->timer = 0; } break;
    default: if (k->timer >= 60) { k->phase = 4; k->timer = 0; } break;
    }
    V3 vu = vmul(k->up, vdot(k->vel, k->up)), vh = vsub(k->vel, vu);
    vh = vadd(vmul(vh, 0.8f), vmul(dir, accel));
    vu = vsub(vu, vmul(k->up, 2.0f));
    V3 center = vadd(vadd(k->pos, vmul(k->up, 60.0f)), vadd(vh, vu));
    int ground = collide(&center, k->up, 60.0f);
    k->pos = vsub(center, vmul(k->up, 60.0f));
    if (ground && vdot(vu, k->up) < 0) vu = v3(0, 0, 0);
    k->vel = vadd(vh, vu);
    if (d < 130.0f * s && above <= 80.0f * s && !mario->hurt_timer) /* body: pushes Mario (ponytail: a nudge) */
        mario->hvel = vadd(mario->hvel, vmul(vnorm(vtangent(to, mario->up)), 3.0f));
}

/* Karikari.cpp / KarikariDirector.cpp: waits until Mario is within 1000, then hops at him (every 11 frames 33 up and
 * 10 towards him, gravity 2.4, damp 0.98 in the air, 0.8 on the ground); touching him (80) it clings (5 at most:
 * the others wait). Clinging ones (phase 2) ride him: his jumps fall (2 + n) / 2 times faster (MarioJump.cpp), every
 * 120 frames they bite a life away unless he has one left (Mario::checkKarikariDamage); a hip drop landing throws
 * them off (43 out, 33 up: DPDAttacked), and so does R2 with the pointer on one (one every 10 frames). A spin kills a free one. phase 0 wait, 1 pursue, 2 cling,
 * 3 thrown; timer its frames; dir its cling slot. ponytail: no star pointer, no frozen / tornado states, a thrown one
 * pursues again once it lands, no drops when it dies, cling slots as offsets around Mario (not his joints) */
static int pointer_a(void);
static int cursor_sees(V3 p, float px);
static void kari_update(Kuribo *k, V3 to, float d)
{
    static const float slot[5][2] = {{150, 0}, {90, 1.2f}, {60, 3.1f}, {110, 4.4f}, {120, 2.2f}}; /* height, angle */
    if (k->phase == 2) {
        static unsigned release_frame; /* KarikariDirector sNoReleaseTime: one release per 10 frames */
        int pointed = pointer_a() && game_frame - release_frame >= 10 && cursor_sees(k->pos, 30.0f);
        if (pointed) release_frame = game_frame; /* requestRelease: R2 on a clinging one throws it off */
        if (hip_hit || pointed) {
            V3 away = vnorm(vtangent(vsub(k->pos, mario->pos), mario->up));
            k->vel = vadd(vmul(away, 43.0f), vmul(mario->up, 33.0f));
            k->phase = 3; k->timer = 0; kari_cling--;
            printf("karikari %d thrown off (%d left)\n", k->row, kari_cling);
            return;
        }
        float a = slot[k->dir][1];
        V3 side = vnorm(vcross(mario->up, mario->face));
        k->pos = vadd(vadd(mario->pos, vmul(mario->up, slot[k->dir][0] - 40.0f)),
                      vmul(vadd(vmul(mario->face, cosf(a)), vmul(side, sinf(a))), 35.0f));
        k->up = mario->up;
        return;
    }
    if (k->phase != 3 && d < 120.0f && mario->spin_timer > 0) { /* tryBlowOut */
        k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up);
        return;
    }
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    if (k->phase == 1 && kari_cling < 5 && vlen(vsub(c, vadd(k->pos, vmul(k->up, 30.0f)))) < 80.0f + HIT_BODY_R) {
        int used[5] = {0};
        for (int i = 0; i < nkuribo; i++)
            if (kuribo[i].kind == WALK_KARIKARI && kuribo[i].phase == 2 && kuribo[i].state != KURIBO_DEAD) used[kuribo[i].dir] = 1;
        for (k->dir = 0; used[k->dir]; k->dir++) {}
        k->phase = 2; kari_cling++;
        if (kari_cling == 1) kari_timer = 120;
        printf("karikari %d clings (%d)" "\n", k->row, kari_cling);
        return;
    }
    k->timer++;
    V3 vu = vmul(k->up, vdot(k->vel, k->up)), vh = vsub(k->vel, vu);
    vu = vsub(vu, vmul(k->up, 2.4f));
    V3 center = vadd(vadd(k->pos, vmul(k->up, 30.0f)), vadd(vh, vu));
    int ground = collide(&center, k->up, 30.0f);
    k->pos = vsub(center, vmul(k->up, 30.0f));
    if (ground && vdot(vu, k->up) <= 0) {
        vu = v3(0, 0, 0); vh = vmul(vh, 0.8f);
        if (k->phase == 0 && d < 1000.0f) { k->phase = 1; k->timer = 0; }
        else if (k->phase == 3 && k->timer > 10) { k->phase = d < 1000.0f ? 1 : 0; k->timer = 0; }
        else if (k->phase == 1) {
            if (d > 1000.0f) k->phase = 0;
            else if (k->timer >= 11) {
                vu = vmul(k->up, 33.0f);
                vh = vmul(vnorm(vtangent(to, k->up)), 10.0f);
                k->timer = 0;
            }
        }
    } else
        vh = vmul(vh, 0.98f);
    k->vel = vadd(vh, vu);
}

static void kari_bite(void) /* Mario::checkKarikariDamage */
{
    if (!kari_cling) return;
    if (mario->hurt_timer) { kari_timer = 120; return; }
    if (--kari_timer > 0) return;
    kari_timer = 120;
    if (life > 1) { life--; printf("karikari bite: life %d" "\n", life); }
}

/* Karon.cpp (Dry Bones): within 700 of Mario it pursues him (accel 0.25, damp 0.95), beyond 1200 it waits; a stomp,
 * hip drop or spin breaks it into a heap (Broken / StepBroken) for 300 frames, then Recovery (~60) and it waits again:
 * it never dies. Touching Mario from the side hurts him and bounces it back (HitReaction, 30 frames); a star piece
 * staggers it. phase 0 wait, 1 pursue, 2 broken (timer to 360), 3 hit reaction; timer its frames. ponytail: no
 * territory walk or rail, faces Mario at once, the heap is the model squashed */
static void karon_update(Kuribo *k, V3 to, float d)
{
    float above = vdot(to, k->up), s = sc->objs[k->row].scale[1];
    V3 dir = vnorm(vtangent(to, k->up));
    k->timer++;
    if (k->phase == 2) {
        if (k->timer >= 360) { k->phase = 0; k->timer = 0; }
    } else if (d < 180.0f * s && ((mario->vup < 0 && above > 80.0f * s) || mario->spin_timer > 0 || mario->hip == HIP_FALL)) {
        if (mario->vup < 0 && above > 80.0f * s && mario->hip != HIP_FALL) { mario->vup = 15.0f; mario->grounded = 0; } /* touch jump */
        k->phase = 2; k->timer = 0;
        printf("karon %d broken" "\n", k->row);
    } else if (k->phase == 3) {
        if (k->timer >= 30) { k->phase = 0; k->timer = 0; }
    } else if (d < 130.0f * s && above <= 80.0f * s && !mario->hurt_timer) {
        hurt_mario(k->pos, 15.0f);
        k->vel = vadd(vmul(dir, -10.0f), vmul(k->up, 20.0f));
        k->phase = 3; k->timer = 0;
    } else if (k->phase == 0 && d < 700.0f) { k->phase = 1; k->timer = 0; }
    else if (k->phase == 1 && d > 1200.0f) { k->phase = 0; k->timer = 0; }
    float accel = k->phase == 1 ? 0.25f : 0.0f, damp = k->phase == 1 ? 0.95f : 0.8f;
    V3 vu = vmul(k->up, vdot(k->vel, k->up)), vh = vsub(k->vel, vu);
    vh = vadd(vmul(vh, damp), vmul(dir, accel));
    if (k->phase == 2) vh = v3(0, 0, 0);
    vu = vsub(vu, vmul(k->up, 1.2f));
    V3 center = vadd(vadd(k->pos, vmul(k->up, 60.0f)), vadd(vh, vu));
    int ground = collide(&center, k->up, 60.0f);
    k->pos = vsub(center, vmul(k->up, 60.0f));
    if (ground && vdot(vu, k->up) < 0) vu = v3(0, 0, 0);
    k->vel = vadd(vh, vu);
}

/* Teresa.cpp (Boo): floats 150 over the ground (addVelocityKeepHeightUseShadow, damp 0.9); when Mario faces it (his
 * front towards it, not squatting) within 1500 it stops shy (Shay), when he turns his back within 1800 it chases him
 * (accel 0.75 ramping over 20 frames, 240 frames at most); touching him hurts and it laughs (AttackSuccess, 60); a
 * spin near it (SpinStormRange, ~350) drifts it away (1.25 a frame, 50 frames), into a wall it hides 60 frames and
 * comes back. phase 0 wait, 1 chase, 2 shy, 3 drift, 4 hidden, 5 laugh; timer its frames. ponytail: no transparency,
 * no light that kills it (GlaringLightArea, the sun), no star piece stop, no walk / rail walk, faces Mario at once */
static void teresa_update(Kuribo *k, V3 to, float d)
{
    (void)to;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), body = vadd(k->pos, vmul(k->up, 70.0f));
    int facing = vdot(mario->face, vsub(body, mario->pos)) > 0 && !mario->squat;
    k->timer++;
    switch (k->phase) {
    case 4:
        if (k->timer >= 60) { k->phase = 0; k->timer = 0; } /* AppearFromWall */
        return;
    case 3:
        k->vel = vadd(k->vel, vmul(vnorm(vsub(body, c)), 1.25f));
        if (k->timer >= 50) { k->phase = 0; k->timer = 0; }
        break;
    case 5:
        if (k->timer >= 60) { k->phase = 0; k->timer = 0; }
        break;
    case 2:
        k->vel = v3(0, 0, 0);
        if (!(facing && d < 1500.0f)) { k->phase = 1; k->timer = 0; } /* Aggressive: after him */
        break;
    case 1:
        if (facing && d < 1500.0f) { k->phase = 2; k->timer = 0; break; }
        k->vel = vadd(k->vel, vmul(vnorm(vsub(c, body)), 0.75f * (k->timer < 20 ? k->timer / 20.0f : 1.0f)));
        if (k->timer > 240 || d > 3000.0f) { k->phase = 0; k->timer = 0; }
        break;
    default:
        if (facing && d < 1500.0f) { k->phase = 2; k->timer = 0; }
        else if (d < 1800.0f) { k->phase = 1; k->timer = 0; }
    }
    if (k->phase != 3 && mario->spin_timer > 0 && d < 350.0f) { k->phase = 3; k->timer = 0; }
    if ((k->phase == 1 || k->phase == 0) && vlen(vsub(c, body)) < 70.0f + HIT_BODY_R && !mario->hurt_timer) {
        hurt_mario(body, 15.0f);
        k->phase = 5; k->timer = 0;
    }
    V3 ground;
    if (drop_to_ground(vadd(k->pos, vmul(k->up, 100.0f)), k->up, 700.0f, &ground)) { /* keep 150 over the ground */
        float h = vdot(vsub(k->pos, ground), k->up);
        k->vel = vadd(k->vel, vmul(k->up, (150.0f - h) * 0.04f));
    }
    k->vel = vmul(k->vel, 0.9f);
    V3 center = vadd(body, k->vel), was = center;
    int wall = 0;
    collide_n(&center, k->up, 70.0f, NULL, NULL, &wall);
    if (k->phase == 3 && (wall || vlen(vsub(center, was)) > 1.0f)) { /* tryHideWall */
        k->phase = 4; k->timer = 0; k->vel = v3(0, 0, 0);
        printf("teresa %d hides in a wall" "\n", k->row);
    }
    k->pos = vsub(center, vmul(k->up, 70.0f));
}

/* Butterfly.cpp, with the right-stick pointer: at rest it drifts back to its home (accel 0.01 towards it, at most 2,
 * damp 0.99); the pointer on it (its 3D target radius 100) within 500 of home makes it run away (12 a frame away from
 * the pointer's ray, damp 0.97, 30 frames); the pointer within 10 pixels of it brings out its star piece once (arg1
 * unset). phase 0 rest, 1 running; dir: its piece is out. ponytail: no Hive (following a slow pointer), no perching,
 * no sleeping-Mario visit, it faces Mario like the walkers */
static void cursor_ray(V3 *o, V3 *d);
static int cursor_sees(V3 p, float px);
static void drop_pieces(V3 p, V3 up, int n);
/* StinkBugSmall.cpp / StinkBugBase.cpp: a beetle at its home facing its front. Mario in its territory (within arg1
 * (1000) + 400, 200 along its up, 600 across, in front) makes it turn to him (3 degrees a frame, within arg0 of its
 * front) for 40 frames (RushStart + 10), then rush at 20 along its face until arg1 from home or a wall (Dash), stop
 * (DashEnd, 20 frames) and walk back at 5 (Back). With arg0 > 0 it sweeps its front +-arg0 at 1 degree a frame while
 * waiting (Search). Its horn (100 x scale ahead) hurts Mario (EnemyAttackStrong) and it stops (Attack); a spin or a
 * star piece makes it guard (SpinReaction, 30 frames); a hip drop kills it: 3 star pieces. phase 0 wait, 1 dash sign,
 * 2 dash, 3 dash end, 4 back, 5 attack / guard, 6 dying; from its turn in degrees from its front, to the sweep sign.
 * ponytail: no standing on its back (Panic / Shake), no star pointer swoon, arg0 -1 taken as 0, arg1 -1 as 1000 */
static V3 stink_front(const Kuribo *k)
{
    V3 x, z;
    row_axes(&sc->objs[k->row], &x, &z);
    V3 f = vnorm(vtangent(z, k->up));
    float a = k->from * 0.0174533f;
    return vadd(vmul(f, cosf(a)), vmul(vcross(k->up, f), sinf(a)));
}

static void stink_update(Kuribo *k, V3 to, float d)
{
    const Row *r = &sc->objs[k->row];
    float s = r->scale[0], range = r->arg[1] > 0 ? (float)r->arg[1] : 1000.0f, sweep = r->arg[0] > 0 ? (float)r->arg[0] : 0;
    V3 home = row_pos(r), f = stink_front(k), c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    k->timer++;
    if (k->phase == 6) { /* HipDropDown */
        if (k->timer >= 30) {
            k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up);
            drop_pieces(k->pos, k->up, 3);
            printf("stink bug row %d: dead" "\n", k->row);
        }
        return;
    }
    float above = vdot(to, k->up);
    if (mario->hip == HIP_FALL && d < 150.0f * s + HIT_BODY_R && above > 0) { /* isMsgPlayerHipDrop */
        k->phase = 6; k->timer = 0; mario->hip = HIP_LAND; mario->hip_t = 0; mario->vup = 15.0f;
        printf("stink bug row %d: hip drop" "\n", k->row);
        return;
    }
    if (k->phase != 2 && k->phase != 5 && mario->spin_timer > 0 && d < 200.0f * s) { k->phase = 5; k->timer = 0; } /* guard */
    if (k->phase != 5 && k->phase != 6) { /* the horn */
        V3 horn = vadd(vadd(k->pos, vmul(f, 100.0f * s)), vmul(k->up, 70.0f * s)), hc = vsub(c, horn);
        if (vlen(hc) < 100.0f * s + HIT_BODY_R && vdot(vsub(c, k->pos), f) > 0 && !mario->hurt_timer) {
            hurt_mario(k->pos, 25.0f); /* sendMsgEnemyAttackStrong */
            if (k->phase != 2) { k->phase = 5; k->timer = 0; }
            printf("stink bug row %d: horn hits Mario" "\n", k->row);
        }
    }
    V3 side = vcross(k->up, f), from_home = vsub(mario->pos, k->pos);
    int seen = vlen(vsub(mario->pos, k->pos)) < range + 400.0f && fabsf(vdot(from_home, k->up)) <= 200.0f &&
               fabsf(vdot(from_home, side)) <= 600.0f && vdot(from_home, f) > 0; /* isPlayerInTerritory */
    float speed = 0;
    switch (k->phase) {
    case 0: /* Wait / Search */
        k->pos = home;
        if (seen) { k->phase = 1; k->timer = 0; break; }
        if (sweep > 0) {
            if (k->to == 0) k->to = 1;
            k->from += k->to;
            if (fabsf(k->from) > sweep) { k->from = k->from > 0 ? sweep : -sweep; k->to = -k->to; }
        }
        break;
    case 1: { /* DashSign: turn to Mario within +-arg0 of its front */
        k->pos = home;
        V3 t = vtangent(to, k->up);
        if (vlen(t) > 1e-3f) {
            float turn = atan2f(vdot(t, side), vdot(t, f)) * 57.29578f;
            k->from += turn > 3 ? 3 : turn < -3 ? -3 : turn;
            float lim = sweep > 0 ? sweep : 0;
            if (k->from > lim) k->from = lim;
            if (k->from < -lim) k->from = -lim;
        }
        if (k->timer >= 40) { k->phase = 2; k->timer = 0; }
        break;
    }
    case 2: speed = 20.0f; break; /* Dash */
    case 3: if (k->timer >= 20) { k->phase = 4; k->timer = 0; } break;
    case 4: { /* Back */
        V3 h = vtangent(vsub(home, k->pos), k->up);
        if (vlen(h) < 10.0f) { k->pos = home; k->phase = 0; k->timer = 0; break; }
        k->vel = vmul(vnorm(h), 5.0f);
        break;
    }
    default: if (k->timer >= 30) { k->phase = 4; k->timer = 0; } break; /* Attack / SpinReaction */
    }
    if (k->phase == 2 || k->phase == 4) { /* moving: on the ground, walls stop the dash */
        if (k->phase == 2) {
            float left = range - vlen(vtangent(vsub(k->pos, home), k->up));
            k->vel = vmul(f, left < speed ? (left > 0 ? left : 0) : speed);
        }
        V3 center = vadd(vadd(k->pos, k->vel), vmul(k->up, 100.0f * s - 2.0f)), gn, wn;
        int wall = 0;
        collide_n(&center, k->up, 100.0f * s, &gn, &wn, &wall);
        k->pos = vsub(center, vmul(k->up, 100.0f * s));
        if (k->phase == 2 && (wall || vlen(vtangent(vsub(k->pos, home), k->up)) >= range - 1.0f)) {
            k->phase = 3; k->timer = 0; k->vel = v3(0, 0, 0);
            printf("stink bug row %d: dash ends%s" "\n", k->row, wall ? " at a wall" : "");
        }
    }
}

/* Unizo.cpp (Unizo, UnizoLand, UnizoShoal): a rolling sea urchin (radius 126.36). Mario within 1500 (on the ground,
 * not hurt) makes it jump (sqrt(2 x 70 x gravity rate) up; rate 0.15 at sea, 0.8 on land) and chase him: 0.1 a frame
 * towards him turned +-45 degrees by sin(frame / 20), up to 4 (12 in all); on the ground x 0.96 chasing, x 0.9 else;
 * gravity 0.2 on the ground, 0.8 in the air (the jump's rate while jumping, floor rebound 0.6). Touching Mario hurts
 * him (EnemyAttackStrong) and bounces it (5 up, 1 back), then it rests 120 frames (CollidePlayer). Stomps and spins
 * do nothing; a star piece only shakes it; enemy attacks (PunchingKinoko, CocoNut) break it: 6 star pieces. phase 0
 * wait, 1 jump, 2 chase, 3 collide; timer its frames; to its roll angle. ponytail: no damage-floor JumpDown, no spin
 * drift, no shoal water ripples, the roll turns about the current velocity (no quaternion kept) */
static void unizo_update(Kuribo *k, V3 to, float d)
{
    const float R = 126.36f, rate = k->dir == 1 ? 0.8f : 0.15f;
    V3 up = k->up, c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), center = vadd(k->pos, vmul(up, R));
    k->timer++;
    int grounded = 0;
    { /* on the ground? a sphere a little lower touches */
        V3 probe = vsub(center, vmul(up, 4.0f));
        Hit h[4];
        grounded = kcl_query(probe, R, h, 4) > 0;
    }
    switch (k->phase) {
    case 0: /* Wait */
        if (d < 1500.0f && grounded && !mario->hurt_timer) {
            k->vel = vadd(k->vel, vmul(up, sqrtf(2.0f * 70.0f * rate)));
            k->phase = 1; k->timer = 0;
        }
        break;
    case 1: if (k->timer >= 60) { k->phase = 2; k->timer = 0; } /* Jump (its Search animation) */
        /* fall through */
    case 2: { /* Chase */
        if (k->phase == 1 && k->timer < 20) break;
        V3 dir = vtangent(to, up);
        if (vlen(dir) > 1e-3f) {
            dir = vnorm(dir);
            float sp = vlen(vtangent(k->vel, up)), cr = sinf(game_frame / 20.0f) * (sp / 4.0f < 1.0f ? sp / 4.0f : 1.0f);
            float a = 0.0174533f * 45.0f * cr;
            dir = vadd(vmul(dir, cosf(a)), vmul(vcross(up, dir), sinf(a)));
            if (vdot(dir, k->vel) <= 0 || sp < 4.0f) k->vel = vadd(k->vel, vmul(dir, 0.1f));
        }
        if (k->phase == 2 && d > 1500.0f) { k->phase = 0; k->timer = 0; }
        break;
    }
    default: if (k->timer > 120) { k->phase = 2; k->timer = 0; } break; /* CollidePlayer */
    }
    /* udpateInfluence: gravity, ground brake, the move */
    k->vel = vsub(k->vel, vmul(up, k->phase == 1 ? rate : grounded ? 0.2f : 0.8f));
    if (grounded) k->vel = vmul(k->vel, k->phase == 2 || k->phase == 1 ? 0.96f : 0.9f);
    if (vlen(k->vel) > 12.0f) k->vel = vmul(vnorm(k->vel), 12.0f);
    if (k->phase != 2 && k->phase != 1 && grounded && vlen(k->vel) < 0.5f) k->vel = v3(0, 0, 0);
    V3 nc = vadd(center, k->vel), gn, wn;
    int wall = 0;
    int g2 = collide_n(&nc, up, R, &gn, &wn, &wall);
    if (g2 && vdot(k->vel, gn) < 0) k->vel = vsub(k->vel, vmul(gn, vdot(k->vel, gn) * (k->phase == 1 ? 1.6f : 1.0f)));
    if (wall && vdot(k->vel, wn) < 0) k->vel = vsub(k->vel, vmul(wn, vdot(k->vel, wn)));
    k->to += vlen(vtangent(vsub(nc, center), up)) / R; /* rotateQuatRollBall */
    k->pos = vsub(nc, vmul(up, R));
    if (vlen(vsub(c, vadd(k->pos, vmul(up, R)))) < 115.2f + HIT_BODY_R && !mario->hurt_timer && k->phase != 3) {
        hurt_mario(vadd(k->pos, vmul(up, R)), 25.0f); /* sendMsgEnemyAttackStrong, doAttack */
        V3 away = vtangent(vsub(k->pos, mario->pos), up);
        k->vel = vadd(vmul(up, 5.0f), vlen(away) > 1e-3f ? vnorm(away) : v3(0, 0, 0));
        k->phase = 3; k->timer = 0;
        printf("unizo row %d hits Mario" "\n", k->row);
    }
}

/* Poihana.cpp: an ape at its home. Mario within 800 (sSearchRadius) makes it turn to him (4 degrees a frame, 30
 * frames) and chase him (0.5 a frame up to 10, turning 4); farther than 1100 it walks home (0.5 up to 5). Mario on
 * the ground within 100 in front is caught (ShootUpCharge, 20 frames) and thrown up at sqrt(2 x arg0) (default 1000);
 * caught in the air he goes up at once. Idle it walks round home (WalkAround: 120 frames every 180). A spin knocks it
 * out (Shock, then Swoon 110 frames, Recover 30); a stomp bounces Mario off. Ground friction x 0.95 ahead, x 0.8 to the
 * side. phase 0 wait, 1 search, 2 chase, 3 charge, 4 back home, 5 walk around, 6 shock / swoon; from its turn from its
 * front (degrees); timer its frames. ponytail: no sleep, no drowning, no back attack flip, no star pointer swoon */
static V3 stink_front(const Kuribo *k);
static void poihana_update(Kuribo *k, V3 to, float d) /* to: unused, Mario is the target */
{
    (void)to;
    const Row *r = &sc->objs[k->row];
    V3 up = k->up, f = stink_front(k), home = row_pos(r);
    k->timer++;
    float speed_max = 0, accel = 0;
    V3 target = mario->pos;
    if (k->phase == 6) { /* Shock / Swoon / Recover */
        if (k->timer >= 12 + 110 + 30) { k->phase = 0; k->timer = 0; }
    } else if (mario->spin_timer > 0 && d < 250.0f) { /* isMsgPlayerHitAll: tryShock */
        k->phase = 6; k->timer = 0; k->vel = v3(0, 0, 0);
        printf("poihana row %d: knocked out" "\n", k->row);
    }
    switch (k->phase) {
    case 0:
        if (d < 800.0f) { k->phase = 1; k->timer = 0; }
        else if (k->timer >= 180) { k->phase = 5; k->timer = 0; }
        break;
    case 1: if (k->timer >= 30) { k->phase = 2; k->timer = 0; } break;
    case 2: speed_max = 10.0f; accel = 0.5f; if (d > 1100.0f) { k->phase = 4; k->timer = 0; } break;
    case 3:
        if (k->timer >= 20) { /* ShootUp: Mario thrown up */
            float h = r->arg[0] > 0 ? (float)r->arg[0] : 1000.0f;
            mario->vup = sqrtf(2.0f * h); mario->hvel = v3(0, 0, 0); mario->grounded = 0; mario->jump_kind = 0;
            printf("poihana row %d: throws Mario up at %d" "\n", k->row, (int)mario->vup);
            k->phase = 4; k->timer = 0;
        } else { mario->pos = vadd(k->pos, vmul(f, 100.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; } /* held */
        break;
    case 4: target = home; speed_max = 5.0f; accel = 0.5f;
        if (vlen(vtangent(vsub(home, k->pos), up)) < 100.0f) { k->phase = 0; k->timer = 0; }
        else if (d < 800.0f && k->timer > 120) { k->phase = 1; k->timer = 0; }
        break;
    case 5: { /* WalkAround: a circle 350 round home */
        float a = game_frame * 0.02f + k->row;
        V3 x, z;
        row_axes(r, &x, &z);
        target = vadd(home, vadd(vmul(vnorm(vtangent(x, up)), 350.0f * cosf(a)), vmul(vnorm(vtangent(z, up)), 350.0f * sinf(a))));
        speed_max = 5.0f; accel = 0.5f;
        if (d < 800.0f) { k->phase = 1; k->timer = 0; } else if (k->timer >= 120) { k->phase = 0; k->timer = 0; }
        break;
    }
    }
    if (k->phase == 1 || k->phase == 2 || k->phase == 4 || k->phase == 5) { /* turnDirectionToTargetUseGroundNormalDegree */
        V3 t = vtangent(vsub(target, k->pos), up);
        if (vlen(t) > 1e-3f) {
            float turn = atan2f(vdot(t, vcross(up, f)), vdot(t, f)) * 57.29578f, rate = k->phase == 4 || k->phase == 5 ? 2.0f : 4.0f;
            k->from += turn > rate ? rate : turn < -rate ? -rate : turn;
            f = stink_front(k);
        }
    }
    if (accel > 0 && vdot(k->vel, f) < speed_max) k->vel = vadd(k->vel, vmul(f, accel));
    V3 ahead = vmul(f, vdot(k->vel, f)), side = vsub(vtangent(k->vel, up), ahead); /* controlVelocity */
    k->vel = vadd(vmul(ahead, 0.95f), vmul(side, 0.8f));
    if (k->phase == 3 || k->phase == 6) k->vel = v3(0, 0, 0);
    V3 center = vadd(vadd(k->pos, k->vel), vmul(up, 125.0f - 2.0f)), gn, wn;
    int wall = 0;
    collide_n(&center, up, 125.0f, &gn, &wn, &wall);
    k->pos = vsub(center, vmul(up, 125.0f));
    /* Mario: caught in front, bounced off its head */
    V3 rel = vsub(mario->pos, k->pos);
    float above = vdot(rel, up), fw = vdot(rel, f);
    if ((k->phase == 2 || k->phase == 1) && vlen(vtangent(rel, up)) < 100.0f + HIT_BODY_R && fw > 0 && above < 150.0f && above > -60.0f &&
        !mario->hurt_timer) { /* AutoRushBegin: on the ground a charge, in the air thrown at once */
        k->phase = 3; k->timer = mario->grounded ? 0 : 20;
        printf("poihana row %d: catches Mario" "\n", k->row);
    } else if (mario->vup < 0 && above > 150.0f && above < 300.0f && vlen(vtangent(rel, up)) < 150.0f) { /* trampled */
        mario->vup = 15.0f; mario->grounded = 0;
    }
}

static void butterfly_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 home = row_pos(r);
    k->timer++;
    if (!k->dir && r->arg[1] < 0 && cursor_sees(k->pos, 10.0f)) { /* tryAppearStarPeace */
        drop_pieces(k->pos, k->up, 1);
        k->dir = 1;
        printf("butterfly %d: star piece" "\n", k->row);
    }
    V3 o, d;
    cursor_ray(&o, &d);
    V3 to = vsub(k->pos, o), off = vsub(to, vmul(d, vdot(to, d)));
    if (k->phase == 0 && vdot(to, d) > 0 && vlen(off) < 100.0f && vlen(vsub(k->pos, home)) < 500.0f) { /* tryRunAway */
        k->vel = vmul(vlen(off) > 1.0f ? vnorm(off) : k->up, 12.0f);
        k->phase = 1; k->timer = 0;
    }
    if (k->phase == 1) {
        k->vel = vmul(k->vel, 0.97f);
        if (k->timer >= 30) { k->phase = 0; k->timer = 0; }
    } else {
        k->vel = vadd(vmul(k->vel, 0.99f), vmul(vsub(home, k->pos), 0.01f)); /* sBackToHomeAccel */
        if (vlen(k->vel) > 2.0f) k->vel = vmul(vnorm(k->vel), 2.0f);
    }
    V3 center = vadd(k->pos, k->vel);
    collide(&center, k->up, 30.0f);
    k->pos = center;
}

static void update_kuribo(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->state == KURIBO_DEAD || !row_shown(k->row)) continue;
        k->up = up_at(k->pos, k->up);
        V3 to = vsub(mario->pos, k->pos);
        float d = vlen(to);
        if (d > 3000.0f) continue; /* sActiveDistance: far ones wait (their zone may be loaded low, no collision) */
        if (sc->objs[k->row].zone < 64 && !zone_kcl[sc->objs[k->row].zone]) continue; /* no ground yet: they would fall */
        int tako = k->kind == WALK_TAKOHEI || k->kind == WALK_MERAMERA || k->kind == WALK_KMINI; /* KuriboMini.cpp:
                                                                             * a spin blows it down (requestBlowDown);
                                                                             * Meramera.cpp: a stomp, hip drop or spin
                                                                             * kills it, a touch burns (requestAttack) */
        if (k->kind == WALK_TAKOBO) { takobo_update(k, to, d); continue; }
        if (k->kind == WALK_MKOOPA) { mkoopa_update(k, to, d); continue; }
        if (k->kind == WALK_KARIKARI) { kari_update(k, to, d); continue; }
        if (k->kind == WALK_KARON) { karon_update(k, to, d); continue; }
        if (k->kind == WALK_TERESA) { teresa_update(k, to, d); continue; }
        if (k->kind == WALK_BUTTERFLY) { butterfly_update(k); continue; }
        if (k->kind == WALK_STINKBUG) { stink_update(k, to, d); continue; }
        if (k->kind == WALK_UNIZO) { unizo_update(k, to, d); continue; }
        if (k->kind == WALK_POIHANA) { poihana_update(k, to, d); continue; }
        if (k->kind == WALK_SPIDER) { /* JumpSpider.cpp: hops at Mario; ponytail: hop size and rate guessed (Petari's
                                        * hJumpDistance / hWaitTime are not in the decompilation) */
            V3 vu = vmul(k->up, vdot(k->vel, k->up)), vh = vsub(k->vel, vu);
            vu = vsub(vu, vmul(k->up, 1.5f));
            V3 center = vadd(vadd(k->pos, vmul(k->up, 45.0f)), vadd(vh, vu));
            int ground = collide(&center, k->up, 45.0f);
            k->pos = vsub(center, vmul(k->up, 45.0f));
            if (ground && vdot(vu, k->up) <= 0) { /* hGroundDamp; on the ground it may hop again */
                vh = vmul(vh, 0.8f); vu = v3(0, 0, 0);
                if (++k->timer > 60 && d < 1200.0f) {
                    vh = vmul(vnorm(vtangent(to, k->up)), 10.0f); vu = vmul(k->up, 18.0f); k->timer = 0;
                }
            } else
                vh = vmul(vh, 0.98f); /* hAirDampH */
            k->vel = vadd(vh, vu);
            float above = vdot(to, k->up);
            if (d < 130.0f && mario->vup < 0 && above > 40.0f) {
                k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); pop_coins(k->pos, k->up, 1);
                mario->vup = 15.0f; mario->grounded = 0;
            } else if (d < 170.0f && mario->spin_timer > 0) {
                k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); pop_coins(k->pos, k->up, 1);
            } else if (d < 110.0f && above <= 40.0f && !ground) /* hurts while jumping, pushes otherwise */
                hurt_mario(k->pos, 15.0f);
            continue;
        }
        if (k->kind == WALK_PACKUN) { /* PackunPetit.cpp: stands; within reach it bites (head sensor 50) */
            float above = vdot(to, k->up);
            k->timer = d < 300.0f ? k->timer + 1 : 0; /* in reach: 70 frames winding up, then a 20-frame bite */
            int bite = k->timer % 90 >= 70;
            if (!bite && d < 150.0f && mario->vup < 0 && above > 60.0f) { /* TrampleDown */
                k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); mario->vup = 15.0f; mario->grounded = 0;
                pop_coins(k->pos, k->up, 1);
            } else if (!bite && d < 200.0f && mario->spin_timer > 0) { /* PunchDown */
                k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); pop_coins(k->pos, k->up, 1);
            } else if (bite) {
                V3 head = vadd(vadd(k->pos, vmul(k->up, 120.0f)), vmul(vnorm(vtangent(to, k->up)), 80.0f));
                if (vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), head)) < 50.0f + HIT_BODY_R)
                    hurt_mario(head, 20.0f);
            }
            continue;
        }
        if (k->state == KURIBO_STUN) {
            if (--k->timer <= 0) k->state = KURIBO_WALK;
        } else if (d < (tako ? 1600.0f : k->kind == WALK_KOTEBUG ? 1000.0f : 1500.0f)) { /* chase on the surface (ponytail: no turn rate, no notice
                                                         * animation); TakoHei.cpp: sFindDistance, sPursueAccel 0.5,
                                                         * sNormalGroundFrec 0.85 */
            V3 dir = vnorm(vtangent(to, k->up));
            k->vel = tako ? vadd(vmul(k->vel, 0.85f), vmul(dir, 0.5f)) : vadd(vmul(k->vel, 0.8f), vmul(dir, 4.0f * 0.2f));
        } else {
            k->vel = vmul(k->vel, tako ? 0.85f : 0.8f);
        }
        V3 center = vadd(vadd(k->pos, vmul(k->up, 45.0f)), vsub(k->vel, vmul(k->up, 6.0f)));
        collide(&center, k->up, 45.0f);
        k->pos = vsub(center, vmul(k->up, 45.0f));
        /* stomp: Mario falling onto its top; spin: stun; side touch: Mario knocked back */
        float above = vdot(to, k->up);
        /* TakoHei: a spin (requestPunch) kills it too, and the hip drop (requestHipDropDown). KoteBug.cpp: a stomp or
         * a spin overturns it for 240 frames (hOverturnTime), the next one while it lies on its back kills it; a hip
         * drop kills it at once */
        int kote = k->kind == WALK_KOTEBUG;
        if (d < (tako ? 170.0f : 130.0f) && mario->vup < 0 && above > 40.0f) {
            if (kote && k->state == KURIBO_WALK && mario->hip != HIP_FALL) { k->state = KURIBO_STUN; k->timer = 240; }
            else if (kote && k->state == KURIBO_STUN && k->timer > 210 && mario->hip != HIP_FALL)
                continue; /* the bounce off the first stomp lands on it again: not a second stomp yet, no bounce */
            else { k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); pop_coins(k->pos, k->up, 1); }
            mario->vup = 15.0f; mario->grounded = 0;
        } else if (d < 170.0f && mario->spin_timer > 0 && (k->state == KURIBO_WALK || (kote && k->timer < 230))) {
            if (tako || (kote && k->state == KURIBO_STUN)) { k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, k->pos, k->up); pop_coins(k->pos, k->up, 1); }
            else { k->state = KURIBO_STUN; k->timer = kote ? 240 : 180; }
        } else if (d < (tako ? 140.0f : 100.0f) && above <= 40.0f && k->state == KURIBO_WALK) /* not from above */
            hurt_mario(k->pos, 15.0f);
    }
}

/* Breakables (breakable()): Mario's spin near one, or his hip drop landing on it, breaks it; collision goes at once,
 * then its drops appear. ponytail: no upper punch, drops appear where the box was instead of popping out, a broken
 * HipDropRock is gone (no wreck left), no star piece on a switch kill */
/* n star pieces in a ring around p, 120 up. ponytail: they stay put (appearStarPiece throws them about) */
static void drop_pieces(V3 p, V3 up, int n)
{
    for (int k = 0; k < n; k++) {
        float a = 6.2831853f * k / n + 0.5f;
        V3 side = vnorm(vtangent(v3(cosf(a), 0.3f, sinf(a)), up));
        pieces[npieces].pos = vadd(p, vadd(vmul(up, 120.0f), vmul(side, 90.0f)));
        pieces[npieces].row = -1;
        pieces[npieces++].alive = 1;
    }
}

static void break_inst(const Inst *in, int drops)
{
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    const Row *r = &sc->objs[in->row];
    int n, sp;
    breakable(sc->types + 32 * r->type, r, &n, &sp);
    if (!drops) n = sp = 0;
    V3 up = up_at(p, mario->up);
    pop_coins(p, up, n);
    drop_pieces(p, up, sp); /* WoodBox arg2 */
    row_kill(in->row);
    const char *name = sc->types + 32 * r->type;
    if (in->breaks != 3) /* ItemBlocks stay */
        fx_at(strncmp(name, "CrystalCage", 11) == 0 ? FX_CRYSTALCAGES_BREAK : FX_PUNCHBOX_BREAK, p, up);
    printf("%s %d broken, %d coins, %d star pieces\n", name, in->row, n, sp);
}

static void break_boxes(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, BODY_RADIUS));
    V3 head = vadd(mario->pos, vmul(mario->up, 2.0f * BODY_RADIUS + 60.0f));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (in->breaks != 3) continue;
        /* ItemBlock: Mario rising into it from below (its sensor: radius 100, 100 up its axis) */
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        V3 d = vsub(vadd(p, vmul(mario->up, 100.0f * in->s)), head);
        if (mario->vup > 0 && vdot(d, mario->up) > -40.0f && vdot(d, mario->up) < 120.0f * in->s &&
            vlen(vtangent(d, mario->up)) < 110.0f * in->s) {
            mario->vup = 0;
            break_inst(in, 1);
            game_rebind();
            return;
        }
    }
    for (int i = 0; i < ninsts; i++)
        if (insts[i].breaks == 1 && (row_step[insts[i].row] & 0x7FFF)) row_step[insts[i].row]--;
    if (mario->spin_timer <= 0 && !hip_hit)
        return;
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->breaks || in->breaks == 3) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        if (hip_hit ? i != hip_inst : in->breaks == 2 || vlen(vsub(c, p)) > in->reach + 120.0f) continue;
        unsigned short *hit = &row_step[in->row]; /* CrystalCageL: 0x8000 hit once, low bits frames until the next */
        if (strcmp(sc->types + 32 * sc->objs[in->row].type, "CrystalCageL") == 0 && !(*hit & 0x8000)) {
            *hit = 0x8000 | (SPIN_TIME + 20);
            printf("CrystalCageL %d hit\n", in->row);
            continue;
        }
        if (*hit & 0x7FFF) continue;
        break_inst(in, 1);
        game_rebind();
        return;
    }
}

/* PicketSwitch.cpp / HipDropSwitch.cpp: a hip drop landing on it turns SW_A on; HipDropTimerSwitch.cpp: for arg0
 * frames (default 300, mTimerMax; row_step counts them down), then off. ponytail: the stake does not sink */
static void update_hip_switches(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->hipsw) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        V3 d = vsub(mario->pos, v3(in->m[12], in->m[13], in->m[14])); /* on it: over its footprint (the ground
                                                                         * instance may be the planet around it) */
        int on = hip_inst == i || (fabsf(vdot(d, mario->up)) < in->reach && vlen(vtangent(d, mario->up)) < 0.7f * in->reach);
        if (hip_hit && on && !sw_on(r, SW_A)) {
            sw_set(r, SW_A, 1);
            if (in->hipsw == 2) *t = r->arg[0] > 0 ? r->arg[0] : 300;
            printf("%s %d on\n", sc->types + 32 * r->type, in->row);
        } else if (in->hipsw == 2 && *t && --*t == 0)
            sw_set(r, SW_A, 0);
    }
}

/* Mario standing on a moving instance: his position in its old pose put back in the new one */
static void carry(const float old[16], const Inst *in)
{
    V3 d = vsub(mario->pos, v3(old[12], old[13], old[14]));
    float k = 1.0f / (in->s * in->s), l[3] = {vdot(d, v3(old[0], old[1], old[2])) * k,
                                             vdot(d, v3(old[4], old[5], old[6])) * k,
                                             vdot(d, v3(old[8], old[9], old[10])) * k};
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    for (int j = 0; j < 3; j++) p = vadd(p, vmul(v3(in->m[4 * j], in->m[4 * j + 1], in->m[4 * j + 2]), l[j]));
    mario->pos = p;
}

/* Rotating map parts (MapPartsRotator): RotateSpeed * 0.01 degrees a frame; RotateAngle 0 turns for ever, else it
 * turns that far, holds RotateStopTime frames, then RotateType 0 stops, 1 swings back, 2 goes on; MoveConditionType
 * other than 0 waits for Mario on it. Mario standing on one (last frame's ground) is carried along. ponytail: constant
 * speed (no RotateAccelType easing), no start sign motion */
static void update_rotors(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->rotor) continue;
        const Row *r = &sc->objs[in->row];
        int row = in->row, packed = r->arg[7], type = packed & 15, cond = packed >> 8 & 15, hold = packed >> 12 & 0x3FFF;
        if (!row_turn[row]) {
            if (cond && mario_ground != i) continue;
            row_turn[row] = 1;
        }
        if (row_turn[row] == 2) continue; /* RotateType 0: stopped at its end */
        if (row_hold[row]) { row_hold[row]--; continue; }
        float speed = r->arg[4] * 0.01f, range = r->arg[5] > 0 ? (float)r->arg[5] : 0, sg = speed > 0 ? 1.0f : -1.0f;
        int dir = row_turn[row];
        float old[16], prev = row_angle[row], a = prev + fabsf(speed) * sg * dir;
        memcpy(old, in->m, 64);
        if (range > 0 && type == 2) { /* goes on: holds at every multiple of the angle */
            if (floorf(a * sg / range) != floorf(prev * sg / range)) { a = sg * ceilf(prev * sg / range) * range; row_hold[row] = hold; }
        } else if (range > 0 && ((dir > 0 && a * sg >= range) || (dir < 0 && a * sg <= 0))) { /* the end of a turn */
            a = dir > 0 ? sg * range : 0;
            row_turn[row] = type == 0 ? 2 : -dir;
            row_hold[row] = hold;
        }
        if (range == 0 || type == 2) a = fmodf(a, 360.0f);
        row_angle[row] = a;
        rotor_matrix(in);
        if (in->railer) railer_place(in);
        if (mario_ground == i) carry(old, in);
    }
}

/* MapPartsRailMover.cpp: a map part on a rail moves along it at the speed of the last point that set one (point_arg0),
 * waits point_arg5 frames at a point, and at an end MoveStopType (path_arg1, default 1): 0 stops, 1 turns back, 2
 * starts over, 3 vanishes; MoveConditionType other than 0 waits for Mario on it. It carries Mario like a rotating
 * part. ponytail: no acceleration (point_arg1), no time-based speed (point_arg7), no rail posture */
static void update_railers(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->railer) continue;
        const Row *r = &sc->objs[in->row];
        const unsigned char *e = in->railer;
        int row = in->row, cond = r->arg[7] >> 8 & 15, stop = *(const int *)(e + 16);
        u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
        const unsigned char *pa = e + 24 + 12 * np + 12 * nr; /* nr x {f32 coord, s32 arg0, s32 arg5} */
        float len = *(const float *)(pa + 12 * (nr - 1));
        if (!row_rdir[row]) {
            if (cond && mario_ground != i) continue;
            row_rdir[row] = 1;
        }
        if (row_rdir[row] == 2) continue;
        if (row_rhold[row]) { row_rhold[row]--; continue; }
        float c = row_coord[row];
        int dir = row_rdir[row];
        for (u32 k = 0; k < nr; k++) /* the speed of the last point behind it that sets one */
            if (*(const float *)(pa + 12 * k) <= c + 0.01f && *(const int *)(pa + 12 * k + 4) >= 0)
                row_speed[row] = (float)*(const int *)(pa + 12 * k + 4);
        float n = c + row_speed[row] * dir, old[16];
        memcpy(old, in->m, 64);
        for (u32 k = 1; k + 1 < nr; k++) { /* passing a point that holds */
            float pc = *(const float *)(pa + 12 * k);
            int hold = *(const int *)(pa + 12 * k + 8);
            if (hold > 0 && ((c < pc && n >= pc) || (c > pc && n <= pc))) { n = pc; row_rhold[row] = hold; break; }
        }
        if (n >= len || n <= 0) { /* an end */
            int type = stop >= 0 ? stop : 1;
            n = n >= len ? len : 0;
            int hold = *(const int *)(pa + 12 * (n > 0 ? nr - 1 : 0) + 8);
            if (hold > 0) row_rhold[row] = hold;
            if (type == 0) row_rdir[row] = 2;
            else if (type == 1) row_rdir[row] = -dir;
            else if (type == 2) n = 0;
            else { row_kill(row); game_rebind(); return; }
        }
        row_coord[row] = n;
        railer_place(in);
        if (mario_ground == i) carry(old, in);
    }
}

static void update_flips(void)
{
    for (int i = 0; i < ninsts; i++) {
        if (!insts[i].flip) continue;
        unsigned short *st = &row_step[insts[i].row];
        if (*st & FLIP_DONE) continue;
        /* on it: grounded over its footprint (ground_inst may be the planet under a flush panel) */
        V3 d = vsub(mario->pos, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]));
        int on = mario->grounded && fabsf(vdot(d, mario->up)) < insts[i].reach * 0.5f && /* origin under the top */
                 vlen(vtangent(d, mario->up)) < 0.7f * insts[i].reach;
        if ((*st & FLIP_LOCK) && !on && mario->grounded) *st &= ~FLIP_LOCK;
        if ((*st & FLIP_LOCK) || !on) { *st &= FLIP_BACK | FLIP_LOCK; continue; }
        if (*st / FLIP_STEP < 2) { *st += FLIP_STEP; continue; }
        *st = ((*st & FLIP_BACK) ^ FLIP_BACK) | FLIP_LOCK;
    }
}

/* CollapsePlane (Petari CollapsePlane.cpp): once stood on, the plane shrinks in x / z (1 - 0.7 step / time) and after
 * `time` frames vanishes with its collision, for good. ponytail: the collision keeps its full size while shrinking,
 * no star pointer stop */
static void update_collapse(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->collapse || !row_step[in->row]) continue;
        if (++row_step[in->row] >= in->collapse) {
            row_dead[in->row] = 1;
            game_rebind();
            return;
        }
    }
}

/* SpaceMine (Petari SpaceMine.cpp): sensor radius 100; touching it is an explosion on Mario and the mine is gone.
 * NeedlePlant (NeedlePlant.cpp): sensor 150 * scale above its base, radius 70 * scale; touching it pricks Mario, the
 * plant stays. BlackHole / BlackHoleCube (BlackHole.cpp): inside the sphere of radius 500 * scale, or the cube's box
 * of +-500 * scale, Mario is inhaled: a miss. ponytail: no star-piece shots or invincibility kills, the mine does not
 * spin, no inhale demo, black holes never switch off */
static void update_hazards(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->hazard) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        float r = 100.0f;
        if (in->hazard >= 3) { /* black hole: sphere 500 * scale, or Mario's sensor inside the cube's box */
            if (mario->launch >= 0) continue; /* carried by a launch star: bound, nothing inhales him */
            const Row *rw = &sc->objs[in->row];
            V3 d = vsub(c, p), x, z;
            row_axes(rw, &x, &z);
            int in_hole = in->hazard == 3 ? vlen(d) < 500.0f * rw->scale[0] + HIT_BODY_R
                                          : fabsf(vdot(d, x)) < 500.0f * rw->scale[0] &&
                                                fabsf(vdot(d, vcross(z, x))) < 500.0f * rw->scale[1] &&
                                                fabsf(vdot(d, z)) < 500.0f * rw->scale[2];
            if (in_hole) { printf("black hole %d\n", in->row); miss(); return; }
            continue;
        }
        if (in->hazard == 2) {
            p = vadd(p, vmul(up_at(p, mario->up), 150.0f * in->s));
            r = 70.0f * in->s;
        }
        if (vlen(vsub(c, p)) > r + HIT_BODY_R) continue;
        hurt_mario(p, in->hazard == 1 ? 25.0f : 15.0f);
        if (in->hazard == 1) {
            fx_emit(FX_SPACEMINE_EXPLOSION, in->m);
            row_kill(in->row);
            printf("SpaceMine %d exploded\n", in->row);
            game_rebind();
            return;
        }
    }
}

/* AreaObj forms (Petari AreaForm.cpp; NameObjFactory: Switch / Death Cube base origin, Sphere, Cylinder base origin):
 * 0 box from the base (1000 x scale, y from 0), 1 cylinder from the base (radius 500 x scale_x, height 1000 x
 * scale_y), 2 sphere (radius 500 x scale_x); -1 not an area */
static int area_shape(const char *name)
{
    const char *f = strstr(name, "Cube") ? "Cube" : strstr(name, "Cylinder") ? "Cylinder" : strstr(name, "Sphere") ? "Sphere" : 0;
    return !f ? -1 : f[1] == 'u' ? 0 : f[1] == 'y' ? 1 : 2;
}

static int area_inside(const Row *r, V3 p)
{
    V3 d = vsub(p, row_pos(r)), x, z;
    int shape = area_shape(sc->types + 32 * r->type);
    if (shape == 2) return vlen(d) < 500.0f * r->scale[0];
    row_axes(r, &x, &z);
    V3 y = vcross(z, x);
    float h = vdot(d, y);
    if (h < 0.0f || h > (shape == 1 ? 500.0f : 1000.0f) * r->scale[1]) return 0; /* AreaFormCylinder: 500 x scale_y */
    if (shape == 1) return vlen(vtangent(d, y)) < 500.0f * r->scale[0];
    return fabsf(vdot(d, x)) < 500.0f * r->scale[0] && fabsf(vdot(d, z)) < 500.0f * r->scale[2];
}

/* CollisionBlocker (above). TimerSwitch.cpp: SW_B on starts arg0 frames, then SW_A goes on and the timer is gone. SwitchSynchronizer.cpp
 * (Reverse): SW_A follows the opposite of SW_B. GroupSwitchWatcher.cpp: SW_A on once all (arg1 -1) or any one (arg1
 * 0) of the arg0 switches from SW_B on are on; action arg2 -1 does it once, else SW_A follows the group. Then a
 * changed switch rebinds the instances its appear switch shows or hides. */
static void update_switches(void)
{
    for (int i = 0; i < nlogic; i++) {
        int row = logic[i].row;
        const Row *r = &sc->objs[row];
        if (row_dead[row]) continue;
        if (logic[i].kind == LOGIC_SWAREA) { /* SwitchArea.cpp */
            int in = area_inside(r, mario->pos);
            if (r->arg[2] != -1 && !mario->grounded) continue;                          /* isUpdate: on ground */
            if (r->arg[0] == -1 && sw_on(r, SW_A) != (r->arg[1] != -1)) continue;      /* nothing left to do */
            if ((r->sw[SW_B] < 0 || sw_on(r, SW_B)) && in) sw_set(r, SW_A, r->arg[1] == -1);
            else if (r->arg[0] != -1) sw_set(r, SW_A, 0);                              /* only while inside */
            continue;
        }
        if (logic[i].kind == LOGIC_RESTART) { /* RestartCube.cpp: inside (on the ground unless arg3 is -1), a miss
                                                * brings Mario back to the start point of its zone with MarioNo arg0 */
            if (!row_shown(row) || (r->arg[3] != -1 && !mario->grounded) || !area_inside(r, mario->pos)) continue;
            for (u32 k = 0; k < sc->nstarts; k++)
                if (sc->starts[k].zone == r->zone && sc->starts[k].arg[0] == r->arg[0] &&
                    vlen(vsub(row_pos(&sc->starts[k]), start_pos)) > 1.0f) {
                    start_pos = row_pos(&sc->starts[k]);
                    printf("restart point %d (row %d)\n", r->arg[0], row);
                }
            continue;
        }
        if (logic[i].kind == LOGIC_DEATH) { /* DeathArea.cpp: forceKillPlayerByAbyss */
            if (row_shown(row) && area_inside(r, mario->pos)) { printf("death area %d\n", row); miss(); }
            continue;
        }
        if (logic[i].kind == LOGIC_PIECESPOT) { /* StarPieceSpot.cpp: arg0 star pieces (default 5) burst out when its
                                                  * appear switch goes on, or without one at a spin within 150 */
            V3 p = row_pos(r);
            if (r->sw[SW_APPEAR] >= 0 ? !sw_on(r, SW_APPEAR) :
                mario->spin_timer <= 0 || vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), p)) > 150.0f + HIT_BODY_R)
                continue;
            drop_pieces(p, up_at(p, mario->up), r->arg[0] >= 0 ? r->arg[0] : 5);
            row_dead[row] = 1;
            printf("star piece spot %d burst\n", row);
            continue;
        }
        if (logic[i].kind == LOGIC_CHIPS) { /* ChipGroup.cpp: SW_A once every chip of its arg0 is taken */
            const char *chip = sc->types[32 * r->type] == 'Y' ? "YellowChip" : "BlueChip";
            int n = 0, got = 0;
            for (u32 k = 0; k < sc->nobjs; k++)
                if (sc->objs[k].arg[0] == r->arg[0] && strcmp(sc->types + 32 * sc->objs[k].type, chip) == 0) {
                    n++;
                    got += row_dead[k];
                }
            if (n && got == n) {
                sw_set(r, SW_A, 1);
                row_dead[row] = 1;
                printf("%s %d complete\n", sc->types + 32 * r->type, r->arg[0]);
            }
            continue;
        }
        if (logic[i].kind == LOGIC_FLIPS) { /* FlipPanelObserver: all its group's panels on their back side */
            if (!row_shown(row) || r->group < 0) continue;
            int n = 0, back = 0;
            for (u32 k = 0; k < sc->nobjs; k++) {
                const Row *q = &sc->objs[k];
                const char *qn = sc->types + 32 * q->type;
                if (q->group != r->group || q->zone != r->zone ||
                    (strcmp(qn, "FlipPanel") != 0 && strcmp(qn, "FlipPanelReverse") != 0))
                    continue;
                n++;
                back += row_step[k] & FLIP_BACK;
            }
            if (n && back == n) {
                for (u32 k = 0; k < sc->nobjs; k++)
                    if (sc->objs[k].group == r->group && sc->objs[k].zone == r->zone) row_step[k] |= FLIP_DONE;
                sw_set(r, SW_A, 1);
                row_dead[row] = 1;
                printf("flip panels %d complete\n", r->group);
            }
            continue;
        }
        if (logic[i].kind == LOGIC_BLOCKER) { /* CollisionBlocker.cpp: a 50 * scale sphere pushing Mario out until SW_B */
            if (sw_on(r, SW_B)) { row_dead[row] = 1; continue; }
            if (!row_shown(row)) continue;
            V3 c = vadd(mario->pos, vmul(mario->up, BODY_RADIUS)), d = vsub(c, row_pos(r));
            float reach = 50.0f * r->scale[0] + BODY_RADIUS, l = vlen(d);
            if (l < reach && l > 1e-3f) mario->pos = vadd(mario->pos, vmul(d, (reach - l) / l));
        } else if (logic[i].kind == LOGIC_TIMER) { /* row_step: frames left + 1, 0 not started */
            if (!row_step[row] && r->arg[0] > 0 && sw_on(r, SW_B)) row_step[row] = r->arg[0] + 1;
            if (row_step[row] > 1 && --row_step[row] == 1) { sw_set(r, SW_A, 1); row_dead[row] = 1; }
        } else if (logic[i].kind == LOGIC_REVERSE) {
            sw_set(r, SW_A, !sw_on(r, SW_B));
        } else {
            int n = r->arg[0] > 0 ? r->arg[0] : 1, all = 1, any = 0;
            for (int k = 0; k < n; k++) {
                int on = sw_on_n(r, SW_B, k);
                all &= on; any |= on;
            }
            int hit = r->arg[1] == 0 ? any : all;
            if (r->arg[2] < 0) { if (hit && !sw_on(r, SW_A)) { sw_set(r, SW_A, 1); row_dead[row] = 1; } }
            else sw_set(r, SW_A, hit);
        }
    }
    if (sw_changed) {
        sw_changed = 0;
        for (int i = 0; i < ninsts; i++) { /* PunchBox::kill / CrystalCage::forceBreak on SW_A */
            const Row *r = &sc->objs[insts[i].row];
            if (insts[i].breaks && r->sw[SW_A] >= 0 && sw_on(r, SW_A) && !row_dead[insts[i].row] &&
                strncmp(sc->types + 32 * r->type, "Breakable", 9) != 0) /* BreakableCage writes its SW_A */
                break_inst(&insts[i], strcmp(sc->types + 32 * r->type, "PunchBox") != 0);
        }
        game_rebind();
    }
}

static void update_pipes(void)
{
    if (pipe_wait) { /* exeInvalid: re-armed once Mario is on ground after 30 frames */
        if (++pipe_wait > 30 && mario->grounded) {
            pipe_wait = 0;
            if (pipe_out >= 0) { row_step[pipes[pipe_out].row] = 0; pipe_out = -1; game_rebind(); } /* tryHideDown */
        }
        return;
    }
    if (mario->launch >= 0) return;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < npipes; i++) {
        const Pipe *p = &pipes[i];
        if (p->partner < 0 || vlen(vsub(c, vsub(p->top, vmul(p->up, 50.0f)))) > 70.0f + HIT_BODY_R) continue;
        V3 d = vsub(mario->pos, p->top);
        if (vlen(vtangent(d, p->up)) > 50.0f && vdot(d, p->up) < -5.0f) continue; /* beside the pipe, below its top */
        const Pipe *o = &pipes[p->partner];
        mario->pos = o->top;
        mario->up = o->up;
        mario->vup = o->vf; /* endBindAndPlayerForceWeakGravityJumpInputOff(Y * vert + Z * horiz) */
        mario->hvel = vmul(vtangent(o->front, o->up), o->hf);
        mario->grounded = 0; mario->jump_kind = 0; mario->air_timer = 0; mario->hip = 0;
        if (vlen(mario->hvel) > 0.1f) mario->face = vnorm(mario->hvel);
        if (mario_anim) mario_play("earthenpipejump", 1.0f, 0);
        pipe_wait = 1;
        pipe_zone = o->zone;
        if (o->mode == 1 || o->mode == 2) { /* tryShowUp: the hidden partner rises */
            row_step[o->row] = 1;
            pipe_out = o->mode == 1 ? p->partner : -1;
            game_rebind();
        }
        printf("pipe %d -> %d\n", i, p->partner);
        return;
    }
}

/* ElectricRail: sensors of radius 30 at the rail point nearest Mario (and every 100 above it for stacked rails) shock
 * him unless he squats. ponytail: no switch turning the rail off, no ElectricRailMoving */
static void update_electric(void)
{
    if (mario->squat) return;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < nerails; i++) {
        const Spark *e = &erails[i];
        V3 ab = vsub(e->b, e->a);
        float t = vdot(vsub(c, e->a), ab) / fmaxf(vdot(ab, ab), 1e-3f);
        V3 q = vadd(e->a, vmul(ab, fminf(fmaxf(t, 0.0f), 1.0f))), up = up_at(q, mario->up);
        for (int k = 0; k < e->h; k++)
            if (vlen(vsub(c, vadd(q, vmul(up, 100.0f * k)))) < 30.0f + HIT_BODY_R) {
                hurt_mario(q, 15.0f);
                return;
            }
    }
}

/* FireBar (Petari FireBar.cpp): arg7 sticks (default 1) of arg0 balls (default 5) 100 apart, the first arg5 (default
 * 140) out from the core and 50 up its Y axis; they turn 0.1 * arg1 degrees a frame about that axis from its Z
 * axis. A sensor of radius 30 at the point of a stick nearest Mario burns him. Ball k of stick j: */
static V3 firebar_ball(const Inst *in, int j, int k)
{
    const Row *r = &sc->objs[in->row];
    V3 x, z, c = v3(in->m[12], in->m[13], in->m[14]);
    row_axes(r, &x, &z);
    V3 y = vcross(z, x);
    int sticks = r->arg[7] > 0 ? r->arg[7] : 1;
    float a = (0.1f * r->arg[1] * game_frame + 360.0f * (j + 1) / sticks) * 0.0174533f;
    V3 dir = vadd(vmul(z, cosf(a)), vmul(x, sinf(a)));
    float d = (r->arg[5] >= 0 ? r->arg[5] : 140.0f) + 100.0f * k;
    return vadd(c, vadd(vmul(dir, d), vmul(y, 50.0f)));
}


static V3 rock_pos(const Roller *o, float *radius)
{
    const Row *r = &sc->objs[o->row];
    *radius = 225.0f * r->scale[0];
    V3 p = rail_at(o->pts, o->np, o->d);
    return vadd(p, vmul(up_at(p, mario->up), *radius)); /* the rail runs along the ground under the rock */
}

/* ShootingStar.cpp: it appears arg2 (default 2000) up its y from its place, growing for 20 frames, then falls 25 a
 * frame spinning 10 degrees a frame; on the ground it bursts into arg0 star pieces (default 5) of the arg3 it holds
 * (default 5), hides for arg1 frames (default 240) and starts over until it holds none; after 360 frames of falling it
 * starts over. row_turn: 0 appearing, 1 falling, 2 waiting; row_step its frames; row_hold the pieces left.
 * ponytail: the pieces are dropped in a ring (drop_pieces), not thrown */
static V3 shoot_pos(const Inst *in, float *k)
{
    const Row *r = &sc->objs[in->row];
    V3 axis = vnorm(v3(in->m[4], in->m[5], in->m[6]));
    float h = r->arg[2] >= 0 ? (float)r->arg[2] : 2000.0f;
    int t = row_step[in->row], ph = row_turn[in->row];
    *k = ph == 0 ? fminf(t / 20.0f, 1.0f) : ph == 1 ? 1.0f : 0.0f;
    return vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(axis, ph == 1 ? h - 25.0f * t : h));
}

static void update_shooting(void)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->shoot || !row_shown(in->row)) continue;
        int row = in->row;
        const Row *r = &sc->objs[row];
        unsigned short *t = &row_step[row];
        signed char *ph = &row_turn[row];
        ++*t;
        if (*ph == 0 && *t > 20) { *ph = 1; *t = 0; }
        else if (*ph == 2 && *t > (r->arg[1] >= 0 ? r->arg[1] : 240)) { *ph = 0; *t = 0; }
        else if (*ph == 1) {
            if (*t > 360) { *ph = 0; *t = 0; continue; }
            float k;
            V3 p = shoot_pos(in, &k), q = p, up = up_at(p, mario->up), gn = up, wn;
            int wall;
            if (!collide_n(&q, up, 100.0f, &gn, &wn, &wall) && !wall) continue;
            int n = r->arg[0] >= 0 ? r->arg[0] : 5;
            if (n > row_hold[row]) n = row_hold[row];
            drop_pieces(p, up, n);
            row_hold[row] -= n;
            *ph = 2; *t = 0;
            if (!row_hold[row]) row_dead[row] = 1;
            printf("shooting star %d burst %d, %d left\n", row, n, row_hold[row]);
        }
    }
}

/* LavaSteam.cpp: Wait 120 frames (its Sign effect the first 90), Steam 90 (burns: a 330 cylinder up its y, a sphere
 * of 70 at 400), SteamEnd 90, again; with SW_A it waits for the switch, then steams and cycles. row_turn: 0 wait, 1
 * steam, 2 end, 3 waiting for SW_A; row_step its frames. ponytail: a burn is an ordinary hurt */
static void update_steam(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->steam || !row_shown(in->row)) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        if (*ph == 0 && *t == 0 && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) *ph = 3; /* also back to waiting if it goes off */
        if (*ph == 3) {
            if (!sw_on(r, SW_A)) continue;
            *ph = 1; *t = 0;
        }
        ++*t;
        if (*ph == 0 && *t >= 120) { *ph = 1; *t = 0; }
        else if (*ph == 1 && *t >= 90) { *ph = 2; *t = 0; }
        else if (*ph == 2 && *t >= 90) { *ph = 0; *t = 0; }
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        int near = vlen(vsub(p, cam_pos)) < 6000.0f;
        if (*ph == 0 && *t < 90 && near) fx_keep(FX_LAVASTEAM_SIGN, t, in->m);
        if (*ph != 1) continue;
        if (near) fx_keep(FX_LAVASTEAM_STEAM, t, in->m);
        V3 d = vsub(c, p);
        float h = vdot(d, up), rr = 0.7f * HIT_BODY_R;
        if (((h >= 0.0f && h <= 330.0f && vlen(vsub(d, vmul(up, h))) < rr + 10.0f) ||
            (vlen(vsub(c, vadd(p, vmul(up, 400.0f)))) < rr + 70.0f)) && !mario->hurt_timer) {
            hurt_mario(p, 15.0f);
            printf("lava steam %d burns\n", in->row);
        }
    }
}

/* LavaGeyser.cpp: WaitSwitch (SW_A off), Wait arg0 frames (default 180), Sign 90 (its effect), ShootUp (BCK
 * LavaGeyserAppear, 60 frames), ShootKeep arg1 (default 180), ShootDown (LavaGeyserDisappear) hidden, Wait. Burns
 * along its y up to 100 under its Top joint, in a sphere of 100 x scale y. row_turn: 0 wait, 1 sign, 2 up, 3 keep,
 * 4 down, 5 waiting for SW_A; row_step its frames. ponytail: the column is the bind-pose model scaled along its y to
 * Top's height (the BCKs: smoothstep up to 1060, down as 1050 (1 - (t / 52)^2)), not the telescoping joints */
#define GEYSER_BIND 1145.0f /* Top's height in the bind pose */
static float geyser_height(const Inst *in)
{
    float t = row_step[in->row];
    switch (row_turn[in->row]) {
    case 2: t /= 60.0f; return 1060.0f * t * t * (3.0f - 2.0f * t);
    case 3: return 1060.0f;
    case 4: t /= 52.0f; return t < 1.0f ? 1050.0f * (1.0f - t * t) : 0.0f;
    default: return 0.0f;
    }
}

static void update_geyser(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->geyser || !row_shown(in->row)) continue;
        const Row *r = &sc->objs[in->row];
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        int wait = r->arg[0] > 0 ? r->arg[0] : 180, keep = r->arg[1] > 0 ? r->arg[1] : 180;
        if (*ph == 0 && r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) *ph = 5;
        if (*ph == 5) {
            if (!sw_on(r, SW_A)) continue;
            *ph = 1; *t = 0;
        }
        ++*t;
        if (*ph == 0 && *t >= wait) { *ph = 1; *t = 0; }
        else if (*ph == 1 && *t >= 90) { *ph = 2; *t = 0; }
        else if (*ph == 2 && *t >= 60) { *ph = 3; *t = 0; }
        else if (*ph == 3 && *t >= keep) { *ph = 4; *t = 0; }
        else if (*ph == 4 && *t >= 60) { *ph = 0; *t = 0; }
        V3 p = v3(in->m[12], in->m[13], in->m[14]), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        if (*ph == 1 && vlen(vsub(p, cam_pos)) < 6000.0f) fx_keep(FX_LAVAGEYSER_SIGN, t, in->m);
        float top = geyser_height(in) * in->s - 100.0f;
        if (top < 0.0f) continue;
        V3 d = vsub(c, p);
        float h = vdot(d, up);
        h = h < 0.0f ? 0.0f : h > top ? top : h;
        if (vlen(vsub(c, vadd(p, vmul(up, h)))) < 0.7f * HIT_BODY_R + 100.0f * r->scale[1] && !mario->hurt_timer) {
            hurt_mario(p, 15.0f);
            printf("lava geyser %d burns\n", in->row);
        }
    }
}

static void update_plants(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    int moving = vdot(mario->hvel, mario->hvel) >= 2.0f || mario->spin_timer > 0;
    for (int i = 0; i < ninsts; i++) /* groups whose ground was not loaded at their rebind: again while Mario is near */
        if (insts[i].plant && plant_first[insts[i].row] >= 0 && !plant_state[insts[i].row] &&
            vlen(vsub(c, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]))) < 4000.0f)
            place_plants(&insts[i]);
    for (int i = 0; moving && i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->plant || plant_first[in->row] < 0 || !row_shown(in->row) ||
            vlen(vsub(c, v3(in->m[12], in->m[13], in->m[14]))) > in->reach + 1000.0f)
            continue;
        const Row *r = &sc->objs[in->row];
        int n = plant_count(r);
        if (stone_row(r)) { /* SmallStone::tryBreak */
            if (mario->spin_timer <= 0 || plant_state[in->row] != 1) continue;
            V3 ctr = v3(0, 0, 0), up = v3(0, 0, 0);
            int m = 0;
            for (int k = 0; k < n; k++) {
                const Plant *p = &plants[plant_first[in->row] + k];
                if (p->ok) { ctr = vadd(ctr, p->pos); up = p->up; m++; }
            }
            if (!m || vlen(vsub(mario->pos, vadd(vmul(ctr, 1.0f / m), vmul(up, 120.0f)))) > 400.0f) continue;
            for (int k = 0; k < n; k++) {
                Plant *p = &plants[plant_first[in->row] + k];
                if (!p->ok) continue;
                fx_at(FX_PUNCHBOX_BREAK, p->pos, p->up);
                if (r->arg[2] >= 0) drop_pieces(p->pos, p->up, 1);
                p->ok = 0;
            }
            plant_state[in->row] = 2; /* broken */
            row_kill(in->row);
            printf("small stone %d broken" "\n", in->row);
            continue;
        }
        for (int k = 0; k < n; k++) {
            Plant *p = &plants[plant_first[in->row] + k];
            if (!p->ok || !p->item || vlen(vsub(c, p->pos)) > 50.0f * r->scale[1] + HIT_BODY_R) continue;
            p->item = 0;
            if (r->arg[2] == 1) drop_pieces(p->pos, p->up, 1);
            else pop_coins(p->pos, p->up, 1);
            printf("plant group %d: item out\n", in->row);
        }
    }
}

static void shell_explode(Shell *o)
{
    fx_at(FX_SPACEMINE_EXPLOSION, o->pos, up_at(o->pos, mario->up));
    o->state = 0;
}

static void update_shells(void)
{
    for (int i = 0; i < nshells; i += SHELLS_PER) { /* the launchers */
        int row = shells[i].row;
        const Row *r = &sc->objs[row];
        unsigned short *t = &row_step[row];
        V3 p = row_pos(r);
        if (!row_shown(row) || (r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) || vlen(vsub(p, mario->pos)) > 8000.0f) {
            *t = 0;
            continue;
        }
        if (++*t <= (r->arg[0] >= 0 ? r->arg[0] : 300)) continue;
        *t = 0;
        for (int k = 0; k < SHELLS_PER; k++) {
            Shell *o = &shells[i + k];
            if (o->state) continue;
            V3 x, z;
            row_axes(r, &x, &z);
            z = vmul(z, r->scale[2]);
            int point = strcmp(sc->types + 32 * r->type, "IronCannonLauncherPoint") == 0;
            o->pos = vadd(p, vmul(z, point ? 75.0f : 100.0f));
            o->vel = vmul(z, r->arg[1] >= 0 ? (float)r->arg[1] : 30.0f);
            o->dir = z;
            o->t = 0;
            o->state = 1;
            break;
        }
    }
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < nshells; i++) {
        Shell *o = &shells[i];
        if (!o->state) continue;
        o->t++;
        V3 up = up_at(o->pos, mario->up), gn = up, wn;
        int wall;
        if (o->state == 2) o->vel = vsub(o->vel, up);
        V3 center = vadd(o->pos, o->vel);
        int ground = collide_n(&center, up, 75.0f, &gn, &wn, &wall);
        if (o->state == 1) {
            if ((ground || wall) && o->t > 10) { shell_explode(o); continue; }
            o->pos = vadd(o->pos, o->vel);
            if (o->t >= 360) { o->state = 0; continue; }
        } else {
            o->pos = center;
            float into = -vdot(o->vel, gn);
            if (ground && into > 2.0f) o->vel = vadd(o->vel, vmul(gn, 1.55f * into)); /* reboundVelocityFromCollision */
            o->vel = vmul(o->vel, ground ? 0.95f : 0.99f);
            if (o->t >= 45) { o->state = 0; continue; } /* misfire */
        }
        if (o->state != 1 || vlen(vsub(c, o->pos)) > 60.0f + HIT_BODY_R) continue;
        if (mario->vup < 0 && vdot(vsub(mario->pos, o->pos), up) > 30.0f) { /* trampled */
            o->state = 2;
            o->t = 0;
            mario->vup = 15.0f; mario->grounded = 0;
        } else {
            hurt_mario(o->pos, 25.0f);
            shell_explode(o);
            printf("cannon shell hit Mario\n");
        }
    }
}

static void update_rocks(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < nrollers; i += ROCKS_PER) {
        const Row *r = &sc->objs[rollers[i].row];
        unsigned short *t = &row_step[rollers[i].row];
        if (!row_shown(rollers[i].row) || sw_on(r, SW_A)) { /* syncStageSwitchAppear / invalidate */
            for (int k = 0; k < ROCKS_PER; k++) rollers[i + k].alive = 0;
            continue;
        }
        int every = r->arg[1] > 0 ? r->arg[1] * 60 : 0, any = 0;
        for (int k = 0; k < ROCKS_PER; k++) any |= rollers[i + k].alive;
        if (every ? (*t)++ % every == 0 : !any)
            for (int k = 0; k < ROCKS_PER; k++)
                if (!rollers[i + k].alive) { rollers[i + k].alive = 1; rollers[i + k].d = 0; break; }
        for (int k = 0; k < ROCKS_PER; k++) {
            Roller *o = &rollers[i + k];
            if (!o->alive) continue;
            o->d += r->arg[0] > 0 ? (float)r->arg[0] : 10.0f;
            if (o->d >= o->len) { o->alive = 0; continue; } /* breaks at the rail's goal */
            float rad;
            V3 p = rock_pos(o, &rad);
            float dist = vlen(vsub(c, p));
            if (mario->spin_timer > 0 && dist < rad + 120.0f) { o->alive = 0; continue; } /* a spin breaks a rock */
            if (dist < rad + HIT_BODY_R) hurt_mario(p, 25.0f);
        }
    }
}

static void update_firebars(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->firebar) continue;
        const Row *r = &sc->objs[in->row];
        int sticks = r->arg[7] > 0 ? r->arg[7] : 1, balls = r->arg[0] > 0 ? r->arg[0] : 5;
        for (int j = 0; j < sticks; j++) {
            V3 a = firebar_ball(in, j, 0), ab = vsub(firebar_ball(in, j, balls - 1), a);
            float t = fminf(fmaxf(vdot(vsub(c, a), ab) / fmaxf(vdot(ab, ab), 1e-3f), 0.0f), 1.0f);
            V3 q = vadd(a, vmul(ab, t));
            if (vlen(vsub(c, q)) < 30.0f + HIT_BODY_R) { hurt_mario(q, 15.0f); return; }
        }
    }
}

/* PowerStar (Petari PowerStar.cpp: sensor 75 at its centre), BenefitItemOneUp / LifeUp (BenefitItemObj.cpp,
 * BenefitItemLifeUp.cpp: sensor 50, 50 above the base): touched, they are taken for good. LifeUp: 6 life until a miss.
 * ponytail: stars waiting for a demo or switch are out from the start, no get demo (the stage goes on), mushrooms
 * stand still instead of running off */
/* an item's sensor: centre (returned) and radius */
static V3 item_sensor(const Inst *in, float *r)
{
    int state;
    V3 p = in->item == ITEM_STAR ? star_pos(in, &state) : v3(in->m[12], in->m[13], in->m[14]);
    *r = (in->item == ITEM_STAR ? 75.0f : in->item == ITEM_QCOIN ? 150.0f : in->item == ITEM_CHIP ? 80.0f : in->item == ITEM_KEY ? 105.0f : 50.0f) * in->s;
    if (in->item == ITEM_STAR || in->item == ITEM_CHIP) return p;
    return vadd(p, vmul(up_at(p, mario->up), (in->item == ITEM_QCOIN ? 210.0f : in->item == ITEM_KEY ? 105.0f : 50.0f) * in->s));
}

/* An event star (star_wait) waits hidden, or shown inside the cage that declared it (CrystalCageL: 250 up the cage,
 * BreakableCage: 150, DummyDisplayModel); then the PowerStar.cpp appear demo (exeAppearDemoRise / Move): from there
 * (its own place for the hundred coins) 300 up its own y (ease out over 60 frames), at frame 80 off to its place in
 * 120 frames on an arc as high as half the distance across its up (300 to 5000); taken from frame 210. *state: 0
 * hidden, 1 waiting in its cage, 2 appearing, 3 can be taken. ponytail: no demo camera, the stage goes on */
/* PowerStar::setDemoAppearPos: an event star whose group (GroupId) has PowerStarAppearPoints comes out at the one
 * nearest its declarer, not at its own place */
static V3 star_appear_point(const Inst *in, V3 from, V3 own)
{
    const Row *s = &sc->objs[in->row];
    int t = scene_type(sc, "PowerStarAppearPoint");
    if (t < 0 || s->group < 0) return own;
    float best = 1e30f;
    V3 p = own;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != t || r->group != s->group || r->zone != s->zone) continue;
        float d = vlen(vsub(row_pos(r), from));
        if (d < best) { best = d; p = row_pos(r); }
    }
    return p;
}

static V3 star_pos(const Inst *in, int *state)
{
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    int w = star_wait[in->row], t = row_step[in->row];
    *state = 3;
    if (!w) return p;
    V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), o = p;
    if (w > 0) {
        p = star_appear_point(in, row_pos(&sc->objs[w - 1]), p);
        const Row *r = &sc->objs[w - 1];
        const char *name = sc->types + 32 * r->type;
        V3 x, z;
        row_axes(r, &x, &z);
        float h = strcmp(name, "CrystalCageL") == 0 ? 250.0f : strncmp(name, "Breakable", 9) == 0 ? 150.0f : 0.0f;
        o = vadd(row_pos(r), vmul(vcross(z, x), h * r->scale[0]));
        if (!row_dead[w - 1]) { *state = h > 0; return o; }
    } else if (got_coins < 100 && got_purple < 100) { /* ScenePlayingResult: 100 coins, or 100 purple coins */
        *state = 0;
        return o;
    }
    *state = t >= 210 ? 3 : 2;
    if (t >= 200) return p;
    if (t < 80) return vadd(o, vmul(up, 300.0f * sinf(fminf(t / 60.0f, 1.0f) * 1.5707963f)));
    V3 a = vadd(o, vmul(up, 300.0f)), d = vsub(p, a);
    float s = (t - 80) / 120.0f, h = vlen(vsub(d, vmul(up, vdot(d, up)))) * 0.5f;
    return vadd(vadd(a, vmul(d, s)), vmul(up, sinf(s * 3.14159265f) * fminf(fmaxf(h, 300.0f), 5000.0f)));
}

/* NoteFairy.cpp / Note.cpp: on its appear switch (row_shown) the fairy flies along its rail at arg1 (13) a frame and
 * its notes come out as it passes them: arg0 notes spread evenly over the rail, one per rail point with -2, else one
 * every 150 (the rail's point arg0 for -2). Mario touching a note (80) takes it; all taken: its SW_A on and its power
 * star out (arg7, the row dies: star_wait); arg2 (20) seconds after the last one is out the rest are gone. The notes
 * turn 8 degrees a frame (NoteGroup). row_turn 0 hidden, 1 coming out, 2 counting down, 3 over; row_coord the fairy's
 * place on the rail, row_step the countdown. ponytail: no appear demo / camera, no melody, no flashing, a failure is
 * final (no new try) */
static int note_rail(const Row *r, const unsigned char **e)
{
    *e = scene_blob_g && r->path >= 0 ? find_path(scene_blob_g, r->zone, r->path) : NULL;
    return *e != NULL;
}

static void notes_spawn(void) /* at game_init: every fairy's notes, hidden */
{
    nnotes = 0;
    int t = scene_type(sc, "NoteFairy");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        const unsigned char *e;
        if (r->type != t || !note_rail(r, &e)) continue;
        u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
        const float *pts = (const float *)(e + 24), *ctl = pts + 3 * np;
        float len = tsuki_len(e);
        int n = r->arg[0] > 0 ? r->arg[0] : r->arg[0] == -2 ? (int)nr : (int)(len / 150.0f);
        const unsigned char *pa = e + 24 + 12 * np + 12 * nr; /* per rail point: its coordinate first */
        for (int k = 0; k < n && nnotes < MAX_NOTES; k++) {
            notes[nnotes].coord = r->arg[0] == -2 ? *(const float *)(pa + 12 * k) : r->arg[0] > 0 ? len * k / n : 150.0f * k;
            notes[nnotes].pos = r->arg[0] == -2 ? v3(ctl[3 * k], ctl[3 * k + 1], ctl[3 * k + 2]) : rail_at(pts, np, notes[nnotes].coord);
            notes[nnotes].row = (short)i; notes[nnotes].alive = 0;
            nnotes++;
        }
    }
}

static void update_notes(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    int t = scene_type(sc, "NoteFairy");
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != t || row_turn[i] == 3) continue;
        const unsigned char *e;
        if (!note_rail(r, &e)) continue;
        if (row_turn[i] == 0) {
            if (!row_shown(i)) continue;
            row_turn[i] = 1; row_coord[i] = 0;
            printf("note fairy row %d: notes come out (rail %d long)" "\n", (int)i, (int)tsuki_len(e));
        }
        float len = tsuki_len(e), speed = r->arg[1] > 0 ? (float)r->arg[1] : 13.0f;
        int left = 0, all = 0;
        if (row_turn[i] == 1) row_coord[i] += r->arg[3] == -1 ? len : speed; /* AppearNote (type -1): all at once */
        for (int k = 0; k < nnotes; k++) {
            if (notes[k].row != (short)i) continue;
            all++;
            if (row_turn[i] == 1 && notes[k].alive == 0 && row_coord[i] >= notes[k].coord) notes[k].alive = 1; /* the fairy passed it */
            if (notes[k].alive == 1 && vlen(vsub(c, notes[k].pos)) < 80.0f + HIT_BODY_R) {
                notes[k].alive = 2; /* taken (addNoteToCounter) */
                printf("note fairy row %d: note %d taken" "\n", (int)i, k);
            }
            left += notes[k].alive != 2;
        }
        if (row_turn[i] == 1 && row_coord[i] >= len) { row_turn[i] = 2; row_step[i] = 0; }
        if (!left) { /* ACTMES_SUCCESS */
            row_turn[i] = 3;
            if (r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
            row_kill(i); /* its power star comes out (star_wait) */
            printf("note fairy row %d: all %d notes taken" "\n", (int)i, all);
        } else if (row_turn[i] == 2 && ++row_step[i] >= 60 * (r->arg[2] > 0 ? r->arg[2] : 20)) { /* failureNoteCollection */
            row_turn[i] = 3;
            for (int k = 0; k < nnotes; k++) if (notes[k].row == (short)i && notes[k].alive == 1) notes[k].alive = 0;
            printf("note fairy row %d: time is up" "\n", (int)i);
        }
    }
}

static void update_items(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->item) continue;
        if (in->item == ITEM_STAR && star_wait[in->row]) {
            int state;
            star_pos(in, &state);
            if (state < 3) { row_step[in->row] += state == 2; continue; } /* row_step: its appear demo */
        }
        float r;
        V3 p = item_sensor(in, &r);
        if (vlen(vsub(c, p)) > r + HIT_BODY_R) continue;
        if (in->item == ITEM_STAR) {
            printf("power star %d got\n", ++stars);
            star_demo = 1; /* requestPowerStarGetDemo: the PowerStarGet camera and Mario's clip */
            /* PowerStar::exeStageClearDemo step 1: mBaseMtx = its posture _B8 (initPosture: up against the gravity at
             * the star, front its placed Z) at its position, and setPlayerBaseMtx puts Mario there; its
             * startAnimCameraTargetSelf camera runs in that frame. ponytail: arg4 (_127, the PowerStarAppearPoint's
             * frame) is not read */
            const Row *sr = &sc->objs[in->row];
            V3 sp = v3(in->m[12], in->m[13], in->m[14]), su = up_at(sp, mario->up), sx, sz;
            row_axes(sr, &sx, &sz);
            V3 sf = vtangent(sz, su);
            if (vlen(sf) < 1e-3f) sf = vtangent(sx, su);
            mario->pos = sp; mario->up = su; mario->face = vnorm(sf);
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            basis_matrix(star_demo_m, sp, su, mario->face, 0);
        }
        else if (in->item == ITEM_ONEUP) printf("1-up, lives %d\n", ++lives);
        else if (in->item == ITEM_LIFEUP) { life = max_life = 2 * MAX_LIFE; printf("life up\n"); }
        else if (in->item == ITEM_QCOIN) fx_emit(FX_QUESTIONCOIN_GET, in->m);
        else if (in->item == ITEM_CHIP)
            fx_emit(sc->types[32 * sc->objs[in->row].type] == 'Y' ? FX_YELLOWCHIP_GET : FX_BLUECHIP_GET, in->m);
        else if (in->item == ITEM_FIRE) { fire_time = 1200; bee_mode = 0; printf("fire mario\n"); } /* mFireModeTime */
        else if (in->item == ITEM_BEE) { bee_mode = 1; fire_time = 0; bee_meter = 180; printf("bee mario\n"); }
        else if (in->item == ITEM_KEY) { /* KeySwitch::kill: onSwitchA */
            if (sc->objs[in->row].sw[SW_A] >= 0) sw_set(&sc->objs[in->row], SW_A, 1);
            printf("key switch %d taken\n", in->row);
        }
        row_kill(in->row);
        game_rebind();
        return;
    }
}

static void update_pops(void)
{
    for (int i = 0; i < npops; i++) {
        Pop *q = &pops[i];
        Pickup *c = &coins[q->coin];
        if (c->alive == PICK_GONE || q->t >= 600) { c->alive = PICK_GONE; continue; }
        q->t++;
        V3 up = up_at(c->pos, mario->up), gn = up, wn;
        int wall;
        q->vel = vsub(q->vel, up);
        V3 center = vadd(c->pos, q->vel);
        int ground = collide_n(&center, up, 30.0f, &gn, &wn, &wall);
        c->pos = center;
        if (ground) { /* calcRebouond */
            float into = -vdot(q->vel, gn);
            if (into > 0) q->vel = vadd(q->vel, vmul(gn, into * (into >= 2.0f ? 1.75f : 1.0f)));
        }
        q->vel = vmul(q->vel, ground ? 0.9f : 0.995f);
        c->alive = q->t < 30 ? PICK_POP_NEW : q->t > 480 && (q->t & 4) ? PICK_POP_BLINK : PICK_ON;
    }
}

/* The star pointer (Wii remote) on the PS2: a cursor moved by the right stick, faster the further it is pushed (6
 * to 16 pixels a frame), R3 back to the centre. cursor_ray: the camera ray through it (render.c's projection: f =
 * 224 / tan(fovy / 2) in 448-line units, screen x along s = forward x up, screen y down along -u). Star pieces pass
 * to the pointer when they show within 24 pixels of it and 4000 of the camera (StarPiece.cpp: the pointer takes
 * them as it sweeps over). ponytail: no pointer effect, no grab of far pieces through walls checked */
static void cursor_ray(V3 *o, V3 *d)
{
    V3 ce, cw, cu;
    float fovy;
    camera_pose(&ce, &cw, &cu, &fovy);
    V3 fw = vnorm(vsub(cw, ce)), s = vnorm(vcross(fw, cu)), u = vcross(s, fw);
    float f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
    *o = ce;
    *d = vnorm(vadd(vadd(vmul(fw, f), vmul(s, cur_x - 320.0f)), vmul(u, 224.0f - cur_y)));
}

static int cursor_sees(V3 p, float px) /* p shows within px pixels of the cursor */
{
    V3 o, d;
    cursor_ray(&o, &d);
    V3 to = vsub(p, o);
    float z = vdot(to, d);
    if (z <= 0 || z > 4000.0f) return 0;
    V3 ce, cw, cu;
    float fovy;
    camera_pose(&ce, &cw, &cu, &fovy);
    float f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
    return vlen(vsub(to, vmul(d, z))) * f / z < px;
}

static void shoot_piece(void);
static void update_shots(void);
static void update_cursor(void)
{
    float x = pad_rx(), y = pad_ry(), k = 6.0f + 10.0f * sqrtf(x * x + y * y);
    cur_x += x * k; cur_y -= y * k;
    if (pad_pressed(PAD_R3)) { cur_x = 320; cur_y = 224; }
    if (pad_pressed(PAD_L1) || test_l1) { test_l1 = 0; shoot_piece(); }
    update_shots();
    cur_x = cur_x < 0 ? 0 : cur_x > 639 ? 639 : cur_x;
    cur_y = cur_y < 0 ? 0 : cur_y > 447 ? 447 : cur_y;
    for (int i = 0; i < npieces; i++)
        if (pickup_on(&pieces[i], 1) && cursor_sees(pieces[i].pos, 24.0f)) {
            pieces[i].alive = PICK_GONE;
            got_pieces++;
            fx_at(FX_STARPIECE_STARPIECEGET, pieces[i].pos, up_at(pieces[i].pos, mario->up));
        }
}

/* A star piece shot (StarPieceShooter): L1 spends one taken piece, thrown from Mario's chest at 40 a frame towards
 * where the pointer's ray meets the map (or 3000 down it), for 60 frames; it stops on the map. What it hits:
 * walking enemies stagger (Kuribo.cpp requestStagger: KURIBO_STUN 180 frames), Takobo stuns 240 (StunStart), a
 * MechanicKoopaMini turns to SpinHit then fires, a free Karikari is blown out (dies), a Kiraira explodes; HomingKiller
 * and meteors only reflect it (isMsgStarPieceReflect). ponytail: no shooter effect / sound, no piece cost if none
 * are left, PackunPetit and JumpSpider ignore it */
static void shoot_piece(void)
{
    if (got_pieces <= 0) return;
    V3 o, d;
    cursor_ray(&o, &d);
    V3 aim = vadd(o, vmul(d, 3000.0f));
    Hit h[1];
    for (float t = 100.0f; t <= 3000.0f; t += 60.0f)
        if (kcl_query(vadd(o, vmul(d, t)), 30.0f, h, 1)) { aim = vadd(o, vmul(d, t)); break; }
    for (int i = 0; i < MAX_SHOTS; i++)
        if (!shots[i].t) {
            shots[i].pos = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
            shots[i].vel = vmul(vnorm(vsub(aim, shots[i].pos)), 40.0f);
            shots[i].t = 1;
            got_pieces--;
            printf("star piece shot (%d left)" "\n", got_pieces);
            return;
        }
}

static void update_shots(void)
{
    Hit h[1];
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &shots[i];
        if (!s->t) continue;
        if (++s->t > 60) { s->t = 0; continue; }
        s->pos = vadd(s->pos, s->vel);
        int hit = 0;
        for (int k = 0; k < nkuribo && !hit; k++) {
            Kuribo *e = &kuribo[k];
            if (e->state == KURIBO_DEAD || !row_shown(e->row) || vlen(vsub(s->pos, vadd(e->pos, vmul(e->up, 50.0f)))) > 90.0f) continue;
            hit = 1;
            if (e->kind == WALK_TAKOBO) { e->phase = 3; e->timer = 0; }
            else if (e->kind == WALK_MKOOPA) { e->phase = 6; e->timer = 0; }
            else if (e->kind == WALK_KARON) { if (e->phase != 2) { e->phase = 3; e->timer = 0; } }
            else if (e->kind == WALK_BUTTERFLY || e->kind == WALK_TERESA) { hit = 0; continue; }
            else if (e->kind == WALK_UNIZO || e->kind == WALK_POIHANA) { /* isMsgStarPieceAttack: nothing more */ }
            else if (e->kind == WALK_STINKBUG) { if (e->phase != 2 && e->phase != 6) { e->phase = 5; e->timer = 0; } }
            else if (e->kind == WALK_KARIKARI) {
                if (e->phase == 2) { hit = 0; continue; } /* clinging: passes */
                e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up);
            } else if (e->kind != WALK_PACKUN && e->kind != WALK_SPIDER) { e->state = KURIBO_STUN; e->timer = 180; }
            printf("star piece hits walker row %d" "\n", e->row);
        }
        for (int k = 0; k < ninsts && !hit; k++) {
            const Inst *in = &insts[k];
            if (!in->kiraira || row_turn[in->row]) continue;
            V3 up, p = kiraira_pos(in, &up);
            if (vlen(vsub(s->pos, p)) > 150.0f) continue;
            float m[16];
            memcpy(m, in->m, 64);
            m[12] = p.x; m[13] = p.y; m[14] = p.z;
            fx_emit(FX_SPACEMINE_EXPLOSION, m);
            row_turn[in->row] = 1; row_step[in->row] = 0;
            if (sc->objs[in->row].sw[SW_B] >= 0) sw_set(&sc->objs[in->row], SW_B, 1);
            printf("star piece explodes kiraira %d" "\n", in->row);
            hit = 1;
        }
        for (int k = 0; k < ninsts && !hit; k++) { /* SpaceCocoon: a star piece pushes the cocoon 5 */
            Inst *in = &insts[k];
            if (!in->cocoon || vlen(vsub(s->pos, in->head)) > 100.0f) continue;
            in->hvel = vadd(in->hvel, vmul(vnorm(vtangent(s->vel, vnorm(v3(in->m0[4], in->m0[5], in->m0[6])))), 5.0f));
            hit = 1;
        }
        for (int k = 0; k < ninsts && !hit; k++) { /* CocoNut: isMsgStarPieceReflect sends it off at 10 */
            Inst *in = &insts[k];
            if (!in->coco || row_turn[in->row] == 2 || vlen(vsub(s->pos, in->head)) > 55.0f * in->s + 30.0f) continue;
            coco_hit(in, s->vel, 10.0f, -5.0f, up_at(in->head, mario->up));
            hit = 1;
        }
        for (int k = 0; k < ninsts && !hit; k++) { /* PunchingKinoko: isMsgStarPieceAttack pushes its head 15 */
            Inst *in = &insts[k];
            if (!in->punch || row_turn[in->row] == 4 || vlen(vsub(s->pos, in->head)) > 70.0f + 30.0f) continue;
            V3 dir = vnorm(s->vel);
            if (vdot(in->hvel, dir) < 15.0f) in->hvel = vadd(in->hvel, vmul(dir, 15.0f));
            hit = 1;
        }
        for (int k = 0; k < ninsts && !hit; k++) { /* PomponPlant: isMsgStarPieceReflect swings it */
            const Inst *in = &insts[k];
            if (in->giver != 2 || row_turn[in->row] || vlen(vsub(s->pos, giver_sensor(in))) > 90.0f) continue;
            giver_give(in, giver_sensor(in), vnorm(v3(in->m[4], in->m[5], in->m[6])));
            hit = 1;
        }
        for (int k = 0; k < ninsts && !hit; k++) { /* TreasureSpot.cpp: Spout, Mario within 2000 */
            const Inst *in = &insts[k];
            if (!in->tspot || row_turn[in->row]) continue;
            V3 p = v3(in->m[12], in->m[13], in->m[14]), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
            if (vlen(vsub(s->pos, vadd(p, vmul(up, 50.0f)))) > 100.0f + 20.0f || vlen(vsub(p, mario->pos)) > 2000.0f) continue;
            hit = 1;
            pop_coins(vadd(p, vmul(up, 50.0f)), up, 1);
            printf("star piece opens %s row %d" "\n", in->tspot == 1 ? "TreasureSpot" : "CoinFlower", in->row);
            if (in->tspot == 1) { row_kill(in->row); game_rebind(); break; }
            row_turn[in->row] = 1; /* the flower stays, bloomed */
        }
        if (hit || kcl_query(s->pos, 20.0f, h, 1)) s->t = 0;
    }
}

void game_cursor(float *x, float *y) { *x = cur_x; *y = cur_y; }
void game_set_cursor(float x, float y) { cur_x = x; cur_y = y; }

static void collect(Pickup *p, int n, float reach, int *counter, int fx)
{
    V3 c = vadd(mario->pos, vmul(mario->up, BODY_RADIUS));
    for (int i = 0; i < n; i++)
        if (pickup_on(&p[i], 1) && vlen(vsub(p[i].pos, c)) < reach) {
            p[i].alive = PICK_GONE;
            (*counter)++;
            fx_at(fx, p[i].pos, up_at(p[i].pos, mario->up));
        }
}

/* Looping effects (AutoEffectList lights) of what is shown near the camera: star pieces, power stars, launch stars,
 * ? coins and chips. ponytail: by distance only (4000), not by the actor's own clipping */
/* SimpleEffectObj.cpp (the *EffectObj* classes: SnowS, LavaSparksS, AstroTorchLightRed...): a row plays its
 * AutoEffectList group at its place (fx_ambient, tools/jpa.py AMBIENT), while its SW_A is on if it has one; the
 * break / explosion ones (sEffectDataTable's rumbling SEs) play once when their SW_A comes on. Only the 12 nearest
 * within 5000 of the camera run (MAX_EMIT 128 is shared with the game's effects). ponytail: no sound, no clipping by
 * the class's own far distance, no demo casts */
static int ambient_fx(const char *name)
{
    for (int k = 0; k < FX_AMBIENT_N; k++) if (strcmp(name, fx_ambient[k]) == 0) return FX_AMBIENT_FIRST + k;
    return -1;
}

static void ambient_effects(void)
{
    static const char *const once[] = {"IceLayerBreak", "WaterLayerBreak", "StarPieceClusterBreak", "BattleShipExplosionMetal",
                                       "BattleShipExplosionRock", "LavaVolcanoEruption"};
    int best[12], nb = 0;
    float bd[12];
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        const char *name = sc->types + 32 * r->type;
        int fx = ambient_fx(name);
        if (fx < 0 || !row_shown(i)) continue;
        int one = 0;
        for (unsigned k = 0; k < sizeof once / sizeof once[0]; k++) one |= strcmp(name, once[k]) == 0;
        if (one) { /* once, when its switch comes on (row_turn: played) */
            if (r->sw[SW_A] >= 0 && sw_on(r, SW_A) && !row_turn[i]) {
                float m[16];
                mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
                fx_emit(fx, m);
                row_turn[i] = 1;
            }
            continue;
        }
        if (r->sw[SW_A] >= 0 && !sw_on(r, SW_A)) continue;
        float d = vlen(vsub(row_pos(r), cam_pos));
        if (d > 5000.0f) continue;
        if (nb == 12 && d >= bd[11]) continue;
        int k = nb < 12 ? nb++ : 11;
        while (k > 0 && bd[k - 1] > d) { bd[k] = bd[k - 1]; best[k] = best[k - 1]; k--; }
        bd[k] = d; best[k] = (int)i;
    }
    for (int k = 0; k < nb; k++) {
        const Row *r = &sc->objs[best[k]];
        float m[16];
        mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
        fx_keep(ambient_fx(sc->types + 32 * r->type), r, m);
    }
}

static void keep_effects(void)
{
    ambient_effects();
    float m[16];
    for (int i = 0; i < npieces; i++)
        if (pickup_shown(&pieces[i]) && vlen(vsub(pieces[i].pos, cam_pos)) < 4000.0f) {
            basis_matrix(m, pieces[i].pos, up_at(pieces[i].pos, mario->up), v3(0, 0, 1), 0);
            fx_keep(FX_STARPIECE_STARPIECELIGHT, &pieces[i], m);
        }
    for (int i = 0; i < nlaunches; i++)
        if (row_shown(launches[i].row) && vlen(vsub(launches[i].pos, cam_pos)) < 4000.0f) {
            basis_matrix(m, launches[i].pos, up_at(launches[i].pos, mario->up), v3(0, 0, 1), 0);
            fx_keep(launches[i].start < 45 ? FX_SPINDRIVER_SPINDRIVERLIGHT : FX_SUPERSPINDRIVER_SUPERSPINDRIVERLIGHT,
                    &launches[i], m);
        }
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->item || !row_shown(in->row) || vlen(vsub(v3(in->m[12], in->m[13], in->m[14]), cam_pos)) > 4000.0f)
            continue;
        const char *name = sc->types + 32 * sc->objs[in->row].type;
        int fx = in->item == ITEM_STAR ? FX_POWERSTAR_LIGHT : in->item == ITEM_QCOIN ? FX_QUESTIONCOIN_LIGHT :
                 in->item == ITEM_CHIP ? (name[0] == 'Y' ? FX_YELLOWCHIP_WAIT : FX_BLUECHIP_WAIT) : -1;
        if (fx < 0) continue;
        float lm[16];
        memcpy(lm, in->m, 64);
        if (in->item == ITEM_STAR) { /* the light follows an event star */
            int state;
            V3 p = star_pos(in, &state);
            if (!state) continue;
            lm[12] = p.x; lm[13] = p.y; lm[14] = p.z;
        }
        fx_keep(fx, in, lm);
    }
}

/* CometEventKeeper (Red / Black comet): the scenario's limit (sTimeLimitInfoTable, seconds) counts down from the start;
 * at 0 (isReadyToTimeUp) Mario is lost (forceKillPlayerByWaterRace); a power star stops it. ponytail: no countdown
 * sounds, no TimeLimitLayout (the HUD line) */
void game_set_time_limit(int seconds) { time_limit = time_left = seconds * 60; }
int game_time_left(void) { return time_limit ? time_left : -1; }

/* the pose kept for `row` (a free or stale slot bound to its skin, its wait clip started); NULL when the pool is full */
static Anim *obj_anim(int row, const Model *md, const char *cls)
{
    int k = -1;
    for (int i = 0; i < MAX_OBJ_ANIM; i++)
        if (oanim[i].row == row) { k = i; break; }
    if (k < 0)
        for (int i = 0; i < MAX_OBJ_ANIM && k < 0; i++)
            if (oanim[i].row < 0 || game_frame - oanim[i].last > 30) k = i;
    if (k < 0 || !oanim[k].a) return NULL;
    oanim[k].last = game_frame;
    if (oanim[k].row != row || oanim[k].a->skin != md->skin) {
        char n[48];
        Anim *a = oanim[k].a;
        oanim[k].row = row;
        anim_bind(a, md->skin);
        snprintf(n, sizeof n, "%s/wait", cls);
        for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (!anim_play(a, n, 1.0f, 1, 0)) { /* no wait clip: its first */
            size_t l = strlen(n) - 4;
            for (u32 c = 0; c < sc->nclips; c++)
                if (!strncmp(sc->clips[c].name, n, l)) { anim_play(a, sc->clips[c].name, 1.0f, 1, 0); break; }
        }
    }
    return oanim[k].a;
}

/* the instance's skinned clip `clip` of its class (no-op if it is playing); 0 if missing */
static int obj_play(const Inst *in, const char *clip, int loop, int interp)
{
    if (in->anim) in->anim->done = 0;
    if (!in->anim) return 0;
    char n[48];
    snprintf(n, sizeof n, "%s/%s", sc->types + 32 * sc->objs[in->row].type, clip);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    return anim_play(in->anim, n, 1.0f, loop, interp);
}

static void update_obj_anims(void)
{
    for (int i = 0; i < ninsts; i++)
        if (insts[i].anim && vlen(vsub(v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]), cam_pos)) < 8000.0f) {
            for (int k = 0; k < MAX_OBJ_ANIM; k++) if (oanim[k].a == insts[i].anim) oanim[k].last = game_frame;
            if (insts[i].anim->done && !insts[i].anim->loop) obj_play(&insts[i], "wait", 1, 8); /* a one-shot clip over */
            anim_update(insts[i].anim);
        }
}

void game_update(void)
{
    if (starter >= 0 && game_frame == 2) { /* ScenarioStarter::exeRailMove: the flight in (SpinDriverShootPath) */
        mario->pos = launches[starter].pos;
        mario->launch = starter; mario->launch_t = 0; mario->launch_from = mario->pos;
        printf("scenario starter: flight in, %d frames" "\n", launches[starter].flight);
        starter = -1;
    }
    update_obj_anims();
    if (time_limit && time_left > 0 && !stars && --time_left == 0) { printf("time up" "\n"); miss(); }
    hip_hit = 0;
    game_frame++;
    update_rotors();
    update_railers();
    update_mario();
    if (mario->spin_timer == SPIN_TIME - 1) mario_fx(FX_MARIO_SPINLIGHT, 1.0f); /* a spin started this frame */
    if (hip_hit) mario_fx(FX_MARIO_HIPDROPFLOWER, 0.0f);
    update_kuribo();
    kari_bite();
    break_boxes();
    update_collapse();
    update_flips();
    update_hip_switches();
    update_hazards();
    update_items();
    update_electric();
    update_firebars();
    update_rocks();
    update_shells();
    update_shooting();
    update_plants();
    update_steam();
    update_geyser();
    update_assembly();
    update_jumpstands();
    update_tsuki();
    update_kiraira();
    update_meteor();
    update_killers();
    update_flows();
    update_prom();
    update_beeflowers();
    update_ticorails();
    update_givers();
    update_birikyu();
    update_punch();
    update_coconuts();
    update_notes();
    update_cocoons();
    update_fluffs();
    update_jets();
    update_screws();
    update_candles();
    update_fountains();
    update_strays();
    update_bubbles();
    update_dino();
    update_ota();
    if (star_demo) { /* its camera ("powerstar/powerstarget"); Mario plays PowerStarGet meanwhile */
        if (!demo_camera("powerstar/powerstarget", star_demo - 1, star_demo_m)) { star_demo = 0; camera_set_override(0, v3(0, 0, 0), v3(0, 0, 0), v3(0, 1, 0), 45); }
        else star_demo++;
    }
    demo_on = dino_demo_on || ota_demo_on || star_demo || fat_demo();
    update_ticofat();
    update_sambos();
    update_fire();
    place_waterplants();
    update_pipes();
    update_switches();
    update_pops();
    int had = got_coins;
    collect(coins, ncoins, 100.0f, &got_coins, FX_COIN_COINGET);
    life += got_coins - had;
    if (life > max_life) life = max_life;
    if (got_coins / 50 > had / 50) lives++; /* every 50 coins a 1-up */
    collect(purple, npurple, 100.0f, &got_purple, FX_PURPLECOIN_COINGET);
    collect(pieces, npieces, 120.0f, &got_pieces, FX_STARPIECE_STARPIECEGET);
    update_cursor();
    spin_angle += 0.08f;
    CamTarget t = {mario->pos, mario->face, mario->up, !mario->grounded, mario->launch >= 0 && fly_on, fly_hy};
    camera_update(&t);
    V3 w, u;
    float fovy;
    camera_pose(&cam_pos, &w, &u, &fovy); /* cam_pos also steers the stick (camera-relative) */
    keep_effects();
    fx_update();
}

/* --- draw ------------------------------------------------------------------------------------------------ */

/* a walker's clip by kind and state (Kuribo.cpp Walk / Run / Stagger, Takobo.cpp Attack / Stun / Wait, Karikari.cpp
 * Wait / Jump / Bite, Butterfly.cpp, PackunPetit.cpp Wait / Swoon, KuriboMini.cpp Walk / Run / Stagger) */
static const char *walker_clip(const Kuribo *k, int *loop)
{
    float sp = vlen(vtangent(k->vel, k->up));
    *loop = 1;
    switch (k->kind) {
    case WALK_KURIBO:
        if (k->state == KURIBO_STUN) return "swoonground";
        return sp > 4.0f ? "run" : sp > 0.3f ? "walk" : "wait";
    case WALK_KMINI:
        if (k->state == KURIBO_STUN) return "stagger";
        return sp > 3.0f ? "run" : sp > 0.3f ? "walk" : "wait";
    case WALK_TAKOBO:
        if (k->phase == 1) { *loop = 0; return "attack"; }
        return k->phase == 3 ? "stun" : "wait";
    case WALK_KARIKARI:
        return k->phase == 2 ? "bite" : k->phase == 1 || k->phase == 3 ? "jump" : "wait";
    case WALK_BUTTERFLY: return "butterfly";
    case WALK_PACKUN: return k->state == KURIBO_STUN ? "swoon" : "wait";
    default: return "wait";
    }
}

static void inst_draw(const Inst *in, const Model *md, const float m[16])
{
    if (md->skin) { if (in->anim && md == in->md) render_skinned(md, m, (const float (*)[16])in->anim->drw); }
    else render_model(md, m, 0x70, 0xB0, 0x60);
}

static void basis_matrix(float m[16], V3 pos, V3 up, V3 fwd, float angle)
{
    V3 f = vnorm(vtangent(fwd, up));
    if (vlen(f) < 0.1f) f = vnorm(vtangent(v3(1, 0, 0), up));
    V3 r = vcross(up, f);
    float c = cosf(angle), s = sinf(angle);
    V3 x = vadd(vmul(r, c), vmul(f, -s)), z = vadd(vmul(r, s), vmul(f, c));
    mat_identity(m);
    m[0] = x.x; m[1] = x.y; m[2] = x.z;
    m[4] = up.x; m[5] = up.y; m[6] = up.z;
    m[8] = z.x; m[9] = z.y; m[10] = z.z;
    m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
}

static void draw_pickups(const Pickup *p, int n, const Model *md, u8 r, u8 g, u8 b)
{
    float m[16];
    if (!md) return;
    for (int i = 0; i < n; i++) {
        if (!pickup_shown(&p[i])) continue;
        V3 up = up_at(p[i].pos, v3(0, 1, 0));
        basis_matrix(m, p[i].pos, up, v3(0, 0, 1), spin_angle);
        render_model(md, m, r, g, b);
    }
}

void game_draw(void)
{
    V3 ce, cw, cu;
    float fovy;
    camera_pose(&ce, &cw, &cu, &fovy);
#ifdef OVERVIEW_TEST
    { /* the whole galaxy: a camera orbiting the box of the placed objects (the middle 90 % on each axis, so a far
       * stray row does not shrink the rest), a third of a turn every 20 s */
        static V3 c;
        static float rad;
        if (rad == 0) {
            static float ax[3][2048];
            int n = 0;
            for (u32 i = 0; i < sc->nobjs && n < 2048; i++, n++) {
                ax[0][n] = sc->objs[i].pos[0]; ax[1][n] = sc->objs[i].pos[1]; ax[2][n] = sc->objs[i].pos[2];
            }
            float lo[3], hi[3];
            for (int k = 0; k < 3; k++) { /* 5th and 95th percentiles by partial selection */
                for (int a = 0; a < n; a++)
                    for (int b = a + 1; b < n; b++)
                        if (ax[k][b] < ax[k][a]) { float t = ax[k][a]; ax[k][a] = ax[k][b]; ax[k][b] = t; }
                lo[k] = ax[k][n / 20]; hi[k] = ax[k][n - 1 - n / 20];
            }
            c = v3((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2);
            rad = fmaxf(0.5f * vlen(v3(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])), 3000.0f);
            printf("overview: centre %d %d %d radius %d\n", (int)c.x, (int)c.y, (int)c.z, (int)rad);
        }
        float a = game_frame * 0.00175f;
        float d = fminf(rad * 1.3f, 90000.0f); /* nearly level and looking a little up: the sky's horizon (VROrbit:
                                                 * stars above, the sea of clouds below) crosses the picture */
        cw = vadd(c, v3(0, d * 0.25f, 0));
        ce = vadd(c, vmul(vnorm(v3(cosf(a), 0.12f, sinf(a))), d));
        cu = v3(0, 1, 0);
        fovy = 50.0f;
    }
#endif
    render_set_fovy(fovy);
    float eye[3] = {ce.x, ce.y, ce.z}, target[3] = {cw.x, cw.y, cw.z}, up[3] = {cu.x, cu.y, cu.z};
    float m[16];
    render_begin(eye, target, up);
    int sky_drawn = 0;
    for (int i = 0; i < ninsts; i++) { /* skies first, at the camera; ponytail: scaled to fit inside the far plane
                                         * (render.c FAR 120000) so the rest of the scene draws over them by depth */
        const Inst *in = &insts[i];
        if (!in->sky || in->md->radius <= 0) continue;
        float cm[16], k = 108000.0f / (in->md->radius * in->s);
        memcpy(cm, in->m, 64);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= k;
        cm[12] = ce.x; cm[13] = ce.y; cm[14] = ce.z;
        inst_draw(in, in->md, cm);
        sky_drawn = 1;
    }
    if (sky_drawn) render_clear_depth(); /* the sky stays behind whatever its scale put in front */
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->invisible || in->sky) continue;
        if (in->plant) {
            if (plant_first[in->row] < 0) continue;
            int n = plant_count(&sc->objs[in->row]);
            for (int k = 0; k < n; k++) {
                const Plant *p = &plants[plant_first[in->row] + k];
                if (!p->ok) continue;
                basis_matrix(m, p->pos, p->up, v3(0, 0, 1), k * 1.3f);
                for (int j = 0; j < 12; j++) if (j % 4 != 3) m[j] *= in->s;
                inst_draw(in, in->md, m);
            }
            continue;
        }
        if (in->npc) { /* its own up, its front turned to Mario when he is near */
            V3 p = v3(in->m[12], in->m[13], in->m[14]), to = vsub(mario->pos, p);
            if (vdot(to, to) < NPC_TURN * NPC_TURN) {
                V3 up = vmul(v3(in->m[4], in->m[5], in->m[6]), 1.0f / in->s);
                float cm[16];
                basis_matrix(cm, p, up, to, 0);
                for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= in->s;
                inst_draw(in, in->md, cm);
                continue;
            }
        }
        if (in->flip && in->md_flip[0]) { /* the face PanelA / PanelB / PanelEnd leave shown (ponytail: no turn) */
            int turned = (row_step[in->row] & FLIP_BACK) != (strcmp(sc->types + 32 * sc->objs[in->row].type,
                                                                    "FlipPanelReverse") == 0);
            const Model *md = (row_step[in->row] & FLIP_DONE) && in->md_flip[1] ? in->md_flip[1] :
                              turned ? in->md_flip[0] : in->md;
            inst_draw(in, md, in->m);
            continue;
        }
        if (in->shoot) { /* spinning about its y, grown by k */
            float k, rot[16], cm[16], a = game_frame * 0.17453293f;
            V3 p = shoot_pos(in, &k);
            if (k <= 0.0f || row_dead[in->row]) continue;
            mat_identity(rot);
            rot[0] = rot[10] = cosf(a);
            rot[8] = sinf(a); rot[2] = -rot[8];
            mat_mul(cm, in->m, rot);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= k;
            cm[12] = p.x; cm[13] = p.y; cm[14] = p.z;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->assembly && row_turn[in->row] == 4 && row_step[in->row] + 100 >= (sc->objs[in->row].arg[1] > 0 ?
            sc->objs[in->row].arg[1] : 300) && (game_frame & 4))
            continue; /* about to vanish: blinks (ponytail for its Disappear animation) */
        if (in->beeflower && row_turn[in->row] && row_step[in->row] >= 30 && row_step[in->row] < 90) continue; /* gone */
        if (in->prom) { /* the ball while it is out */
            if (row_turn[in->row] < 2) continue;
            float cm[16];
            memcpy(cm, in->m, 64);
            V3 p = prom_pos(in);
            cm[12] = p.x; cm[13] = p.y; cm[14] = p.z;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->killer) { /* its bullet, nose along its front */
            const Killer *k = NULL;
            for (int j = 0; j < nkillers; j++) if (killers[j].row == in->row) k = &killers[j];
            if (!k || row_turn[in->row] == 3) continue;
            float cm[16];
            basis_matrix(cm, k->pos, up_at(k->pos, v3(in->m[4], in->m[5], in->m[6])), k->front, 0);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= in->s;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->meteor) { /* falling along its rail, turning about its x */
            if (row_turn[in->row] != 1) continue;
            V3 dir, p = meteor_pos(in, &dir), up = up_at(p, v3(0, 1, 0));
            float cm[16], rot[16], a = -7.5f * row_step[in->row] * 0.0174533f, tmp[16];
            basis_matrix(tmp, p, fabsf(vdot(up, dir)) > 0.99f ? vnorm(vcross(dir, v3(1, 0, 0))) : up, dir, 0);
            mat_identity(rot);
            rot[5] = rot[10] = cosf(a);
            rot[6] = sinf(a); rot[9] = -rot[6];
            mat_mul(cm, tmp, rot);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= in->s;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->kiraira) { /* drifting, facing Mario within 1500; hidden while Dead */
            if (row_turn[in->row] && row_step[in->row] < 300) continue;
            V3 up, p = kiraira_pos(in, &up), to = vsub(mario->pos, p), fwd = v3(in->m[8], in->m[9], in->m[10]);
            float cm[16];
            basis_matrix(cm, p, up, vdot(to, to) < 1500.0f * 1500.0f ? to : fwd, 0);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= in->s;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->jstand && row_turn[in->row]) { /* bound: squashed along its y */
            float cm[16], k = jstand_squash(in);
            memcpy(cm, in->m, 64);
            for (int j = 4; j < 7; j++) cm[j] *= k;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->geyser) { /* stretched along its y to its height (geyser_height) */
            float k = geyser_height(in) / GEYSER_BIND, cm[16];
            if (k <= 0.0f) continue;
            memcpy(cm, in->m, 64);
            for (int j = 4; j < 7; j++) cm[j] *= k;
            inst_draw(in, in->md, cm);
            continue;
        }
        if (in->collapse && row_step[in->row]) { /* calcJointPlane: x / z scale 1 - 0.7 step / time */
            float k = 1.0f - 0.7f * row_step[in->row] / in->collapse, cm[16];
            memcpy(cm, in->m, 64);
            for (int j = 0; j < 3; j++) { cm[j] *= k; cm[8 + j] *= k; }
            inst_draw(in, in->md, cm);
        } else if (in->item == ITEM_STAR) { /* spins about its own up */
            float rot[16], cm[16];
            mat_identity(rot);
            rot[0] = rot[10] = cosf(spin_angle);
            rot[8] = sinf(spin_angle); rot[2] = -rot[8];
            mat_mul(cm, in->m, rot);
            int state;
            V3 p = star_pos(in, &state);
            if (!state) continue;
            cm[12] = p.x; cm[13] = p.y; cm[14] = p.z;
            inst_draw(in, in->md, cm);
        } else
            inst_draw(in, in->md, in->m);
    }
    for (int i = 0; m_rock && i < nrollers; i++) {
        const Roller *o = &rollers[i];
        if (!o->alive) continue;
        float rad;
        V3 p = rock_pos(o, &rad), up = up_at(p, v3(0, 1, 0));
        V3 ahead = vsub(rail_at(o->pts, o->np, o->d + 10.0f), rail_at(o->pts, o->np, o->d));
        basis_matrix(m, p, up, ahead, 0);
        float roll = o->d / rad, cr = cosf(roll), sr = sinf(roll), rm[16], cm[16]; /* rolls about its x axis */
        mat_identity(rm);
        rm[5] = cr; rm[6] = sr; rm[9] = -sr; rm[10] = cr;
        mat_mul(cm, m, rm);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= sc->objs[o->row].scale[0];
        render_model(m_rock, cm, 0x70, 0xB0, 0x60);
    }
    for (int i = 0; m_shell && i < nshells; i++) {
        const Shell *o = &shells[i];
        if (!o->state) continue;
        basis_matrix(m, o->pos, up_at(o->pos, v3(0, 1, 0)), o->dir, 0);
        render_model(m_shell, m, 0x70, 0xB0, 0x60);
    }
    for (int i = 0; m_fireball && i < ninsts; i++) {
        if (!insts[i].firebar) continue;
        const Row *r = &sc->objs[insts[i].row];
        int sticks = r->arg[7] > 0 ? r->arg[7] : 1, balls = r->arg[0] > 0 ? r->arg[0] : 5;
        for (int j = 0; j < sticks; j++)
            for (int k = 0; k < balls; k++) {
                V3 p = firebar_ball(&insts[i], j, k);
                basis_matrix(m, p, up_at(p, v3(0, 1, 0)), vsub(cam_pos, p), 0);
                render_model(m_fireball, m, 0xF0, 0x80, 0x20);
            }
    }
    for (int i = 0; m_erail && i < nerails; i++) { /* the 600-long rail model stretched over each segment */
        const Spark *e = &erails[i];
        V3 d = vsub(e->b, e->a), mid = vmul(vadd(e->a, e->b), 0.5f), up = up_at(mid, v3(0, 1, 0));
        float len = vlen(d);
        if (len < 1.0f) continue;
        V3 z = vmul(d, 1.0f / len), x = vcross(up, z);
        if (vlen(x) < 0.1f) x = vcross(v3(1, 0, 0), z); /* a rail along gravity */
        x = vnorm(x);
        V3 y = vcross(z, x);
        mat_identity(m);
        m[0] = x.x; m[1] = x.y; m[2] = x.z;
        m[4] = y.x; m[5] = y.y; m[6] = y.z;
        m[8] = z.x * len / 600.0f; m[9] = z.y * len / 600.0f; m[10] = z.z * len / 600.0f;
        for (int k = 0; k < e->h; k++) {
            V3 p = vadd(mid, vmul(up, 100.0f * k));
            m[12] = p.x; m[13] = p.y; m[14] = p.z;
            render_model(m_erail, m, 0x70, 0xB0, 0x60);
        }
    }
    draw_pickups(coins, ncoins, m_coin, 0xF0, 0xD0, 0x30);
    draw_pickups(purple, npurple, m_purple, 0xB0, 0x50, 0xF0);
    draw_pickups(pieces, npieces, m_piece, 0x60, 0xD0, 0xF0);
    for (int i = 0; m_piece && i < MAX_SHOTS; i++)
        if (shots[i].t) {
            basis_matrix(m, shots[i].pos, up_at(shots[i].pos, v3(0, 1, 0)), shots[i].vel, game_frame * 0.5f);
            render_model(m_piece, m, 0x60, 0xD0, 0xF0);
        }
    for (int i = 0; i < nkuribo; i++) {
        const Model *md = kuribo[i].kind == WALK_TAKOHEI ? m_takohei : kuribo[i].kind == WALK_PACKUN ? m_packun :
                          kuribo[i].kind == WALK_KOTEBUG ? m_kotebug : kuribo[i].kind == WALK_SPIDER ? m_spider :
                          kuribo[i].kind == WALK_MERAMERA ? m_meramera : kuribo[i].kind == WALK_TAKOBO ? m_takobo :
                          kuribo[i].kind == WALK_MKOOPA ? m_mkoopa : kuribo[i].kind == WALK_KARIKARI ? m_kari :
                          kuribo[i].kind == WALK_KMINI ? m_kmini :
                          kuribo[i].kind == WALK_KARON ? m_karon :
                          kuribo[i].kind == WALK_TERESA ? m_teresa :
                          kuribo[i].kind == WALK_BUTTERFLY ? m_bfly : kuribo[i].kind == WALK_STINKBUG ? m_stink :
                          kuribo[i].kind == WALK_UNIZO ? m_unizo[kuribo[i].dir] : kuribo[i].kind == WALK_POIHANA ? m_poihana : m_kuribo;
        if (kuribo[i].state == KURIBO_DEAD || !md || !row_shown(kuribo[i].row)) continue;
        if (kuribo[i].kind == WALK_TERESA && kuribo[i].phase == 4) continue; /* hidden in a wall */
        basis_matrix(m, kuribo[i].pos, kuribo[i].up, kuribo[i].kind == WALK_STINKBUG || kuribo[i].kind == WALK_POIHANA ? stink_front(&kuribo[i]) : vsub(mario->pos, kuribo[i].pos), 0);
        if (kuribo[i].kind == WALK_UNIZO) { /* rolled about its side round its centre */
            const Kuribo *k = &kuribo[i];
            V3 f = vtangent(k->vel, k->up);
            f = vlen(f) > 1e-3f ? vnorm(f) : vnorm(vtangent(vsub(mario->pos, k->pos), k->up));
            V3 u2 = vadd(vmul(k->up, cosf(k->to)), vmul(f, sinf(k->to))), f2 = vsub(vmul(f, cosf(k->to)), vmul(k->up, sinf(k->to)));
            basis_matrix(m, vsub(vadd(k->pos, vmul(k->up, 126.36f)), vmul(u2, 126.36f)), u2, f2, 0);
        }
        if (kuribo[i].kind == WALK_STINKBUG) /* its scale; flattened while dying */
            for (int j = 0; j < 12; j++) m[j] *= sc->objs[kuribo[i].row].scale[0] * (kuribo[i].phase == 6 && j >= 4 && j < 8 ? 1.0f - kuribo[i].timer / 40.0f : 1.0f);
        if (kuribo[i].kind == WALK_KARON && kuribo[i].phase == 2) /* a heap of bones (ponytail: squashed) */
            for (int j = 4; j < 7; j++) m[j] *= kuribo[i].timer < 300 ? 0.3f : 0.3f + 0.7f * (kuribo[i].timer - 300) / 60.0f;
        if (md->skin) { /* animated: the clip of its state (walker_clip), facing its way when it walks */
            const Kuribo *k = &kuribo[i];
            const char *cls = sc->types + 32 * sc->objs[k->row].type;
            Anim *a = obj_anim(k->row, md, cls);
            if (!a) continue;
            V3 hv = vtangent(k->vel, k->up);
            if ((k->kind == WALK_KURIBO || k->kind == WALK_KMINI) && vlen(hv) > 0.5f) basis_matrix(m, k->pos, k->up, hv, 0);
            int loop;
            const char *clip = walker_clip(k, &loop);
            char n[48];
            snprintf(n, sizeof n, "%s/%s", cls, clip);
            for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
            anim_play(a, n, 1.0f, loop, 6);
            anim_update(a);
            render_skinned(md, m, (const float (*)[16])a->drw);
            continue;
        }
        render_model(md, m, kuribo[i].state == KURIBO_STUN ? 0x50 : 0x90, 0x50, 0x20);
    }
    basis_matrix(m, mario->pos, mario->up, mario->face, 0);
    if (jb.inst >= 0) { /* swinging round a JumpBranch: his head towards it */
        const Inst *in = &insts[jb.inst];
        V3 up = vnorm(vsub(v3(in->m[12], in->m[13], in->m[14]), jb.pt));
        basis_matrix(m, mario->pos, up, mario->face, 0);
    }
    if (fly_on && mario->launch >= 0) { /* in a launch star's flight: head along the path (fly_pose) */
        m[0] = fly_x.x; m[1] = fly_x.y; m[2] = fly_x.z;
        m[4] = fly_y.x; m[5] = fly_y.y; m[6] = fly_y.z;
        m[8] = fly_z.x; m[9] = fly_z.y; m[10] = fly_z.z;
    }
    if (m_mario_skin && !(mario->hurt_timer & 4) && !mario_hidden)
        render_skinned(m_mario_skin, m, (const float (*)[16])mario_anim->drw);
    if (m_marioball) for (int b = 0; b < MAX_FIREBALLS; b++) { /* Fire Mario's balls, rolling */
        if (!fireballs[b].t) continue;
        basis_matrix(m, fireballs[b].pos, up_at(fireballs[b].pos, v3(0, 1, 0)), fireballs[b].vel, game_frame * 0.5f);
        render_model(m_marioball, m, 0x80, 0x80, 0x80);
    }
    if (ota.row >= 0 && ota.nrv != OTA_WAITONSWITCH && ota.nrv != OTA_APPEARSTAR && ota.nrv != OTA_DEAD) { /* OtaKing, its feet */
        basis_matrix(m, ota.pos, ota.up, ota.f, 0);
        if (m_ota && m_ota->skin && ota.anim->skin == m_ota->skin) render_skinned(m_ota, m, (const float (*)[16])ota.anim->drw);
        else if (m_ota) render_model(m_ota, m, 0x80, 0x80, 0x80);
        for (int k = 0; k < 2 && m_otafoot; k++)
            if (m_otafoot->skin && ota.foot[k]->skin == m_otafoot->skin) render_skinned(m_otafoot, m, (const float (*)[16])ota.foot[k]->drw);
            else if (!k) render_model(m_otafoot, m, 0x80, 0x80, 0x80);
    }
    if (ota.row >= 0) { /* its coconuts (SpinX), fire balls, fire bubbles */
        for (int b = 0; m_coco && b < OTA_BALLS; b++) {
            if (!ota.ball[b].state) continue;
            basis_matrix(m, ota.ball[b].pos, ota.ball[b].up, ota.ball[b].vel, game_frame * 0.3f);
            render_model(m_coco, m, 0x80, 0x80, 0x80);
        }
        for (int i = 0; m_fireball2 && i < OTA_FIRES; i++) {
            if (!ota.fire[i].alive) continue;
            basis_matrix(m, ota.fire[i].pos, ota.fire[i].up, ota.fire[i].vel, game_frame * 0.4f);
            render_model(m_fireball2, m, 0x80, 0x80, 0x80);
        }
        for (int i = 0; m_firebubble && i < OTA_BUBBLES; i++) {
            if (!ota.bub[i].state) continue;
            basis_matrix(m, ota.bub[i].pos, up_at(ota.bub[i].pos, ota.up), ota.bub[i].front, 0);
            render_model(m_firebubble, m, 0x80, 0x80, 0x80);
        }
    }
    if (dino.row >= 0 && dino.phase < 6 && row_shown(dino.row)) { /* DinoPackun, its egg shell, its tail ball */
        basis_matrix(m, dino.pos, dino.up, dino.f, 0);
        if (m_dino && m_dino->skin && dino.anim && dino.anim->skin == m_dino->skin) {
            float jm[16];
            render_skinned(m_dino, m, (const float (*)[16])dino.anim->drw);
            mat_mul(jm, m, dino.anim->world[2]); /* EggShell */
            int broke = dino.nrv == DN_DAMAGE && dino.dnrv == DD_EGGBROKEN; /* exeEggBroken: the shell until 30, then Break */
            if (m_degg && (dino.seq < DSEQ_EGG || (dino.seq == DSEQ_EGG && !(broke && dino.dt >= 30))))
                render_model(m_degg, jm, 0x80, 0x80, 0x80);
            else if (m_deggbrk && broke && dino.dt >= 30) render_model(m_deggbrk, jm, 0x80, 0x80, 0x80);
            mat_mul(jm, m, dino.anim->world[20]); /* TailBall */
            if (m_dball) render_model(m_dball, jm, 0x80, 0x80, 0x80);
        } else {
            if (m_dino) render_model(m_dino, m, 0x80, 0x80, 0x80);
            if (m_degg && dino.seq <= DSEQ_EGG) render_model(m_degg, m, 0x80, 0x80, 0x80);
            basis_matrix(m, dino.bpos, up_at(dino.bpos, dino.up), dino.f, 0);
            if (m_dball) render_model(m_dball, m, 0x80, 0x80, 0x80);
        }
    }
    for (int k = 0; m_piece && k < nfats; k++) /* TicoFat's gifted star pieces */
        for (int g = 0; g < 8; g++) if (fats[k].gift[g].on) {
            basis_matrix(m, fats[k].gift[g].pos, up_at(fats[k].gift[g].pos, v3(0, 1, 0)), v3(0, 0, 1), spin_angle);
            render_model(m_piece, m, 0x60, 0xD0, 0xF0);
        }
    draw_sambo_heads();
    if (m_bubble) for (int b = 0; b < MAX_BUBBLES; b++) { /* AirBubbleGenerator's bubbles */
        if (!bubbles[b].life) continue;
        basis_matrix(m, bubbles[b].pos, up_at(bubbles[b].pos, v3(0, 1, 0)), v3(1, 0, 0), 0);
        render_model(m_bubble, m, 0x80, 0x80, 0x80);
    }
    if (m_note) for (int i = 0; i < nnotes; i++) { /* NoteFairy notes, turning 8 degrees a frame */
        if (notes[i].alive != 1) continue;
        basis_matrix(m, notes[i].pos, up_at(notes[i].pos, v3(0, 1, 0)), v3(1, 0, 0), game_frame * 0.1396263f);
        render_model(m_note, m, 0x80, 0x80, 0x80);
    }
    draw_waterplants();
    fx_draw();
}

void game_debug(char *out, int n)
{
    snprintf(out, n, "mario %d %d %d up %d %d %d cam %d %d %d flips %d slip %d", (int)mario->pos.x, (int)mario->pos.y,
             (int)mario->pos.z, (int)(mario->up.x * 100), (int)(mario->up.y * 100), (int)(mario->up.z * 100), (int)cam_pos.x, (int)cam_pos.y,
             (int)cam_pos.z, ground_flips, slip_frames);
}

int game_coins(void) { return got_coins; }
int game_life(void) { return life; }
int game_lives(void) { return lives; }
int game_misses(void) { return misses; }
int game_stars(void) { return stars; }
int game_purple(void) { return got_purple; }
int game_purple_total(void) { return npurple; }
int game_starpieces(void) { return got_pieces; }
int game_pieces_out(void)
{
    int n = 0;
    for (int i = 0; i < npieces; i++) n += pickup_shown(&pieces[i]);
    return n;
}
int game_kuribo_alive(void)
{
    int n = 0;
    for (int i = 0; i < nkuribo; i++) n += kuribo[i].state != KURIBO_DEAD;
    return n;
}
