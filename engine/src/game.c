#include "game.h"

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
    V3 pos, up, hvel, face;
    float vup;
    int grounded, spin_timer, spin_used, hurt_timer;
} Player;

/* Mario constants from Petari MarioConst.cpp gMarioConstDefault (units per frame at 60 Hz) */
#define WALK_SPEED 13.0f      /* mWalkSpeed */
#define INERTIA 0.9f          /* mInertiaStandardMax */
#define JUMP_SPEED 22.0f      /* mJumpHeight[0] */
#define JUMP_GRAVITY 1.8f     /* mGravityJumping[0] */
#define MAX_DROP 35.0f        /* mMaxDropSpeed[0] */
#define SPIN_LIFT 5.0f        /* mJumpTornadoSpeed */
#define BODY_RADIUS 50.0f     /* ponytail: one sphere for Mario's body; SMG uses several collision checks */

static const Scene *sc;
static const unsigned char *kcl;
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
static const Model *m_coin, *m_purple, *m_piece, *m_kuribo, *m_planet;
static Model m_mario;

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
static int collide(V3 *center, V3 up, float radius)
{
    int ground = 0;
    for (int it = 0; it < 3; it++) {
        KclHit hits[16];
        int n = kcl_check_sphere(kcl, (const float *)center, radius, hits, 16);
        if (!n)
            break;
        int deepest = 0;
        for (int i = 1; i < n; i++)
            if (hits[i].dist > hits[deepest].dist)
                deepest = i;
        const float *fn = kcl_face_normal(kcl, hits[deepest].prism);
        V3 nrm = v3(fn[0], fn[1], fn[2]);
        *center = vadd(*center, vmul(nrm, hits[deepest].dist + 0.01f));
        if (vdot(nrm, up) > 0.5f)
            ground = 1;
    }
    if (!ground) { /* resting check: a sphere a little lower touches ground */
        V3 probe = vsub(*center, vmul(up, 4.0f));
        KclHit hits[16];
        int n = kcl_check_sphere(kcl, (const float *)&probe, radius, hits, 16);
        for (int i = 0; i < n; i++) {
            const float *fn = kcl_face_normal(kcl, hits[i].prism);
            if (vdot(v3(fn[0], fn[1], fn[2]), up) > 0.5f)
                ground = 1;
        }
    }
    return ground;
}

/* --- init ------------------------------------------------------------------------------------------------ */

static const float box_v[] = {-30, 0, -30, 30, 0, -30, 30, 0, 30, -30, 0, 30, -30, 160, -30, 30, 160, -30, 30, 160, 30, -30, 160, 30};
static const unsigned short box_i[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 1, 5, 6, 1, 6, 2,
                                       2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0};
static Group box_g = {8, 12, box_v, box_i};
static VuGroup box_vu;

/* Mario's placeholder box in the VU1 layout (de-indexed, flat red with a fixed light), like tools/slice.py vu_groups */
static void build_box_vu(void)
{
    float *pos = mem_alloc(MEM_PLAYER, 36 * 16);
    u32 *col = mem_alloc(MEM_PLAYER, 36 * 16);
    const V3 L = vnorm(v3(0.35f, 0.75f, -0.55f));
    for (int t = 0; t < 12; t++) {
        const float *a = box_v + 3 * box_i[3 * t], *b = box_v + 3 * box_i[3 * t + 1], *c = box_v + 3 * box_i[3 * t + 2];
        V3 n = vnorm(vcross(v3(b[0] - a[0], b[1] - a[1], b[2] - a[2]), v3(c[0] - a[0], c[1] - a[1], c[2] - a[2])));
        float k = 0.25f + 0.75f * fabsf(vdot(n, L));
        for (int q = 0; q < 3; q++) {
            const float *v = box_v + 3 * box_i[3 * t + q];
            float *o = pos + 4 * (3 * t + q);
            o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; o[3] = 1.0f;
            u32 *cc = col + 4 * (3 * t + q);
            cc[0] = (u32)(0xE0 * k); cc[1] = (u32)(0x20 * k); cc[2] = (u32)(0x20 * k); cc[3] = 0x80;
        }
    }
    box_vu.nverts = 36; box_vu.pos = pos; box_vu.col = col;
}

void game_init(const Scene *s, const unsigned char *blob)
{
    sc = s;
    kcl = s->kcl;
    u32 gsz;
    const unsigned char *g = blob_chunk(blob, "GRAV", &gsz);
    ngrav = *(const int *)g;
    grav = (const Gravity *)(g + 4);
    m_coin = scene_model(s, "Coin");
    m_purple = scene_model(s, "PurpleCoin");
    m_piece = scene_model(s, "StarPiece");
    m_kuribo = scene_model(s, "Kuribo");
    m_planet = scene_model(s, "HeavensDoorMysteriousPlanet");
    m_mario.name = "Mario"; m_mario.ngroups = 1; m_mario.groups = &box_g; m_mario.radius = 170;
    build_box_vu();
    m_mario.vu_ngroups = 1; m_mario.vu = &box_vu;

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
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
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
    cam_pos = vadd(mario->pos, vadd(vmul(mario->up, 500), vmul(mario->face, -1000)));
    printf("game: coins %d, purple %d, star pieces %d, kuribo %d, start (%d %d %d)\n", ncoins, npurple, npieces, nkuribo,
           (int)start_pos.x, (int)start_pos.y, (int)start_pos.z);
}

/* --- update ---------------------------------------------------------------------------------------------- */

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
    if (vlen(want) > 0.1f) m->face = vnorm(want);
    m->hvel = vtangent(m->hvel, m->up);
    m->hvel = vadd(vmul(m->hvel, INERTIA), vmul(want, WALK_SPEED * (1.0f - INERTIA)));

    if (m->grounded && pad_pressed(PAD_CROSS)) { m->vup = JUMP_SPEED; m->grounded = 0; }
    if (pad_pressed(PAD_SQUARE) && m->spin_timer == 0) { /* spin: Wii shake */
        m->spin_timer = 30;
        if (!m->grounded && !m->spin_used) { if (m->vup < SPIN_LIFT) m->vup = SPIN_LIFT; m->spin_used = 1; }
    }
    if (m->spin_timer) m->spin_timer--;
    if (!m->grounded) { m->vup -= JUMP_GRAVITY; if (m->vup < -MAX_DROP) m->vup = -MAX_DROP; }

    V3 center = vadd(vadd(m->pos, vmul(m->up, BODY_RADIUS)), vadd(m->hvel, vmul(m->up, m->vup)));
    int ground = collide(&center, m->up, BODY_RADIUS);
    m->pos = vsub(center, vmul(m->up, BODY_RADIUS));
    if (ground && m->vup <= 0) { m->grounded = 1; m->vup = 0; m->spin_used = 0; }
    else if (!ground) m->grounded = 0;
    if (vlen(m->pos) > 9000.0f) { /* fell off: back to the start */
        m->pos = start_pos; m->hvel = v3(0, 0, 0); m->vup = 0;
    }
    if (m->hurt_timer) m->hurt_timer--;
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
    /* camera: behind and above Mario in his local frame, eased */
    V3 want = vadd(mario->pos, vadd(vmul(mario->up, 450.0f), vmul(mario->face, -900.0f)));
    cam_pos = vadd(cam_pos, vmul(vsub(want, cam_pos), 0.08f));
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
    float eye[3] = {cam_pos.x, cam_pos.y, cam_pos.z};
    V3 tgt = vadd(mario->pos, vmul(mario->up, 120.0f));
    float target[3] = {tgt.x, tgt.y, tgt.z}, up[3] = {mario->up.x, mario->up.y, mario->up.z};
    float m[16];
    render_begin(eye, target, up);
    mat_identity(m);
    if (m_planet) render_model(m_planet, m, 0x70, 0xB0, 0x60);
    draw_pickups(coins, ncoins, m_coin, 0xF0, 0xD0, 0x30);
    draw_pickups(purple, npurple, m_purple, 0xB0, 0x50, 0xF0);
    draw_pickups(pieces, npieces, m_piece, 0x60, 0xD0, 0xF0);
    for (int i = 0; i < nkuribo; i++) {
        if (kuribo[i].state == KURIBO_DEAD || !m_kuribo) continue;
        basis_matrix(m, kuribo[i].pos, kuribo[i].up, vsub(mario->pos, kuribo[i].pos), 0);
        render_model(m_kuribo, m, kuribo[i].state == KURIBO_STUN ? 0x50 : 0x90, 0x50, 0x20);
    }
    basis_matrix(m, mario->pos, mario->up, mario->face, 0);
    render_model(&m_mario, m, mario->hurt_timer & 4 ? 0xFF : 0xE0, 0x20, 0x20);
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
