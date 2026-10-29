#include "anim.h"
#include "mem.h"
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

/* Clips stay zlib-compressed in the blob (design decision 5) and are inflated on play into a few cache slots
 * (least recently started is replaced). Keys: J3DGetKeyFrameInterpolation / JMAHermiteInterpolation
 * (J3DAnimation.cpp:512-602): type 0 keys are (time, value, tangent), type 1 (time, value, in, out); a track with
 * count 0 is the default (scale 1, rotation 0, translation 0), count 1 a constant. Rotations are s16 << dec_shift,
 * 0x8000 = 180 degrees. Joint local = T * Rz * Ry * Rx * S. */

/* Inflated clips live in two pools, at their own sizes (most are a few KB: fixed slots of the largest clip's size
 * held 8 and thrashed, ~8 inflates a frame at the start of Good Egg); the least recently used go until a gap fits.
 * The main pool has the room of 8 of the largest clips, the tracks' pool (a blend group's tracks 1..3, Mario's
 * 基本 / 水泳基本: clips up to TSLOT_MAX) that of 6. */
#define SLOTS 8
#define TSLOTS 6
#define TSLOT_MAX 65536
#define CE_MAX 256

typedef struct { int id; u32 off, len, age; } ClipEnt;
typedef struct { u8 *mem; u32 size; int n; ClipEnt e[CE_MAX]; } ClipCache; /* entries sorted by offset */
static const Scene *sc;
static ClipCache cmain, ctrack;
static u32 age, slot_size;
u32 anim_inflates, anim_inflated; /* clips inflated, and their bytes (profiling) */
static short *info_frames;
static signed char *info_mode;

void anim_init(const Scene *s)
{
    sc = s;
    for (u32 i = 0; i < s->nclips; i++)
        if (s->clips[i].raw > slot_size)
            slot_size = s->clips[i].raw;
    slot_size = (slot_size + 15) & ~15u;
    cmain.size = SLOTS * slot_size;
    cmain.mem = mem_alloc(MEM_PLAYER, cmain.size);
    cmain.n = 0;
    u32 tslot_size = 0;
    for (u32 i = 0; i < s->nclips; i++)
        if (s->clips[i].raw > tslot_size && s->clips[i].raw <= TSLOT_MAX)
            tslot_size = s->clips[i].raw;
    tslot_size = (tslot_size + 15) & ~15u;
    ctrack.size = TSLOTS * tslot_size;
    ctrack.mem = ctrack.size ? mem_alloc(MEM_PLAYER, ctrack.size) : NULL;
    ctrack.n = 0;
    /* here, below the zone packs' arena mark (allocated at the first anim_clip_info, a zone swap released it) */
    info_frames = mem_alloc(MEM_PLAYER, s->nclips * sizeof(short));
    info_mode = mem_alloc(MEM_PLAYER, s->nclips);
    for (u32 i = 0; i < s->nclips; i++) info_frames[i] = -2;
}

u32 anim_cache_bytes(void) { return cmain.size; }

static const u8 *cache_get(ClipCache *c, int id)
{
    for (int i = 0; i < c->n; i++)
        if (c->e[i].id == id) {
            c->e[i].age = ++age;
            return c->mem + c->e[i].off;
        }
    u32 len = (sc->clips[id].raw + 15) & ~15u;
    if (len > c->size) return NULL;
    for (;;) {
        u32 at = 0;
        int k = 0;
        if (c->n < CE_MAX)
            for (; k <= c->n; k++) { /* the first gap that fits, before entry k */
                u32 end = k < c->n ? c->e[k].off : c->size;
                if (end - at >= len) break;
                if (k < c->n) at = c->e[k].off + c->e[k].len;
            }
        if (c->n < CE_MAX && k <= c->n) {
            uLongf zl = len;
            anim_inflates++; anim_inflated += sc->clips[id].raw;
            if (uncompress(c->mem + at, &zl, sc->clips[id].z, sc->clips[id].zsize) != Z_OK) {
                printf("anim: inflate failed for %s\n", sc->clips[id].name);
                return NULL;
            }
            memmove(&c->e[k + 1], &c->e[k], (c->n - k) * sizeof c->e[0]);
            c->e[k] = (ClipEnt){id, at, len, ++age};
            c->n++;
            if (info_frames[id] == -2) { info_frames[id] = *(const short *)(c->mem + at); info_mode[id] = (signed char)c->mem[at + 2]; }
            return c->mem + at;
        }
        int lru = 0; /* no gap: the least recently used goes */
        for (int i = 1; i < c->n; i++)
            if (c->e[i].age < c->e[lru].age) lru = i;
        memmove(&c->e[lru], &c->e[lru + 1], (c->n - lru - 1) * sizeof c->e[0]);
        c->n--;
    }
}

static const u8 *clip_data(int id) { return cache_get(&cmain, id); }

static const u8 *track_data(int id) /* clip_data for a group's tracks 1..3 (their own pool; a larger clip: the main one) */
{
    if (sc->clips[id].raw > TSLOT_MAX || !ctrack.mem) return clip_data(id);
    return cache_get(&ctrack, id);
}

static int clip_id_of(const char *name);
int anim_clip_info(const char *name, int *mode)
{
    int i = clip_id_of(name);
    if (i >= 0) {
            if (info_frames[i] == -2) {
                const u8 *c = clip_data(i);
                info_frames[i] = c ? *(const short *)c : -1;
                info_mode[i] = c ? (signed char)c[2] : 0;
            }
            if (mode) *mode = info_mode[i];
            return info_frames[i];
        }
    if (mode) *mode = 0;
    return -1;
}

int anim_start(Anim *a, const char *name, int interp)
{
    int mode;
    if (anim_clip_info(name, &mode) < 0) return 0;
    a->clip_id = -1;
    return anim_play(a, name, 1.0f, mode == 2 || mode == 4, interp);
}

void anim_bind(Anim *a, const Skin *skin)
{
    memset(a, 0, sizeof *a);
    a->skin = skin;
    a->clip_id = -1;
    for (int k = 0; k < ANIM_TRACKS; k++) a->tclip[k] = -1;
    a->phase = -1.0f;
    a->interp = 1.0f;
    anim_update(a);
}

/* clip names -> the first clip of that name: an open-addressed hash over the scene's clips, rebuilt when the list
 * grows (blob.c adds a pack's clips); more clips than it holds: the plain search */
#define CH_SIZE 4096
static short ch_tab[CH_SIZE];
static u32 ch_n;
static const Clip *ch_clips;
static u32 ch_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h;
}
static int clip_id_of(const char *name)
{
    if (!name) return -1;
    if (sc->nclips > CH_SIZE / 2) {
        for (u32 i = 0; i < sc->nclips; i++) if (!strcmp(sc->clips[i].name, name)) return (int)i;
        return -1;
    }
    if (ch_n != sc->nclips || ch_clips != sc->clips) {
        memset(ch_tab, 0xFF, sizeof ch_tab);
        for (u32 i = 0; i < sc->nclips; i++) {
            u32 h = ch_hash(sc->clips[i].name) & (CH_SIZE - 1);
            while (ch_tab[h] >= 0 && strcmp(sc->clips[ch_tab[h]].name, sc->clips[i].name)) h = (h + 1) & (CH_SIZE - 1);
            if (ch_tab[h] < 0) ch_tab[h] = (short)i; /* a repeated name keeps its first clip */
        }
        ch_n = sc->nclips;
        ch_clips = sc->clips;
    }
    for (u32 h = ch_hash(name) & (CH_SIZE - 1); ch_tab[h] >= 0; h = (h + 1) & (CH_SIZE - 1))
        if (!strcmp(sc->clips[ch_tab[h]].name, name)) return ch_tab[h];
    return -1;
}

void anim_blend(Anim *a, const char *clip, float w)
{
    int id = clip_id_of(clip);
    a->ntrack = id >= 0 ? 2 : 1;
    a->tclip[1] = id;
    a->tw[0] = 1.0f - w; a->tw[1] = w;
}

void anim_group(Anim *a, const char *const *clips, int n, const float *w, float speed, int interp)
{
    int mode = 0;
    anim_clip_info(clips[0], &mode);
    anim_play(a, clips[0], speed, mode == 2 || mode == 4 || n > 1, interp);
    a->ntrack = n > ANIM_TRACKS ? ANIM_TRACKS : n;
    for (int k = 1; k < a->ntrack; k++) a->tclip[k] = clip_id_of(clips[k]);
    anim_weights(a, w);
}

void anim_weights(Anim *a, const float *w)
{
    for (int k = 0; k < a->ntrack; k++) a->tw[k] = w[k];
}

static void local_mtx(const float q[4], const float t[3], const float s[3], float local[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    for (int i = 0; i < 16; i++) local[i] = i % 5 ? 0.0f : 1.0f;
    local[0] = (1 - 2 * (y * y + z * z)) * s[0]; local[1] = 2 * (x * y + z * w) * s[0]; local[2] = 2 * (x * z - y * w) * s[0];
    local[4] = 2 * (x * y - z * w) * s[1]; local[5] = (1 - 2 * (x * x + z * z)) * s[1]; local[6] = 2 * (y * z + x * w) * s[1];
    local[8] = 2 * (x * z + y * w) * s[2]; local[9] = 2 * (y * z - x * w) * s[2]; local[10] = (1 - 2 * (x * x + y * y)) * s[2];
    local[12] = t[0]; local[13] = t[1]; local[14] = t[2];
}

void anim_overlay(Anim *a, const Anim *up, int root)
{
    const Skin *k = a->skin;
    if (!k || up->skin != k) return;
    for (u32 j = 0; j < k->njoints && j < ANIM_MAX_JOINTS; j++) {
        int under = 0;
        for (int p = (int)j; p >= 0; p = k->joints[p].parent) if (p == root) { under = 1; break; }
        if (under) { memcpy(a->q[j], up->q[j], 16); memcpy(a->t[j], up->t[j], 12); memcpy(a->s[j], up->s[j], 12); }
        float local[16];
        local_mtx(a->q[j], a->t[j], a->s[j], local);
        if (k->joints[j].parent < 0) memcpy(a->world[j], local, 64);
        else mat_mul(a->world[j], a->world[k->joints[j].parent], local);
    }
}

void anim_track(Anim *a, int k, const char *clip)
{
    if (k <= 0 || k >= a->ntrack) return; /* (track 0 is the clip: anim_play) */
    int id = clip_id_of(clip);
    if (id >= 0) a->tclip[k] = id;
}

int anim_play(Anim *a, const char *name, float speed, int loop, int interp)
{
    int was_loop = a->loop;
    a->speed = speed;
    a->loop = loop;
    int i = clip_id_of(name);
    if (i >= 0) {
            if (i == a->clip_id)
                return 1;
            a->clip_id = i;
            a->ntrack = 1; /* (a group's tracks: anim_group after this) */
            /* loop to loop (walk <-> run): keep the step phase instead of restarting the cycle */
            a->phase = was_loop && loop && a->clip && anim_length(a) ? a->frame / anim_length(a) : -1.0f;
            a->frame = 0;
            a->done = 0;
            /* XanimePlayer::prepareAnimation: doFreeze, then the ratio starts at 0 */
            memcpy(a->fq, a->q, sizeof a->q);
            memcpy(a->ft, a->t, sizeof a->t);
            memcpy(a->fs, a->s, sizeof a->s);
            a->interp_left = interp;
            a->interp = interp > 0 ? 0.0f : 1.0f;
            return 1;
        }
    return 0;
}

static float clip_len(int id);
int anim_length(const Anim *a)
{
    return a->clip ? (int)clip_len(a->posed_id) : 0;
}

static float hermite(float f, float t0, float v0, float m0, float t1, float v1, float m1)
{
    float d = t1 - t0, t = (f - t0) / d, t2 = t * t;
    return v0 + t * d * m0 + t2 * (3 * (v1 - v0) - 2 * d * m0 - d * m1) + t2 * t * (2 * (v0 - v1) + d * m0 + d * m1);
}

/* the same key search for f32 and s16 tracks */
#define KEY_FN(NAME, T)                                                                                     \
    static float NAME(float f, u32 count, u32 type, const T *k)                                             \
    {                                                                                                       \
        if (count == 1)                                                                                     \
            return k[0];                                                                                    \
        u32 st = type == 0 ? 3 : 4, last = (count - 1) * st;                                                \
        if (f < k[0])                                                                                       \
            return k[1];                                                                                    \
        if (k[last] <= f)                                                                                   \
            return k[last + 1];                                                                             \
        u32 i = 0;                                                                                          \
        while (i + st <= last && k[i + st] <= f)                                                            \
            i += st;                                                                                        \
        return hermite(f, k[i], k[i + 1], type == 0 ? k[i + 2] : k[i + 3], k[i + st], k[i + st + 1], k[i + st + 2]); \
    }
KEY_FN(key_f, float)
KEY_FN(key_s, s16)

/* J3DAnmTextureSRTKey at `frame` for one VUMD group (tools/slice.py btk_entry) -> the UV matrix applied to its stored
 * UVs (columns s, t, 1 in qwords: render.c uploads it): S Mt M0^-1 S^-1, S the texture's UV scale, M0 the static
 * texture matrix baked into them, Mt J3DGetTextureMtx(Maya) of the animated scale U / V, rotation W, translation U / V
 * about the BTK's centre (tools/tex.py tex_mtx). The translation is kept in [0, 1): textures repeat. */
void anim_btk(const u8 *e, float frame, float uvm[12])
{
    u32 frames = *(const u16 *)(e + 2), loop = e[4], maya = e[5];
    const float *S = (const float *)(e + 8), *inv = (const float *)(e + 16), *c = (const float *)(e + 40);
    const u16 *tr = (const u16 *)(e + 48);
    const float *keys = (const float *)(e + 88);
    if (frames) {
        if (loop == 2) frame = fmodf(frame, (float)frames);
        else if (loop >= 3) { frame = fmodf(frame, 2.0f * frames); if (frame > frames) frame = 2.0f * frames - frame; }
        else if (frame > frames) frame = (float)frames;
    }
    float v[5];
    for (int i = 0; i < 5; i++)
        v[i] = key_f(frame, tr[4 * i], tr[4 * i + 1], keys + tr[4 * i + 2]);
    float sx = v[0], sy = v[1], r = v[2] * (3.14159265f / 180.0f), tx = v[3], ty = v[4];
    float cr = cosf(r), sr = sinf(r), m[2][3];
    if (maya) { /* J3DGetTextureMtxMaya */
        m[0][0] = sx * cr; m[0][1] = sy * sr; m[0][2] = (tx - 0.5f) * cr - sr * (ty - 0.5f + sy) + 0.5f;
        m[1][0] = -sx * sr; m[1][1] = sy * cr; m[1][2] = -(tx - 0.5f) * sr - cr * (ty - 0.5f + sy) + 0.5f;
    } else { /* J3DGetTextureMtx */
        m[0][0] = sx * cr; m[0][1] = -sx * sr; m[0][2] = -sx * cr * c[0] + sx * sr * c[1] + c[0] + tx;
        m[1][0] = sy * sr; m[1][1] = sy * cr; m[1][2] = -sy * sr * c[0] - sy * cr * c[1] + c[1] + ty;
    }
    for (int i = 0; i < 2; i++) {
        float a = m[i][0] * inv[0] + m[i][1] * inv[3], b = m[i][0] * inv[1] + m[i][1] * inv[4];
        float t = (m[i][0] * inv[2] + m[i][1] * inv[5] + m[i][2]) * S[i];
        uvm[i] = a * S[i] / S[0];     /* column s */
        uvm[4 + i] = b * S[i] / S[1]; /* column t */
        uvm[8 + i] = t - floorf(t);
    }
}

static void clip_local(const u8 *c, float f, u32 j, float scale[3], float rot[3], float trans[3])
{
    const u16 *h = (const u16 *)c;
    u32 shift = c[3], nj = h[2], ns = h[3], nr = h[4];
    const u16 *tab = h + 8;
    u32 o = 16 + nj * 54;
    o = (o + 3) & ~3u;
    const float *sd = (const float *)(c + o);
    o += 4 * ns;
    const s16 *rd = (const s16 *)(c + o);
    o += 2 * nr;
    o = (o + 3) & ~3u;
    const float *td = (const float *)(c + o);
    for (u32 ax = 0; ax < 3; ax++) {
        const u16 *e = tab + (3 * j + ax) * 9;
        scale[ax] = e[0] ? key_f(f, e[0], e[2], sd + e[1]) : 1.0f;
        rot[ax] = e[3] ? (float)((int)key_s(f, e[3], e[5], rd + e[4]) << shift) * (180.0f / 32768.0f) : 0.0f;
        trans[ax] = e[6] ? key_f(f, e[6], e[8], td + e[7]) : 0.0f;
    }
}

static inline u32 cy_(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
u32 prof_pose, prof_skin; /* EE cycles in anim_pose / anim_skin (profiling) */
void anim_update(Anim *a)
{
    u32 c0 = cy_();
    anim_pose(a);
    u32 c1 = cy_();
    anim_skin(a);
    prof_pose += c1 - c0; prof_skin += cy_() - c1;
}

/* a clip's frame_max without inflating it once known (info_frames) */
static float clip_len(int id)
{
    if (info_frames[id] == -2) {
        const u8 *c = clip_data(id);
        info_frames[id] = c ? *(const short *)c : -1;
        info_mode[id] = c ? (signed char)c[2] : 0;
    }
    return (float)(u16)info_frames[id];
}
float anim_rate = 1.0f; /* logic steps an update stands for (main's catch-up: draw-time updates after 2 or 3 steps) */
/* the frame and the cross-fade one step on, no pose (the model is not drawn: its timing goes on) */
void anim_advance(Anim *a)
{
    if (a->interp_left > 0) { /* XanimePlayer::updateInterpoleRatio */
        a->interp += (1.0f - a->interp) / a->interp_left;
        if (--a->interp_left == 0) a->interp = 1.0f;
    }
    if (a->clip_id >= 0 && info_frames[a->clip_id] != -1) {
        float len = clip_len(a->clip_id);
        if (a->phase >= 0) {
            a->frame = a->phase * len;
            a->phase = -1.0f;
        }
        a->frame += a->speed * anim_rate;
        if (a->frame >= len) {
            if (a->loop)
                a->frame = fmodf(a->frame, len);
            else {
                a->frame = len;
                a->done = 1;
            }
        }
    }
}

/* the half-angle sine / cosine of a joint rotation (degrees); most joints leave some axes at 0, where they are exact
 * (sinf keeps a zero's sign) and the libm calls are skipped */
#define COSH(d) ((d) == 0.0f ? 1.0f : cosf((d) * h))
#define SINH(d) ((d) == 0.0f ? (d) * h : sinf((d) * h))
void anim_pose(Anim *a)
{
    const Skin *k = a->skin;
    a->clip = a->clip_id >= 0 ? clip_data(a->clip_id) : NULL;
    a->posed_id = a->clip_id;
    if (a->interp_left > 0) { /* XanimePlayer::updateInterpoleRatio */
        a->interp += (1.0f - a->interp) / a->interp_left;
        if (--a->interp_left == 0) a->interp = 1.0f;
    }
    if (a->clip) {
        float len = *(const u16 *)a->clip;
        if (a->phase >= 0) {
            a->frame = a->phase * len;
            a->phase = -1.0f;
        }
        a->frame += a->speed * anim_rate;
        if (a->frame >= len) {
            if (a->loop)
                a->frame = fmodf(a->frame, len);
            else {
                a->frame = len;
                a->done = 1;
            }
        }
    }
    /* a group's tracks at the clip's phase (XanimeCore::updateFrame: mFrameRatio x each one's frame_max) */
    const u8 *tc[ANIM_TRACKS] = {a->clip};
    float tf[ANIM_TRACKS] = {a->frame}, tot = 0.0f;
    int nt = a->ntrack > 1 ? a->ntrack : 1;
    float ratio = a->clip && *(const u16 *)a->clip ? a->frame / *(const u16 *)a->clip : 0.0f;
    for (int k = 1; k < nt; k++) {
        tc[k] = a->tclip[k] >= 0 && a->tw[k] != 0.0f ? track_data(a->tclip[k]) : NULL;
        tf[k] = tc[k] ? ratio * *(const u16 *)tc[k] : 0.0f;
    }
    if (nt > 1) for (int k = 0; k < nt; k++) if (tc[k]) tot += a->tw[k];
    for (u32 j = 0; j < k->njoints && j < ANIM_MAX_JOINTS; j++) {
        const SkJoint *jt = &k->joints[j];
        float s[3], r[3], t[3], local[16] __attribute__((aligned(16)));
        if (a->clip && j < ((const u16 *)a->clip)[2])
            clip_local(a->clip, a->frame, j, s, r, t);
        else
            for (int i = 0; i < 3; i++) {
                s[i] = jt->scale[i];
                r[i] = jt->rot[i] * (180.0f / 3.14159265f);
                t[i] = jt->trans[i];
            }
        /* rotation as a quaternion: R = Rz * Ry * Rx -> q = qz * qy * qx */
        const float h = 3.14159265f / 360.0f;
        float cx = COSH(r[0]), sx = SINH(r[0]), cy = COSH(r[1]), sy = SINH(r[1]);
        float cz = COSH(r[2]), sz = SINH(r[2]);
        float q[4] = {cz * cy * sx - sz * sy * cx, cz * sy * cx + sz * cy * sx, sz * cy * cx - cz * sy * sx,
                      cz * cy * cx + sz * sy * sx}; /* x, y, z, w */
        if (nt > 1 && tot > 0.0f) { /* XanimeCore::calcBlend: scale / translation by the normalised weights, the rotation
                                     * lerped in turn by weight / the weights so far (JMAQuatLerp) */
            float S[3] = {0, 0, 0}, T[3] = {0, 0, 0}, Q[4] = {0, 0, 0, 1}, acc = 0.0f;
            for (int k = 0; k < nt; k++) {
                if (!tc[k] || a->tw[k] == 0.0f) continue;
                float sk[3], rk[3], tk[3];
                if (j < ((const u16 *)tc[k])[2]) clip_local(tc[k], tf[k], j, sk, rk, tk);
                else for (int i = 0; i < 3; i++) { sk[i] = jt->scale[i]; rk[i] = jt->rot[i] * (180.0f / 3.14159265f); tk[i] = jt->trans[i]; }
                float bx = COSH(rk[0]), ax_ = SINH(rk[0]), by = COSH(rk[1]), ay = SINH(rk[1]);
                float bz = COSH(rk[2]), az = SINH(rk[2]);
                float qk[4] = {bz * by * ax_ - az * ay * bx, bz * ay * bx + az * by * ax_, az * by * bx - bz * ay * ax_,
                               bz * by * bx + az * ay * ax_};
                float w = a->tw[k] / tot, d = 0, l = 0;
                for (int i = 0; i < 3; i++) { S[i] += sk[i] * w; T[i] += tk[i] * w; }
                acc += w;
                float u = w / acc;
                for (int i = 0; i < 4; i++) d += Q[i] * qk[i];
                float sgn = d < 0 ? -1.0f : 1.0f;
                for (int i = 0; i < 4; i++) { Q[i] = Q[i] * (1 - u) + qk[i] * u * sgn; l += Q[i] * Q[i]; }
                l = 1.0f / sqrtf(l);
                for (int i = 0; i < 4; i++) Q[i] *= l;
            }
            memcpy(q, Q, 16); memcpy(s, S, 12); memcpy(t, T, 12);
        }
        if (a->interp < 1.0f) { /* fade from the frozen pose: nlerp the rotation, lerp scale and translation */
            float w = a->interp, d = 0;
            for (int i = 0; i < 4; i++) d += q[i] * a->fq[j][i];
            float sgn = d < 0 ? -1.0f : 1.0f, l = 0;
            for (int i = 0; i < 4; i++) { q[i] = a->fq[j][i] * (1 - w) * sgn + q[i] * w; l += q[i] * q[i]; }
            l = 1.0f / sqrtf(l);
            for (int i = 0; i < 4; i++) q[i] *= l;
            for (int i = 0; i < 3; i++) {
                t[i] = a->ft[j][i] + (t[i] - a->ft[j][i]) * w;
                s[i] = a->fs[j][i] + (s[i] - a->fs[j][i]) * w;
            }
        }
        memcpy(a->q[j], q, 16);
        memcpy(a->t[j], t, 12);
        memcpy(a->s[j], s, 12);
        float x = q[0], y = q[1], z = q[2], w = q[3];
        mat_identity(local);
        local[0] = (1 - 2 * (y * y + z * z)) * s[0]; local[1] = 2 * (x * y + z * w) * s[0]; local[2] = 2 * (x * z - y * w) * s[0];
        local[4] = 2 * (x * y - z * w) * s[1]; local[5] = (1 - 2 * (x * x + z * z)) * s[1]; local[6] = 2 * (y * z + x * w) * s[1];
        local[8] = 2 * (x * z + y * w) * s[2]; local[9] = 2 * (y * z - x * w) * s[2]; local[10] = (1 - 2 * (x * x + y * y)) * s[2];
        local[12] = t[0]; local[13] = t[1]; local[14] = t[2];
        if (jt->parent < 0)
            memcpy(a->world[j], local, 64);
        else
            mat_mul(a->world[j], a->world[jt->parent], local);
    }
}

void anim_skin(Anim *a)
{
    const Skin *k = a->skin;
    for (u32 d = 0; d < k->ndrw && d < ANIM_MAX_DRW; d++) {
        const SkDrw *dr = &k->drw[d];
        if (!dr->count) {
            memcpy(a->drw[d], a->world[dr->first], 64);
            continue;
        }
        float *out = a->drw[d];
        memset(out, 0, 64);
        for (u32 i = 0; i < dr->count; i++) {
            const SkMix *mx = &k->mix[dr->first + i];
            float ib[16], m[16];
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++)
                    ib[c * 4 + r] = mx->invbind[r * 4 + c];
            ib[3] = ib[7] = ib[11] = 0.0f;
            ib[15] = 1.0f;
            mat_mul(m, a->world[mx->joint], ib);
            for (int q = 0; q < 16; q++)
                out[q] += mx->weight * m[q];
        }
    }
}
