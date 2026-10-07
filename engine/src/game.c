#include "game.h"
#include "anim.h"
#include "camera.h"

#include <stdio.h>
#include <string.h>

#include "gravity.h"
#include "kcl.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "vmath.h"

/* Gameplay subset of HeavensDoorGalaxy scenario 2, data-oriented: one compact array per type, sized at load,
 * no allocation per object. Behaviour follows Petari where noted; differences are listed in docs/phase1-report.md. */

typedef struct { V3 pos; int alive; } Pickup;                    /* 16 B: Coin, PurpleCoin, StarPiece */
typedef struct { V3 pos, vel, up; int state, timer; } Kuribo;     /* 44 B -> 48 with alignment */
enum { KURIBO_WALK, KURIBO_STUN, KURIBO_DEAD };

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
} Player;

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
#define SLIP_ANGLE_COS 0.5f       /* mSlipAngle 60: steeper floors are not ground (Mario slides off) */
#define WALL_ANGLE_COS 0.087f     /* mForceWallAngle 85: only faces steeper than this are walls; 60-85 slopes are
                                   * neither ground nor wall, Mario just slides down them */
#define WALL_KEEP 6               /* frames a wall contact stays valid (wall slide clip, wall kick) */
#define GROUNDING 20.0f           /* mGravityGrounding: downward step while on the ground keeps Mario on crests */
#define DROP_WAIT 5               /* mDropWaitTime: frames in the air before the fall clip (MarioMove.cpp:138) */
#define TURN_STAND 0.9f           /* mTurnAngleSpeed (rad per frame, standing) */
#define TURN_MOVE 0.15f           /* mTurnAngleSpeed2, above mFastTurnSpeed 0.1 (MarioMove.cpp:368-373) */
#define AIR_CONTROL 0.1f          /* ponytail: one air steering factor instead of Petari's air-walk timers */
#define BODY_RADIUS 50.0f         /* ponytail: one sphere for Mario's body; SMG uses several collision checks */

static const Scene *sc;
/* Placed static models (planets and every class without behaviour yet): drawn, and collided if they have a KCL */
typedef struct {
    const Model *md;
    const unsigned char *kcl;
    float m[16];     /* world matrix (uniform scale s) */
    float s, reach;  /* scale, bounding radius in world units */
} Inst;
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
static int got_coins, got_purple, got_pieces;
static float spin_angle;
static V3 cam_pos;
static const Model *m_coin, *m_purple, *m_piece, *m_kuribo;
static const Model *m_mario_skin; /* Mario.arc, skinned (SKIN chunk) */
static Anim *mario_anim;

/* Interpolation frames per clip from Petari's marioAnimeTable (MarioAnimatorData.hpp, XanimeGroupInfo _8):
 * locomotion (基本) and squat 6, fall 15, long jump and its landing 6, back flip and its landing 6, triple-jump
 * landing 6, wall slide 10; jumps, plain landings, spins and the wall kick snap (0). */
static void mario_play(const char *clip, float speed, int loop)
{
    static const struct { const char *clip; int frames; } t[] = {
        {"wait", 6}, {"walk", 6}, {"run", 6}, {"squatwait", 6}, {"squatwalk", 6}, {"fall", 15}, {"jumpbroad", 6},
        {"jumpbroadland", 6}, {"jumpback", 6}, {"jumpbackland", 6}, {"jumprollland", 6}, {"wallslide", 10}};
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

/* rail polyline from the PATH chunk: {u16 zone, u16 closed, s32 id, u32 npts, f32 pts[][3]} */
static const float *find_rail(const unsigned char *blob, int id, u32 *npts, int *closed)
{
    const unsigned char *p = blob_chunk(blob, "PATH", NULL);
    u32 n = *(const u32 *)p;
    p += 4;
    for (u32 i = 0; i < n; i++) {
        u32 np = *(const u32 *)(p + 8);
        if (*(const int *)(p + 4) == id) {
            *npts = np;
            *closed = *(const unsigned short *)(p + 2);
            return (const float *)(p + 12);
        }
        p += 12 + 12 * np;
    }
    return NULL;
}

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

typedef struct { V3 n; float dist; } Hit; /* world normal, penetration */

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
    if (wall) *wall = 0;
    for (int it = 0; it < 3; it++) {
        Hit hits[16];
        int n = kcl_query(*center, radius, hits, 16);
        if (!n)
            break;
        int deepest = 0;
        for (int i = 1; i < n; i++)
            if (hits[i].dist > hits[deepest].dist)
                deepest = i;
        V3 nrm = hits[deepest].n;
        *center = vadd(*center, vmul(nrm, hits[deepest].dist + 0.01f));
        float d = vdot(nrm, up);
        if (d > SLIP_ANGLE_COS) {
            ground = 1;
            if (ground_n) *ground_n = nrm;
        } else if (d > -0.3f && d < WALL_ANGLE_COS && wall) {
            *wall = 1;
            *wall_n = nrm;
        }
    }
    if (!ground) { /* resting check: a sphere a little lower touches ground */
        V3 probe = vsub(*center, vmul(up, 4.0f));
        Hit hits[16];
        int n = kcl_query(probe, radius, hits, 16);
        for (int i = 0; i < n; i++)
            if (vdot(hits[i].n, up) > SLIP_ANGLE_COS) {
                ground = 1;
                if (ground_n) *ground_n = hits[i].n;
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
    u32 gsz;
    const unsigned char *g = blob_chunk(blob, "GRAV", &gsz);
    ngrav = *(const int *)g;
    grav = (const Gravity *)(g + 4);
    m_coin = scene_model(s, "Coin");
    m_purple = scene_model(s, "PurpleCoin");
    m_piece = scene_model(s, "StarPiece");
    m_kuribo = scene_model(s, "Kuribo");
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

    ncoins = count_type("Coin", 0);
    npurple = count_type("PurpleCoin", 0) + count_type("CirclePurpleCoinGroup", 1) + count_type("PurpleRailCoin", 1);
    npieces = count_type("StarPiece", 0);
    nkuribo = count_type("Kuribo", 0);
    coins = mem_alloc(MEM_OBJECTS, ncoins * sizeof(Pickup));
    purple = mem_alloc(MEM_OBJECTS, npurple * sizeof(Pickup));
    pieces = mem_alloc(MEM_OBJECTS, npieces * sizeof(Pickup));
    kuribo = mem_alloc(MEM_OBJECTS, nkuribo * sizeof(Kuribo));
    mario = mem_alloc(MEM_PLAYER, sizeof(Player));

    int ic = 0, ip = 0, is = 0, ik = 0;
    int t_coin = scene_type(s, "Coin"), t_pc = scene_type(s, "PurpleCoin"), t_circle = scene_type(s, "CirclePurpleCoinGroup");
    int t_rail = scene_type(s, "PurpleRailCoin"), t_piece = scene_type(s, "StarPiece"), t_kuribo = scene_type(s, "Kuribo");
    insts = mem_alloc(MEM_OBJECTS, s->nobjs * sizeof(Inst));
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if (r->type != t_coin && r->type != t_pc && r->type != t_piece && r->type != t_kuribo && r->type != t_circle &&
            r->type != t_rail) {
            const char *name = s->types + 32 * r->type;
            const Model *md = scene_model(s, name);
            if (md) {
                Inst *in = &insts[ninsts++];
                in->md = md;
                in->kcl = scene_kcl(s, name);
                mat_translate_rot_scale(in->m, r->pos, r->rot, r->scale);
                in->s = r->scale[0];
                in->reach = md->radius * in->s;
            }
        }
        if (r->type == t_coin) { coins[ic].pos = row_pos(r); coins[ic++].alive = 1; }
        else if (r->type == t_pc) { purple[ip].pos = row_pos(r); purple[ip++].alive = 1; }
        else if (r->type == t_piece) { pieces[is].pos = row_pos(r); pieces[is++].alive = 1; }
        else if (r->type == t_kuribo) {
            kuribo[ik].pos = row_pos(r);
            kuribo[ik].up = up_at(kuribo[ik].pos, v3(0, 1, 0));
            kuribo[ik++].state = KURIBO_WALK;
        } else if (r->type == t_circle) { /* CircleCoinGroup::placementCoin: arg0 coins, arg2 radius (default 200) */
            V3 side, front, c = row_pos(r);
            row_axes(r, &side, &front);
            float radius = r->arg[2] > 0 ? (float)r->arg[2] : 200.0f;
            for (int k = 0; k < r->arg[0]; k++) {
                float a = 6.2831853f * k / r->arg[0];
                purple[ip].pos = vadd(c, vadd(vmul(front, radius * cosf(a)), vmul(side, radius * sinf(a))));
                purple[ip++].alive = 1;
            }
        } else if (r->type == t_rail && r->arg[0] > 0) {
            u32 np; int closed;
            const float *pts = find_rail(blob, r->path, &np, &closed);
            if (pts && np > 1)
                ip += place_on_rail(purple + ip, pts, np, closed, r->arg[0]);
        }
    }
    npurple = ip;
    /* Mario starts at the Mysterious zone's start point (zone 1); fall back to the first */
    const Row *st = &s->starts[0];
    for (u32 i = 0; i < s->nstarts; i++)
        if (s->starts[i].zone == 1) { st = &s->starts[i]; break; }
    start_pos = row_pos(st);
    mario->pos = start_pos;
    mario->up = up_at(mario->pos, v3(0, 1, 0));
    mario->face = vnorm(vtangent(v3(0, 0, 1), mario->up));
    mario->jump_kind = mario->last_jump = -1;
    mario->wall_timer = 1000;
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
    printf("game: %d static instances, coins %d, purple %d, star pieces %d, kuribo %d, start (%d %d %d)\n", ninsts,
           ncoins, npurple, npieces, nkuribo, (int)start_pos.x, (int)start_pos.y, (int)start_pos.z);
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

static void update_mario(void)
{
    Player *m = mario;
    m->up = up_at(m->pos, m->up);
    /* camera-relative stick on the plane under Mario */
    V3 cam_fwd = vnorm(vtangent(vsub(m->pos, cam_pos), m->up));
    V3 cam_right = vnorm(vcross(cam_fwd, m->up));
    V3 want = vadd(vmul(cam_right, pad_lx()), vmul(cam_fwd, pad_ly()));
    if (pad_held(PAD_UP)) want = vadd(want, cam_fwd);
    if (pad_held(PAD_DOWN)) want = vsub(want, cam_fwd);
    if (pad_held(PAD_RIGHT)) want = vadd(want, cam_right);
    if (pad_held(PAD_LEFT)) want = vsub(want, cam_right);
    if (vlen(want) > 1.0f) want = vnorm(want);
    float stick = vlen(want);
    m->hvel = vtangent(m->hvel, m->up);
    float ratio = vlen(m->hvel) / WALK_SPEED;

    if (m->grounded) {
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
        if (m->wall_timer <= WALL_KEEP && pad_pressed(PAD_CROSS)) { /* wall kick: away from the wall (tryWallJump) */
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

    float down = m->grounded ? -GROUNDING : m->vup; /* on the ground: step down to follow the floor */
    V3 center = vadd(vadd(m->pos, vmul(m->up, BODY_RADIUS)), vadd(m->hvel, vmul(m->up, down)));
    V3 gn = m->up;
    int was_ground = m->grounded;
    V3 wn = m->wall_n;
    int ground = collide_n(&center, m->up, BODY_RADIUS, &gn, &wn, &m->wall);
    if (m->wall) { m->wall_n = wn; m->wall_timer = 0; } else if (m->wall_timer < 1000) m->wall_timer++;
    m->pos = vsub(center, vmul(m->up, BODY_RADIUS));
    if (ground && m->vup <= 0) {
        if (!was_ground && (m->jump_kind > 0 || m->last_jump >= 0 || m->air_timer > DROP_WAIT)) {
            /* landing from a jump or a real fall: keep the jump kind for the 2nd / 3rd jump window */
            static const char *land[9] = {"land", "jump2land", "jumprollland", "land", "land", "jumpbroadland",
                                          "jumpbackland", "land", "land"};
            m->land_timer = 0;
            if (mario_anim && m->jump_kind >= 0) mario_play(land[m->jump_kind], 1.0f, 0);
        }
        m->grounded = 1; m->vup = 0; m->spin_used = 0; m->jump_kind = -1; m->low_grav = 0;
        m->ground_n = gn;
        m->hvel = vtangent(m->hvel, gn); /* walk along the slope */
    } else if (!ground) {
        if (m->grounded) { m->jump_kind = 0; m->air_timer = 0; m->last_jump = -1; }
        m->grounded = 0;
    }
    if (m->wall && !m->grounded) { /* stop pushing into the wall */
        float into = vdot(m->hvel, m->wall_n);
        if (into < 0) m->hvel = vsub(m->hvel, vmul(m->wall_n, into));
    }
    if (lost(m->pos)) { /* fell off: back to the start */
        m->pos = start_pos; m->hvel = v3(0, 0, 0); m->vup = 0;
    }
    if (m->hurt_timer) m->hurt_timer--;

    if (mario_anim) { /* animation: the one-shot clip of a move plays out, then the state picks a loop */
        float sp = vlen(m->hvel) / WALK_SPEED;
        int busy = mario_anim->clip_id >= 0 && !mario_anim->loop && !mario_anim->done;
        if (m->grounded && m->spin_timer > 0) mario_play("spinground", 1.0f, 0);
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

static void update_kuribo(void)
{
    for (int i = 0; i < nkuribo; i++) {
        Kuribo *k = &kuribo[i];
        if (k->state == KURIBO_DEAD) continue;
        k->up = up_at(k->pos, k->up);
        V3 to = vsub(mario->pos, k->pos);
        float d = vlen(to);
        if (k->state == KURIBO_STUN) {
            if (--k->timer <= 0) k->state = KURIBO_WALK;
        } else if (d < 1500.0f) { /* chase on the surface (ponytail: no turn rate, no notice animation) */
            V3 dir = vnorm(vtangent(to, k->up));
            k->vel = vadd(vmul(k->vel, 0.8f), vmul(dir, 4.0f * 0.2f));
        } else {
            k->vel = vmul(k->vel, 0.8f);
        }
        V3 center = vadd(vadd(k->pos, vmul(k->up, 45.0f)), vsub(k->vel, vmul(k->up, 6.0f)));
        collide(&center, k->up, 45.0f);
        k->pos = vsub(center, vmul(k->up, 45.0f));
        /* stomp: Mario falling onto its top; spin: stun; side touch: Mario knocked back */
        float above = vdot(to, k->up);
        if (d < 130.0f && mario->vup < 0 && above > 40.0f) { k->state = KURIBO_DEAD; mario->vup = 15.0f; mario->grounded = 0; }
        else if (d < 170.0f && mario->spin_timer > 0 && k->state == KURIBO_WALK) { k->state = KURIBO_STUN; k->timer = 180; }
        else if (d < 100.0f && k->state == KURIBO_WALK && mario->hurt_timer == 0) {
            mario->hvel = vmul(vnorm(vtangent(to, k->up)), 15.0f);
            mario->vup = 10.0f; mario->grounded = 0; mario->hurt_timer = 60;
        }
    }
}

static void collect(Pickup *p, int n, float reach, int *counter)
{
    V3 c = vadd(mario->pos, vmul(mario->up, BODY_RADIUS));
    for (int i = 0; i < n; i++)
        if (p[i].alive && vlen(vsub(p[i].pos, c)) < reach) { p[i].alive = 0; (*counter)++; }
}

void game_update(void)
{
    update_mario();
    update_kuribo();
    collect(coins, ncoins, 100.0f, &got_coins);
    collect(purple, npurple, 100.0f, &got_purple);
    collect(pieces, npieces, 120.0f, &got_pieces);
    spin_angle += 0.08f;
    CamTarget t = {mario->pos, mario->face, mario->up, !mario->grounded};
    camera_update(&t);
    V3 w, u;
    float fovy;
    camera_pose(&cam_pos, &w, &u, &fovy); /* cam_pos also steers the stick (camera-relative) */
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
        if (!p[i].alive) continue;
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
    render_set_fovy(fovy);
    float eye[3] = {ce.x, ce.y, ce.z}, target[3] = {cw.x, cw.y, cw.z}, up[3] = {cu.x, cu.y, cu.z};
    float m[16];
    render_begin(eye, target, up);
    for (int i = 0; i < ninsts; i++)
        render_model(insts[i].md, insts[i].m, 0x70, 0xB0, 0x60);
    draw_pickups(coins, ncoins, m_coin, 0xF0, 0xD0, 0x30);
    draw_pickups(purple, npurple, m_purple, 0xB0, 0x50, 0xF0);
    draw_pickups(pieces, npieces, m_piece, 0x60, 0xD0, 0xF0);
    for (int i = 0; i < nkuribo; i++) {
        if (kuribo[i].state == KURIBO_DEAD || !m_kuribo) continue;
        basis_matrix(m, kuribo[i].pos, kuribo[i].up, vsub(mario->pos, kuribo[i].pos), 0);
        render_model(m_kuribo, m, kuribo[i].state == KURIBO_STUN ? 0x50 : 0x90, 0x50, 0x20);
    }
    basis_matrix(m, mario->pos, mario->up, mario->face, 0);
    if (m_mario_skin && !(mario->hurt_timer & 4))
        render_skinned(m_mario_skin, m, (const float (*)[16])mario_anim->drw);
}

void game_debug(char *out, int n)
{
    snprintf(out, n, "mario %d %d %d up %d %d %d cam %d %d %d", (int)mario->pos.x, (int)mario->pos.y,
             (int)mario->pos.z, (int)(mario->up.x * 100), (int)(mario->up.y * 100), (int)(mario->up.z * 100), (int)cam_pos.x, (int)cam_pos.y,
             (int)cam_pos.z);
}

int game_coins(void) { return got_coins; }
int game_purple(void) { return got_purple; }
int game_purple_total(void) { return npurple; }
int game_starpieces(void) { return got_pieces; }
int game_kuribo_alive(void)
{
    int n = 0;
    for (int i = 0; i < nkuribo; i++) n += kuribo[i].state != KURIBO_DEAD;
    return n;
}
