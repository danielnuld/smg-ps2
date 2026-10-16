#include "fx.h"

#include "render.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A lean port of JParticle (Petari src/JSystem/JParticle): per resource one emitter (JPADynamicsBlock::create: rate,
 * rate step, max frame, start frame; volumes cube / sphere / cylinder / torus / point / circle / line), particles
 * (JPABaseParticle::init_p / calc_p: omni / axis / direction cone / random velocities, air resistance, gravity
 * fields, follow), drawn as billboards with JPAExtraShape's scale and alpha curves and the BSP1 colour keys.
 * ponytail: no child particles, rotation, texture animation, other field types, other shape types (all
 * billboards: stripes too), emitter local rotation; colour combine baked into the textures by the converter.
 * Joint-held effects (fx_named) take the joint's place and turn at the host's size. */

typedef struct {
    float rate, rate_rndm, life_rndm, vol_size, min_rad, sweep;
    float vel_omni, vel_axis, vel_dir, vel_rndm, spread, vel_ratio, air, moment_rndm;
    float trs[3], dir[3], scl[3], grav[3];
    float size[2];
    float sc_in_t, sc_out_t, sc_in_x, sc_out_x, sc_in_y, sc_out_y, sc_rndm;
    float al_in_t, al_out_t, al_in, al_base, al_out;
    s16 life, max_frame, start_frame, clr_max;
    u16 tex;
    u8 vol, add, rate_step, esp, follow, nkeys; /* esp: 1 scale curve, 2 alpha curve, 4 scale X / Y differ */
    u8 prm[4];
    s16 key_frame[8];
    u8 key_rgba[8][4];
} PtRes; /* 228 bytes: tools/jpa.py pack */

typedef struct { u16 n, res[7]; float off[3], pad; } FxDef; /* 32 bytes */

#define MAX_EMIT 256
#define MAX_PTCL 1536

typedef struct {
    const PtRes *r;
    const void *key;    /* fx_keep owner, NULL for a one-shot */
    float m[16];        /* emitter world matrix (the owner's) */
    float s;            /* its scale */
    float acc;          /* fractional particles carried to the next emission */
    u16 tick, wait, nptcl;
    u8 live, stop, kept, first;
} Emitter;

typedef struct {
    float loc[3], off[3], v1[3], v0[3];
    float scale, moment;
    u16 age, life, em;
} Ptcl;

static const PtRes *res;
static const FxDef *defs;
static u32 nres, ndefs, tex0;
/* EffectKeeper's animation-driven AutoEffectList rows (tools/jpa.py anim_effects): 72 bytes each */
typedef struct { char cls[24], clip[24]; u16 start, end, fx; s16 joint; float off[3], scale; } FxAuto;
static const FxAuto *autos;
static u32 nautos;
static u16 *auto_ix; /* autos by (cls, clip) (fx_anim's lookup) */
static int auto_cmp(const FxAuto *a, const FxAuto *b)
{
    int c = strcmp(a->cls, b->cls);
    return c ? c : strcmp(a->clip, b->clip);
}
/* its order against "cls/clip" (cls nc characters long) */
static int auto_key(const FxAuto *a, const char *cls, u32 nc, const char *clip)
{
    int c = strncmp(a->cls, cls, nc);
    if (c) return c;
    if (a->cls[nc]) return 1;
    return strcmp(a->clip, clip);
}
static int wp_tex = -1; /* the first WaterPlant texture (PTCL header word 2), -1 none */
static Emitter em[MAX_EMIT];
static Ptcl pt[MAX_PTCL];
static u16 freelist[MAX_PTCL], nfree;
static u16 alive[MAX_PTCL], nalive;
static Sprite spr[MAX_PTCL];
static u32 seed = 0x12345678;

static float rf(void) { seed = seed * 1103515245u + 12345u; return (seed >> 8) * (1.0f / 16777216.0f); } /* [0, 1) */
static float zp(void) { return rf() * 2.0f - 1.0f; }                                                       /* [-1, 1) */
static float zh(void) { return rf() - 0.5f; }                                                              /* [-0.5, 0.5) */

void fx_init(const Scene *s)
{
    memset(em, 0, sizeof em);
    nalive = 0;
    nfree = MAX_PTCL;
    for (int i = 0; i < MAX_PTCL; i++)
        freelist[i] = MAX_PTCL - 1 - i;
    res = NULL;
    if (!s->ptcl)
        return;
    nres = ((const u32 *)s->ptcl)[0];
    ndefs = ((const u32 *)s->ptcl)[1];
    wp_tex = ((const int *)s->ptcl)[2];
    defs = (const FxDef *)(s->ptcl + 16);
    res = (const PtRes *)(defs + ndefs);
    nautos = ((const u32 *)s->ptcl)[3];
    autos = (const FxAuto *)(res + nres);
    auto_ix = malloc(sizeof *auto_ix * (nautos + 1));
    for (u32 i = 0; auto_ix && i < nautos; i++) { /* insertion sort: a few thousand rows, once */
        u32 j = i;
        while (j > 0 && auto_cmp(&autos[auto_ix[j - 1]], &autos[i]) > 0) { auto_ix[j] = auto_ix[j - 1]; j--; }
        auto_ix[j] = (u16)i;
    }
    tex0 = s->ptcl_tex0;
    printf("fx: %u effects, %u particle resources\n", ndefs, nres);
}

/* m * v (rotation and scale only) */
static void rot(const float m[16], const float v[3], float out[3])
{
    for (int i = 0; i < 3; i++)
        out[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2];
}

static void norm_to(float v[3], float len)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float k = l > 1e-6f ? len / l : 0.0f;
    v[0] *= k; v[1] *= k; v[2] *= k;
}

/* the emitter's world position: its matrix applied to the resource's translation */
static void emitter_pos(const Emitter *e, float p[3])
{
    rot(e->m, e->r->trs, p);
    p[0] += e->m[12]; p[1] += e->m[13]; p[2] += e->m[14];
}

static void create(int ei)
{
    if (!nfree)
        return;
    Emitter *e = &em[ei];
    const PtRes *r = e->r;
    u16 pi = freelist[--nfree];
    Ptcl *p = &pt[pi];
    alive[nalive++] = pi;
    e->nptcl++;
    p->em = ei;
    /* volume (JPAVolume*): a position and the omni / axis directions */
    float vp[3] = {0, 0, 0}, omni[3], axis[3];
    float size = r->vol_size, th = r->sweep * zp() * 3.14159265f;
    float rad = size * (r->min_rad + rf() * (1.0f - r->min_rad));
    switch (r->vol) {
    case 0: vp[0] = size * zh(); vp[1] = size * zh(); vp[2] = size * zh(); break;                 /* cube */
    case 1: {                                                                                         /* sphere */
        float ph = zp() * 1.5707963f;
        vp[0] = rad * cosf(ph) * sinf(th); vp[1] = -rad * sinf(ph); vp[2] = rad * cosf(ph) * cosf(th);
        break;
    }
    case 2: vp[0] = rad * sinf(th); vp[1] = size * zp(); vp[2] = rad * cosf(th); break;            /* cylinder */
    case 3: vp[0] = size * sinf(th); vp[2] = size * cosf(th); break;                               /* torus (ring) */
    case 5: vp[0] = rad * sinf(th); vp[2] = rad * cosf(th); break;                                 /* circle */
    case 6: vp[2] = size * zh(); break;                                                            /* line */
    default: break;                                                                                /* point */
    }
    if (r->vol == 4) {
        omni[0] = zh(); omni[1] = zh(); omni[2] = zh();
    } else {
        omni[0] = vp[0] * r->scl[0]; omni[1] = vp[1] * r->scl[1]; omni[2] = vp[2] * r->scl[2];
    }
    axis[0] = omni[0]; axis[1] = 0; axis[2] = omni[2];
    norm_to(omni, r->vel_omni);
    norm_to(axis, r->vel_axis);
    /* direction cone (JPAGetYZRotateMtx(zp * 0x8000 * spread, random) after the direction matrix) */
    float d[3] = {0, 0, 0};
    if (r->vel_dir != 0.0f) {
        const float *D = r->dir;
        float u[3] = {D[1], -D[0], 0};
        if (fabsf(D[2]) > 0.9f) { u[0] = 0; u[1] = D[2]; u[2] = -D[1]; }
        norm_to(u, 1.0f);
        float v[3] = {D[1] * u[2] - D[2] * u[1], D[2] * u[0] - D[0] * u[2], D[0] * u[1] - D[1] * u[0]};
        float a = zp() * 3.14159265f * r->spread, b = zp() * 3.14159265f, sa = sinf(a);
        for (int i = 0; i < 3; i++)
            d[i] = r->vel_dir * (sa * (cosf(b) * u[i] + sinf(b) * v[i]) + cosf(a) * D[i]);
    }
    float k = 1.0f + zp() * r->vel_ratio, v1[3], lp[3];
    for (int i = 0; i < 3; i++) {
        v1[i] = k * (omni[i] + axis[i] + d[i] + r->vel_rndm * zh());
        lp[i] = vp[i] * r->scl[i];
    }
    /* into the owner's orientation (unit rotation: the scale applies to positions, JPA mPublicScale) */
    float m1[16];
    memcpy(m1, e->m, sizeof m1);
    for (int i = 0; i < 12; i++)
        m1[i] /= e->s;
    rot(m1, v1, p->v1);
    rot(m1, lp, p->loc);
    emitter_pos(e, p->off);
    p->v0[0] = p->v0[1] = p->v0[2] = 0;
    p->moment = 1.0f - r->moment_rndm * rf();
    p->life = (u16)((1.0f - r->life_rndm * rf()) * r->life);
    if (!p->life)
        p->life = 1;
    p->age = 0;
    p->scale = (r->esp & 1) ? 1.0f + zp() * r->sc_rndm : 1.0f;
}

static Emitter *new_emitter(const PtRes *r, const void *key, const float m[16], const float off[3])
{
    for (int i = 0; i < MAX_EMIT; i++) {
        Emitter *e = &em[i];
        if (e->live)
            continue;
        memset(e, 0, sizeof *e);
        e->r = r;
        e->key = key;
        memcpy(e->m, m, sizeof e->m);
        e->s = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        if (e->s < 1e-6f) e->s = 1.0f;
        float o[3];
        rot(m, off, o);
        e->m[12] += o[0]; e->m[13] += o[1]; e->m[14] += o[2];
        e->live = e->first = e->kept = 1;
        return e;
    }
    return NULL;
}

void fx_emit(int fx, const float m[16])
{
    if (!res || fx < 0 || fx >= (int)ndefs)
        return;
    const FxDef *d = &defs[fx];
    for (int i = 0; i < d->n; i++)
        new_emitter(&res[d->res[i]], NULL, m, d->off);
}

void fx_anim(const char *clip, float from, float to, const float m[16], const float (*world)[16], const void *key)
{
    const char *slash = strchr(clip, '/');
    if (!res || !slash) return;
    u32 nc = (u32)(slash - clip);
    if (!auto_ix) return;
    int lo = 0, hi = (int)nautos; /* the first row not before cls/clip */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (auto_key(&autos[auto_ix[mid]], clip, nc, slash + 1) < 0) lo = mid + 1; else hi = mid;
    }
    for (u32 k = (u32)lo; k < nautos && !auto_key(&autos[auto_ix[k]], clip, nc, slash + 1); k++) {
        u32 i = auto_ix[k];
        const FxAuto *a = &autos[i];
        if (a->start == 0xFFFF) continue;
        float st = a->start;
        int on;
        if (a->end != 0xFFFF) on = to >= st && to <= a->end;      /* kept from its start to its end frame */
        else if (to >= from) on = from < st && st <= to;           /* one shot as the clip passes its start */
        else on = st > from || st <= to;                           /* the clip went round (or restarted) */
        if (!on) continue;
        float e[16], j[16], o[16];
        if (a->joint >= 0) mat_mul(j, m, world[a->joint]);
        else memcpy(j, m, sizeof j);
        mat_identity(o);
        for (int k = 0; k < 3; k++) { o[5 * k] = a->scale; o[12 + k] = a->off[k]; }
        mat_mul(e, j, o);
        if (a->end != 0xFFFF) fx_keep(a->fx, (const char *)key + i, e);
        else fx_emit(a->fx, e);
    }
}

int fx_named(const char *cls, const char *name, const float m[16], const float (*world)[16], const void *key)
{
    char c[24], n[24];
    int k = 0, hit = 0;
    for (; cls[k] && k < 23; k++) c[k] = (char)tolower((unsigned char)cls[k]);
    c[k] = 0;
    for (k = 0; name[k] && k < 23; k++) n[k] = (char)tolower((unsigned char)name[k]);
    n[k] = 0;
    for (u32 i = 0; res && i < nautos; i++) {
        const FxAuto *a = &autos[i];
        if (a->start != 0xFFFF || strcmp(a->cls, c) || strcmp(a->clip, n)) continue;
        float e[16], j[16], o[16];
        if (a->joint >= 0 && world) {
            mat_mul(j, m, world[a->joint]);
            for (int c = 0; c < 3; c++) { /* the joint's place and turn; the size is the host's (m), not the bone's */
                float l = sqrtf(j[4 * c] * j[4 * c] + j[4 * c + 1] * j[4 * c + 1] + j[4 * c + 2] * j[4 * c + 2]);
                float h = sqrtf(m[4 * c] * m[4 * c] + m[4 * c + 1] * m[4 * c + 1] + m[4 * c + 2] * m[4 * c + 2]);
                if (l > 1e-6f) for (int r = 0; r < 3; r++) j[4 * c + r] *= h / l;
            }
        } else memcpy(j, m, sizeof j);
        mat_identity(o);
        for (int q = 0; q < 3; q++) { o[5 * q] = a->scale; o[12 + q] = a->off[q]; }
        mat_mul(e, j, o);
        if (key) fx_keep(a->fx, (const char *)key + i, e);
        else fx_emit(a->fx, e);
        hit = 1;
    }
    return hit;
}

void fx_keep(int fx, const void *key, const float m[16])
{
    if (!res || fx < 0 || fx >= (int)ndefs)
        return;
    const FxDef *d = &defs[fx];
    for (int i = 0; i < d->n; i++) {
        const PtRes *r = &res[d->res[i]];
        Emitter *e = NULL;
        for (int j = 0; j < MAX_EMIT && !e; j++)
            if (em[j].live && !em[j].stop && em[j].key == key && em[j].r == r)
                e = &em[j];
        if (e) {
            e->kept = 1;
            memcpy(e->m, m, 48);                       /* follow the owner (offset kept: m[12..14] below) */
            float o[3];
            rot(m, d->off, o);
            e->m[12] = m[12] + o[0]; e->m[13] = m[13] + o[1]; e->m[14] = m[14] + o[2];
        } else {
            new_emitter(r, key, m, d->off);
        }
    }
}

void fx_update(void)
{
    for (int i = 0; i < MAX_EMIT; i++) {
        Emitter *e = &em[i];
        if (!e->live)
            continue;
        const PtRes *r = e->r;
        if (e->key) {
            if (!e->kept)
                e->stop = 1;
            e->kept = 0;
        }
        if (e->wait < r->start_frame) {
            e->wait++;
            continue;
        }
        /* JPABaseEmitter::processTermination, JPADynamicsBlock::create */
        if (r->max_frame < 0 || (r->max_frame > 0 && e->tick >= r->max_frame))
            e->stop = 1;
        if (!e->stop && e->tick % (r->rate_step + 1) == 0) {
            float c = r->rate * (1.0f + r->rate_rndm * zp());
            e->acc += c;
            if (!(e->acc >= 0.0f && e->acc < 4096.0f)) /* a huge or broken rate: the loop below ran for billions */
                e->acc = 0.0f;
            int n = (int)e->acc;
            e->acc -= n;
            if (e->first && c > 0.0f && c < 1.0f)
                n = 1;
            if (n > (int)nfree)
                n = nfree; /* no more than the pool can hold */
            while (n-- > 0)
                create(i);
            e->first = 0;
        }
        e->tick++;
    }
    static float eg[MAX_EMIT][3]; /* each emitter's gravity through its matrix (unscaled), once a frame */
    for (int i = 0; i < MAX_EMIT; i++) {
        if (!em[i].live) continue;
        float m1[16];
        memcpy(m1, em[i].m, sizeof m1);
        for (int k = 0; k < 12; k++)
            m1[k] /= em[i].s;
        rot(m1, em[i].r->grav, eg[i]);
    }
    for (int i = 0; i < nalive;) {
        Ptcl *p = &pt[alive[i]];
        Emitter *e = &em[p->em];
        const PtRes *r = e->r;
        if (++p->age >= p->life) {
            e->nptcl--;
            freelist[nfree++] = alive[i];
            alive[i] = alive[--nalive];
            continue;
        }
        if (r->follow)
            emitter_pos(e, p->off);
        const float *g = eg[p->em];
        for (int k = 0; k < 3; k++) {
            p->v0[k] += g[k];
            p->v1[k] *= r->air;
            p->loc[k] += p->moment * (p->v1[k] + p->v0[k]);
        }
        i++;
    }
    for (int i = 0; i < MAX_EMIT; i++)
        if (em[i].live && em[i].stop && !em[i].nptcl)
            em[i].live = 0;
}

/* BSP1 colour keys at frame f (JPACalcClrIdxNormal: clamped to the last frame), linear between keys */
static void key_colour(const PtRes *r, int f, u8 out[4])
{
    if (f > r->clr_max) f = r->clr_max;
    int k = 0;
    while (k + 1 < r->nkeys && r->key_frame[k + 1] <= f)
        k++;
    if (k + 1 >= r->nkeys || r->key_frame[k + 1] == r->key_frame[k]) {
        memcpy(out, r->key_rgba[k], 4);
        return;
    }
    float t = (float)(f - r->key_frame[k]) / (r->key_frame[k + 1] - r->key_frame[k]);
    for (int c = 0; c < 4; c++)
        out[c] = (u8)(r->key_rgba[k][c] + t * (r->key_rgba[k + 1][c] - r->key_rgba[k][c]));
}

void fx_draw(void)
{
    int n = 0;
    for (int i = 0; i < nalive; i++) {
        const Ptcl *p = &pt[alive[i]];
        const Emitter *e = &em[p->em];
        const PtRes *r = e->r;
        if (r->tex == 0xFFFF)
            continue;
        float t = (float)p->age / p->life, sc = p->scale, a = 1.0f;
        if (r->esp & 1) { /* JPACalcScaleX */
            if (t < r->sc_in_t)
                sc *= (1.0f - r->sc_in_x) / r->sc_in_t * t + r->sc_in_x;
            else if (t > r->sc_out_t && r->sc_out_t != 1.0f)
                sc *= (r->sc_out_x - 1.0f) / (1.0f - r->sc_out_t) * (t - r->sc_out_t) + 1.0f;
        }
        if (r->esp & 2) { /* JPACalcAlphaAnm */
            if (t < r->al_in_t)
                a = r->al_in + (r->al_base - r->al_in) / r->al_in_t * t;
            else if (t > r->al_out_t && r->al_out_t != 1.0f)
                a = r->al_base + (r->al_out - r->al_base) / (1.0f - r->al_out_t) * (t - r->al_out_t);
            else
                a = r->al_base;
        }
        u8 c[4];
        if (r->nkeys)
            key_colour(r, p->age, c);
        else
            memcpy(c, r->prm, 4);
        float al = c[3] * a * (128.0f / 255.0f);
        if (al < 1.0f || sc <= 0.0f)
            continue;
        Sprite *s = &spr[n++];
        for (int k = 0; k < 3; k++)
            s->pos[k] = p->off[k] + p->loc[k] * e->s;
        s->half = 25.0f * r->size[0] * sc * e->s;
        s->rgba = (c[0] * 128 / 255) | (c[1] * 128 / 255) << 8 | (c[2] * 128 / 255) << 16 | (u32)(al > 128 ? 128 : al) << 24;
        s->tex = tex0 + r->tex;
        s->add = r->add;
    }
    render_sprites(spr, n);
}

u32 fx_count(void) { return nalive; }

int fx_waterplant_tex(int k) { return res && wp_tex >= 0 ? (int)tex0 + wp_tex + k : -1; }
int fx_plantstalk_tex(void) { return fx_waterplant_tex(4); }
int fx_dinofoot_tex(void) { return fx_waterplant_tex(5); }
int fx_ocean_tex(int k) { return fx_waterplant_tex(6 + k); }
