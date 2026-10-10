#include "game.h"
#include "anim.h"
#include "fx.h"
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
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
typedef struct { V3 pos, vel, up; int state, timer, row, kind; } Kuribo; /* walking enemies; row: its placement */
enum { KURIBO_WALK, KURIBO_STUN, KURIBO_DEAD };
enum { WALK_KURIBO, WALK_TAKOHEI, WALK_PACKUN, WALK_KOTEBUG, WALK_SPIDER, WALK_MERAMERA };

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
    int shoot;       /* ShootingStar: falls, bursts into star pieces (update_shooting) */
    int plant;       /* PlantGroup (Flower / FlowerBlue / CutBush Group): its model drawn at each member (plants) */
    const unsigned char *railer; /* map part on a rail: its PATH entry; moves along it (update_railers) */
} Inst;
enum { ITEM_STAR = 1, ITEM_ONEUP, ITEM_LIFEUP, ITEM_QCOIN, ITEM_CHIP };
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
 * show. ponytail: no SW_SLEEP, switches are not saved */
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
static int row_shown(int i)
{
    const Row *r = &sc->objs[i];
    return !row_dead[i] && (r->sw[SW_APPEAR] < 0 || sw_on(r, SW_APPEAR));
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
static const Model *m_coin, *m_purple, *m_piece, *m_kuribo, *m_takohei, *m_packun, *m_kotebug, *m_spider, *m_meramera;
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
static Launch *launches;
static int nlaunches;
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
 * plane, scale included (makeMtxTRS). ponytail: StarPieceFlow (pieces moving along a rail) is not ported. */
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
    npieces = count_type("StarPiece", 0) + box_pieces;
    int t_spg = scene_type(s, "StarPieceGroup");
    for (u32 i = 0; i < s->nobjs; i++)
        if (s->objs[i].type == t_spg)
            npieces += star_group(NULL, &s->objs[i], blob);
    nkuribo = count_type("Kuribo", 0) + count_type("TakoHei", 0) + count_type("PackunPetit", 0) + count_type("KoteBug", 0) +
              count_type("JumpSpider", 0) + count_type("Meramera", 0);
    box_coins += nkuribo; /* each drops a coin (appearCoinPop) */
    ncoins += nkuribo;
    coins = mem_alloc(MEM_OBJECTS, ncoins * sizeof(Pickup));
    purple = mem_alloc(MEM_OBJECTS, npurple * sizeof(Pickup));
    pieces = mem_alloc(MEM_OBJECTS, npieces * sizeof(Pickup));
    kuribo = mem_alloc(MEM_OBJECTS, nkuribo * sizeof(Kuribo));
    pops = mem_alloc(MEM_OBJECTS, (box_coins + 1) * sizeof(Pop));
    mario = mem_alloc(MEM_PLAYER, sizeof(Player));

    int ic = 0, ip = 0, is = 0, ik = 0;
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    int t_tako = scene_type(s, "TakoHei"), t_packun = scene_type(s, "PackunPetit"), t_kote = scene_type(s, "KoteBug");
    int t_spider = scene_type(s, "JumpSpider"), t_mera = scene_type(s, "Meramera");
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
        else if (r->type == t_kuribo || r->type == t_tako || r->type == t_packun || r->type == t_kote || r->type == t_spider ||
                 r->type == t_mera) {
            kuribo[ik].kind = r->type == t_tako ? WALK_TAKOHEI : r->type == t_packun ? WALK_PACKUN :
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
    ncoins = ic; /* box coins and star pieces are appended as boxes break */
    npieces = is;
    int t_super[3] = {scene_type(s, "SuperSpinDriver"), scene_type(s, "SuperSpinDriverPink"),
                      scene_type(s, "SuperSpinDriverGreen")}, t_spin = scene_type(s, "SpinDriver");
    launches = mem_alloc(MEM_OBJECTS, (count_type("SuperSpinDriver", 0) + count_type("SuperSpinDriverPink", 0) +
                                       count_type("SuperSpinDriverGreen", 0) + count_type("SpinDriver", 0) + 1) * sizeof(Launch));
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        int super = r->type == t_super[0] || r->type == t_super[1] || r->type == t_super[2];
        if (!super && r->type != t_spin) continue;
        u32 np = 0; int closed;
        const float *pts = r->path >= 0 ? find_rail(blob, r->zone, r->path, &np, &closed) : NULL;
        Launch *L = &launches[nlaunches];
        u32 nraw = 0;
        if (pts) find_rail_of(blob, r->zone, r->path, &nraw, &closed, 1);
        if (pts && np >= 2)
            init_launch(L, r, pts, np, nraw <= 2);
        else if (super)
            continue;
        else { /* SpinDriver without a rail: thrown along its Y axis at arg0 */
            V3 x, z;
            row_axes(r, &x, &z);
            memset(L, 0, sizeof *L);
            L->pos = row_pos(r);
            L->kick = vmul(vcross(z, x), r->arg[0] > 0 ? (float)r->arg[0] : 40.0f);
        }
        L->start = super ? 45 : 20;
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
            if (t == t_fg || t == t_fb || t == t_cb) { plant_first[i] = total; total += plant_count(&s->objs[i]); }
        }
        plants = mem_alloc(MEM_OBJECTS, (total + 1) * sizeof(Plant));
        for (u32 i = 0; i < s->nobjs; i++) { /* initMember: the first arg1 hold an item, then shuffled */
            if (plant_first[i] < 0) continue;
            Plant *g = &plants[plant_first[i]];
            int n = plant_count(&s->objs[i]), items = s->objs[i].arg[1] > 0 ? s->objs[i].arg[1] : 0;
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
        CamTarget t = {mario->pos, mario->face, mario->up, 0};
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

/* a rail-moving part's translation: its rail at row_coord */
static V3 rail_at(const float *p, u32 n, float d);
static void railer_place(Inst *in)
{
    const unsigned char *e = in->railer;
    V3 p = rail_at((const float *)(e + 24), *(const u32 *)(e + 8), row_coord[in->row]);
    in->m[12] = p.x; in->m[13] = p.y; in->m[14] = p.z;
}

static int plant_count(const Row *r) { return r->arg[0] > 0 ? r->arg[0] : 7; }

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
    int ring = 0, n = plant_count(r), hits = 0;
    float angle = 0, step = 6.2831853f, radius = 0;
    for (int k = 0; k < n; k++) {
        Plant *p = &plants[plant_first[row] + k];
        V3 at = vadd(c, vadd(vmul(x, cosf(angle) * radius), vmul(y, sinf(angle) * radius)));
        p->up = up_at(at, up);
        hits += p->ok = drop_to_ground(vadd(at, vmul(p->up, 100.0f)), p->up, 1000.0f, &p->pos);
        angle += step;
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
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    int t_spg = scene_type(s, "StarPieceGroup"), t_erail = scene_type(s, "ElectricRail"), t_tako = scene_type(s, "TakoHei"),
        t_packun = scene_type(s, "PackunPetit"), t_kote = scene_type(s, "KoteBug"),
        t_spider = scene_type(s, "JumpSpider"), t_mera = scene_type(s, "Meramera");
    m_erail = scene_model(s, "ElectricRail");
    m_fireball = scene_model(s, "FireBarBall");
    m_rock = scene_model(s, "RockCreator");
    m_shell = scene_model(s, "KoopaJrShipCannonShell");
    if (!m_shell) m_shell = scene_model(s, "IronCannonLauncherPoint");
    ninsts = 0;
    for (u32 i = 0; i < s->nobjs; i++) { /* every placed model without behaviour: planets and the rest */
        const Row *r = &s->objs[i];
        if (r->type == t_coin || r->type == t_pc || r->type == t_piece || r->type == t_kuribo || r->type == t_circle ||
            r->type == t_tako || r->type == t_packun || r->type == t_kote || r->type == t_spider || r->type == t_mera ||
            r->type == t_rail || r->type == t_spg || r->type == t_erail || r->type == scene_type(s, "RockCreator"))
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
        int dc, dp;
        in->breaks = breakable(name, r, &dc, &dp);
        in->collapse = strcmp(name, "CollapsePlane") == 0 ? (r->arg[0] > 0 ? r->arg[0] : 140) : 0;
        in->hazard = strcmp(name, "SpaceMine") == 0 ? 1 : strcmp(name, "NeedlePlant") == 0 ? 2 :
                     strcmp(name, "BlackHole") == 0 ? 3 : strcmp(name, "BlackHoleCube") == 0 ? 4 : 0;
        in->item = strcmp(name, "PowerStar") == 0 ? ITEM_STAR : strcmp(name, "BenefitItemOneUp") == 0 ? ITEM_ONEUP :
                   strcmp(name, "BenefitItemLifeUp") == 0 ? ITEM_LIFEUP : strcmp(name, "QuestionCoin") == 0 ? ITEM_QCOIN :
                   strcmp(name, "YellowChip") == 0 || strcmp(name, "BlueChip") == 0 ? ITEM_CHIP : 0;
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
        in->sky = sky_class(name);
        in->shoot = strcmp(name, "ShootingStar") == 0;
        in->steam = strcmp(name, "LavaSteam") == 0;
        in->geyser = strcmp(name, "LavaGeyser") == 0;
        in->plant = strcmp(name, "FlowerGroup") == 0 || strcmp(name, "FlowerBlueGroup") == 0 || strcmp(name, "CutBushGroup") == 0;
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
        in->kcl_ready = 0;
        zone_grow(in);
    }
    for (int i = 0; plant_first && i < ninsts; i++)
        if (insts[i].plant) { insts[i].kcl_ready = 1; place_plants(&insts[i]); }
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

/* nearest low zone (distance to its sphere's surface) under `limit`, or -1 */
static int nearest_low_zone(V3 p, const unsigned char *full, float limit)
{
    int want = -1;
    for (int z = 0; z < MAX_ZONES; z++) {
        if (zone_r[z] < 0 || full[z]) continue;
        float d = vlen(vsub(p, zone_c[z])) - zone_r[z];
        if (d < limit) { limit = d; want = z; }
    }
    return want;
}

int game_zone_wanted(const unsigned char *full)
{
    if (pipe_zone >= 0) { /* came out of a pipe in a low zone: its collision is needed now */
        if (!full[pipe_zone]) return pipe_zone;
        pipe_zone = -1;
    }
    /* the nearest low zone whose sphere (+1000) Mario is in, once he is out of every full zone's model sphere (else
     * the zone he stands in would go low and come back each frame). ponytail: proximity only, no visibility */
    for (int i = 0; i < ninsts; i++) {
        const Inst *in = &insts[i];
        V3 d = vsub(mario->pos, v3(in->m[12], in->m[13], in->m[14]));
        if (in->zone && full[in->zone] && vdot(d, d) < in->reach * in->reach) return -1;
    }
    return nearest_low_zone(mario->pos, full, 1000.0f);
}

int game_zone_next(const unsigned char *full)
{
    if (mario->launch < 0) /* on foot: the nearest low zone within 5000 of its sphere starts reading early */
        return nearest_low_zone(mario->pos, full, 5000.0f);
    /* flying: the low zone around the launch star's landing point */
    return nearest_low_zone(launch_pos(&launches[mario->launch], 1.0f), full, 1500.0f);
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
 * beside it standing. Returns its row or -1. */
int game_test_enemy(int kind, int k, int how)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *e = &kuribo[i];
        if (e->kind != kind || e->state == KURIBO_DEAD || k--) continue;
        V3 side = vnorm(vtangent(v3(1, 0, 0), e->up));
        if (vlen(side) < 0.5f) side = vnorm(vtangent(v3(0, 0, 1), e->up));
        mario->up = e->up;
        mario->pos = how == 0 ? vadd(e->pos, vmul(e->up, 150.0f)) : vadd(e->pos, vmul(side, how == 1 ? 140.0f : 90.0f));
        mario->hvel = v3(0, 0, 0); mario->vup = how == 0 ? -5.0f : 0.0f; mario->grounded = 0;
        mario->spin_timer = how == 1 ? SPIN_TIME : 0;
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
        if (in->item != ITEM_STAR || star_wait[in->row] <= 0) continue;
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
    for (int i = 0; i < nlaunches; i++)
        if ((launches[i].start < 45) == small) return game_test_launch(i);
    return -1;
}

int game_launch_state(void) { return mario->launch; }

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
static int update_launch(Player *m)
{
    Launch *L = &launches[m->launch];
    int t = ++m->launch_t;
    if (t <= L->start) { /* exeShootStart: to the centre over 15 frames, then the wind-up */
        float k = t < 15 ? t / 15.0f : 1.0f;
        m->pos = vadd(m->launch_from, vmul(vsub(L->pos, m->launch_from), k));
        return 1;
    }
    int small = L->start < 45, s = t - L->start;
    if (!L->pts) { /* SpinDriver without a rail: endBindAndSpinDriverJump with its kick */
        L->cool = 60;
        m->launch = -1;
        m->vup = vdot(L->kick, m->up); m->hvel = vtangent(L->kick, m->up); m->grounded = 0;
        m->jump_kind = 0; m->air_timer = 0; m->spin_used = 0;
        if (mario_anim) mario_play("spaceflyshortstart", 1.0f, 0);
        return 0;
    }
    float u = (float)s / L->flight;
    if (s == 1 && mario_anim) mario_play(small ? "spaceflyshortstart" : "spaceflystart", 1.0f, 0);
    if (s == L->flight / 5 && mario_anim) mario_play(small ? "spaceflyshort" : "spaceflyloop", 1.0f, 1);
    if (s == L->flight - 70 && mario_anim) mario_play("spaceflyend", 1.0f, 0);
    V3 p = launch_pos(L, u > 1 ? 1 : u), q = launch_pos(L, u + 0.01f > 1 ? 1 : u + 0.01f);
    V3 dir = vsub(q, p);
    m->pos = p;
    if (vlen(dir) > 1e-3f) m->face = vnorm(vtangent(dir, m->up));
    if (s < L->flight)
        return 1;
    /* endBind: Mario falls from the path's end; the star rests 60 frames (exeCoolDown) */
    L->cool = 60;
    m->launch = -1;
    m->hvel = v3(0, 0, 0); m->vup = 0; m->grounded = 0;
    m->jump_kind = 0; m->air_timer = DROP_WAIT + 1; m->spin_used = 0;
    return 0;
}

static void update_mario(void)
{
    Player *m = mario;
    m->up = up_at(m->pos, m->up);
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
        m->vup -= g;
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
    if (lost(m->pos)) /* fell off */
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
    mario->pos = start_pos; mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->hurt_timer = 0; mario->hip = 0;
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

static void update_kuribo(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->state == KURIBO_DEAD || !row_shown(k->row)) continue;
        k->up = up_at(k->pos, k->up);
        V3 to = vsub(mario->pos, k->pos);
        float d = vlen(to);
        if (d > 3000.0f) continue; /* sActiveDistance: far ones wait (their zone may be loaded low, no collision) */
        int tako = k->kind == WALK_TAKOHEI || k->kind == WALK_MERAMERA; /* Meramera.cpp: a stomp, hip drop or spin
                                                                             * kills it, a touch burns (requestAttack) */
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
    if (h < 0.0f || h > 1000.0f * r->scale[1]) return 0;
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
    for (int i = 0; moving && i < ninsts; i++) {
        const Inst *in = &insts[i];
        if (!in->plant || plant_first[in->row] < 0 || !row_shown(in->row) ||
            vlen(vsub(c, v3(in->m[12], in->m[13], in->m[14]))) > in->reach + 1000.0f)
            continue;
        const Row *r = &sc->objs[in->row];
        int n = plant_count(r);
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
    *r = (in->item == ITEM_STAR ? 75.0f : in->item == ITEM_QCOIN ? 150.0f : in->item == ITEM_CHIP ? 80.0f : 50.0f) * in->s;
    if (in->item == ITEM_STAR || in->item == ITEM_CHIP) return p;
    return vadd(p, vmul(up_at(p, mario->up), (in->item == ITEM_QCOIN ? 210.0f : 50.0f) * in->s));
}

/* An event star (star_wait) waits hidden, or shown inside the cage that declared it (CrystalCageL: 250 up the cage,
 * BreakableCage: 150, DummyDisplayModel); then the PowerStar.cpp appear demo (exeAppearDemoRise / Move): from there
 * (its own place for the hundred coins) 300 up its own y (ease out over 60 frames), at frame 80 off to its place in
 * 120 frames on an arc as high as half the distance across its up (300 to 5000); taken from frame 210. *state: 0
 * hidden, 1 waiting in its cage, 2 appearing, 3 can be taken. ponytail: no demo camera, the stage goes on */
static V3 star_pos(const Inst *in, int *state)
{
    V3 p = v3(in->m[12], in->m[13], in->m[14]);
    int w = star_wait[in->row], t = row_step[in->row];
    *state = 3;
    if (!w) return p;
    V3 up = vnorm(v3(in->m[4], in->m[5], in->m[6])), o = p;
    if (w > 0) {
        const Row *r = &sc->objs[w - 1];
        const char *name = sc->types + 32 * r->type;
        V3 x, z;
        row_axes(r, &x, &z);
        float h = strcmp(name, "CrystalCageL") == 0 ? 250.0f : strncmp(name, "Breakable", 9) == 0 ? 150.0f : 0.0f;
        o = vadd(row_pos(r), vmul(vcross(z, x), h * r->scale[0]));
        if (!row_dead[w - 1]) { *state = h > 0; return o; }
    } else if (got_coins < 100) {
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
        if (in->item == ITEM_STAR) printf("power star %d got\n", ++stars);
        else if (in->item == ITEM_ONEUP) printf("1-up, lives %d\n", ++lives);
        else if (in->item == ITEM_LIFEUP) { life = max_life = 2 * MAX_LIFE; printf("life up\n"); }
        else if (in->item == ITEM_QCOIN) fx_emit(FX_QUESTIONCOIN_GET, in->m);
        else if (in->item == ITEM_CHIP)
            fx_emit(sc->types[32 * sc->objs[in->row].type] == 'Y' ? FX_YELLOWCHIP_GET : FX_BLUECHIP_GET, in->m);
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
static void keep_effects(void)
{
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

void game_update(void)
{
    hip_hit = 0;
    game_frame++;
    update_rotors();
    update_railers();
    update_mario();
    if (mario->spin_timer == SPIN_TIME - 1) mario_fx(FX_MARIO_SPINLIGHT, 1.0f); /* a spin started this frame */
    if (hip_hit) mario_fx(FX_MARIO_HIPDROPFLOWER, 0.0f);
    update_kuribo();
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
    spin_angle += 0.08f;
    CamTarget t = {mario->pos, mario->face, mario->up, !mario->grounded};
    camera_update(&t);
    V3 w, u;
    float fovy;
    camera_pose(&cam_pos, &w, &u, &fovy); /* cam_pos also steers the stick (camera-relative) */
    keep_effects();
    fx_update();
}

/* --- draw ------------------------------------------------------------------------------------------------ */

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
        render_model(in->md, cm, 0x70, 0xB0, 0x60);
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
                render_model(in->md, m, 0x70, 0xB0, 0x60);
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
                render_model(in->md, cm, 0x70, 0xB0, 0x60);
                continue;
            }
        }
        if (in->flip && in->md_flip[0]) { /* the face PanelA / PanelB / PanelEnd leave shown (ponytail: no turn) */
            int turned = (row_step[in->row] & FLIP_BACK) != (strcmp(sc->types + 32 * sc->objs[in->row].type,
                                                                    "FlipPanelReverse") == 0);
            const Model *md = (row_step[in->row] & FLIP_DONE) && in->md_flip[1] ? in->md_flip[1] :
                              turned ? in->md_flip[0] : in->md;
            render_model(md, in->m, 0x70, 0xB0, 0x60);
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
            render_model(in->md, cm, 0x70, 0xB0, 0x60);
            continue;
        }
        if (in->geyser) { /* stretched along its y to its height (geyser_height) */
            float k = geyser_height(in) / GEYSER_BIND, cm[16];
            if (k <= 0.0f) continue;
            memcpy(cm, in->m, 64);
            for (int j = 4; j < 7; j++) cm[j] *= k;
            render_model(in->md, cm, 0x70, 0xB0, 0x60);
            continue;
        }
        if (in->collapse && row_step[in->row]) { /* calcJointPlane: x / z scale 1 - 0.7 step / time */
            float k = 1.0f - 0.7f * row_step[in->row] / in->collapse, cm[16];
            memcpy(cm, in->m, 64);
            for (int j = 0; j < 3; j++) { cm[j] *= k; cm[8 + j] *= k; }
            render_model(in->md, cm, 0x70, 0xB0, 0x60);
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
            render_model(in->md, cm, 0x70, 0xB0, 0x60);
        } else
            render_model(in->md, in->m, 0x70, 0xB0, 0x60);
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
    for (int i = 0; i < nkuribo; i++) {
        const Model *md = kuribo[i].kind == WALK_TAKOHEI ? m_takohei : kuribo[i].kind == WALK_PACKUN ? m_packun :
                          kuribo[i].kind == WALK_KOTEBUG ? m_kotebug : kuribo[i].kind == WALK_SPIDER ? m_spider :
                          kuribo[i].kind == WALK_MERAMERA ? m_meramera : m_kuribo;
        if (kuribo[i].state == KURIBO_DEAD || !md || !row_shown(kuribo[i].row)) continue;
        basis_matrix(m, kuribo[i].pos, kuribo[i].up, vsub(mario->pos, kuribo[i].pos), 0);
        render_model(md, m, kuribo[i].state == KURIBO_STUN ? 0x50 : 0x90, 0x50, 0x20);
    }
    basis_matrix(m, mario->pos, mario->up, mario->face, 0);
    if (m_mario_skin && !(mario->hurt_timer & 4))
        render_skinned(m_mario_skin, m, (const float (*)[16])mario_anim->drw);
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
int game_stars(void) { return stars; }
int game_misses(void) { return misses; }
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
