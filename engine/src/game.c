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
#include "mario_const.h"

/* Gameplay subset of HeavensDoorGalaxy scenario 2, data-oriented: one compact array per type, sized at load,
 * no allocation per object. Behaviour follows Petari where noted; differences are listed in docs/phase1-report.md. */

typedef struct { V3 pos; short alive, row; } Pickup;             /* 16 B: Coin, PurpleCoin, StarPiece; row: the
                                                                  * placement it came from, -1 a drop */
enum { PICK_GONE, PICK_ON, PICK_POP_NEW, PICK_POP_BLINK }; /* alive: popping coins cannot be taken while new and
                                                             * are not drawn on their blink frames */
typedef struct { V3 pos, vel, up; int state, timer, row, kind; int phase, dir; float from, to, len;
    /* the walkers ported literally (walker kit): nerve / step, the WalkerState's nerve / step, _B8 (front), _A8 as its
     * axes, the binder's ground / wall, TerritoryMover, WalkerStateStagger's mVelH, ItemGenerator, the action */
    int nrv, step, snrv, sstep, sdead, on_ground, on_wall, bind_off, item;
    V3 front, qy, qz, grav, gn, wn, tcenter, ttarget, velh;
    char act[20]; int act_seq, act_drawn, act_frames, act_mode, act_interp; float act_t, act_rate;
    V3 bpos, bvel; int bshow; /* a second model it throws (PackunPetit's blown head) */ } Kuribo; /* walking
                                         * enemies; row: its placement; phase .. len: Takobo's patrol (takobo_update) */
enum { KURIBO_WALK, KURIBO_STUN, KURIBO_DEAD };
enum { WALK_KURIBO, WALK_TAKOHEI, WALK_PACKUN, WALK_KOTEBUG, WALK_SPIDER, WALK_MERAMERA, WALK_TAKOBO, WALK_MKOOPA, WALK_KARIKARI, WALK_KMINI, WALK_KARON, WALK_TERESA, WALK_BUTTERFLY, WALK_STINKBUG, WALK_UNIZO, WALK_POIHANA, WALK_PETARI };
/* Petari.cpp's nerves (petari_update) */
enum { PT_LURK, PT_JUMPOUT, PT_WAIT, PT_APPROACH, PT_ESCAPE, PT_SWOONSTART, PT_SWOON, PT_SWOONEND, PT_LAND, PT_SMASH,
       PT_SPINOUT, PT_KICKOUT };
static int petari_solid(const Kuribo *k);
static void petari_blow(Kuribo *k, V3 from, int nrv);
static int walker_blow(Kuribo *e, V3 from);
static int kari_slot[5];
static int kari_cling, kari_timer; /* Karikari clinging to Mario (KarikariDirector: 5 at most), frames to its damage */

typedef struct {
    V3 pos, up, hvel, face, ground_n, wall_n;
    float vup;
    int grounded, spin_timer, hurt_timer; /* mMovementStates._1; the punch hit timer (setPunchHitTimer) */
    int jump_kind;      /* _430: 0-2 jump / 2nd / 3rd, 4 turn, 5 long jump, 6 back flip, 7 wall, 8 spin, 13 launched */
    int land_timer;     /* _3CE: frames on the ground since landing */
    int air_timer;      /* _3BC: frames since take-off */
    int squat, wall;    /* mMovementStates._A; touching a wall this frame (wall_n valid) */
    int slip_keep;      /* frames Mario may still stand on a 60-85 degree slope before it slips (Petari _41C) */
    /* Petari Mario's movement state (the Mario fields named after it) */
    int jumping;        /* mMovementStates.jumping */
    float walk;         /* mWalkSpeed (of MC.mWalkSpeed) */
    int tsi;            /* mTargetWalkSpeedIndex */
    float height;       /* mVerticalSpeed: height over the ground */
    V3 vel, vel_after;  /* mVelocity, mVelocityAfter: this frame's move */
    V3 sd, last_sd, move_dir, move_dir_keep; /* _22C, _238, _328, _334 */
    V3 turn_cross, turn_cross3, turn_ready_front, turn_from; /* _244, _3D8, _3E4, _220 */
    float stick_z0, stick_ang, stick_ang0, last_rx, last_ry; /* _38, _528, _524, _250 */
    float retain_ang, start_spin_ratio; /* _2B4, _3F4 */
    int retain_t, retain_a, retain_b; /* _40E, _10._A, _10._B */
    int turning, u_turn, u_side, weak_turn; /* mMovementStates.turning, _10._C, _274, _3D4 */
    int turn_ready, turn_slip, turn_brake, turn_neutral; /* _3D2, _3D0, mMovementStates._4, _10 */
    int stand_neutral, stand_turn; /* _40C, _750 */
    int slow_start, start_spin, start_spin2; /* _404, _3FA, _3FC */
    int brake1, brake2; /* _71E, _71F */
    int drop_grace;     /* _3C0 */
    int connect, spin_lock, squat_lock, slip_front, squat_walk; /* mMovementStates._5, _2B, _21, _9, _26 */
    int keep_jump, digital, neutral, wall_hit, no_air_walk, drop_free, wall_free; /* _13, _E, _1D, _10._E, _2F, _20, _22 */
    int fall_t, back_t, air_walk_t, air_spin, air_lock, spin_kind, zero_gr; /* _3BE, _3CA, _3CC, _402, _428, _42A, _424 */
    int hip_still;      /* _422 */
    V3 hip_jv, jump_from, last_pos; /* _2F8, _4B0 */
    int spin_cool, spin_t, magic; /* MarioActor _946, _945; MarioMagic active */
    int walling, wall_t, wall_pull, wall_cancel; /* MarioWall: active, _14, _18, _1C */
    float wall_drop;    /* MarioWall _20 */
    V3 wall_jv0, wall_cancel_n; /* MarioWall _30, _24 */
    int launch, launch_t; /* launch star carrying Mario (-1 none) and frames since it caught him */
    int hip, hip_t;     /* hip drop: HIP_START / HIP_FALL / HIP_LAND (0 none), frames in that phase */
    V3 launch_from;     /* where Mario was caught */
} Player;

/* SuperSpinDriver (Petari SuperSpinDriver.cpp, SpinDriverShootPath.cpp, ParabolicPath.cpp): bind radius 300,
 * ShootStart 45 frames (Mario pulled to the centre over 15), then Shoot along the path for mFlightTime (arg0,
 * default 300). Path: rails of at most 2 points are a parabola from the star to the rail's end whose top is the
 * rail's start (initFromMaxHeight); longer rails are followed by length with the star's offset eased out over the
 * first half. The operate ring never moves Mario (SuperSpinDriver _17D is never set: radius rate 0, no -75 offset, no
 * pointer steering). ponytail: rails are straight segments between control points (the game's are Bezier), no camera
 * of its own, no pull-in (arg2). SuperSpinDriverPink / Green are the same class (Green needs no star).
 * SpinDriver (SpinDriver.cpp): bind radius 300, ShootStart 20 frames (a dip 200 under it along its up); on a rail the same path for arg0 frames (default
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
    V3 kick, up;      /* up: its Y axis (SpinDriver's wind-up) */
    int starter;      /* ScenarioStarter's flight: no ShootStart, its own motion times and first pose */
    float initrot;    /* its arg5: the first pose's turn about the path (calcInitPose) */
} Launch;
#define LAUNCH_BIND 300.0f

static void init_launch(Launch *L, const Row *r, const float *pts, u32 np, int parab);
static V3 launch_pos(const Launch *L, float t);

/* Mario constants: MC (Petari MarioConst.cpp gMarioConstDefault, tools/mario_const.py) */
#define SPIN_TIME 25              /* setPunchHitTimer(0x19): the spin's hit frames */
#define SLIP_ANGLE_COS 0.5f       /* mSlipAngle 60: steeper ground is a slope Mario slips down */
#define WALL_ANGLE_COS 0.087f     /* mForceWallAngle 85: steeper faces are walls; up to here they are ground */
#define SLIP_KEEP 15              /* Mario::updateFloorCode: frames on a slope before its floor code turns slip */
#define SLIP_ACCEL 1.2f           /* ponytail: slide speed gain per frame x sin(slope); Petari's slip move is not
                                   * ported, tune by eye */
#define SLIP_MAX 30.0f
#define GROUNDING 20.0f           /* mGravityGrounding: downward step while on the ground keeps Mario on crests */
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
    int seesaw;      /* PhantomShipHandle: MapPartsSeesaw1AxisRotator, turned by Mario's weight (update_seesaws) */
    int sky;         /* sky dome (Petari Sky): centred on the camera, drawn first */
    int sun;         /* BrightSun: its Sun model far off the camera along its rotation, drawn with the skies */
    int steam;       /* LavaSteam: vents on a cycle and burns (update_steam) */
    int geyser;      /* LavaGeyser: shoots up on a cycle and burns (update_geyser, geyser_height) */
    int assembly;    /* AssemblyBlock: floats at its home, flies into place when Mario is near (assembly_place) */
    V3 gup;          /* AssemblyBlock: up at its home (its bobbing) */
    int jstand;      /* JumpStand: holds Mario landing on it, then throws him up (update_jumpstands) */
    int warppod;     /* WarpPod: takes Mario to its pair (warp_try, warp_update) */
    int kiraira;     /* Kiraira: a chained mine, explodes on touch and recovers (update_kiraira, kiraira_pos) */
    int kgun;        /* KillerGunnerSingle: charges and fires its shell (update_kgunners) */
    int orock;       /* OtaRock: throws coconuts and fire balls, down by a coconut sent back (update_orocks) */
    int hhp;         /* HammerHeadPackun: swings its head at Mario, down by a stomp in its chance (update_hhps) */
    int vine;        /* Plant (the beanstalk): its seed; the stalk grows and Mario climbs it (update_vines) */
    int ibubble;     /* ItemBubble: bobs holding coins / star pieces, bursts at a touch or a shot (update_ibubbles) */
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
static unsigned char *row_mp_a;  /* per OBJS row: its SW_A came on once (MapObjActor::exeWait started its parts) */
static unsigned short *row_rhold; /* frames it still waits at a rail point */
static const unsigned char *scene_blob_g; /* the scene blob (PATH) for rebinds */
static int hip_hit, hip_inst;   /* Mario landed a hip drop this frame, on instance hip_inst (-1 none) */
static int mario_ground = -1;   /* instance Mario stands on this frame, -1 in the air */
static float cur_x = 320, cur_y = 224; /* the star pointer's stand-in, moved by the right stick (HUD units) */
static float cur_vx, cur_vy;            /* its screen velocity (getStarPointerScreenVelocity) */
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
    int bstate, bt;                /* its tail ball: 0 Wait, 1 Shoot, 2 Charge, 3 Reverse, 4 Rebound, 5 Lock */
    V3 bpos, bvel;
    int seq, nt, dnrv, dt, ddir;   /* DinoPackunVs1's step, its nerve's frames, its damage state, its frames, the hit's side
                                    * (1 left, 2 right, 3 top) */
    int prev_nrv, react;           /* the nerve a punch interrupted; a star piece's hit reaction (frames left) */
    V3 tp[9], tv[9], tdir[9];      /* DinoPackunTail's nodes (TailRoot, Tail1..7; the ball is bpos / bvel): world
                                    * position, velocity, mNodeDirection */
    int chain_ok, tlock1;          /* the tail active (activate: resetJoint done); node 1 locked */
    float t18;                     /* DinoPackunTail _18 (addAccelToBck: DinoPackunStateAwake) */
    Anim *anim, *brk;              /* its pose; its egg shell break model's (mShellBreakModel) */
} dino = {.row = -1};
static const Model *m_dino, *m_dball, *m_degg, *m_deggcrack, *m_deggbrk;
/* OtaKing (Petari Boss/OtaKing.cpp, Enemy/CocoNutBall.cpp, Enemy/FireBall.cpp, Enemy/FireBubble.cpp): one per scene */
#define OTA_BALLS 3
#define OTA_FIRES 6
#define OTA_BUBBLES 6
static struct {
    int row, nrv, t, hits, ec;       /* its nerve (OTA_*), the nerve's frames, mHits, _EC (coconuts thrown) */
    float yaw, turn;                 /* mRotation.y from its placed front (degrees), mTurnSpeed */
    float c0, c4;                    /* the coconuts' throw height range (arg0 _C0 450, arg2 _C4 0) */
    Anim *anim, *foot[2];            /* its body, its front feet (OtaKingFoot: mFeet[0] the R clips, [1] the L ones) */
    Anim *magma, *lfoot[4];          /* OtaKingMagma, the four OtaKingLongFoot (mLongFeet) */
    char mact[16], lact[4][12];      /* their clips: startBckThisAndBloomModel / the feet's nerves */
    int mseq, lseq[4], lt[4], lnrv[4], ldrawn[4], mdrawn; /* restart counters, the feet's nerve frames / nerve (0
                                      * hidden, 1 Wait, 2 Damage, 3 AppearDemo, 4 DownDemo) */
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
#define MAX_OROCKS 4
enum { OR_WAIT, OR_THROWCOCO, OR_THROWFIRE, OR_DOWN, OR_HIDE, OR_HIDEWAIT, OR_SHOW, OR_DEAD };
static struct {
    int row, nrv, t, wait, cnt;
    V3 pos, up, f;
    struct { V3 pos, vel, grav, up; int state, t, rally; } ball[4]; /* CocoNutBall: 0 dead, 1 Throw, 2 HitBackToHost, 4 Rebound */
    struct { V3 pos, vel, up; int alive; } fire[3];
} orocks[MAX_OROCKS];
static int norocks;
static int orock_of(int row);
static int hhp_blow(V3 p, float rad);
static int hhp_of(int row);
static V3 hhp_joint(const Inst *in, int k, int j, float x);
static const Model *m_pleaf; /* HammerHeadPackun's leaf */
#define MAX_HHPS 4
enum { HH_WAIT, HH_SEARCH, HH_ATTACK, HH_REST, HH_RECOVER, HH_DAMAGE, HH_RISE, HH_HIT, HH_CROW, HH_SMASHKILL,
       HH_BLOWKILL };
static struct {
    int row, nrv, step, imm, ct, clen, spun;
    V3 pos, up, f; /* _94, _A0 */
} hhps[MAX_HHPS];
static int nhhps;
static void seesaw_matrix(Inst *in);
static int nseesaws;
static int nfollows;
#define MAX_FOLLOWS 8
#define FOLLOW_N 16
static struct { int row, nrv, step, n, slot[FOLLOW_N], alive; float angle; V3 x, y, z, off[FOLLOW_N], vel[FOLLOW_N]; } follows[MAX_FOLLOWS];
#define MAX_SEESAWS 8
static struct { int row, nrv, step, hip; float vel, angle; } seesaws[MAX_SEESAWS];
static int seesaw_of(int row);
#define MAX_VINES 4
#define VINE_PTS 256
#define VINE_LEAVES 32
enum { VN_WAITFAR, VN_SEEDWAIT, VN_WAITDEMO, VN_DEMOWAIT, VN_GROWUP, VN_HANGWAITGROW, VN_HANGUPGROW, VN_GROWTHSTOP,
       VN_GROWTHWAIT, VN_GROWTHWAITINVALID, VN_HANGSTART, VN_HANGUP, VN_HANGDOWN };
typedef struct { V3 pos, side, up, front; float thick; } VPoint;
static struct {
    int row, nrv, step, alive, demo, top_on, rider, grabbed_top, accel_t, n, ngrown, nleaves, rail_n, grow_t, seeded;
    const float *rail;
    float len, coord, ride_coord, ride_vel, stalk_len, growth, grow_speed, jump, jump_oy;
    int dir;
    V3 pos, up, front, base;
    float seed_m[16]; /* mSeedMtx */
    VPoint info[VINE_PTS], stalk[VINE_PTS];
    struct { float coord, size, spring, sspeed; V3 pos, side, up, front; float m[16]; } leaf[VINE_LEAVES];
} vines[MAX_VINES];
static int nvines, vine_ride = -1, vine_test_sq;
static const Model *m_vtop, *m_vleaf;
static int vine_try(Player *m);
static int vine_hold(Player *m);
static const Model *m_ota, *m_otafoot, *m_coco, *m_fireball2, *m_firebubble, *m_otamagma, *m_otalfoot;
/* OtaKingMagma's clip (startBckThisAndBloomModel); one-shots go back to Wait (Down: EndWait) at their end */
void ota_lfoot_mtx(int k, float out[16]);
static int lstep_of(int k) { static const int st[4] = {3, 8, 14, 18}; return st[k]; }
static void ota_magma(const char *clip) { snprintf(ota.mact, sizeof ota.mact, "%s", clip); ota.mseq++; }
static void ota_lfoot(int k, int nrv, const char *clip)
{
    ota.lnrv[k] = nrv; ota.lt[k] = 0;
    if (clip) { snprintf(ota.lact[k], sizeof ota.lact[k], "%s", clip); ota.lseq[k]++; }
}
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
typedef struct { int row; V3 pos, front, up, fpos; int freeze, ftime; float rotz; } Killer; /* HomingKiller: its bullet
                                                     * (update_killers): mUp, mFreezePos, mFreezeTime, mRotation.z */
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
    if (strcmp(name, "TakoBarrelA") == 0) { /* mBreakFlags 1: only its SW_B (initCaseUseSwitchB) breaks it */
        if (r->arg[3] == -1) *coins = r->arg[6] > 0 ? r->arg[6] : 1;
        else if (r->arg[3] == 3) *pieces = r->arg[6] > 0 ? r->arg[6] : 5;
        return 4;
    }
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
static int pieces_cap, pieces_base; /* star piece slots: the placed ones, then launched ones (reused once gone) */
static V3 *piece_vel;           /* a launched star piece's velocity (StarPiece::exeFall) */
static short *piece_t;          /* its frames since launch, 0 a placed one */
#define MAX_WRECKS 16           /* break models / broken objects left (break_inst, update_wrecks) */
static struct { const Model *brk, *left; float m[16]; int t, coins, pieces, row; V3 p, up; } wrecks[MAX_WRECKS];
static int nwrecks;
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
/* CoinRotater (degrees a frame): coins 8, QuestionCoin its in-water matrix 4; StarPiece 15; PowerStar::processWait 3 */
static float coin_rot, qcoin_rot, piece_rot, star_rot;
static V3 cam_pos;
static const Model *m_packunhead;
static const Model *m_coin, *m_purple, *m_piece, *m_kuribo, *m_takohei, *m_packun, *m_kotebug, *m_spider, *m_meramera, *m_takobo, *m_mkoopa, *m_kari, *m_kmini, *m_karon, *m_teresa, *m_bfly, *m_stink, *m_unizo[3], *m_note, *m_poihana, *m_petari;
static const Model *m_mario_skin; /* Mario.arc, skinned (SKIN chunk) */
static Anim *mario_anim;
/* ElectricRail (Petari ElectricRail.cpp, ElectricRailHolder.cpp): one segment per pair of rail points, arg0 rails
 * stacked 100 apart against gravity (default 1) */
typedef struct { V3 a, b; int h, row; } Spark;
static Spark *erails;
static int nerails;
static const Model *m_erail, *m_fireball, *m_rock;
/* RockCreator / Rock (Petari RockCreator.cpp, Rock.cpp; WanwanRolling(Mini / Gold) are RockCreators too): see
 * update_rocks */
typedef struct { int row, type, nrocks, first, timer, valid, active, delay; float speed; const unsigned char *rail;
                 const float *pts; u32 np; int closed; float len; } RockC;
typedef struct { int c, alive, nrv, step, railpt, on_ground, on_wall, bind, air, normalgrav, invbind, slow, ftime,
                 unfreeze, bt; float coord, rotx, gravrate, rad, rotspeed; V3 pos, prev, vel, fall, front, grav, gn, wn,
                 fpos, bpos, bup, bfront; } Rock;
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
static RockC *rockcs;
static int nrockcs;
static Rock *rocks;
static int nrocks;
static const Model *m_rockbreak, *m_wanwan, *m_wanwanmini, *m_wanwanbreak;
static V3 rot_axis(V3 v, V3 a, float ang);
static void needle_attacked(V3 p, float r);
static void launch_pieces(V3 p, V3 up, int n, float h, float v);
static V3 rail_at(const float *p, u32 n, float d);
static unsigned game_frame;      /* frames since the scene started (FireBar angles) */
static int scene_stop;           /* MR::stopScene: frames the actors stay still (a hit's pause) */
/* EarthenPipe (Petari EarthenPipe.cpp): pipes sharing arg3 are a pair; the top is 100 * scale.y along the pipe's up
 * (-gravity, or its Y axis when arg6); a binder of radius 70 at 50 below the top takes Mario if he is within 50 of
 * the axis or above the top, and the partner throws him out with Y * arg1 + Z * arg0 (default 20, 5). A pipe works
 * again once Mario stood on ground 30 frames after the warp. Mario goes through bound (pipe_bind: Ready, In, the
 * hidden partner rising, Out, the jump). ponytail: no camera, no water pipes' stream */
typedef struct { V3 top, up, front; float hf, vf; int id, partner, zone, row, mode; } Pipe; /* mode arg2: 1 / 2
                    * hidden until the partner is used (row_step[row] 1 while out), 1 hides again after the exit */
static int pipe_out = -1; /* the mode 1 pipe Mario came out of, hidden once the pipes re-arm */
static Pipe *pipes;
static int npipes, pipe_wait, pipe_zone = -1;
static int pipe_seq, pipe_from, pipe_t; /* bound in a pipe: 1 Ready, 2 PlayerIn, 3 TargetPipeShowUp, 4 PlayerOut */
static V3 pipe_p0, pipe_u0, pipe_f0;  /* Mario's matrix as he was bound (_120) */
static int pipe_bind(Player *m);
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
 * PlantMember::tryPush) shakes it; a member shaken by a touch gives its item (update_plants). Mario's spin within the
 * group's sensor + 600 (ACTMES_SPIN_STORM_RANGE) shakes the members by distance but gives nothing. Every 300 frames
 * one member with an item plays HintShake. ponytail: the ray is stepped spheres; the members are drawn still (their
 * Wait / Shake / HintShake clips are not played) and the HintShakeLeaf effect is not in the FX table */
typedef struct { V3 pos, up; int ok, item; short nrv, step; char touch; } Plant;
enum { PL_WAIT, PL_HINT, PL_WEAK, PL_MIDDLE, PL_STRONG }; /* PlantMember nerves */
typedef struct { V3 c; float r; int hint_t, hint_i; } PlantGroupS; /* its bounding centre, sensor radius, hint timer */
static PlantGroupS *pgroups; /* per OBJS row */
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

/* Interpolation frames per clip from Petari's marioAnimeTable (MarioAnimatorData.hpp, XanimeGroupInfo _8); one-shot
 * clips start over when played again (changeAnimation) */
static void mario_play(const char *clip, float speed, int loop)
{
    static const struct { const char *clip; int frames; } t[] = {
        {"wait", 6}, {"walk", 6}, {"walksoft", 6}, {"run", 6}, {"damagewait", 6}, {"squatwait", 2}, {"squatwalk", 2},
        {"fall", 15}, {"jumpbroad", 6}, {"jumpbroadland", 6}, {"jumpback", 6}, {"jumpbackland", 6}, {"jumprollland", 6},
        {"jumpturn", 6}, {"wallslide", 10}, {"slidehipforward", 6}, {"hipdropstart", 2}, {"hipdropland", 6},
        {"brake", 4}, {"runend", 4}, {"wallhit", 6}, {"push", 6}, {"spin2nd", 1}, {"squatend", 6}};
    int f = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (strcmp(t[i].clip, clip) == 0) f = t[i].frames;
    if (!loop && mario_anim->clip_id >= 0 && strcmp(sc->clips[mario_anim->clip_id].name, clip) == 0) {
        mario_anim->frame = 0; mario_anim->done = 0; mario_anim->speed = speed; mario_anim->loop = 0;
        return;
    }
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
        p += 24 + 12 * np + 32 * nr;
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
    row_mp_a = mem_alloc(MEM_OBJECTS, s->nobjs);
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
            for (int k = 0; k < 8; k++) {
                Anim *an = mem_alloc(MEM_PLAYER, sizeof(Anim));
                memset(an, 0, sizeof(Anim));
                if (k >= 4) ota.lfoot[k - 4] = an; else if (k == 3) ota.magma = an; else if (k) ota.foot[k - 1] = an; else ota.anim = an;
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
            dino.brk = mem_alloc(MEM_PLAYER, sizeof(Anim)); memset(dino.brk, 0, sizeof(Anim));
            break;
        }
    npieces = count_type("StarPiece", 0) + box_pieces;
    int t_spg = scene_type(s, "StarPieceGroup"), t_flow = scene_type(s, "StarPieceFlow");
    for (u32 i = 0; i < s->nobjs; i++)
        if (s->objs[i].type == t_spg)
            npieces += star_group(NULL, &s->objs[i], blob);
        else if (s->objs[i].type == t_flow)
            npieces += star_flow(NULL, 0, &s->objs[i], blob);
    nkuribo = count_type("Petari", 0) + count_type("Poihana", 0) + count_type("Unizo", 0) + count_type("UnizoLand", 0) + count_type("UnizoShoal", 0) + count_type("StinkBugSmall", 0) + count_type("Kuribo", 0) + count_type("TakoHei", 0) + count_type("PackunPetit", 0) + count_type("KoteBug", 0) +
              count_type("JumpSpider", 0) + count_type("Meramera", 0) + count_type("Takobo", 0) +
              count_type("MechanicKoopaMini", 0) + count_type("Karikari", 0) +
              count_type("KuriboMini", 0) + count_type("Karon", 0) +
              count_type("Teresa", 0) + count_type("Butterfly", 0);
    int ntspots = count_type("TreasureSpot", 0) + count_type("CoinFlower", 0); /* a coin each when shot */
    for (u32 i = 0, tb = scene_type(s, "ItemBubble"); i < s->nobjs; i++) /* ItemBubble's coins (arg0 0, arg1 of them) */
        if (s->objs[i].type == (int)tb && s->objs[i].arg[0] <= 0) ntspots += s->objs[i].arg[1] > 1 ? s->objs[i].arg[1] : 1;
    box_coins += nkuribo + ntspots; /* each drops a coin (appearCoinPop) */
    ncoins += nkuribo + ntspots;
    coins = mem_alloc(MEM_OBJECTS, ncoins * sizeof(Pickup));
    purple = mem_alloc(MEM_OBJECTS, npurple * sizeof(Pickup));
    pieces_cap = npieces + 256;
    pieces = mem_alloc(MEM_OBJECTS, pieces_cap * sizeof(Pickup));
    piece_vel = mem_alloc(MEM_OBJECTS, pieces_cap * sizeof(V3));
    piece_t = mem_alloc(MEM_OBJECTS, pieces_cap * sizeof(short));
    memset(piece_t, 0, pieces_cap * sizeof(short));
    kuribo = mem_alloc(MEM_OBJECTS, nkuribo * sizeof(Kuribo));
    pops = mem_alloc(MEM_OBJECTS, (box_coins + 1) * sizeof(Pop));
    mario = mem_alloc(MEM_PLAYER, sizeof(Player));
    nkillers = 0;
    norocks = 0;
    nhhps = 0;
    nvines = 0; vine_ride = -1;
    nseesaws = 0;
    nfollows = 0;
    nflows = 0;
    kari_cling = 0;
    for (int i = 0; i < 5; i++) kari_slot[i] = -1;

    int ic = 0, ip = 0, is = 0, ik = 0;
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    int t_tako = scene_type(s, "TakoHei"), t_packun = scene_type(s, "PackunPetit"), t_kote = scene_type(s, "KoteBug");
    int t_spider = scene_type(s, "JumpSpider"), t_mera = scene_type(s, "Meramera"), t_takobo = scene_type(s, "Takobo");
    int t_mkoopa = scene_type(s, "MechanicKoopaMini"), t_kari = scene_type(s, "Karikari"), t_kmini = scene_type(s, "KuriboMini");
    int t_karon = scene_type(s, "Karon"), t_teresa = scene_type(s, "Teresa"), t_bfly = scene_type(s, "Butterfly");
    int t_stink = scene_type(s, "StinkBugSmall"), t_uni = scene_type(s, "Unizo"), t_unil = scene_type(s, "UnizoLand"),
        t_unis = scene_type(s, "UnizoShoal"), t_poi = scene_type(s, "Poihana"), t_petari = scene_type(s, "Petari");
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
                 r->type == t_uni || r->type == t_unil || r->type == t_unis || r->type == t_poi || r->type == t_petari) {
            memset(&kuribo[ik], 0, sizeof(Kuribo));
            kuribo[ik].dir = r->type == t_unil ? 1 : r->type == t_unis ? 2 : 0; /* Unizo: sea, land, shoal */
            kuribo[ik].kind = r->type == t_petari ? WALK_PETARI : r->type == t_poi ? WALK_POIHANA : r->type == t_uni || r->type == t_unil || r->type == t_unis ? WALK_UNIZO : r->type == t_stink ? WALK_STINKBUG : r->type == t_bfly ? WALK_BUTTERFLY : r->type == t_teresa ? WALK_TERESA : r->type == t_karon ? WALK_KARON : r->type == t_kmini ? WALK_KMINI : r->type == t_kari ? WALK_KARIKARI : r->type == t_mkoopa ? WALK_MKOOPA : r->type == t_takobo ? WALK_TAKOBO : r->type == t_tako ? WALK_TAKOHEI : r->type == t_packun ? WALK_PACKUN :
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
    npieces = is; pieces_base = 0;
    nwrecks = 0;
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
        { V3 x, z; row_axes(r, &x, &z); L->up = vnorm(vcross(z, x)); } /* MR::calcUpVec */
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
                erails[n].row = i;
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
        pgroups = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(PlantGroupS));
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
            for (int k = 0; k < n; k++) { g[k].nrv = PL_WAIT; g[k].step = 0; g[k].touch = 3; }
            pgroups[i].hint_t = (3 + rand() % 7) * 10; /* MR::getRandom(3, 10) * 10 */
            pgroups[i].hint_i = 0;
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
    { /* RockCreator::init: rocks for (rail length / speed + 85) / delay + 2, or one */
        int t_rc = scene_type(s, "RockCreator"), t_ww = scene_type(s, "WanwanRolling"), t_wm = scene_type(s, "WanwanRollingMini"),
            t_wg = scene_type(s, "WanwanRollingGold"), nc = 0, nr = 0;
        for (int pass = 0; pass < 2; pass++) {
            nc = nr = 0;
            for (u32 i = 0; i < s->nobjs; i++) {
                const Row *r = &s->objs[i];
                if ((r->type != t_rc && r->type != t_ww && r->type != t_wm && r->type != t_wg) || r->path < 0) continue;
                u32 np; int closed;
                const float *pts = find_rail(blob, r->zone, r->path, &np, &closed);
                if (!pts || np < 2) continue;
                float len = 0, speed = r->arg[0] != -1 ? (float)r->arg[0] : 10.0f;
                for (u32 k = 0; k + 1 < np; k++)
                    len += vlen(vsub(v3(pts[3 * k + 3], pts[3 * k + 4], pts[3 * k + 5]), v3(pts[3 * k], pts[3 * k + 1], pts[3 * k + 2])));
                int delay = r->arg[1] != -1 ? r->arg[1] * 60 : -1;
                int n = delay < 0 ? 1 : ((int)(len / speed) + 85) / (delay > 0 ? delay : 1) + 2;
                if (pass) {
                    RockC *c = &rockcs[nc];
                    memset(c, 0, sizeof *c);
                    c->row = i; c->type = r->type == t_ww ? 1 : r->type == t_wm ? 2 : r->type == t_wg ? 3 : 0;
                    c->speed = speed; c->delay = delay; c->nrocks = n; c->first = nr; c->valid = 1;
                    c->rail = find_path(blob, r->zone, r->path); c->pts = pts; c->np = np; c->closed = closed; c->len = len;
                    for (int k = 0; k < n; k++) { memset(&rocks[nr + k], 0, sizeof(Rock)); rocks[nr + k].c = nc; }
                }
                nc++; nr += n;
            }
            if (!pass) {
                rockcs = mem_alloc(MEM_OBJECTS, (nc + 1) * sizeof(RockC));
                rocks = mem_alloc(MEM_OBJECTS, (nr + 1) * sizeof(Rock));
            }
        }
        nrockcs = nc; nrocks = nr;
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
    mario->jump_kind = 0; mario->stand_neutral = 10;
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

/* KillerGunnerSingle.cpp: Wait 120 frames, Charge until its KillerGunnerSingleCharge BTK stops (99 frames), Shoot
 * playing KillerGunnerSingleShoot: its "shell" sensor, 250 at (0, 0, 700) in its frame, valid from step 0 to 40,
 * hurts Mario (sendMsgEnemyAttackMaximum: damageLarge) and the nerve turns Attack; the BCK's end (59) back to Wait.
 * row_turn the nerve (0 Wait, 1 Charge, 2 Shoot, 3 Attack), row_step its step. ponytail: the charge BTK is not shown,
 * no KillerGunnerSingleSmoke effect (not in the FX table) */
static void update_kgunners(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->kgun || !row_shown(in->row)) continue;
        unsigned short *t = &row_step[in->row];
        signed char *ph = &row_turn[in->row];
        if (*ph == 0 && *t == 120) { *ph = 1; *t = 0; }
        else if (*ph == 1 && *t >= 99) { /* isBtkStopped */
            obj_play(in, "killergunnersingleshoot", 0, 0);
            *ph = 2; *t = 0;
        } else if (*ph == 2 || *ph == 3) {
            if (*ph == 2 && *t < 40 && !mario->hurt_timer) {
                V3 shell = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(vnorm(v3(in->m[8], in->m[9], in->m[10])), 700.0f));
                if (vlen(vsub(c, shell)) < 250.0f) {
                    hurt_mario(shell, MC.mJumpDistLargeDamage);
                    *ph = 3;
                    printf("killer gunner %d hits\n", in->row);
                }
            }
            if (*t >= 59 || (in->anim && in->anim->done)) { *ph = 0; *t = 0; continue; } /* isBckStopped */
        }
        ++*t;
    }
}

static void pop_coins(V3 p, V3 up, int n);
/* ItemBubble.cpp: scale x 1.2, faces the camera; bobs 30 along the world's Y (sin over 120 frames) with its items'
 * frame (_CC) trailing at 0.1; arg0 0: arg1 coins (1 to 3 at fixed spots, more in a ring of 60 about Z, 60 lower),
 * 1: arg1 star pieces (one at its centre, more in the ring); the items turn 8 degrees a frame. Mario's body sensor (100)
 * on its sensor (120 x scale) or a star piece shot bursts it (kill: the dead switch, the coins appearCoinFix, the star
 * pieces launched 24 against the gravity). ponytail: the coins pop instead of appearing fixed, no rail (Good Egg's
 * have none), no BubbleBreak effect (not in the FX table) */
static int bubble_items(const Row *r, V3 *off)
{
    int type = r->arg[0] > 0 ? r->arg[0] : 0, n = r->arg[1] > 1 ? r->arg[1] : 1;
    if (n > 16) n = 16;
    static const float c1[1][2] = {{0, -67.38f}}, c2[2][2] = {{-55.66f, -92.29f}, {55.66f, -43.95f}},
                       c3[3][2] = {{0, -19.04f}, {-55.66f, -130.37f}, {67.38f, -130.37f}};
    for (int i = 0; i < n; i++) {
        if (type == 0 && n <= 3) {
            const float *q = n == 1 ? c1[i] : n == 2 ? c2[i] : c3[i];
            off[i] = v3(q[0], q[1], 0);
        } else if (type == 1 && n == 1) off[i] = v3(0, 0, 0);
        else { /* (0, 60, 0) turned 2 pi / n about Z each, (0, -60, 0) below for coins */
            float a = 6.2831853f * i / n;
            off[i] = v3(-60.0f * sinf(a), 60.0f * cosf(a) + (type == 0 ? -60.0f : 0.0f), 0);
        }
    }
    return n;
}

static V3 bubble_item_pos(const Inst *in, V3 off)
{
    const Row *r = &sc->objs[in->row];
    float a = r->rot[2] * 0.0174533f, k = 1.2f * r->scale[0];
    return vadd(in->head, v3((off.x * cosf(a) - off.y * sinf(a)) * k, (off.x * sinf(a) + off.y * cosf(a)) * k, off.z * k));
}

static V3 bubble_pos(const Inst *in)
{
    const Row *r = &sc->objs[in->row];
    return vadd(row_pos(r), v3(0, 30.0f * sinf(2.0f * (row_step[in->row] / 120.0f) * 3.14159265f), 0));
}

static void bubble_burst(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    V3 off[16];
    int n = bubble_items(r, off);
    for (int i = 0; i < n; i++) {
        V3 p = bubble_item_pos(in, off[i]), up = up_at(p, mario->up);
        if (r->arg[0] == 1) launch_pieces(p, up, 1, 0.0f, 24.0f);
        else pop_coins(p, up, 1);
    }
    row_kill(in->row);
    printf("item bubble %d bursts\n", in->row);
}

static void update_ibubbles(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->ibubble || !row_shown(in->row)) continue;
        row_step[in->row]++;
        V3 p = bubble_pos(in);
        in->head = vadd(in->head, vmul(vsub(p, in->head), 0.1f)); /* blendMtx(_CC, _9C, 0.1) */
        if (vlen(vsub(c, p)) < 120.0f * 1.2f * sc->objs[in->row].scale[0] + 100.0f) bubble_burst(in); /* receiveMsgPush */
    }
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
 * cos(1 - arg1 x 0.0001) a frame (arg1 default 3), kept 150 over the ground under it (cChaseHeightMin), rolling up to
 * 60 degrees a degree a frame as it turns (updateRotateZ), its up following gravity 2 degrees a frame. It breaks on
 * the map (SandColumn on ground facing up), on Mario (an explosion attack), 3000 from him while chasing, after 600
 * frames of straight flight, blowing up the walkers within 250 (sendMsgExplosionToNearActor); 180 frames later the
 * next one comes out. R2 with the pointer on it freezes it 20 frames, shaking along the camera's X. row_turn 0
 * appear, 1 move, 2 chase, 3 broken; row_step its frames. ponytail: no Smoke / SmokeChase trails (looping effects),
 * no propeller / BCKs, no target sensors (GoToTarget), Torpedo / MagnumKiller not handled */
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
static void walker_explosion(V3 from); /* sendMsgExplosionToNearActor: the walkers within 250 */
static int line_hits(V3 a, V3 b);
/* updateRotateZ: roll towards the turn (cChaseRotateZDot 0.95, a degree a frame, at most 60), else back to 0 */
static void killer_rotz(Killer *k, V3 dir, V3 grav)
{
    V3 f = vtangent(k->front, grav), s = vtangent(dir, grav);
    if (fabsf(vdot(k->front, grav)) < 0.999f && fabsf(vdot(vnorm(dir), grav)) < 0.999f && vdot(f, s) < 0.95f) {
        k->rotz += vdot(vcross(f, s), grav) > 0.0f ? 1.0f : -1.0f;
        k->rotz = k->rotz > 60.0f ? 60.0f : k->rotz < -60.0f ? -60.0f : k->rotz;
    } else k->rotz = k->rotz < 0 ? (k->rotz + 1.0f > 0 ? 0 : k->rotz + 1.0f) : (k->rotz - 1.0f < 0 ? 0 : k->rotz - 1.0f);
}
static V3 killer_target(const Killer *k, V3 grav) /* calcFrontVecToTarget, then the shadow's height check */
{
    V3 t = vnorm(vsub(vadd(mario->pos, vmul(vnorm(mario->up), 200.0f)), k->pos));
    if (line_hits(k->pos, vadd(k->pos, vmul(grav, 150.0f)))) { /* isNear(shadow position, cChaseHeightMin) */
        V3 h = vtangent(t, k->up);
        if (vlen(h) > 1e-6f) t = vnorm(h);
    }
    return t;
}
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
        if (*ph == 0) { /* exeAppear */
            V3 f = vnorm(vtangent(v3(in->m[8], in->m[9], in->m[10]), up));
            int s = *t < 20 ? *t : 20;
            k->front = f; k->up = up; k->rotz = 0; k->freeze = 0;
            k->pos = vadd(base, vmul(f, 200.0f * s / 20.0f));
            if (*t > 20 && *t < 42) {
                int u = *t - 20;
                k->pos = vadd(k->pos, vmul(f, (22 - u) * sinf(u * 100.0f * 0.0174533f) * 18.0f / 22.0f));
            }
            if (*t >= 72) {
                float m[16];
                basis_matrix(m, k->pos, k->up, k->front, 0);
                fx_emit(FX_HOMINGKILLER_SHOOT, m);
                *ph = 1; *t = 0;
            }
            continue;
        }
        V3 g = vmul(up_at(k->pos, up), -1.0f);
        int brk = 0;
        if (!k->freeze && pointer_a() && cursor_sees(k->pos, 40.0f)) { /* tryFreeze */
            k->freeze = 1; k->fpos = k->pos;
            printf("homing killer %d frozen\n", in->row);
        }
        if (k->freeze) { /* exeFreeze: still, shaking along the camera's X, its timers held */
            --*t;
            k->ftime++;
            V3 ce, cw, cu;
            float fovy;
            camera_pose(&ce, &cw, &cu, &fovy);
            V3 camx = vnorm(vcross(vnorm(vsub(cw, ce)), cu));
            float sh = cosf(fmodf(k->ftime * 75.0f, 360.0f) * 0.0174533f) * 5.0f * (20 - k->freeze) / 20.0f;
            k->pos = vadd(k->fpos, vmul(camx, sh));
            if (*ph == 2) { V3 tg = killer_target(k, g); k->front = turn_to(k->front, tg, 0.9997f); killer_rotz(k, tg, g); }
            if (pointer_a() && cursor_sees(k->pos, 40.0f)) k->freeze = 1; /* setNerve(Freeze): starts over */
            else if (++k->freeze > 20) { k->pos = k->fpos; k->freeze = 0; }
            if (vlen(vsub(c, k->pos)) < 70.0f + HIT_BODY_R) { hurt_mario(k->pos, 25.0f); brk = 1; }
            if (!brk) continue;
        }
        if (!brk && *ph == 1) { /* processMove, tryChaseStart */
            V3 lvl = vnorm(vtangent(k->front, g));
            if (vlen(lvl) > 0.5f) k->front = turn_to(k->front, lvl, 0.9995f);
            if (*t > 600) brk = 1;
            V3 to = vsub(mario->pos, k->pos);
            float d = vlen(to), start = r->arg[0] > 0 ? (float)r->arg[0] : 2000.0f;
            if (*t >= 35 && r->arg[2] <= 0 && d < start && d > 0 &&
                (r->arg[3] > 0 || vdot(vmul(to, 1.0f / d), k->front) > 0.0f)) { *ph = 2; printf("homing killer %d chases\n", in->row); }
            k->up = vmul(g, -1.0f);
        } else if (!brk) { /* processChase */
            V3 tg = killer_target(k, g);
            k->front = turn_to(k->front, tg, 1.0f - (r->arg[1] >= 0 ? r->arg[1] : 3) * 0.0001f);
            killer_rotz(k, tg, g);
            if (vlen(vsub(mario->pos, k->pos)) > 3000.0f) brk = 1;
            k->up = turn_to(k->up, vmul(g, -1.0f), cosf(2.0f * 0.0174533f)); /* cUpVecRotateSpeed */
        }
        if (!brk) k->pos = vadd(k->pos, vmul(k->front, 12.0f));
        Hit h[1];
        int ground = 0;
        if (!brk && vlen(vsub(c, k->pos)) < 70.0f + HIT_BODY_R) {
            hurt_mario(k->pos, 25.0f);
            printf("homing killer %d hits Mario\n", in->row);
            brk = 1;
        } else if (!brk && (*ph == 2 || *t >= 35) && kcl_query(k->pos, 70.0f, h, 1)) {
            printf("homing killer %d breaks on the map\n", in->row);
            brk = 1;
            ground = vdot(g, h[0].n) < -0.75f; /* cSandColumnEmitDot */
        }
        if (brk) { /* exeBreak */
            float m[16];
            basis_matrix(m, k->pos, vmul(g, -1.0f), k->front, 0);
            fx_emit(FX_HOMINGKILLER_EXPLOSION, m);
            if (ground) fx_emit(FX_HOMINGKILLER_SANDCOLUMN, m);
            walker_explosion(k->pos);
            needle_attacked(k->pos, 250.0f);
            *ph = 3; *t = 0; k->freeze = 0;
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
 * pointer on it holds it still (StopPointing). Either stops the others of its group (GroupId: ACTMES_GROUP_MOVE_STOP,
 * AttackWait) until it moves again (ACTMES_GROUP_MOVE_START). Birikyu (BirikyuWithFace) faces its rail's way (arg1
 * 1 backwards) or turns about its Y as it circles. row_turn 0 not started, 1 moving, 2 attack, 3 waiting at an end,
 * 4 waiting for its group, 5 held by the pointer; row_coord / row_rdir on a rail, row_angle the circle; row_step its
 * frames. ponytail: no shadow, no electric loop effect (its own-named one) */
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
    if (in->biri != 2) return;
    /* BirikyuWithFace::calcAndSetBaseMtx */
    float s = r->scale[0];
    V3 x, y, z;
    if (in->biri_rail) {
        const float *pp = (const float *)(in->biri_rail + 24);
        u32 np = *(const u32 *)(in->biri_rail + 8);
        V3 dir = vnorm(vsub(rail_at(pp, np, row_coord[in->row] + 1.0f), rail_at(pp, np, row_coord[in->row] - 1.0f)));
        if (r->arg[1] == 1) dir = vmul(dir, -1.0f);
        if (row_rdir[in->row] < 0) dir = vmul(dir, -1.0f);   /* !isRailGoingToEnd */
        V3 up = v3(0, 1, 0);                                   /* _AC */
        x = dir;
        if (fabsf(vdot(up, dir)) > 0.999f) { z = vnorm(vtangent(v3(0, 0, 1), x)); y = vcross(z, x); } /* makeMtxSideFront */
        else { y = vnorm(vtangent(up, x)); z = vcross(x, y); }                                         /* makeMtxSideUp */
    } else {
        float base[16], one[3] = {1, 1, 1};
        mat_translate_rot_scale(base, r->pos, r->rot, one);
        V3 ax = vnorm(v3(base[4], base[5], base[6]));
        float a = row_angle[in->row];
        x = rot_axis(v3(1, 0, 0), ax, a); y = rot_axis(v3(0, 1, 0), ax, a); z = rot_axis(v3(0, 0, 1), ax, a); /* makeRotate(_AC, _C4) */
    }
    in->m[0] = x.x * s; in->m[1] = x.y * s; in->m[2] = x.z * s;
    in->m[4] = y.x * s; in->m[5] = y.y * s; in->m[6] = y.z * s;
    in->m[8] = z.x * s; in->m[9] = z.y * s; in->m[10] = z.z * s;
}

/* sendMsgToGroupMember: stop (AttackWait) the moving members, or start (goMove) the waiting ones */
static void biri_group(const Inst *from, int start)
{
    const Row *r = &sc->objs[from->row];
    if (r->group < 0) return;
    for (int i = 0; i < ninsts; i++) {
        const Inst *q = &insts[i];
        if (!q->biri || q == from) continue;
        const Row *o = &sc->objs[q->row];
        if (o->group != r->group || o->zone != r->zone) continue;
        if (!start && row_turn[q->row] == 1) { row_turn[q->row] = 4; row_step[q->row] = 0; }
        else if (start && row_turn[q->row] != 1) { row_turn[q->row] = 1; row_step[q->row] = 0; }
    }
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
        if (*ph == 4) { /* exeAttackWait */ }
        else if (*ph == 5) { if (!(pointer_a() && cursor_sees(p, 30.0f))) { *ph = 1; *t = 0; biri_group(in, 1); } } /* StopPointing */
        else if (*ph == 2) { if (*t >= 90) { *ph = 1; *t = 0; biri_group(in, 1); } }
        else if (*ph == 3) {
            const unsigned char *e = in->biri_rail;
            u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
            const unsigned char *pa = e + 24 + 12 * np + 12 * nr;
            int wait = *(const int *)(pa + 12 * (row_coord[row] > 0 ? nr - 1 : 0) + 4);
            if (*t >= wait) { *ph = 1; *t = 0; }
        } else if (pointer_a() && cursor_sees(p, 30.0f)) { *ph = 5; *t = 0; biri_group(in, 0); } /* tryStopPointing */
        else {
            if (in->biri_rail) {
                const unsigned char *e = in->biri_rail;
                u32 np = *(const u32 *)(e + 8), nr = *(const u32 *)(e + 12);
                const unsigned char *pa = e + 24 + 12 * np + 12 * nr;
                float len = tsuki_len(e), n = row_coord[row] + speed * row_rdir[row];
                if (n >= len || n <= 0) { /* isRailReachedGoal: reverse, wait its point_arg0 */
                    n = n >= len ? len : 0;
                    row_rdir[row] = -row_rdir[row];
                    if (*(const int *)(pa + 12 * (n > 0 ? nr - 1 : 0) + 4) > 0) { *ph = 3; *t = 0; }
                    else if (in->biri == 2) fx_at(FX_BIRIKYU_CLASH, p, v3(0, 1, 0));
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
            if (in->biri == 2) fx_at(FX_BIRIKYU_HIT, p, v3(0, 1, 0));
            biri_group(in, 0);
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
 * 3 brake, 4 crushed, 5 crushed end; row_step its frames. Drawn skinned: its Ball joint at the head, its Y turned
 * from the base to it (ballMtxCallBack), the stalk bending between Root and Ball. ponytail: the head keeps its
 * height over the base plane, not over the ground (addVelocityKeepHeight's shadow); no collision for the head, no 2P
 * pointer snap, no breakables hit, no anim scale */
static void punch_place(Inst *in)
{
    V3 base = v3(in->m0[12], in->m0[13], in->m0[14]), up0 = vnorm(v3(in->m0[4], in->m0[5], in->m0[6]));
    float s = in->s;
    if (in->md && in->md->skin) { /* makeMtxUpNoSupportPos: its base stays; the Ball joint follows the head */
        basis_matrix(in->m, base, up0, v3(in->m0[8], in->m0[9], in->m0[10]), 0);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) in->m[j] *= s;
        return;
    }
    V3 d = vsub(in->head, base);
    float l = vlen(d);
    V3 up = l > 1e-3f ? vmul(d, 1.0f / l) : up0;
    float stretch = row_turn[in->row] == 4 ? 0.25f : l / 130.0f;
    basis_matrix(in->m, base, up, v3(in->m0[8], in->m0[9], in->m0[10]), 0);
    for (int j = 0; j < 3; j++) { in->m[j] *= s; in->m[4 + j] *= s * stretch; in->m[8 + j] *= s; }
}

/* PunchingKinoko::ballMtxCallBack: joint Ball (1) at the head, its Y turned from the base towards it (turnMtxToYDirRate
 * 1), in model space */
static void punch_ball(const Inst *in, Anim *a)
{
    if (a->skin->njoints < 2) return;
    V3 base = v3(in->m[12], in->m[13], in->m[14]);
    V3 x = vmul(v3(in->m[0], in->m[1], in->m[2]), 1.0f / (in->s * in->s)), y = vmul(v3(in->m[4], in->m[5], in->m[6]), 1.0f / (in->s * in->s)),
       z = vmul(v3(in->m[8], in->m[9], in->m[10]), 1.0f / (in->s * in->s));
    V3 dw = vsub(in->head, base), dl = v3(vdot(dw, x), vdot(dw, y), vdot(dw, z)); /* model space */
    V3 ny = vnorm(dl);
    if (vlen(ny) < 1e-6f) ny = v3(0, 1, 0);
    V3 ax = vcross(v3(0, 1, 0), ny);
    float c = ny.y, sn = vlen(ax);
    V3 cx = v3(1, 0, 0), cz = v3(0, 0, 1);
    if (sn > 1e-6f) { ax = vmul(ax, 1.0f / sn); float ang = atan2f(sn, c); cx = rot_axis(cx, ax, ang); cz = rot_axis(cz, ax, ang); }
    float *w = a->world[1];
    w[0] = cx.x; w[1] = cx.y; w[2] = cx.z; w[3] = 0;
    w[4] = ny.x; w[5] = ny.y; w[6] = ny.z; w[7] = 0;
    w[8] = cz.x; w[9] = cz.y; w[10] = cz.z; w[11] = 0;
    w[12] = dl.x; w[13] = dl.y; w[14] = dl.z; w[15] = 1;
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
                if (e->kind == WALK_PETARI) { walker_blow(e, in->head); continue; } /* isMsgToEnemyAttackBlow */
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
static int in_water(V3 p, V3 up, float *depth);
static int walker_blow(Kuribo *e, V3 from);
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
                fx_at(FX_COCONUT_SPINHITMARK, in->head, up);
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
        int broken = in->cstrong && hhp_blow(in->head, rad); /* HammerHeadPackun: isMsgToEnemyAttackBlow */
        if (in->cstrong) /* sendMsgToEnemyAttackBlow */
            for (int k = 0; k < nkuribo && !broken; k++) {
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY || e->kind == WALK_TERESA ||
                    vlen(vsub(in->head, vadd(e->pos, vmul(e->up, 50.0f)))) > rad + 60.0f) continue;
                int took = walker_blow(e, in->head);
                if (!took) continue;
                if (took < 0) { e->state = KURIBO_DEAD; row_kill(e->row); fx_at(FX_KURIBO_DEATHSMOKEKURIBO, e->pos, e->up); pop_coins(e->pos, e->up, 1); }
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
        if (in->cstrong) needle_attacked(in->head, rad); /* sendMsgToEnemyAttackBlow */
        float depth;
        if (!broken && (lost(in->head) || in_water(in->head, up, &depth))) broken = 1; /* tryDisappear */
        for (int k = 0; k < nlogic && !broken; k++)
            if (logic[k].kind == LOGIC_DEATH && row_shown(logic[k].row) && area_inside(&sc->objs[logic[k].row], in->head)) broken = 1;
        if (broken) { fx_at(FX_COCONUT_COCONUTBREAK, in->head, up); *ph = 2; *t = 0; in->m[12] = 1e9f; continue; } /* hidden until it comes back */
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
    V3 lo = c, hi = c; /* calcBoundingSphereRadius: the box of the placed members from the centre (its else-if kept) */
    for (int k = 0; k < n; k++) {
        const Plant *p = &plants[plant_first[row] + k];
        if (!p->ok) continue;
        if (p->pos.x < lo.x) lo.x = p->pos.x; else if (hi.x < p->pos.x) hi.x = p->pos.x;
        if (p->pos.y < lo.y) lo.y = p->pos.y; else if (hi.y < p->pos.y) hi.y = p->pos.y;
        if (p->pos.z < lo.z) lo.z = p->pos.z; else if (hi.z < p->pos.z) hi.z = p->pos.z;
    }
    V3 sz = vsub(hi, lo);
    float br = (sz.x < sz.y ? (sz.y < sz.z ? sz.z : sz.y) : (sz.x < sz.z ? sz.z : sz.x)) / 2.0f;
    pgroups[row].c = vmul(vadd(hi, lo), 0.5f);
    pgroups[row].r = 160.0f + br * r->scale[1]; /* sSize + radius x scale */
}

void game_rebind(void)
{
    const Scene *s = sc;
    m_coin = scene_model(s, "Coin");
    m_purple = scene_model(s, "PurpleCoin");
    m_piece = scene_model(s, "StarPiece");
    m_kuribo = scene_model(s, "Kuribo");
    m_takohei = scene_model(s, "TakoHei");
    m_packun = scene_model(s, "PackunPetit"); m_packunhead = scene_model(s, "PackunPetitHead");
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
    m_poihana = scene_model(s, "Poihana"); m_petari = scene_model(s, "Petari");
    m_marioball = scene_model(s, "MarioFireBall");
    m_bubble = scene_model(s, "AirBubble");
    m_ota = scene_model(s, "OtaKing"); m_otafoot = scene_model(s, "OtaKingFoot"); m_coco = scene_model(s, "CocoNut");
    m_otamagma = scene_model(s, "OtaKingMagma"); m_otalfoot = scene_model(s, "OtaKingLongFoot");
    m_pleaf = scene_model(s, "PackunLeaf");
    m_vtop = scene_model(s, "PlantTop"); m_vleaf = scene_model(s, "PlantLeaf");
    m_fireball2 = scene_model(s, "FireBall"); m_firebubble = scene_model(s, "FireBubble");
    m_sambohead = scene_model(s, "CocoSamboHead");
    m_dino = scene_model(s, "DinoPackun"); m_dball = scene_model(s, "DinoPackunTailBall"); m_degg = scene_model(s, "DinoPackunEggShell");
    m_deggbrk = scene_model(s, "DinoPackunEggShellBreak"); m_deggcrack = scene_model(s, "DinoPackunEggShell#EggBroken");
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
    m_rock = scene_model(s, "RockCreator"); m_rockbreak = scene_model(s, "RockBreak");
    m_wanwan = scene_model(s, "WanwanRolling"); m_wanwanmini = scene_model(s, "WanwanRollingMini");
    m_wanwanbreak = scene_model(s, "WanwanRollingBreak");
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
            r->type == t_uni || r->type == t_unil || r->type == t_unis || r->type == t_poi || r->type == scene_type(s, "Petari") || r->type == scene_type(s, "RockCreator") || r->type == scene_type(s, "WanwanRolling") ||
            r->type == scene_type(s, "WanwanRollingMini") || r->type == scene_type(s, "WanwanRollingGold") ||
            r->type == scene_type(s, "DinoPackun") || r->type == scene_type(s, "OtaKing"))
            continue;
        const char *name = s->types + 32 * r->type;
        const Model *md = scene_model(s, name);
        if (!md || (row_dead && !row_shown(i) && strcmp(name, "Plant"))) continue; /* Plant: its seed (a PartsModel) stays */
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
        in->sun = strcmp(name, "BrightSun") == 0;
        in->shoot = strcmp(name, "ShootingStar") == 0;
        in->steam = strcmp(name, "LavaSteam") == 0;
        in->geyser = strcmp(name, "LavaGeyser") == 0;
        in->assembly = strncmp(name, "AssemblyBlock", 13) == 0;
        in->jstand = strcmp(name, "JumpStand") == 0;
        in->warppod = strcmp(name, "WarpPod") == 0;
        in->kiraira = strcmp(name, "Kiraira") == 0;
        in->kgun = strcmp(name, "KillerGunnerSingle") == 0;
        in->orock = strcmp(name, "OtaRock") == 0;
        in->hhp = strcmp(name, "HammerHeadPackun") == 0;
        in->vine = strcmp(name, "Plant") == 0;
        in->ibubble = strcmp(name, "ItemBubble") == 0;
        if (in->ibubble) in->head = row_pos(r); /* its items' frame (_CC) trails the bubble */
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
        in->seesaw = strcmp(name, "PhantomShipHandle") == 0;
        if (in->seesaw) { in->rotor = 0; seesaw_matrix(in); }
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

int game_test_kgun(void)
{
    test_nolost = 1;
    for (int i = 0; i < ninsts; i++) { /* the first KillerGunnerSingle: Mario 700 along its Z (its shell sensor), still */
        const Inst *in = &insts[i];
        if (!in->kgun) continue;
        V3 z = vnorm(v3(in->m[8], in->m[9], in->m[10])), p = vadd(v3(in->m[12], in->m[13], in->m[14]), vmul(z, 700.0f));
        mario->up = up_at(p, mario->up);
        mario->pos = vsub(p, vmul(mario->up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->walk = 0;
        return in->row;
    }
    int ty = scene_type(sc, "KillerGunnerSingle"); /* its zone not in yet: Mario by its row, the zone streams in */
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == ty) {
            V3 p = row_pos(&sc->objs[i]);
            mario->up = up_at(p, mario->up);
            mario->pos = vadd(p, vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_ibubble(int touch) /* the first ItemBubble: Mario 600 off it (touch 0) or in it (1); its zone first */
{
    test_nolost = 1;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->ibubble || row_dead[in->row]) continue;
        V3 p = bubble_pos(in), up = up_at(p, mario->up);
        mario->up = up;
        mario->pos = vsub(vadd(p, vmul(vnorm(vtangent(v3(1, 0, 0), up)), touch ? 0.0f : 600.0f)), vmul(up, HIT_BODY_H));
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->walk = 0;
        return in->row;
    }
    int ty = scene_type(sc, "ItemBubble");
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == ty && !row_dead[i]) {
            V3 p = row_pos(&sc->objs[i]);
            mario->up = up_at(p, mario->up);
            mario->pos = vadd(p, vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_orock(void) /* the first OtaRock: Mario 700 in front, spinning at coconuts near him; its zone first */
{
    test_nolost = 1;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->orock || row_dead[in->row]) continue;
        int k = orock_of(in->row);
        if (k < 0) return -1;
        V3 at = vadd(orocks[k].pos, vmul(vnorm(vtangent(v3(in->m[8], in->m[9], in->m[10]), orocks[k].up)), 700.0f));
        if (vlen(vsub(mario->pos, at)) > 300.0f) { mario->pos = vadd(at, vmul(orocks[k].up, 100.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
        for (int b = 0; b < 4; b++)
            if (orocks[k].ball[b].state == 1 && vlen(vsub(orocks[k].ball[b].pos, c)) < 380.0f) mario->spin_timer = SPIN_TIME;
        mario->hurt_timer = 8;
        return orocks[k].nrv * 1000 + in->row;
    }
    int ty = scene_type(sc, "OtaRock");
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == ty && !row_dead[i]) {
            V3 p = row_pos(&sc->objs[i]);
            mario->up = up_at(p, mario->up);
            mario->pos = vadd(p, vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_hhp(void) /* the HammerHeadPackun: Mario 650 in front until its Rest, then a spin on its head, then a
                         * trample onto it in Damage; its zone first */
{
    test_nolost = 1;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->hhp || row_dead[in->row]) continue;
        int k = hhp_of(in->row);
        if (k < 0) return -1;
        typeof(&hhps[0]) o = &hhps[k];
        V3 head = hhp_joint(in, k, 5, 120.0f);
        if (o->nrv == HH_REST && o->step >= 10 && !o->spun && !mario->spin_timer) { /* beside its head, a spin */
            mario->pos = vadd(vsub(head, vmul(o->up, vdot(vsub(head, o->pos), o->up))), vmul(vnorm(vcross(o->up, o->f)), 150.0f));
            mario->up = o->up; mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME;
        } else if (o->nrv == HH_DAMAGE && o->step == 30) { /* over its head, falling */
            mario->pos = vadd(head, vmul(o->up, 150.0f));
            mario->up = o->up; mario->hvel = v3(0, 0, 0); mario->vup = -5.0f; mario->grounded = 0; mario->hip = 0;
        } else if (o->nrv != HH_REST && o->nrv != HH_DAMAGE && o->nrv < HH_SMASHKILL) {
            V3 at = vadd(o->pos, vmul(o->f, 650.0f));
            if (vlen(vsub(mario->pos, at)) > 300.0f) { mario->pos = vadd(at, vmul(o->up, 100.0f)); mario->up = o->up; mario->hvel = v3(0, 0, 0); mario->vup = 0; }
        }
        return o->nrv * 1000 + (o->step > 999 ? 999 : o->step);
    }
    int ty = scene_type(sc, "HammerHeadPackun");
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == ty) {
            if (row_dead[i]) return -3;
            V3 p = row_pos(&sc->objs[i]);
            mario->up = up_at(p, mario->up);
            mario->pos = vadd(p, vmul(mario->up, 300.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_vine(void) /* the Plant at the HammerHeadPackun: its appear switch on, grown Mario jumps into it a third
                          * of the way up and spins up it every 8 frames until he leaves it at the top */
{
    test_nolost = 1;
    int ty = scene_type(sc, "Plant");
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != ty || r->sw[SW_APPEAR] < 0 || r->sw[SW_APPEAR] != 3) continue;
        int k = -1;
        for (int j = 0; j < nvines; j++) if (vines[j].row == (int)i) k = j;
        if (k < 0) { /* its zone first */
            V3 p = row_pos(r);
            mario->up = up_at(p, mario->up);
            mario->pos = vadd(p, vmul(mario->up, 300.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
        typeof(&vines[0]) o = &vines[k];
        if (!sw_on(r, SW_APPEAR)) { sw_set((Row *)r, SW_APPEAR, 1); return -3; }
        static int done, wait;
        if (vine_ride == k) { if (game_frame % 8 == 0) vine_test_sq = 1; wait = 0; }
        else if ((o->nrv == VN_GROWTHSTOP || o->nrv == VN_GROWTHWAIT) && !done) { /* in the air beside it */
            const VPoint *q = &o->info[o->n / 3];
            mario->up = up_at(q->pos, mario->up);
            mario->pos = vadd(vsub(q->pos, vmul(mario->up, HIT_BODY_H)), vmul(q->side, 60.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 5.0f; mario->grounded = 0;
        } else if (o->nrv == VN_GROWTHWAITINVALID) done = 1;
        if (done && ++wait == 90) printf("vine test: Mario %d %d %d after the jump\n", (int)mario->pos.x, (int)mario->pos.y, (int)mario->pos.z);
        return o->nrv * 10000 + (int)o->coord;
    }
    return -1;
}

int game_test_petari(void) /* the first Petari: Mario 800 off it (it comes, waits, surfaces), then beside it spinning,
                            * then onto it while it swoons */
{
    test_nolost = 1;
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->kind != WALK_PETARI) continue;
        if (k->state == KURIBO_DEAD) return -3;
        V3 up = vmul(k->grav, -1.0f);
        if (!k->front.x && !k->front.y && !k->front.z) up = up_at(k->pos, mario->up);
        static int placed;
        if (!placed) { /* 800 off along its front */
            V3 f = vnorm(vtangent(v3(1, 0, 0), up));
            mario->up = up; mario->pos = vadd(vadd(k->pos, vmul(f, 800.0f)), vmul(up, 50.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            placed = 1;
        } else if (k->nrv == PT_WAIT && !k->phase && k->step > 70 && !mario->spin_timer) { /* beside it, a spin */
            mario->up = up; mario->pos = vadd(k->pos, vmul(vnorm(vtangent(vsub(mario->pos, k->pos), up)), 80.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            mario->spin_timer = SPIN_TIME;
        } else if (k->nrv == PT_SWOON && k->step == 40) { /* onto it */
            mario->up = up; mario->pos = vadd(k->pos, vmul(up, 200.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = -5.0f; mario->grounded = 0; mario->hip = 0;
        }
        return k->nrv * 1000 + (k->step > 999 ? 999 : k->step) + (k->phase ? 100000 : 0);
    }
    return -1;
}

int game_test_seesaw(int phase) /* the first PhantomShipHandle: Mario dropped 150 along its X (phase 0) or onto it in a
                                 * hip drop (phase 1); its zone first */
{
    test_nolost = 1;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->seesaw) continue;
        int k = seesaw_of(in->row);
        if (k < 0) return -1;
        V3 pos = v3(in->m[12], in->m[13], in->m[14]), up = up_at(pos, mario->up);
        V3 x = vnorm(v3(in->m[0], in->m[1], in->m[2]));
        static int last = -1;
        if (phase != last) {
            last = phase;
            mario->up = up; mario->pos = vadd(vadd(pos, vmul(x, phase ? -150.0f : 150.0f)), vmul(up, 400.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
            if (phase) { mario->hip = HIP_START; mario->hip_t = 0; }
        }
        if (game_frame % 20 == 0) printf("seesaw: nrv %d angle %d vel x100 %d on %d hip %d\n", seesaws[k].nrv, (int)seesaws[k].angle, (int)(seesaws[k].vel * 100), mario_ground == i, mario->hip);
        return seesaws[k].nrv;
    }
    int ty = scene_type(sc, "PhantomShipHandle");
    for (u32 i = 0; i < sc->nobjs; i++)
        if (sc->objs[i].type == ty) {
            V3 p = row_pos(&sc->objs[i]);
            mario->up = up_at(p, mario->up); mario->pos = vadd(p, vmul(mario->up, 400.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
            return -2;
        }
    return -1;
}

int game_test_follow(int phase) /* the first StarPieceFollowGroup: its SW_A on (phase 0), Mario still; phase 1: Mario put
                                 * on its first piece left; returns pieces out << 8 | taken */
{
    test_nolost = 1;
    int ty = scene_type(sc, "StarPieceFollowGroup");
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != ty || r->sw[SW_A] < 0) continue;
        if (phase == 0 && !sw_on(r, SW_A)) sw_set(r, SW_A, 1);
        int out = 0;
        for (int j = 0; j < nfollows; j++)
            if (follows[j].row == (int)i)
                for (int q = 0; q < follows[j].n; q++)
                    if (pieces[follows[j].slot[q]].alive != PICK_GONE) {
                        out++;
                        if (phase == 1 && out == 1) { mario->pos = vsub(pieces[follows[j].slot[q]].pos, vmul(mario->up, 50.0f)); }
                    }
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return out << 8 | got_pieces;
    }
    return -1;
}

int game_test_kgun_state(void)
{
    for (int i = 0; i < ninsts; i++) if (insts[i].kgun) return row_turn[insts[i].row] * 1000 + row_step[insts[i].row];
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
            mario->face = vnorm(vtangent(v3(1, 0, 0), m->up));
            mario->walk = 0.2f; mario->vup = 0; /* walking through it (a move of 2.6 a frame) */
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
    int back = 0;
    for (int b = 0; b < OTA_BALLS; b++) back |= ota.ball[b].state == 3;
    if (!back && ota.nrv != OTA_WAITONSWITCH && (vlen(vsub(mario->pos, at)) > 900.0f || (mario->jumping && mario->air_timer > 300))) { /* a rally lost: back to the rim */
        mario->pos = vadd(at, vmul(ota.up, 300.0f)); mario->hvel = v3(0, 0, 0); mario->vup = 0;
    }
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
    mario->hip = HIP_START; mario->hip_t = 0; mario->jumping = 1; mario->zero_gr = MC.mHipDropZeroGrTime; /* as if R1 was pressed up there */
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
    if (!nrockcs) return -1;
    mario->pos = rail_at(rockcs[0].pts, rockcs[0].np, 0); /* where its rocks start: inside the area that wakes it */
    mario->hvel = v3(0, 0, 0); mario->vup = 0;
    return rockcs[0].row;
}

int game_test_rock(void)
{
    for (int i = 0; i < nrocks; i++) {
        Rock *o = &rocks[i];
        if (!o->alive) continue;
        const RockC *c = &rockcs[o->c];
        mario->pos = rail_at(c->pts, c->np, o->coord + 300.0f); /* on its way, 300 ahead */
        mario->hvel = v3(0, 0, 0); mario->vup = 0;
        return c->row;
    }
    return -1;
}

int game_rocks_alive(void)
{
    int n = 0;
    for (int i = 0; i < nrocks; i++) n += rocks[i].alive && rocks[i].nrv != 4;
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
 * (Fall) (calcShootMotionTime). fly_on: the draw takes this basis (the ring's -75 x radius rate is 0: never set) */
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

static void m_power_jump(Player *m, V3 v);

static int update_launch(Player *m)
{
    Launch *L = &launches[m->launch];
    int t = ++m->launch_t;
    if (t <= L->start) {
        if (L->start < 45) { /* SpinDriver::exeShootStart: from where he was to 200 under the star along its up and back
                              * ((2 (p - 0.5))^4 over 20 frames), the blend complete at half way */
            float p = fminf((t - 1) / 20.0f, 1.0f), h = 2.0f * (p - 0.5f), v = h * h * h * h, v2 = fminf(2.0f * p, 1.0f);
            V3 dip = vsub(L->pos, vmul(L->up, 200.0f * (1.0f - v)));
            m->pos = vadd(vmul(m->launch_from, 1.0f - v2), vmul(dip, v2));
        } else { /* SuperSpinDriver::exeShootStart: to the centre over 15 frames */
            float k = fminf((t - 1) / 15.0f, 1.0f);
            m->pos = vadd(m->launch_from, vmul(vsub(L->pos, m->launch_from), k));
        }
        return 1;
    }
    int small = !L->starter && L->start < 45, s = t - L->start;
    if (!L->pts) { /* SpinDriver without a rail: endBindAndSpinDriverJump with its kick */
        L->cool = 60;
        m->launch = -1;
        m_power_jump(m, L->kick);
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
    m_power_jump(m, step);
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
        m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_lock = 0;
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
    m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_lock = 0;
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
 * stops its chase 15 frames (isHitReaction); 8 star pieces from its Head as each battle starts. ponytail: no damage
 * camera (startDamageCamera), no footprints, no Mario puppet clips in the demos, the head sensor at the Head joint only once the
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

/* DinoPackunTail (DinoPackunTail / TailNode / TailPart / TailRoot / Ball.cpp): TailRoot follows its clip, Tail1..7
 * are bodies pulled to their links (90; the ball 120), kept bent (sKeepBendPower) and, in Find, to the clip (_18);
 * the ball takes the chain only in Wait / Rebound. Off (pure clip, the ball on its joint) before the fight and in the
 * demos (mTail->deactivate / startDemoAndReset). */
static void dino_pose(void)
{
    if (dino.brk && m_deggbrk && m_deggbrk->skin && dino.nrv == DN_DAMAGE && dino.dnrv == DD_EGGBROKEN && dino.dt >= 30) {
        if (dino.brk->skin != m_deggbrk->skin) anim_bind(dino.brk, m_deggbrk->skin);
        anim_play(dino.brk, "dinopackuneggshellbreak/break", 1.0f, 0, 0); /* exeEggBroken step 30: startBck Break */
        anim_update(dino.brk);
    }
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
    /* DinoPackunTail::updateJoint (active): _9C (preCalcJoint) the clip's joint places, then addAccelKeepDistance,
     * addAccelKeepBend, addAccelToBck; the parts (DinoPackunTailPart::exeWait) fall 0.7, x 0.88, bound by spheres of
     * sJointRadius; the joints (turnJointLocalXDir, calcJointScale) turned along the chain at the nodes */
    static const float bend[9] = {30, 30, 25, 20, 15, 10, 10, 10, 10}; /* root: sKeepBendPower[0]; the ball: 10 */
    static const float rad[9] = {0, 80, 70, 60, 55, 50, 45, 50, 135};
    V3 org = v3(m[12], m[13], m[14]), ax = v3(m[0], m[1], m[2]), ay = v3(m[4], m[5], m[6]), az = v3(m[8], m[9], m[10]);
#define TOW(l) vadd(org, vadd(vadd(vmul(ax, (l).x), vmul(ay, (l).y)), vmul(az, (l).z)))
#define TOL(w) v3(vdot(vsub(w, org), ax), vdot(vsub(w, org), ay), vdot(vsub(w, org), az))
    V3 bck[9], gup = dino.up, g = vmul(gup, -1.0f);
    for (int i = 0; i < 9; i++) bck[i] = TOW(v3(a->world[12 + i][12], a->world[12 + i][13], a->world[12 + i][14]));
    if (!dino.chain_ok) { /* activate: resetJoint */
        for (int i = 0; i < 9; i++) {
            dino.tp[i] = bck[i]; dino.tv[i] = v3(0, 0, 0);
            const float *w = a->world[12 + i];
            dino.tdir[i] = vnorm(TOW(v3(w[0], w[1], w[2])));
            dino.tdir[i] = vnorm(vsub(dino.tdir[i], org));
        }
        dino.bpos = bck[8]; dino.bvel = v3(0, 0, 0);
        dino.chain_ok = 1;
    }
    dino.tp[0] = bck[0];                                   /* DinoPackunTailRoot::calcJointDirection: mPosition */
    V3 *P = dino.tp, *Vv = dino.tv;
    P[8] = dino.bpos;
    V3 add[9];
    for (int i = 0; i < 9; i++) add[i] = v3(0, 0, 0);
    for (int i = 1; i < 9; i++) { /* addAccelKeepDistance (_10 1) */
        V3 dv = vsub(P[i], P[i - 1]);
        if (vlen(dv) < 1e-6f) dv = v3(0.1f, 0, 0);
        float l = vlen(dv), link = i == 8 ? 120.0f : 90.0f, k = 0.9f * (link - l) / 2.0f;
        dv = vmul(dv, 1.0f / l);
        add[i] = vadd(add[i], vmul(dv, k));
        add[i - 1] = vsub(add[i - 1], vmul(dv, k));
    }
    { /* addAccelKeepBend (_C 1) */
        V3 d0 = vnorm(dino.tdir[0]);
        for (int i = 1; i < 9; i++) {
            V3 d1 = dino.tdir[i];
            if (vlen(d1) < 1e-6f) continue;
            d1 = vnorm(d1);
            V3 axis = vcross(d0, d1);
            float cs = vdot(d0, d1);
            if (vlen(axis) > 1e-6f && cs < 1.1f) { /* makeAxisAndCosignVecToVec */
                axis = vnorm(axis);
                float rate = 1.0f - (cs + 1.0f) / 2.1f, k = rate * bend[i];
                add[i] = vadd(add[i], vmul(vnorm(vcross(d1, axis)), k));
                if (i >= 2) add[i - 2] = vadd(add[i - 2], vmul(vnorm(vcross(d0, axis)), k));
            }
            d0 = d1;
        }
    }
    if (dino.t18 > 0.0f) for (int i = 1; i < 9; i++) add[i] = vadd(add[i], vmul(vsub(bck[i], P[i]), dino.t18)); /* addAccelToBck */
    for (int i = 1; i < 8; i++) { /* addNodeVelocityHost (not locked), then the part's own movement */
        if (i == 1 && dino.tlock1) { Vv[i] = v3(0, 0, 0); continue; } /* LockPosition */
        Vv[i] = vadd(Vv[i], add[i]);
        Vv[i] = vmul(vadd(Vv[i], vmul(g, 0.7f)), 0.88f);
        V3 c = vadd(P[i], Vv[i]), gn = gup, wn = gup;
        int wall = 0;
        if (collide_n(&c, gup, rad[i], &gn, &wn, &wall)) { float into = vdot(Vv[i], gn); if (into < 0) Vv[i] = vsub(Vv[i], vmul(gn, into)); }
        if (wall) { float into = vdot(Vv[i], wn); if (into < 0) Vv[i] = vsub(Vv[i], vmul(wn, into)); } /* reboundVelocityFromCollision 0 */
        P[i] = c;
    }
    /* DinoPackunBall::addNodeVelocity: Wait 1, Rebound the rate over 180, else none */
    float bk = dino.bstate == 0 ? 1.0f : dino.bstate == 4 ? fminf(dino.bt / 180.0f, 1.0f) : 0.0f;
    dino.bvel = vadd(dino.bvel, vmul(add[8], bk));
    /* the joints: input = the parent's final x the clip's local; X turned to the next node (the ball: from the last) */
    float fin[9][16], inv[16];
    for (int i = 0; i < 9; i++) {
        float *w = a->world[12 + i], in_[16];
        if (i == 0) memcpy(in_, w, 64);
        else { /* parent's final x inverse(parent clip) x clip */
            const float *pw = a->world[12 + i - 1];
            float tmp[16];
            mat_identity(inv); /* rigid inverse of the parent's clip matrix (orthonormal part; scale undone) */
            float sx = vlen(v3(pw[0], pw[1], pw[2])), sy = vlen(v3(pw[4], pw[5], pw[6])), sz = vlen(v3(pw[8], pw[9], pw[10]));
            float r[9] = {pw[0] / sx, pw[1] / sx, pw[2] / sx, pw[4] / sy, pw[5] / sy, pw[6] / sy, pw[8] / sz, pw[9] / sz, pw[10] / sz};
            inv[0] = r[0]; inv[4] = r[1]; inv[8] = r[2];
            inv[1] = r[3]; inv[5] = r[4]; inv[9] = r[5];
            inv[2] = r[6]; inv[6] = r[7]; inv[10] = r[8];
            inv[12] = -(r[0] * pw[12] + r[1] * pw[13] + r[2] * pw[14]);
            inv[13] = -(r[3] * pw[12] + r[4] * pw[13] + r[5] * pw[14]);
            inv[14] = -(r[6] * pw[12] + r[7] * pw[13] + r[8] * pw[14]);
            mat_mul(tmp, inv, w);
            mat_mul(in_, fin[i - 1], tmp);
        }
        V3 x = vnorm(v3(in_[0], in_[1], in_[2])), y = vnorm(v3(in_[4], in_[5], in_[6]));
        dino.tdir[i] = vnorm(vsub(TOW(x), org));             /* mNodeDirection (world) */
        V3 pl = TOL(i == 8 ? dino.bpos : P[i]);
        V3 dirl = i < 8 ? vsub(TOL(i + 1 == 8 ? dino.bpos : P[i + 1]), pl) : vsub(pl, TOL(P[7]));
        V3 nx = vlen(dirl) > 1e-6f ? vnorm(dirl) : x;
        V3 axis = vcross(x, nx);                              /* turnQuatXDirRad: the shortest turn of X onto it */
        float sn = vlen(axis), cs = vdot(x, nx);
        V3 ny = y;
        if (sn > 1e-6f) ny = rot_axis(y, vmul(axis, 1.0f / sn), atan2f(sn, cs));
        ny = vnorm(vtangent(ny, nx));
        V3 nz = vcross(nx, ny);
        float *f = fin[i];
        f[0] = nx.x; f[1] = nx.y; f[2] = nx.z; f[3] = 0;
        f[4] = ny.x; f[5] = ny.y; f[6] = ny.z; f[7] = 0;
        f[8] = nz.x; f[9] = nz.y; f[10] = nz.z; f[11] = 0;
        f[12] = pl.x; f[13] = pl.y; f[14] = pl.z; f[15] = 1;
        memcpy(w, f, 64);
        if (i >= 1 && i < 8) { /* calcJointScale: X stretched to the next node over the link, Y / Z by sqrt(1 / it) */
            V3 nxt = i + 1 == 8 ? dino.bpos : P[i + 1];
            float st = vlen(vsub(nxt, P[i])) / 90.0f;
            st = st < 0.2f ? 0.2f : st > 5.0f ? 5.0f : st;
            float sh = sqrtf(1.0f / st);
            for (int j = 0; j < 3; j++) { w[j] *= st; w[4 + j] *= sh; w[8 + j] *= sh; }
        }
    }
#undef TOW
#undef TOL
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
    if (dino.seq == DSEQ_LV1 || dino.seq == DSEQ_LV2 || dino.seq == DSEQ_LV3) { /* exeBattleLv*: appearStarPiece(8) from Head */
        V3 hp = vadd(dino.pos, vmul(dino.up, 350.0f));
        if (dino.anim && m_dino && dino.anim->skin == m_dino->skin && dino.anim->skin->njoints > 27) {
            float m[16], jm[16];
            basis_matrix(m, dino.pos, dino.up, dino.f, 0);
            mat_mul(jm, m, dino.anim->world[27]);
            hp = v3(jm[12], jm[13], jm[14]);
        }
        launch_pieces(hp, dino.up, 8, 50.0f, 60.0f);
    }
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
    V3 bup = up_at(dino.bpos, up);
    dino.bt++;
    switch (dino.bstate) {
    case 5: dino.bvel = v3(0, 0, 0); break; /* exeLock */
    case 0: case 4: /* exeWait / exeRebound: gravity 1, x 0.9 */
        dino.bvel = vmul(vadd(dino.bvel, vmul(bup, -1.0f)), 0.9f);
        if (dino.bstate == 4 && dino.bt >= 180) { dino.bstate = 0; dino.bt = 0; }
        if (dino.bstate == 0 && battle && dino.nrv != DN_DAMAGE && mario->spin_timer > 0 &&
            vlen(vsub(c, dino.bpos)) < 135.0f + HIT_BODY_R + 60.0f) { /* requestPunch: PUNCHED_BALL to the head */
            V3 away = vtangent(vsub(dino.bpos, mario->pos), bup);
            away = vlen(away) > 1e-3f ? vnorm(away) : vmul(dino.f, -1.0f);
            dino.bvel = vadd(vmul(away, 120.0f), vmul(bup, 40.0f)); /* addVelocitySeparateHV 120, 40 */
            dino.bstate = 1; dino.bt = 0; scene_stop = 3; /* exeShoot: stopScene(3) */
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
                scene_stop = egg ? 5 : 15; /* exeEggBroken stopScene(5) / updateDamage stopScene(15) */
            }
            printf("dino packun: hit %d" "\n", dino.hits);
        }
        break;
    }
    V3 bc = vadd(dino.bpos, dino.bvel);
    if (dino.bstate != 5 && collide_n(&bc, bup, 135.0f, &gn, &wn, &wall)) { /* reboundVelocityFromCollision 0, 0, 1 */
        float into = vdot(dino.bvel, gn);
        if (into < 0) dino.bvel = vsub(dino.bvel, vmul(gn, into));
    }
    dino.bpos = bc;
    /* the tail's locks (DinoPackunBattleEgg::appear, StateDamage exePunched: node 1; exeEggBroken: the end until 30;
     * updateDamage unlocks node 1) and _18 (DinoPackunStateAwake: 0.015 x the ease-out over 30 during Find) */
    dino.tlock1 = (egg && !(dino.nrv == DN_DAMAGE && dino.dnrv == DD_EGGBROKEN && dino.dt >= 30)) ||
                  (dino.nrv == DN_DAMAGE && dino.dnrv == DD_PUNCHED);
    if (dino.nrv == DN_DAMAGE && dino.dnrv == DD_EGGBROKEN) {
        if (dino.dt < 30) { if (dino.bstate != 5) { dino.bstate = 5; dino.bt = 0; } }
        else if (dino.bstate == 5) { dino.bstate = 4; dino.bt = 0; } /* requestUnLockPosition: Rebound */
    }
    { float u = fminf(dino.nt / 30.0f, 1.0f); dino.t18 = dino.nrv == DN_FIND ? 0.015f * (1.0f - (1.0f - u) * (1.0f - u)) : 0.0f; }
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
 * OtaKingMagma (its clips at the ripples) and the four OtaKingLongFoot (burning Mario) with it; hits x 8 star pieces
 * at each Damage / PowerUp. ponytail: no star pointer (coconut freeze, fire reflect, bubble reflect), no crown material
 * hiding (PowerUp step 83), the magma's BTK flow / bloom / collision, no effects */
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

/* CocoNutBall::calcHitBackDstPos / calcHitBackVelocitAndGravity: to its host's body sensor 100 up (150 aside unless
 * Mario stands right in front of the host), 42 across, gravity 2.2 (1.2 aside) */
static void coco_hitback(V3 pos, V3 up, V3 host, V3 body, V3 *vel, V3 *grav)
{
    V3 hb = vnorm(vtangent(vsub(host, mario->pos), up)), kb = vnorm(vtangent(vsub(pos, mario->pos), up));
    int right = vdot(up, vcross(hb, kb)) > 0.0f, front = vdot(vmul(hb, -1.0f), kb) < -0.95f;
    V3 dst = vadd(body, vmul(up, 100.0f));
    if (!front) dst = vadd(dst, vmul(vnorm(vcross(up, hb)), right ? 150.0f : -150.0f));
    V3 dir = vsub(dst, pos), h = vtangent(dir, up);
    float dv = vdot(dir, up), f1 = vlen(h) / 42.0f;
    if (f1 < 1.0f) f1 = 1.0f;
    V3 cr = vnorm(vcross(up, vnorm(h)));
    *grav = vmul(up, -2.2f);
    *vel = vadd(vmul(vnorm(h), 42.0f), vmul(up, dv / f1 + 1.1f * f1));
    if (!front) {
        float sf = right ? 1.2f : -1.2f;
        *vel = vadd(*vel, vmul(cr, sf * f1 / 2.0f));
        *grav = vsub(*grav, vmul(cr, sf));
    }
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
            coco_hitback(k->pos, k->up, ota.pos, body, &k->vel, &k->grav);
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

/* OtaRock.cpp: faces Mario; Wait 30-90 frames (120-180 with arg0: no coconuts), then two coconuts (CocoNutBall, 4 of
 * them: Attack, thrown at step 40 from its Mouth at 15, 120 over Mario turned 5-10 degrees either way) and a fire ball
 * (FireBall at 15, 120 over Mario), again; all four coconuts gone and Mario 4000 off (or its SW_A off) it Hides, and in
 * HideWait shows again at 3800 (SW_A on). Its body sensor (300, 180 up) hurts Mario (sendMsgEnemyAttackStrong); a
 * coconut spun back (HitBackToHost) into it, or any enemy attack, puts it Down: Death, stopScene(16) at step 2, then
 * kill (its dead switch: Good Egg's TakoBarrelA break on it). ponytail: no OtaRockDeath / coconut effects (not in
 * the FX table), no anim scale (hit reaction), no 2P freeze / reflect */

static int orock_of(int row)
{
    for (int i = 0; i < norocks; i++) if (orocks[i].row == row) return i;
    if (norocks == MAX_OROCKS) return -1;
    typeof(&orocks[0]) o = &orocks[norocks];
    memset(o, 0, sizeof *o);
    const Row *r = &sc->objs[row];
    V3 x, z;
    row_axes(r, &x, &z);
    o->row = row; o->pos = row_pos(r); o->up = up_at(o->pos, v3(0, 1, 0)); o->f = vnorm(vtangent(z, o->up));
    o->nrv = r->arg[0] != -1 ? OR_WAIT : OR_HIDEWAIT; /* appear: mNoThrowCocoNut */
    o->t = 0;
    return norocks++;
}

static void orock_set(int k, int nrv) { orocks[k].nrv = nrv; orocks[k].t = 0; }

static V3 orock_mouth(const Inst *in, int k) /* FixedPosition "Mouth" (joint 10), (0, 0, 50) */
{
    if (in->anim && in->anim->skin && 10 < in->anim->skin->njoints) {
        float jm[16];
        mat_mul(jm, in->m, in->anim->world[10]);
        return v3(jm[12] + jm[8] * 50.0f, jm[13] + jm[9] * 50.0f, jm[14] + jm[10] * 50.0f);
    }
    return vadd(orocks[k].pos, vmul(orocks[k].up, 180.0f));
}

static void update_orocks(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->orock || row_dead[in->row]) continue;
        int k = orock_of(in->row);
        if (k < 0) continue;
        typeof(&orocks[0]) o = &orocks[k];
        if (in->md && in->md->skin) in->anim = obj_anim(in->row, in->md, "OtaRock"); /* its slot kept (the pool may be full at rebind) */
        const Row *r = &sc->objs[in->row];
        int nothrow = r->arg[0] != -1, st = o->t++, dead = 0, swa = r->sw[SW_A] >= 0;
        float dm = vlen(vsub(mario->pos, o->pos));
        for (int b = 0; b < 4; b++) dead += !o->ball[b].state;
        V3 body = vadd(o->pos, vmul(o->up, 180.0f));
        if (o->nrv != OR_DOWN && o->nrv != OR_DEAD) { /* updateBaseMtx: up against the gravity, front to Mario */
            V3 fr = vnorm(vtangent(vsub(mario->pos, o->pos), o->up));
            if (vlen(fr) > 0.5f) o->f = fr;
            basis_matrix(in->m, o->pos, o->up, o->f, 0);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) in->m[j] *= r->scale[0];
            if (vlen(vsub(c, body)) < 300.0f + 100.0f) hurt_mario(body, 15.0f); /* attackSensor */
        }
        switch (o->nrv) {
        case OR_WAIT:
            if (!st) { obj_play(in, "wait", 1, 0); o->wait = nothrow ? 120 + rand() % 60 : 30 + rand() % 60; }
            if (!nothrow && dead == 4 && (swa ? !sw_on(r, SW_A) : dm >= 4000.0f)) { orock_set(k, OR_HIDE); break; } /* tryToHide */
            if (st > o->wait) {
                if (!nothrow && o->cnt < 2) { if (dead > 0) orock_set(k, OR_THROWCOCO); }
                else { o->cnt = 0; orock_set(k, OR_THROWFIRE); }
            }
            break;
        case OR_THROWCOCO: case OR_THROWFIRE:
            if (!st) obj_play(in, "attack", 0, 0);
            if (st == 40) {
                V3 m = orock_mouth(in, k);
                if (o->nrv == OR_THROWCOCO) { /* throwCocoNut: appearAndThrow(mouth, +-5..10) */
                    for (int b = 0; b < 4; b++) if (!o->ball[b].state) {
                        typeof(&o->ball[0]) q = &o->ball[b];
                        float a = (5.0f + (rand() % 1000) * 0.005f) * (rand() & 1 ? 1.0f : -1.0f) * 0.0174533f;
                        q->pos = m; q->up = up_at(m, o->up); q->state = 1; q->t = 0; q->rally = 0;
                        V3 tg = vadd(mario->pos, vmul(q->up, 120.0f)), d = rot_axis(vsub(tg, m), q->up, a);
                        q->vel = vmul(vnorm(d), 15.0f); /* setVelocityToPlayer(15, angle) */
                        break;
                    }
                    o->cnt++;
                } else { /* throwFireBall: 15 at 120 over Mario */
                    for (int f = 0; f < 3; f++) if (!o->fire[f].alive) {
                        o->fire[f].pos = m; o->fire[f].up = up_at(m, o->up); o->fire[f].alive = 1;
                        o->fire[f].vel = vmul(vnorm(vsub(vadd(mario->pos, vmul(o->fire[f].up, 120.0f)), m)), 15.0f);
                        break;
                    }
                }
            }
            if (st >= 89) orock_set(k, OR_WAIT); /* isBckStopped (Attack 89) */
            break;
        case OR_DOWN:
            if (!st) {
                obj_play(in, "death", 0, 0);
                for (int b = 0; b < 4; b++) o->ball[b].state = 0;
                for (int f = 0; f < 3; f++) o->fire[f].alive = 0;
                printf("ota rock %d down\n", in->row);
            }
            if (st == 2) scene_stop = 16;
            if (st >= 34) { orock_set(k, OR_DEAD); row_kill(in->row); game_rebind(); return; } /* Death 34: kill */
            break;
        case OR_HIDE:
            if (!st) obj_play(in, "hide", 0, 0);
            if (st >= 29) orock_set(k, OR_HIDEWAIT);
            break;
        case OR_HIDEWAIT:
            if (!st) obj_play(in, "hidewait", 0, 0);
            if (swa ? sw_on(r, SW_A) : dm < 3800.0f) orock_set(k, OR_SHOW);
            break;
        case OR_SHOW:
            if (!st) obj_play(in, "show", 0, 0);
            if (st >= 59) orock_set(k, OR_WAIT);
            break;
        }
        for (int b = 0; b < 4; b++) { /* its CocoNutBalls */
            typeof(&o->ball[0]) q = &o->ball[b];
            if (!q->state) continue;
            q->t++;
            if (q->state == 2 && q->t >= 1) q->vel = vadd(q->vel, q->grav);
            if (q->state == 4) q->vel = vadd(q->vel, vmul(q->up, -2.0f));
            q->pos = vadd(q->pos, q->vel);
            Hit h[1];
            int bound = q->state != 2 && kcl_query(q->pos, 60.0f, h, 1) > 0;
            float far = q->state == 1 ? vlen(vsub(q->pos, o->pos)) - 10000.0f : vlen(vsub(q->pos, mario->pos)) - 5000.0f;
            if (bound || far > 0) { q->state = 0; continue; }
            float dc = vlen(vsub(c, q->pos));
            if ((q->state == 1 || q->state == 4) && mario->spin_timer > 0 && dc < 400.0f && q->t > 1) { /* HitBackToHost */
                q->rally++;
                coco_hitback(q->pos, q->up, o->pos, body, &q->vel, &q->grav);
                q->state = 2; q->t = 0;
                printf("ota rock %d: coconut hit back\n", in->row);
                continue;
            }
            if (q->state == 1 && dc < 40.0f + HIT_BODY_R) { /* FLIP_VERYWEAK, Rebound */
                V3 away = vnorm(vtangent(vsub(q->pos, mario->pos), q->up));
                q->vel = vadd(vmul(q->up, 15.0f), vmul(away, 5.0f));
                q->state = 4; q->t = 0;
                continue;
            }
            if (q->state == 2 && vlen(vsub(q->pos, body)) < 300.0f + 40.0f && o->nrv != OR_DOWN) { /* sendMsgEnemyAttack */
                q->state = 0;
                orock_set(k, OR_DOWN);
            }
        }
        for (int f = 0; f < 3; f++) { /* its FireBalls */
            if (!o->fire[f].alive) continue;
            o->fire[f].pos = vadd(o->fire[f].pos, o->fire[f].vel);
            Hit h[1];
            if (kcl_query(o->fire[f].pos, 60.0f, h, 1) > 0 || vlen(vsub(o->fire[f].pos, mario->pos)) > 5000.0f) { o->fire[f].alive = 0; continue; }
            if (vlen(vsub(c, o->fire[f].pos)) < 40.0f + HIT_BODY_R) { hurt_mario(o->fire[f].pos, 20.0f); o->fire[f].alive = 0; }
        }
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
        if (ota.t == 1) for (int k = 0; k < 4; k++) ota_lfoot(k, 3, "appear"); /* mLongFeet startAppearDemo */
        if (ota.t == 41) ota_magma("attack");                                   /* cAppearDemoRippleStepS */
        if (ota.t == 191) ota_magma("down");                                    /* appearDemo (Down), then Wait */
        if (ota.t >= 424) { ota.yaw = ota_player_angle(); for (int k = 0; k < 4; k++) ota_lfoot(k, 1, "wait"); ota_set(OTA_THROWFIREWAIT); }
        break;
    case OTA_WAIT: case OTA_THROWCOCOWAIT: { /* tryThrowCocoNutOrFireBallIfWait */
        int w = ota.nrv == OTA_WAIT ? 75 : 45;
        if (ota.t > w && ota_valid_coco()) { if (ota.nrv == OTA_WAIT) ota.ec = 0; ota_set(OTA_THROWCOCO); }
        else if (ota.t > 2 * w && ota_valid_fire()) ota_set(OTA_THROWFIRE);
        break;
    }
    case OTA_THROWCOCO:
        if (ota.t == 31) ota_magma("attack"); /* cCocoNutRippleStep */
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
        if (ota.t == 31) ota_magma("fireattack"); /* cFireBallRippleStep */
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
        if (ota.t == 1 || (ota.nrv == OTA_POWERUP && (ota.t == 74 || ota.t == 176))) ota_magma("damage"); /* cDamageCrownFallFrame, cBlurStartStep */
        if (ota.t == 1) launch_pieces(vadd(ota.pos, v3(0, 300.0f, 0)), ota.up, ota.hits * 8, 60.0f, 50.0f); /* appearStarPiece */
        if (ota.t == 3) scene_stop = 16; /* stopScene (Lv1) */
        if (ota.t >= (ota.nrv == OTA_POWERUP ? 256 : 179)) {
            if (ota.nrv == OTA_POWERUP && r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
            ota_set(ota_valid_bubble() ? OTA_BUBBLEATTACK : ota_valid_fire() ? OTA_THROWFIRE : OTA_THROWFIREWAIT);
        }
        break;
    case OTA_DOWN:
        if (ota.t == 1) {
            for (int k = 0; k < 4; k++) ota_lfoot(k, 4, NULL); /* mLongFeet startDownDemo */
            for (int b = 0; b < OTA_BALLS; b++) ota.ball[b].state = 0;
            for (int i = 0; i < OTA_FIRES; i++) ota.fire[i].alive = 0;
            for (int i = 0; i < OTA_BUBBLES; i++) ota.bub[i].state = 0;
        }
        if (ota.t >= 3) { ota_set(OTA_DOWNDEMO); ota.yaw = 43.0f; row_kill(ota.row); /* requestAppearPowerStar */ }
        break;
    case OTA_DOWNDEMO:
        ota.yaw = 43.0f; /* cDownDemoRotate */
        if (ota.t == 1) ota_magma("damage");
        if (ota.t == 91) ota_magma("down"); /* cDownRippleStep: Down, then EndWait */
        if (ota.t >= 300) { ota_set(OTA_APPEARSTAR); printf("ota king: down, its star" "\n"); }
        break;
    case OTA_APPEARSTAR:
        if (ota.t >= 60) ota_set(OTA_DEAD);
        break;
    case OTA_HITBACK: case OTA_NEARATTACK:
        if (ota.t == 1) ota_magma("hitback");
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
    /* OtaKingLongFoot (mLongFeet, not Lv2): hidden until the appear demo (Appear held at 0 until its step 3 / 8 / 14 / 18,
     * then played), Wait (from a random frame), Damage when its foot sensors (Foot14 / 12 / 10 / 08 / 06 / 05, 50 to 150)
     * burn Mario (sendMsgEnemyAttackFire), Down at its step in the down demo, gone with the star */
    static const int lstep[4] = {3, 8, 14, 18}, sj[6] = {12, 10, 8, 6, 4, 3};
    static const float sr[6] = {50, 90, 110, 150, 150, 150}, so[6][3] = {{50, 0, 20}, {75, 0, 20}, {125, 0, 0}, {125, 0, -40}, {100, 0, -30}, {80, 0, -40}};
    if (ota.nrv == OTA_WAITONSWITCH) for (int k = 0; k < 4; k++) if (ota.lnrv[k] != 0) ota_lfoot(k, 0, "appear");
    for (int k = 0; k < 4; k++) {
        ota.lt[k]++;
        if (ota.lnrv[k] == 2 && ota.lt[k] > 1 && ota.lfoot[k]->done) ota_lfoot(k, 1, "wait"); /* setNerveAtBckStopped */
        if (ota.lnrv[k] == 4 && ota.lt[k] == lstep[k] + 1) ota_lfoot(k, 4, "down");
        if (ota.lnrv[k] == 1 && m_otalfoot && m_otalfoot->skin && ota.lfoot[k]->skin == m_otalfoot->skin && !mario->hurt_timer) {
            float fm[16];
            ota_lfoot_mtx(k, fm);
            for (int q = 0; q < 6; q++) {
                float jm[16];
                mat_mul(jm, fm, ota.lfoot[k]->world[sj[q]]);
                V3 sp = v3(jm[12] + jm[0] * so[q][0] + jm[4] * so[q][1] + jm[8] * so[q][2],
                           jm[13] + jm[1] * so[q][0] + jm[5] * so[q][1] + jm[9] * so[q][2],
                           jm[14] + jm[2] * so[q][0] + jm[6] * so[q][1] + jm[10] * so[q][2]);
                if (vlen(vsub(c, sp)) < sr[q] + HIT_BODY_R) { hurt_mario(sp, 20.0f); ota_lfoot(k, 2, "damage"); break; }
            }
        }
    }
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
    if (ota.magma && m_otamagma && m_otamagma->skin) { /* OtaKingMagma */
        Anim *a = ota.magma;
        if (a->skin != m_otamagma->skin) { anim_bind(a, m_otamagma->skin); if (!ota.mact[0]) ota_magma("wait"); ota.mdrawn = -1; }
        char n[48];
        snprintf(n, sizeof n, "otakingmagma/%s", ota.mact);
        if (ota.mdrawn != ota.mseq) { anim_start(a, n, 0); ota.mdrawn = ota.mseq; }
        else if (a->done && !a->loop) { ota_magma(strcmp(ota.mact, "down") || ota.nrv != OTA_DOWNDEMO ? "wait" : "endwait"); }
        anim_update(a);
    }
    for (int k = 0; k < 4 && m_otalfoot && m_otalfoot->skin && ota.lfoot[k]; k++) { /* OtaKingLongFoot */
        Anim *a = ota.lfoot[k];
        if (a->skin != m_otalfoot->skin) { anim_bind(a, m_otalfoot->skin); if (!ota.lact[k][0]) ota_lfoot(k, ota.nrv == OTA_WAITONSWITCH ? 0 : 1, ota.nrv == OTA_WAITONSWITCH ? "appear" : "wait"); ota.ldrawn[k] = -1; }
        char n[48];
        snprintf(n, sizeof n, "otakinglongfoot/%s", ota.lact[k]);
        if (ota.ldrawn[k] != ota.lseq[k]) {
            anim_start(a, n, 0); ota.ldrawn[k] = ota.lseq[k];
            if (ota.lnrv[k] == 1 && anim_length(a)) a->frame = (float)(rand() % anim_length(a)); /* setBckFrameAtRandom */
        }
        a->speed = (ota.lnrv[k] == 0 || (ota.lnrv[k] == 3 && ota.lt[k] <= lstep_of(k))) ? 0.0f : 1.0f; /* held at 0 until its step */
        anim_update(a);
    }
}

/* mLongFeet's FixedPosition: a translation to OtaKing's place x their (pos, rot degrees), scale cLongFootScale */
void ota_lfoot_mtx(int k, float out[16])
{
    static const float fp[4][6] = {{735, 80, -55, -9, 266, 0}, {-959, 130, 0, 0, 107, 14}, {0, 43, 884, -8, 159, 0}, {55, 55, -896, -9, -14, 0}};
    float t[16], l[16], sc3[3] = {1.0f, 0.9f, 0.8f};
    mat_identity(t);
    t[12] = ota.pos.x; t[13] = ota.pos.y; t[14] = ota.pos.z;
    mat_translate_rot_scale(l, fp[k], fp[k] + 3, sc3);
    mat_mul(out, t, l);
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
    if (hhp_blow(fireballs[b].pos, 80.0f)) { fireballs[b].t = 0; return; } /* HammerHeadPackun: isMsgFireBallAttack */
    for (int k = 0; k < nkuribo; k++) {
        Kuribo *e = &kuribo[k];
        if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY ||
            vlen(vsub(fireballs[b].pos, vadd(e->pos, vmul(e->up, 60.0f)))) > 80.0f + 60.0f) continue;
        if (e->kind == WALK_PETARI) { /* isMsgFireBallAttack while solid: KickOut */
            if (!petari_solid(e)) continue;
            petari_blow(e, fireballs[b].pos, PT_KICKOUT); fireballs[b].t = 0; return;
        }
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
            hhp_blow(p, 60.0f); /* HammerHeadPackun: isMsgJetTurtleAttack */
            for (int k = 0; k < nkuribo; k++) { /* ACTMES_JET_TURTLE_ATTACK */
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || e->kind == WALK_BUTTERFLY ||
                    vlen(vsub(p, vadd(e->pos, vmul(e->up, 60.0f)))) > 60.0f + 80.0f) continue;
                if (e->kind == WALK_PETARI) { if (petari_solid(e)) petari_blow(e, p, PT_KICKOUT); continue; } /* KickOut */
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
    m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->spin_lock = 0;
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

/* --- Mario's movement --------------------------------------------------------------------------------------
 * After Petari: Mario.cpp (update, actionMain, inputStick, writeBackPhyisicalVector, setFrontVecKeepUp), MarioMove.cpp
 * (mainMove, decideInertia, calcShadowDir, retainMoveDir, isEnableTurn), MarioWalk.cpp (getTargetWalkSpeed,
 * decideSquatWalkAnimation, decideWalkSpeed, decideWalkAnimation, updateBrakeAnimation, updateWalkSpeed, stopWalk),
 * MarioJump.cpp (tryJump, tryTurnJump, trySquatJump, tryBackJump, trySpinJump, tryWallJump, tryDrop, initJumpParam,
 * procJump, checkWallJumpHit, jumpToHipDrop, procHipDrop, doAirWalk, cancelTornadoJump, doLanding), MarioWall.cpp
 * (checkWallStick, isEnableStickWall, MarioWall::start / update / startJump, moveWallSlide), MarioMagic.cpp (the ground
 * spin), MarioActor.cpp updateSwingAction / MarioActorPunch.cpp trySpinPunch; constants MC (tools/mario_const.py).
 * The gravity is -up; mJumpVec is hvel + up x vup. m->vel is the frame's move (mVelocity).
 * ponytail: our collision (collide_n) stands in for the Binder / checkGround / checkAllWall: no distance to the ceiling
 * (no ceiling hits, no squat uppercut), the height over the ground is a probe; no 2D, skate, ice, sand, slider, bee wall
 * walk, slip up, side step, cliff hold, rabbit; the standing turn snaps (no drawn _74C ease); the quad walk blend
 * (基本: WalkSoft / Walk / Run / Wait at sWeightTable) and その場足踏み (Run / Wait) play their heaviest clip */
static V3 m_cx, m_cy;          /* the camera's axes on the plane under Mario (calcMoveDir) */
static float m_sx, m_sy, m_sz; /* mStickPos */
static float m_rx, m_ry;       /* the raw stick (MarioActor::getStickValue) */
static V3 m_pad;               /* mWorldPadDir */
static int m_req_jump;         /* isRequestJump: A this frame */
static int m_draw_d;           /* mDrawStates._D: the stick held still (retainMoveDir) */

static float ang_abs(V3 a, V3 b) /* MR::diffAngleAbs */
{
    float la = vlen(a), lb = vlen(b);
    if (la == 0 || lb == 0) return 0;
    float x = vdot(a, b) / (la * lb);
    return x >= 1.0f ? 0.0f : x <= -1.0f ? 3.14159265f : acosf(x);
}

static float ang_abs_h(V3 a, V3 b, V3 up) { return ang_abs(vtangent(a, up), vtangent(b, up)); } /* diffAngleAbsHorizontal */

static int blend_sphere(V3 a, V3 b, V3 *o, float t) /* MR::vecBlendSphere */
{
    float la = vlen(a), lb = vlen(b), d = la == 0 || lb == 0 ? 0 : ang_abs(a, b), l = la * (1 - t) + lb * t;
    if (d < 0.1f) { *o = vmul(vnorm(vadd(vmul(vnorm(a), 1 - t), vmul(vnorm(b), t))), l); return 1; }
    if (d >= 3.14159265f) return 0;
    *o = vmul(vnorm(vadd(vmul(a, sinf(d * (1 - t))), vmul(b, sinf(d * t)))), l);
    return 1;
}

static V3 rot_toward(V3 from, V3 to, V3 axis, float ang) /* MR::vecRotAxis */
{
    float d = ang_abs(from, to);
    if (d == 0 || d <= ang) return to;
    if (d < 3.14159265f && vdot(vcross(from, to), axis) < 0) ang = -ang;
    return rot_axis(from, axis, ang);
}

static float clamp01(float f) { return f < 0 ? 0 : f > 1 ? 1 : f; }

static V3 m_jv(const Player *m) { return vadd(m->hvel, vmul(m->up, m->vup)); }
static void m_set_jv(Player *m, V3 j) { m->vup = vdot(j, m->up); m->hvel = vtangent(j, m->up); }
static int m_rising(const Player *m) { return m->vup > 0; } /* isRising */

static int m_clip_is(const char *name)
{
    return mario_anim && mario_anim->clip_id >= 0 && strcmp(sc->clips[mario_anim->clip_id].name, name) == 0;
}
static int m_clip_on(const char *name) { return m_clip_is(name) && (mario_anim->loop || !mario_anim->done); }
static void m_clip_stop(void) { if (mario_anim && !mario_anim->loop) mario_anim->done = 1; } /* stopAnimation(nullptr) */

static V3 m_move_dir(float x, float y) { return vadd(vmul(m_cx, x), vmul(m_cy, y)); } /* calcMoveDir */

static V3 m_shadow(const Player *m, V3 v) /* calcShadowDir: v on the plane under Mario */
{
    V3 d = vnorm(v), n = m->up;
    if (fabsf(vdot(d, n)) > fabsf(vdot(d, m->ground_n))) n = m->ground_n;
    return vnorm(vcross(vcross(n, d), n));
}

static void m_front(Player *m, V3 v) /* setFrontVecKeepUp */
{
    V3 f = vtangent(v, m->up);
    if (vlen(f) > 1e-4f) m->face = vnorm(f);
    m->sd = m->face;
}

static int m_stick_front_back(const Player *m) /* checkStickFrontBack */
{
    if (m_sz != 0) {
        float d = vdot(m_pad, vmul(m->face, -1.0f));
        if (d < -0.5f) return 1;
        if (d > 0.5f && !m->neutral) return 2;
    }
    return 0;
}

static int m_check_squat(Player *m, int a1) /* MarioModule::checkSquat: Z (R1) */
{
    if (kari_cling) return 0;
    if (m->squat_lock) { if (!pad_held(PAD_R1)) m->squat_lock = 0; return 0; }
    if (!a1 && !m->grounded && m->height > 100.0f) return 0;
    return pad_held(PAD_R1);
}

/* Mario::inputStick: x 1.5, clamped, |stick| under 0.01 is 0, angles within mStickAngleMargin of an axis snap to it */
static void m_input_stick(Player *m)
{
    float x = pad_lx(), y = pad_ly();
    if (pad_held(PAD_UP)) y += 1.0f;
    if (pad_held(PAD_DOWN)) y -= 1.0f;
    if (pad_held(PAD_RIGHT)) x += 1.0f;
    if (pad_held(PAD_LEFT)) x -= 1.0f;
    m_rx = x; m_ry = y;
    x = fminf(fmaxf(1.5f * x, -1.0f), 1.0f);
    y = fminf(fmaxf(1.5f * y, -1.0f), 1.0f);
    m->stick_z0 = m_sz;
    float z = fminf(sqrtf(x * x + y * y), 1.0f);
    if (z < 0.01f) z = 0;
    float a = atan2f(y, x), q = 1.5707963f, mg = MC.mStickAngleMargin, r;
    while (a < 0) a += 6.2831853f;
    int quot = 0;
    while (a > q) { a -= q; quot++; }
    r = a >= q - mg ? q : a <= mg ? 0.0f : q / (q - 2.0f * mg) * (a - mg);
    r += q * quot;
    m_sx = z * cosf(r); m_sy = z * sinf(r); m_sz = z;
    m->stick_ang0 = m->stick_ang; m->stick_ang = r;
    if (z <= 0.01f) m->neutral = 0; /* _1D */
    m_pad = vnorm(m_move_dir(m_sx, m_sy));
}

/* retainMoveDir: _D once the stick has kept its angle 30 frames (_40E) */
static void m_retain(Player *m)
{
    float ang = atan2f(m_sy, m_sx), diff = fabsf(ang - m->retain_ang);
    if (diff > 3.14159265f) diff = 6.2831853f - diff;
    if (m_clip_on("run") && m->stand_turn) diff = 1.0f; /* その場足踏み */
    if (m_clip_on("turn")) diff = 1.0f;
    float limit = m->land_timer < 2 ? 0.1f : 0.06f;
    if (m->retain_b) { if (m_sz != 0) m->retain_b = 0; diff = 1.0f; }
    int retain = !(m->jumping && !m->retain_a) && !(m->jumping && m->wall);
    m_draw_d = 0;
    if (diff < limit && m_sz != 0 && retain) {
        if (!m->retain_t || (m->retain_a && m->jumping)) { m_draw_d = 1; m->retain_t = 0; }
        else m->retain_t--;
    } else {
        if (m->jumping) { if (m_sz != 0) m->retain_a = 0; }
        else m->retain_a = 1;
        if (!m->retain_t) m->weak_turn = 0;
        m->retain_t = 30;
        if (m_sz == 0) m->retain_b = 1;
    }
    m->retain_ang = ang;
}

static int m_enable_turn(const Player *m) /* isEnableTurn */
{
    return m->grounded && !m->turn_brake && m->slip_keep && !m->squat && !m->magic && !m->hip && !m->spin_timer;
}

static void m_stop_walk(Player *m) /* stopWalk (cancelSquatMode, the brake clips stopped) */
{
    m->tsi = 0; m->walk = 0; m->brake1 = m->brake2 = 0; m->weak_turn = 0;
    m->turn_neutral = 0; m->squat = 0; m->turning = 0; m->turn_ready = 0; m->start_spin_ratio = 0;
    if (m_clip_is("runend") || m_clip_is("brake")) m_clip_stop();
}

static float m_target_walk(const Player *m) /* getTargetWalkSpeed (sWalkTargetTable) */
{
    static const float t[8] = {0.0f, 0.15f, 0.25f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f};
    return m->squat ? 0.0f : t[m->tsi];
}

static float m_inertia(Player *m, float speed) /* decideInertia */
{
    if (m->walk > 1.0f) return MC.mInertiaOverSpeed;
    float in = (1.0f - m->walk) * MC.mInertiaStandardStop + m->walk * MC.mInertiaStandardMax;
    in = (1.0f - m->start_spin_ratio) * in + m->start_spin_ratio * MC.mInertiaStartSpin;
    if (speed == 0.0f) {
        in = MC.mInertiaStop;
        if (m->turn_neutral) in = MC.mInertiaTurnSlip;
        if (m->turn_brake) in = MC.mInertiaTurning;
        if (m->land_timer < 30) in = MC.mInertiaJumpFinish;
        if (m->squat) in = MC.mInertiaSquat;
    }
    if (m->walk < 0.08f && m->land_timer > 10 && speed > 0.5f && m->squat) { m->start_spin = MC.mStartSpinTime; m->walk = 0.08f; }
    if (m->start_spin) { m->start_spin--; in = MC.mInertiaStartSpin; if (!m->start_spin) m->start_spin2 = 60; }
    if (m->start_spin2) m->start_spin2--;
    return in;
}

static void m_update_walk_speed(Player *m) /* updateWalkSpeed */
{
    float t = m_target_walk(m), f2 = 1.0f;
    if (t == 0.0f) m->slow_start = MC.mSlowStartTime;
    if (m->slow_start) { f2 = (float)(MC.mSlowStartTime - m->slow_start) / MC.mSlowStartTime; m->slow_start--; }
    t *= f2 * f2;
    m->squat = 0;
    if (m_check_squat(m, 0)) {
        m->squat = 1;
        if (m->wall) { m->tsi = 0; m->walk = 0; }
    }
    if (m->turn_slip || m->turn_neutral) t = 0.0f;
    float in = m_inertia(m, t);
    m->walk = m->walk * in + t * (1.0f - in);
}

static void m_decide_walk_speed(Player *m) /* decideWalkSpeed: the stick picks mTargetWalkSpeedIndex */
{
    static const float a[7] = {0.15f, 0.3f, 0.45f, 0.6f, 0.7f, 0.85f, 0.99f}, b[7] = {0.02f, 0.2f, 0.4f, 0.5f, 0.65f, 0.75f, 0.98f};
    int dec = m->tsi && m_sz < b[m->tsi - 1], i;
    for (i = 0; i < 7 && m_sz >= a[i]; i++) ;
    if (m->tsi <= i || dec) m->tsi = i;
    int n = kari_cling > 5 ? 5 : kari_cling;
    if (n && m->tsi > 5 - n) m->tsi = 5 - n;
}

/* doBrakingAnimation / updateBrakeAnimation: 歩行制動ブレーキ (RunEnd) after a run let go; its end stops the walk */
static void m_update_brake(Player *m)
{
    if (m->brake2) {
        if (!m_clip_on("runend")) m->brake2 = 0;
        else {
            m->brake2--;
            if (m_sz != 0) m->brake2 = 0;
            if (!m->brake2) { m_clip_stop(); m_stop_walk(m); }
        }
    } else if (m_clip_on("runend") && (mario_anim->done || m->tsi)) m_clip_stop();
}

/* decideWalkAnimation: the 基本 blend by mTargetWalkSpeedIndex, its speed from the foot steps, the start-spin ratio, the
 * brake timers; DamageWait for the wait at 1 life of more than 2 */
static void m_decide_walk_anim(Player *m)
{
    static const float W[8][4] = {{0, 0, 0, 1}, {1, 0, 0, 0}, {0.75f, 0.25f, 0, 0}, {0.25f, 0.75f, 0, 0},
                                  {0, 1, 0, 0}, {0, 0.75f, 0.25f, 0}, {0, 0.25f, 0.75f, 0}, {0, 0, 1, 0}};
    static const float step[4] = {1.2f, 1.5f, 1.3f, 0.0f}, tt[8] = {0.0f, 0.15f, 0.25f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f};
    static const char *Q[4] = {"walksoft", "walk", "run", "wait"};
    int best = 0;
    for (int i = 1; i < 4; i++) if (W[m->tsi][i] > W[m->tsi][best]) best = i;
    const char *clip = best == 3 && life == 1 && max_life > 2 ? "damagewait" : Q[best];
    if (m->tsi == 0 && m->walk < 0.2f) /* controlWaitAnimation (ponytail: no special waits) */
        mario_play(clip, 1.0f, 1);
    else {
        float f3 = 0;
        for (int i = 0; i < 4; i++) f3 += step[i] * W[m->tsi][i];
        float sp = f3 == 0.0f ? 0.33f : 0.5f * (60.0f * (0.01f * (m->walk * MC.mWalkSpeed) / f3));
        float target = m_target_walk(m), d = target - m->walk;
        if (m->walk > tt[5]) { float k = (m->walk - tt[5]) / (1.0f - tt[5]); d = d * (1.0f - k) + d * d * k; }
        else if (m->walk < tt[3]) { float k = (tt[3] - m->walk) / tt[3]; d = d * k + sqrtf(fmaxf(d, 0.0f)) * (1.0f - k); }
        float f5 = fminf(fmaxf(1.0f + 4.0f * d, 1.0f), 2.0f);
        if (target < tt[6]) f5 = 1.0f;
        float f6 = MC.mStartSpinAnimeRatio * (0.5f * (60.0f * (target * MC.mWalkSpeed * 0.01f / step[2]))), k2 = 2.0f - f5;
        if (sp < f6) sp = sp * k2 + f6 * (1.0f - k2);
        m->start_spin_ratio = f5 - 1.0f;
        mario_play(clip, sp, 1);
    }
    if (m->tsi > 5 && m->walk > 0.9f && !m->turn_brake) m->brake1 = MC.mBrakeFirstTimer;
    if (m->brake1) m->brake1--;
    if (m->tsi == 0) {
        if (m->brake1 && m->slip_keep) { mario_play("runend", 1.0f, 0); m->brake2 = MC.mBrakeSecondTimer; }
        m->brake1 = 0;
    }
}

/* decideSquatWalkAnimation: しゃがみ基本 (SquatWait / SquatWalk), squat walking at mSquatWalkMin..MaxSpeed */
static void m_squat_anim(Player *m)
{
    m->start_spin_ratio = 0;
    if (m->walk < MC.mSpeedSquatWalkLower) {
        if (m_sz != 0) { m->walk = MC.mSquatWalkMinSpeed; m->tsi = 1; m->squat_walk = 1; }
        else { m->tsi = 0; m->squat_walk = 0; }
    } else if (m_sz == 0) m->tsi = 0;
    else if (m->squat_walk) { m->walk = (MC.mSquatWalkMaxSpeed - MC.mSquatWalkMinSpeed) * m_sz + MC.mSquatWalkMinSpeed; m->tsi = 1; }
    if (m->tsi) mario_play("squatwalk", m->squat_walk ? 60.0f / MC.mSquatWalkStep * m->walk : 1.0f, 1);
    else mario_play("squatwait", 1.0f, 1);
}

static void m_proc_jump(Player *m, int a1);
static void m_init_jump(Player *m);

static void m_try_turn_jump(Player *m) /* tryTurnJump */
{
    m->jump_kind = 4;
    V3 h;
    if (m->turn_ready) {
        h = vnorm(vtangent(m->turn_ready_front, m->up));
        m_front(m, vmul(m->turn_ready_front, -1.0f));
        m->turn_from = m->turn_ready_front;
    } else h = vnorm(vtangent(m->turn_from, m->up));
    m_set_jv(m, vadd(vmul(h, -MC.mTurnJumpFrontSpeed), vmul(m->up, MC.mJumpHeight[4])));
    m->digital = 1; m->grounded = 0;
    m_proc_jump(m, 1);
    mario_play("jumpturn", 1.0f, 0);
#ifdef BENCH
    printf("jump jumpturn\n");
#endif
    m->turn_slip = 0; m->jumping = 1; m->turn_brake = 0; m->connect = 0; m->walk = 0; m->retain_a = 0;
}

static void m_try_squat_jump(Player *m) /* trySquatJump: the long jump */
{
    m->squat_lock = 0;
    m->jump_kind = 5;
    m_set_jv(m, vadd(vmul(m->face, MC.mSquatJumpFrontSpeed), vmul(m->up, MC.mJumpHeight[5])));
    m->digital = 1; m->grounded = 0;
    m_proc_jump(m, 1);
    mario_play("jumpbroad", 1.0f, 0);
#ifdef BENCH
    printf("jump jumpbroad\n");
#endif
    m->jumping = 1; m->turn_slip = 0; m->connect = 0; m->turn_brake = 0;
}

static void m_try_back_jump(Player *m) /* tryBackJump: the back flip */
{
    m->squat_lock = 1; m->squat = 0;
    m->jump_kind = 6;
    m_set_jv(m, vadd(vmul(m->face, -MC.mSquatJumpBackSpeed), vmul(m->up, MC.mJumpHeight[6])));
    m->digital = 1; m->grounded = 0;
    m_proc_jump(m, 1);
    mario_play("jumpback", 1.0f, 0);
#ifdef BENCH
    printf("jump jumpback\n");
#endif
    m->walk = 0; m->jumping = 1; m->turn_slip = 0; m->turn_brake = 0; m->connect = 0;
}

static void m_try_jump(Player *m) /* tryJump: 1st / 2nd / 3rd jump, or the turn, long and back jumps */
{
    if (m->jumping) return;
    int squat = m_check_squat(m, 1);
    m->squat_lock = 1;
    if (m->land_timer > MC.mJumpConnectTime || !m->connect) m->jump_kind = 0;
    else m->jump_kind = m->jump_kind + 1;
    if (m->jump_kind > 2) m->jump_kind = 0;
    if (m->jump_kind == 2 && m->walk < MC.mJumpConnectSpeed) m->jump_kind = 0;
    if (m->turn_brake || (m->turn_ready && m_stick_front_back(m) == 2)) {
        if (MC.mTurnSlipTime - m->turn_slip >= MC.mTurnJumpInhibitTime) { m_try_turn_jump(m); return; }
        m->turn_slip = 0; m->turn_brake = 0;
    }
    m->slip_front = !m->wall; /* checkWallRiseAndSlipFront */
    if (m->wall) m->walk = 0;
    if (squat && m->walk > 0.3f) { m_try_squat_jump(m); return; }
    if (m->squat) { m_try_back_jump(m); return; }
    m_set_jv(m, vadd(vmul(m->face, m->walk * MC.mJumpFrontSpeed), vmul(m->up, MC.mJumpHeight[m->jump_kind])));
    m->digital = 0; m->grounded = 0;
    m_proc_jump(m, 1);
    m->jumping = 1;
    static const char *clips[3] = {"jump", "jump2", "jumproll"};
    mario_play(clips[m->jump_kind], 1.0f, 0);
    m->connect = 1;
#ifdef BENCH
    printf("jump %s\n", clips[m->jump_kind]);
#endif
}

static void m_try_drop(Player *m) /* tryDrop: off a ledge (cDropFrontSpeed 2) */
{
    m->squat_lock = 1;
    m_set_jv(m, vmul(m->face, m->walk * 2.0f));
    m->jump_kind = 0;
    m_proc_jump(m, 1);
    m->jumping = 1; m->connect = 0; m->digital = 1;
    if (m->wall) m_stop_walk(m);
}

static void m_cancel_tornado(Player *m) /* cancelTornadoJump */
{
    m->jump_kind = 0;
    if (m_clip_is("spin")) m_clip_stop();
    m->jump_from = m->pos;
    m->spin_lock = 1;
}

static void m_try_spin_jump(Player *m) /* trySpinJump (_42A 1: the button, not a shake) */
{
    if (m->hip || m->jump_kind == 8) return;
    m->jump_kind = 8; m->spin_kind = 1;
    m_set_jv(m, vadd(vmul(m->face, m->walk * MC.mJumpTornadoSpeed), vmul(m->up, MC.mSpinJumpHeight)));
    m->digital = 0;
    m->air_spin = MC.mAirWalkTimeSpin;
    m_proc_jump(m, 1);
    mario_play("spin", 1.0f, 0);
    m->jumping = 1; m->turn_brake = 0; m->connect = 0; m->neutral = 0; m->turn_slip = 0; m->walk = 0;
#ifdef BENCH
    printf("jump spin\n");
#endif
}

static void m_try_wall_jump(Player *m, V3 v) /* tryWallJump(v, true) */
{
    m->jump_kind = 7;
    m->jumping = 1; m->connect = 0; m->digital = 1; m->hip = 0; m->slip_front = 1;
    m_set_jv(m, v);
    m->jump_from = m->pos;
    m->air_timer = 0; m->keep_jump = 1; m->back_t = 0; m->air_walk_t = 0; m->fall_t = 0;
    m->neutral = 1;
    mario_play("walljump", 1.0f, 0);
    m_front(m, v);
    m_stop_walk(m);
}

/* endBindAndSpinDriverJump (RushEndInfo 3, flags 0xC0000000) -> tryForcePowerJump: kind 13, digital, no air walk
 * (_2F) until the next jump */
static void m_power_jump(Player *m, V3 v)
{
    m->jump_kind = 13;
    m->jumping = 1; m->grounded = 0; m->connect = 0; m->digital = 1; m->hip = 0; m->walling = 0; m->magic = 0;
    m_set_jv(m, v);
    m->air_timer = 0; m->back_t = 0; m->air_walk_t = 0; m->fall_t = 0; m->keep_jump = 1;
    m->jump_from = m->pos;
    m->air_spin = MC.mAirWalkTime;
    m->spin_lock = 0;
    m->no_air_walk = 1;
    m->drop_free = vdot(v, m->up) < 0; /* _20: no drop speed cap */
    m->wall_free = 1;                  /* _22: walls leave the jump vector alone */
}

static void m_init_jump(Player *m) /* initJumpParam */
{
    m->keep_jump = 1; m->wall_hit = 0; m->no_air_walk = 0; m->drop_free = 0; m->wall_free = 0;
    m->air_timer = 0; m->back_t = 0; m->air_walk_t = 0; m->fall_t = 0;
    m->turn_neutral = 0; m->turn_brake = 0; m->u_turn = 0; m->turn_ready = 0; m->turn_slip = 0; m->land_timer = 0;
    if (m->jump_kind != 5) m->squat = 0;
    m->jump_from = m->pos;
}

static int m_jump_to_hip_drop(Player *m) /* jumpToHipDrop */
{
    if (m->hip || m->jump_kind == 5) return 0;
    if ((m->jump_kind == 9 || m->jump_kind >= 11) && m_rising(m)) return 0;
    if (m->height < MC.mHipDropLimitHeight) return 0;
    if (m->jump_kind == 8) m_cancel_tornado(m);
    m->hip = HIP_START; m->hip_still = 0;
    m->squat_lock = 0;
    m->hvel = v3(0, 0, 0); m->vup = 0;
    if (m->jump_kind == 4) { m_front(m, vmul(m->turn_from, -1.0f)); m->jump_kind = 0; }
    mario_play("hipdropstart", 1.0f, 0);
    m->zero_gr = MC.mHipDropZeroGrTime;
    return 1;
}

/* procHipDrop: still while HipDropStart plays and mHipDropZeroGrTime after, then down at mGravityHipDrop x a rate
 * falling to 0.1 near mLimitSpeedHipDrop; the landing (HipDropLand) ends at its end, the stick after 15, A after 5 */
static void m_proc_hip(Player *m)
{
    if (m->hip == HIP_START) {
        if (mario_anim && !mario_anim->done) return;
        if (m->zero_gr) { m->zero_gr--; return; }
        m->hip = HIP_FALL;
        mario_play("hipdrop", 1.0f, 1);
        return;
    }
    if (m->zero_gr) { m->zero_gr--; return; }
    if (m->hip == HIP_LAND) {
        int end = 0;
        if (!m->grounded && m->air_timer > 3 && m->height > 10.0f) { /* off the ground again: falling on */
            m->hip = HIP_FALL; mario_play("hipdrop", 1.0f, 1);
            m_set_jv(m, m->hip_jv);
        } else {
            if (m->land_timer > 15 && m_sz != 0) end = 1;
            if (m->land_timer > 5 && m_req_jump) end = 1;
            if ((mario_anim && mario_anim->done) || end) {
                m->jumping = 0; m->hip = 0;
                m_clip_stop();
                if (m_req_jump) { m_try_jump(m); return; }
            }
            m->walk = 0;
            m->hvel = v3(0, 0, 0); m->vup = 0;
            m->vel = v3(0, 0, 0);
            return;
        }
    } else {
        m->air_timer++;
        if (vlen(vsub(m->pos, m->last_pos)) < 0.001f && ++m->hip_still == 15) { /* stuck */
            m->jumping = 0; m->hip = 0; m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 1;
            return;
        }
    }
    m->vel = m_jv(m);
    float rate = clamp01(0.1f + (1.0f - vlen(m_jv(m)) / MC.mLimitSpeedHipDrop));
    m->vup -= MC.mGravityHipDrop * rate;
    if (vlen(m->hvel) > 10.0f) m->hvel = vmul(m->hvel, 0.5f);
    V3 j = m_jv(m);
    if (vlen(j) > MC.mLimitSpeedHipDrop) m_set_jv(m, vmul(vnorm(j), MC.mLimitSpeedHipDrop));
}

static void m_hip_land(Player *m) /* procHipDrop's first frame on the ground */
{
    m->land_timer = 0; m->walk = 0; m->brake1 = 0;
    hip_hit = 1; hip_inst = ground_inst; /* tell the objects under Mario */
    kari_cling = 0;                      /* MR::removeAllClingingKarikari */
    m->hip = HIP_LAND;
    mario_play("hipdropland", 1.0f, 0);
    m->hip_jv = m_jv(m);
    m->air_timer = 0;
    m->hvel = v3(0, 0, 0); m->vup = 0;
}

/* doAirWalk: the stick in the air; pulling back brakes (mBackJumpRatio over mBackJumpLimitFrame), the front speed
 * decays after mJumpFrontReductionBeginTime, the stick pushes mWalkSpeed x mAirWalkTimerFact1 / (t + Fact2) */
static void m_air_walk(Player *m)
{
    if (m->jump_kind == 3 || m->no_air_walk) return;
    if (m->neutral) { if (m->air_timer > MC.mWaitNeutralTimer) m->neutral = 0; return; }
    if (m->air_lock) { m->air_lock--; return; }
    V3 md = vtangent(m_move_dir(m_sx, m_sy), m->up), side;
    int inhibit = 0;
    float fd = vdot(md, m->face);
    side = vsub(md, vmul(m->face, fd));
    if (m->jump_kind == 8) inhibit = 1;
    else if (fd >= 0) {
        float red = m->jump_kind == 5 ? MC.mSquatJumpFrontReduction : MC.mJumpFrontReduction;
        int begin = m->jump_kind == 5 ? MC.mSquatJumpFrontReductionBTime : MC.mJumpFrontReductionBeginTime;
        if (m->air_timer > begin) m->hvel = vmul(m->hvel, red);
    } else {
        if (!m_rising(m) && m->height < MC.mLandTurnHeight) m->turn_neutral = 1;
        else m->back_t++;
        float killer = m_clip_on("jumpturn") ? (m->back_t = 0, 0.1f)
                     : m->back_t < MC.mBackJumpLimitFrame ? MC.mBackJumpRatio * ((float)(MC.mBackJumpLimitFrame - m->back_t) / MC.mBackJumpLimitFrame)
                     : 0.0f;
        md = vadd(side, vmul(m->face, fd * killer));
        float back = -vdot(md, m->face);
        if (back > MC.mMaxBackJumpSpeed) md = vmul(vnorm(md), MC.mMaxBackJumpSpeed);
        else if (back < 0 && m->back_t < MC.mBackJumpLimitFrame) m->air_walk_t = 10;
    }
    if (!inhibit) {
        float al = vdot(vnorm(md), vnorm(m_jv(m)));
        if (al < 0) m->hvel = vadd(m->hvel, vmul(m->hvel, MC.mAirWalkSpeedKiller * al));
    }
    if (m->jump_kind == 8) m->hvel = vadd(m->hvel, vmul(md, MC.mWalkSpeed * 5.0f / MC.mAirWalkTimerFact2));
    else m->hvel = vadd(m->hvel, vmul(md, MC.mWalkSpeed * MC.mAirWalkTimerFact1 / (m->air_timer + MC.mAirWalkTimerFact2)));
    if (!m_rising(m)) m->vel = vadd(m->vel, vmul(md, MC.mWalkSpeed * MC.mAirWalkTimerFact3));
    float cap = m->jump_kind == 8 ? 6.0f : MC.mMaxJumpSpeed;
    if (vlen(m->hvel) > cap) m->hvel = vmul(vnorm(m->hvel), cap);
}

/* procJump: the jump vector moves Mario, then falls at mGravityJumping[kind] x 0.5 (mGravityRatioA) while A is held,
 * the jump is "digital" or he is falling, x 1 once A is let go rising; Karikari clinging x (2 + n) / 2 */
static void m_proc_jump(Player *m, int a1)
{
    if (m->hip) { m_proc_hip(m); return; }
    if (m->zero_gr) { m->zero_gr--; return; }
    if (!pad_held(PAD_CROSS)) m->keep_jump = 0;
    int rising = m_rising(m);
    int conn = a1 || m->keep_jump || m->digital || !rising || m->jump_kind == 2;
    float gs = conn ? MC.mGravityRatioA : 1.0f;
    if (m->jump_kind == 13 && (m->digital || !rising)) gs = 1.0f;
    if (a1) m_init_jump(m);
    else if (pad_pressed(PAD_R1) && m_jump_to_hip_drop(m)) return;
    if (!rising) {
        if (m_clip_on("jump2")) m_clip_stop();
        m->fall_t++;
    }
    int was_air_walk = 0;
    if (m->air_walk_t) { was_air_walk = 1; m->air_walk_t--; }
    if (m->jump_kind == 8 && m->spin_kind) {
        if (m->air_spin) m->air_spin--;
        if (!m->air_spin) { m_cancel_tornado(m); m->air_spin = MC.mAirWalkTime; }
    }
    float acc = m->jump_kind == 12 ? 1.8f : MC.mGravityJumping[m->jump_kind == 13 ? 9 : m->jump_kind];
    if (m->jump_kind == 8 && m->spin_kind == 1) acc = MC.mSpinJumpGravity;
    if (m->jump_kind == 8 && !m_rising(m)) acc *= 0.25f;
    if (kari_cling) acc *= (2.0f + kari_cling) / 2.0f;
    if (was_air_walk) acc = MC.mGravityAirWalk;
    m->vel = m_jv(m);
    m->vup -= acc * gs;
    /* checkWallJumpHit: a long jump into a wall bounces off it (ponytail: no MarioBlown) */
    if (m->jump_kind == 5 && m->wall) {
        float into = vdot(m->hvel, m->wall_n);
        if (into < 0) {
            m->hvel = vsub(m->hvel, vmul(m->wall_n, into));
            if (vlen(m->hvel) > 1e-3f) m_front(m, vmul(m->hvel, -1.0f));
            m->spin_lock = 1; m->air_spin = 0; m->air_lock = 60;
        }
    }
    if (!(m->jump_kind == 13 && m->drop_free) && -m->vup >= MC.mMaxDropSpeed[0]) m->vup = -MC.mMaxDropSpeed[0];
    m_air_walk(m);
    m->air_timer++;
}

/* checkWallStick / isEnableStickWall: falling against a wall (not in a hip drop, the spin's first 25 frames, Karikari
 * clinging, the wall left at the same angle) Mario sticks to it. ponytail: the wall contact stands for the 120 probe */
static void m_check_wall_stick(Player *m)
{
    if (!m->jumping || m->grounded || m->hip || !m->wall || m_rising(m) || kari_cling) return;
    if (m->wall_cancel && vdot(m->wall_cancel_n, m->wall_n) < MC.mWallStickCancelAngle) m->wall_cancel = 0;
    if (m->wall_cancel) return;
    if (vdot(m->face, m->wall_n) > 0) return; /* a back wall needs the stick into it */
    if (m->height < MC.mWallStickGrHeight) return;
    if (m_clip_on("spin") && m->spin_t < 25) return;
    if (!m_clip_on("walljump") && !m->slip_front && !(m_sz != 0 && vdot(m_pad, m->wall_n) < -0.5f)) return;
    m->walling = 1; m->wall_t = 0; m->wall_drop = 0; m->wall_pull = 0;
    m->wall_jv0 = vnorm(m->hvel); /* initTriangleJump */
    mario_play("wallkeep", 1.0f, 1);
    m_stop_walk(m);
    m->jumping = 0; m->hip = 0;
    m_front(m, vmul(m->wall_n, -1.0f));
}

/* MarioWall::update: slides down (mWallDropSpeedNormal after mWallStickTime), A kicks off (startJump: the wall's normal
 * x mWallJumpPowerXZ + up x mWallJumpPowerY, mirrored off the way he came in within 15 frames), let go after
 * mWallReleaseTime + mWallStickTime, off the wall or on the ground */
static int m_wall_update(Player *m)
{
    int release = 0;
    m->wall_t++;
    if (m->grounded) {
        if (m->slip_keep) { m_front(m, m->wall_n); mario_play("land", 1.0f, 0); }
        release = 1;
    }
    if (m->wall_t >= MC.mWallReleaseTime + MC.mWallStickTime) release = 1;
    if (m->wall_t >= 3 && !m->wall) release = 1;
    if (release) {
        if (!m->grounded) { m->wall_cancel = 1; m->wall_cancel_n = m->wall_n; m_front(m, vmul(m->wall_n, -1.0f)); }
        m->vel_after = vmul(m->wall_n, m->slip_keep ? 30.0f : 10.0f);
        m->walling = 0;
        return 0;
    }
    if (m_req_jump) {
        V3 v = m->wall_n;
        float d = vdot(v, vmul(m->wall_jv0, -1.0f));
        if (m->wall_t < 15 && vlen(m->wall_jv0) > 0 && d < MC.mWallTriJumpMargin && d > 0) {
            V3 ax = vnorm(vcross(vmul(m->wall_jv0, -1.0f), v));
            v = rot_axis(v, ax, acosf(d));
        }
        m->walling = 0;
        m->wall_cancel = 1; m->wall_cancel_n = m->wall_n;
        m_try_wall_jump(m, vadd(vmul(v, MC.mWallJumpPowerXZ), vmul(m->up, MC.mWallJumpPowerY)));
        m->spin_lock = 0;
        return 0;
    }
    float blend = 0.9f, speed = MC.mWallDropSpeedNormal;
    int side = m_sz != 0 && vdot(m_pad, m->wall_n) > 0.5f && !m->neutral ? 2 : 0; /* checkStickWallSide (1 counts as 0) */
    if (side == 2) { if (m->wall_t < 165) m->wall_t = 165; if (!m->wall_pull) m->wall_pull = 1; }
    else if (m->wall_t >= MC.mWallStickTime && !m_clip_on("wallkeep")) mario_play("wallslide", 1.0f, 1);
    if (m->wall_pull) m->wall_pull++;
    if (m->wall_t < MC.mWallStickTime) blend = 1.0f;
    m->wall_drop = m->wall_drop * blend + speed * (1.0f - blend);
    m->vel = vmul(m->up, -m->wall_drop);
    if (m->wall_t >= MC.mWallStickTime && m_clip_is("wallkeep") && mario_anim->loop) mario_play("wallslide", 1.0f, 1);
    float ratio = m->wall_t < MC.mWallStickTime ? 0.0f : 1.0f - (m->wall_t - MC.mWallStickTime) * MC.mWallSideMoveRatio;
    V3 c = vnorm(vcross(vmul(m->up, -1.0f), m->wall_n)); /* moveWallSlide */
    if (m_sz != 0 && vlen(c) > 0) {
        float d = vdot(c, m_pad);
        if (fabsf(d) > 0.2f) {
            d = d < -0.2f ? (0.2f + d) / 0.8f : (d - 0.2f) / 0.8f;
            m->vel = vadd(m->vel, vmul(c, 10.0f * d * clamp01(ratio)));
        }
    }
    return 1;
}

/* doLanding: the landing clip by the jump, a run on (the stick full, or more than 6 forward kept as walk speed), the
 * hard landing over mHardLandingHeight */
static void m_do_landing(Player *m)
{
    int hard = 0, keep = 0;
    m->jumping = 0; m->land_timer = 0;
    m->spin_lock = 0; /* ponytail: Petari's _2B clear on the ground was not found; one spin per jump */
    V3 jv = m_jv(m);
    if (m->air_timer <= 3 && fabsf(vdot(vsub(m->pos, m->jump_from), m->up)) < 1.0f && vlen(jv) < 10.0f) {
        m_clip_stop();
        return;
    }
    m->squat_lock = 0;
    m->air_spin = MC.mAirWalkTime;
    if (vdot(vsub(m->jump_from, m->pos), m->up) > MC.mHardLandingHeight) hard = 1;
    if (!hard && m->slip_keep) {
        if (m->jump_kind == 5 && m_check_squat(m, 0)) { m->squat = 1; m->walk = 1.0f; m_clip_stop(); return; }
        if ((m_sz > 0.9f && m->back_t == 0) || (!m->hip && m_check_squat(m, 0))) {
            m_clip_stop();
            if (!m->retain_a) { m->turn_ready_front = m->face; m->turn_ready = MC.mTurnReadyTime; } /* recordTurnSlipAngle */
            if (m->jump_kind == 5) m->squat = 0;
            return;
        }
        switch (m->jump_kind) { case 2: case 4: case 5: case 6: case 8: case 11: keep = 1; }
        float fd = vdot(m->face, jv);
        if (fd > 6.0f && !keep) {
            m->walk = fminf(fmaxf(fd / MC.mJumpFrontSpeed, 0.0f), 2.0f);
            m_clip_stop();
            m->start_spin = 0; m->brake1 = 0;
            if (!m->retain_a) { m->turn_ready_front = m->face; m->turn_ready = MC.mTurnReadyTime; }
            return;
        }
    }
    if (m->jump_kind == 4) m_front(m, vmul(m->turn_from, -1.0f));
    if (m->slip_keep) {
        static const char *land[14] = {"land", "jump2land", "jumprollland", "", "jumpturnland", "jumpbroadland",
                                       "jumpbackland", "land", "land", "land", "", "land", "land", "land"};
        int k = m->jump_kind;
        if (k == 5 && !m_check_squat(m, 0)) m->squat = 0;
        if (k == 3 || k == 10) m_clip_stop();
        else mario_play(land[k], 1.0f, 0);
        m_stop_walk(m);
        if (hard) { mario_play("landstiffen", 1.0f, 0); m->land_timer += 16; }
    } else {
        mario_play("landslope", 1.0f, 0);
        m->brake1 = 0;
    }
}

/* mainMove: the ground move. The stick turns Mario (on the spot over 10 degrees from still, at mTurnAngleSpeed /
 * Speed2 / SlowWalk running, weakened for mWeakTurnTime, a U-turn arc at 0.12 a frame), pulling it back from a run
 * brakes and turns (ターンブレーキ), then he moves mWalkSpeed x his walk speed along the ground */
static void m_main_move(Player *m)
{
    int a1 = 0;
    V3 dir = m->move_dir;
    int dir_set = 0;
    if (m_req_jump) { m_try_jump(m); return; }
    if (!m->grounded) {
        if (m->drop_grace) m->drop_grace--;
        else { m_try_drop(m); return; }
    } else if (m_sz < MC.mWallBackHangStickPower) m->drop_grace = 0;
    else if (m->drop_grace < MC.mDropWaitTime) m->drop_grace = MC.mDropWaitTime;
    /* MarioAnimator::setWalkMode */
    if (m->turn_neutral && !m->squat && m_stick_front_back(m) == 2 && !m->magic) mario_play("brake", 1.0f, 0);
    else if (m_clip_on("brake")) { mario_play("runend", 1.0f, 0); m->brake2 = 0; m->tsi = 0; }
    if (m_sz == 0) {
        m->move_dir = m->move_dir_keep;
        if (vlen(m->move_dir) < 1e-4f) m->move_dir = m->face;
        dir = m->move_dir; dir_set = 1;
        m->sd = m_shadow(m, m->move_dir);
        m->stand_neutral = 10;
    } else if (!m_clip_on("brake") && !m_clip_on("turn") && !m->turn_neutral && m->walk < 0.05f) {
        int a3 = 1;
        if (m->stand_neutral) {
            if (m_sz < 0.75f) { a3 = 0; m->stand_neutral--; m_sx = m_sy = m_sz = 0; }
            else m->stand_neutral = 0;
        }
        if (a3) {
            dir = m_move_dir(m_sx, m_sy); dir_set = 1;
            m->sd = m_shadow(m, dir);
            if (ang_abs_h(m_shadow(m, m->face), m->sd, m->up) > 3.14159265f * 10.0f / 180.0f) {
                m_front(m, m->sd);
                m->stand_turn = MC.mStandingTurnTime; /* setFrontVecKeepUp(_22C, mStandingTurnTime) */
                return;
            }
            m_front(m, m->sd);
        } else dir = m->move_dir;
    } else {
        float x = m_sx, y = m_sy;
        if (fabsf(y) > MC.mStickMarginYstart) {
            if (fabsf(x) < MC.mStickMarginX) x = 0;
            else x = x > 0 ? (x - MC.mStickMarginX) / (1.0f - MC.mStickMarginX) : (x + MC.mStickMarginX) / (1.0f - MC.mStickMarginX);
        } else if (fabsf(x) > MC.mStickMarginXstart) {
            if (fabsf(y) < MC.mStickMarginY) y = 0;
            else y = y > 0 ? (y - MC.mStickMarginY) / (1.0f - MC.mStickMarginY) : (y + MC.mStickMarginY) / (1.0f - MC.mStickMarginY);
        }
        dir = m_move_dir(x, y); dir_set = 1;
        m->sd = m_shadow(m, dir);
    }
    V3 fs = m_shadow(m, m->face);
    float dot = vdot(fs, m->sd), dot2 = m->turn_ready ? vdot(m->turn_ready_front, m->sd) : dot;
    if (dot >= cosf(MC.mTurnSlipAngle) && !m->turn_brake && m->walk > MC.mSlipSpeed && m->stick_z0 > m_sz && !m_draw_d) {
        m->turn_ready_front = m->face; m->turn_ready = MC.mTurnReadyTime; /* recordTurnSlipAngle */
    } else if (m->turn_ready) m->turn_ready--;
    if (m->turn_ready) {
        if (m_sz < MC.mTurnSlipNeutral) m->turn_neutral = 1;
        if (m->walk < 0.1f) { m->turn_ready = 0; m->walk = 0; }
        if (m_clip_on("brake") || m->turn_neutral) a1 = 1;
    } else {
        if (!m->squat && m->turn_neutral && dot2 < cosf(MC.mTurnSlipAngle) && !m->turn_brake) {
            m->turn_from = m->turn_ready_front;
            m_front(m, vmul(m->turn_from, -1.0f));
            m->turn_slip = MC.mTurnSlipTime; m->turn_brake = 1; m->walk = 0;
            mario_play("turn", 1.0f, 0);
        }
        m->turn_neutral = 0;
    }
    if (m->turn_slip) { m->turn_slip--; m->walk = 0; }
    if (!m->turn_slip && m->turn_brake) {
        if (m_sz != 0 && ang_abs_h(m_pad, m->face, m->up) > 3.14159265f * 0.5f) {
            m_clip_stop(); m_front(m, vmul(m->face, -1.0f)); m->turn_brake = 0;
        }
        if (m_clip_is("turn") && mario_anim->done) {
            m->turn_brake = 0;
            if (m_sz < 0.1f) m_stop_walk(m);
        }
    }
    float ts = MC.mTurnAngleSpeed;
    if (m->walk > MC.mFastTurnSpeed) { ts = MC.mTurnAngleSpeed2; if (m->tsi < 5) ts = MC.mTurnAngleSpeedSlowWalk; }
    fs = m_shadow(m, m->face);
    if (!m->turn_brake) {
        V3 cross = vcross(m->last_sd, m->sd);
        do {
            if (m_sz == 0) { m->u_turn = 0; m->turning = 0; break; }
            if (vlen(m->turn_cross) < 1e-6f) {
                if (fabsf(m->last_rx - m_rx) > 1e-6f || fabsf(m->last_ry - m_ry) > 1e-6f) { m->turning = 1; break; }
            } else if (vdot(cross, m->turn_cross) > 0) { m->turning = 1; break; }
            if (m->turn_slip) { m->u_turn = 0; m->turning = 0; }
            if (ang_abs(fs, m->sd) <= 0.707f) {
                V3 c2 = vcross(fs, m->sd);
                if (m->u_side ? vdot(c2, m->up) > 0 : vdot(c2, m->up) < 0) m->u_turn = 0;
            }
        } while (0);
        m->last_rx = m_rx; m->last_ry = m_ry;
        if (m->turning) {
            V3 c3 = vcross(fs, m->sd);
            if (vdot(c3, m->turn_cross3) < 0) {
                if (!(vdot(cross, m->turn_cross) > 0) && !(vdot(fs, m->sd) < 0)) m->weak_turn = 0;
            } else if (m->weak_turn < MC.mWeakTurnTime) m->weak_turn++;
            m->turn_cross3 = c3; m->turn_cross = cross; m->last_sd = m->sd;
            float f1 = 1.0f;
            if (m->weak_turn < MC.mWeakTurnTime && m->tsi > 4) f1 = (float)m->weak_turn / MC.mWeakTurnTime;
            ts *= sqrtf(f1);
        } else {
            if (m->weak_turn) m->weak_turn--;
            if (!m->weak_turn) { ts *= 0.01f; m->turn_cross = v3(0, 0, 0); m->turn_cross3 = vcross(fs, m->sd); }
            m->last_sd = m->sd;
        }
        V3 v2 = m->face;
        if (m->u_turn) v2 = rot_axis(fs, m->up, m->u_side ? 0.12f : -0.12f);
        else if (ang_abs(m->face, m->sd) < 0.1f) v2 = m->stick_ang0 == m->stick_ang ? m->sd : m->face;
        else {
            float d = ang_abs(m->face, m->sd);
            if (!blend_sphere(m->face, m->sd, &v2, clamp01(ts / d))) {
                m->weak_turn = 0;
                v2 = rot_toward(fs, m->sd, m->up, 22.5f * 3.14159265f / 180.0f);
            }
            if (d > 2.5f && m->weak_turn == MC.mWeakTurnTime && !m_draw_d) {
                m->u_side = vdot(vcross(fs, v2), m->up) > 0;
                m->u_turn = 1;
            }
        }
        m->turning = 1;
        if (!(m->stand_turn && m_clip_on("run")) && dot > 0.99f) m->turning = 0;
        if (!a1) m_front(m, v2);
    } else {
        if (!m->turn_slip) {
            V3 b;
            if (!blend_sphere(fs, m->sd, &b, ts)) b = rot_toward(fs, m->sd, m->up, 22.5f * 3.14159265f / 180.0f);
            if (!a1) m_front(m, b);
        }
        m->u_turn = 0; m->turning = 0;
    }
    if (m->tsi) m->stand_turn = 0; /* stopAnimation("その場足踏み") */
    V3 v = vmul(m->face, MC.mWalkSpeed * m->walk);
    m->move_dir = dir_set ? dir : m->move_dir;
    if (m->squat) { if (m->tsi && dir_set) m->move_dir_keep = dir; }
    else if (!m->turn_ready && dir_set) m->move_dir_keep = dir;
    if (m->wall && vdot(v, m->wall_n) < 0 && -vdot(m->face, m->wall_n) < cosf(MC.mWallPushAngleRange * 3.14159265f / 180.0f))
        v = vtangent(v, m->wall_n);
    float sp = vlen(v); /* along the ground: (_368 x v) x _368 at v's length */
    V3 c = vnorm(vcross(m->ground_n, vnorm(v)));
    m->vel = vmul(vcross(c, m->ground_n), sp);
}

/* the spin (MarioActor::updateSwingAction): in the air trySpinJump once a jump (then 空パンチ), on the ground
 * MarioMagic's 地上ひねり; mSpinIntervalTime + 34 frames before the next on the ground */
static void m_swing(Player *m)
{
    if (m->jumping) m->spin_cool = 0;
    if (m->spin_cool) m->spin_cool--;
    if (!pad_pressed(PAD_SQUARE) || m->spin_cool || m->walling) return;
    if (m->jumping && !m->spin_lock && !m->hip) {
        m_try_spin_jump(m);
        m->spin_timer = 25; m->spin_t = 0; /* setPunchHitTimer(0x19) */
    } else if (!m_clip_on("spinground")) {
        if (m->jumping) { /* trySpinPunch in the air: 空パンチ */
            if (m->hip) return;
            mario_play("spin2nd", 1.0f, 0);
            m->spin_timer = 15; m->spin_t = 0; m->walk = 0;
        } else if (!m->squat && m->slip_keep) { /* startMagic (ponytail: no サマーソルト from a squat) */
            m->magic = 1;
            mario_play("spinground", 1.0f, 0);
#ifdef BENCH
            printf("spin ground\n");
#endif
            m->spin_timer = 25; m->spin_t = 0;
        } else return;
    } else return;
    m->spin_cool = MC.mSpinIntervalTime + 0x22;
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
    if (pipe_bind(m)) {
        if (mario_anim) anim_update(mario_anim);
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
    if (vine_ride >= 0 || (warp.from < 0 && jb.inst < 0 && coc.inst < 0 && flf.inst < 0 && vine_try(m))) { /* Plant */
        if (vine_hold(m)) {
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
    /* Mario::update from inputStick on: actionMain (a status: MarioWall / MarioMagic; procJump; or mainMove and
     * updateWalkSpeed), the move against the map, writeBackPhyisicalVector, doLanding, then MarioAnimator::update */
    m_cy = vnorm(vtangent(vsub(m->pos, cam_pos), m->up));
    m_cx = vnorm(vcross(m_cy, m->up));
    m_input_stick(m);
#ifdef SLOPE_TEST
    { /* push into the 77 degree face slope_test() chose for 240 frames, then let go */
        static int t;
        extern V3 slope_n;
        V3 w = ++t < 240 ? vmul(vnorm(vtangent(vmul(slope_n, -1.0f), m->up)), 0.6f) : v3(0, 0, 0);
        m_sx = vdot(w, m_cx); m_sy = vdot(w, m_cy); m_sz = vlen(w); m_pad = vnorm(w);
    }
#endif
    m_req_jump = pad_pressed(PAD_CROSS);
    m->height = 0; /* mVerticalSpeed: the height over the ground, probed in 20s up to 160 */
    if (!m->grounded) {
        Hit h[1];
        m->height = 1000.0f;
        for (float d = 20.0f; d <= 160.0f; d += 20.0f)
            if (kcl_query(vsub(m->pos, vmul(m->up, d)), 20.0f, h, 1)) { m->height = d; break; }
    }
    m->vel = v3(0, 0, 0); m->vel_after = v3(0, 0, 0);
    if (!m->grounded && !m->jumping && !m->walling && m->vup != 0) { /* thrown by an object: a free jump */
        m->jumping = 1; m->digital = 1; m->keep_jump = 1; m->jump_from = m->pos; m->hip = 0;
    }
    m_retain(m);
    m_swing(m);
    if (m->spin_timer) m->spin_timer--;
    m->spin_t++;

    int slipping = m->grounded && !m->jumping && m->slip_keep == 0, done = 0;
    if (m->magic) { /* MarioMagic::update */
        if (m_req_jump) { m->magic = 0; m_try_jump(m); done = 1; }
        else if (!m_clip_on("spinground") || m->jumping || slipping) m->magic = 0;
        else { m_main_move(m); m_update_walk_speed(m); done = 1; }
    }
    if (done) ;
    else if (m->walling) m_wall_update(m);
    else if (m->jumping) { m_proc_jump(m, 0); m_check_wall_stick(m); }
    else if (slipping) { /* slide down the slope (hip slide), facing downhill; jumping still works */
        V3 downhill = vtangent(vmul(m->up, -1.0f), m->ground_n);
        float sn = vlen(downhill);
        m->hvel = vtangent(m->hvel, m->up);
        if (sn > 1e-3f) {
            downhill = vmul(downhill, 1.0f / sn);
            m->hvel = vadd(vmul(m->hvel, 0.98f), vmul(downhill, SLIP_ACCEL * sn));
            if (vlen(m->hvel) > SLIP_MAX) m->hvel = vmul(vnorm(m->hvel), SLIP_MAX);
            m->face = vnorm(vtangent(downhill, m->up));
        }
        m->vel = m->hvel; m->walk = 0; m->tsi = 0;
        if (m_req_jump) m_try_jump(m);
    } else { m_main_move(m); m_update_walk_speed(m); }
    /* MarioBee.cpp: A held after mBeeAirWalkInhibitTime (30) with flight left lifts him (mBeeFlyConstantFactor 5 x
     * mBeeAccelRatio 0.02 a frame up to mBeeUpSpeedMax 5, his own rise x mBeePushRiseGravityEraser 0.9); let go
     * he drops slowly (mBeeFreeDropAcc 0.1 to 5). ponytail: no wall walk, no honey walls, no bee animations */
    if (bee_mode && m->jumping && !m->hip) {
        if ((pad_held(PAD_CROSS) || test_cross) && m->air_timer > 30 && bee_meter > 0) {
            bee_meter--; bee_drop = 0;
            bee_rise += 5.0f * 0.02f;
            if (bee_rise > 5.0f) bee_rise = 5.0f;
            m->vup = m->vup > bee_rise ? m->vup * 0.9f : bee_rise;
        } else if (m->vup <= 0) {
            bee_rise = 0;
            if (bee_drop < 5.0f) bee_drop += 0.1f;
            m->vup = -bee_drop;
        }
    }
    if (slipping) slip_frames++;

    V3 move = vadd(m->vel, m->vel_after), center = vadd(m->pos, vmul(m->up, BODY_RADIUS));
    if (m->grounded && !m->jumping && !m->walling) move = vadd(move, vmul(m->up, -GROUNDING)); /* follow the floor */
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
    if (m->wall) m->wall_n = wn;
    m->pos = vsub(center, vmul(m->up, BODY_RADIUS));
    /* writeBackPhyisicalVector: a wall halves the jump's part into it; on the ground walking into one slows the walk */
    if (m->wall && m->jumping && !m->wall_free) {
        float into = vdot(m_jv(m), m->wall_n);
        if (into <= 0) m_set_jv(m, vsub(m_jv(m), vmul(m->wall_n, into * 0.5f)));
    } else if (m->wall && !m->jumping && vdot(m->vel, m->wall_n) < 0) {
        float slow = 1.0f - clamp01((-vdot(m->wall_n, m->face) - 0.3f) * 1.4285715f);
        m->tsi = 0;
        m->walk *= slow;
    }
    if (ground && (!m->jumping || m->vup <= 0)) {
        m->grounded = 1;
        m->ground_n = gn;
        if (m->jumping && m->hip == HIP_FALL) m_hip_land(m);
        else if (m->jumping && !m->hip) m_do_landing(m);
        if (!m->hip) { m->vup = 0; m->hvel = vtangent(m->vel, m->up); }
        if (vdot(gn, m->up) < SLIP_ANGLE_COS) { if (m->slip_keep) m->slip_keep--; }
        else m->slip_keep = SLIP_KEEP;
        bee_meter = 180; bee_rise = bee_drop = 0; /* landing refills the flight */
    } else if (!ground) m->grounded = 0;
    if (m->grounded && !m->jumping) m->land_timer++; /* updateTimers: _3CE */
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

    if (mario_anim) { /* MarioAnimator::update: one-shot clips play out, then the state's default */
        int busy = mario_anim->clip_id >= 0 && !mario_anim->loop && !mario_anim->done;
        if (m->hip || m->magic || m->walling) ; /* their own clips */
        else if (m->grounded && !m->jumping && m->slip_keep == 0) mario_play("slidehipforward", 1.0f, 1); /* 坂すべり下向きあおむけ */
        else if (m->grounded && !m->jumping) {
            if (m->squat) m_squat_anim(m);
            else {
                static const char *cancel[] = {"land", "jump2land", "jumprollland", "jumpturnland", "jumpbroadland",
                                               "jumpbackland", "landslope", "squatend"}; /* isCancelableAnimationRun */
                int can = 0;
                for (unsigned i = 0; i < sizeof cancel / sizeof cancel[0]; i++) can |= m_clip_is(cancel[i]);
                if (busy && can) { m_decide_walk_speed(m); if (m->tsi) { m_clip_stop(); busy = 0; } }
                if (m->stand_turn && m->tsi == 0 && m_enable_turn(m)) mario_play("run", 1.0f, 1); /* その場足踏み */
                else if (!busy) { m_decide_walk_speed(m); m_decide_walk_anim(m); }
                else { if (m->turning || m->turn_brake) m_decide_walk_speed(m); m_update_brake(m); }
            }
        } else if (!m->grounded && !busy && (m->jumping || !m->drop_grace)) mario_play("fall", 1.0f, 1); /* 落下 */
        anim_update(mario_anim);
    }
    if (m->stand_turn) m->stand_turn--;
    m->last_pos = m->pos;
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


/* ---- walker kit: Petari's LiveActor nerve stepping (Spine::update), Binder (a sphere 70 x scale over its feet),
 * ActorMovementUtil / MathUtil helpers and WalkerState* (Wander, FindPlayer, Chase, Stagger) for the walkers ported
 * literally. setNerve: step -1, so the next frame runs the new nerve at step 0 ---- */
static void w_nrv(Kuribo *k, int n) { k->nrv = n; k->step = -1; }
static void w_snrv(Kuribo *k, int n) { k->snrv = n; k->sstep = -1; }
static const char *walker_cls(const Kuribo *k) { return sc->types + 32 * sc->objs[k->row].type; }
static float w_scale(const Kuribo *k) { return sc->objs[k->row].scale[1]; }

/* MR::startAction: its clip from frame 0, looping as its BCK says (anim_start in the draw) */
static void w_act(Kuribo *k, const char *name)
{
    char n[48];
    k->act_interp = 0; k->act_rate = 1.0f;
    snprintf(k->act, sizeof k->act, "%s", name);
    snprintf(n, sizeof n, "%s/%s", walker_cls(k), name);
    for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
    k->act_frames = anim_clip_info(n, &k->act_mode);
    k->act_seq++; k->act_t = 0;
}
static int w_bck_stopped(const Kuribo *k) /* isBckStopped: a one-time clip at its end */
{
    return k->act_mode != 2 && k->act_mode != 4 && k->act_t >= k->act_frames;
}

/* turnVecToVecCos(OnPlane): at most acos(cosmax) towards `to` (made perpendicular to `axis`) */
static int turn_on_plane(V3 *dir, V3 to, V3 axis, float cosmax)
{
    V3 pt = vnorm(vtangent(to, axis));
    if (vlen(pt) < 1e-6f) return 0;
    if (cosmax <= -1.0f) { *dir = pt; return 1; }
    V3 from = *dir;
    if (vdot(from, pt) > cosmax) { *dir = pt; return 1; }
    V3 rej = vtangent(pt, vnorm(from));
    if (vlen(rej) < 1e-6f) { *dir = vnorm(vadd(from, vmul(vnorm(vcross(from, axis)), 0.0f))); return 0; }
    *dir = vnorm(vadd(vmul(vnorm(from), cosmax), vmul(vnorm(rej), sqrtf(1.0f - cosmax * cosmax))));
    return 0;
}
static V3 w_up_ref(const Kuribo *k) { return k->on_ground ? k->gn : vmul(k->grav, -1.0f); }
/* turnDirectionToTargetUseGroundNormalDegree */
static void w_turn_to(Kuribo *k, V3 target, float deg)
{
    turn_on_plane(&k->front, vsub(target, k->pos), k->on_ground ? k->gn : k->grav, cosf(deg * 0.0174533f));
}
static int w_on_ground(const Kuribo *k) { return k->on_ground && vdot(k->vel, k->gn) <= 0.0f; } /* isOnGround */
/* calcVelocityMoveToDirection + addVelocity */
static void w_add_move(Kuribo *k, V3 dir, float speed)
{
    V3 a = vmul(vnorm(vtangent(dir, k->grav)), speed);
    if (w_on_ground(k)) a = vtangent(a, k->gn);
    k->vel = vadd(k->vel, a);
}
/* isNearAngleDegreeHV of the player from its front */
static int w_sight_fan(const Kuribo *k, float dist, float degh, float degv)
{
    V3 to = vsub(mario->pos, k->pos);
    if (vlen(to) >= dist || vlen(to) < 1e-6f) return 0;
    V3 h = vnorm(vtangent(to, k->grav));
    if (vdot(h, vnorm(k->front)) < cosf(degh * 0.0174533f)) return 0;
    return fabsf(vdot(vnorm(to), k->grav)) <= fabsf(sinf((degv > 90.0f ? 90.0f : degv) * 0.0174533f));
}
/* ponytail: the KCL has no line test here; a line is stepped spheres (radius 15, every 25) */
static int line_hits(V3 a, V3 b)
{
    V3 d = vsub(b, a);
    int n = (int)(vlen(d) / 25.0f) + 1;
    for (int i = 0; i <= n; i++) {
        Hit h[1];
        if (kcl_query(vadd(a, vmul(d, (float)i / n)), 15.0f, h, 1)) return 1;
    }
    return 0;
}
/* MapUtil isFallNextMove: no ground under `fwd` ahead of it along its way */
static int w_fall_next(const Kuribo *k, float fwd, float upper, float lower)
{
    V3 f = vtangent(k->vel, k->grav);
    if (vlen(f) < 1e-6f) return 0;
    V3 s = vsub(vadd(k->pos, vmul(vnorm(f), fwd)), vmul(k->grav, upper));
    return !line_hits(s, vadd(s, vmul(k->grav, upper + lower)));
}
/* WalkerStateFunction::calcPassiveMovement (reboundVelocityFromEachCollision -1, -1, 0, 0: only a roof stops it) */
static void w_passive(Kuribo *k, float grav, float air, float ground)
{
    if (k->on_wall == 2 && vdot(k->vel, k->wn) < 0) k->vel = vsub(k->vel, vmul(k->wn, vdot(k->vel, k->wn)));
    if (!w_on_ground(k)) { k->vel = vadd(k->vel, vmul(k->grav, grav)); k->vel = vmul(k->vel, air); }
    else k->vel = vmul(k->vel, ground);
}
/* calcReboundVelocity(vel, normal, rate) */
static int rebound(V3 *v, V3 n, float rate)
{
    float d = vdot(*v, n);
    if (d >= 0) return 0;
    *v = vsub(*v, vmul(n, (rate + 1.0f) * d));
    return 1;
}
static int rebound_hv(V3 *v, V3 n, float rv, float rh)
{
    float d = vdot(*v, n);
    if (d >= 0) return 0;
    *v = vsub(vmul(vsub(*v, vmul(n, d)), rh), vmul(n, d * rv));
    return 1;
}
/* rotate v about a unit axis */
static V3 rot_axis(V3 v, V3 a, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    return vadd(vadd(vmul(v, c), vmul(vcross(a, v), s)), vmul(a, vdot(a, v) * (1.0f - c)));
}
/* blendQuatUpFront: its up turned rate_up of the way to `up`, then its front rate_front of the way to `front` */
static void w_blend_quat(Kuribo *k, V3 up, V3 front, float rate_up, float rate_front)
{
    float c = vdot(k->qy, up);
    V3 ax = vcross(k->qy, up);
    if (vlen(ax) > 1e-6f) {
        float ang = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * rate_up;
        ax = vnorm(ax);
        k->qy = vnorm(rot_axis(k->qy, ax, ang)); k->qz = vnorm(rot_axis(k->qz, ax, ang));
    }
    V3 tf = vnorm(vtangent(front, k->qy));
    if (vlen(tf) < 1e-6f) return;
    c = vdot(k->qz, tf);
    float ang = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * rate_front;
    if (vdot(vcross(k->qz, tf), k->qy) < 0) ang = -ang;
    k->qz = vnorm(vtangent(rot_axis(k->qz, k->qy, ang), k->qy));
}
/* Binder: the sphere (radius r over its feet) moved by its velocity, pushed out of the map; ground / wall / roof */
static void w_bind(Kuribo *k, float r)
{
    V3 up = vmul(k->grav, -1.0f);
    k->pos = vadd(k->pos, k->vel);
    if (k->bind_off) { k->on_ground = k->on_wall = 0; return; }
    V3 c = vadd(k->pos, vmul(up, r)), gn = up, wn = up;
    int wall = 0;
    k->on_ground = collide_n(&c, up, r, &gn, &wn, &wall);
    k->gn = k->on_ground ? gn : up;
    k->on_wall = wall ? 2 : 0; k->wn = wn;
    k->pos = vsub(c, vmul(up, r));
}
static int in_death(V3 p) /* MR::isInDeath: a death area holds it */
{
    for (int i = 0; i < nlogic; i++)
        if (logic[i].kind == LOGIC_DEATH && row_shown(logic[i].row) && area_inside(&sc->objs[logic[i].row], p)) return 1;
    return 0;
}

/* Mario's offence (MarioActorOffensiveMsg) against a sensor at c of radius r: 1 trample (falling onto it), 2 hip drop
 * (onto it), 3 hit all (spin, within mSensorRadiusAttack 120), 0 none */
enum { MSG_NONE, MSG_TRAMPLE, MSG_HIPDROP, MSG_HITALL };
/* MarioActor::cylinderPushCheck: Mario's cylinder (radius 20 + _4B0 35, height _4B4 60, 70 jumping) about his centre
 * overlaps the sensor */
static int mario_touch(V3 c, float r)
{
    V3 off = vsub(c, vadd(mario->pos, vmul(mario->up, HIT_BODY_H)));
    float axial = vdot(off, mario->up), radial = vlen(vtangent(off, mario->up));
    return axial > -r && r + 55.0f - radial > 0.0f && r + (mario->grounded ? 60.0f : 70.0f) - axial > 0.0f;
}
static int mario_offence(V3 c, float r, V3 grav)
{
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), rel = vsub(mc, c);
    float d = vlen(rel), fall = mario->grounded ? 0.0f : -mario->vup;
    int above = vdot(grav, vsub(mario->pos, c)) <= 0.0f, horiz = r + 35.0f - vlen(vtangent(rel, grav)) > 0.0f; /* isUnderTarget, cylinderHorizontalCheck */
    if (mario->hip == HIP_FALL && d < r + 80.0f + fall && above && horiz) return MSG_HIPDROP;   /* mSensorRadiusHipDropAttack 80 */
    if (d < r + 120.0f) {                                                                       /* mSensorRadiusAttack 120 */
        if (!mario->grounded && mario->vup < 0 && mario->hip != HIP_FALL && d < r + 80.0f + 1.5f * fall && above && horiz)
            return MSG_TRAMPLE;                                                                 /* mSensorRadiusTrampleAttack 80 */
        if (mario->spin_timer > 0 && mario_touch(c, r)) return MSG_HITALL;                     /* sendBodyAttack */
    }
    return MSG_NONE;
}

/* Plant.cpp, PlantStalk.cpp, PlantLeaf.cpp, PlantRailInfo.cpp, PlantPoint.cpp after Petari (the beanstalk; "vine"
 * here, FlowerGroup's members being Plant). Its rail is the stalk: PlantRailInfo points every 30 along it (the last at
 * its end), PlantStalk the same reversed (0 the tip, thickness easing out 0.45 to 1). The seed (PlantSeed, at the
 * rail's start on the map) waits (WaitFar) until Mario is within 700, buds (SeedWait: pushes him at 50); a spin on it
 * (300, 50 up) or its appear switch makes it grow: with arg2 a demo (60: Bud, 90: GrowUp) holding Mario, the stalk
 * growing at 15-100 (x 0.95, +15-30 every 5-30 frames) to the rail's length, PlantTop at its tip scaled by the growth,
 * the leaves ((L - 300) / 200 + 2 of them from L - 100 down, 100-300 apart, x 1-1.5, turned 90-270 degrees about Y in
 * turn) growing with it. Mario touching its bind sensor (100, at the nearest stalk point) in the air or spinning takes
 * hold (AutoRushBegin): HangStart (GrowPlantCatch, rising at his speed clamped 15-35 unless he fell faster than 2,
 * or held near the top), HangUp (gravity 0.3, a spin +1 at least 10, turning round it 1.3 x speed, at most 30),
 * HangDown (back down at most 20, a spin climbs again at 10), at the top a jump along the tip's up at arg0 (30)
 * (endBindAndPlayerForceWeakGravityJump), at the foot he lets go; then it ignores him 60 frames or until he lands.
 * Leaves near him spring (0.02 x his speed). Taken while growing (no demo) he rides it up (HangWaitGrowUp /
 * HangUpGrowUp). ponytail: no demo or catch cameras (multi-actor cameras), no rail clipping, no items taken by its
 * Ride sensor, no rush cancel; the stalk's three faces each one colour (the mean of their two vertex colours), drawn
 * through render_strips one segment a strip */
static float frand(float a, float b) { return a + (b - a) * (rand() % 10001) / 10000.0f; }

/* the rail (its Bezier polyline) by length: position, direction to its end */
static V3 vrail_pos(int k, float d) { return rail_at(vines[k].rail, vines[k].rail_n, d < 0 ? 0 : d); }
static V3 vrail_tan(int k, float d)
{
    const float *p = vines[k].rail;
    for (int i = 0; i + 1 < vines[k].rail_n; i++) {
        V3 a = v3(p[3 * i], p[3 * i + 1], p[3 * i + 2]), b = v3(p[3 * i + 3], p[3 * i + 4], p[3 * i + 5]);
        float l = vlen(vsub(b, a));
        if (d <= l || i + 2 == vines[k].rail_n) return l > 0 ? vmul(vsub(b, a), 1.0f / l) : v3(0, 1, 0);
        d -= l;
    }
    return v3(0, 1, 0);
}
static float vrail_nearest(int k, V3 q) /* moveCoordAndTransToNearestRailPos */
{
    const float *p = vines[k].rail;
    float best = 1e30f, at = 0, run = 0;
    for (int i = 0; i + 1 < vines[k].rail_n; i++) {
        V3 a = v3(p[3 * i], p[3 * i + 1], p[3 * i + 2]), ab = vsub(v3(p[3 * i + 3], p[3 * i + 4], p[3 * i + 5]), a);
        float l = vlen(ab), t = l > 0 ? vdot(vsub(q, a), ab) / (l * l) : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float dd = vlen(vsub(q, vadd(a, vmul(ab, t))));
        if (dd < best) { best = dd; at = run + t * l; }
        run += l;
    }
    return at;
}
static V3 vrail_dir(int k) { return vmul(vrail_tan(k, vines[k].coord), (float)vines[k].dir); } /* getRailDirection */
static void vrail_move(int k, float v) /* moveCoordAndFollowTrans */
{
    typeof(&vines[0]) o = &vines[k];
    o->coord += o->dir * v;
    o->coord = o->coord < 0 ? 0 : o->coord > o->len ? o->len : o->coord;
    o->pos = vrail_pos(k, o->coord);
}

static void vpoint(VPoint *p, V3 pos, V3 up, float thick) /* PlantPoint::PlantPoint */
{
    p->pos = pos; p->up = up; p->side = v3(1, 0, 0); p->front = v3(0, 0, 1); p->thick = thick;
    V3 f = vcross(p->side, up);
    if (vlen(f) < 1e-6f) { p->side = vnorm(vcross(up, p->front)); p->front = vcross(p->side, up); } /* makeAxisUpFront */
    else { p->front = vnorm(vcross(p->side, up)); p->side = vcross(up, p->front); }        /* makeAxisUpSide */
}

static void vinfo_at(int k, float coord, V3 *pos, V3 *up) /* PlantRailInfo::calcPosAndAxisY */
{
    typeof(&vines[0]) o = &vines[k];
    float nm = coord / o->len * o->n;
    int s = (int)nm, e = s + 1;
    if (s >= o->n) s = o->n - 1;
    if (e >= o->n) e = o->n - 1;
    float t = nm - s;
    *pos = vadd(vmul(o->info[s].pos, 1.0f - t), vmul(o->info[e].pos, t));
    *up = vnorm(vadd(vmul(o->info[s].up, 1.0f - t), vmul(o->info[e].up, t)));
}

static void vleaf_mtx(float m[16], V3 side, V3 up, V3 front, float s, V3 pos) /* setXYZDir, scale, setTrans */
{
    mat_identity(m);
    m[0] = side.x * s; m[1] = side.y * s; m[2] = side.z * s;
    m[4] = up.x * s; m[5] = up.y * s; m[6] = up.z * s;
    m[8] = front.x * s; m[9] = front.y * s; m[10] = front.z * s;
    m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
}

static int vine_of(int row)
{
    for (int i = 0; i < nvines; i++) if (vines[i].row == row) return i;
    if (nvines == MAX_VINES) return -1;
    const Row *r = &sc->objs[row];
    u32 np = 0;
    int closed = 0;
    const float *pts = r->path >= 0 ? find_rail(scene_blob_g, r->zone, r->path, &np, &closed) : NULL;
    if (!pts || np < 2) return -1;
    int k = nvines;
    typeof(&vines[0]) o = &vines[k];
    memset(o, 0, sizeof *o);
    o->row = row; o->rail = pts; o->rail_n = (int)np; o->dir = 1;
    for (u32 i = 0; i + 1 < np; i++) o->len += vlen(vsub(v3(pts[3 * i + 3], pts[3 * i + 4], pts[3 * i + 5]), v3(pts[3 * i], pts[3 * i + 1], pts[3 * i + 2])));
    o->jump = r->arg[0] != -1 ? (float)r->arg[0] : 30.0f; /* getJMapInfoArg0NoInit */
    o->jump_oy = r->arg[1] != -1 ? (float)r->arg[1] : 0.0f;
    o->demo = r->arg[2] != -1;
    o->grow_speed = frand(15.0f, 100.0f); o->grow_t = (int)frand(5.0f, 30.0f);
    o->pos = o->base = vrail_pos(k, 0); /* calcRailStartPointPos */
    o->up = v3(0, 1, 0); o->front = v3(0, 0, 1);
    /* PlantRailInfo (speed 30) */
    o->n = (int)(o->len / 30.0f) + 2;
    if (o->n > VINE_PTS) o->n = VINE_PTS; /* ponytail: rails over 7600 cut */
    for (int i = 0; i < o->n - 1; i++) vpoint(&o->info[i], vrail_pos(k, 30.0f * i), vrail_tan(k, 30.0f * i), 1.0f);
    vpoint(&o->info[o->n - 1], vrail_pos(k, o->len), o->info[o->n - 2].up, 1.0f);
    o->info[0].side = o->info[o->n - 2].side; o->info[0].up = o->info[o->n - 2].up; o->info[0].front = o->info[o->n - 2].front;
    /* PlantStalk */
    for (int i = 0; i < o->n; i++) {
        const VPoint *q = &o->info[o->n - 1 - i];
        vpoint(&o->stalk[i], q->pos, q->up, 0.45f + 0.55f * sinf((float)i / o->n * 1.5707963f));
    }
    o->stalk[0].pos = o->pos;
    /* initLeaf */
    o->nleaves = (int)((o->len - 100.0f - 200.0f) / (300.0f - 100.0f)) + 2;
    if (o->nleaves > VINE_LEAVES) o->nleaves = VINE_LEAVES;
    V3 base = v3(0, 0, 1);
    float lp = o->len - 100.0f;
    for (int i = 0; i < o->nleaves; i++) {
        float ratio = (float)i / o->nleaves;
        V3 rd = vrail_tan(k, lp), g = base;
        float d = vdot(g, rd);
        if (fabsf(d) > 0.7f) g = vnorm(d > 0 ? vcross(v3(0, 1, 0), rd) : vcross(rd, v3(0, 1, 0)));
        typeof(&o->leaf[0]) L = &o->leaf[i];
        L->coord = lp; L->size = 1.0f + 0.5f * sinf(ratio * 1.5707963f); L->front = g; L->pos = o->pos;
        L->side = vnorm(vcross(v3(0, 1, 0), g)); L->up = vcross(g, L->side); /* makeAxisFrontUp */
        vleaf_mtx(L->m, L->side, L->up, L->front, L->size * 0.0001f, L->pos);
        lp -= 100.0f + 200.0f * ratio;
        float a = frand(90.0f, 270.0f) * 0.0174533f;
        base = v3(base.x * cosf(a) + base.z * sinf(a), base.y, -base.x * sinf(a) + base.z * cosf(a)); /* about Y */
    }
    o->alive = r->sw[SW_APPEAR] < 0; /* useStageSwitchReadAppear: dead until it */
    o->nrv = VN_WAITFAR; o->step = -1;
    return nvines++;
}

static void vine_set(int k, int nrv) { vines[k].nrv = nrv; vines[k].step = -1; }
static void vine_seed(const Inst *in, const char *clip)
{
    char n[48];
    snprintf(n, sizeof n, "plant/%s", clip);
    if (in->anim) anim_start(in->anim, n, 0);
}
static void vine_start_grow(int k) { vine_set(k, vines[k].demo ? VN_WAITDEMO : VN_GROWUP); } /* startGrowUp */

static int vstalk_grow(int k) /* PlantStalk::updateGrowUp */
{
    typeof(&vines[0]) o = &vines[k];
    if (--o->grow_t <= 0) { o->grow_speed += frand(15.0f, 30.0f); o->grow_t = (int)frand(5.0f, 30.0f); }
    o->stalk_len += o->grow_speed;
    if (o->stalk_len <= 0) { o->stalk_len = 0; o->grow_speed = 0; }
    o->grow_speed *= 0.95f;
    o->grow_speed = o->grow_speed < 15.0f ? 15.0f : o->grow_speed > 100.0f ? 100.0f : o->grow_speed;
    o->growth = o->stalk_len / o->len;
    o->growth = o->growth < 0 ? 0 : o->growth > 1 ? 1 : o->growth;
    if (o->stalk_len >= o->len) { o->stalk_len = o->len; o->growth = 1.0f; o->ngrown = o->n; return 1; }
    o->ngrown = (int)(o->stalk_len / 30.0f);
    o->ngrown = o->ngrown < 2 ? 2 : o->ngrown > o->n ? o->n : o->ngrown;
    V3 pos, ay;
    vinfo_at(k, o->stalk_len, &pos, &ay);
    V3 fr = vnorm(vcross(v3(1, 0, 0), ay)), sd = vcross(ay, fr); /* makeAxisUpSide */
    o->stalk[0].side = sd; o->stalk[0].up = ay; o->stalk[0].front = fr; o->stalk[0].pos = pos;
    for (int i = 1; i < o->ngrown; i++) {
        const VPoint *q = &o->info[o->ngrown - 1 - i];
        o->stalk[i].side = q->side; o->stalk[i].up = q->up; o->stalk[i].front = q->front; o->stalk[i].pos = q->pos;
    }
    return 0;
}

static int vleaf_spring(int k, int i, float growth) /* PlantLeaf::updateSpring */
{
    typeof(&vines[0].leaf[0]) L = &vines[k].leaf[i];
    L->sspeed -= L->spring * 0.1f;
    L->sspeed = L->sspeed < -0.5f ? -0.5f : L->sspeed > 0.5f ? 0.5f : L->sspeed;
    L->spring += L->sspeed;
    L->spring = L->spring < -0.9f ? -0.9f : L->spring > 0.9f ? 0.9f : L->spring;
    L->sspeed *= 0.9f;
    if (fabsf(L->spring) < 0.01f && fabsf(L->sspeed) < 0.001f) {
        L->spring = 0; L->sspeed = 0;
        L->side = vnorm(vcross(L->up, L->front)); L->up = vcross(L->front, L->side);
        vleaf_mtx(L->m, L->side, L->up, L->front, L->size * growth, L->pos);
        return 1;
    }
    V3 s = L->front;
    s.y += L->spring;
    s = vnorm(s);
    V3 f = vnorm(vcross(L->up, s)), u = vcross(s, f);
    vleaf_mtx(L->m, f, u, s, L->size * growth, L->pos);
    return 0;
}

static void vine_bind_leaves(int k) /* updateBindLeaf */
{
    typeof(&vines[0]) o = &vines[k];
    float power = o->dir > 0 ? o->ride_vel : -o->ride_vel;
    V3 at = vadd(vmul(vrail_dir(k), o->dir > 0 ? -20.0f : 20.0f), o->pos);
    for (int i = 0; i < o->nleaves; i++) {
        if (vlen(vsub(o->leaf[i].pos, at)) < 50.0f) o->leaf[i].sspeed += power * 0.02f;
        vleaf_spring(k, i, o->growth);
    }
}

static int vine_grow(int k) /* Plant::updateGrowUp */
{
    typeof(&vines[0]) o = &vines[k];
    int done = vstalk_grow(k);
    for (int i = 0; i < o->nleaves; i++) { /* PlantLeaf::updateGrowUp(offset 10) */
        typeof(&o->leaf[0]) L = &o->leaf[i];
        V3 pos, ay;
        vinfo_at(k, L->coord * o->growth, &pos, &ay);
        L->pos = vadd(pos, vmul(L->front, 10.0f));
        float t = o->growth * o->growth;
        V3 up = v3(ay.x * (1 - t), ay.y * (1 - t) + t, ay.z * (1 - t));
        L->side = vnorm(vcross(up, L->front)); L->up = vcross(L->front, L->side);
        vleaf_mtx(L->m, L->side, L->up, L->front, L->size * o->growth, L->pos);
    }
    if (!done) return 0;
    if (o->rider) {
        o->coord = vrail_nearest(k, o->pos); o->pos = vrail_pos(k, o->coord);
        if (o->nrv == VN_HANGUPGROW) { o->dir = 1; vine_set(k, VN_HANGUP); }
        else { o->dir = -1; vine_set(k, VN_HANGDOWN); }
    } else vine_set(k, VN_GROWTHWAIT);
    printf("plant %d grown\n", o->row);
    return 1;
}

static int vine_demo_on(void) /* 伸び植物（成長）: from the demo's start to the stalk's end */
{
    for (int k = 0; k < nvines; k++)
        if (vines[k].alive && vines[k].demo && (vines[k].nrv == VN_WAITDEMO || vines[k].nrv == VN_DEMOWAIT || vines[k].nrv == VN_GROWUP))
            return 1;
    return 0;
}

static void vine_release(int k, V3 v, int top) /* endBindAndPlayerJump / ...ForceWeakGravityJump */
{
    vines[k].rider = 0; vine_ride = -1;
    if (mario_anim) mario_play("growplantjump", 1.0f, 0);
    if (top) m_power_jump(mario, v);
    else { mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0; mario->jump_kind = 0; mario->air_timer = 0; }
    vine_set(k, VN_GROWTHWAITINVALID);
    printf("plant %d: let go%s\n", vines[k].row, top ? " at the top" : "");
}

static int vine_hang_up(int k, float rate) /* updateHangUp */
{
    typeof(&vines[0]) o = &vines[k];
    o->ride_vel -= 0.3f;
    o->ride_vel = o->ride_vel < 0 ? 0 : o->ride_vel > 35.0f ? 35.0f : o->ride_vel;
    vine_bind_leaves(k);
    if (o->ride_vel <= 0) { o->dir = -1; vine_set(k, VN_HANGDOWN); return 1; }
    vrail_move(k, o->ride_vel);
    o->up = vrail_dir(k);
    o->front = rot_axis(o->front, o->up, rate * 0.0174533f);
    return 0;
}

static void vine_accel(int k, int swing) /* tryAccelHangUp */
{
    typeof(&vines[0]) o = &vines[k];
    if (o->accel_t > 0 || !swing) return;
    o->accel_t = 0;
    o->ride_vel += 1.0f;
    if (o->ride_vel < 10.0f) o->ride_vel = 10.0f;
}

/* AutoRushBegin: Mario's touch on its bind sensor */
static int vine_try(Player *m)
{
    if (m->launch >= 0) return 0;
    V3 c = vadd(m->pos, vmul(m->up, HIT_BODY_H));
    for (int k = 0; k < nvines; k++) {
        typeof(&vines[0]) o = &vines[k];
        int n = o->nrv;
        if (!o->alive || n == VN_WAITFAR || n == VN_SEEDWAIT || n == VN_GROWTHWAITINVALID) continue;
        if ((m->spin_timer <= 0 && m->grounded) || (n == VN_GROWUP && m->grounded)) continue;
        if (o->demo && (n == VN_GROWUP || n == VN_WAITDEMO || n == VN_DEMOWAIT)) continue;
        int best = 0;
        float bd = 1e30f;
        for (int i = 3; i < o->n - 3; i++) { /* calcNearestPointPos(3) */
            float d = vlen(vsub(o->info[i].pos, m->pos));
            if (d < bd) { bd = d; best = i; }
        }
        if (vlen(vsub(o->info[best].pos, c)) > 100.0f + HIT_BODY_R) continue;
        o->rider = 1; vine_ride = k;
        o->coord = vrail_nearest(k, m->pos); o->pos = vrail_pos(k, o->coord);
        o->front = m->face;
        o->ride_vel = m->vup;
        int cl = 0; /* PlantStalk::calcPlayerCoord */
        bd = 1e30f;
        for (int i = 0; i < o->ngrown; i++) { float d = vlen(vsub(o->stalk[i].pos, c)); if (d < bd) { bd = d; cl = i; } }
        if (o->stalk_len > 15.0f) {
            o->ride_coord = (o->ngrown - cl) * 30.0f / o->stalk_len * o->len;
            o->ride_coord = o->ride_coord < 0 ? 0 : o->ride_coord > o->len ? o->len : o->ride_coord;
        } else o->ride_coord = o->len;
        if (n == VN_GROWUP) { o->ride_vel = 0; vine_set(k, VN_HANGWAITGROW); }
        else if (o->coord >= o->len - 150.0f) {
            o->ride_vel = 0; o->ride_coord = o->len - 150.0f; o->dir = -1; o->grabbed_top = 1; vine_set(k, VN_HANGSTART);
        } else if (o->ride_vel >= -2.0f) {
            o->ride_vel = o->ride_vel < 15.0f ? 15.0f : o->ride_vel > 35.0f ? 35.0f : o->ride_vel;
            o->dir = 1; vine_set(k, VN_HANGSTART);
        } else { o->ride_vel = 0; o->dir = -1; vine_set(k, VN_HANGSTART); }
        m->hip = 0; m->spin_timer = 0; m->hvel = v3(0, 0, 0); m->vup = 0;
        printf("plant %d: taken at %d of %d\n", o->row, (int)o->coord, (int)o->len);
        return 1;
    }
    return 0;
}

/* while Mario hangs: its base matrix (isMsgUpdateBaseMtx) */
static int vine_hold(Player *m)
{
    if (vine_ride < 0) return 0;
    typeof(&vines[0]) o = &vines[vine_ride];
    V3 f = vtangent(o->front, o->up);
    m->pos = o->pos; m->up = o->up; m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    if (vlen(f) > 1e-3f) m->face = vnorm(f);
    return 1;
}

static void update_vines(void)
{
    int swing = pad_pressed(PAD_SQUARE) || vine_test_sq; /* isPadSwing */
    vine_test_sq = 0;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->vine) continue;
        int k = vine_of(in->row);
        if (k < 0) continue;
        typeof(&vines[0]) o = &vines[k];
        const Row *r = &sc->objs[in->row];
        if (in->md && in->md->skin) in->anim = obj_anim(in->row, in->md, "Plant");
        if (!o->seeded && in->kcl_ready) { /* initAfterPlacement: makeMtxOnMapCollision(1000) */
            V3 up = up_at(o->base, v3(0, 1, 0)), hit;
            o->seeded = 1;
            if (drop_to_ground(o->base, up, 1000.0f, &hit)) basis_matrix(o->seed_m, hit, up, v3(0, 0, 1), 0);
            else basis_matrix(o->seed_m, o->base, up, v3(0, 0, 1), 0);
        }
        if (o->seeded) memcpy(in->m, o->seed_m, 64);
        if (!o->alive) {
            if (sw_on(r, SW_APPEAR)) { /* appear */
                o->alive = 1; o->top_on = 1;
                vine_start_grow(k);
                printf("plant %d appears\n", in->row);
            } else continue;
        }
        int st = ++o->step;
        switch (o->nrv) {
        case VN_WAITFAR:
            if (!st) vine_seed(in, "wait");
            if (vlen(vsub(mario->pos, o->pos)) < 700.0f) vine_set(k, VN_SEEDWAIT);
            break;
        case VN_SEEDWAIT: {
            if (!st) { vine_seed(in, "bud"); o->top_on = 0; }
            V3 seed = vadd(o->pos, vmul(up_at(o->pos, mario->up), 50.0f));
            float d = vlen(vsub(c, seed));
            if (d < 50.0f + HIT_BODY_R && d > 1e-3f) /* attackSensor: sendMsgPush at 50 */
                mario->pos = vadd(mario->pos, vmul(vtangent(vsub(c, seed), mario->up), (50.0f + HIT_BODY_R - d) / d));
            if (mario_offence(seed, 300.0f, vmul(up_at(o->pos, mario->up), -1.0f)) == MSG_HITALL) { /* a spin */
                o->top_on = 1;
                vine_start_grow(k);
                printf("plant %d spun: grows\n", in->row);
            }
            break;
        }
        case VN_WAITDEMO: /* tryStartDemoWithoutCinemaFrame */
            vine_set(k, VN_DEMOWAIT);
            break;
        case VN_DEMOWAIT:
            if (st == 60) vine_seed(in, "bud");
            if (st == 90) vine_set(k, VN_GROWUP);
            break;
        case VN_GROWUP:
            if (!st) vine_seed(in, "growup");
            vine_grow(k);
            break;
        case VN_HANGWAITGROW:
            if (!st && mario_anim) mario_play("growplantcatch", 1.0f, 0);
            if (vine_grow(k)) break;
            vinfo_at(k, o->ride_coord * o->growth, &o->pos, &o->up);
            if (swing && o->ride_coord < o->len) vine_set(k, VN_HANGUPGROW);
            break;
        case VN_HANGUPGROW: {
            if (!st) { if (mario_anim) mario_play("growplanthangup", 1.0f, 0); o->accel_t = 0; o->ride_vel = 10.0f; }
            if (vine_grow(k)) break;
            o->accel_t--;
            vine_accel(k, swing);
            o->ride_vel -= 0.3f;
            if (o->ride_vel <= 0) {
                o->ride_vel = 0;
                if (mario_anim) mario_play("growplanthangdown", 1.0f, 0);
                vine_set(k, VN_HANGWAITGROW);
                break;
            }
            o->ride_coord += o->ride_vel;
            if (o->ride_coord > o->len) o->ride_coord = o->len;
            vinfo_at(k, o->ride_coord * o->growth, &o->pos, &o->up);
            float rs = 1.3f * o->ride_vel;
            o->front = rot_axis(o->front, o->up, (rs >= 30.0f ? 30.0f : rs) * 0.0174533f);
            vine_bind_leaves(k);
            if (o->ride_coord >= o->len) { o->ride_coord = o->len; o->ride_vel = 0; vine_set(k, VN_HANGWAITGROW); }
            break;
        }
        case VN_GROWTHSTOP:
            break;
        case VN_GROWTHWAIT: {
            int rest = 0;
            for (int j = 0; j < o->nleaves; j++) rest += vleaf_spring(k, j, 1.0f);
            if (rest == o->nleaves) vine_set(k, VN_GROWTHSTOP);
            break;
        }
        case VN_GROWTHWAITINVALID:
            for (int j = 0; j < o->nleaves; j++) vleaf_spring(k, j, 1.0f);
            if (st == 60 || mario->grounded) vine_set(k, VN_GROWTHWAIT);
            break;
        case VN_HANGSTART:
            if (!st && mario_anim) mario_play("growplantcatch", 1.0f, 0);
            if (!vine_hang_up(k, 15.0f) && (!mario_anim || mario_anim->done)) vine_set(k, VN_HANGUP);
            break;
        case VN_HANGUP: {
            if (!st) { if (mario_anim) mario_play("growplanthangup", 1.0f, 0); o->dir = 1; o->accel_t = 0; }
            float rs = o->ride_vel * 1.3f;
            if (vine_hang_up(k, rs >= 30.0f ? 30.0f : rs)) break;
            if (o->dir > 0 && o->coord >= o->len) { /* tryReachGoal */
                o->coord = o->len; o->pos = vrail_pos(k, o->len);
                mario->pos = o->pos;
                const VPoint *tip = &o->stalk[0];
                V3 g = vmul(up_at(o->pos, tip->up), -1.0f), ef = vtangent(tip->up, g);
                if (vlen(ef) > 1e-3f) mario->face = vnorm(ef);
                vine_release(k, vadd(vmul(tip->up, o->jump), vmul(g, -o->jump_oy)), 1);
                break;
            }
            o->accel_t--;
            vine_accel(k, swing);
            break;
        }
        case VN_HANGDOWN: {
            if (!st) { if (mario_anim) mario_play("growplanthangdown", 1.0f, 0); o->dir = -1; }
            if (!swing) o->grabbed_top = 0; /* tryHangUp */
            else if (!o->grabbed_top) { o->dir = -o->dir; o->ride_vel = 10.0f; vine_set(k, VN_HANGUP); break; }
            o->ride_vel += 0.3f;
            o->ride_vel = o->ride_vel < 0 ? 0 : o->ride_vel > 20.0f ? 20.0f : o->ride_vel;
            vine_bind_leaves(k);
            if (o->dir < 0 ? o->coord <= 10.0f : o->coord >= o->len - 10.0f) { vine_release(k, v3(0, 0, 0), 0); break; }
            vrail_move(k, o->ride_vel);
            V3 rd = vrail_dir(k);
            o->up = vmul(rd, -1.0f);
            float rs = o->ride_vel * 1.3f;
            o->front = rot_axis(o->front, rd, (rs >= 20.0f ? 20.0f : rs) * 0.0174533f);
            break;
        }
        }
    }
}

static void draw_vines(void)
{
    static Strip st[48];
    int tex = fx_plantstalk_tex(), ns = 0;
    /* sColorPlusZ ff, sColorPlusX 64, sColorMinusX 96: each face's two, averaged (GS 0x80 = 1) */
    static const u32 face_rgba[3] = {0x80595959u, 0x80656565u, 0x803F3F3Fu};
    for (int k = 0; k < nvines; k++) {
        typeof(&vines[0]) o = &vines[k];
        if (vlen(vsub(o->pos, cam_pos)) > 12000.0f) continue;
        for (int i = 0; tex >= 0 && i + 1 < o->ngrown; i++) { /* drawGrowUp: three faces, one segment a strip */
            V3 e[2][3];
            for (int j = 0; j < 2; j++) {
                const VPoint *p = &o->stalk[i + j];
                V3 sd = vmul(p->side, p->thick * 10.0f), fr = vmul(p->front, p->thick * 10.0f);
                e[j][0] = vadd(p->pos, fr);              /* +Z */
                e[j][1] = vsub(vadd(p->pos, sd), fr);    /* +X */
                e[j][2] = vsub(vsub(p->pos, sd), fr);    /* -X */
            }
            static const int fa[3][2] = {{0, 1}, {2, 0}, {1, 2}};
            for (int f = 0; f < 3; f++) {
                Strip *s = &st[ns++];
                for (int l = 0; l < 4; l++) { /* level 0 (v 1) at point i + 1, level 3 (v 0) at point i */
                    float t = 1.0f - l / 3.0f;
                    for (int side = 0; side < 2; side++) {
                        V3 q = vadd(vmul(e[0][fa[f][side]], 1.0f - t), vmul(e[1][fa[f][side]], t));
                        s->p[l][side][0] = q.x; s->p[l][side][1] = q.y; s->p[l][side][2] = q.z;
                    }
                }
                s->rgba = face_rgba[f];
                s->tex = (u16)tex;
                if (ns == 48) { render_strips(st, ns); ns = 0; }
            }
        }
        if (!o->alive || o->nrv == VN_WAITFAR || o->nrv == VN_SEEDWAIT) continue;
        if (m_vleaf) for (int i = 0; i < o->nleaves; i++) render_model(m_vleaf, o->leaf[i].m, 0x80, 0x80, 0x80);
        if (m_vtop && o->top_on && o->ngrown > 0) { /* mTopMtx at the tip, mScale the growth */
            const VPoint *p = &o->stalk[0];
            float m[16];
            vleaf_mtx(m, p->side, p->up, p->front, o->growth, p->pos);
            render_model(m_vtop, m, 0x80, 0x80, 0x80);
        }
    }
    if (ns) render_strips(st, ns);
}

/* HammerHeadPackun.cpp after Petari (PackunFlower, its leaf PackunLeaf a PartsModel at Spin1 turned (0, -90, -90),
 * 100 along it with arg0 > 0): Wait until Mario (his up along its own) is within 800, Search turning its front 1
 * degree a frame to him (on its plane) until he is within 30 degrees of it, Attack (178: turning to him the first 60
 * frames), Rest (Chance, 90), Recover (Recovery, 185), Wait. Its body sensor (Head + (120, 0, 0), 130) hurts Mario
 * strongly while isAttackSensorValid (in Attack: Hit, then Crow 150), its stem sensors (blow at Spin1 120, push1-4
 * at Spin2 / Spin3 45-60) hurt him; else they push him out. On its body while isChance (Rest, Damage, Recover <= 50,
 * Rise <= 30): a hip drop (Mario sent off) or a trample, SmashKill (Down 36); a spin, Damage (SwoonStart 92, Swoon,
 * Rise at 180: SwoonEnd 185) or Rise after 3 (mImmunityTime, reset in Rest). A PunchingKinoko on its blow sensor, a
 * jet turtle, a fire ball or a blow from an enemy: BlowKill (BlowDown 60, stopScene 8). kill: its dead switch, 9
 * star pieces from its Head (10, 40). A star piece is reflected. ponytail: no 2P pointer (Numb), no JointRumbler,
 * no camera shake / rumble / sound, no hit or PointerTouchManual effects, no shadow; Mario's spin hits it once a
 * spin (his sensor messages are not per frame here) */

static int hhp_of(int row)
{
    for (int i = 0; i < nhhps; i++) if (hhps[i].row == row) return i;
    if (nhhps == MAX_HHPS) return -1;
    typeof(&hhps[0]) o = &hhps[nhhps];
    memset(o, 0, sizeof *o);
    const Row *r = &sc->objs[row];
    float m[16], one[3] = {1, 1, 1};
    mat_translate_rot_scale(m, r->pos, r->rot, one); /* initPosture: makeMtxTR, its Y and Z */
    o->row = row; o->pos = row_pos(r); o->up = vnorm(v3(m[4], m[5], m[6])); o->f = vnorm(v3(m[8], m[9], m[10]));
    o->nrv = HH_WAIT; o->step = -1;
    return nhhps++;
}

static void hhp_set(int k, int nrv) { hhps[k].nrv = nrv; hhps[k].step = -1; }

static const char *const hhp_clips[] = {"wait", "attack", "chance", "recovery", "swoonstart", "swoon", "swoonend",
                                        "crow", "down", "blowdown"};
static const short hhp_lens[] = {121, 178, 48, 185, 92, 65, 185, 150, 36, 60};
static void hhp_bck(const Inst *in, int k, int c) /* startBck on it and its leaf */
{
    char n[48];
    hhps[k].ct = 0; hhps[k].clen = hhp_lens[c];
    if (in->anim) { snprintf(n, sizeof n, "hammerheadpackun/%s", hhp_clips[c]); anim_start(in->anim, n, 0); }
    Anim *la = m_pleaf && m_pleaf->skin ? obj_anim((int)sc->nobjs + in->row, m_pleaf, "PackunLeaf") : NULL;
    if (la) { snprintf(n, sizeof n, "packunleaf/%s", hhp_clips[c]); anim_start(la, n, 0); }
}
#define HHP_STOPPED(o) ((o)->ct >= (o)->clen)

static V3 hhp_joint(const Inst *in, int k, int j, float x) /* joint j's world place + (x, 0, 0) on it */
{
    if (in->anim && in->anim->skin && j < in->anim->skin->njoints) {
        float jm[16];
        mat_mul(jm, in->m, in->anim->world[j]);
        return v3(jm[12] + jm[0] * x, jm[13] + jm[1] * x, jm[14] + jm[2] * x);
    }
    return vadd(hhps[k].pos, vmul(hhps[k].up, 115.0f * (j - 1) + x));
}

static int hhp_in_range(int k) /* isTargetInRange */
{
    return vdot(mario->up, hhps[k].up) > 0.0f && vlen(vsub(mario->pos, hhps[k].pos)) < 800.0f;
}

static int hhp_player_dir(int k, V3 *d) /* calcPlayerDir */
{
    V3 v = vtangent(vsub(mario->pos, hhps[k].pos), hhps[k].up);
    if (vlen(v) < 1e-3f) return 0;
    *d = vnorm(v);
    return 1;
}

static void hhp_turn_to(int k, V3 d, float deg) /* turnTo: at most deg, then verticalizeFrontVec */
{
    typeof(&hhps[0]) o = &hhps[k];
    float c = vdot(o->f, d), a = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c), mx = deg * 3.14159265f / 180.0f;
    V3 ax = vcross(o->f, d);
    ax = vlen(ax) > 1e-6f ? vnorm(ax) : o->up;
    o->f = rot_axis(o->f, ax, a > mx ? mx : a);
    o->f = vnorm(vtangent(o->f, o->up));
}

static int hhp_attack_valid(int k) /* isAttackSensorValid */
{
    int n = hhps[k].nrv, s = hhps[k].step;
    return !(n == HH_ATTACK && s < 0) && n != HH_REST && !(n == HH_RECOVER && s <= 40) && n != HH_DAMAGE &&
           !(n == HH_RISE && s < 60) && n != HH_SMASHKILL && n != HH_BLOWKILL;
}

static int hhp_chance(int k) /* isChance */
{
    int n = hhps[k].nrv, s = hhps[k].step;
    return (n == HH_REST && s >= 0) || n == HH_DAMAGE || (n == HH_RECOVER && s <= 50) || (n == HH_RISE && s <= 30);
}

/* its six sensors: body, blow, push1-4 (initSensor) */
static void hhp_sensors(const Inst *in, int k, V3 p[6], float r[6])
{
    p[0] = hhp_joint(in, k, 5, 120.0f); r[0] = 130.0f;
    p[1] = hhp_joint(in, k, 2, 0.0f);   r[1] = 120.0f;
    p[2] = hhp_joint(in, k, 3, 0.0f);   r[2] = 45.0f;
    p[3] = hhp_joint(in, k, 3, 50.0f);  r[3] = 45.0f;
    p[4] = hhp_joint(in, k, 4, 0.0f);   r[4] = 45.0f;
    p[5] = hhp_joint(in, k, 4, 80.0f);  r[5] = 60.0f;
}

/* receiveMsgEnemyAttack (isMsgToEnemyAttackBlow) / receiveMsgPlayerAttack (jet turtle, fire ball): something of
 * radius rad at p on any of its sensors blows it down; 1 if one took it */
static int hhp_blow(V3 p, float rad)
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->hhp || row_dead[in->row]) continue;
        int k = hhp_of(in->row);
        if (k < 0 || hhps[k].nrv == HH_SMASHKILL || hhps[k].nrv == HH_BLOWKILL) continue;
        V3 sp[6];
        float sr[6];
        hhp_sensors(in, k, sp, sr);
        for (int s = 0; s < 6; s++)
            if (vlen(vsub(p, sp[s])) < sr[s] + rad) {
                scene_stop = 8;
                hhp_set(k, HH_BLOWKILL);
                printf("hammer head packun %d blown\n", in->row);
                return 1;
            }
    }
    return 0;
}

static int hhp_reflects(V3 p, float rad) /* isMsgStarPieceReflect on any of its sensors */
{
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->hhp || row_dead[in->row]) continue;
        int k = hhp_of(in->row);
        if (k < 0) continue;
        V3 sp[6];
        float sr[6];
        hhp_sensors(in, k, sp, sr);
        for (int s = 0; s < 6; s++) if (vlen(vsub(p, sp[s])) < sr[s] + rad) return 1;
    }
    return 0;
}

static void update_hhps(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->hhp || row_dead[in->row]) continue;
        int k = hhp_of(in->row);
        if (k < 0) continue;
        typeof(&hhps[0]) o = &hhps[k];
        if (in->md && in->md->skin) in->anim = obj_anim(in->row, in->md, "HammerHeadPackun");
        Anim *la = m_pleaf && m_pleaf->skin ? obj_anim((int)sc->nobjs + in->row, m_pleaf, "PackunLeaf") : NULL;
        int st = ++o->step;
        o->ct++;
        V3 d;
        switch (o->nrv) {
        case HH_WAIT:
            if (!st) hhp_bck(in, k, 0);
            if (hhp_in_range(k)) hhp_set(k, HH_SEARCH);
            break;
        case HH_SEARCH:
            if (hhp_player_dir(k, &d)) {
                hhp_turn_to(k, d, 1.0f);
                if (vdot(d, o->f) >= cosf(30.0f * 3.14159265f / 180.0f)) hhp_set(k, HH_ATTACK); /* isInFieldOfView */
            }
            break;
        case HH_ATTACK:
            if (!st) hhp_bck(in, k, 1);
            if (st <= 60 && hhp_player_dir(k, &d)) hhp_turn_to(k, d, 1.0f); /* turnToAttackVec(60): never past 60 */
            if (HHP_STOPPED(o)) hhp_set(k, HH_REST);
            break;
        case HH_REST:
            if (!st) { hhp_bck(in, k, 2); o->imm = 0; }
            if (st == 90) hhp_set(k, HH_RECOVER);
            break;
        case HH_RECOVER:
            if (!st) hhp_bck(in, k, 3);
            if (HHP_STOPPED(o)) hhp_set(k, HH_WAIT);
            break;
        case HH_DAMAGE:
            if (!st) { hhp_bck(in, k, 4); o->imm++; }
            if (o->clen == hhp_lens[4] && HHP_STOPPED(o)) hhp_bck(in, k, 5); /* isBckOneTimeAndStopped: Swoon */
            if (st == 180) hhp_set(k, HH_RISE);
            break;
        case HH_RISE:
            if (!st) hhp_bck(in, k, 6);
            if (HHP_STOPPED(o)) {
                if (hhp_in_range(k)) { hhp_bck(in, k, 0); hhp_set(k, HH_SEARCH); }
                else hhp_set(k, HH_WAIT);
            }
            break;
        case HH_HIT:
            if (HHP_STOPPED(o)) hhp_set(k, HH_CROW); /* the Attack clip runs on */
            break;
        case HH_CROW:
            if (!st) hhp_bck(in, k, 7);
            if (HHP_STOPPED(o)) hhp_set(k, HH_WAIT);
            break;
        case HH_SMASHKILL: case HH_BLOWKILL:
            if (!st) { hhp_bck(in, k, o->nrv == HH_SMASHKILL ? 8 : 9); printf("hammer head packun %d down\n", in->row); }
            if (HHP_STOPPED(o)) { /* kill */
                launch_pieces(hhp_joint(in, k, 5, 0.0f), o->up, 9, 10.0f, 40.0f);
                row_kill(in->row);
                game_rebind();
                return;
            }
            break;
        }
        if (la) anim_update(la);
        basis_matrix(in->m, o->pos, o->up, o->f, 0); /* calcAndSetBaseMtx */
        const float *s3 = sc->objs[in->row].scale;
        for (int j = 0; j < 3; j++) { in->m[j] *= s3[0]; in->m[4 + j] *= s3[1]; in->m[8 + j] *= s3[2]; }
        V3 sp[6];
        float sr[6];
        hhp_sensors(in, k, sp, sr);
        int killing = o->nrv == HH_SMASHKILL || o->nrv == HH_BLOWKILL;
        for (int j = 0; j < ninsts && !killing; j++) /* attackSensor: its blow sensor on a PunchingKinoko */
            if (insts[j].punch && !row_dead[insts[j].row] && vlen(vsub(insts[j].head, sp[1])) < sr[1] + 70.0f) {
                scene_stop = 8;
                hhp_set(k, HH_BLOWKILL);
                printf("punching kinoko %d blows hammer head packun %d\n", insts[j].row, in->row);
                killing = 1;
            }
        if (mario->spin_timer <= 0) o->spun = 0;
        /* Mario's attacks on its body (receiveMsgPlayerAttack) */
        int msg = mario_offence(sp[0], sr[0], vmul(o->up, -1.0f));
        if (killing) {
            if (msg == MSG_TRAMPLE) { mario->vup = 15.0f; mario->grounded = 0; }
        } else if (hhp_chance(k) && msg == MSG_HIPDROP) { /* sendMsgAwayJump */
            hhp_set(k, HH_SMASHKILL);
            mario->hip = 0; mario->vup = 20.0f; mario->grounded = 0;
            mario->hvel = vmul(vnorm(vtangent(vsub(mario->pos, sp[0]), mario->up)), 5.0f);
            continue;
        } else if (hhp_chance(k) && msg == MSG_TRAMPLE) {
            hhp_set(k, HH_SMASHKILL);
            mario->vup = 15.0f; mario->grounded = 0;
            continue;
        } else if (hhp_chance(k) && msg == MSG_HITALL && !o->spun) {
            o->spun = 1;
            hhp_set(k, o->imm >= 3 ? HH_RISE : HH_DAMAGE);
            printf("hammer head packun %d spun (%d)\n", in->row, o->imm);
            continue;
        }
        /* its sensors on Mario (attackSensor) */
        for (int s = 0; s < 6; s++) {
            float dist = vlen(vsub(c, sp[s]));
            if (dist >= sr[s] + HIT_BODY_R) continue;
            if (s && !mario->hurt_timer) hurt_mario(sp[s], 15.0f); /* sendMsgEnemyAttack */
            else if (!s && !killing && hhp_attack_valid(k) && !mario->hurt_timer) { /* sendMsgEnemyAttackStrong */
                hurt_mario(sp[0], 25.0f);
                if (o->nrv == HH_ATTACK && o->step >= 0) hhp_set(k, HH_HIT);
            } else { /* sendMsgPush */
                V3 out = vtangent(vsub(c, sp[s]), mario->up);
                float l = vlen(out);
                if (l > 1e-3f) mario->pos = vadd(mario->pos, vmul(out, (sr[s] + HIT_BODY_R - dist) / l));
            }
        }
    }
}

static void draw_hhp_leaves(void) /* PartsModel PackunLeaf: Spin1 x makeMtxTR((100 with arg0 > 0, 0, 0), (0, -90, -90)) */
{
    if (!m_pleaf || !m_pleaf->skin) return;
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->hhp || row_dead[in->row] || !in->anim || !in->anim->skin || in->anim->skin->njoints < 3) continue;
        Anim *la = obj_anim((int)sc->nobjs + in->row, m_pleaf, "PackunLeaf");
        if (!la) continue;
        float jm[16], fx[16], m[16], one[3] = {1, 1, 1}, rot[3] = {0, -90.0f, -90.0f};
        float tr[3] = {sc->objs[in->row].arg[0] > 0 ? 100.0f : 0.0f, 0, 0};
        mat_mul(jm, in->m, in->anim->world[2]);
        mat_translate_rot_scale(fx, tr, rot, one);
        mat_mul(m, jm, fx);
        render_skinned(m_pleaf, m, (const float (*)[16])la->drw);
    }
}

/* ---- WalkerStateWander (TerritoryMover 500): Wait mWaitTime, Walk at most mWalkTime to a random point ---- */
enum { WW_WAIT, WW_WALK };
static void ws_wander(Kuribo *k, float grav, float air, float ground, int wait, int walk, float speed, float turn)
{
    if (k->sstep == 0 && k->snrv == WW_WAIT) w_act(k, "Wait");
    if (k->sstep == 0 && k->snrv == WW_WALK) w_act(k, "Walk");
    if (k->snrv == WW_WAIT) {
        w_passive(k, grav, air, ground);
        if (k->sstep > wait) { /* decideNextTargetPos: a random way, 500 off the centre */
            V3 r = v3((rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f);
            k->ttarget = vadd(k->tcenter, vmul(vnorm(vtangent(vnorm(r), k->grav)), 500.0f));
            w_snrv(k, WW_WALK);
        }
    } else {
        w_turn_to(k, k->ttarget, turn);
        V3 a = vnorm(vtangent(vsub(k->ttarget, k->pos), k->grav)), b = vnorm(vtangent(k->front, k->grav));
        if (vdot(a, b) >= cosf(8.0f * 0.0174533f)) w_add_move(k, k->front, speed); /* isFaceToTargetHorizontalDegree 8 */
        w_passive(k, grav, air, ground);
        if (w_fall_next(k, 150.0f, 150.0f, 150.0f)) { k->vel = v3(0, 0, 0); w_snrv(k, WW_WAIT); }
        else if (k->sstep > walk || vlen(vtangent(vsub(k->ttarget, k->pos), k->grav)) < 20.0f) w_snrv(k, WW_WAIT);
    }
    k->sstep++;
}
/* ---- WalkerStateFindPlayer: Find (Turn, mJumpStartStep 30), JumpStart, Jump (mJumpVelocity), Land ---- */
enum { WF_FIND, WF_JUMPSTART, WF_JUMP, WF_JUMPEND };
static void ws_find(Kuribo *k, float grav, float air, float ground, float jumpv, float turn)
{
    int s = k->sstep;
    switch (k->snrv) {
    case WF_FIND:
        if (s == 0) w_act(k, "Turn");
        w_passive(k, grav, air, ground);
        w_turn_to(k, mario->pos, turn);
        if (s == 30) w_snrv(k, WF_JUMPSTART);
        break;
    case WF_JUMPSTART:
        if (s == 0) w_act(k, "JumpStart");
        w_passive(k, grav, air, ground);
        if (w_bck_stopped(k)) w_snrv(k, WF_JUMP);
        break;
    case WF_JUMP:
        if (s == 0) { k->vel = vadd(k->vel, vmul(k->grav, -jumpv)); w_act(k, "Jump"); }
        w_passive(k, grav, air, ground);
        if (k->on_ground && s > 5) w_snrv(k, WF_JUMPEND);
        break;
    case WF_JUMPEND:
        if (s == 0) w_act(k, "Land");
        w_passive(k, grav, air, ground);
        if (w_bck_stopped(k)) k->sdead = 1;
        break;
    }
    k->sstep++;
}
/* ---- WalkerStateChase: Start (Run at mChaseSpeed, turning mTurnMaxRateDegree 2 while in sight; out after
 * mForceChaseEndTime 300, or mChaseTime 130 out of sight), End (Wait mChaseEndWaitTime 30) ---- */
enum { WC_START, WC_END };
static void ws_chase(Kuribo *k, float grav, float air, float ground, float speed, float near, float fanh, float fanv)
{
    int s = k->sstep;
    if (k->snrv == WC_START) {
        if (s == 0) w_act(k, "Run");
        int sight = w_sight_fan(k, near, fanh, fanv);
        if (sight) w_turn_to(k, mario->pos, 2.0f);
        w_add_move(k, k->front, speed);
        w_passive(k, grav, air, ground);
        if (w_fall_next(k, 150.0f, 150.0f, 150.0f)) { k->vel = v3(0, 0, 0); w_snrv(k, WC_END); }
        else if (s > 300 || (s > 130 && !sight)) w_snrv(k, WC_END);
    } else {
        if (s == 0) w_act(k, "Wait");
        w_passive(k, grav, air, ground);
        if (s > 30) k->sdead = 1;
    }
    k->sstep++;
}
/* ---- WalkerStateStagger (default params): punched away (mPunchPowerH / V 10, 10 clockwise), StaggerStart then
 * Stagger, turning 40 degrees a frame eased out between 60 and 120, weaving; StaggerEnd after mStaggerTime 180 ---- */
enum { WS_STAGGER, WS_STAGGEREND };
static void ws_stagger_punch(Kuribo *k, V3 from)
{
    V3 d = vtangent(vsub(k->pos, from), k->grav);
    if (vlen(d) > 1e-6f) k->velh = vnorm(d);
    k->vel = vsub(vmul(k->velh, 10.0f), vmul(k->grav, 10.0f)); /* setVelocitySeparateHV */
    V3 cw = vcross(k->grav, vnorm(vsub(from, k->pos)));        /* addVelocityClockwiseToTarget */
    if (vlen(cw) > 1e-6f) w_add_move(k, cw, 10.0f);
}
static float ease_in_out(int step, int a, int b, float v0, float v1) /* calcNerveEaseInOutValue */
{
    float t = step <= a ? 0.0f : step >= b ? 1.0f : (float)(step - a) / (b - a);
    t = (1.0f - cosf(t * 3.14159265f)) * 0.5f;
    return v0 + (v1 - v0) * t;
}
static void ws_stagger(Kuribo *k, float grav, float air, float ground)
{
    int s = k->sstep;
    if (k->snrv == WS_STAGGER) {
        if (s == 0) { w_act(k, "StaggerStart"); scene_stop = 4; } /* MR::stopScene(mStopSceneFrames) */
        if (!strcmp(k->act, "StaggerStart") && w_bck_stopped(k)) w_act(k, "Stagger");
        V3 d = vtangent(k->velh, k->grav);
        if (vlen(d) > 1e-6f) k->velh = vnorm(d);
        float t = ease_in_out(s, 60, 120, 1.0f, 0.0f);
        k->front = vnorm(rot_axis(k->front, k->grav, 40.0f * t * 0.0174533f));
        float sw = sinf(fmodf(3.0f * s, 360.0f) * 0.0174533f);
        w_add_move(k, vcross(k->grav, k->velh), t * 0.5f * sw);
        w_add_move(k, k->velh, 0.35f * t);
        w_passive(k, grav, air, ground);
        if (k->on_wall == 2 && rebound(&k->vel, k->wn, 0.9f)) { /* reboundWall */
            V3 h = vtangent(k->vel, k->grav);
            if (vlen(h) > 1e-6f) k->velh = vnorm(h);
        }
        if (s > 180) w_snrv(k, WS_STAGGEREND);
    } else {
        if (s == 0) w_act(k, "StaggerEnd");
        w_passive(k, grav, air, ground);
        if (w_bck_stopped(k)) k->sdead = 1;
    }
    k->sstep++;
}
/* updateActorState: the state appears on the host nerve's first step; returns 1 once it is dead */
static int w_state(Kuribo *k, void (*appear)(Kuribo *))
{
    if (k->step == 0) { k->sdead = 0; appear(k); }
    return k->sdead;
}
static void ap_wander(Kuribo *k) { w_snrv(k, WW_WAIT); k->sstep = 0; }
static void ap_find(Kuribo *k) { w_snrv(k, WF_FIND); k->sstep = 0; }
static void ap_chase(Kuribo *k) { w_snrv(k, WC_START); k->sstep = 0; }
static void ap_stagger(Kuribo *k) { w_snrv(k, WS_STAGGER); k->sstep = 0; }

/* The walker kit's kill: DeathSmokeKuribo, its switch, its item (ItemGenerator: 1 a coin, 2 three star pieces) */
static void w_kill(Kuribo *k)
{
    k->state = KURIBO_DEAD; row_kill(k->row);
    fx_at(k->kind == WALK_KMINI ? FX_KURIBOMINI_DEATH : FX_KURIBO_DEATHSMOKEKURIBO, k->pos, vmul(k->grav, -1.0f));
    if (k->item == 1) pop_coins(k->pos, vmul(k->grav, -1.0f), 1);
    else if (k->item == 2) drop_pieces(k->pos, vmul(k->grav, -1.0f), 3);
}

/* Kuribo.cpp after Petari: KuriboParam (gravity 1.5, air 0.99, ground 0.93, sight 1000 / 70 / 30 degrees, wander 0.2
 * 120 / 120 turning 3, chase 0.4, find turning 5 jumping 20) and its nerves: Wander until Mario is in its sight fan
 * (FindPlayer: Turn, JumpStart, Jump, Land), Chase (Run), back to Wander (its centre where it stopped); NonActive
 * beyond 3000 on the ground; its attack sensor (45 x scale) hurts Mario in Wander / FindPlayer / Chase unless he is
 * hip-dropping (AttackSuccess: Hit, turning to him, 60); a trample: FlatDown (20, a coin), a hip drop: HipDropDown (40,
 * a coin), a spin: Stagger, or a kick once 15 into it: BlowDown (36 / 30 away, 30, three star pieces); arg1 0 starts
 * it AppearFromBox (7 / 30 along its front, Land 15 after the ground). Dead in a death area or water, no item.
 * ponytail: no star pointer bind (BindStarPointer / AnimScaleController), no push between walkers, no shadow, no
 * velocity areas / rail-moving ground, no sounds */
enum { KN_NONACTIVE, KN_APPEARFROMBOX, KN_BLOW, KN_BLOWLAND, KN_WANDER, KN_FINDPLAYER, KN_CHASE, KN_ATTACKSUCCESS,
       KN_STAGGER, KN_FLATDOWN, KN_HIPDROPDOWN, KN_PRESSDOWN, KN_BLOWDOWN };
#define KB_G 1.5f, 0.99f, 0.93f
static int kn_in(const Kuribo *k, int mask) { return (mask >> k->nrv) & 1; }
#define KNB(n) (1 << (n))
static void kuribo_init(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 x, z;
    row_axes(r, &x, &z);
    k->grav = vmul(up_at(k->pos, v3(0, 1, 0)), -1.0f);
    k->qy = vmul(k->grav, -1.0f);
    k->qz = vnorm(vtangent(z, k->qy));
    if (vlen(k->qz) < 1e-3f) k->qz = vnorm(vtangent(x, k->qy));
    k->front = k->qz;                                /* makeQuatAndFrontFromRotate */
    k->tcenter = k->pos;                             /* TerritoryMover's centre */
    k->item = 2;                                     /* makeActorAppeared: setTypeStarPeace(3) */
    if (r->arg[1] == 0) { w_nrv(k, KN_APPEARFROMBOX); k->step = 0; }
    else { w_nrv(k, KN_WANDER); k->step = 0; }
}
static void kuribo_update(Kuribo *k)
{
    float s = w_scale(k);
    if (!k->front.x && !k->front.y && !k->front.z) kuribo_init(k);
    k->grav = vmul(up_at(k->pos, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    int st = k->step;
    switch (k->nrv) {
    case KN_WANDER:
        w_state(k, ap_wander);
        ws_wander(k, KB_G, 120, 120, 0.2f, 3.0f);
        if (w_sight_fan(k, 1000.0f, 70.0f, 30.0f)) w_nrv(k, KN_FINDPLAYER); /* tryFind */
        else if (vlen(vsub(mario->pos, k->pos)) >= 3000.0f && k->on_ground) { k->vel = v3(0, 0, 0); w_nrv(k, KN_NONACTIVE); }
        break;
    case KN_FINDPLAYER:
        w_state(k, ap_find);
        ws_find(k, KB_G, 20.0f, 5.0f);
        if (k->sdead) w_nrv(k, KN_CHASE);
        break;
    case KN_CHASE:
        w_state(k, ap_chase);
        ws_chase(k, KB_G, 0.4f, 1000.0f, 70.0f, 30.0f);
        if (k->sdead) { w_nrv(k, KN_WANDER); k->tcenter = k->pos; } /* setWanderCenter */
        break;
    case KN_STAGGER:
        if (st == 0 && k->snrv != WS_STAGGER) ap_stagger(k);
        ws_stagger(k, KB_G);
        if (k->sdead) w_nrv(k, KN_WANDER);
        break;
    case KN_NONACTIVE:
        if (st == 0) k->bind_off = 1;
        k->vel = v3(0, 0, 0);
        if (vlen(vsub(mario->pos, k->pos)) < 3000.0f) { k->bind_off = 0; w_nrv(k, KN_WANDER); } /* endNonActive */
        break;
    case KN_APPEARFROMBOX: /* then exeBlow */
        if (st == 0) { w_act(k, "AppearFromBox"); k->vel = vsub(vmul(vnorm(vtangent(k->qz, k->grav)), 7.0f), vmul(k->grav, 30.0f)); k->bind_off = 1; }
        /* fall through */
    case KN_BLOW:
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f)); /* calcPassiveMovement */
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (k->on_wall == 2) rebound_hv(&k->vel, k->wn, 0.2f, 0.7f);
        if (st == 10) k->bind_off = 0;
        if (k->on_ground && st > 10) { w_act(k, "Land"); k->bind_off = 0; w_nrv(k, KN_BLOWLAND); } /* tryEndBlow */
        break;
    case KN_BLOWLAND:
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f));
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (st > 15) w_nrv(k, KN_CHASE);
        break;
    case KN_ATTACKSUCCESS:
        if (st == 0) w_act(k, "Hit");
        w_turn_to(k, mario->pos, 5.0f);
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f));
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (k->on_wall == 2) rebound_hv(&k->vel, k->wn, 0.2f, 0.7f);
        if (st > 60) w_nrv(k, KN_WANDER);
        break;
    case KN_HIPDROPDOWN: case KN_FLATDOWN: case KN_PRESSDOWN:
        if (st == 0) k->vel = v3(0, 0, 0);
        if (st > (k->nrv == KN_FLATDOWN ? 20 : k->nrv == KN_HIPDROPDOWN ? 40 : 180)) { w_kill(k); return; }
        break;
    case KN_BLOWDOWN:
        if (st == 0) w_act(k, "Blow");
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f));
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (k->on_wall == 2) rebound_hv(&k->vel, k->wn, 0.2f, 0.7f);
        turn_on_plane(&k->front, vmul(k->vel, -1.0f), k->grav, cosf(30.0f * 0.0174533f));
        if (st > 30) { w_kill(k); return; }
        break;
    }
    k->step++;
    k->act_t += k->act_rate;
    /* control(): blendQuatFromGroundAndFront 0.05 / 0.5; tryDead */
    w_blend_quat(k, w_up_ref(k), k->front, 0.05f, 0.5f);
    float depth;
    if (kn_in(k, KNB(KN_WANDER) | KNB(KN_FINDPLAYER) | KNB(KN_CHASE) | KNB(KN_STAGGER) | KNB(KN_NONACTIVE) | KNB(KN_ATTACKSUCCESS)) &&
        (in_death(k->pos) || in_water(k->pos, vmul(k->grav, -1.0f), &depth))) { k->item = 0; w_kill(k); return; }
    w_bind(k, 70.0f * s);
    k->up = k->qy;
    /* sensors: Mario's offence on its body (75 x scale at 75 up), its attack (45 x scale) on him */
    V3 c = vsub(k->pos, vmul(k->grav, 75.0f * s));
    int enable_stamp = !kn_in(k, KNB(KN_NONACTIVE) | KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN) | KNB(KN_BLOW));
    int enable_punch = !kn_in(k, KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN) | KNB(KN_BLOW));
    int dying = kn_in(k, KNB(KN_NONACTIVE) | KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN));
    int msg = mario_offence(c, 75.0f * s, k->grav);
    int kick = k->nrv == KN_STAGGER && !(k->snrv == WS_STAGGER && k->sstep - 1 < 15); /* isEnableKick */
    int upside = k->nrv == KN_STAGGER && !(k->snrv == WS_STAGGEREND && k->sstep - 1 > 15); /* isUpsideDown */
    if (msg == MSG_TRAMPLE && enable_stamp && !dying) {
        w_act(k, upside ? "FlatDownReverse" : "FlatDown"); w_nrv(k, KN_FLATDOWN); k->bind_off = 1; k->item = 1;
        mario->vup = 15.0f; mario->grounded = 0; /* doTrampleJump */
    } else if (msg == MSG_HIPDROP && !dying) {
        w_act(k, upside ? "HipDropDownReverse" : "HipDropDown"); w_nrv(k, KN_HIPDROPDOWN); k->bind_off = 1; k->item = 1;
    } else if (msg == MSG_HITALL && enable_punch) {
        if (!kick) { if (k->nrv != KN_STAGGER || k->snrv != WS_STAGGER || k->sstep > 1) { ws_stagger_punch(k, mario->pos); w_nrv(k, KN_STAGGER); ap_stagger(k); } }
        else { /* requestBlowDown: setVelocityBlowAttack 36 / 30 */
            V3 h = vnorm(vtangent(vsub(k->pos, mario->pos), k->grav));
            k->vel = vsub(vmul(h, 36.0f), vmul(k->grav, 30.0f));
            if (vlen(vsub(mario->pos, k->pos)) < 1000.0f) scene_stop = 4; /* stopSceneForDefaultHit */
            w_nrv(k, KN_BLOWDOWN); k->item = 2;
        }
    } else if (kn_in(k, KNB(KN_WANDER) | KNB(KN_FINDPLAYER) | KNB(KN_CHASE)) && mario->hip != HIP_FALL && !mario->hurt_timer &&
               vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), c)) < 45.0f * s + HIT_BODY_R) { /* attackSensor */
        hurt_mario(c, 15.0f);
        w_nrv(k, KN_ATTACKSUCCESS);
    }
}

/* KuriboMini.cpp after Petari: KuriboMiniParam (as Kuribo's, wander 0.1, chase 0.2), binder 60, body 60 / attack 40
 * at 60 up (not scaled); Wander / FindPlayer / Chase as Kuribo's, no NonActive; a trample: FlatDown (20), a hip drop:
 * HipDropDown (FlatDown clip, 40), a spin: BlowDown (22 / 25 away, BlowDown clip, 30), each a coin; a kick while it
 * staggers: BlowDown, three star pieces; its effect Death; SW_B makes it vanish. ponytail: as Kuribo's */
static void kmini_update(Kuribo *k)
{
    if (!k->front.x && !k->front.y && !k->front.z) { kuribo_init(k); w_nrv(k, KN_WANDER); k->step = 0; }
    if (sw_on(&sc->objs[k->row], SW_B)) { k->state = KURIBO_DEAD; return; } /* listenStageSwitchOnB: makeActorDead */
    k->grav = vmul(up_at(k->pos, vmul(k->grav, -1.0f)), -1.0f);
    int st = k->step;
    float depth;
    int deadmap = in_death(k->pos) || in_water(k->pos, vmul(k->grav, -1.0f), &depth); /* tryDeadMap */
    switch (k->nrv) {
    case KN_WANDER:
        w_state(k, ap_wander);
        ws_wander(k, KB_G, 120, 120, 0.1f, 3.0f);
        if (w_sight_fan(k, 1000.0f, 70.0f, 30.0f)) w_nrv(k, KN_FINDPLAYER);
        else if (deadmap) { k->item = 0; w_kill(k); return; }
        break;
    case KN_FINDPLAYER:
        w_state(k, ap_find);
        ws_find(k, KB_G, 20.0f, 5.0f);
        if (k->sdead) w_nrv(k, KN_CHASE);
        else if (deadmap) { k->item = 0; w_kill(k); return; }
        break;
    case KN_CHASE:
        w_state(k, ap_chase);
        ws_chase(k, KB_G, 0.2f, 1000.0f, 70.0f, 30.0f);
        if (k->sdead) { w_nrv(k, KN_WANDER); k->tcenter = k->pos; }
        if (deadmap) { k->item = 0; w_kill(k); return; }
        break;
    case KN_STAGGER:
        if (st == 0 && k->snrv != WS_STAGGER) ap_stagger(k);
        ws_stagger(k, KB_G);
        if (k->sdead) w_nrv(k, KN_WANDER);
        else if (deadmap) { k->item = 0; w_kill(k); return; }
        break;
    case KN_ATTACKSUCCESS:
        if (st == 0) w_act(k, "Hit");
        turn_on_plane(&k->front, vsub(mario->pos, k->pos), k->grav, cosf(5.0f * 0.0174533f)); /* turnDirectionToPlayerDegree */
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f));
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (k->on_wall == 2) rebound_hv(&k->vel, k->wn, 0.2f, 0.7f);
        if (deadmap) { k->item = 0; w_kill(k); return; }
        if (st > 60) w_nrv(k, KN_WANDER);
        break;
    case KN_HIPDROPDOWN: case KN_FLATDOWN: case KN_PRESSDOWN:
        if (st == 0) k->vel = v3(0, 0, 0);
        if (st > (k->nrv == KN_FLATDOWN ? 20 : k->nrv == KN_HIPDROPDOWN ? 40 : 180)) { w_kill(k); return; }
        break;
    case KN_BLOWDOWN:
        if (st == 0) w_act(k, "BlowDown");
        if (!w_on_ground(k)) k->vel = vadd(k->vel, vmul(k->grav, 1.5f));
        k->vel = vmul(k->vel, w_on_ground(k) ? 0.93f : 0.99f);
        if (k->on_wall == 2) rebound_hv(&k->vel, k->wn, 0.2f, 0.7f);
        turn_on_plane(&k->front, vmul(k->vel, -1.0f), k->grav, cosf(30.0f * 0.0174533f));
        if (st > 30) { w_kill(k); return; }
        break;
    default: w_nrv(k, KN_WANDER); break;
    }
    k->step++;
    k->act_t += k->act_rate;
    w_blend_quat(k, w_up_ref(k), k->front, 0.05f, 0.5f);
    w_bind(k, 60.0f);
    k->up = k->qy;
    V3 c = vsub(k->pos, vmul(k->grav, 60.0f));
    int down = kn_in(k, KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN)); /* isDown */
    if (down) return;
    int msg = mario_offence(c, 60.0f, k->grav);
    if (msg == MSG_TRAMPLE) {
        w_act(k, "FlatDown"); w_nrv(k, KN_FLATDOWN); k->bind_off = 1; k->item = 1;
        mario->vup = 15.0f; mario->grounded = 0;
    } else if (msg == MSG_HIPDROP) {
        w_act(k, "FlatDown"); w_nrv(k, KN_HIPDROPDOWN); k->bind_off = 1; k->item = 1;
    } else if (msg == MSG_HITALL) { /* requestBlowDown 22 / 25 (a kick while staggering: three star pieces) */
        V3 h = vnorm(vtangent(vsub(k->pos, mario->pos), k->grav));
        k->item = k->nrv == KN_STAGGER ? 2 : 1;
        k->vel = vsub(vmul(h, 22.0f), vmul(k->grav, 25.0f));
        if (vlen(vsub(mario->pos, k->pos)) < 1000.0f) scene_stop = 4;
        w_nrv(k, KN_BLOWDOWN);
    } else if (kn_in(k, KNB(KN_WANDER) | KNB(KN_FINDPLAYER) | KNB(KN_CHASE)) && mario->hip != HIP_FALL && !mario->hurt_timer &&
               vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), c)) < 40.0f + HIT_BODY_R) {
        hurt_mario(c, 15.0f);
        w_nrv(k, KN_ATTACKSUCCESS);
    }
}

/* the world position of joint j of a ported walker's model (its pose from the draw's last frame), at local offset off */
static Anim *obj_anim(int row, const Model *md, const char *cls);
static V3 w_joint(Kuribo *k, const Model *md, int j, V3 off)
{
    V3 p = k->pos;
    Anim *an = md && md->skin ? obj_anim(k->row, md, walker_cls(k)) : NULL;
    if (!an || an->skin != md->skin || j >= an->skin->njoints) return vadd(p, vmul(k->qy, 70.0f));
    const float *w = an->world[j], *sc3 = sc->objs[k->row].scale;
    V3 l = v3(w[0] * off.x + w[4] * off.y + w[8] * off.z + w[12], w[1] * off.x + w[5] * off.y + w[9] * off.z + w[13],
              w[2] * off.x + w[6] * off.y + w[10] * off.z + w[14]);
    V3 x = vcross(k->qy, k->qz);
    return vadd(p, vadd(vadd(vmul(x, l.x * sc3[0]), vmul(k->qy, l.y * sc3[1])), vmul(k->qz, l.z * sc3[2])));
}

/* SpinHitController (start / execute): blown off at h from the hit's side and v up, gravity g a frame; its items
 * (star pieces, or coins) at step `item`; killed once it binds after step 1, or past `max` */
static void spin_hit_start(Kuribo *k, V3 from, float h, float v)
{
    V3 d = vnorm(vtangent(vsub(k->pos, from), k->grav));
    k->vel = vsub(vmul(d, h), vmul(k->grav, v));
}
static int spin_hit_exec(Kuribo *k, int max, int item, int stop, float g, int nitems, int coins, int stop_scene)
{
    if (k->step == 0 && stop_scene) scene_stop = stop;
    k->vel = vadd(k->vel, vmul(k->grav, g));
    V3 up = vmul(k->grav, -1.0f);
    if (k->step == item && k->item) { if (coins) pop_coins(k->pos, up, nitems); else drop_pieces(k->pos, up, nitems); k->item = 0; }
    if (((k->on_ground || k->on_wall) && k->step > 1) || k->step > max) {
        if (k->step < item && k->item) { if (coins) pop_coins(k->pos, up, nitems); else drop_pieces(k->pos, up, nitems); k->item = 0; }
        return 1;
    }
    return 0;
}

/* Takobo.cpp after Petari: patrols its X axis (arg1 0 / 1, else Z; 1 and 3 start backwards) between -arg3 and arg3
 * (default 500) about its place at arg2 (default 5): each leg eased in-out over 1.3 x its length / speed (Move, no
 * binder); within 800 of Mario it turns to him 0.05 rad a frame, within 250 once facing him (0.1) it Attacks (its fist,
 * the attack sensor at joint Hear 80 x scale, hurts strongly at steps 85-90), Wait 60. Its body (70 x scale) hurts Mario
 * touching it in Move / Attack unless he is on top (HitReaction) or hip-dropping; a trample or hip drop: Press (30, a
 * coin); a spin: HitPunch (SpinHitController 22 / 21 / 3, 1.5, 20 / 35, three star pieces); a star piece: StunStart
 * (rate 2), Stun 120, StunEnd, and a spin while stunned blows it too. Its effects TakoboDeath, Hit. ponytail: no star
 * pointer (DpdPointed), no freeze (Ice), no fire floor bounce (arg0), no anim scale */
enum { TN_WAIT, TN_MOVE, TN_PRESS, TN_ATTACK, TN_HITPUNCH, TN_STUNSTART, TN_STUN, TN_STUNEND, TN_HITREACTION };
static void takobo_update(Kuribo *k, V3 to, float d)
{
    (void)to;
    const Row *r = &sc->objs[k->row];
    float s = r->scale[1];
    if (!k->front.x && !k->front.y && !k->front.z) { /* init */
        V3 x, z;
        row_axes(r, &x, &z);
        k->grav = vmul(up_at(k->pos, v3(0, 1, 0)), -1.0f);
        k->qy = vmul(k->grav, -1.0f); k->qz = k->front = vnorm(vtangent(z, k->qy));
        k->tcenter = k->pos;                                                    /* _A4 */
        k->velh = r->arg[1] == 0 || r->arg[1] == 1 ? vnorm(x) : vnorm(z);       /* _B0 */
        k->dir = r->arg[1] == 1 || r->arg[1] == 3;                             /* _BC */
        k->bind_off = 1;                                                        /* initAfterPlacement: offBind */
        k->item = 1;
        w_nrv(k, TN_WAIT); k->step = 0;
    }
    float speed = r->arg[2] != -1 ? (float)r->arg[2] : 5.0f, half = r->arg[3] != -1 ? (float)r->arg[3] : 500.0f;
    int st = k->step;
    switch (k->nrv) {
    case TN_WAIT:
        if (st == 0) { w_act(k, "Wait"); k->vel = v3(0, 0, 0); }
        if (st > 60) { k->vel = v3(0, 0, 0); w_nrv(k, TN_MOVE); }
        break;
    case TN_MOVE: {
        if (st == 0) { /* _C8 .. _CC, _D0 frames */
            float v3_ = vdot(vsub(k->pos, vsub(k->tcenter, vmul(k->velh, half))), k->velh);
            if (!k->dir) { k->to = half; k->from = -half + v3_; k->len = (half - (-half + v3_)) / speed; }
            else { k->from = -half; k->to = -half + v3_; k->len = ((-half + v3_) - k->from) / speed; }
            k->len *= 1.3f;
        }
        float rate = k->len > 0 ? (st < k->len ? st / k->len : 1.0f) : 1.0f; /* calcNerveRate */
        if (k->dir) rate = 1.0f - rate;
        float e = k->from + (k->to - k->from) * (0.5f - 0.5f * cosf(rate * 3.14159265f)); /* getEaseInOutValue */
        k->vel = vsub(vadd(k->tcenter, vmul(k->velh, e)), k->pos);
        if (st > k->len) { k->dir = !k->dir; k->vel = v3(0, 0, 0); w_nrv(k, TN_MOVE); }
        else {
            V3 t = vsub(mario->pos, k->pos);
            float mag = vlen(t);
            t = vnorm(vtangent(t, k->grav));
            if (mag < 800.0f && vlen(t) > 1e-6f) { /* turnVecToVecRadian 0.05 */
                float c = vdot(k->front, t), left = acosf(c > 1 ? 1 : c < -1 ? -1 : c);
                float turn = left < 0.05f ? left : 0.05f;
                V3 ax = vcross(k->front, t);
                if (vlen(ax) > 1e-6f) k->front = vnorm(rot_axis(k->front, vnorm(ax), turn));
                if (mag < 250.0f && left - turn < 0.1f) { k->vel = v3(0, 0, 0); w_nrv(k, TN_ATTACK); }
            }
        }
        break;
    }
    case TN_ATTACK:
        if (st == 0) w_act(k, "Attack");
        k->vel = v3(0, 0, 0);
        if (st >= 85 && st < 90) { /* the attack sensor: sendMsgEnemyAttackStrong */
            V3 fist = w_joint(k, m_takobo, 7, v3(10.0f, 2.0f, 0.0f));
            if (!mario->hurt_timer && vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), fist)) < 80.0f * s + HIT_BODY_R)
                hurt_mario(fist, 25.0f);
        }
        if (w_bck_stopped(k)) w_nrv(k, TN_WAIT);
        break;
    case TN_PRESS:
        if (st == 0) w_act(k, "Press");
        if (st > 30) { pop_coins(vsub(k->pos, vmul(k->grav, 80.0f)), vmul(k->grav, -1.0f), 1); /* generateCoin */
                       k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_TAKOBO_TAKOBODEATH, k->pos, k->qy); return; }
        break;
    case TN_HITPUNCH:
        if (st == 0) { w_act(k, "HitPunch"); k->bind_off = 0; }
        if (spin_hit_exec(k, 22, 21, 3, 1.5f, 3, 0, k->timer)) {
            k->state = KURIBO_DEAD; row_kill(k->row); fx_at(FX_TAKOBO_TAKOBODEATH, k->pos, k->qy); return;
        }
        break;
    case TN_STUNSTART:
        if (st == 0) { w_act(k, "StunStart"); k->act_rate = 2.0f; fx_at(FX_TAKOBO_HIT, vsub(k->pos, vmul(k->grav, 70.0f * s)), k->qy); k->vel = v3(0, 0, 0); }
        if (w_bck_stopped(k)) w_nrv(k, TN_STUN);
        break;
    case TN_STUN:
        if (st == 0) w_act(k, "Stun");
        if (st > 120) w_nrv(k, TN_STUNEND);
        break;
    case TN_STUNEND: case TN_HITREACTION:
        if (st == 0) w_act(k, k->nrv == TN_STUNEND ? "StunEnd" : "HitReaction");
        if (w_bck_stopped(k)) w_nrv(k, TN_WAIT);
        break;
    }
    k->step++;
    k->act_t += k->act_rate;
    k->qy = vmul(k->grav, -1.0f); k->qz = vnorm(vtangent(k->front, k->qy)); /* calcMtxFromGravityAndZAxis */
    k->up = k->qy;
    w_bind(k, 80.0f * s);
    /* sensors */
    V3 c = vsub(k->pos, vmul(k->grav, 70.0f * s)), mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    if (k->nrv == TN_PRESS || k->nrv == TN_HITPUNCH) return;
    int msg = mario_offence(c, 70.0f * s, k->grav);
    if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) {
        w_nrv(k, TN_PRESS);
        if (msg == MSG_TRAMPLE) { mario->vup = 15.0f; mario->grounded = 0; }
    } else if (msg == MSG_HITALL) {
        spin_hit_start(k, mario->pos, 20.0f, 35.0f); k->timer = 1; w_nrv(k, TN_HITPUNCH); k->item = 1;
    } else if (vlen(vsub(mc, c)) < 70.0f * s + HIT_BODY_R && mario->hip != HIP_FALL && !mario->hurt_timer &&
               (k->nrv == TN_MOVE || k->nrv == TN_ATTACK) && vdot(vsub(mario->pos, c), k->qy) < 0.25f * 70.0f * s) {
        hurt_mario(c, 15.0f);               /* sendMsgEnemyAttack (not isPlayerExistUp) */
        k->vel = v3(0, 0, 0); w_nrv(k, TN_HITREACTION);
    }
    (void)d;
}

/* Karikari.cpp / KarikariDirector.cpp after Petari: Fall to the ground (SmokeLand), Land (90: Wait from 20 turning to
 * Mario 0.25), then Pursue within 1000 (every 11 frames 33 up and 10 at him; gravity 2.4, damp 0.8 on the ground
 * (still under 10), 0.98 in the air), PrePursue (Search, 11 frames turning, 50 up) from Wait, NoCalcWait (no binder)
 * once on the ground beyond 1000, WatchFor (Glad: hops 35 under 1.4 every 45, five already clinging). Its body (100 at
 * 30 up) within 80 of Mario's: PreCling (3 frames to its slot), Cling at his joint (FacePosition, ArmL2, Hip, Spine1,
 * ShoulderR with their offsets / turns: KarikariDirector cling_data). Clinging ones: his jumps fall (2 + n) / 2 times
 * faster (MarioJump.cpp), every 120 frames a bite takes a life unless he has one left (Mario::checkKarikariDamage);
 * his spin blows them all out (BlowOut), a hip drop landing throws them off (43 out, 33 up: DPDAttacked), R2 with the
 * pointer on one releases it (one per 10 frames: requestRelease). A spin on a free one: BlowOut (10 out, 38 up, 1.3,
 * dead on the ground or after 60, a star piece); a star piece: BlowOutStarPiece (three); dead in a death area / water.
 * ponytail: no Frozen / FrozenRecover / Release (Mario's star spin), no tornado, no push between them, the pointer's
 * screen velocity taken from where it points (fromCursorPos) */
static int pointer_a(void);
static int cursor_sees(V3 p, float px);
static void cursor_ray(V3 *o, V3 *d);
enum { RN_FALL, RN_LAND, RN_NOCALCWAIT, RN_WAIT, RN_PREPURSUE, RN_PURSUE, RN_WATCHFOR, RN_PRECLING, RN_CLING,
       RN_DPDATTACKED, RN_BLOWOUT, RN_BLOWOUTSTARPIECE, RN_PRESS };
/* kari_slot (KarikariDirector mClingingKarikari): the walker index clinging in each slot, -1 none */
static unsigned kari_norelease;                   /* sNoReleaseTime: the frame a release is allowed again */
static void kari_turn(Kuribo *k, V3 dir, float ratio) /* tryTurnToDirection: TQuat setRotate(front, dir, ratio) */
{
    V3 d = vnorm(dir);
    if (vlen(d) < 1e-6f) return;
    float c = vdot(k->front, d);
    V3 ax = vcross(k->front, d);
    if (vlen(ax) < 1e-6f) return;
    k->front = vnorm(rot_axis(k->front, vnorm(ax), acosf(c > 1 ? 1 : c < -1 ? -1 : c) * ratio));
}
static void kari_env(Kuribo *k) /* applyEnvironmentInfluenceToVelocity */
{
    if (w_on_ground(k)) {
        float into = vdot(k->vel, k->gn);
        if (into < 0) k->vel = vsub(k->vel, vmul(k->gn, into)); /* reboundVelocityFromCollision 0, 0, 1 */
        k->vel = vmul(k->vel, 0.8f);
        V3 h = vtangent(k->vel, k->gn);
        if (vdot(h, h) < 100.0f) k->vel = vmul(k->gn, vdot(k->gn, k->vel));
    } else k->vel = vmul(k->vel, k->nrv == RN_DPDATTACKED ? 0.9f : 0.98f);
}
static void kari_unregist(Kuribo *k)
{
    for (int i = 0; i < 5; i++) if (kari_slot[i] == k - kuribo) { kari_slot[i] = -1; kari_cling--; }
}
static void kari_dead(Kuribo *k, int pieces) /* kill (Death) or makeActorDead, its star pieces 80 up */
{
    if (k->nrv == RN_PRECLING || k->nrv == RN_CLING) kari_unregist(k);
    V3 up = vmul(k->grav, -1.0f);
    if (pieces) drop_pieces(vadd(k->pos, vmul(up, 80.0f)), up, pieces);
    fx_at(FX_KARIPON_DEATH, k->pos, up);
    k->state = KURIBO_DEAD; row_kill(k->row);
}
static int kari_blowout(Kuribo *k, V3 from, int star) /* tryBlowOut */
{
    if (k->nrv == RN_BLOWOUT || k->nrv == RN_BLOWOUTSTARPIECE || k->nrv == RN_DPDATTACKED || k->nrv == RN_PRESS) return 0;
    V3 h = vnorm(vtangent(vsub(k->pos, from), k->grav));
    k->vel = vsub(vmul(h, 10.0f), vmul(k->grav, 38.0f));
    fx_at(FX_KARIPON_HIT, k->pos, vmul(k->grav, -1.0f));
    if (k->nrv == RN_PRECLING || k->nrv == RN_CLING) kari_unregist(k);
    w_nrv(k, star ? RN_BLOWOUTSTARPIECE : RN_BLOWOUT);
    return 1;
}
static void kari_release(Kuribo *k, int hipdrop) /* tryHipDropRelease / tryDPDRelease: off him, DPDAttacked */
{
    if (hipdrop) {
        V3 away = vnorm(vtangent(vsub(k->pos, mario->pos), k->grav));
        k->vel = vsub(vmul(away, 43.0f), vmul(k->grav, 33.0f));
    } else { /* setVelocityFromCursorMove: away from the cursor on the screen, 18, then 40 up */
        V3 o, d;
        cursor_ray(&o, &d);
        V3 off = vsub(k->pos, o);
        V3 v = vnorm(vsub(off, vmul(d, vdot(off, d))));
        if (vdot(v, k->grav) > 0) v = vnorm(vtangent(v, k->grav));
        k->vel = vsub(vmul(v, 18.0f), vmul(k->grav, 40.0f));
    }
    kari_unregist(k);
    k->bind_off = 0;
    w_nrv(k, RN_DPDATTACKED);
}
static int kari_dpd(Kuribo *k) /* tryDPDAttacked: R2 with the pointer on it */
{
    if (!pointer_a() || !cursor_sees(k->pos, 30.0f)) return 0;
    if (k->nrv == RN_PRECLING || k->nrv == RN_CLING) { /* requestRelease */
        if (game_frame >= kari_norelease) { kari_release(k, 0); kari_norelease = game_frame + 10; }
    } else { kari_release(k, 0); fx_at(FX_KARIPON_TOUCH, k->pos, vmul(k->grav, -1.0f)); }
    return 1;
}
/* KarikariDirector cling_data: a slot's matrix, Mario's joint x (pos, rot degrees) */
static void kari_cling_mtx(int slot, float out[16])
{
    static const int joint[5] = {17, 21, 3, 10, 24};
    static const float data[5][6] = {{-5.86f, 35.0f, 17.0f, -18.63f, 8.09f, -23.03f}, {0, 0, 0, 0, -27.42f, 0},
                                     {-19.04f, 5.86f, 5.86f, 165.59f, 80.86f, 0}, {0, 0, 0, 0, 113.55f, 0},
                                     {19.04f, 0, 0, 2.29f, -199.69f, 205.0f}};
    float m[16], jm[16], l[16], one[3] = {1, 1, 1};
    basis_matrix(m, mario->pos, mario->up, mario->face, 0);
    if (mario_anim && mario_anim->skin && joint[slot] < mario_anim->skin->njoints) mat_mul(jm, m, mario_anim->world[joint[slot]]);
    else memcpy(jm, m, sizeof jm);
    mat_translate_rot_scale(l, data[slot], data[slot] + 3, one);
    mat_mul(out, jm, l);
}
static void kari_update(Kuribo *k, V3 to, float d)
{
    (void)to;
    if (!k->front.x && !k->front.y && !k->front.z) {
        V3 x, z;
        row_axes(&sc->objs[k->row], &x, &z);
        k->grav = vmul(up_at(k->pos, v3(0, 1, 0)), -1.0f);
        k->front = vnorm(vtangent(z, vmul(k->grav, -1.0f)));
        w_nrv(k, RN_FALL); k->step = 0;
    }
    if (k->nrv != RN_CLING && k->nrv != RN_PRECLING && k->nrv != RN_NOCALCWAIT)
        k->grav = vmul(up_at(k->pos, vmul(k->grav, -1.0f)), -1.0f);
    float depth;
    if (in_death(k->pos) || in_water(k->pos, vmul(k->grav, -1.0f), &depth)) { kari_dead(k, 0); return; } /* control */
    int st = k->step;
    float g = 2.4f;
    V3 toh = vnorm(vtangent(vsub(mario->pos, k->pos), k->grav));
    float d2 = vdot(vsub(mario->pos, k->pos), vsub(mario->pos, k->pos));
    switch (k->nrv) {
    case RN_FALL:
        if (st == 0) { w_act(k, "Wait"); k->bind_off = 0; }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (w_on_ground(k)) { fx_at(FX_KARIPON_SMOKELAND, k->pos, vmul(k->grav, -1.0f)); w_nrv(k, RN_LAND); }
        break;
    case RN_LAND:
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (st == 0) { w_act(k, "Land"); k->bind_off = 0; k->vel = v3(0, 0, 0); }
        if (kari_dpd(k)) break;
        if (st > 90) { w_nrv(k, d2 < 1000.0f * 1000.0f ? RN_PURSUE : RN_WAIT); break; }
        if (st == 20) { w_act(k, "Wait"); break; }
        if (st > 20) kari_turn(k, toh, 0.25f);
        break;
    case RN_NOCALCWAIT:
        if (st == 0) k->bind_off = 1;
        k->vel = v3(0, 0, 0);
        if (sqrtf(d2) < 1000.0f) w_nrv(k, RN_WAIT);
        break;
    case RN_WAIT:
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (st == 0) { k->bind_off = 0; w_act(k, "Wait"); }
        if (kari_dpd(k)) break;
        if (d2 < 1000.0f * 1000.0f) { w_nrv(k, RN_PREPURSUE); break; }
        if (w_on_ground(k)) w_nrv(k, RN_NOCALCWAIT);
        break;
    case RN_PREPURSUE:
        if (st == 0) { k->bind_off = 0; w_act(k, "Search"); }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (kari_dpd(k)) break;
        if (st < 11) { kari_turn(k, vsub(mario->pos, k->pos), 0.25f); break; }
        if (st == 11) { k->vel = vsub(k->vel, vmul(k->grav, 50.0f)); break; }
        if (w_on_ground(k)) { w_nrv(k, RN_PURSUE); fx_at(FX_KARIPON_SMOKELAND, k->pos, vmul(k->grav, -1.0f)); }
        break;
    case RN_PURSUE:
        if (st == 0) { k->bind_off = 0; w_act(k, "Jump"); }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (kari_dpd(k)) break;
        if (st < 11) { kari_turn(k, toh, 0.25f); break; }
        if (st == 11) { k->vel = vadd(vsub(k->vel, vmul(k->grav, 33.0f)), vmul(toh, 10.0f)); break; }
        if (w_on_ground(k)) {
            fx_at(FX_KARIPON_SMOKELAND, k->pos, vmul(k->grav, -1.0f));
            if (kari_cling == 5) { w_nrv(k, RN_WATCHFOR); break; }
            if (d2 > 1000.0f * 1000.0f) { w_nrv(k, RN_WAIT); break; }
            kari_turn(k, toh, 0.25f);
            w_nrv(k, RN_PURSUE);
        }
        break;
    case RN_WATCHFOR:
        if (st == 0) { k->bind_off = 0; w_act(k, "Glad"); }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, 1.4f));
        if (kari_dpd(k)) break;
        if (!w_on_ground(k)) break;
        if (st < 7) {
            kari_turn(k, vsub(mario->pos, k->pos), 0.25f);
            if (kari_cling < 5 || d2 > 500.0f * 500.0f) w_nrv(k, RN_PREPURSUE);
            break;
        }
        if (st == 7) { k->vel = vsub(k->vel, vmul(k->grav, 35.0f)); break; }
        if (st > 45) w_nrv(k, RN_WATCHFOR);
        break;
    case RN_PRECLING: {
        if (st == 0) { k->bind_off = 1; w_act(k, "Wait"); }
        if (kari_dpd(k)) break;
        float m[16], t = st / 3.0f;
        kari_cling_mtx(k->dir, m);
        k->vel = vadd(vmul(k->vel, 1.0f - t), vmul(vsub(v3(m[12], m[13], m[14]), k->pos), t));
        if (st > 3) w_nrv(k, RN_CLING);
        break;
    }
    case RN_CLING: {
        if (st == 0) { k->bind_off = 1; w_act(k, "Bite"); k->vel = v3(0, 0, 0); if (kari_cling == 1) kari_timer = 120; }
        if (kari_dpd(k)) break;
        float m[16];
        kari_cling_mtx(k->dir, m);
        k->pos = v3(m[12], m[13], m[14]);
        k->qy = vnorm(v3(m[4], m[5], m[6])); k->qz = vnorm(v3(m[8], m[9], m[10])); /* setBaseTRMtx(mClingPosition) */
        break;
    }
    case RN_DPDATTACKED:
        if (st == 0) { k->bind_off = 0; w_act(k, "SpinX"); k->act_rate = 2.0f; }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, 2.4f));
        if (w_on_ground(k)) { fx_at(FX_KARIPON_SMOKELAND, k->pos, vmul(k->grav, -1.0f)); w_nrv(k, RN_LAND); }
        break;
    case RN_BLOWOUT: case RN_BLOWOUTSTARPIECE:
        if (st == 0) { k->bind_off = 0; w_act(k, "SpinX"); k->act_rate = 2.0f; }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, 1.3f));
        if (((k->on_ground || k->on_wall) && st > 0) || st > 60) { kari_dead(k, k->nrv == RN_BLOWOUT ? 1 : 3); return; }
        break;
    case RN_PRESS:
        if (st == 0) { k->bind_off = 0; w_act(k, "Press"); }
        kari_env(k); k->vel = vadd(k->vel, vmul(k->grav, g));
        if (st > 40) { kari_dead(k, 0); return; }
        break;
    }
    k->step++;
    k->act_t += k->act_rate;
    if (k->nrv != RN_CLING) { /* calcMtxFromGravityAndZAxis */
        k->qy = vmul(k->grav, -1.0f); k->qz = vnorm(vtangent(k->front, k->qy));
    }
    k->up = k->qy;
    if (k->nrv == RN_CLING) return;
    w_bind(k, 30.0f * sc->objs[k->row].scale[1]);
    /* attackSensor: in Wait / PrePursue / Pursue (_9E) within 80 of Mario's centre it clings (registCling) */
    int n = k->nrv;
    V3 c = vsub(k->pos, vmul(k->grav, 30.0f));
    if ((n == RN_WAIT || n == RN_PREPURSUE || n == RN_PURSUE) && kari_cling < 5 &&
        vlen(vsub(c, vadd(mario->pos, vmul(mario->up, HIT_BODY_H)))) < 80.0f) {
        for (int i = 0; i < 5; i++)
            if (kari_slot[i] < 0) { kari_slot[i] = (int)(k - kuribo); k->dir = i; kari_cling++; break; }
        k->vel = v3(0, 0, 0);
        w_nrv(k, RN_PRECLING);
        printf("karikari %d clings (%d)" "\n", k->row, kari_cling);
        return;
    }
    if (n != RN_PRECLING && mario_offence(c, 100.0f, k->grav) == MSG_HITALL) kari_blowout(k, mario->pos, 0); /* isMsgPlayerHitAll */
    (void)d;
}

/* KarikariDirector::movement: Mario's spin blows the clinging ones out, his hip drop landing throws them off */
static void kari_director(void)
{
    if (!kari_cling) return;
    for (int i = 0; i < 5; i++) {
        if (kari_slot[i] < 0) continue;
        Kuribo *k = &kuribo[kari_slot[i]];
        if (mario->spin_timer == SPIN_TIME - 1) kari_blowout(k, mario->pos, 0); /* isPlayerSwingAction */
        else if (hip_hit) kari_release(k, 1);                                 /* removeAllClingingKarikari */
    }
}

/* PackunPetit.cpp after Petari: Wait (its clip eased in over 60) until Mario is within 1700, Find (Suspect), Threat
 * (turning 2 degrees a frame to him, TurnLeft / TurnRight when he is more than 90 off its front, easing from 10 to 2
 * degrees); after 50 within 1100 and facing him (10) AttackStart (Attack, 70), Attack (12), AttackBack, then Threat
 * or Wait beyond 1700 / 2000; NonActive beyond 5000. Its attack sensor (joint Head + (50, 0, 0), 50) hurts Mario
 * strongly unless he hip-drops (HitWaitForAttack: Impact, Hit); a trample or hip drop: TrampleDown (Press, a coin);
 * a spin: PunchDown (its head PackunPetitHead flies 20 out, 40 up under 2.5: three star pieces at step 20 or where it
 * strikes, dead at 30); a star piece: SwoonStart, Swoon 250 (a spin then punches it down), SwoonToThreat. arg0 1: it
 * never turns. ponytail: no star pointer (DPDSwoon), no shadow, no hit mark effect */
enum { PN_NONACTIVE, PN_WAIT, PN_FIND, PN_THREAT, PN_LEFTTURN, PN_RIGHTTURN, PN_ATTACKSTART, PN_ATTACK, PN_ATTACKBACK,
       PN_HITWAITFORATTACK, PN_HIT, PN_TRAMPLEDOWN, PN_PUNCHDOWN, PN_SWOONSTART, PN_SWOON, PN_SWOONTOTHREAT };
static int packun_face(const Kuribo *k, float deg) /* isFaceToPlayerHorizontalDegree */
{
    V3 a = vnorm(vtangent(vsub(mario->pos, k->pos), k->grav)), b = vnorm(vtangent(k->front, k->grav));
    return vdot(a, b) >= cosf(deg * 0.0174533f);
}
static int packun_turn(Kuribo *k) /* tryTurn */
{
    if (sc->objs[k->row].arg[0] > 0 || packun_face(k, 90.0f)) return 0;
    V3 side = vcross(k->qy, k->qz); /* calcSideVec: its X */
    w_nrv(k, vdot(vsub(mario->pos, k->pos), side) > 0 ? PN_LEFTTURN : PN_RIGHTTURN);
    return 1;
}
static void packun_select_wait(Kuribo *k) /* selectNrvWait */
{
    if (vlen(vsub(mario->pos, k->pos)) >= 1700.0f) w_nrv(k, PN_WAIT);
    else w_nrv(k, k->nrv == PN_SWOON ? PN_SWOONTOTHREAT : PN_THREAT);
}
static void packun_kill(Kuribo *k)
{
    V3 up = vmul(k->grav, -1.0f);
    if (k->bshow == 1) { fx_at(FX_PACKUNPETITHEAD_DEATH, k->bpos, up); drop_pieces(k->bpos, up, 3); }
    else if (!k->bshow && sc->objs[k->row].sw[SW_DEAD] < 0) pop_coins(k->pos, up, 1); /* no blown model, no dead switch */
    k->bshow = 0;
    fx_at(FX_PACKUNPETIT_DEATH, k->pos, up);
    k->state = KURIBO_DEAD; row_kill(k->row);
}
static void packun_punch(Kuribo *k, V3 from) /* punchDown */
{
    V3 d = vnorm(vtangent(vsub(k->pos, from), k->grav));
    k->bvel = vsub(vmul(d, 20.0f), vmul(k->grav, 40.0f));
    w_nrv(k, PN_PUNCHDOWN);
}
static void packun_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    float s = r->scale[1];
    if (!k->front.x && !k->front.y && !k->front.z) {
        V3 x, z;
        row_axes(r, &x, &z);
        float m[16];
        mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
        k->qy = vnorm(v3(m[4], m[5], m[6]));                       /* calcUpVec */
        k->grav = vmul(up_at(k->pos, k->qy), -1.0f);               /* calcGravity */
        k->front = vnorm(vtangent(z, k->qy));                      /* calcFrontVec */
        w_nrv(k, PN_WAIT); k->step = 0;
    }
    float dm = vlen(vsub(mario->pos, k->pos));
    int st = k->step;
    switch (k->nrv) {
    case PN_NONACTIVE:
        if (dm < 5000.0f) w_nrv(k, PN_WAIT);
        break;
    case PN_WAIT:
        if (st == 0) { w_act(k, "Wait"); k->act_interp = 60; }
        if (dm < 1700.0f) w_nrv(k, PN_FIND);
        else if (dm >= 5000.0f) w_nrv(k, PN_NONACTIVE);
        break;
    case PN_FIND:
        if (st == 0) w_act(k, "Suspect");
        if (w_bck_stopped(k)) w_nrv(k, PN_THREAT);
        break;
    case PN_THREAT:
        if (st == 0) w_act(k, "Threat");
        if (r->arg[0] <= 0) {
            if (packun_turn(k)) break;
            w_turn_to(k, mario->pos, 2.0f);
        }
        if (st > 50 && dm < 1100.0f && (packun_face(k, 10.0f) || r->arg[0] > 0)) w_nrv(k, PN_ATTACKSTART);
        else if (dm >= 2000.0f) w_nrv(k, PN_WAIT);
        break;
    case PN_LEFTTURN: case PN_RIGHTTURN: {
        if (st == 0) w_act(k, k->nrv == PN_LEFTTURN ? "TurnLeft" : "TurnRight");
        float deg = 10.0f - st;
        w_turn_to(k, mario->pos, deg < 2.0f ? 2.0f : deg);
        if (w_bck_stopped(k)) w_nrv(k, PN_THREAT);
        break;
    }
    case PN_ATTACKSTART:
        if (st == 0) w_act(k, "Attack");
        if (st == 70) w_nrv(k, PN_ATTACK);
        break;
    case PN_ATTACK:
        if (st == 12) w_nrv(k, PN_ATTACKBACK);
        break;
    case PN_ATTACKBACK:
        if (w_bck_stopped(k) && !packun_turn(k)) packun_select_wait(k);
        break;
    case PN_HITWAITFORATTACK:
        if (st == 0 && strcmp(k->act, "Attack")) w_act(k, "Impact");
        if (w_bck_stopped(k)) w_nrv(k, PN_HIT);
        break;
    case PN_HIT:
        if (st == 0) w_act(k, "Hit");
        if (w_bck_stopped(k) && !packun_turn(k)) packun_select_wait(k);
        break;
    case PN_TRAMPLEDOWN:
        if (st == 0) w_act(k, "Press");
        if (w_bck_stopped(k)) { packun_kill(k); return; }
        break;
    case PN_PUNCHDOWN:
        if (st == 0) {
            k->bshow = 1;
            k->bpos = w_joint(k, m_packun, 5, v3(0, 0, 0)); /* copyJointPos Head */
            w_act(k, "Blow");
            scene_stop = 5;                                  /* stopSceneForDefaultHit (the spin) */
        }
        k->bvel = vadd(k->bvel, vmul(k->grav, 2.5f));
        k->bpos = vadd(k->bpos, k->bvel);
        if (k->bshow == 1) {
            Hit h[1];
            if (st == 20 || kcl_query(k->bpos, 50.0f, h, 1)) { /* checkStrikeBallToMap 50 */
                V3 up = vmul(k->grav, -1.0f);
                fx_at(FX_PACKUNPETITHEAD_DEATH, k->bpos, up);
                drop_pieces(k->bpos, up, 3);
                k->bshow = 2; /* hidden */
            }
        }
        if (st == 30) { packun_kill(k); return; }
        break;
    case PN_SWOONSTART:
        if (st == 0) w_act(k, "SwoonStart");
        if (w_bck_stopped(k)) w_nrv(k, PN_SWOON);
        break;
    case PN_SWOON:
        if (st == 0) w_act(k, "Swoon");
        if (st == 250) packun_select_wait(k);
        break;
    case PN_SWOONTOTHREAT:
        if (st == 0) w_act(k, "SwoonToThreat");
        if (w_bck_stopped(k)) w_nrv(k, PN_THREAT);
        break;
    }
    k->step++;
    k->act_t += k->act_rate;
    k->qz = vnorm(vtangent(k->front, k->qy)); /* makeMtxUpFrontPos */
    k->up = k->qy;
    if (k->nrv == PN_TRAMPLEDOWN || k->nrv == PN_PUNCHDOWN || k->nrv == PN_NONACTIVE) return;
    /* sensors: Mario's offence on its body (Head + (50, 0, 0), 90); its attack (same place, 50) on him */
    V3 head = w_joint(k, m_packun, 5, v3(50.0f, 0, 0));
    int busy = k->nrv == PN_HITWAITFORATTACK || k->nrv == PN_HIT, msg = busy ? MSG_NONE : mario_offence(head, 90.0f * s, k->grav);
    if (k->nrv == PN_ATTACK && (msg == MSG_TRAMPLE || msg == MSG_HIPDROP || msg == MSG_HITALL)) msg = MSG_NONE;
    if (msg == MSG_TRAMPLE || msg == MSG_HIPDROP) {
        w_nrv(k, PN_TRAMPLEDOWN);
        if (msg == MSG_TRAMPLE) { mario->vup = 15.0f; mario->grounded = 0; }
    } else if (msg == MSG_HITALL) packun_punch(k, mario->pos);
    else if (!mario->hurt_timer && vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), head)) < 50.0f * s + HIT_BODY_R) {
        hurt_mario(head, 25.0f); /* sendMsgEnemyAttackStrong */
        if (mario->hip != HIP_FALL) w_nrv(k, PN_HITWAITFORATTACK);
    }
}

/* screen position of p (calcScreenPosition: 640 x 448), 0 if behind the camera */
static int screen_pos(V3 p, float *x, float *y)
{
    V3 ce, cw, cu;
    float fovy;
    camera_pose(&ce, &cw, &cu, &fovy);
    V3 fw = vnorm(vsub(cw, ce)), sv = vnorm(vcross(fw, cu)), u = vcross(sv, fw), to = vsub(p, ce);
    float z = vdot(to, fw), f = 224.0f / tanf(fovy * 3.14159265f / 360.0f);
    if (z <= 1.0f) return 0;
    *x = 320.0f + vdot(to, sv) * f / z; *y = 224.0f - vdot(to, u) * f / z;
    return 1;
}
static float converge(float v, float to, float step) { return v < to ? (v + step > to ? to : v + step) : (v - step < to ? to : v - step); }

/* Butterfly.cpp after Petari: Wait (back to its home 0.01 a frame, at most 2, damp 0.99, clip rate easing to 1),
 * RunAway when the pointer sweeps onto it (moving towards it, B up, within 500 of home: 12 away on the screen, damp
 * 0.97, rate to 3, 30 frames), Hive while B (L1) is held with the pointer within 100 pixels (towards where it points
 * 0.05, at most 2.5, rate to 1.2); it turns its front to its way (at most acos 0.997 a frame) when faster than 0.5;
 * arg1 -1: a star piece once the pointer is within 10 pixels; binder 30. ponytail: no perching (NPC sensors, sleeping
 * Mario), all of them the BRK's first colour (arg0 needs a per-instance BRK) */
enum { BN_WAIT, BN_RUNAWAY, BN_HIVE };
static int bfly_run_away(Kuribo *k, V3 home)
{
    float sx, sy;
    if (!cursor_sees(k->pos, 100.0f) || pad_held(PAD_L1) || vlen(vsub(k->pos, home)) > 500.0f || !screen_pos(k->pos, &sx, &sy)) return 0;
    float dx = sx - cur_x, dy = sy - cur_y;
    if (cur_vx * dx + cur_vy * dy <= 0.0f) return 0;
    V3 ce, cw, cu;
    float fovy;
    camera_pose(&ce, &cw, &cu, &fovy);
    V3 fw = vnorm(vsub(cw, ce)), camx = vnorm(vcross(fw, cu)), camy = vcross(camx, fw);
    V3 v = vadd(vmul(camx, dx), vmul(camy, -dy));                 /* camX x + (-camY) y, screen y down */
    if (vdot(v, k->grav) > 0.0f) v = vtangent(v, k->grav);
    k->vel = vadd(k->vel, vmul(vnorm(v), 12.0f));
    w_nrv(k, BN_RUNAWAY);
    return 1;
}
static void butterfly_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    V3 home = row_pos(r);
    if (!k->front.x && !k->front.y && !k->front.z) {
        float m[16], one[3] = {1, 1, 1};
        mat_translate_rot_scale(m, r->pos, r->rot, one);           /* mQuat.setEuler(mRotation) */
        k->qy = vnorm(v3(m[4], m[5], m[6])); k->qz = k->front = vnorm(v3(m[8], m[9], m[10]));
        k->grav = vmul(up_at(k->pos, k->qy), -1.0f);
        k->dir = r->arg[1] != -1;                                  /* mStarPieceAppeared */
        k->act_rate = 1.0f;
        w_act(k, "Butterfly");
        w_nrv(k, BN_WAIT); k->step = 0;
    }
    k->grav = vmul(up_at(k->pos, vmul(k->grav, -1.0f)), -1.0f);
    /* control: updatePosture, tryAppearStarPeace */
    V3 vh = vtangent(k->vel, k->grav);
    if (vlen(k->vel) >= 0.5f && vlen(vh) > 1e-6f) {
        V3 tf = vnorm(vtangent(vnorm(vh), k->qy));
        if (vlen(tf) > 1e-6f) {
            float c = vdot(k->qz, tf);
            if (c > 0.997f) k->qz = tf;
            else {
                V3 rej = vnorm(vtangent(tf, k->qz));
                k->qz = vnorm(vadd(vmul(k->qz, 0.997f), vmul(rej, sqrtf(1.0f - 0.997f * 0.997f))));
            }
        }
    }
    if (!k->dir && cursor_sees(k->pos, 10.0f)) { drop_pieces(k->pos, vmul(k->grav, -1.0f), 1); k->dir = 1; }
    switch (k->nrv) {
    case BN_WAIT:
        if (bfly_run_away(k, home)) break;
        if (pad_held(PAD_L1) && cursor_sees(k->pos, 100.0f) && vlen(vsub(k->pos, home)) <= 500.0f) { w_nrv(k, BN_HIVE); break; } /* tryHive */
        k->act_rate = converge(k->act_rate, 1.0f, 0.02f);
        k->vel = vmul(k->vel, 0.99f);
        if (vlen(vsub(home, k->pos)) > 1e-3f) {
            k->vel = vadd(k->vel, vmul(vnorm(vsub(home, k->pos)), 0.01f));
            if (vlen(k->vel) > 2.0f) k->vel = vmul(vnorm(k->vel), 2.0f);
        }
        break;
    case BN_RUNAWAY:
        k->act_rate = converge(k->act_rate, 3.0f, 0.02f);
        k->vel = vmul(k->vel, 0.97f);
        if (k->step == 30) w_nrv(k, BN_WAIT);
        break;
    case BN_HIVE: {
        if (bfly_run_away(k, home)) break;
        if (vlen(vsub(k->pos, home)) > 500.0f) { w_nrv(k, BN_WAIT); break; }
        V3 o, d;
        cursor_ray(&o, &d);
        V3 p = vadd(o, vmul(d, vdot(vsub(k->pos, o), d)));         /* calcStarPointerWorldPointingPos at its depth */
        k->vel = vadd(k->vel, vmul(vsub(p, k->pos), 0.05f));
        if (vlen(k->vel) > 2.5f) k->vel = vmul(vnorm(k->vel), 2.5f);
        k->act_rate = converge(k->act_rate, 1.2f, 0.02f);
        break;
    }
    }
    k->step++;
    k->act_t += k->act_rate;
    k->up = k->qy;
    V3 c = vadd(k->pos, k->vel);                                    /* initBinder(30, 0) */
    collide(&c, vmul(k->grav, -1.0f), 30.0f);
    k->pos = c;
}

/* a ported walker's isMsgToEnemyAttackBlow / isMsgExplosionAttack from `from`: 1 if it took it, -1 if it is not
 * ported (the caller's old handling), 0 if it ignored it */

/* Petari.cpp after Petari (the hiding star-piece mole): hidden (Lurk) until Mario is within 1500 and it is on the
 * screen, then Wait (it surfaces at 60: Wait clip), Approach (hidden, 0.5 towards him) beyond 700 until within 600,
 * Escape (hidden, from him meandering -90..90 every 60-120 frames, turning away when he runs at it, off walls within
 * 600, 0.45 boosted by 0.5 x min(1, 4 / (d / 100)^2), damp 0.97) within 500 until beyond 710; moveAndTurnToDirection
 * (gravity 1.3, turning 3 degrees, 180 the first escaping frame). Its spin sensor (100) takes a spin in Wait / Escape:
 * SwoonStart (Appearance, 5 off Mario), Swoon 180 (a star piece restarts it), SwoonEnd (Hide), Land 45 then Escape.
 * Swooning, a spin: SpinOut (Damage, blown 10 away), a trample or hip drop: Smash (Press); both throw its star pieces
 * (arg1, 20) 2 every 5 frames from Head3 (20, 40), the rest at the end, then kill (its dead switch). Its body (60, 60
 * up) pushes Mario while shown (isSolidBody); a jet turtle or fire ball then: KickOut, an enemy's blow: SpinOut.
 * Its up turns 0.1 a frame to the ground's normal (control). ponytail: no 2P pointer (Freeze), no kick, no
 * footprints (FootPrint decals) or smoke effects, no push from enemies, no move limit collision, no AnimScaleController
 * (hit reaction); Mario's spin hits once a spin */
/* Kuribo fields: phase hidden (hideModel), timer star pieces left, sstep mMeanderStep, to mMeanderAngle, ttarget
 * mTargetDir, tcenter mBodyCenter, bshow its spin latch */
static int petari_solid(const Kuribo *k) /* isSolidBody */
{
    return !k->phase && (k->nrv == PT_SWOONSTART || k->nrv == PT_SWOON || k->nrv == PT_SWOONEND || k->nrv == PT_WAIT);
}
static void petari_move(Kuribo *k, V3 dir, float speed, float grav, float damp, float deg) /* moveAndTurnToDirection */
{
    turn_on_plane(&k->front, dir, k->grav, cosf(deg * 0.0174533f));
    V3 g = k->on_ground ? vmul(k->gn, -1.0f) : k->grav; /* turnDirectionToGround */
    V3 f = vnorm(vtangent(k->front, g));
    if (vlen(f) > 0.5f) k->front = f;
    if (!k->on_ground) speed = 0; /* their callers: no push off the ground */
    w_add_move(k, k->front, speed);
    k->vel = vmul(k->vel, damp);
    if (k->on_wall == 2 && vdot(k->vel, k->wn) < 0) k->vel = vsub(k->vel, vmul(k->wn, vdot(k->vel, k->wn))); /* rebound 0, 0, 1 */
    k->vel = k->on_ground ? vsub(k->vel, vmul(k->gn, grav)) : vadd(k->vel, vmul(k->grav, grav)); /* addVelocityToGravityOrGround */
}
static void petari_blow(Kuribo *k, V3 from, int nrv) /* setVelocityBlowAttack 10, 0 */
{
    V3 h = vtangent(vsub(k->pos, from), k->grav);
    k->vel = vlen(h) > 1e-3f ? vmul(vnorm(h), 10.0f) : v3(0, 0, 0);
    if (vlen(vsub(mario->pos, k->pos)) < 1000.0f) scene_stop = 4; /* stopSceneForDefaultHit */
    w_nrv(k, nrv);
}
static void petari_pieces(Kuribo *k, int all) /* appearStarPieceGradually / All */
{
    V3 up = vmul(k->grav, -1.0f);
    if (all) { if (k->timer > 0) launch_pieces(k->pos, up, k->timer, 20.0f, 40.0f); return; }
    if (k->step % 5) return;
    int n = k->timer < 2 ? k->timer : 2;
    if (n > 0) launch_pieces(w_joint(k, m_petari, 5, v3(0, 0, 0)), up, n, 20.0f, 40.0f);
    k->timer -= n;
}
static void petari_escape_dir(Kuribo *k) /* calcEscapeDirection, meander, avoidPlayer, avoidWall */
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), d = vsub(k->tcenter, c);
    k->ttarget = vlen(d) > 1e-3f ? vnorm(d) : k->front;
    if (k->step == k->sstep) { k->to = frand(-90.0f, 90.0f); k->sstep = k->step + 60 + rand() % 61; }
    k->ttarget = rot_axis(k->ttarget, vmul(k->grav, -1.0f), k->to * 0.0174533f);
    float sign = vdot(vcross(k->ttarget, k->front), k->grav) < 0 ? -1.0f : 1.0f;
    V3 tp = vsub(c, k->tcenter);
    if (vlen(tp) > 1e-3f) {
        float w = 3.0f / vdot(vmul(tp, 0.01f), vmul(tp, 0.01f));
        w = w > 1 ? 1 : w;
        V3 mv = vadd(mario->hvel, vmul(mario->up, mario->vup));
        float ap = vlen(mv) > 1e-6f ? vdot(vmul(vnorm(tp), -1.0f), vnorm(mv)) : 0.0f;
        w *= ap < 0 ? 0 : ap;
        k->ttarget = rot_axis(k->ttarget, k->grav, w * 90.0f * sign * 0.0174533f);
    }
    V3 pd = vtangent(k->ttarget, k->grav);
    if (vlen(pd) > 1e-3f) {
        pd = vnorm(pd);
        for (float t = 25.0f; t <= 600.0f; t += 25.0f) { /* getFirstPolyOnLineToMap (stepped spheres) */
            Hit h[1];
            V3 p = vadd(k->tcenter, vmul(pd, t));
            if (!kcl_query(p, 15.0f, h, 1)) continue;
            V3 wn = vtangent(h[0].n, k->grav);
            if (vlen(wn) > 1e-3f) {
                wn = vnorm(wn);
                float pr = 1.0f - t / 600.0f;
                pr = pr < 0 ? 0 : pr > 1 ? 1 : pr;
                k->ttarget = vsub(k->ttarget, vmul(wn, vdot(wn, k->ttarget) * 2.0f * pr * pr * 2.0f));
            }
            break;
        }
    }
}
static void petari_update(Kuribo *k)
{
    const Row *r = &sc->objs[k->row];
    if (!k->front.x && !k->front.y && !k->front.z) { /* init */
        V3 x, z;
        row_axes(r, &x, &z);
        k->front = z; k->ttarget = v3(0, 0, 1);
        k->grav = vmul(up_at(k->pos, vmul(z, 0.0f)), -1.0f);
        k->qy = vmul(k->grav, -1.0f); k->qz = vnorm(vtangent(z, k->qy));
        k->timer = r->arg[1] != -1 ? r->arg[1] : 20; /* initStarPiece */
        k->phase = 1; /* makeActorAppeared: hideModel */
        w_nrv(k, PT_LURK); k->step = 0;
    }
    k->grav = vmul(up_at(k->pos, vmul(k->grav, -1.0f)), -1.0f); /* onCalcGravity */
    int st = k->step;
    float dm = vlen(vsub(mario->pos, k->pos));
    k->tcenter = vsub(k->pos, vmul(k->grav, 60.0f)); /* calcCenter */
    switch (k->nrv) {
    case PT_LURK: {
        if (st == 0) k->vel = v3(0, 0, 0);
        float sx, sy;
        if (dm < 1500.0f && screen_pos(k->pos, &sx, &sy) && sx >= 0 && sx < 640 && sy >= 0 && sy < 448) w_nrv(k, PT_JUMPOUT);
        break;
    }
    case PT_JUMPOUT:
        w_nrv(k, PT_WAIT);
        break;
    case PT_WAIT:
        if (st == 0) k->vel = v3(0, 0, 0);
        petari_move(k, k->ttarget, 0.0f, 1.3f, 0.92f, 3.0f);
        if (st == 60) {
            if (k->phase) { k->phase = 0; w_act(k, "Wait"); }
            else { w_act(k, "Wait"); k->act_interp = 30; }
        }
        if (dm > 700.0f) w_nrv(k, PT_APPROACH);
        else if (dm < 500.0f) w_nrv(k, PT_ESCAPE);
        break;
    case PT_APPROACH: {
        if (st == 0) k->phase = 1;
        V3 d = vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), k->tcenter);
        k->ttarget = vlen(d) > 1e-3f ? vnorm(d) : k->front; /* calcApproachDirection */
        petari_move(k, k->ttarget, 0.5f, 1.3f, 0.92f, 3.0f);
        if (dm < 600.0f) w_nrv(k, PT_WAIT);
        break;
    }
    case PT_ESCAPE: case PT_LAND: {
        if (st == 0) { if (k->nrv == PT_ESCAPE) k->phase = 1; k->sstep = 0; }
        petari_escape_dir(k);
        float dc = vlen(vsub(vadd(mario->pos, vmul(mario->up, HIT_BODY_H)), k->tcenter)) * 0.01f, b = 4.0f / (dc * dc);
        b = b > 1 ? 1 : b;
        if (k->nrv == PT_ESCAPE) petari_move(k, k->ttarget, 0.45f + 0.45f * 0.5f * b, 1.3f, 0.97f, st == 0 ? 180.0f : 10.0f);
        else petari_move(k, k->ttarget, 1.0f, 1.3f, 0.99f, st == 0 ? 180.0f : 10.0f);
        if (k->nrv == PT_ESCAPE && dm > 710.0f) w_nrv(k, PT_WAIT);
        if (k->nrv == PT_LAND && st == 45) w_nrv(k, PT_ESCAPE);
        break;
    }
    case PT_SWOONSTART:
        if (st == 0) { /* calcSpinOutVelocity(5) */
            V3 d = vtangent(vsub(k->tcenter, vadd(mario->pos, vmul(mario->up, HIT_BODY_H))), k->grav);
            if (vlen(d) > 1e-3f) { k->front = vnorm(vmul(d, -1.0f)); k->vel = vmul(vnorm(d), 5.0f); }
            k->phase = 0; w_act(k, "Appearance");
        }
        petari_move(k, k->ttarget, 0.0f, 1.3f, 0.985f, 3.0f);
        if (w_bck_stopped(k)) w_nrv(k, PT_SWOON);
        break;
    case PT_SWOON:
        if (st == 0) { k->vel = v3(0, 0, 0); w_act(k, "Swoon"); }
        petari_move(k, k->ttarget, 0.0f, 1.3f, 0.0f, 3.0f);
        if (st == 180) w_nrv(k, PT_SWOONEND);
        break;
    case PT_SWOONEND:
        if (st == 0) w_act(k, "Hide");
        petari_move(k, k->ttarget, 0.0f, 1.3f, 0.0f, 3.0f);
        if (w_bck_stopped(k)) { k->phase = 1; w_nrv(k, PT_LAND); }
        break;
    case PT_SMASH:
        if (st == 0) { k->phase = 0; w_act(k, "Press"); }
        petari_pieces(k, 0);
        if (w_bck_stopped(k)) { petari_pieces(k, 1); k->state = KURIBO_DEAD; row_kill(k->row); printf("petari %d smashed\n", k->row); return; }
        break;
    case PT_SPINOUT: case PT_KICKOUT: {
        if (st == 0) { k->phase = 0; w_act(k, "Damage"); }
        turn_on_plane(&k->front, vmul(k->vel, -1.0f), k->grav, -1.0f); /* turnDirectionDegree 180 */
        V3 g = w_on_ground(k) ? vmul(k->gn, -1.0f) : k->grav;
        k->vel = vadd(vsub(k->vel, vmul(g, vdot(g, k->vel))), vmul(g, 1.3f));
        petari_pieces(k, 0);
        if (w_bck_stopped(k)) { petari_pieces(k, 1); k->state = KURIBO_DEAD; row_kill(k->row); printf("petari %d spun out\n", k->row); return; }
        break;
    }
    }
    k->step++;
    k->act_t += k->act_rate;
    /* control: its up 0.1 of the way to the ground's normal (or against gravity), front kept */
    V3 to = w_on_ground(k) ? k->gn : vmul(k->grav, -1.0f), ax = vcross(k->qy, to);
    if (vlen(ax) > 1e-6f) {
        float c = vdot(k->qy, to);
        k->qy = vnorm(rot_axis(k->qy, vnorm(ax), acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 0.1f));
    }
    V3 qz = vnorm(vtangent(k->front, k->qy));
    if (vlen(qz) > 0.5f) k->qz = qz;
    w_bind(k, 60.0f);
    k->up = k->qy;
    /* sensors */
    if (mario->spin_timer <= 0) k->bshow = 0;
    V3 body = vadd(k->pos, vmul(k->qy, 60.0f)), c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    int msg = mario_offence(body, 60.0f, k->grav);
    if (k->nrv == PT_SWOON && msg == MSG_HITALL && !k->bshow) { k->bshow = 1; petari_blow(k, mario->pos, PT_SPINOUT); return; }
    if (k->nrv == PT_SWOON && (msg == MSG_TRAMPLE || msg == MSG_HIPDROP)) {
        if (msg == MSG_TRAMPLE) { mario->vup = 15.0f; mario->grounded = 0; }
        w_nrv(k, PT_SMASH);
        return;
    }
    if ((k->nrv == PT_WAIT || k->nrv == PT_ESCAPE) && !k->bshow && mario->spin_timer > 0 && mario_touch(k->pos, 100.0f)) {
        k->bshow = 1; w_nrv(k, PT_SWOONSTART); /* receivePlayerAttackAtSpin */
        printf("petari %d spun: swoons\n", k->row);
        return;
    }
    float d = vlen(vsub(c, body));
    if (petari_solid(k) && d < 60.0f + HIT_BODY_R && d > 1e-3f) /* attackSensor: sendMsgPush */
        mario->pos = vadd(mario->pos, vmul(vtangent(vsub(c, body), mario->up), (60.0f + HIT_BODY_R - d) / d));
}

static int walker_blow(Kuribo *e, V3 from)
{
    if (e->kind == WALK_PETARI) { /* receiveMsgEnemyAttack: isMsgToEnemyAttackBlow while solid */
        if (!petari_solid(e)) return 0;
        petari_blow(e, from, PT_SPINOUT);
        return 1;
    }
    if (e->kind == WALK_PACKUN) { /* punchDown */
        if (e->nrv == PN_TRAMPLEDOWN || e->nrv == PN_PUNCHDOWN) return 0;
        packun_punch(e, from);
        return 1;
    }
    if (e->kind != WALK_KURIBO && e->kind != WALK_KMINI && e->kind != WALK_TAKOBO && e->kind != WALK_KARIKARI) return -1;
    {
        if (e->kind == WALK_KURIBO || e->kind == WALK_KMINI) { /* requestBlowDown */
            if (kn_in(e, KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN) | KNB(KN_BLOW))) return 0;
            V3 h = vnorm(vtangent(vsub(e->pos, from), e->grav));
            float hv = e->kind == WALK_KURIBO ? 36.0f : 22.0f, vv = e->kind == WALK_KURIBO ? 30.0f : 25.0f;
            e->vel = vsub(vmul(h, hv), vmul(e->grav, vv));
            e->item = 2;
            w_nrv(e, KN_BLOWDOWN);
        } else if (e->kind == WALK_TAKOBO) { /* startWithoutStopScene */
            if (e->nrv == TN_PRESS || e->nrv == TN_HITPUNCH) return 0;
            spin_hit_start(e, from, 20.0f, 35.0f); e->timer = 0; e->item = 1; w_nrv(e, TN_HITPUNCH);
        } else if (e->kind == WALK_KARIKARI) { /* blown 30 from the explosion */
            if (e->nrv == RN_BLOWOUT || e->nrv == RN_BLOWOUTSTARPIECE || e->nrv == RN_DPDATTACKED || e->nrv == RN_PRESS) return 0;
            if (e->nrv == RN_PRECLING || e->nrv == RN_CLING) kari_unregist(e);
            e->vel = vmul(vnorm(vsub(e->pos, from)), 30.0f);
            w_nrv(e, RN_BLOWOUT);
        }
    }
    return 1;
}

/* sendMsgExplosionToNearActor (cExplosionDistance 250): each walker's isMsgExplosionAttack */
static void walker_explosion(V3 from)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (e->state == KURIBO_DEAD || !row_shown(e->row) || vlen(vsub(e->pos, from)) > 250.0f) continue;
        walker_blow(e, from);
    }
}

static void update_kuribo(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->state == KURIBO_DEAD || !row_shown(k->row)) continue;
        k->up = up_at(k->pos, k->up);
        V3 to = vsub(mario->pos, k->pos);
        float d = vlen(to);
        if (k->kind == WALK_PETARI) { /* ponytail: still while its zone has no collision (streaming) */
            if (sc->objs[k->row].zone >= 64 || zone_kcl[sc->objs[k->row].zone]) petari_update(k);
            continue;
        }
        if (k->kind == WALK_KURIBO || k->kind == WALK_KMINI || k->kind == WALK_PACKUN) { /* ported after Petari (their own NonActive);
                                       * ponytail: still while its zone has no collision (streaming) */
            if (sc->objs[k->row].zone >= 64 || zone_kcl[sc->objs[k->row].zone]) {
                if (k->kind == WALK_KURIBO) kuribo_update(k);
                else if (k->kind == WALK_PACKUN) packun_update(k);
                else if (d < 3000.0f) kmini_update(k); /* ponytail: far ones wait (no clipping system) */
            }
            continue;
        }
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
/* MR::appearStarPiece (StarPieceDirector::appearPiece, StarPiece::launch): n star pieces from p, each 40 along `up`
 * plus up to 10 at random on each axis, falling (update_launched_pieces); gettable after 25 frames */
static void drop_pieces(V3 p, V3 up, int n) { launch_pieces(p, up, n, 10.0f, 40.0f); }
/* appearStarPieceToDirection: n pieces along dir at v, up to h at random on each axis */
static void launch_pieces(V3 p, V3 up, int n, float h, float v)
{
    if (!pieces_base) pieces_base = npieces;
    for (int k = 0; k < n; k++) {
        int slot = -1;
        for (int i = pieces_base; i < npieces && slot < 0; i++) if (pieces[i].alive == PICK_GONE && pieces[i].row < 0) slot = i;
        if (slot < 0) { if (npieces >= pieces_cap) return; slot = npieces++; }
        V3 rnd = vmul(v3((rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f, (rand() % 2001 - 1000) / 1000.0f), h);
        pieces[slot].pos = p;
        pieces[slot].row = -1;
        pieces[slot].alive = PICK_POP_NEW;
        piece_vel[slot] = vadd(vmul(vnorm(up), v), rnd);
        piece_t[slot] = 1;
    }
}

/* StarPiece::exeFall: x 0.97 then gravity 1 a frame; off the ground (binder 40) it keeps only its upward part, at
 * least 20 (it hops in place); taken after 25 frames (mGettableDelayCounter), gone after 600 or in a death area */
static int in_death(V3 p);

/* StarPieceFollowGroup.cpp: dead until its SW_A comes on (a SwitchCube); then, unless its SW_B is on, arg0 (6) star
 * pieces (in EggStar 2 at most 100 minus Mario's: isStageStarPieceFollowGroupLimit) appear on a ring of arg2 (350)
 * round it and follow Mario (FollowToPlayer): their axes eased 0.25 a frame to his side / front / up, each piece set to
 * his place + 10 x his velocity + its ring point (cos / sin of its angle, the ring turning 0.025 a frame) x (1 + 0.25
 * cos 3 x angle); taken at a touch. After arg1 (1200) frames FollowEnd: after 20 they drift out (0.05 x their ring
 * offset) and along his way (10); at 120 the rest vanish and its SW_A goes off; all taken but one: it dies.
 * ponytail: no StarPieceFlyingBlur effect; the pieces' velocity is their move (no binder, as in FollowPlayer) */
static int follow_limit;
void game_set_piece_follow_limit(int on) { follow_limit = on; }
static void update_follow_groups(void)
{
    int ty = scene_type(sc, "StarPieceFollowGroup");
    if (ty < 0) return;
    for (u32 i = 0; i < sc->nobjs; i++) {
        const Row *r = &sc->objs[i];
        if (r->type != ty || r->sw[SW_A] < 0 || row_dead[i]) continue;
        int k = -1;
        for (int j = 0; j < nfollows; j++) if (follows[j].row == (int)i) k = j;
        if (k < 0) {
            if (nfollows == MAX_FOLLOWS || !sw_on(r, SW_A)) continue;
            k = nfollows++;
            memset(&follows[k], 0, sizeof follows[0]);
            follows[k].row = (int)i;
        }
        typeof(&follows[0]) o = &follows[k];
        V3 side = vnorm(vcross(mario->up, mario->face)), up = mario->up, fr = mario->face;
        float rad = r->arg[2] != -1 ? (float)r->arg[2] : 350.0f;
        int limit = r->arg[1] != -1 ? r->arg[1] : 1200;
        if (!o->alive) { /* onSwitchA: beginFollowPieces */
            if (!sw_on(r, SW_A)) continue;
            if (r->sw[SW_B] >= 0 && sw_on(r, SW_B)) { row_dead[i] = 1; continue; }
            int n = r->arg[0] != -1 ? r->arg[0] : 6;
            if (follow_limit) { if (got_pieces >= 100) { row_dead[i] = 1; continue; } if (n > 100 - got_pieces) n = 100 - got_pieces; }
            if (n > FOLLOW_N) n = FOLLOW_N;
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
            V3 x = v3(m[0], m[1], m[2]), z = v3(m[8], m[9], m[10]);
            o->n = 0;
            if (!pieces_base) pieces_base = npieces;
            for (int j = 0; j < n; j++) { /* placementPiece */
                int slot = -1;
                for (int q = pieces_base; q < npieces && slot < 0; q++) if (pieces[q].alive == PICK_GONE && pieces[q].row < 0 && !piece_t[q]) slot = q;
                if (slot < 0) { if (npieces >= pieces_cap) break; slot = npieces++; }
                float a = 6.2831853f * j / n;
                pieces[slot].pos = vadd(row_pos(r), vadd(vmul(z, cosf(a) * rad), vmul(x, sinf(a) * rad)));
                pieces[slot].row = -1; pieces[slot].alive = PICK_ON; piece_t[slot] = 0;
                o->slot[o->n++] = slot;
            }
            o->alive = 1; o->nrv = 0; o->step = -1; o->angle = 0;
            printf("star piece follow group %u: %d pieces\n", i, o->n);
        }
        int st = ++o->step, left = 0;
        for (int j = 0; j < o->n; j++) left += pieces[o->slot[j]].alive != PICK_GONE;
        V3 mv = vadd(mario->hvel, vmul(mario->up, mario->vup));
        if (o->nrv == 0) { /* FollowToPlayer */
            if (st == 0) { o->x = side; o->z = fr; o->y = up; }
            V3 *ax[3] = {&o->x, &o->z, &o->y}, to[3] = {side, fr, up}; /* updateAxises 0.25 */
            for (int a = 0; a < 3; a++) {
                V3 c = vcross(*ax[a], to[a]);
                float d = vdot(*ax[a], to[a]);
                if (vlen(c) > 1e-6f) *ax[a] = vnorm(rot_axis(*ax[a], vnorm(c), acosf(d > 1 ? 1 : d < -1 ? -1 : d) * 0.25f));
            }
            V3 at = vadd(mario->pos, vmul(mv, 10.0f));
            float a = o->angle;
            int gone = 0;
            for (int j = 0; j < o->n; j++) {
                a += 6.2831853f / o->n;
                Pickup *pc = &pieces[o->slot[j]];
                if (pc->alive == PICK_GONE) { if (++gone == o->n - 1) left = 0; continue; }
                V3 v = vadd(vmul(o->x, cosf(a) * rad), vmul(o->z, sinf(a) * rad));
                o->off[j] = v;
                o->vel[j] = vsub(vadd(at, vmul(v, 1.0f + 0.25f * cosf(3.0f * a))), pc->pos);
                pc->pos = vadd(pc->pos, o->vel[j]);
            }
            o->angle += 0.025f;
            if (st > limit) { o->nrv = 1; o->step = -1; }
        } else { /* FollowEnd */
            V3 way = vlen(mv) > 1e-6f ? vnorm(mv) : v3(0, 0, 0);
            int gone = 0;
            for (int j = 0; j < o->n; j++) {
                Pickup *pc = &pieces[o->slot[j]];
                if (pc->alive == PICK_GONE) { if (++gone == o->n - 1) left = 0; continue; }
                if (st > 20) o->vel[j] = vadd(o->vel[j], vmul(o->off[j], 0.05f));
                o->vel[j] = vadd(o->vel[j], vmul(way, 10.0f));
                pc->pos = vadd(pc->pos, o->vel[j]);
            }
            if (st > 120) { /* kill, offSwitchA, allKillPieces */
                sw_set(r, SW_A, 0);
                for (int j = 0; j < o->n; j++) pieces[o->slot[j]].alive = PICK_GONE;
                left = 0;
            }
        }
        if (!left) { /* kill */
            for (int j = 0; j < o->n; j++) pieces[o->slot[j]].alive = PICK_GONE;
            o->alive = 0; o->n = 0;
            printf("star piece follow group %u over\n", i);
        }
    }
}

static void update_launched_pieces(void)
{
    for (int i = pieces_base; pieces_base && i < npieces; i++) {
        if (!piece_t[i] || pieces[i].alive == PICK_GONE) { piece_t[i] = 0; continue; }
        V3 up = up_at(pieces[i].pos, mario->up), g = vmul(up, -1.0f);
        piece_vel[i] = vadd(vmul(piece_vel[i], 0.97f), g);
        V3 c = vadd(pieces[i].pos, piece_vel[i]), gn = up, wn = up;
        int wall = 0, ground = collide_n(&c, up, 40.0f, &gn, &wn, &wall);
        pieces[i].pos = c;
        if (ground || wall) { /* reboundVelocityFromCollision 0.99 */
            float into = vdot(piece_vel[i], ground ? gn : wn);
            if (into < 0) piece_vel[i] = vsub(piece_vel[i], vmul(ground ? gn : wn, 1.99f * into));
            if (ground && !wall) piece_vel[i] = vmul(up, vdot(up, piece_vel[i]));
            if (vdot(piece_vel[i], piece_vel[i]) < 400.0f) piece_vel[i] = vmul(vlen(piece_vel[i]) > 1e-3f ? vnorm(piece_vel[i]) : up, 20.0f);
        }
        if (++piece_t[i] > 25 && pieces[i].alive == PICK_POP_NEW) pieces[i].alive = PICK_ON;
        if (piece_t[i] > 600 || in_death(pieces[i].pos)) { pieces[i].alive = PICK_GONE; piece_t[i] = 0; }
    }
}

/* What a WoodBox / CrystalCage(S / M) leaves (WoodBox::doHit / exeHit, CrystalCage::exeBreak): its break model's Break
 * clip (WoodBoxBreak, CrystalCageSBreak), its items at step 15 (the box; a cage pops its coin at once), then the
 * object itself on its BVA's broken frame for good (WoodBox without respawn, a cage with arg0 0), no collision */
static void update_wrecks(void)
{
    for (int i = 0; i < nwrecks; i++) {
        if (wrecks[i].t < 0) continue;
        if (++wrecks[i].t == 16) { /* exeHit step 15 */
            pop_coins(wrecks[i].p, wrecks[i].up, wrecks[i].coins);
            drop_pieces(wrecks[i].p, wrecks[i].up, wrecks[i].pieces);
        }
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
    const char *name = sc->types + 32 * r->type;
    int box = strcmp(name, "WoodBox") == 0, cage = strcmp(name, "CrystalCageS") == 0 || strcmp(name, "CrystalCageM") == 0;
    if ((box || cage) && nwrecks < MAX_WRECKS) {
        char v[48];
        snprintf(v, sizeof v, "%s#%s", name, name);
        typeof(&wrecks[0]) w = &wrecks[nwrecks++];
        memcpy(w->m, in->m, 64);
        w->brk = scene_model(sc, box ? "WoodBoxBreak" : "CrystalCageSBreak");
        w->left = (box ? r->arg[1] != 0 : r->arg[0] != 1) ? scene_model(sc, v) : NULL;
        w->t = 0; w->row = in->row; w->p = p; w->up = up;
        w->coins = box ? n : 0; w->pieces = box ? sp : 0;
        if (cage) { pop_coins(p, up, n); scene_stop = 3; } /* appearCoinPop at once; stopScene(3) at step 2 */
        fx_at(box ? FX_WOODBOXBREAK_BREAK : FX_CRYSTALCAGES_BREAK, p, up);
        row_kill(in->row);
        printf("%s %d broken, %d coins, %d star pieces\n", name, in->row, n, sp);
        return;
    }
    pop_coins(p, up, n);
    drop_pieces(p, up, sp); /* WoodBox arg2 */
    row_kill(in->row);
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
    for (int i = 0; i < ninsts; i++) /* SimpleBreakableObj::initCaseUseSwitchB: SW_B on breaks it */
        if (insts[i].breaks == 4 && !row_dead[insts[i].row] && sc->objs[insts[i].row].sw[SW_B] >= 0 &&
            sw_on(&sc->objs[insts[i].row], SW_B)) {
            break_inst(&insts[i], 1);
            game_rebind();
            return;
        }
    for (int i = 0; i < ninsts; i++)
        if (insts[i].breaks == 1 && (row_step[insts[i].row] & 0x7FFF)) row_step[insts[i].row]--;
    if (mario->spin_timer <= 0 && !hip_hit)
        return;
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->breaks || in->breaks >= 3) continue;
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
/* MapObjActor's map parts functions (rotator, rail mover): with SW_A they wait in exeWait until it comes on, then run
 * on; with SW_B they run only while it is on (listenStageSwitchOnOffB: start / endMapPartsFunctions); without either
 * they start at init. SideSpikeMoveStep's star pointer stop (WalkerStateBindStarPointer) is the 2P pointer's only
 * (isStarPointerPointing2POnPressButton), so a "SideSpikeMoveStepA" is its rail mover alone in one-player */
static int mp_running(const Row *r, int row)
{
    if (r->sw[SW_A] >= 0 && !row_mp_a[row]) {
        if (!sw_on(r, SW_A)) return 0;
        row_mp_a[row] = 1;
    }
    return r->sw[SW_B] < 0 || sw_on(r, SW_B);
}


/* PhantomShipHandle.cpp / MapPartsSeesaw1AxisRotator.cpp: turned about its Z by Mario's weight. Stay until he stands
 * on it, then each frame his torque about the axis (sign of (his place - its) x gravity) adds 0.001 x his distance
 * along its X / (0.1 x inertia: RotateAccelType, 1000 unset); after 2 frames the restoring force (RotateStopTime x
 * 0.01 towards its first up; slower than 0.1 and upright: stopped) and friction 0.99; clamped to RotateSpeed x 0.01
 * a frame; RotateAngle > 0 limits it (bounced back x -0.5). A hip drop on it adds 40 x that. Stopped: Stay. */
enum { SS_STAY, SS_MOVESTART, SS_MOVE, SS_HIPDROP };
static int seesaw_of(int row)
{
    for (int i = 0; i < nseesaws; i++) if (seesaws[i].row == row) return i;
    if (nseesaws == MAX_SEESAWS) return -1;
    memset(&seesaws[nseesaws], 0, sizeof seesaws[0]);
    seesaws[nseesaws].row = row; seesaws[nseesaws].step = -1; /* start(): Stay */
    return nseesaws++;
}
static void seesaw_matrix(Inst *in) /* its placement turned `angle` about its own Z */
{
    const Row *r = &sc->objs[in->row];
    int k = seesaw_of(in->row);
    float base[16], rz[16], a = k >= 0 ? seesaws[k].angle * 0.0174533f : 0;
    mat_translate_rot_scale(base, r->pos, r->rot, r->scale);
    mat_identity(rz);
    rz[0] = cosf(a); rz[1] = sinf(a); rz[4] = -sinf(a); rz[5] = cosf(a);
    mat_mul(in->m, base, rz);
}
static void update_seesaws(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->seesaw) continue;
        int k = seesaw_of(in->row);
        if (k < 0) continue;
        typeof(&seesaws[0]) o = &seesaws[k];
        const Row *r = &sc->objs[in->row];
        int packed = r->arg[7] > 0 ? r->arg[7] : 0, acc = packed >> 4 & 15, stop = packed >> 12 & 0x3FFF;
        float vmax = r->arg[4] != -1 ? r->arg[4] * 0.01f : 0, limit = r->arg[5] > 0 ? (float)r->arg[5] : 0;
        float inertia = 0.1f * (acc > 0 ? (float)acc : 1000.0f);
        V3 pos = v3(in->m[12], in->m[13], in->m[14]), ax = vnorm(v3(in->m[8], in->m[9], in->m[10]));
        V3 x = vnorm(v3(in->m[0], in->m[1], in->m[2])), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        V3 g = vmul(up_at(pos, v3(0, 1, 0)), -1.0f), off = vsub(mario->pos, pos);
        float dist = 0.001f * fabsf(vdot(x, off)), sgn = vdot(vcross(off, g), ax) < 0 ? -1.0f : 1.0f;
        int on = mario_ground == i;
        if (hip_hit && hip_inst == i) { o->nrv = SS_HIPDROP; o->step = -1; o->hip = 1; } /* tryHipDrop */
        int st = ++o->step;
        if (o->nrv == SS_STAY) {
            if (on) { o->nrv = SS_MOVESTART; o->step = -1; }
            continue;
        }
        if (o->nrv == SS_MOVESTART && fabsf(o->vel) > 0.1f) { o->nrv = SS_MOVE; o->step = -1; continue; }
        if (o->nrv == SS_HIPDROP && st == 0) o->vel += sgn * 40.0f * dist / inertia; /* addForceHipDrop */
        /* rotate: updateVelocity */
        if (on) o->vel += sgn * dist / inertia;
        if (o->nrv != SS_MOVESTART && st > 2) {
            V3 up0 = up_at(pos, v3(0, 1, 0)); /* ponytail: its first up taken as against gravity (placements are upright) */
            float force = vdot(vcross(up, up0), ax);
            if (fabsf(force) < 1e-6f && fabsf(o->vel) <= 0.1f) o->vel = 0;
            else o->vel += (force < 0 ? -1.0f : 1.0f) * 0.01f * stop / inertia;
            o->vel *= 0.99f;
        }
        o->vel = o->vel < -vmax ? -vmax : o->vel > vmax ? vmax : o->vel;
        if (o->nrv == SS_HIPDROP) { if (!o->hip) { o->nrv = SS_MOVE; o->step = -1; } o->hip = 0; }
        if (o->vel == 0) { o->nrv = SS_STAY; o->step = -1; continue; }
        if (limit > 0) { /* isGoingToReachTargetAngle */
            V3 nu = vadd(vmul(up, cosf(o->vel * 0.0174533f)), vmul(vcross(ax, up), sinf(o->vel * 0.0174533f)));
            float c = vdot(vnorm(nu), vmul(g, -1.0f));
            if (acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f >= limit) { o->vel *= -0.5f; continue; }
        }
        float old[16];
        memcpy(old, in->m, 64);
        o->angle += o->vel;
        seesaw_matrix(in);
        if (on) carry(old, in);
    }
}

static void update_rotors(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->rotor) continue;
        const Row *r = &sc->objs[in->row];
        int row = in->row, packed = r->arg[7], type = packed & 15, cond = packed >> 8 & 15, hold = packed >> 12 & 0x3FFF;
        if (!mp_running(r, row)) continue;
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
        if (!mp_running(r, row)) continue;
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
        Inst *in = &insts[i];
        if (in->anim && in->md && in->md->skin) {
            if (in->flip == 1) { /* init: startBck PanelB, its last frame */
                obj_play(in, "panelb", 0, 0);
                in->anim->frame = (float)anim_length(in->anim);
                in->flip = 2;
            }
            if ((*st & FLIP_DONE) && in->flip >= 2 && in->flip < 22 && ++in->flip == 22) obj_play(in, "panelend", 0, 0); /* EndPrepare 20, End */
        }
        if (*st & FLIP_DONE) continue;
        /* on it: grounded over its footprint (ground_inst may be the planet under a flush panel) */
        V3 d = vsub(mario->pos, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]));
        int on = mario->grounded && fabsf(vdot(d, mario->up)) < insts[i].reach * 0.5f && /* origin under the top */
                 vlen(vtangent(d, mario->up)) < 0.7f * insts[i].reach;
        if ((*st & FLIP_LOCK) && !on && mario->grounded) *st &= ~FLIP_LOCK;
        if ((*st & FLIP_LOCK) || !on) { *st &= FLIP_BACK | FLIP_LOCK; continue; }
        if (*st / FLIP_STEP < 2) { *st += FLIP_STEP; continue; }
        *st = ((*st & FLIP_BACK) ^ FLIP_BACK) | FLIP_LOCK;
        if (in->anim && in->md && in->md->skin) obj_play(in, (*st & FLIP_BACK) ? "panela" : "panelb", 0, 0); /* BackLand / FrontLand */
    }
}

/* CollapsePlane (Petari CollapsePlane.cpp): once stood on, the plane shrinks in x / z (1 - 0.7 step / time) and after
 * `time` frames vanishes with its collision (Vanish), for good. ponytail: the collision keeps its full size while
 * shrinking (the KCL takes uniform scale only), no star pointer stop */
static void update_collapse(void)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->collapse || !row_step[in->row]) continue;
        if (++row_step[in->row] >= in->collapse) {
            fx_at(FX_COLLAPSEPLANE_VANISH, v3(in->m[12], in->m[13], in->m[14]), vnorm(v3(in->m[4], in->m[5], in->m[6])));
            row_dead[in->row] = 1;
            game_rebind();
            return;
        }
    }
}

/* SpaceMine (Petari SpaceMine.cpp): sensor radius 100; touching it is an explosion on Mario and the mine is gone.
 * NeedlePlant (NeedlePlant.cpp): sensor 150 * scale above its base, radius 70 * scale; touching it pricks Mario and
 * it shakes (Shake); an enemy attack (a coconut, a rock, an explosion) breaks it (Break: a coin, arg0 0 three star
 * pieces: needle_attacked). BlackHole / BlackHoleCube (BlackHole.cpp): inside the sphere of radius 500 * scale, or the cube's box
 * of +-500 * scale, Mario is inhaled: a miss; walkers in it die (nothing dropped); SW_A kills it. ponytail: no star-piece
 * shots or invincibility kills, the mine does not spin, no inhale demo (actor camera, Disappear) */
/* NeedlePlant::receiveMsgEnemyAttack: an enemy attack within its sensor (180 x scale, 150 x scale up) breaks it */
static void needle_attacked(V3 p, float r)
{
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (in->hazard != 2 || !row_shown(in->row)) continue;
        V3 b = v3(in->m[12], in->m[13], in->m[14]), up = vnorm(v3(in->m[4], in->m[5], in->m[6]));
        V3 c = vadd(b, vmul(up, 150.0f * in->s));
        if (vlen(vsub(c, p)) > 180.0f * in->s + r) continue;
        fx_at(FX_NEEDLEPLANT_BREAK, c, up);
        int a0 = sc->objs[in->row].arg[0];
        if (a0 == 0) drop_pieces(b, up, 3); else if (a0 == -1) pop_coins(b, up, 1);
        row_kill(in->row);
        printf("needle plant %d breaks" "\n", in->row);
    }
}

static void update_hazards(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        if (!in->hazard) continue;
        V3 p = v3(in->m[12], in->m[13], in->m[14]);
        float r = 100.0f;
        if (in->hazard >= 3) { /* black hole: sphere 500 * scale, or Mario's sensor inside the cube's box */
            const Row *rw = &sc->objs[in->row];
            if (!row_shown(in->row)) continue;
            if (rw->sw[SW_A] >= 0 && sw_on(rw, SW_A)) { row_kill(in->row); continue; } /* exeWait: SW_A kills it */
            V3 x, z;
            row_axes(rw, &x, &z);
            for (int k = 0; k < nkuribo; k++) { /* ACTMES_INHALE_BLACK_HOLE: the walkers die, nothing dropped */
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row)) continue;
                V3 de = vsub(e->pos, p);
                int inside = in->hazard == 3 ? vlen(de) < 500.0f * rw->scale[0]
                                             : fabsf(vdot(de, x)) < 500.0f * rw->scale[0] && fabsf(vdot(de, vcross(z, x))) < 500.0f * rw->scale[1] &&
                                                   fabsf(vdot(de, z)) < 500.0f * rw->scale[2];
                if (!inside || e->kind == WALK_BUTTERFLY) continue;
                if (e->kind == WALK_KARIKARI && (e->nrv == RN_PRECLING || e->nrv == RN_CLING)) kari_unregist(e);
                e->state = KURIBO_DEAD; row_kill(e->row);
            }
            if (mario->launch >= 0) continue; /* carried by a launch star: bound, nothing inhales him */
            V3 d = vsub(c, p);
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
        if (vlen(vsub(c, p)) > r + HIT_BODY_R || (in->hazard == 2 && mario->hurt_timer)) continue;
        hurt_mario(p, in->hazard == 1 ? 25.0f : 15.0f);
        if (in->hazard == 2) obj_play(in, "shake", 0, 0); /* sendMsgEnemyAttack: Shake */
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

/* EarthenPipe::exeReady / PlayerIn / TargetPipeShowUp / PlayerOut: Mario bound through the pipes (his base matrix,
 * startBckPlayer EarthenPipeReady / In / Out / Jump); the jump out: endBindAndPlayerForceWeakGravityJumpInputOff */
static int in_water(V3 p, V3 up, float *depth);
static int mario_hidden;
static int pipe_bind(Player *m)
{
    if (!pipe_seq) return 0;
    const Pipe *p = &pipes[pipe_from], *o = &pipes[p->partner];
    pipe_t++;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    mario_hidden = 0;
    if (pipe_seq == 1) {
        int fr = anim_clip_info("earthenpipeready", NULL);
        float u = fr > 0 ? fminf((float)pipe_t / fr, 1.0f) : 1.0f; /* blendMtx by calcNerveRate(frame max) */
        V3 ce, cw, cu;
        float fovy;
        camera_pose(&ce, &cw, &cu, &fovy);
        V3 cf = vnorm(vtangent(vsub(ce, p->top), p->up));
        m->pos = vadd(vmul(pipe_p0, 1.0f - u), vmul(p->top, u));
        m->up = vnorm(vadd(vmul(pipe_u0, 1.0f - u), vmul(p->up, u)));
        m->face = vnorm(vtangent(vadd(vmul(pipe_f0, 1.0f - u), vmul(cf, u)), m->up));
        if (!mario_anim || (mario_anim->done && pipe_t > 1)) { pipe_seq = 2; pipe_t = 0; if (mario_anim) mario_play("earthenpipein", 1.0f, 0); }
    } else if (pipe_seq == 2) {
        if (!mario_anim || (mario_anim->done && pipe_t > 1)) {
            pipe_zone = o->zone;
            if (o->mode == 1 || o->mode == 2) { /* tryShowUp: the hidden partner rises (WaitToShowUp 20, ShowUp 30) */
                row_step[o->row] = 1;
                pipe_out = o->mode == 1 ? p->partner : -1;
                game_rebind();
                pipe_seq = 3;
            } else pipe_seq = 4;
            pipe_t = 0;
        }
    } else if (pipe_seq == 3) { /* exeTargetPipeShowUp: hidden until the partner is up */
        mario_hidden = 1;
        m->pos = o->top;
        if (pipe_t >= 50) { pipe_seq = 4; pipe_t = 0; }
    } else if (pipe_seq == 4) { /* exePlayerOut: at the partner's base matrix */
        if (pipe_t == 1 && mario_anim) mario_play("earthenpipeout", 1.0f, 0);
        m->pos = o->top; m->up = o->up;
        if (vlen(vtangent(o->front, o->up)) > 1e-3f) m->face = vnorm(vtangent(o->front, o->up));
        if (pipe_t > 1 && (!mario_anim || mario_anim->done)) {
            float depth;
            m->vup = o->vf;
            m->hvel = in_water(o->top, o->up, &depth) ? v3(0, 0, 0) : vmul(vtangent(o->front, o->up), o->hf);
            m->grounded = 0; m->jump_kind = 0; m->air_timer = 0; m->hip = 0;
            if (vlen(m->hvel) > 0.1f) m->face = vnorm(m->hvel);
            if (mario_anim) mario_play("earthenpipejump", 1.0f, 0);
            pipe_seq = 0; pipe_wait = 1;
        }
    }
    return 1;
}

static void update_pipes(void)
{
    if (pipe_seq) return;
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
        /* receiveOtherMsg AutoRushBegin: bound, Ready (from his matrix to the top, up the pipe, facing the camera) */
        pipe_seq = 1; pipe_from = i; pipe_t = 0;
        pipe_p0 = mario->pos; pipe_u0 = mario->up; pipe_f0 = mario->face;
        mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hip = 0;
        if (mario_anim) mario_play("earthenpipeready", 1.0f, 0);
        printf("pipe %d -> %d\n", i, p->partner);
        return;
    }
}

/* ElectricRail: sensors of radius 30 at the rail point nearest Mario (and every 100 above it for stacked rails) shock
 * him unless he squats; SW_A on makes it vanish (listenStageSwitchOnA: disappear). ponytail: no ElectricRailMoving, no
 * 20-frame shrink as it vanishes */
static int erail_on(const Spark *e)
{
    const Row *r = &sc->objs[e->row];
    return row_shown(e->row) && !(r->sw[SW_A] >= 0 && sw_on(r, SW_A));
}
static void update_electric(void)
{
    if (mario->squat) return;
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < nerails; i++) {
        const Spark *e = &erails[i];
        if (!erail_on(e)) continue;
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



/* ShootingStar.cpp: it appears arg2 (default 2000) up its y from its place, growing for 20 frames, then falls 25 a
 * frame spinning 10 degrees a frame; on the ground it bursts into arg0 star pieces (default 5) of the arg3 it holds
 * (default 5), hides for arg1 frames (default 240) and starts over until it holds none; after 360 frames of falling it
 * starts over. row_turn: 0 appearing, 1 falling, 2 waiting; row_step its frames; row_hold the pieces left. The burst
 * is appearStarPiece(arg0, 15, sLaunchYVel 40). ponytail: the binder's NoAction wall filter is not applied */
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
            launch_pieces(p, up, n, 15.0f, 40.0f);
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

/* PlantMember's clip lengths (Flower / FlowerBlue / CutBush archives): Shake 39 frames played from hShakeStartFrame 3
 * at hWeakRate 0.5 / hMiddleRate 1 / hStrongRate 1.5, HintShake 92 */
static int plant_clip_len(int nrv) { return nrv == PL_HINT ? 92 : nrv == PL_WEAK ? 72 : nrv == PL_MIDDLE ? 36 : 24; }

static void update_plants(void)
{
    V3 c = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int i = 0; i < ninsts; i++) /* groups whose ground was not loaded at their rebind: again while Mario is near */
        if (insts[i].plant && plant_first[insts[i].row] >= 0 && !plant_state[insts[i].row] &&
            vlen(vsub(c, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]))) < 4000.0f)
            place_plants(&insts[i]);
    for (int i = 0; i < ninsts; i++) {
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
        if (plant_state[in->row] != 1) continue;
        PlantGroupS *g = &pgroups[in->row];
        Plant *mb = &plants[plant_first[in->row]];
        for (int k = 0; k < n; k++) { /* PlantGroup::control: animControl, then the nerve */
            Plant *p = &mb[k];
            if (!p->ok) continue;
            if (p->item && ((p->nrv == PL_WEAK && p->touch == 1) ||
                            ((p->nrv == PL_MIDDLE || p->nrv == PL_STRONG) && (p->touch == 1 || p->touch == 2)))) {
                p->item = 0; /* generateItem: a coin pop, or a star piece (appearStarPiece 10, 40) */
                if (r->arg[2] == 1) drop_pieces(p->pos, p->up, 1);
                else pop_coins(p->pos, p->up, 1);
                printf("plant group %d: item out\n", in->row);
            }
            p->step++;
            if (p->nrv != PL_WAIT && p->step >= plant_clip_len(p->nrv)) { p->nrv = PL_WAIT; p->step = 0; } /* isBckStopped */
        }
        if (--g->hint_t <= 0) { /* emitHintEffect */
            g->hint_t = 300;
            int idx = (n + g->hint_i) % n;
            do {
                if (mb[idx].ok && mb[idx].item && mb[idx].nrv == PL_WAIT) {
                    mb[idx].nrv = PL_HINT; mb[idx].step = 0;
                    g->hint_i = (idx + n + 1) % n;
                    break;
                }
                idx = (idx + n + 1) % n;
            } while (idx != g->hint_i);
        }
        float gd = vlen(vsub(c, g->c));
        if (mario->spin_timer > 0 && mario->spin_t < 15 && gd < g->r + 600.0f) /* ACTMES_SPIN_STORM_RANGE: tryShake */
            for (int k = 0; k < n; k++) {
                Plant *p = &mb[k];
                if (!p->ok || (p->nrv >= PL_WEAK && p->step < 20)) continue; /* hIsShakableStep */
                float d = vlen(vtangent(vsub(c, p->pos), p->up));
                int nv = d < 100.0f ? PL_STRONG : d < 300.0f ? PL_MIDDLE : d < 500.0f ? PL_WEAK : -1;
                if (nv >= 0) { p->nrv = (short)nv; p->step = 0; }
            }
        V3 mv = vtangent(mario->vel, mario->up);
        if (gd < g->r + 100.0f && vdot(mv, mv) >= 2.0f) /* attackSensor: Mario's body sensor (100) moving: tryPush */
            for (int k = 0; k < n; k++) {
                Plant *p = &mb[k];
                if (!p->ok || (p->nrv >= PL_WEAK && p->step < 20)) continue;
                float rr = 100.0f + 50.0f * r->scale[1]; /* + sCollisionRadius x scale */
                if (vdot(vsub(c, p->pos), vsub(c, p->pos)) >= rr * rr) continue;
                p->touch = 1;
                p->nrv = p->nrv <= PL_HINT ? PL_WEAK : p->nrv == PL_WEAK ? PL_MIDDLE : PL_STRONG;
                p->step = 0;
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

/* RockCreator.cpp / Rock.cpp after Petari. The creator (arg0 speed, default 10; arg1 seconds between rocks, unset: a single
 * one; arg2 (Wanwan: arg5) breaks at the rail's goal; appear switch shows it, SW_A ends it for good) lets one out every
 * delay. A rock (radius 225 x scale): Appear (8 frames along its rail at 7, 45 for Wanwans, rocking 15, still 4 / 25),
 * AppearMoveInvalidBind (along the rail at its speed, rolling speed x 180 x 1.1 / (pi r) degrees a frame, on the ground
 * under it, 45 frames or while the next point's arg2 is set), Move (towards its rail's place under gravity 1.5 a frame,
 * 0.2 from a point with arg0 until it lands, at most 30; a point with arg1: MoveInvalidBind, on the rail without
 * collision). It breaks on a wall it runs into (dot -0.5), at the rail's goal (or vanishes), on what it touches that is
 * a rock; its body hurts Mario strongly (arg5: it slows down 150 frames); a spin on its weak sensor (125 at (0, 0, -150))
 * breaks a Rock (nine star pieces). Break: its RockBreak model's Break clip, then gone. R2 with the pointer on it
 * freezes it 20 frames. It blows the walkers it rolls over. ponytail: no Smoke trail (looping effect), no BTK size
 * frame, no fire ground, no shadow, no Gold star */
enum { RK_APPEAR, RK_APPEARMOVEINVALIDBIND, RK_MOVE, RK_MOVEINVALIDBIND, RK_BREAK, RK_FREEZE };
static float rock_scale(const RockC *c) { return sc->objs[c->row].scale[0]; }
static float rock_point_along(const RockC *c, int i) /* raw point i's distance along the rail */
{
    u32 np = *(const u32 *)(c->rail + 8);
    return *(const float *)(c->rail + 24 + 12 * np + 12 * *(const u32 *)(c->rail + 12) + 12 * i);
}
static int rock_point_arg(const RockC *c, int i, int which) /* point_arg0 / 1 / 2 of raw point i */
{
    u32 np = *(const u32 *)(c->rail + 8), nr = *(const u32 *)(c->rail + 12);
    i = i < 0 ? 0 : i >= (int)nr ? (int)nr - 1 : i;
    const unsigned char *rec = c->rail + 24 + 12 * np + 12 * nr;
    if (which == 0) return *(const int *)(rec + 12 * i + 4);
    return *(const int *)(rec + 12 * nr + 8 * i + 4 * (which - 1));
}
static int rock_point_no(const RockC *c, float coord) /* getCurrentRailPointNo */
{
    int nr = (int)*(const u32 *)(c->rail + 12), k = 0;
    for (int i = 0; i < nr; i++) if (rock_point_along(c, i) <= coord) k = i;
    return k;
}
static V3 rock_rail_pos(const RockC *c, float coord)
{
    if (c->closed) coord = fmodf(coord + c->len * 4.0f, c->len);
    return rail_at(c->pts, c->np, coord);
}
static V3 rock_rail_dir(const RockC *c, float coord)
{
    return vnorm(vsub(rock_rail_pos(c, coord + 5.0f), rock_rail_pos(c, coord - 5.0f)));
}
static void rock_move_on_rail(Rock *o, const RockC *c, float speed, float rot, int bind) /* moveOnRail */
{
    o->coord += speed;
    o->pos = rock_rail_pos(c, o->coord);
    o->rotx = fmodf(o->rotx + rot, 360.0f);
    if (bind) { /* getFirstPolyOnLineToMap 2 radii along gravity: on the ground */
        for (float t = 0; t <= 2.0f * o->rad; t += 20.0f) {
            Hit h[1];
            V3 q = vadd(o->pos, vmul(o->grav, t));
            if (kcl_query(q, 10.0f, h, 1)) { o->pos = vsub(q, vmul(o->grav, o->rad)); break; }
        }
    }
}
static void rock_break(Rock *o, const RockC *c, int pieces) /* setNerveBreak */
{
    V3 up = vmul(o->grav, -1.0f);
    if (pieces && c->type <= 1) drop_pieces(o->pos, up, 9); /* appearStarPieceToDirection cStarPieceNum */
    o->nrv = RK_BREAK; o->step = -1;
}
static int rock_reached_goal(Rock *o, const RockC *c) /* tryBreakReachedGoal */
{
    if (c->closed || o->coord < c->len) return 0;
    int brk = c->type == 0 ? sc->objs[c->row].arg[2] > 0 : sc->objs[c->row].arg[5] > 0;
    if (brk) { o->nrv = RK_BREAK; o->step = -1; } else o->alive = 0;
    return 1;
}
static void rock_appear(Rock *o, const RockC *c) /* Rock::appear */
{
    const Row *r = &sc->objs[c->row];
    V3 at = row_pos(r);
    o->alive = 1; o->fall = v3(0, 0, 0); o->air = 0; o->gravrate = 1.5f; o->railpt = -1; o->invbind = 0; o->rotx = 0;
    o->normalgrav = 0; o->slow = 0; o->ftime = 0;
    o->rad = 225.0f * rock_scale(c);
    o->rotspeed = c->speed * 180.0f * 1.1f / (3.14159265f * o->rad);
    o->grav = vmul(up_at(at, v3(0, 1, 0)), -1.0f);
    float best = 1e30f;                                         /* moveCoordAndTransToNearestRailPos */
    for (float d = 0; d <= c->len; d += 20.0f) {
        float dd = vlen(vsub(rail_at(c->pts, c->np, d), at));
        if (dd < best) { best = dd; o->coord = d; }
    }
    o->pos = o->prev = rock_rail_pos(c, o->coord);
    o->front = vnorm(vtangent(rock_rail_dir(c, o->coord), o->grav));
    if (!c->closed) { o->bind = 0; o->nrv = RK_APPEAR; } else { o->bind = 1; o->nrv = RK_MOVE; }
    o->step = 0;
}
static void update_rocks(void)
{
    V3 mc = vadd(mario->pos, vmul(mario->up, HIT_BODY_H));
    for (int ci = 0; ci < nrockcs; ci++) { /* RockCreator */
        RockC *c = &rockcs[ci];
        const Row *r = &sc->objs[c->row];
        if (c->valid && sw_on(r, SW_A)) { /* invalidate */
            for (int k = 0; k < c->nrocks; k++) rocks[c->first + k].alive = 0;
            c->valid = 0;
        }
        if (!c->valid) continue;
        if (!row_shown(c->row)) { c->active = 0; for (int k = 0; k < c->nrocks; k++) rocks[c->first + k].alive = 0; continue; }
        if (!c->active) { c->active = 1; c->timer = 0; } /* appear: Active */
        if (c->active == 1 && (c->delay < 0 || c->timer >= c->delay)) { /* exeActive: create */
            for (int k = 0; k < c->nrocks; k++)
                if (!rocks[c->first + k].alive) { rock_appear(&rocks[c->first + k], c); break; }
            if (c->delay < 0) c->active = 2; /* Deactive */
            c->timer = 0;
        }
        c->timer++;
    }
    for (int i = 0; i < nrocks; i++) {
        Rock *o = &rocks[i];
        if (!o->alive) continue;
        const RockC *c = &rockcs[o->c];
        float s = rock_scale(c), spd = c->speed;
        int st = o->step, appear_time = c->type == 0 ? 8 : 45;
        if (sc->objs[c->row].arg[3] != -1) appear_time = sc->objs[c->row].arg[3];
        /* control (before the nerve: Petari's control runs after it, the velocity it sets is the binder's next) */
        if (o->nrv != RK_BREAK) {
            V3 f = vsub(o->pos, o->prev);
            if (vlen(f) > 1e-3f) {
                f = vnorm(f); o->prev = o->pos;
                if (o->nrv <= RK_MOVEINVALIDBIND) o->front = turn_to(o->front, f, 0.999f);
            }
            if (o->on_ground) o->fall = v3(0, 0, 0);
        }
        int pointed = pointer_a() && cursor_sees(o->pos, 60.0f);
        switch (o->nrv) {
        case RK_APPEAR: {
            float rs = 7.0f * 180.0f * 1.1f / (o->rad * 3.14159265f);
            int stop = c->type == 0 ? 4 : 25;
            if (st == 0) o->rotx = fmodf(-rs * appear_time, 360.0f);
            if (st < appear_time) rock_move_on_rail(o, c, 7.0f, rs, 0);
            if (st > appear_time && st < appear_time + 15) {
                int u = st - appear_time;
                o->rotx = (15 - u) * sinf(fmodf(u * 100.0f, 360.0f) * 0.0174533f) * 5.0f / 15.0f;
            }
            if (st == stop + appear_time + 15) { o->nrv = RK_APPEARMOVEINVALIDBIND; o->step = -1; }
            break;
        }
        case RK_APPEARMOVEINVALIDBIND: {
            int nr = (int)*(const u32 *)(c->rail + 12), cur = rock_point_no(c, o->coord);
            int inv = cur + 1 < nr && rock_point_arg(c, cur + 1, 2) > 0; /* isForceInvalidBindSection */
            if (pointed) { o->fpos = o->pos; o->ftime = 0; o->unfreeze = o->nrv; o->nrv = RK_FREEZE; o->step = -1; break; }
            rock_move_on_rail(o, c, spd, o->rotspeed, inv);
            if (!inv && ++o->invbind > 45) { o->bind = 1; o->nrv = RK_MOVE; o->step = -1; }
            break;
        }
        case RK_MOVE: {
            if (o->on_ground) {
                if (o->air >= 20) fx_at(c->type == 0 ? FX_ROCK_LANDATTRDEFAULT : FX_WANWANROLLING_LAND, vadd(o->pos, vmul(o->grav, o->rad)), vmul(o->grav, -1.0f));
                o->air = 0;
                if (o->normalgrav && o->gravrate != 1.5f) o->gravrate = 1.5f;
            } else {
                if (o->air < 20) o->air++;
                o->normalgrav = 1;
            }
            if (o->on_wall && vdot(rock_rail_dir(c, o->coord), o->wn) < -0.5f) { /* isBreakByWall */
                if (c->type == 0) { o->nrv = RK_BREAK; o->step = -1; } else o->alive = 0;
                break;
            }
            if (rock_reached_goal(o, c)) break;
            float ms = spd;
            if (o->slow > 0) { float u = (float)o->slow / 150.0f; ms = spd * (1.0f - (1.0f - u) * (1.0f - u)); o->slow--; } /* getEaseOutValue */
            int prev = o->railpt;                                  /* move */
            o->coord += ms;
            o->railpt = rock_point_no(c, o->coord);
            if (prev != o->railpt) {
                if (rock_point_arg(c, o->railpt, 0) >= 0) { o->normalgrav = 0; o->gravrate = 0.2f; }
                if (rock_point_arg(c, o->railpt, 1) >= 0) { o->nrv = RK_MOVEINVALIDBIND; o->step = -1; break; }
            }
            float rot = o->rotspeed * ms / spd;
            if (o->slow > 130) {
                int u = 150 - o->slow;
                rot += (20 - u) * sinf(fmodf(u * 100.0f, 360.0f) * 0.0174533f) * 5.0f / 20.0f;
            }
            o->rotx = fmodf(o->rotx + rot, 360.0f);
            if (pointed) { o->fpos = o->pos; o->ftime = 0; o->unfreeze = o->nrv; o->nrv = RK_FREEZE; o->step = -1; break; }
            /* control: towards the rail's place, falling */
            o->vel = vtangent(vsub(rock_rail_pos(c, o->coord), o->pos), o->grav);
            o->fall = vadd(o->fall, vmul(o->grav, o->gravrate));
            o->vel = vadd(o->vel, o->fall);
            if (vlen(o->vel) > 30.0f) o->vel = vmul(vnorm(o->vel), 30.0f);
            break;
        }
        case RK_MOVEINVALIDBIND:
            if (st == 0) o->bind = 0;
            if (!rock_reached_goal(o, c)) rock_move_on_rail(o, c, spd, o->rotspeed, 0);
            break;
        case RK_BREAK:
            if (st == 0) { /* the break model where it is, turned at random about gravity */
                o->bpos = o->pos; o->bup = vmul(o->grav, -1.0f);
                o->bfront = rot_axis(o->front, o->bup, (rand() % 6283) / 1000.0f);
                o->bt = 0;
                if (!(c->type == 0 ? m_rockbreak : m_wanwanbreak)) { o->alive = 0; break; }
            }
            { int fr = anim_clip_info(c->type == 0 ? "rockbreak/break" : "wanwanrollingbreak/break", NULL);
              if (++o->bt >= (fr > 0 ? fr : 60)) o->alive = 0; }
            break;
        case RK_FREEZE: {
            o->ftime++;
            V3 ce, cw, cu;
            float fovy;
            camera_pose(&ce, &cw, &cu, &fovy);
            V3 camx = vnorm(vcross(vnorm(vsub(cw, ce)), cu));
            if (st == 0) fx_at(c->type == 0 ? FX_ROCK_TOUCH : FX_WANWANROLLING_TOUCH, o->pos, vmul(o->grav, -1.0f));
            o->pos = vadd(o->fpos, vmul(camx, 5.0f * cosf(fmodf(o->ftime * 75.0f, 360.0f) * 0.0174533f) * (20 - st) / 20.0f));
            if (pointed) { o->step = -1; break; }
            if (st == 20) { o->pos = o->fpos; o->nrv = o->unfreeze; o->step = -1; }
            o->vel = v3(0, 0, 0);
            break;
        }
        }
        o->step++;
        if (!o->alive || o->nrv == RK_BREAK) continue;
        /* binder (Move only: the others place it themselves) */
        if (o->nrv == RK_MOVE && o->bind) {
            V3 cpos = vadd(o->pos, o->vel), gn = vmul(o->grav, -1.0f), wn = gn;
            int wall = 0;
            o->on_ground = collide_n(&cpos, vmul(o->grav, -1.0f), o->rad, &gn, &wn, &wall);
            o->gn = gn; o->on_wall = wall; o->wn = wn;
            o->pos = cpos;
        } else { o->on_ground = 0; o->on_wall = 0; }
        if (lost(o->pos)) { o->alive = 0; continue; }
        /* sensors: body (225 x scale; Mini 0.3) and the Rock's weak one (125 at (0, 0, -150), both x scale) */
        float br = 225.0f * (c->type == 2 ? 0.3f : s);
        if (o->nrv != RK_APPEAR && c->type == 0) {
            V3 side = vcross(vmul(o->grav, -1.0f), o->front);
            (void)side;
            V3 weak = vadd(o->pos, vmul(o->front, -150.0f * s));
            if (mario_offence(weak, 125.0f * s, o->grav) == MSG_HITALL) { rock_break(o, c, 1); continue; }
        }
        if (vlen(vsub(mc, o->pos)) < br + HIT_BODY_R && !mario->hurt_timer) {
            if (o->nrv != RK_FREEZE && o->nrv != RK_APPEAR) {
                hurt_mario(o->pos, 25.0f);                         /* sendMsgEnemyAttackStrong */
                if (sc->objs[c->row].arg[5] > 0 && c->type == 0 && o->nrv == RK_MOVE) o->slow = 150;
            }
        }
        if (o->nrv != RK_FREEZE) needle_attacked(o->pos, br);
        if (o->nrv != RK_FREEZE) /* sendMsgToEnemyAttackBlowOrTrample */
            for (int k = 0; k < nkuribo; k++) {
                Kuribo *e = &kuribo[k];
                if (e->state == KURIBO_DEAD || !row_shown(e->row) || vlen(vsub(vadd(e->pos, vmul(e->up, 50.0f)), o->pos)) > br + 60.0f) continue;
                walker_blow(e, o->pos);
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
/* BenefitItemObj (BenefitItemOneUp / LifeUp) after Petari: arg6 turns it pi / 60 a frame about its up in Wait;
 * arg1 1 (appearGround): when its appear switch brings it out, hidden 60 frames, then Appear (its clip), then Wait or,
 * with arg2, PreEscape (30) and Escape: 7 along its Z, kept on the ground (its gravity its placed down: offCalcGravity)
 * at 5, 1 a frame (at most 20) in the air with x 0.95, off a wall it jumps 20; after 600 frames blinking faster it
 * vanishes. row_turn 0 wait, 1 appear ground, 2 pre-escape, 3 escape, 4 gone; row_step its frames.
 * ponytail: arg2 2 (fleeing Mario) runs as 1, no shadow, no Glow / Move / Vanish effects */
static void benefit_update(Inst *in)
{
    const Row *r = &sc->objs[in->row];
    signed char *ph = &row_turn[in->row];
    unsigned short *t = &row_step[in->row];
    V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), g = vmul(up, -1.0f);
    if (*ph == 0 && *t == 0 && r->sw[SW_APPEAR] >= 0 && (r->arg[1] & 0xF) == 1 && r->arg[1] != -1) { *ph = 1; } /* appear(): appearGround */
    ++*t;
    in->invisible = 0;
    switch (*ph) {
    case 0:
        if (r->arg[6] > 0) { /* doRotateY: pi / 60 a frame about its up */
            V3 x = v3(in->m[0], in->m[1], in->m[2]), z = v3(in->m[8], in->m[9], in->m[10]);
            x = rot_axis(x, up, 3.14159265f / 60.0f); z = rot_axis(z, up, 3.14159265f / 60.0f);
            in->m[0] = x.x; in->m[1] = x.y; in->m[2] = x.z; in->m[8] = z.x; in->m[9] = z.y; in->m[10] = z.z;
        }
        break;
    case 1: /* exeAppearGround */
        if (*t < 61) { in->invisible = 1; break; }
        if (*t == 61) obj_play(in, "appear", 0, 0);
        if (*t > 62 && in->anim && in->anim->done) { *ph = r->arg[2] > 0 ? 2 : 0; *t = 0; }
        break;
    case 2: if (*t >= 31) { *ph = 3; *t = 0; } break; /* exePreEscape */
    case 3: { /* exeEscape: doEscape */
        V3 x, z;
        row_axes(r, &x, &z);
        if (*t == 1) { in->head = vmul(vnorm(z), 7.0f); in->hvel = v3(0, 0, 0); in->cstrong = 0; } /* initEscape: _BC, _C8, _E5 */
        V3 *bc = &in->head, *c8 = &in->hvel, pos = v3(in->m[12], in->m[13], in->m[14]);
        V3 center = vadd(pos, vmul(up, 50.0f)), gn = up, wn = up;
        int wall = 0, ground = collide_n(&center, up, 50.0f, &gn, &wn, &wall);
        (void)center;
        if (ground) {
            float l = vlen(*bc);
            *bc = vmul(vnorm(vtangent(*bc, g)), l);
            if (vlen(*bc) > 7.0f) *bc = vmul(vnorm(*bc), 7.0f);
            if (in->cstrong) { in->cstrong = 0; obj_play(in, "land", 0, 0); }
            *c8 = vmul(g, 5.0f);
        } else {
            *bc = vmul(*bc, 0.95f); in->cstrong = 1;
            *c8 = vadd(*c8, g);
            if (vlen(*c8) > 20.0f) *c8 = vmul(vnorm(*c8), 20.0f);
        }
        V3 vel = vadd(*bc, *c8);
        if (wall && !in->cstrong && vdot(wn, vel) < 0.0f) {
            *bc = vsub(vtangent(*bc, wn), vmul(wn, vdot(*bc, wn)));
            *c8 = vadd(*c8, vmul(g, -20.0f));
            in->cstrong = 1;
            vel = vadd(*bc, *c8);
        }
        V3 c = vadd(vadd(pos, vel), vmul(up, 50.0f));
        collide(&c, up, 50.0f);
        pos = vsub(c, vmul(up, 50.0f));
        in->m[12] = pos.x; in->m[13] = pos.y; in->m[14] = pos.z;
        int left = 600 - *t; /* _B0 and its blink pattern: from 180 4 of 12 hidden, 120 3 of 8, 60 2 of 4 */
        if (left <= 0) { *ph = 4; row_kill(in->row); break; }
        if (left <= 180) { int per = left <= 60 ? 4 : left <= 120 ? 8 : 12, off = left <= 60 ? 2 : left <= 120 ? 3 : 4; in->invisible = left % per < off; }
        break;
    }
    }
}

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
        if (in->item == ITEM_ONEUP || in->item == ITEM_LIFEUP) { benefit_update(in); if (in->invisible && row_turn[in->row] == 1) continue; }
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
    float x = pad_rx(), y = pad_ry(), k = 6.0f + 10.0f * sqrtf(x * x + y * y), px = cur_x, py = cur_y;
    cur_x += x * k; cur_y -= y * k;
    if (pad_pressed(PAD_R3)) { cur_x = 320; cur_y = 224; }
    if (pad_pressed(PAD_L1) || test_l1) { test_l1 = 0; shoot_piece(); }
    update_shots();
    cur_x = cur_x < 0 ? 0 : cur_x > 639 ? 639 : cur_x;
    cur_y = cur_y < 0 ? 0 : cur_y > 447 ? 447 : cur_y;
    cur_vx = cur_x - px; cur_vy = cur_y - py;
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
            if (e->kind == WALK_TAKOBO) { /* isMsgStarPieceAttack: StunStart (not while pressed / blown) */
                if (e->nrv != TN_PRESS && e->nrv != TN_HITPUNCH) w_nrv(e, TN_STUNSTART);
            } else if (e->kind == WALK_KURIBO || e->kind == WALK_KMINI) { /* requestStagger (if it can be punched) */
                if (!kn_in(e, KNB(KN_FLATDOWN) | KNB(KN_HIPDROPDOWN) | KNB(KN_PRESSDOWN) | KNB(KN_BLOWDOWN) | KNB(KN_BLOW))) {
                    ws_stagger_punch(e, s->pos); w_nrv(e, KN_STAGGER); ap_stagger(e);
                }
            }
            else if (e->kind == WALK_MKOOPA) { e->phase = 6; e->timer = 0; }
            else if (e->kind == WALK_KARON) { if (e->phase != 2) { e->phase = 3; e->timer = 0; } }
            else if (e->kind == WALK_BUTTERFLY || e->kind == WALK_TERESA) { hit = 0; continue; }
            else if (e->kind == WALK_PETARI) { /* isMsgStarPieceAttack: a hit reaction; Swoon starts over */
                if (e->phase) { hit = 0; continue; }
                if (e->nrv == PT_SWOON) w_nrv(e, PT_SWOON);
            }
            else if (e->kind == WALK_UNIZO || e->kind == WALK_POIHANA) { /* isMsgStarPieceAttack: nothing more */ }
            else if (e->kind == WALK_STINKBUG) { if (e->phase != 2 && e->phase != 6) { e->phase = 5; e->timer = 0; } }
            else if (e->kind == WALK_KARIKARI) kari_blowout(e, s->pos, 1); /* isMsgStarPieceAttack: BlowOutStarPiece */
            else if (e->kind == WALK_PACKUN) { /* isMsgStarPieceAttack: SwoonStart */
                if (e->nrv != PN_TRAMPLEDOWN && e->nrv != PN_PUNCHDOWN) w_nrv(e, PN_SWOONSTART);
            } else if (e->kind != WALK_SPIDER) { e->state = KURIBO_STUN; e->timer = 180; }
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
        if (!hit && hhp_reflects(s->pos, 30.0f)) hit = 1; /* HammerHeadPackun: isMsgStarPieceReflect */
        for (int k = 0; k < ninsts && !hit; k++) { /* ItemBubble: isMsgStarPieceAttack bursts it */
            Inst *in = &insts[k];
            if (!in->ibubble || !row_shown(in->row) || vlen(vsub(s->pos, bubble_pos(in))) > 120.0f * 1.2f * sc->objs[in->row].scale[0]) continue;
            bubble_burst(in);
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
        snprintf(n, sizeof n, "%s/%s", cls, cls); /* tryStartAllAnim: the clip of its own name, as its BCK loops */
        for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (anim_start(a, n, 0)) return oanim[k].a;
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
            if (insts[i].punch) { anim_pose(insts[i].anim); punch_ball(&insts[i], insts[i].anim); anim_skin(insts[i].anim); continue; }
            if (insts[i].anim->done && !insts[i].anim->loop && !insts[i].vine && !obj_play(&insts[i], sc->types + 32 * sc->objs[insts[i].row].type, 1, 0))
                obj_play(&insts[i], "wait", 1, 8); /* a one-shot clip over: back to its own clip (tryStartAllAnim), else Wait */
            anim_update(insts[i].anim);
        }
}

void game_update(void)
{
    if (scene_stop > 0) { scene_stop--; return; } /* SceneFunction stopScene: everything holds */
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
    update_seesaws(); /* after Mario: his hip drop (hip_hit) this frame */
    if (mario->spin_timer == SPIN_TIME - 1) mario_fx(FX_MARIO_SPINLIGHT, 1.0f); /* a spin started this frame */
    if (hip_hit) mario_fx(FX_MARIO_HIPDROPFLOWER, 0.0f);
    update_launched_pieces();
    update_follow_groups();
    update_wrecks();
    update_kuribo();
    kari_director();
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
    update_kgunners();
    update_orocks();
    update_hhps();
    update_vines();
    update_ibubbles();
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
    demo_on = dino_demo_on || ota_demo_on || star_demo || fat_demo() || vine_demo_on();
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
    coin_rot = fmodf(coin_rot + 8.0f * 0.0174533f, 6.2831853f);
    qcoin_rot = fmodf(qcoin_rot + 4.0f * 0.0174533f, 6.2831853f);
    piece_rot = fmodf(piece_rot + 15.0f * 0.0174533f, 6.2831853f);
    star_rot = fmodf(star_rot + 3.0f * 0.0174533f, 6.2831853f);
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

static void draw_pickups(const Pickup *p, int n, const Model *md, u8 r, u8 g, u8 b, float rot)
{
    float m[16];
    if (!md) return;
    for (int i = 0; i < n; i++) {
        if (!pickup_shown(&p[i])) continue;
        V3 up = up_at(p[i].pos, v3(0, 1, 0));
        basis_matrix(m, p[i].pos, up, v3(0, 0, 1), rot);
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
    for (int i = 0; i < ninsts; i++) { /* BrightSun::control / controlSunModel: the Sun at the camera + its rotation's
                                         * z x 100000, scale 100, turned from +z to the camera (setRotate: the shortest
                                         * turn, Y 180 when opposite). ponytail: no lens flare / glare (LensFlare) */
        const Inst *in = &insts[i];
        if (!in->sun || !row_shown(in->row)) continue;
        V3 z = vnorm(v3(in->m[8], in->m[9], in->m[10])), pos = vadd(ce, vmul(z, 100000.0f)), d = vmul(z, -1.0f);
        float vx = -d.y, vy = d.x, k = d.z < -0.999f ? 0.0f : 1.0f / (1.0f + d.z), cm[16] = {0};
        if (d.z < -0.999f) { cm[0] = -1; cm[5] = 1; cm[10] = -1; }
        else {
            cm[0] = 1 - k * vy * vy; cm[1] = k * vx * vy; cm[2] = -vy;
            cm[4] = k * vx * vy; cm[5] = 1 - k * vx * vx; cm[6] = vx;
            cm[8] = vy; cm[9] = -vx; cm[10] = 1 - k * (vx * vx + vy * vy);
        }
        for (int j = 0; j < 12; j++) cm[j] *= 100.0f;
        cm[12] = pos.x; cm[13] = pos.y; cm[14] = pos.z; cm[15] = 1;
        inst_draw(in, in->md, cm);
        sky_drawn = 1;
    }
    if (sky_drawn) render_clear_depth(); /* the sky stays behind whatever its scale put in front */
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (in->invisible || in->sky || in->sun) continue;
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
        if (in->flip && in->md->skin) { inst_draw(in, in->md, in->m); continue; } /* turned by its clips */
        if (in->flip && in->md_flip[0]) { /* the face PanelA / PanelB / PanelEnd leave shown (static: no turn) */
            int turned = (row_step[in->row] & FLIP_BACK) != (strcmp(sc->types + 32 * sc->objs[in->row].type,
                                                                    "FlipPanelReverse") == 0);
            const Model *md = (row_step[in->row] & FLIP_DONE) && in->md_flip[1] ? in->md_flip[1] :
                              turned ? in->md_flip[0] : in->md;
            inst_draw(in, md, in->m);
            continue;
        }
        if (in->ibubble) { /* ItemBubble::calcAndSetBaseMtx: Z to the camera, Y the camera's up; its items inside */
            if (row_dead[in->row]) continue;
            const Row *r = &sc->objs[in->row];
            V3 p = bubble_pos(in), ce, cw, cu, z = vnorm(vsub(cam_pos, p));
            float fovy, cm[16], k = 1.2f * r->scale[0];
            camera_pose(&ce, &cw, &cu, &fovy);
            V3 x = vnorm(vcross(cu, z)), y = vcross(z, x);
            float ax[3][3] = {{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}};
            mat_identity(cm);
            for (int a = 0; a < 3; a++) for (int j = 0; j < 3; j++) cm[4 * a + j] = ax[a][j] * k;
            cm[12] = p.x; cm[13] = p.y; cm[14] = p.z;
            V3 off[16];
            int n = bubble_items(r, off);
            const Model *im = r->arg[0] == 1 ? m_piece : m_coin;
            for (int q = 0; im && q < n; q++) {
                float am[16];
                basis_matrix(am, bubble_item_pos(in, off[q]), v3(0, 1, 0), v3(0, 0, 1), row_step[in->row] * 8.0f * 0.0174533f);
                if (r->arg[0] == 1) render_model(im, am, 0x60, 0xD0, 0xF0);
                else render_model(im, am, 0xF0, 0xD0, 0x30);
            }
            inst_draw(in, in->md, cm);
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
            float cm[16], rz[16], tm[16];
            V3 kup = vlen(k->up) > 0.5f ? k->up : up_at(k->pos, v3(in->m[4], in->m[5], in->m[6]));
            basis_matrix(tm, k->pos, vnorm(vtangent(kup, k->front)), k->front, 0); /* makeMtxFrontUpPos, then rotation z */
            mat_identity(rz);
            rz[0] = rz[5] = cosf(k->rotz * 0.0174533f); rz[1] = sinf(k->rotz * 0.0174533f); rz[4] = -rz[1];
            mat_mul(cm, tm, rz);
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
            rot[0] = rot[10] = cosf(star_rot);
            rot[8] = sinf(star_rot); rot[2] = -rot[8];
            mat_mul(cm, in->m, rot);
            int state;
            V3 p = star_pos(in, &state);
            if (!state) continue;
            cm[12] = p.x; cm[13] = p.y; cm[14] = p.z;
            inst_draw(in, in->md, cm);
        } else if (in->item == ITEM_QCOIN) { /* its base matrix x the CoinRotater's in-water Y turn */
            float rot[16], cm[16];
            mat_identity(rot);
            rot[0] = rot[10] = cosf(qcoin_rot);
            rot[8] = sinf(qcoin_rot); rot[2] = -rot[8];
            mat_mul(cm, in->m, rot);
            inst_draw(in, in->md, cm);
        } else
            inst_draw(in, in->md, in->m);
    }
    for (int i = 0; i < nwrecks; i++) { /* wrecks: the break model's clip until it stops, the broken object for good */
        typeof(&wrecks[0]) w = &wrecks[i];
        if (w->left) render_model(w->left, w->m, 0x80, 0x80, 0x80);
        if (w->t < 0 || !w->brk) continue;
        if (!w->brk->skin) { if (w->t > 60) w->t = -1; else render_model(w->brk, w->m, 0x80, 0x80, 0x80); continue; }
        int box = w->brk == scene_model(sc, "WoodBoxBreak");
        Anim *ba = obj_anim((int)sc->nobjs * 3 + i, w->brk, box ? "WoodBoxBreak" : "CrystalCageSBreak");
        if (!ba) continue;
        if (w->t <= 1) anim_start(ba, box ? "woodboxbreak/break" : "crystalcagesbreak/break", 0);
        anim_update(ba);
        if (ba->done && w->t > 16) { w->t = -1; continue; } /* isBckStopped: kill */
        render_skinned(w->brk, w->m, (const float (*)[16])ba->drw);
    }
    for (int i = 0; i < nrocks; i++) { /* Rock::calcBaseMtx: up against gravity, its front, turned rotx about its x */
        Rock *o = &rocks[i];
        if (!o->alive) continue;
        const RockC *c = &rockcs[o->c];
        float s3 = rock_scale(c), cm[16], rm[16];
        if (o->nrv == RK_BREAK) {
            const Model *bm = c->type == 0 ? m_rockbreak : m_wanwanbreak;
            if (!bm) continue;
            basis_matrix(m, o->bpos, o->bup, o->bfront, 0);
            for (int j = 0; j < 12; j++) if (j % 4 != 3) m[j] *= s3;
            if (bm->skin) {
                Anim *ba = obj_anim((int)sc->nobjs * 2 + i, bm, c->type == 0 ? "RockBreak" : "WanwanRollingBreak");
                if (!ba) continue;
                if (o->bt <= 1) { anim_start(ba, c->type == 0 ? "rockbreak/break" : "wanwanrollingbreak/break", 0); }
                anim_update(ba);
                render_skinned(bm, m, (const float (*)[16])ba->drw);
            } else render_model(bm, m, 0x80, 0x80, 0x80);
            continue;
        }
        const Model *md = c->type == 0 ? m_rock : c->type == 2 ? m_wanwanmini : m_wanwan;
        if (!md) continue;
        basis_matrix(m, o->pos, vmul(o->grav, -1.0f), o->front, 0);
        float a = o->rotx * 0.0174533f;
        mat_identity(rm);
        rm[5] = cosf(a); rm[6] = sinf(a); rm[9] = -rm[6]; rm[10] = rm[5];
        mat_mul(cm, m, rm);
        for (int j = 0; j < 12; j++) if (j % 4 != 3) cm[j] *= s3;
        render_model(md, cm, 0x80, 0x80, 0x80);
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
        if (!erail_on(e)) continue; /* exeDisappeared: hideModel */
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
    draw_pickups(coins, ncoins, m_coin, 0xF0, 0xD0, 0x30, coin_rot);
    draw_pickups(purple, npurple, m_purple, 0xB0, 0x50, 0xF0, coin_rot);
    draw_pickups(pieces, npieces, m_piece, 0x60, 0xD0, 0xF0, piece_rot);
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
                          kuribo[i].kind == WALK_UNIZO ? m_unizo[kuribo[i].dir] : kuribo[i].kind == WALK_POIHANA ? m_poihana : kuribo[i].kind == WALK_PETARI ? m_petari : m_kuribo;
        if (kuribo[i].state == KURIBO_DEAD || !md || !row_shown(kuribo[i].row)) continue;
        if (kuribo[i].kind == WALK_TERESA && kuribo[i].phase == 4) continue; /* hidden in a wall */
        if (kuribo[i].kind == WALK_PETARI && kuribo[i].phase) continue; /* hideModel */
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
            Kuribo *k = &kuribo[i];
            const char *cls = sc->types + 32 * sc->objs[k->row].type;
            Anim *a = obj_anim(k->row, md, cls);
            if (!a) continue;
            if (k->act[0]) { /* ported walkers: the action their nerves started (startAction), at _A8 and its scale */
                char n[48];
                snprintf(n, sizeof n, "%s/%s", cls, k->act);
                for (char *c = n; *c; c++) *c = (char)tolower((unsigned char)*c);
                if (k->act_drawn != k->act_seq || a->clip_id < 0 || strcmp(sc->clips[a->clip_id].name, n)) { anim_start(a, n, k->act_interp); k->act_drawn = k->act_seq; }
                a->speed = k->act_rate; /* setBckRate */
                anim_update(a);
                basis_matrix(m, k->pos, k->qy, k->qz, 0);
                const float *sc3 = sc->objs[k->row].scale;
                for (int j = 0; j < 3; j++) { m[j] *= sc3[0]; m[4 + j] *= sc3[1]; m[8 + j] *= sc3[2]; }
                render_skinned(md, m, (const float (*)[16])a->drw);
                if (k->kind == WALK_PACKUN && k->bshow == 1 && m_packunhead && m_packunhead->skin) { /* mBlownModel: BlowHead */
                    Anim *ha = obj_anim((int)sc->nobjs + k->row, m_packunhead, "PackunPetitHead");
                    if (ha) {
                        anim_update(ha);
                        basis_matrix(m, k->bpos, k->qy, k->qz, 0);
                        for (int j = 0; j < 3; j++) { m[j] *= sc3[0]; m[4 + j] *= sc3[1]; m[8 + j] *= sc3[2]; }
                        render_skinned(m_packunhead, m, (const float (*)[16])ha->drw);
                    }
                }
                continue;
            }
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
    if (ota.row >= 0 && ota.nrv != OTA_DEAD) { /* OtaKingMagma (initDefaultPos: its placement), mLongFeet */
        if (m_otamagma && m_otamagma->skin && ota.magma && ota.magma->skin == m_otamagma->skin) {
            const Row *orow = &sc->objs[ota.row];
            mat_translate_rot_scale(m, orow->pos, orow->rot, orow->scale);
            render_skinned(m_otamagma, m, (const float (*)[16])ota.magma->drw);
        }
        for (int k = 0; k < 4 && m_otalfoot && m_otalfoot->skin && ota.nrv != OTA_APPEARSTAR; k++) {
            if (!ota.lnrv[k] || !ota.lfoot[k] || ota.lfoot[k]->skin != m_otalfoot->skin) continue; /* hideModel */
            ota_lfoot_mtx(k, m);
            render_skinned(m_otalfoot, m, (const float (*)[16])ota.lfoot[k]->drw);
        }
    }
    draw_hhp_leaves();
    draw_vines();
    for (int k = 0; k < norocks; k++) { /* OtaRocks' coconuts and fire balls */
        for (int b = 0; m_coco && b < 4; b++) {
            if (!orocks[k].ball[b].state) continue;
            basis_matrix(m, orocks[k].ball[b].pos, orocks[k].ball[b].up, orocks[k].ball[b].vel, game_frame * 0.3f);
            render_model(m_coco, m, 0x80, 0x80, 0x80);
        }
        for (int f = 0; m_fireball2 && f < 3; f++) {
            if (!orocks[k].fire[f].alive) continue;
            basis_matrix(m, orocks[k].fire[f].pos, orocks[k].fire[f].up, orocks[k].fire[f].vel, game_frame * 0.4f);
            render_model(m_fireball2, m, 0x80, 0x80, 0x80);
        }
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
                render_model(broke && m_deggcrack ? m_deggcrack : m_degg, jm, 0x80, 0x80, 0x80); /* startBva EggBroken */
            else if (m_deggbrk && broke && dino.dt >= 30 && dino.dt <= 120) { /* killed after step 120 */
                if (m_deggbrk->skin && dino.brk->skin == m_deggbrk->skin) render_skinned(m_deggbrk, jm, (const float (*)[16])dino.brk->drw);
                else render_model(m_deggbrk, jm, 0x80, 0x80, 0x80);
            }
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
            basis_matrix(m, fats[k].gift[g].pos, up_at(fats[k].gift[g].pos, v3(0, 1, 0)), v3(0, 0, 1), piece_rot);
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
    size_t l = strlen(out);
    snprintf(out + l, n - l, " camtype %s g%d j%d k%d h%d w%d m%d", camera_type_name(), mario->grounded, mario->jumping,
             mario->jump_kind, mario->hip, mario->walling, mario->magic);
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
